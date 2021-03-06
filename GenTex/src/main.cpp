#include <cstdlib>
#include <iostream>
#include <vector>
#include <algorithm>
#include <cmath>
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <memory>
#include "args.hxx"
#include "nlohmann/json.hpp"

#include "shape.h"
#include "vec3.h"

#define TINYOBJLOADER_IMPLEMENTATION
#include "./tiny_obj_loader.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

constexpr Object::Vertex rectangleVertex[] = {
    { 0.0f, 0.5f, 1.0f, 0.0f, 0.0f },
    { 0.5f, 0.0f, 0.0f, 0.0f, 0.8f },
    { 0.5f, 1.0f, 0.0f, 0.8f, 0.0f }
};

inline Vec3f Hsv2rgb(float h, float s, float v){
    Vec3f c = Vec3f(h, s, v);
    const Vec3f hue(h, h, h);
    const Vec3f K = Vec3f(1.0, 2.0 / 3.0, 1.0 / 3.0);
    const Vec3f Kx_ONE = Vec3f(1, 1, 1);
    const Vec3f Kw = Vec3f(3, 3, 3);
    Vec3f p = vabs(vfract(hue + K) * 6.0 - Kw);
    return c.z() * vmix(Kx_ONE, vclamp(p - Kx_ONE, 0.0f, 1.0f), c.y());
}

class Sphere {
public:
    Sphere() {}
    Sphere(Vec3f _center, float _r) {
        center = _center;
        r = _r;
    }

    void invert(Vec3f &pos, float &dr) {
        Vec3f diff = pos - center;
        float lenSq = vdot(diff, diff);
        float k = (r * r) / lenSq;
        dr *= k; // (r * r) / lenSq
        pos = diff * k + center;
    }

    float distance(const Vec3f pos) {
        return vdistance(pos, center) - r;
    }

    Vec3f center;
    float r;
};

class Plane {
public:
    Plane(Vec3f _origin, Vec3f _normal) {
        origin = _origin;
        normal = _normal;
    }

    float distance(const Vec3f pos) {
        return vdot(pos - origin, normal);
    }

    Vec3f origin;
    Vec3f normal;
};

class Sphairahedron {
public:
    Sphairahedron(Vec3f _bboxMin, Vec3f _bboxMax,
                  std::vector<Sphere> _spheres,
                  std::vector<Plane> _planes,
                  std::vector<Plane> _boundingPlanes,
                  std::vector<Plane> _dividePlanes,
                  std::vector<Sphere> _finiteSpheres,
                  std::vector<Sphere> _convexSpheres,
                  Sphere _boundingSphere) {
        bboxMin = _bboxMin;
        bboxMax = _bboxMax;
        spheres = _spheres;
        planes = _planes;
        boundingPlanes = _boundingPlanes;
        dividePlanes = _dividePlanes;
        finiteSpheres = _finiteSpheres;
        convexSpheres = _convexSpheres;
        boundingSphere = _boundingSphere;
    }

    Vec3f bboxMin;
    Vec3f bboxMax;
    std::vector<Plane> boundingPlanes;
    std::vector<Sphere> spheres;
    std::vector<Plane> planes;
    std::vector<Plane> dividePlanes;

    std::vector<Sphere> finiteSpheres;
    std::vector<Sphere> convexSpheres;
    Sphere boundingSphere;

    std::vector<GLint> uniLocations;

    void getUniLocations(GLint program) {
        uniLocations = std::vector<GLint>();
        uniLocations.push_back(glGetUniformLocation(program, "u_numPrismSpheres"));
        std::string si;
        for (int i = 0; i < spheres.size(); i++) {
            si = "u_prismSpheres["+ std::to_string(i) +"].center";
            uniLocations.push_back(glGetUniformLocation(program, si.c_str()));
            si = "u_prismSpheres["+ std::to_string(i) +"].r";
            uniLocations.push_back(glGetUniformLocation(program, si.c_str()));
        }

        uniLocations.push_back(glGetUniformLocation(program, "u_numPrismPlanes"));
        for (int i = 0; i < planes.size(); i++) {
            si = "u_prismPlanes["+ std::to_string(i) +"].origin";
            uniLocations.push_back(glGetUniformLocation(program, si.c_str()));
            si = "u_prismPlanes["+ std::to_string(i) +"].normal";
            uniLocations.push_back(glGetUniformLocation(program, si.c_str()));
        }

        uniLocations.push_back(glGetUniformLocation(program, "u_numFiniteSpheres"));
        for (int i = 0; i < finiteSpheres.size(); i++) {
            si = "u_finiteSpheres["+ std::to_string(i) +"].center";
            uniLocations.push_back(glGetUniformLocation(program, si.c_str()));
            si = "u_finiteSpheres["+ std::to_string(i) +"].r";
            uniLocations.push_back(glGetUniformLocation(program, si.c_str()));
        }

        si = "u_boundingSphere.center";
        uniLocations.push_back(glGetUniformLocation(program, si.c_str()));
        si = "u_boundingSphere.r";
        uniLocations.push_back(glGetUniformLocation(program, si.c_str()));

        uniLocations.push_back(glGetUniformLocation(program, "u_bboxMin"));
        uniLocations.push_back(glGetUniformLocation(program, "u_isFinite"));
    }

    void setUniformValues(bool isFinite) {
        int index = 0;
        glUniform1i(uniLocations[index++], spheres.size());
        for (int i = 0; i < spheres.size(); i++) {
            glUniform3f(uniLocations[index++],
                        spheres[i].center[0],
                        spheres[i].center[1],
                        spheres[i].center[2]);
            glUniform2f(uniLocations[index++],
                        spheres[i].r,
                        spheres[i].r * spheres[i].r);
        }

        glUniform1i(uniLocations[index++], planes.size());
        for (int i = 0; i < planes.size(); i++) {
            glUniform3f(uniLocations[index++],
                        planes[i].origin[0],
                        planes[i].origin[1],
                        planes[i].origin[2]);
            glUniform3f(uniLocations[index++],
                        planes[i].normal[0],
                        planes[i].normal[1],
                        planes[i].normal[2]);
        }

        glUniform1i(uniLocations[index++], finiteSpheres.size());
        for (int i = 0; i < finiteSpheres.size(); i++) {
            glUniform3f(uniLocations[index++],
                        finiteSpheres[i].center[0],
                        finiteSpheres[i].center[1],
                        finiteSpheres[i].center[2]);
            glUniform2f(uniLocations[index++],
                        finiteSpheres[i].r,
                        finiteSpheres[i].r * finiteSpheres[i].r);
        }

        glUniform3f(uniLocations[index++],
                    boundingSphere.center[0],
                    boundingSphere.center[1],
                    boundingSphere.center[2]);
        glUniform2f(uniLocations[index++],
                    boundingSphere.r,
                    boundingSphere.r * boundingSphere.r);

        glUniform3f(uniLocations[index++], bboxMin[0], bboxMin[1], bboxMin[2]);
        glUniform1i(uniLocations[index++], isFinite);
    }

    int iisInfSphairahedron(Vec3f pos) {
        int invNum = 0;
        float dr = 1.0;
        for(int n = 0; n < MAX_IIS_ITER_COUNT; n++) {
            bool inFund = true;
            std::for_each(spheres.begin(), spheres.end(),
                          [&](Sphere s){
                              if (vdistance(pos, s.center) < s.r) {
                                  s.invert(pos, dr);
                                  invNum++;
                                  inFund = false;
                              }
                          });
            std::for_each(planes.begin(), planes.end(),
                          [&](Plane p){
                              pos = pos - p.origin;
                              float d = vdot(pos, p.normal);
                              if (d > 0.) {
                                  pos = pos - 2.f * d * p.normal;
                                  invNum++;
                                  inFund = false;
                              }
                              pos = pos + p.origin;
                          });
            if (inFund) break;
        }

        return invNum;
    }

    inline Vec3f computeColor (Vec3f pos) {
        int invNum = iisInfSphairahedron(pos);
        if (invNum == -1) {
            return Vec3f(0, 0, 0);
        }
        return Hsv2rgb((float(invNum) * 0.01), 1., 1.);
    }

private:
    int MAX_IIS_ITER_COUNT = 100;
};

Sphairahedron CreateSphairahedronFromJson(nlohmann::json jsonObj) {
    Vec3f bboxMin(jsonObj["bboxMin"][0], jsonObj["bboxMin"][1], jsonObj["bboxMin"][2]);
    Vec3f bboxMax(jsonObj["bboxMax"][0], jsonObj["bboxMax"][1], jsonObj["bboxMax"][2]);

    std::vector<Sphere> spheres;
    for (auto data: jsonObj["prismSpheres"]) {
        spheres.push_back(Sphere(Vec3f(data["center"][0],
                                       data["center"][1],
                                       data["center"][2]),
                                 data["r"].get<float>()));
    }

    std::vector<Sphere> finiteSpheres;
    for (auto data: jsonObj["finiteSpheres"]) {
        finiteSpheres.push_back(Sphere(Vec3f(data["center"][0],
                                             data["center"][1],
                                             data["center"][2]),
                                       data["r"].get<float>()));
    }

    Sphere boundingSphere(Vec3f(jsonObj["boundingSphere"]["center"][0],
                                jsonObj["boundingSphere"]["center"][1],
                                jsonObj["boundingSphere"]["center"][2]),
                          jsonObj["boundingSphere"]["r"].get<float>());

    std::vector<Sphere> convexSpheres;
    for (auto data: jsonObj["convexSpheres"]) {
        convexSpheres.push_back(Sphere(Vec3f(data["center"][0],
                                             data["center"][1],
                                             data["center"][2]),
                                       data["r"].get<float>()));
    }

    std::vector<Plane> planes;
    for (auto data: jsonObj["prismPlanes"]) {
        planes.push_back(Plane(Vec3f(data["p1"][0],
                                     data["p1"][1],
                                     data["p1"][2]),
                               Vec3f(data["normal"][0],
                                     data["normal"][1],
                                     data["normal"][2])));
    }

    std::vector<Plane> boundingPlanes;
    for (auto data: jsonObj["boundingPlanes"]) {
        Vec3f origin(data["p1"][0],
                     data["p1"][1],
                     data["p1"][2]);
        Vec3f normal(data["normal"][0],
                     data["normal"][1],
                     data["normal"][2]);
        boundingPlanes.push_back(Plane(origin, normal));
    }

    std::vector<Plane> dividePlanes;
    for (auto data: jsonObj["dividePlanes"]) {
        dividePlanes.push_back(Plane(Vec3f(data["p1"][0],
                                           data["p1"][1],
                                           data["p1"][2]),
                                     Vec3f(data["normal"][0],
                                           data["normal"][1],
                                           data["normal"][2])));
    }

    std::cout << "bbox min (" << bboxMin.x() << ", "
              << bboxMin.y() << ", "
              << bboxMin.z()  << ") "<< std::endl;
    std::cout << "bbox max (" << bboxMax.x() << ", "
              << bboxMax.y() << ", "
              << bboxMax.z()  << ") "<< std::endl;
    std::cout << "number of prism spheres " << spheres.size() << std::endl;
    std::cout << "number of prism planes " << planes.size() << std::endl;

    return Sphairahedron(bboxMin, bboxMax,
                         spheres, planes,
                         boundingPlanes,
                         dividePlanes,
                         finiteSpheres,
                         convexSpheres,
                         boundingSphere);
}

// LoadShader and LinkShader functions are from exrview
// https://github.com/syoyo/tinyexr/tree/master/examples/exrview
bool LoadShader(
    GLenum shaderType,  // GL_VERTEX_SHADER or GL_FRAGMENT_SHADER (or maybe GL_COMPUTE_SHADER)
    GLuint& shader,
    const char* shaderSourceFilename)
{
    GLint val = 0;

    // free old shader/program
    if (shader != 0) glDeleteShader(shader);

    static GLchar srcbuf[16384];
    FILE *fp = fopen(shaderSourceFilename, "rb");
    if (!fp) {
        fprintf(stderr, "failed to load shader: %s\n", shaderSourceFilename);
        return false;
    }
    fseek(fp, 0, SEEK_END);
    size_t len = ftell(fp);
    rewind(fp);
    len = fread(srcbuf, 1, len, fp);
    srcbuf[len] = 0;
    fclose(fp);

    static const GLchar *src = srcbuf;

    shader = glCreateShader(shaderType);
    glShaderSource(shader, 1, &src, NULL);
    glCompileShader(shader);
    glGetShaderiv(shader, GL_COMPILE_STATUS, &val);
    if (val != GL_TRUE) {
        char log[4096];
        GLsizei msglen;
        glGetShaderInfoLog(shader, 4096, &msglen, log);
        printf("%s\n", log);
        assert(val == GL_TRUE && "failed to compile shader");
    }

    printf("Load shader [ %s ] OK\n", shaderSourceFilename);
    return true;
}

bool LinkShader(
    GLuint& prog,
    GLuint& vertShader,
    GLuint& fragShader)
{
    GLint val = 0;

    if (prog != 0) {
        glDeleteProgram(prog);
    }

    prog = glCreateProgram();

    glAttachShader(prog, vertShader);
    glAttachShader(prog, fragShader);
    glLinkProgram(prog);

    glGetProgramiv(prog, GL_LINK_STATUS, &val);
    assert(val == GL_TRUE && "failed to link shader");

    printf("Link shader OK\n");

    return true;
}

std::vector<Object::Vertex> LoadObj(std::string objFilename,
                                    Sphairahedron sphairahedron) {
    std::vector<Object::Vertex>  vertexList;

    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;

    std::string err;

    std::cout << "Load " << objFilename << std::endl;

    bool ret = tinyobj::LoadObj(&attrib, &shapes, &materials, &err, objFilename.c_str());

    if (!err.empty()) { // `err` may contain warning message.
        std::cerr << err << std::endl;
    }

    if (!ret) {
        std::cerr << "Failed to load " << objFilename << std::endl;
        exit(1);
    }

    printf("# of vertices  = %d\n", (int) (attrib.vertices.size()) / 3);
    printf("# of normals   = %d\n", (int) (attrib.normals.size()) / 3);
    printf("# of texcoords = %d\n", (int) (attrib.texcoords.size()) / 2);
    printf("# of materials = %d\n", (int) materials.size());
    printf("# of shapes    = %d\n", (int) shapes.size());
    printf("\n");

    Vec3f vMin(99999., 99999., 99999.);
    Vec3f vMax(-99999., -99999., -99999.);

    for (size_t s = 0; s < shapes.size(); s++) {
        printf("Shape %d\n", (int) shapes.size());
        printf("# of faces %d\n", (int) shapes[s].mesh.num_face_vertices.size());
        size_t index_offset = 0;
        for (size_t f = 0; f < shapes[s].mesh.num_face_vertices.size(); f++) {
            // The number of vertexes in the face. 3, 4 or 5?
            int fv = shapes[s].mesh.num_face_vertices[f];
            if(fv > 3) {
                printf("quad!!");
                continue;
            }

            Vec3f uvMin(2, 0, 2);
            Vec3f uvMax(-1, 0, -1);
            // Loop over vertices in the face.
            for (int v = 0; v < fv; v++) {
                // access to vertex
                tinyobj::index_t idx = shapes[s].mesh.indices[index_offset + v];
                tinyobj::real_t vx = attrib.vertices[3*idx.vertex_index+0];
                tinyobj::real_t vy = attrib.vertices[3*idx.vertex_index+1];
                tinyobj::real_t vz = attrib.vertices[3*idx.vertex_index+2];
                tinyobj::real_t nx = attrib.normals[3*idx.normal_index+0];
                tinyobj::real_t ny = attrib.normals[3*idx.normal_index+1];
                tinyobj::real_t nz = attrib.normals[3*idx.normal_index+2];
                tinyobj::real_t tx = attrib.texcoords[2*idx.texcoord_index+0];
                tinyobj::real_t ty = attrib.texcoords[2*idx.texcoord_index+1];
                uvMin[0] = std::min(tx, uvMin[0]);
                uvMin[2] = std::min(ty, uvMin[2]);
                uvMax[0] = std::max(tx, uvMax[0]);
                uvMax[2] = std::max(ty, uvMax[2]);
                vertexList.push_back({tx, ty, vx, vy, vz});

                vMin[0] = std::min(vx, vMin[0]);
                vMin[1] = std::min(vy, vMin[1]);
                vMin[2] = std::min(vz, vMin[2]);

                vMax[0] = std::max(vx, vMax[0]);
                vMax[1] = std::max(vy, vMax[1]);
                vMax[2] = std::max(vz, vMax[2]);
            }
            index_offset += fv;
        }
    }

    printf("min %f %f %f\n", vMin[0], vMin[1], vMin[2]);
    printf("max %f %f %f\n", vMax[0], vMax[1], vMax[2]);

    return vertexList;
}

int main(int argc, char** argv) {
    args::ArgumentParser parser("Generate mesh.");
    args::ValueFlag<std::string> inputObj(parser, "obj", "Input obj file",
                                          {'i', "input"}, "scene.obj");
    args::ValueFlag<std::string> inputJson(parser, "json", "Input json file",
                                           {'j', "json"}, "scene.json");
    args::ValueFlag<std::string> outputBasenameArg(parser, "outputBasename",
                                                   "Base name of output files (.png)",
                                                   {'o', "out"}, "texture");
    args::ValueFlag<int> sizeArg(parser, "textureSize",
                                 "texture size (1024)",
                                 {'s', "size"}, 1024);
    args::Flag isFinite(parser, "isFinite", "Generate finite limit set", {'f', "finite"});
    args::HelpFlag help(parser, "help", "Display this help menu", {'h', "help"});

    try {
        parser.ParseCLI(argc, argv);
    } catch (args::Help) {
        std::cout << parser;
        return 0;
    } catch (args::ParseError e) {
        std::cerr << e.what() << std::endl;
        std::cerr << parser;
        return 1;
    } catch (args::ValidationError e) {
        std::cerr << e.what() << std::endl;
        std::cerr << parser;
        return 1;
    }

    std::string inputJsonFileName = args::get(inputJson);
    std::ifstream ifs(inputJsonFileName);
    nlohmann::json jsonObj;
    if (!ifs) {
        std::cout << "Can't open " << inputJsonFileName << std::endl;
        return 1;
    }
    ifs >> jsonObj;
    ifs.close();
    std::cerr << "create" << std::endl;
    Sphairahedron sphairahedron = CreateSphairahedronFromJson(jsonObj);
    std::cerr << "Load json" << std::endl;
    std::vector<Object::Vertex> vertexList = LoadObj(args::get(inputObj), sphairahedron);
    std::cerr << "done" << std::endl;
    GLFWwindow* window;

    /* Initialize the library */
    if (!glfwInit())
        return -1;

    atexit(glfwTerminate);

    int windowWidth = args::get(sizeArg);
    int windowHeight = args::get(sizeArg);

    /* Create a windowed mode window and its OpenGL context */
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_SAMPLES, 10);
    glEnable(GL_MULTISAMPLE);
    // glfwWindowHint( GLFW_VISIBLE, 0 );
    window = glfwCreateWindow(windowWidth, windowHeight,
                              "Hello World", NULL, NULL);
    if (!window)
    {
        return -1;
    }

    /* Make the window's context current */
    glfwMakeContextCurrent(window);

    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK)
    {
        std::cerr << "Can't initialize GLEW" << std::endl;
        return 1;
    }


    GLuint vert_id = 0, frag_id = 0, prog_id = 0;
    {
        bool ret = LoadShader(GL_VERTEX_SHADER, vert_id, "./src/shader.vert");
        if (!ret) {
            fprintf(stderr, "Failed to compile vertex shader.\n");
            exit(-1);
        }
    }

    {
        bool ret = LoadShader(GL_FRAGMENT_SHADER, frag_id, "./src/shader.frag");
        if (!ret) {
            fprintf(stderr, "Failed to compile fragment shader.\n");
            exit(-1);
        }
    }

    {
        bool ret = LinkShader(prog_id, vert_id, frag_id);
        if (!ret) {
            fprintf(stderr, "Failed to link shader.\n");
            exit(-1);
        }
    }

    glBindAttribLocation(prog_id, 0, "uv");
    glBindAttribLocation(prog_id, 1, "position");
    glBindFragDataLocation(prog_id, 0, "fragment");
    glUseProgram(prog_id);

    glfwSwapInterval(1);
    glClearColor(0.f, 0.f, 0.f, 1.f);

//    std::unique_ptr<const Shape> shape(new Shape(2, 3,
//                                                 rectangleVertex));
    std::unique_ptr<const Shape> shape(new Shape(2, vertexList.size(),
                                                 &vertexList[0]));

    sphairahedron.getUniLocations(prog_id);
    sphairahedron.setUniformValues(isFinite);

    /* Loop until the user closes the window */
    while (!glfwWindowShouldClose(window))
    {
        /* Render here */
        glClear(GL_COLOR_BUFFER_BIT);
        shape->draw();

        /* Swap front and back buffers */
        glfwSwapBuffers(window);

        /* Poll for and process events */
        glfwPollEvents();
    }

    glReadBuffer( GL_BACK );
    unsigned char *textureData = new unsigned char[windowWidth * windowHeight * 3];
    std::fill(textureData, textureData + windowWidth * windowHeight * 3, 0);

    glReadPixels(
        0,
        0,
        windowWidth,
        windowHeight,
        GL_RGB,
        GL_UNSIGNED_BYTE,
        textureData
	);

    std::string filename = args::get(outputBasenameArg) + ".png";
    stbi_write_png(filename.c_str(), windowWidth, windowHeight,
                   3, textureData,
                   0);
    delete[] textureData;

    return 0;
}
