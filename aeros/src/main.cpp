#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include <windows.h>
#include <commdlg.h>

#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <limits>
#include <thread>
#include <chrono>
#include <cmath>
#include <algorithm>
#include <queue>

#include "flow_params.h"

extern "C" float runCudaTest(const std::vector<float>& vertices);
extern "C" void initParticlesCUDA(std::vector<float>& positions, std::vector<float>& colors,
                                  int numParticles, const FlowParams& params);
extern "C" void updateParticlesCUDA(std::vector<float>& positions, std::vector<float>& colors,
                                    int numParticles, const FlowParams& params, float dt);
extern "C" void computeVertexPressureCUDA(const std::vector<float>& vertices,
                                          const std::vector<float>& normals,
                                          std::vector<float>& outColors,
                                          int numVertices, const FlowParams& params);
extern "C" void setVoxelData(const int* voxel, const float* dist,
                             int nx, int ny, int nz,
                             float mnX, float mnY, float mnZ,
                             float csX, float csY, float csZ);

const unsigned int SCR_WIDTH  = 1280;
const unsigned int SCR_HEIGHT = 720;

int display_w = SCR_WIDTH;
int display_h = SCR_HEIGHT;

glm::vec3 cameraPos   = glm::vec3(0.0f, 0.0f, 5.0f);
glm::vec3 cameraFront = glm::vec3(0.0f, 0.0f, -1.0f);
glm::vec3 cameraUp    = glm::vec3(0.0f, 1.0f, 0.0f);

float yaw   = -90.0f;
float pitch =  0.0f;
float fov   =  45.0f;

float lastX = SCR_WIDTH / 2.0f;
float lastY = SCR_HEIGHT / 2.0f;
bool firstMouse = true;

float deltaTime = 0.0f;
float lastFrame = 0.0f;

bool showModel       = true;
bool showBoundingBox = false;
bool showAxes        = true;
bool lightingEnabled = true;
bool showParticles   = true;
bool showStreamlines = true;
bool showPressure    = false;
bool showLiftDrag    = true;
bool showObstacle    = false;

glm::vec3 bgColor        = glm::vec3(0.05f, 0.06f, 0.09f);
glm::vec3 bboxColor      = glm::vec3(0.3f, 0.3f, 0.3f);
glm::vec3 obstacleColor  = glm::vec3(0.2f, 0.5f, 0.9f);
float obstacleAlpha    = 0.12f;
float streamlineAlpha  = 0.85f;
float streamlineWidth  = 1.5f;

bool  vsyncEnabled = true;
bool  limitFPS     = false;
float maxFPS       = 60.0f;
float cameraSpeedMultiplier = 1.0f;
float mouseSensitivity = 0.3f;

int useCUDA = 1;

float flowSpeed     = 2.0f;
float flowAzimuth   = 0.0f;
float flowElevation = 0.0f;
float timeScale     = 1.0f;
float strouhal      = 0.2f;
float wakeStrength  = 0.4f;
float wakeLength    = 8.0f;

int   numParticles     = 15000;
float particleSize     = 2.0f;
float maxSpeedForColor = 5.0f;
unsigned int particleVAO = 0;
unsigned int particleVBO_pos = 0, particleVBO_col = 0;
std::vector<float> particlePositions;
std::vector<float> particleColors;
// Сколько частиц реально аллоцировано в буферах/векторах.
// Слайдер Count меняет numParticles, но буферы перевыделяются только в initParticles().
int particleDrawCount = 0;

int   numStreamlines     = 24;
int   streamlineSteps    = 300;
float streamlineStepSize = 0.08f;
unsigned int streamlineVAO = 0, streamlineVBO = 0;
int streamlineVertexCount  = 0;

unsigned int modelVAO = 0, modelVBO_vertices = 0, modelVBO_normals = 0, modelVBO_colors = 0;
int modelVertexCount = 0;
std::vector<float> g_vertices;
std::vector<float> g_normals;
std::vector<float> g_vertexColors;
glm::vec3 modelColor = glm::vec3(0.75f, 0.75f, 0.78f);

unsigned int bboxVAO = 0, bboxVBO = 0;
unsigned int axesVAO = 0, axesVBO = 0;

unsigned int obstacleVAO = 0, obstacleVBO = 0, obstacleEBO = 0;
int obstacleIndexCount = 0;

glm::vec3 liftVector(0.0f);
glm::vec3 dragVector(0.0f);
float liftMagnitude = 0.0f;
float dragMagnitude = 0.0f;
glm::vec3 centerOfPressure(0.0f);
unsigned int liftDragVAO = 0, liftDragVBO = 0;
bool liftDragDirty = true;

glm::vec3 minBB, maxBB, center;
float maxDim = 1.0f;

FlowParams flowParams;

std::vector<int>   g_voxelData;
std::vector<float> g_distanceField;
int   g_voxNx = 0, g_voxNy = 0, g_voxNz = 0;
float g_voxMinX = 0, g_voxMinY = 0, g_voxMinZ = 0;
float g_voxMaxX = 0, g_voxMaxY = 0, g_voxMaxZ = 0;
int   voxelResolution = 48;
bool  useVoxelCollision = true;

// =====================================================
// Callbacks
// =====================================================
void framebuffer_size_callback(GLFWwindow* window, int width, int height) {
    glViewport(0, 0, width, height);
    display_w = width;
    display_h = height;
}

void mouse_callback(GLFWwindow* window, double xpos, double ypos) {
    if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_MIDDLE) != GLFW_PRESS) {
        lastX = (float)xpos;
        lastY = (float)ypos;
        return;
    }
    float xoffset = (float)xpos - lastX;
    float yoffset = lastY - (float)ypos;
    lastX = (float)xpos;
    lastY = (float)ypos;

    xoffset *= mouseSensitivity;
    yoffset *= mouseSensitivity;

    yaw   += xoffset;
    pitch += yoffset;
    if (pitch > 89.0f)  pitch = 89.0f;
    if (pitch < -89.0f) pitch = -89.0f;

    glm::vec3 front;
    front.x = cosf(glm::radians(yaw)) * cosf(glm::radians(pitch));
    front.y = sinf(glm::radians(pitch));
    front.z = sinf(glm::radians(yaw)) * cosf(glm::radians(pitch));
    cameraFront = glm::normalize(front);
}

void scroll_callback(GLFWwindow* window, double xoffset, double yoffset) {
    fov -= (float)yoffset;
    if (fov < 1.0f)  fov = 1.0f;
    if (fov > 90.0f) fov = 90.0f;
}

void processInput(GLFWwindow* window) {
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
        glfwSetWindowShouldClose(window, true);

    float cameraSpeed = 10.0f * deltaTime * cameraSpeedMultiplier;
    if (glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS ||
        glfwGetKey(window, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS)
        cameraSpeed *= 5.0f;

    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) cameraPos += cameraSpeed * cameraFront;
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) cameraPos -= cameraSpeed * cameraFront;
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) cameraPos -= glm::normalize(glm::cross(cameraFront, cameraUp)) * cameraSpeed;
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) cameraPos += glm::normalize(glm::cross(cameraFront, cameraUp)) * cameraSpeed;
    if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS) cameraPos += cameraSpeed * cameraUp;
    if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS) cameraPos -= cameraSpeed * cameraUp;
}

// =====================================================
// Шейдеры
// =====================================================
const char* vertexShaderSource = R"(
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec3 aColor;
uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
out vec3 FragPos;
out vec3 Normal;
out vec3 VColor;
void main() {
    FragPos = vec3(model * vec4(aPos, 1.0));
    Normal = mat3(transpose(inverse(model))) * aNormal;
    VColor = aColor;
    gl_Position = projection * view * vec4(FragPos, 1.0);
}
)";

const char* fragmentShaderSource = R"(
#version 330 core
out vec4 FragColor;
in vec3 FragPos;
in vec3 Normal;
in vec3 VColor;
uniform vec3 lightPos;
uniform vec3 viewPos;
uniform vec3 lightColor;
uniform vec3 objectColor;
uniform bool useLighting;
uniform bool useVertexColor;
uniform float alpha;
void main() {
    vec3 base = useVertexColor ? VColor : objectColor;
    if (!useLighting) {
        FragColor = vec4(base, alpha);
        return;
    }
    float ambientStrength = 0.35;
    vec3 ambient = ambientStrength * lightColor;
    vec3 norm = normalize(Normal);
    vec3 lightDir = normalize(lightPos - FragPos);
    float diff = max(dot(norm, lightDir), 0.0);
    vec3 diffuse = diff * lightColor;
    float specularStrength = 0.3;
    vec3 viewDir = normalize(viewPos - FragPos);
    vec3 reflectDir = reflect(-lightDir, norm);
    float spec = pow(max(dot(viewDir, reflectDir), 0.0), 32);
    vec3 specular = specularStrength * spec * lightColor;
    vec3 lit = ambient + diffuse + specular;
    vec3 result;
    if (useVertexColor) {
        result = base * (0.7f + 0.3f * diff);
    } else {
        result = lit * base;
    }
    FragColor = vec4(result, alpha);
}
)";

const char* particleVertexShaderSource = R"(
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aColor;
uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
out vec3 vColor;
void main() {
    vColor = aColor;
    gl_Position = projection * view * model * vec4(aPos, 1.0);
}
)";

const char* particleFragmentShaderSource = R"(
#version 330 core
in vec3 vColor;
out vec4 FragColor;
void main() {
    FragColor = vec4(vColor, 1.0);
}
)";

const char* lineVertexShaderSource = R"(
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aColor;
uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
uniform bool useVertexColor;
out vec3 vColor;
void main() {
    vColor = useVertexColor ? aColor : vec3(1.0);
    gl_Position = projection * view * model * vec4(aPos, 1.0);
}
)";

const char* lineFragmentShaderSource = R"(
#version 330 core
in vec3 vColor;
uniform vec3 lineColor;
uniform bool useVertexColor;
uniform float alpha;
out vec4 FragColor;
void main() {
    vec3 c = useVertexColor ? vColor : lineColor;
    FragColor = vec4(c, alpha);
}
)";

// =====================================================
// STL loader
// =====================================================
bool loadSTL(const std::string& filename, std::vector<float>& vertices, std::vector<float>& normals) {
    std::ifstream file(filename, std::ios::binary);
    if (!file.is_open()) return false;

    // Формат определяем по размеру файла: бинарный STL имеет размер ровно
    // 84 + 50 * кол-во_треугольников байт. Всё остальное пробуем парсить как ASCII.
    // (Проверка по слову "solid" ненадёжна: бинарные файлы тоже могут начинаться с "solid".)
    file.seekg(0, std::ios::end);
    std::streamoff fileSize = file.tellg();
    file.seekg(80, std::ios::beg);
    uint32_t nt = 0;
    if (fileSize >= 84)
        file.read(reinterpret_cast<char*>(&nt), sizeof(nt));
    bool isBinary = (fileSize >= 84) && (fileSize == 84 + (std::streamoff)nt * 50);

    if (isBinary) {
        file.clear(); file.seekg(84, std::ios::beg);
        for (uint32_t i = 0; i < nt; i++) {
            float nx, ny, nz;
            file.read(reinterpret_cast<char*>(&nx), sizeof(float));
            file.read(reinterpret_cast<char*>(&ny), sizeof(float));
            file.read(reinterpret_cast<char*>(&nz), sizeof(float));
            for (int v = 0; v < 3; v++) {
                float x, y, z;
                file.read(reinterpret_cast<char*>(&x), sizeof(float));
                file.read(reinterpret_cast<char*>(&y), sizeof(float));
                file.read(reinterpret_cast<char*>(&z), sizeof(float));
                vertices.push_back(x); vertices.push_back(y); vertices.push_back(z);
                normals.push_back(nx);  normals.push_back(ny);  normals.push_back(nz);
            }
            file.ignore(2);
        }
        file.close();
        return !vertices.empty();
    }

    // ASCII STL
    file.clear(); file.seekg(0);
    std::string line;
    std::vector<glm::vec3> tv, tn;
    glm::vec3 cn(0.0f);
    while (std::getline(file, line)) {
        std::istringstream iss(line);
        std::string t; iss >> t;
        if (t == "facet") { std::string nk; iss >> nk; iss >> cn.x >> cn.y >> cn.z; }
        else if (t == "vertex") { glm::vec3 v; iss >> v.x >> v.y >> v.z; tv.push_back(v); tn.push_back(cn); }
    }
    for (size_t i = 0; i + 2 < tv.size(); i += 3) {
        glm::vec3 v0 = tv[i], v1 = tv[i+1], v2 = tv[i+2];
        glm::vec3 n = tn[i];
        if (glm::length(n) < 0.0001f) n = glm::normalize(glm::cross(v1-v0, v2-v0));
        for (int k = 0; k < 3; k++) {
            glm::vec3 v = tv[i+k];
            vertices.push_back(v.x); vertices.push_back(v.y); vertices.push_back(v.z);
            normals.push_back(n.x);  normals.push_back(n.y);  normals.push_back(n.z);
        }
    }
    file.close();
    return !vertices.empty();
}

// =====================================================
// BBox, оси
// =====================================================
void createBoundingBoxVAO() {
    if (bboxVAO == 0) glGenVertexArrays(1, &bboxVAO);
    if (bboxVBO == 0) glGenBuffers(1, &bboxVBO);
    glm::vec3 bmin = minBB, bmax = maxBB;
    float v[] = {
        bmin.x,bmin.y,bmin.z, bmax.x,bmin.y,bmin.z,
        bmax.x,bmin.y,bmin.z, bmax.x,bmax.y,bmin.z,
        bmax.x,bmax.y,bmin.z, bmin.x,bmax.y,bmin.z,
        bmin.x,bmax.y,bmin.z, bmin.x,bmin.y,bmin.z,
        bmin.x,bmin.y,bmax.z, bmax.x,bmin.y,bmax.z,
        bmax.x,bmin.y,bmax.z, bmax.x,bmax.y,bmax.z,
        bmax.x,bmax.y,bmax.z, bmin.x,bmax.y,bmax.z,
        bmin.x,bmax.y,bmax.z, bmin.x,bmin.y,bmax.z,
        bmin.x,bmin.y,bmin.z, bmin.x,bmin.y,bmax.z,
        bmax.x,bmin.y,bmin.z, bmax.x,bmin.y,bmax.z,
        bmax.x,bmax.y,bmin.z, bmax.x,bmax.y,bmax.z,
        bmin.x,bmax.y,bmin.z, bmin.x,bmax.y,bmax.z
    };
    glBindVertexArray(bboxVAO);
    glBindBuffer(GL_ARRAY_BUFFER, bboxVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(v), v, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3*sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);
}

void createAxesVAO(float size) {
    if (axesVAO == 0) glGenVertexArrays(1, &axesVAO);
    if (axesVBO == 0) glGenBuffers(1, &axesVBO);
    float v[] = {
        0,0,0, size,0,0,
        0,0,0, 0,size,0,
        0,0,0, 0,0,size
    };
    glBindVertexArray(axesVAO);
    glBindBuffer(GL_ARRAY_BUFFER, axesVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(v), v, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3*sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);
}

// =====================================================
// Эллипсоид
// =====================================================
void createObstacleSphere(int stacks = 48, int slices = 48) {
    std::vector<float> v;
    std::vector<unsigned int> idx;
    for (int i = 0; i <= stacks; ++i) {
        float phi = glm::pi<float>() * (float)i / stacks;
        float y = cosf(phi), r = sinf(phi);
        for (int j = 0; j <= slices; ++j) {
            float theta = 2.0f * glm::pi<float>() * (float)j / slices;
            float x = r * cosf(theta);
            float z = r * sinf(theta);
            v.push_back(x); v.push_back(y); v.push_back(z);
            v.push_back(x); v.push_back(y); v.push_back(z);
        }
    }
    for (int i = 0; i < stacks; ++i)
        for (int j = 0; j < slices; ++j) {
            int a = i*(slices+1)+j;
            int b = a + slices + 1;
            idx.push_back(a); idx.push_back(b); idx.push_back(a+1);
            idx.push_back(a+1); idx.push_back(b); idx.push_back(b+1);
        }
    obstacleIndexCount = (int)idx.size();
    glGenVertexArrays(1, &obstacleVAO);
    glGenBuffers(1, &obstacleVBO);
    glGenBuffers(1, &obstacleEBO);
    glBindVertexArray(obstacleVAO);
    glBindBuffer(GL_ARRAY_BUFFER, obstacleVBO);
    glBufferData(GL_ARRAY_BUFFER, v.size()*sizeof(float), v.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, obstacleEBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, idx.size()*sizeof(unsigned int), idx.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6*sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6*sizeof(float), (void*)(3*sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);
}

// =====================================================
// FlowParams
// =====================================================
void updateFlowParams() {
    flowParams.centerX = center.x;
    flowParams.centerY = center.y;
    flowParams.centerZ = center.z;

    flowParams.radiusX = (maxBB.x - minBB.x) * 0.5f;
    flowParams.radiusY = (maxBB.y - minBB.y) * 0.5f;
    flowParams.radiusZ = (maxBB.z - minBB.z) * 0.5f;
    if (flowParams.radiusX < 0.001f) flowParams.radiusX = 0.1f;
    if (flowParams.radiusY < 0.001f) flowParams.radiusY = 0.1f;
    if (flowParams.radiusZ < 0.001f) flowParams.radiusZ = 0.1f;

    float az = glm::radians(flowAzimuth);
    float el = glm::radians(flowElevation);
    glm::vec3 dir(cosf(el) * cosf(az), sinf(el), cosf(el) * sinf(az));
    dir = glm::normalize(dir);

    flowParams.vx = dir.x * flowSpeed;
    flowParams.vy = dir.y * flowSpeed;
    flowParams.vz = dir.z * flowSpeed;

    flowParams.time      = (float)glfwGetTime();
    flowParams.timeScale = timeScale;
    flowParams.strouhal  = strouhal;
    flowParams.wakeStrength = wakeStrength;
    flowParams.wakeLength   = wakeLength;

    float margin = 0.5f * maxDim;
    flowParams.minX = minBB.x - margin;
    flowParams.maxX = maxBB.x + margin;
    flowParams.minY = minBB.y - margin;
    flowParams.maxY = maxBB.y + margin;
    flowParams.minZ = minBB.z - margin;
    flowParams.maxZ = maxBB.z + margin;

    flowParams.maxSpeed = maxSpeedForColor;

    flowParams.gridNx = g_voxNx;
    flowParams.gridNy = g_voxNy;
    flowParams.gridNz = g_voxNz;
    flowParams.gridMinX = g_voxMinX;
    flowParams.gridMinY = g_voxMinY;
    flowParams.gridMinZ = g_voxMinZ;
    flowParams.gridMaxX = g_voxMaxX;
    flowParams.gridMaxY = g_voxMaxY;
    flowParams.gridMaxZ = g_voxMaxZ;
    flowParams.cellSizeX = (g_voxMaxX - g_voxMinX) / fmaxf((float)g_voxNx, 1.0f);
    flowParams.cellSizeY = (g_voxMaxY - g_voxMinY) / fmaxf((float)g_voxNy, 1.0f);
    flowParams.cellSizeZ = (g_voxMaxZ - g_voxMinZ) / fmaxf((float)g_voxNz, 1.0f);
    flowParams.gridCellCount = g_voxNx * g_voxNy * g_voxNz;
}

// =====================================================
// SDF sampling (CPU)
// =====================================================
float sampleSDFCPU(const glm::vec3& p) {
    if (g_distanceField.empty() || g_voxNx <= 0) return 1000.0f;
    int ix = (int)((p.x - g_voxMinX) / flowParams.cellSizeX);
    int iy = (int)((p.y - g_voxMinY) / flowParams.cellSizeY);
    int iz = (int)((p.z - g_voxMinZ) / flowParams.cellSizeZ);
    if (ix < 0 || ix >= g_voxNx || iy < 0 || iy >= g_voxNy || iz < 0 || iz >= g_voxNz)
        return 1000.0f;
    // Поле хранится в ВОКСЕЛЯХ (шаг BFS = 1), переводим в мировые единицы,
    // иначе зоны влияния зависят от разрешения сетки.
    return g_distanceField[(iz * g_voxNy + iy) * g_voxNx + ix] * flowParams.cellSizeX;
}

glm::vec3 sdfNormalCPU(const glm::vec3& p) {
    if (g_distanceField.empty()) return glm::vec3(0,1,0);
    int ix = (int)((p.x - g_voxMinX) / flowParams.cellSizeX);
    int iy = (int)((p.y - g_voxMinY) / flowParams.cellSizeY);
    int iz = (int)((p.z - g_voxMinZ) / flowParams.cellSizeZ);
    if (ix <= 0 || ix >= g_voxNx-1 || iy <= 0 || iy >= g_voxNy-1 || iz <= 0 || iz >= g_voxNz-1)
        return glm::vec3(0,1,0);
    float dx = g_distanceField[(iz*g_voxNy+iy)*g_voxNx + (ix+1)]
             - g_distanceField[(iz*g_voxNy+iy)*g_voxNx + (ix-1)];
    float dy = g_distanceField[(iz*g_voxNy+(iy+1))*g_voxNx + ix]
             - g_distanceField[(iz*g_voxNy+(iy-1))*g_voxNx + ix];
    float dz = g_distanceField[((iz+1)*g_voxNy+iy)*g_voxNx + ix]
             - g_distanceField[((iz-1)*g_voxNy+iy)*g_voxNx + ix];
    glm::vec3 n(dx, dy, dz);
    float len = glm::length(n);
    if (len < 1e-6f) return glm::vec3(0,1,0);
    return n / len;
}

// =====================================================
// Поле скоростей CPU (усиленный boost)
// =====================================================
glm::vec3 computeVelocityFieldCPU(const glm::vec3& p, const FlowParams& prm) {
    glm::vec3 v(prm.vx, prm.vy, prm.vz);

    if (!g_distanceField.empty()) {
        float d = sampleSDFCPU(p);
        if (d > 0.0f && d < 100.0f) {
            glm::vec3 n = sdfNormalCPU(p);
            float k = 3.0f * prm.cellSizeX;
            float factor = expf(-d / k);
            if (factor > 1e-4f) {
                float vn = glm::dot(v, n);
                v -= factor * vn * n;

                if (d < 8.0f * prm.cellSizeX) {
                    float boost = expf(-d / (k * 2.0f)) * 0.7f;
                    v += boost * v;
                }
            }
        }
    }

    float vmag = sqrtf(prm.vx*prm.vx + prm.vy*prm.vy + prm.vz*prm.vz);
    if (vmag > 1e-4f) {
        float dx = prm.vx/vmag, dy = prm.vy/vmag, dz = prm.vz/vmag;
        float rx = p.x - prm.centerX, ry = p.y - prm.centerY, rz = p.z - prm.centerZ;
        float along = rx*dx + ry*dy + rz*dz;
        float px = rx - along*dx, py = ry - along*dy, pz = rz - along*dz;
        float perp = sqrtf(px*px + py*py + pz*pz);
        if (perp < 1e-4f) perp = 1e-4f;

        float D = 2.0f * fmaxf(prm.radiusY, prm.radiusZ);
        if (along > D*0.5f && along < prm.wakeLength) {
            float decay = expf(-(along - D*0.5f) / (prm.wakeLength * 0.4f));
            float width = expf(-perp*perp / (D*D*1.5f));
            float pnx=px/perp, pny=py/perp, pnz=pz/perp;

            float vtx = dy*pnz - dz*pny;
            float vty = dz*pnx - dx*pnz;
            float vtz = dx*pny - dy*pnx;

            float omega = 6.2831853f * prm.strouhal * vmag / D;
            float phase = omega * prm.time - along * 2.0f;
            float amp = prm.wakeStrength * decay * width * vmag;

            v.x += amp * sinf(phase) * vtx;
            v.y += amp * sinf(phase) * vty;
            v.z += amp * sinf(phase) * vtz;

            float lat = amp * 0.5f;
            v.x += lat * cosf(phase) * pnx;
            v.y += lat * cosf(phase) * pny;
            v.z += lat * cosf(phase) * pnz;

            float turb = 0.3f * amp * sinf(prm.time*3.0f + along*3.0f + perp*5.0f);
            v.x += turb; v.y += turb*0.5f; v.z += turb*0.5f;
        }
    }
    return v;
}

glm::vec3 colorForPoint(const glm::vec3& v, float sdfDist, const FlowParams& prm) {
    float speed = glm::length(v);
    float spdT = glm::clamp(speed / (prm.maxSpeed + 1e-6f), 0.0f, 1.0f);
    float cell = prm.cellSizeX;

    if (sdfDist < 1.5f * cell) {
        return glm::vec3(1.0f, 0.2f, 0.0f);
    } else if (sdfDist < 4.0f * cell) {
        float b = glm::clamp((sdfDist - 1.5f * cell) / (2.5f * cell), 0.0f, 1.0f);
        glm::vec3 hot(1.0f, 0.5f, 0.0f);
        glm::vec3 cold;
        if (spdT < 0.5f) cold = glm::vec3(1.0f, spdT*2.0f, 0.0f);
        else             cold = glm::vec3(1.0f-(spdT-0.5f)*2.0f, 1.0f, 0.0f);
        return hot * (1.0f - b) + cold * b;
    } else {
        if (spdT < 0.5f) return glm::vec3(1.0f, spdT*2.0f, 0.0f);
        else             return glm::vec3(1.0f-(spdT-0.5f)*2.0f, 1.0f, 0.0f);
    }
}

// =====================================================
// Вокселизация
// =====================================================
static bool rayTri(const glm::vec3& orig, const glm::vec3& dir,
                   const glm::vec3& v0, const glm::vec3& v1, const glm::vec3& v2) {
    glm::vec3 e1 = v1-v0, e2 = v2-v0;
    glm::vec3 pv = glm::cross(dir, e2);
    float det = glm::dot(e1, pv);
    if (fabsf(det) < 1e-8f) return false;
    float inv = 1.0f/det;
    glm::vec3 tv = orig - v0;
    float u = glm::dot(tv, pv)*inv;
    if (u < 0.0f || u > 1.0f) return false;
    glm::vec3 qv = glm::cross(tv, e1);
    float v = glm::dot(dir, qv)*inv;
    if (v < 0.0f || u+v > 1.0f) return false;
    float t = glm::dot(e2, qv)*inv;
    return t > 1e-6f;
}

static bool insideMesh(const glm::vec3& p, const std::vector<float>& verts) {
    int hits = 0;
    glm::vec3 dir(1,0,0);
    for (size_t i = 0; i + 8 < verts.size(); i += 9) {
        glm::vec3 v0(verts[i],   verts[i+1], verts[i+2]);
        glm::vec3 v1(verts[i+3], verts[i+4], verts[i+5]);
        glm::vec3 v2(verts[i+6], verts[i+7], verts[i+8]);
        if (rayTri(p, dir, v0, v1, v2)) hits++;
    }
    return (hits % 2) == 1;
}

void buildVoxelGrid(const std::vector<float>& verts, int res) {
    std::cout << "Voxelizing at resolution " << res << "..." << std::endl;
    auto t0 = std::chrono::high_resolution_clock::now();

    float margin = 0.1f * maxDim;
    g_voxMinX = minBB.x - margin; g_voxMaxX = maxBB.x + margin;
    g_voxMinY = minBB.y - margin; g_voxMaxY = maxBB.y + margin;
    g_voxMinZ = minBB.z - margin; g_voxMaxZ = maxBB.z + margin;

    float sizeX = g_voxMaxX - g_voxMinX;
    float sizeY = g_voxMaxY - g_voxMinY;
    float sizeZ = g_voxMaxZ - g_voxMinZ;
    float m = fmaxf(sizeX, fmaxf(sizeY, sizeZ));

    g_voxNx = res;
    g_voxNy = (int)(res * sizeY / m); if (g_voxNy < 4) g_voxNy = 4;
    g_voxNz = (int)(res * sizeZ / m); if (g_voxNz < 4) g_voxNz = 4;

    float csx = sizeX / g_voxNx;
    float csy = sizeY / g_voxNy;
    float csz = sizeZ / g_voxNz;

    int total = g_voxNx * g_voxNy * g_voxNz;
    g_voxelData.assign(total, 0);

    for (int k = 0; k < g_voxNz; k++)
        for (int j = 0; j < g_voxNy; j++)
            for (int i = 0; i < g_voxNx; i++) {
                glm::vec3 p(g_voxMinX + (i+0.5f)*csx,
                            g_voxMinY + (j+0.5f)*csy,
                            g_voxMinZ + (k+0.5f)*csz);
                if (insideMesh(p, verts))
                    g_voxelData[(k*g_voxNy + j)*g_voxNx + i] = 1;
            }

    g_distanceField.assign(total, 1000.0f);
    std::vector<int> q;
    const int off[6][3] = {{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};

    for (int k = 0; k < g_voxNz; k++)
        for (int j = 0; j < g_voxNy; j++)
            for (int i = 0; i < g_voxNx; i++) {
                int idx = (k*g_voxNy + j)*g_voxNx + i;
                bool here = g_voxelData[idx] == 1;
                bool isSurf = false;
                for (auto& o : off) {
                    int ni=i+o[0], nj=j+o[1], nk=k+o[2];
                    if (ni<0||ni>=g_voxNx||nj<0||nj>=g_voxNy||nk<0||nk>=g_voxNz) continue;
                    int nidx = (nk*g_voxNy + nj)*g_voxNx + ni;
                    if ((g_voxelData[nidx]==1) != here) { isSurf = true; break; }
                }
                if (isSurf) {
                    g_distanceField[idx] = here ? -0.5f : 0.5f;
                    q.push_back(idx);
                }
            }

    size_t head = 0;
    while (head < q.size()) {
        int idx = q[head++];
        int i = idx % g_voxNx;
        int j = (idx / g_voxNx) % g_voxNy;
        int k = idx / (g_voxNx * g_voxNy);
        float d = g_distanceField[idx];
        for (auto& o : off) {
            int ni=i+o[0], nj=j+o[1], nk=k+o[2];
            if (ni<0||ni>=g_voxNx||nj<0||nj>=g_voxNy||nk<0||nk>=g_voxNz) continue;
            int nidx = (nk*g_voxNy + nj)*g_voxNx + ni;
            float nd = (d<0) ? (d-1.0f) : (d+1.0f);
            if (fabsf(nd) < fabsf(g_distanceField[nidx]) - 0.01f) {
                g_distanceField[nidx] = nd;
                q.push_back(nidx);
            }
        }
    }

    setVoxelData(g_voxelData.data(), g_distanceField.data(),
                 g_voxNx, g_voxNy, g_voxNz,
                 g_voxMinX, g_voxMinY, g_voxMinZ,
                 csx, csy, csz);

    auto t1 = std::chrono::high_resolution_clock::now();
    double ms = std::chrono::duration<double, std::milli>(t1-t0).count();
    std::cout << "Voxelized: " << g_voxNx << "x" << g_voxNy << "x" << g_voxNz
              << " in " << ms << " ms" << std::endl;
}

// =====================================================
// Частицы
// =====================================================
void initParticles() {
    updateFlowParams();
    particleDrawCount = numParticles;
    if (useCUDA == 1) {
        initParticlesCUDA(particlePositions, particleColors, numParticles, flowParams);
    } else {
        particlePositions.resize(numParticles*3);
        particleColors.resize(numParticles*3);
        float z = flowParams.minZ + 0.1f * (flowParams.maxZ - flowParams.minZ);
        int nSide = (int)sqrtf((float)numParticles) + 1;
        for (int i = 0; i < numParticles; i++) {
            float r1 = fabsf(sinf(i*12.9898f + 78.233f) * 43758.5453f); r1 -= floorf(r1);
            float r2 = fabsf(cosf(i*39.346f + 11.135f) * 24634.6345f); r2 -= floorf(r2);
            int ix = i % nSide, iy = i / nSide;
            float stX = (flowParams.maxX - flowParams.minX) / nSide;
            float stY = (flowParams.maxY - flowParams.minY) / nSide;
            particlePositions[3*i]   = flowParams.minX + ix*stX + (r1-0.5f)*stX*0.5f;
            particlePositions[3*i+1] = flowParams.minY + iy*stY + (r2-0.5f)*stY*0.5f;
            particlePositions[3*i+2] = z;
            particleColors[3*i] = 0.3f;
            particleColors[3*i+1] = 0.8f;
            particleColors[3*i+2] = 1.0f;
        }
    }
    if (particleVAO == 0) glGenVertexArrays(1, &particleVAO);
    if (particleVBO_pos == 0) glGenBuffers(1, &particleVBO_pos);
    if (particleVBO_col == 0) glGenBuffers(1, &particleVBO_col);
    glBindVertexArray(particleVAO);
    glBindBuffer(GL_ARRAY_BUFFER, particleVBO_pos);
    glBufferData(GL_ARRAY_BUFFER, particlePositions.size()*sizeof(float), particlePositions.data(), GL_DYNAMIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3*sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, particleVBO_col);
    glBufferData(GL_ARRAY_BUFFER, particleColors.size()*sizeof(float), particleColors.data(), GL_DYNAMIC_DRAW);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 3*sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);
}

void updateParticles(float dt) {
    updateFlowParams();
    // Работаем только с аллоцированным количеством: слайдер Count меняет
    // numParticles, а перевыделение происходит в initParticles() (по отпусканию слайдера).
    const int n = particleDrawCount;
    if (n <= 0) return;
    if (useCUDA == 1) {
        updateParticlesCUDA(particlePositions, particleColors, n, flowParams, dt);
    } else {
        for (int i = 0; i < n; i++) {
            glm::vec3 p(particlePositions[3*i], particlePositions[3*i+1], particlePositions[3*i+2]);
            glm::vec3 v = computeVelocityFieldCPU(p, flowParams);
            glm::vec3 np = p + v * dt * flowParams.timeScale;

            bool collided = false;
            float surfDist = 1000.0f;

            if (useVoxelCollision && !g_distanceField.empty()) {
                int ix = (int)((np.x - g_voxMinX) / flowParams.cellSizeX);
                int iy = (int)((np.y - g_voxMinY) / flowParams.cellSizeY);
                int iz = (int)((np.z - g_voxMinZ) / flowParams.cellSizeZ);
                if (ix>=0 && ix<g_voxNx && iy>=0 && iy<g_voxNy && iz>=0 && iz<g_voxNz) {
                    int idx = (iz*g_voxNy + iy)*g_voxNx + ix;
                    surfDist = g_distanceField[idx];
                    if (surfDist < 0.0f) {
                        glm::vec3 nrm = sdfNormalCPU(np);
                        float push = fabsf(surfDist) + 0.5f * flowParams.cellSizeX;
                        np += nrm * push;
                        collided = true;

                        float vmag = glm::length(glm::vec3(flowParams.vx, flowParams.vy, flowParams.vz));
                        if (vmag < 1e-4f) vmag = 1e-4f;
                        glm::vec3 Vinf(flowParams.vx, flowParams.vy, flowParams.vz);

                        float vinf_n = glm::dot(Vinf, nrm);
                        glm::vec3 vinf_t = Vinf - vinf_n * nrm;

                        float vn = glm::dot(v, nrm);
                        v -= vn * nrm;

                        float perpFactor = fabsf(vinf_n) / vmag;
                        float slideBoost = 1.2f + 0.8f * perpFactor;

                        v += vinf_t * slideBoost * 0.6f;

                        float spd = glm::length(v);
                        if (spd < 0.3f * vmag) {
                            v = vinf_t * slideBoost;
                        }
                    } else if (surfDist < 1.5f * flowParams.cellSizeX) {
                        collided = true;
                    }
                }
            }

            if (np.x < flowParams.minX || np.x > flowParams.maxX ||
                np.y < flowParams.minY || np.y > flowParams.maxY ||
                np.z < flowParams.minZ || np.z > flowParams.maxZ) {
                float r1 = fabsf(sinf(i*12.9898f + flowParams.time*10.0f) * 43758.5453f); r1 -= floorf(r1);
                float r2 = fabsf(cosf(i*39.346f + flowParams.time*15.0f) * 24634.6345f); r2 -= floorf(r2);
                np.x = flowParams.minX + (flowParams.maxX - flowParams.minX)*(0.05f + 0.9f*r1);
                np.y = flowParams.minY + (flowParams.maxY - flowParams.minY)*(0.05f + 0.9f*r2);
                np.z = flowParams.minZ + 0.05f;
            }

            particlePositions[3*i] = np.x;
            particlePositions[3*i+1] = np.y;
            particlePositions[3*i+2] = np.z;

            glm::vec3 c = colorForPoint(v, surfDist, flowParams);
            particleColors[3*i] = c.x;
            particleColors[3*i+1] = c.y;
            particleColors[3*i+2] = c.z;
        }
    }
    glBindBuffer(GL_ARRAY_BUFFER, particleVBO_pos);
    glBufferSubData(GL_ARRAY_BUFFER, 0, particlePositions.size()*sizeof(float), particlePositions.data());
    glBindBuffer(GL_ARRAY_BUFFER, particleVBO_col);
    glBufferSubData(GL_ARRAY_BUFFER, 0, particleColors.size()*sizeof(float), particleColors.data());
}

// =====================================================
// Линии тока
// =====================================================
void computeStreamlines() {
    updateFlowParams();
    if (maxDim < 0.001f) return;

    std::vector<float> verts;
    verts.reserve(numStreamlines * streamlineSteps * 12);

    glm::vec3 flowDir(flowParams.vx, flowParams.vy, flowParams.vz);
    if (glm::length(flowDir) < 1e-6f) flowDir = glm::vec3(1,0,0);
    flowDir = glm::normalize(flowDir);

    glm::vec3 upRef(0.0f, 1.0f, 0.0f);
    if (fabs(glm::dot(upRef, flowDir)) > 0.95f) upRef = glm::vec3(1.0f, 0.0f, 0.0f);
    glm::vec3 right = glm::normalize(glm::cross(flowDir, upRef));
    glm::vec3 up    = glm::normalize(glm::cross(right, flowDir));

    float startDist = maxDim * 0.8f;
    glm::vec3 startPlaneCenter = center - flowDir * startDist;
    startPlaneCenter.x = glm::clamp(startPlaneCenter.x, flowParams.minX + 0.1f*maxDim, flowParams.maxX - 0.1f*maxDim);
    startPlaneCenter.y = glm::clamp(startPlaneCenter.y, flowParams.minY + 0.1f*maxDim, flowParams.maxY - 0.1f*maxDim);
    startPlaneCenter.z = glm::clamp(startPlaneCenter.z, flowParams.minZ + 0.1f*maxDim, flowParams.maxZ - 0.1f*maxDim);

    float spread = maxDim * 0.8f;
    int grid = (int)ceilf(sqrtf((float)numStreamlines));
    if (grid < 1) grid = 1;

    int linesDrawn = 0;
    for (int gy = 0; gy < grid && linesDrawn < numStreamlines; gy++) {
        for (int gx = 0; gx < grid && linesDrawn < numStreamlines; gx++) {
            float fx = (grid <= 1) ? 0.0f : ((float)gx/(grid-1) - 0.5f) * 2.0f;
            float fy = (grid <= 1) ? 0.0f : ((float)gy/(grid-1) - 0.5f) * 2.0f;
            glm::vec3 start = startPlaneCenter + right*(fx*spread) + up*(fy*spread);

            glm::vec3 p = start, prev = p;
            glm::vec3 v_prev = computeVelocityFieldCPU(prev, flowParams);
            float d_prev = sampleSDFCPU(prev);
            glm::vec3 c_prev = colorForPoint(v_prev, d_prev, flowParams);

            for (int s = 0; s < streamlineSteps; s++) {
                glm::vec3 v = computeVelocityFieldCPU(p, flowParams);
                float sp = glm::length(v);
                if (sp < 1e-6f) break;
                float distToCenter = glm::length(p - center);
                float stepLen = streamlineStepSize;
                if (distToCenter < maxDim*1.5f) stepLen *= 0.5f;
                if (distToCenter < maxDim*0.9f) stepLen *= 0.5f;

                glm::vec3 dir = v / sp;
                p = p + dir * stepLen;

                float bigMargin = maxDim * 2.0f;
                if (p.x < flowParams.minX - bigMargin || p.x > flowParams.maxX + bigMargin ||
                    p.y < flowParams.minY - bigMargin || p.y > flowParams.maxY + bigMargin ||
                    p.z < flowParams.minZ - bigMargin || p.z > flowParams.maxZ + bigMargin)
                    break;

                float d_p = sampleSDFCPU(p);
                glm::vec3 c_p = colorForPoint(v, d_p, flowParams);

                verts.push_back(prev.x); verts.push_back(prev.y); verts.push_back(prev.z);
                verts.push_back(c_prev.x); verts.push_back(c_prev.y); verts.push_back(c_prev.z);
                verts.push_back(p.x);    verts.push_back(p.y);    verts.push_back(p.z);
                verts.push_back(c_p.x);  verts.push_back(c_p.y);  verts.push_back(c_p.z);

                prev = p; v_prev = v; c_prev = c_p;
            }
            linesDrawn++;
        }
    }

    std::cout << "Streamlines: " << linesDrawn << " lines, " << (verts.size()/6) << " vertices" << std::endl;
    streamlineVertexCount = (int)(verts.size() / 6);

    if (streamlineVAO == 0) glGenVertexArrays(1, &streamlineVAO);
    if (streamlineVBO == 0) glGenBuffers(1, &streamlineVBO);
    glBindVertexArray(streamlineVAO);
    glBindBuffer(GL_ARRAY_BUFFER, streamlineVBO);
    glBufferData(GL_ARRAY_BUFFER, verts.size()*sizeof(float),
                 verts.empty()?nullptr:verts.data(), GL_DYNAMIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6*sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6*sizeof(float), (void*)(3*sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);
}

// =====================================================
// Давление
// =====================================================
void updateVertexColors() {
    updateFlowParams();
    int numVerts = (int)(g_vertices.size() / 3);
    if (numVerts == 0) return;

    if (useCUDA == 1) {
        computeVertexPressureCUDA(g_vertices, g_normals, g_vertexColors, numVerts, flowParams);
    } else {
        g_vertexColors.resize(numVerts*3);
        float vinf = sqrtf(flowParams.vx*flowParams.vx + flowParams.vy*flowParams.vy + flowParams.vz*flowParams.vz);
        if (vinf < 1e-4f) vinf = 1e-4f;
        for (int i = 0; i < numVerts; i++) {
            glm::vec3 p(g_vertices[3*i], g_vertices[3*i+1], g_vertices[3*i+2]);
            glm::vec3 v = computeVelocityFieldCPU(p, flowParams);
            float speed = glm::length(v);
            float speedRatio = speed / vinf;
            float cp = 1.0f - speedRatio*speedRatio;

            float rx = p.x - flowParams.centerX;
            float ry = p.y - flowParams.centerY;
            float rz = p.z - flowParams.centerZ;
            float fl = 1.0f / vinf;
            float along = rx*(flowParams.vx*fl) + ry*(flowParams.vy*fl) + rz*(flowParams.vz*fl);
            float D = 2.0f * fmaxf(flowParams.radiusY, flowParams.radiusZ);
            if (along > D * 0.3f) {
                float w = (along - D*0.3f) / D;
                if (w > 1.0f) w = 1.0f;
                cp -= 0.8f * w * w;
            }
            if (cp>1.0f) cp=1.0f; if (cp<-3.0f) cp=-3.0f;

            float t = (cp+1.0f)/2.0f;
            t = glm::clamp(t, 0.0f, 1.0f);
            glm::vec3 col;
            if (t<0.25f) { float k=t/0.25f; col=glm::vec3(0,k,1); }
            else if (t<0.5f) { float k=(t-0.25f)/0.25f; col=glm::vec3(0,1,1-k); }
            else if (t<0.75f) { float k=(t-0.5f)/0.25f; col=glm::vec3(k,1,0); }
            else { float k=(t-0.75f)/0.25f; col=glm::vec3(1,1-k,0); }
            g_vertexColors[3*i]=col.x; g_vertexColors[3*i+1]=col.y; g_vertexColors[3*i+2]=col.z;
        }
    }

    if (modelVBO_colors == 0) {
        glGenBuffers(1, &modelVBO_colors);
        glBindVertexArray(modelVAO);
        glBindBuffer(GL_ARRAY_BUFFER, modelVBO_colors);
        glBufferData(GL_ARRAY_BUFFER, g_vertexColors.size()*sizeof(float), g_vertexColors.data(), GL_DYNAMIC_DRAW);
        glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, 3*sizeof(float), (void*)0);
        glEnableVertexAttribArray(2);
        glBindVertexArray(0);
    } else {
        glBindBuffer(GL_ARRAY_BUFFER, modelVBO_colors);
        glBufferSubData(GL_ARRAY_BUFFER, 0, g_vertexColors.size()*sizeof(float), g_vertexColors.data());
    }
}

// =====================================================
// Lift / Drag
// =====================================================
void computeLiftDrag() {
    updateFlowParams();
    int numVerts = (int)(g_vertices.size() / 3);
    if (numVerts == 0) return;

    float vinf = sqrtf(flowParams.vx*flowParams.vx + flowParams.vy*flowParams.vy + flowParams.vz*flowParams.vz);
    if (vinf < 1e-4f) vinf = 1e-4f;

    glm::vec3 flowDir(flowParams.vx, flowParams.vy, flowParams.vz);
    flowDir /= vinf;

    glm::vec3 totalForce(0.0f);
    glm::vec3 cpSum(0.0f);
    float areaSum = 0.0f;

    for (size_t i = 0; i + 8 < g_vertices.size(); i += 9) {
        glm::vec3 v0(g_vertices[i],   g_vertices[i+1], g_vertices[i+2]);
        glm::vec3 v1(g_vertices[i+3], g_vertices[i+4], g_vertices[i+5]);
        glm::vec3 v2(g_vertices[i+6], g_vertices[i+7], g_vertices[i+8]);
        glm::vec3 n (g_normals[i],    g_normals[i+1],  g_normals[i+2]);

        glm::vec3 triCenter = (v0+v1+v2) / 3.0f;
        glm::vec3 cr = glm::cross(v1-v0, v2-v0);
        float area = 0.5f * glm::length(cr);
        if (area < 1e-9f) continue;

        glm::vec3 vel = computeVelocityFieldCPU(triCenter, flowParams);
        float speed = glm::length(vel);
        float cp = 1.0f - (speed*speed)/(vinf*vinf);

        glm::vec3 force = -cp * n * area;
        totalForce += force;
        cpSum += triCenter * (cp * area);
        areaSum += area;
    }

    dragMagnitude = glm::dot(totalForce, flowDir);
    dragVector = flowDir * dragMagnitude;

    glm::vec3 liftDir = glm::vec3(0,1,0) - flowDir * glm::dot(glm::vec3(0,1,0), flowDir);
    if (glm::length(liftDir) > 1e-6f) {
        liftDir = glm::normalize(liftDir);
        liftMagnitude = glm::dot(totalForce, liftDir);
        liftVector = liftDir * liftMagnitude;
    } else {
        liftMagnitude = 0.0f;
        liftVector = glm::vec3(0.0f);
    }

    if (areaSum > 1e-9f) centerOfPressure = cpSum / areaSum;
    liftDragDirty = true;
}

void updateLiftDragArrows() {
    if (!liftDragDirty) return;
    liftDragDirty = false;

    float dragScale = 0.5f / (maxDim + 1e-6f);
    float liftScale = 0.5f / (maxDim + 1e-6f);

    std::vector<float> verts;
    if (fabs(dragMagnitude) > 1e-6f) {
        glm::vec3 s = centerOfPressure;
        glm::vec3 e = s + dragVector * dragScale;
        verts.push_back(s.x); verts.push_back(s.y); verts.push_back(s.z);
        verts.push_back(e.x); verts.push_back(e.y); verts.push_back(e.z);
    }
    if (fabs(liftMagnitude) > 1e-6f) {
        glm::vec3 s = centerOfPressure;
        glm::vec3 e = s + liftVector * liftScale;
        verts.push_back(s.x); verts.push_back(s.y); verts.push_back(s.z);
        verts.push_back(e.x); verts.push_back(e.y); verts.push_back(e.z);
    }

    if (liftDragVAO == 0) glGenVertexArrays(1, &liftDragVAO);
    if (liftDragVBO == 0) glGenBuffers(1, &liftDragVBO);
    glBindVertexArray(liftDragVAO);
    glBindBuffer(GL_ARRAY_BUFFER, liftDragVBO);
    glBufferData(GL_ARRAY_BUFFER, verts.size()*sizeof(float), verts.data(), GL_DYNAMIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3*sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);
}

// =====================================================
// Загрузка
// =====================================================
std::string openFileDialog() {
    OPENFILENAMEA ofn;
    char fileName[MAX_PATH] = "";
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = nullptr;
    ofn.lpstrFilter = "STL Files\0*.stl\0All Files\0*.*\0";
    ofn.lpstrFile = fileName;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (GetOpenFileNameA(&ofn)) return std::string(fileName);
    return "";
}

bool loadModel(const std::string& path) {
    std::vector<float> vertices, normals;
    if (!loadSTL(path, vertices, normals)) return false;

    g_vertices = vertices;
    g_normals = normals;
    g_vertexColors.assign(vertices.size(), 0.75f);

    minBB = glm::vec3(FLT_MAX);
    maxBB = glm::vec3(-FLT_MAX);
    for (size_t i = 0; i < vertices.size(); i += 3) {
        glm::vec3 v(vertices[i], vertices[i+1], vertices[i+2]);
        minBB = glm::min(minBB, v);
        maxBB = glm::max(maxBB, v);
    }
    center = (minBB + maxBB) * 0.5f;
    maxDim = glm::length(maxBB - minBB);
    if (maxDim < 0.0001f) maxDim = 1.0f;

    cameraPos = center + glm::vec3(maxDim*1.8f, maxDim*0.8f, maxDim*1.8f);
    cameraFront = glm::normalize(center - cameraPos);
    yaw = glm::degrees(atan2f(cameraFront.z, cameraFront.x));
    pitch = glm::degrees(asinf(cameraFront.y));

    if (modelVAO == 0) glGenVertexArrays(1, &modelVAO);
    if (modelVBO_vertices == 0) glGenBuffers(1, &modelVBO_vertices);
    if (modelVBO_normals == 0) glGenBuffers(1, &modelVBO_normals);
    glBindVertexArray(modelVAO);
    glBindBuffer(GL_ARRAY_BUFFER, modelVBO_vertices);
    glBufferData(GL_ARRAY_BUFFER, vertices.size()*sizeof(float), vertices.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3*sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, modelVBO_normals);
    glBufferData(GL_ARRAY_BUFFER, normals.size()*sizeof(float), normals.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 3*sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);
    modelVertexCount = (int)(vertices.size() / 3);

    createBoundingBoxVAO();
    createAxesVAO(maxDim * 0.6f);

    buildVoxelGrid(vertices, voxelResolution);

    initParticles();
    computeStreamlines();
    updateVertexColors();
    computeLiftDrag();
    updateLiftDragArrows();
    return true;
}

// =====================================================
// main
// =====================================================
int main() {
    if (!glfwInit()) {
        MessageBoxA(nullptr, "Failed to init GLFW", "Error", MB_ICONERROR);
        return -1;
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);

    GLFWwindow* window = glfwCreateWindow(SCR_WIDTH, SCR_HEIGHT, "AeroS Engine", nullptr, nullptr);
    if (!window) {
        MessageBoxA(nullptr, "Failed to create GLFW window", "Error", MB_ICONERROR);
        glfwTerminate();
        return -1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
    glfwSetCursorPosCallback(window, mouse_callback);
    glfwSetScrollCallback(window, scroll_callback);
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        MessageBoxA(nullptr, "Failed to init GLAD", "Error", MB_ICONERROR);
        glfwTerminate();
        return -1;
    }
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    createObstacleSphere(48, 48);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330 core");

    unsigned int vs = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vs, 1, &vertexShaderSource, nullptr);
    glCompileShader(vs);
    unsigned int fs = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fs, 1, &fragmentShaderSource, nullptr);
    glCompileShader(fs);
    unsigned int modelShaderProgram = glCreateProgram();
    glAttachShader(modelShaderProgram, vs);
    glAttachShader(modelShaderProgram, fs);
    glLinkProgram(modelShaderProgram);
    glDeleteShader(vs); glDeleteShader(fs);

    unsigned int pvs = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(pvs, 1, &particleVertexShaderSource, nullptr);
    glCompileShader(pvs);
    unsigned int pfs = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(pfs, 1, &particleFragmentShaderSource, nullptr);
    glCompileShader(pfs);
    unsigned int particleShaderProgram = glCreateProgram();
    glAttachShader(particleShaderProgram, pvs);
    glAttachShader(particleShaderProgram, pfs);
    glLinkProgram(particleShaderProgram);
    glDeleteShader(pvs); glDeleteShader(pfs);

    unsigned int lvs = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(lvs, 1, &lineVertexShaderSource, nullptr);
    glCompileShader(lvs);
    unsigned int lfs = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(lfs, 1, &lineFragmentShaderSource, nullptr);
    glCompileShader(lfs);
    unsigned int lineShaderProgram = glCreateProgram();
    glAttachShader(lineShaderProgram, lvs);
    glAttachShader(lineShaderProgram, lfs);
    glLinkProgram(lineShaderProgram);
    glDeleteShader(lvs); glDeleteShader(lfs);

    std::string modelPath = openFileDialog();
    if (modelPath.empty()) {
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        glfwTerminate();
        return 0;
    }
    if (!loadModel(modelPath)) {
        MessageBoxA(nullptr, "Failed to load model.", "Error", MB_ICONERROR);
    }

    glfwShowWindow(window);

    static float prevSpeed = flowSpeed;
    static float prevAz    = flowAzimuth;
    static float prevEl    = flowElevation;
    static float prevWake  = wakeStrength;
    static float prevStro  = strouhal;
    static float prevWL    = wakeLength;

    while (!glfwWindowShouldClose(window)) {
        float currentFrame = glfwGetTime();
        deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;
        // Ограничиваем dt: на первом кадре (после диалога/вокселизации) он может
        // исчисляться секундами, что телепортирует все частицы.
        if (deltaTime > 0.1f)   deltaTime = 0.1f;
        if (deltaTime <= 0.0f)  deltaTime = 0.0001f;

        if (limitFPS) {
            double target = 1.0 / maxFPS;
            while (glfwGetTime() - currentFrame < target)
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        processInput(window);

        if (showParticles) updateParticles(deltaTime);
        if (showPressure)  updateVertexColors();
        computeLiftDrag();
        updateLiftDragArrows();

        if (showStreamlines && (fabs(prevSpeed-flowSpeed) > 1e-3f ||
                                fabs(prevAz-flowAzimuth) > 1e-3f ||
                                fabs(prevEl-flowElevation) > 1e-3f ||
                                fabs(prevWake-wakeStrength) > 1e-3f ||
                                fabs(prevStro-strouhal) > 1e-3f ||
                                fabs(prevWL-wakeLength) > 1e-3f)) {
            prevSpeed = flowSpeed;
            prevAz = flowAzimuth;
            prevEl = flowElevation;
            prevWake = wakeStrength;
            prevStro = strouhal;
            prevWL = wakeLength;
            computeStreamlines();
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_Once);
        ImGui::SetNextWindowSize(ImVec2(380, 720), ImGuiCond_Once);
        ImGui::Begin("AeroS Control");
        ImGui::Text("FPS: %.1f", 1.0f/deltaTime);
        ImGui::Text("Vertices: %d", modelVertexCount);
        ImGui::Text("Triangles: %d", modelVertexCount/3);
        ImGui::Text("Voxel Grid: %dx%dx%d", g_voxNx, g_voxNy, g_voxNz);
        ImGui::Separator();

        if (ImGui::CollapsingHeader("Flow", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::SliderFloat("Speed", &flowSpeed, 0.0f, 10.0f);
            ImGui::SameLine(); ImGui::SetNextItemWidth(80);
            ImGui::InputFloat("##spd", &flowSpeed, 0, 0, "%.2f");
            ImGui::SliderFloat("Azimuth", &flowAzimuth, 0.0f, 360.0f);
            ImGui::SameLine(); ImGui::SetNextItemWidth(80);
            ImGui::InputFloat("##az", &flowAzimuth, 0, 0, "%.1f");
            ImGui::SliderFloat("Elevation", &flowElevation, -90.0f, 90.0f);
            ImGui::SameLine(); ImGui::SetNextItemWidth(80);
            ImGui::InputFloat("##el", &flowElevation, 0, 0, "%.1f");
            ImGui::SliderFloat("Time Scale", &timeScale, 0.01f, 3.0f, "%.2f");
            ImGui::SliderFloat("Strouhal", &strouhal, 0.05f, 0.5f, "%.3f");
            ImGui::SliderFloat("Wake Strength", &wakeStrength, 0.0f, 1.5f);
            ImGui::SliderFloat("Wake Length", &wakeLength, 2.0f, 30.0f);
        }

        if (ImGui::CollapsingHeader("Particles", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Checkbox("Show Particles", &showParticles);
            ImGui::SliderInt("Count", &numParticles, 100, 200000);
            // Перевыделяем буферы только когда слайдер отпущен
            if (ImGui::IsItemDeactivatedAfterEdit()) initParticles();
            ImGui::SliderFloat("Size", &particleSize, 1.0f, 8.0f);
            ImGui::SliderFloat("Max Speed Color", &maxSpeedForColor, 0.5f, 20.0f);
            if (ImGui::Button("Reset Particles")) initParticles();
        }

        if (ImGui::CollapsingHeader("Streamlines", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Checkbox("Show Streamlines", &showStreamlines);
            ImGui::SliderInt("Count##sl", &numStreamlines, 4, 200);
            ImGui::SliderInt("Steps", &streamlineSteps, 20, 1000);
            ImGui::SliderFloat("Step Size", &streamlineStepSize, 0.01f, 0.5f);
            ImGui::SliderFloat("Line Width", &streamlineWidth, 1.0f, 5.0f);
            ImGui::SliderFloat("Alpha", &streamlineAlpha, 0.1f, 1.0f);
            if (ImGui::Button("Rebuild Streamlines")) computeStreamlines();
        }

        if (ImGui::CollapsingHeader("Pressure & Forces", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Checkbox("Show Pressure Colors", &showPressure);
            ImGui::Checkbox("Show Lift/Drag Vectors", &showLiftDrag);
            ImGui::Text("Drag:  %.3f", dragMagnitude);
            ImGui::Text("Lift:  %.3f", liftMagnitude);
        }

        if (ImGui::CollapsingHeader("Voxel Collision", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Checkbox("Enable Voxel Collision", &useVoxelCollision);
            ImGui::SliderInt("Voxel Resolution", &voxelResolution, 16, 128);
            if (ImGui::Button("Rebuild Voxel Grid")) {
                buildVoxelGrid(g_vertices, voxelResolution);
            }
        }

        if (ImGui::CollapsingHeader("Display", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Checkbox("Show Model", &showModel);
            ImGui::Checkbox("Show Obstacle", &showObstacle);
            if (showObstacle) {
                ImGui::SliderFloat("Obstacle Alpha", &obstacleAlpha, 0.02f, 1.0f, "%.2f");
                ImGui::ColorEdit3("Obstacle Color", &obstacleColor[0]);
            }
            ImGui::Checkbox("Show Bounding Box", &showBoundingBox);
            ImGui::Checkbox("Show Axes", &showAxes);
            ImGui::Checkbox("Lighting", &lightingEnabled);
            ImGui::ColorEdit3("Background", &bgColor[0]);
        }

        if (ImGui::CollapsingHeader("Compute")) {
            ImGui::RadioButton("CUDA", &useCUDA, 1);
            ImGui::SameLine();
            ImGui::RadioButton("CPU", &useCUDA, 0);
            if (ImGui::Button("Open Model")) {
                std::string p = openFileDialog();
                if (!p.empty()) loadModel(p);
            }
        }

        ImGui::End();

        glClearColor(bgColor.x, bgColor.y, bgColor.z, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glm::mat4 view = glm::lookAt(cameraPos, cameraPos + cameraFront, cameraUp);
        glm::mat4 projection = glm::perspective(glm::radians(fov),
            (float)display_w/(float)display_h, 0.05f, maxDim*50.0f);
        glm::mat4 model = glm::mat4(1.0f);

        glm::vec3 lightPos = center + glm::vec3(maxDim*2.0f, maxDim*2.5f, maxDim*2.0f);
        glm::vec3 lightColor(1.0f);

        if (showModel) {
            glUseProgram(modelShaderProgram);
            glUniformMatrix4fv(glGetUniformLocation(modelShaderProgram, "model"), 1, GL_FALSE, glm::value_ptr(model));
            glUniformMatrix4fv(glGetUniformLocation(modelShaderProgram, "view"), 1, GL_FALSE, glm::value_ptr(view));
            glUniformMatrix4fv(glGetUniformLocation(modelShaderProgram, "projection"), 1, GL_FALSE, glm::value_ptr(projection));
            glUniform3fv(glGetUniformLocation(modelShaderProgram, "lightPos"), 1, &lightPos[0]);
            glUniform3fv(glGetUniformLocation(modelShaderProgram, "viewPos"), 1, &cameraPos[0]);
            glUniform3fv(glGetUniformLocation(modelShaderProgram, "lightColor"), 1, &lightColor[0]);
            glUniform3fv(glGetUniformLocation(modelShaderProgram, "objectColor"), 1, &modelColor[0]);
            glUniform1i(glGetUniformLocation(modelShaderProgram, "useLighting"), lightingEnabled ? 1 : 0);
            glUniform1i(glGetUniformLocation(modelShaderProgram, "useVertexColor"), showPressure ? 1 : 0);
            glUniform1f(glGetUniformLocation(modelShaderProgram, "alpha"), 1.0f);
            glBindVertexArray(modelVAO);
            glDrawArrays(GL_TRIANGLES, 0, modelVertexCount);
            glBindVertexArray(0);
        }

        if (showObstacle) {
            glDepthMask(GL_FALSE);
            glUseProgram(modelShaderProgram);
            glm::mat4 om = glm::translate(glm::mat4(1.0f), center) *
                           glm::scale(glm::mat4(1.0f), glm::vec3(flowParams.radiusX, flowParams.radiusY, flowParams.radiusZ));
            glUniformMatrix4fv(glGetUniformLocation(modelShaderProgram, "model"), 1, GL_FALSE, glm::value_ptr(om));
            glUniformMatrix4fv(glGetUniformLocation(modelShaderProgram, "view"), 1, GL_FALSE, glm::value_ptr(view));
            glUniformMatrix4fv(glGetUniformLocation(modelShaderProgram, "projection"), 1, GL_FALSE, glm::value_ptr(projection));
            glUniform3fv(glGetUniformLocation(modelShaderProgram, "lightPos"), 1, &lightPos[0]);
            glUniform3fv(glGetUniformLocation(modelShaderProgram, "viewPos"), 1, &cameraPos[0]);
            glUniform3fv(glGetUniformLocation(modelShaderProgram, "lightColor"), 1, &lightColor[0]);
            glUniform3fv(glGetUniformLocation(modelShaderProgram, "objectColor"), 1, &obstacleColor[0]);
            glUniform1i(glGetUniformLocation(modelShaderProgram, "useLighting"), 1);
            glUniform1i(glGetUniformLocation(modelShaderProgram, "useVertexColor"), 0);
            glUniform1f(glGetUniformLocation(modelShaderProgram, "alpha"), obstacleAlpha);
            glBindVertexArray(obstacleVAO);
            glDrawElements(GL_TRIANGLES, obstacleIndexCount, GL_UNSIGNED_INT, 0);
            glBindVertexArray(0);
            glDepthMask(GL_TRUE);
        }

        glUseProgram(lineShaderProgram);
        glUniformMatrix4fv(glGetUniformLocation(lineShaderProgram, "model"), 1, GL_FALSE, glm::value_ptr(model));
        glUniformMatrix4fv(glGetUniformLocation(lineShaderProgram, "view"), 1, GL_FALSE, glm::value_ptr(view));
        glUniformMatrix4fv(glGetUniformLocation(lineShaderProgram, "projection"), 1, GL_FALSE, glm::value_ptr(projection));
        glUniform1f(glGetUniformLocation(lineShaderProgram, "alpha"), 1.0f);
        glUniform1i(glGetUniformLocation(lineShaderProgram, "useVertexColor"), 0);

        if (showBoundingBox) {
            glUniform3fv(glGetUniformLocation(lineShaderProgram, "lineColor"), 1, &bboxColor[0]);
            glBindVertexArray(bboxVAO);
            glDrawArrays(GL_LINES, 0, 24);
            glBindVertexArray(0);
        }
        if (showAxes) {
            glBindVertexArray(axesVAO);
            glUniform3f(glGetUniformLocation(lineShaderProgram, "lineColor"), 1,0,0);
            glDrawArrays(GL_LINES, 0, 2);
            glUniform3f(glGetUniformLocation(lineShaderProgram, "lineColor"), 0,1,0);
            glDrawArrays(GL_LINES, 2, 2);
            glUniform3f(glGetUniformLocation(lineShaderProgram, "lineColor"), 0,0,1);
            glDrawArrays(GL_LINES, 4, 2);
            glBindVertexArray(0);
        }

        if (showStreamlines && streamlineVertexCount > 0) {
            glUseProgram(lineShaderProgram);
            glUniformMatrix4fv(glGetUniformLocation(lineShaderProgram, "model"), 1, GL_FALSE, glm::value_ptr(model));
            glUniformMatrix4fv(glGetUniformLocation(lineShaderProgram, "view"), 1, GL_FALSE, glm::value_ptr(view));
            glUniformMatrix4fv(glGetUniformLocation(lineShaderProgram, "projection"), 1, GL_FALSE, glm::value_ptr(projection));
            glUniform1i(glGetUniformLocation(lineShaderProgram, "useVertexColor"), 1);
            glUniform1f(glGetUniformLocation(lineShaderProgram, "alpha"), streamlineAlpha);
            glLineWidth(streamlineWidth);
            glBindVertexArray(streamlineVAO);
            glDrawArrays(GL_LINES, 0, streamlineVertexCount);
            glBindVertexArray(0);
            glLineWidth(1.0f);
        }

        if (showParticles) {
            glUseProgram(particleShaderProgram);
            glUniformMatrix4fv(glGetUniformLocation(particleShaderProgram, "model"), 1, GL_FALSE, glm::value_ptr(model));
            glUniformMatrix4fv(glGetUniformLocation(particleShaderProgram, "view"), 1, GL_FALSE, glm::value_ptr(view));
            glUniformMatrix4fv(glGetUniformLocation(particleShaderProgram, "projection"), 1, GL_FALSE, glm::value_ptr(projection));
            glPointSize(particleSize);
            glBindVertexArray(particleVAO);
            glDrawArrays(GL_POINTS, 0, particleDrawCount);
            glBindVertexArray(0);
            glPointSize(1.0f);
        }

        if (showLiftDrag) {
            glUseProgram(lineShaderProgram);
            glUniformMatrix4fv(glGetUniformLocation(lineShaderProgram, "model"), 1, GL_FALSE, glm::value_ptr(model));
            glUniformMatrix4fv(glGetUniformLocation(lineShaderProgram, "view"), 1, GL_FALSE, glm::value_ptr(view));
            glUniformMatrix4fv(glGetUniformLocation(lineShaderProgram, "projection"), 1, GL_FALSE, glm::value_ptr(projection));
            glUniform1i(glGetUniformLocation(lineShaderProgram, "useVertexColor"), 0);
            glUniform1f(glGetUniformLocation(lineShaderProgram, "alpha"), 1.0f);
            glLineWidth(3.0f);
            if (fabs(dragMagnitude) > 1e-6f) {
                glUniform3f(glGetUniformLocation(lineShaderProgram, "lineColor"), 1,0.35f,0.15f);
                glBindVertexArray(liftDragVAO);
                glDrawArrays(GL_LINES, 0, 2);
                glBindVertexArray(0);
            }
            if (fabs(liftMagnitude) > 1e-6f) {
                glUniform3f(glGetUniformLocation(lineShaderProgram, "lineColor"), 0.3f,1,0.3f);
                glBindVertexArray(liftDragVAO);
                glDrawArrays(GL_LINES, 2, 2);
                glBindVertexArray(0);
            }
            glLineWidth(1.0f);
        }

        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwTerminate();
    return 0;
}