#include <glad/glad.h>
#include <glm/glm.hpp>

#include <iostream>
#include <vector>

#include "globals.h"
#include "stl_loader.h"
#include "gl_utils.h"
#include "voxel_grid.h"
#include "particles.h"
#include "streamlines.h"
#include "forces.h"
#include "model.h"

// =====================================================
// Загрузка модели
// =====================================================
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
