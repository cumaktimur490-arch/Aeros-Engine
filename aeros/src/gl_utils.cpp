#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

#include <windows.h>

#include <iostream>
#include <vector>

#include "globals.h"
#include "gl_utils.h"

// =====================================================
// Компиляция шейдерной программы с проверкой ошибок
// =====================================================
unsigned int compileProgram(const char* vsSrc, const char* fsSrc) {
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
            std::cerr << "Shader error: " << log << std::endl;
            MessageBoxA(nullptr, log, "Shader compile/link error", MB_ICONERROR);
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
    return prog;
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
void createObstacleSphere(int stacks, int slices) {
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
