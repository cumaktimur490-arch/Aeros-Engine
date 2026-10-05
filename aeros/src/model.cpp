#include <glad/glad.h>
#include <glm/glm.hpp>

#include <iostream>
#include <vector>
#include <cfloat>
#include <algorithm>
#include <chrono>

#include "globals.h"
#include "stl_loader.h"
#include "gl_utils.h"
#include "voxel_grid.h"
#include "particles.h"
#include "streamlines.h"
#include "forces.h"
#include "model.h"
#include "lbm.h"
#include "atmosphere.h"

// =====================================================
// Загрузка модели v1.8.0 — исправлено + улучшения
// =====================================================
bool loadModel(const std::string& path) {
    std::vector<float> vertices, normals;
    if (!loadSTL(path, vertices, normals)) {
        std::cerr << "[Model] Failed to load STL: " << path << std::endl;
        return false;
    }
    if (vertices.empty() || normals.empty()) {
        std::cerr << "[Model] Empty model" << std::endl;
        return false;
    }
    if (vertices.size() != normals.size()) {
        std::cerr << "[Model] Vertex/normal size mismatch" << std::endl;
        return false;
    }
    if (vertices.size() % 9 != 0) {
        std::cerr << "[Model] Warning: vertices not multiple of 9 (triangles)" << std::endl;
    }

    // Нормализация модели — центрирование и проверка
    minBB = glm::vec3(FLT_MAX);
    maxBB = glm::vec3(-FLT_MAX);
    bool hasValid = false;
    for (size_t i = 0; i + 2 < vertices.size(); i += 3) {
        float x = vertices[i], y = vertices[i+1], z = vertices[i+2];
        if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) continue;
        glm::vec3 v(x,y,z);
        minBB = glm::min(minBB, v);
        maxBB = glm::max(maxBB, v);
        hasValid = true;
    }
    if (!hasValid) {
        std::cerr << "[Model] No valid vertices" << std::endl;
        return false;
    }
    center = (minBB + maxBB) * 0.5f;
    glm::vec3 size = maxBB - minBB;
    maxDim = glm::length(size);
    if (maxDim < 0.0001f || !std::isfinite(maxDim)) maxDim = 1.0f;

    // Проверка нормалей
    int badNormals = 0;
    for (size_t i = 0; i + 2 < normals.size(); i += 3) {
        float nx = normals[i], ny = normals[i+1], nz = normals[i+2];
        if (!std::isfinite(nx) || !std::isfinite(ny) || !std::isfinite(nz)) {
            // Пересчитаем нормаль из треугольника
            size_t triStart = (i/9)*9;
            if (triStart + 8 < vertices.size()) {
                glm::vec3 v0(vertices[triStart], vertices[triStart+1], vertices[triStart+2]);
                glm::vec3 v1(vertices[triStart+3], vertices[triStart+4], vertices[triStart+5]);
                glm::vec3 v2(vertices[triStart+6], vertices[triStart+7], vertices[triStart+8]);
                glm::vec3 n = glm::cross(v1-v0, v2-v0);
                float len = glm::length(n);
                if (len > 1e-9f) n /= len; else n = glm::vec3(0,1,0);
                normals[i]=n.x; normals[i+1]=n.y; normals[i+2]=n.z;
                badNormals++;
            }
        } else {
            float len = sqrtf(nx*nx+ny*ny+nz*nz);
            if (len < 1e-6f || fabsf(len-1.0f) > 0.1f) {
                if (len > 1e-9f) {
                    normals[i]/=len; normals[i+1]/=len; normals[i+2]/=len;
                } else {
                    size_t triStart = (i/9)*9;
                    if (triStart + 8 < vertices.size()) {
                        glm::vec3 v0(vertices[triStart], vertices[triStart+1], vertices[triStart+2]);
                        glm::vec3 v1(vertices[triStart+3], vertices[triStart+4], vertices[triStart+5]);
                        glm::vec3 v2(vertices[triStart+6], vertices[triStart+7], vertices[triStart+8]);
                        glm::vec3 n = glm::cross(v1-v0, v2-v0);
                        float l = glm::length(n);
                        if (l > 1e-9f) n/=l; else n=glm::vec3(0,1,0);
                        normals[i]=n.x; normals[i+1]=n.y; normals[i+2]=n.z;
                    }
                }
                badNormals++;
            }
        }
    }
    if (badNormals > 0) {
        std::cout << "[Model] Fixed " << badNormals << " normals" << std::endl;
    }

    g_vertices = vertices;
    g_normals = normals;
    // Исправлено: g_vertexColors должен быть numVerts*3, а не vertices.size() заполненный 0.75
    g_vertexColors.assign(vertices.size(), 0.75f);
    // Сделаем цвет чуть более интересным по умолчанию
    for (size_t i = 0; i + 2 < g_vertexColors.size(); i += 3) {
        g_vertexColors[i] = 0.75f + 0.05f*sinf((float)i*0.1f);
        g_vertexColors[i+1] = 0.75f + 0.05f*cosf((float)i*0.13f);
        g_vertexColors[i+2] = 0.78f;
    }

    // Камера
    cameraPos = center + glm::vec3(maxDim*1.8f, maxDim*0.8f, maxDim*1.8f);
    cameraFront = glm::normalize(center - cameraPos);
    if (!std::isfinite(cameraFront.x) || glm::length(cameraFront) < 1e-6f) {
        cameraFront = glm::vec3(0,0,-1);
    }
    yaw = glm::degrees(atan2f(cameraFront.z, cameraFront.x));
    pitch = glm::degrees(asinf(glm::clamp(cameraFront.y, -0.99f, 0.99f)));

    // VAO
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

    std::cout << "[Model] Loaded: " << path << " verts=" << modelVertexCount 
              << " tris=" << modelVertexCount/3 
              << " BB=[" << minBB.x << "," << minBB.y << "," << minBB.z << "]-["
              << maxBB.x << "," << maxBB.y << "," << maxBB.z << "]"
              << " maxDim=" << maxDim << std::endl;

    createBoundingBoxVAO();
    createAxesVAO(maxDim * 0.6f);
    createGroundPlane(maxDim * 5.0f);
    createGrid(maxDim * 5.0f, 20);

    auto tVox0 = std::chrono::high_resolution_clock::now();
    buildVoxelGrid(vertices, voxelResolution);
    auto tVox1 = std::chrono::high_resolution_clock::now();
    perfVoxelMs = std::chrono::duration<float, std::milli>(tVox1-tVox0).count();

    // Авто ref area
    if (aeroAutoRefArea) {
        aeroRefArea = (maxBB.y - minBB.y) * (maxBB.z - minBB.z) * 0.6f;
        if (aeroRefArea < 0.01f) aeroRefArea = maxDim*maxDim*0.5f;
        if (aeroRefArea < 1e-6f) aeroRefArea = 1.0f;
        std::cout << "[Model] Auto ref area: " << aeroRefArea << " m2" << std::endl;
    }

    // Reynolds number на основе модели
    {
        float L = maxDim;
        float V = flowSpeed;
        float nu = 1.5e-5f; // кинематическая вязкость воздуха
        if (airDensity > 1e-6f) {
            // mu/rho
            float mu = 1.81e-5f;
            nu = mu / airDensity;
        }
        aeroReNumber = V * L / (nu + 1e-9f);
        std::cout << "[Model] Re = " << aeroReNumber << " (V=" << V << " L=" << L << " nu=" << nu << ")" << std::endl;
    }

    if (lbmParams.enabled) {
        initLBM();
    }

    initParticles();
    computeStreamlines();
    updateVertexColors();
    computeLiftDrag();
    updateLiftDragArrows();
    return true;
}
