#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <cmath>
#include <iostream>
#include <vector>
#include <cstring>

#include "globals.h"
#include "gl_utils.h"

// =====================================================
// Компиляция шейдерной программы с проверкой ошибок — v1.9.0 улучшено
// =====================================================
unsigned int compileProgram(const char* vsSrc, const char* fsSrc) {
    if (!vsSrc || !fsSrc) {
        std::cerr << "[Shader] Null source provided" << std::endl;
        return 0;
    }
    if (strlen(vsSrc) < 10 || strlen(fsSrc) < 10) {
        std::cerr << "[Shader] Source too short" << std::endl;
        return 0;
    }
    auto check = [](unsigned int obj, bool isShader) -> bool {
        GLint ok = 0;
        char log[4096] = {0};
        if (isShader) {
            glGetShaderiv(obj, GL_COMPILE_STATUS, &ok);
            if (!ok) glGetShaderInfoLog(obj, sizeof(log), nullptr, log);
        } else {
            glGetProgramiv(obj, GL_LINK_STATUS, &ok);
            if (!ok) glGetProgramInfoLog(obj, sizeof(log), nullptr, log);
        }
        if (!ok) {
            std::cerr << "[Shader] Error: " << log << std::endl;
#ifdef _WIN32
            MessageBoxA(nullptr, log, "Shader compile/link error", MB_ICONERROR);
#endif
        }
        return ok != 0;
    };

    unsigned int vs = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vs, 1, &vsSrc, nullptr);
    glCompileShader(vs);
    unsigned int fs = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fs, 1, &fsSrc, nullptr);
    glCompileShader(fs);

    if (!check(vs, true) || !check(fs, true)) {
        glDeleteShader(vs); glDeleteShader(fs);
        return 0;
    }

    unsigned int prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);
    glDeleteShader(vs); glDeleteShader(fs);

    if (!check(prog, false)) {
        glDeleteProgram(prog);
        return 0;
    }
    glValidateProgram(prog);
    GLint valid = 0;
    glGetProgramiv(prog, GL_VALIDATE_STATUS, &valid);
    if (!valid) {
        char vLog[1024] = {0};
        glGetProgramInfoLog(prog, sizeof(vLog), nullptr, vLog);
        std::cerr << "[Shader] Validation warning: " << vLog << std::endl;
    }
    return prog;
}

// =====================================================
// BBox, оси — v1.9.0 с проверками
// =====================================================
void createBoundingBoxVAO() {
    if (bboxVAO == 0) glGenVertexArrays(1, &bboxVAO);
    if (bboxVBO == 0) glGenBuffers(1, &bboxVBO);
    glm::vec3 bmin = minBB, bmax = maxBB;
    if (!std::isfinite(bmin.x) || !std::isfinite(bmax.x) || glm::length(bmax-bmin) < 1e-6f) {
        bmin = glm::vec3(-1); bmax = glm::vec3(1);
    }
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
    if (size < 1e-6f || !std::isfinite(size)) size = 1.0f;
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
// Эллипсоид — v1.9.0 улучшено, меньше дегенератов на полюсах
// =====================================================
void createObstacleSphere(int stacks, int slices) {
    if (stacks < 3) stacks = 3;
    if (slices < 3) slices = 3;
    if (stacks > 128) stacks = 128;
    if (slices > 128) slices = 128;
    if (obstacleVAO == 0) glGenVertexArrays(1, &obstacleVAO);
    if (obstacleVBO == 0) glGenBuffers(1, &obstacleVBO);
    if (obstacleEBO == 0) glGenBuffers(1, &obstacleEBO);
    std::vector<float> v;
    std::vector<unsigned int> idx;
    v.reserve((stacks+1)*(slices+1)*6);
    idx.reserve(stacks*slices*6);
    for (int i = 0; i <= stacks; ++i) {
        float phi = glm::pi<float>() * (float)i / stacks;
        float y = cosf(phi), r = sinf(phi);
        for (int j = 0; j <= slices; ++j) {
            float theta = 2.0f * glm::pi<float>() * (float)j / slices;
            float x = r * cosf(theta);
            float z = r * sinf(theta);
            // Avoid degenerate normals at poles
            glm::vec3 normal(x, y, z);
            float nLen = glm::length(normal);
            if (nLen > 1e-6f) normal /= nLen;
            else normal = glm::vec3(0, (y>0?1:-1), 0);
            v.push_back(x); v.push_back(y); v.push_back(z);
            v.push_back(normal.x); v.push_back(normal.y); v.push_back(normal.z);
        }
    }
    for (int i = 0; i < stacks; ++i)
        for (int j = 0; j < slices; ++j) {
            int a = i*(slices+1)+j;
            int b = a + slices + 1;
            // Skip degenerate at poles where triangle area ~0
            if (i == 0) {
                idx.push_back(a); idx.push_back(b); idx.push_back(a+1);
            } else if (i == stacks-1) {
                idx.push_back(a); idx.push_back(b); idx.push_back(a+1);
            } else {
                idx.push_back(a); idx.push_back(b); idx.push_back(a+1);
                idx.push_back(a+1); idx.push_back(b); idx.push_back(b+1);
            }
        }
    obstacleIndexCount = (int)idx.size();
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
// Ground plane (для авто — фото 2,5) — v1.9.0 улучшено
// =====================================================
void createGroundPlane(float size) {
    if (size < 1e-6f || !std::isfinite(size)) size = 10.0f;
    if (size > 1000.0f) size = 1000.0f;
    if (groundVAO == 0) glGenVertexArrays(1, &groundVAO);
    if (groundVBO == 0) glGenBuffers(1, &groundVBO);
    if (groundEBO == 0) glGenBuffers(1, &groundEBO);

    float half = size * 0.5f;
    float y = 0.0f;
    float verts[] = {
        -half, y, -half,  0,1,0,
         half, y, -half,  0,1,0,
         half, y,  half,  0,1,0,
        -half, y,  half,  0,1,0
    };
    unsigned int indices[] = {0,1,2, 0,2,3};

    groundIndexCount = 6;
    glBindVertexArray(groundVAO);
    glBindBuffer(GL_ARRAY_BUFFER, groundVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, groundEBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6*sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6*sizeof(float), (void*)(3*sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);
}

void createGrid(float size, int divisions) {
    if (size < 1e-6f) size = 10.0f;
    if (size > 1000.0f) size = 1000.0f;
    if (divisions < 1) divisions = 10;
    if (divisions > 100) divisions = 100;
    if (gridVAO == 0) glGenVertexArrays(1, &gridVAO);
    if (gridVBO == 0) glGenBuffers(1, &gridVBO);

    std::vector<float> lines;
    lines.reserve((divisions+1)*4*3);
    float half = size*0.5f;
    float step = size / divisions;
    for (int i = 0; i <= divisions; ++i) {
        float pos = -half + i*step;
        lines.push_back(pos); lines.push_back(0); lines.push_back(-half);
        lines.push_back(pos); lines.push_back(0); lines.push_back(half);
        lines.push_back(-half); lines.push_back(0); lines.push_back(pos);
        lines.push_back(half); lines.push_back(0); lines.push_back(pos);
    }
    glBindVertexArray(gridVAO);
    glBindBuffer(GL_ARRAY_BUFFER, gridVBO);
    glBufferData(GL_ARRAY_BUFFER, lines.size()*sizeof(float), lines.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3*sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);
}

void createSlicePlane(int axis, float pos01) {
    if (sliceVAO == 0) glGenVertexArrays(1, &sliceVAO);
    if (sliceVBO == 0) glGenBuffers(1, &sliceVBO);

    glm::vec3 bmin = minBB, bmax = maxBB;
    if (!std::isfinite(bmin.x) || glm::length(bmax-bmin) < 1e-6f) {
        bmin = glm::vec3(-1); bmax = glm::vec3(1);
    }
    glm::vec3 p = bmin + (bmax-bmin)*glm::clamp(pos01,0.0f,1.0f);
    float verts[12];
    if (axis == 0) {
        float x = p.x;
        verts[0]=x; verts[1]=bmin.y; verts[2]=bmin.z;
        verts[3]=x; verts[4]=bmax.y; verts[5]=bmin.z;
        verts[6]=x; verts[7]=bmax.y; verts[8]=bmax.z;
        verts[9]=x; verts[10]=bmin.y; verts[11]=bmax.z;
    } else if (axis == 1) {
        float y = p.y;
        verts[0]=bmin.x; verts[1]=y; verts[2]=bmin.z;
        verts[3]=bmax.x; verts[4]=y; verts[5]=bmin.z;
        verts[6]=bmax.x; verts[7]=y; verts[8]=bmax.z;
        verts[9]=bmin.x; verts[10]=y; verts[11]=bmax.z;
    } else {
        float z = p.z;
        verts[0]=bmin.x; verts[1]=bmin.y; verts[2]=z;
        verts[3]=bmax.x; verts[4]=bmin.y; verts[5]=z;
        verts[6]=bmax.x; verts[7]=bmax.y; verts[8]=z;
        verts[9]=bmin.x; verts[10]=bmax.y; verts[11]=z;
    }
    glBindVertexArray(sliceVAO);
    glBindBuffer(GL_ARRAY_BUFFER, sliceVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_DYNAMIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3*sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);
}

void cleanupGLResources() {
    // v1.9.0: check if context is current before deleting
    auto safeDeleteVAO = [](unsigned int& vao) {
        if (vao != 0) {
            if (glIsVertexArray(vao)) glDeleteVertexArrays(1, &vao);
            vao = 0;
        }
    };
    auto safeDeleteBuf = [](unsigned int& buf) {
        if (buf != 0) {
            if (glIsBuffer(buf)) glDeleteBuffers(1, &buf);
            buf = 0;
        }
    };
    safeDeleteVAO(modelVAO);
    safeDeleteBuf(modelVBO_vertices);
    safeDeleteBuf(modelVBO_normals);
    safeDeleteBuf(modelVBO_colors);
    safeDeleteVAO(bboxVAO); safeDeleteBuf(bboxVBO);
    safeDeleteVAO(axesVAO); safeDeleteBuf(axesVBO);
    safeDeleteVAO(obstacleVAO); safeDeleteBuf(obstacleVBO); safeDeleteBuf(obstacleEBO);
    safeDeleteVAO(groundVAO); safeDeleteBuf(groundVBO); safeDeleteBuf(groundEBO);
    safeDeleteVAO(sliceVAO); safeDeleteBuf(sliceVBO);
    safeDeleteVAO(gridVAO); safeDeleteBuf(gridVBO);
    safeDeleteVAO(particleVAO); safeDeleteBuf(particleVBO_pos); safeDeleteBuf(particleVBO_col);
    safeDeleteVAO(streamlineVAO); safeDeleteBuf(streamlineVBO);
    safeDeleteVAO(liftDragVAO); safeDeleteBuf(liftDragVBO);
    obstacleIndexCount = 0;
    groundIndexCount = 0;
    streamlineVertexCount = 0;
}
