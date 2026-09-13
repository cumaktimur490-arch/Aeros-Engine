#include <glad/glad.h>
#include <glm/glm.hpp>

#include <cmath>
#include <vector>
#include <iostream>

#include "globals.h"
#include "flow_field.h"
#include "voxel_grid.h"
#include "streamlines.h"

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

