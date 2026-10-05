#include <glad/glad.h>
#include <glm/glm.hpp>

#include <cmath>
#include <vector>
#include <iostream>

#include "globals.h"
#include "flow_field.h"
#include "voxel_grid.h"
#include "streamlines.h"
#include "lbm.h"

// =====================================================
// Линии тока — v1.5.0 LBM + RK4 интеграция
// =====================================================

// RK4 шаг для более точных линий тока
static glm::vec3 rk4Step(const glm::vec3& p, float h, const FlowParams& prm) {
    glm::vec3 v1 = computeVelocityFieldCPU(p, prm);
    float m1 = glm::length(v1);
    if (m1 < 1e-8f) return p;
    glm::vec3 k1 = glm::normalize(v1);

    glm::vec3 p2 = p + k1 * (h*0.5f);
    glm::vec3 v2 = computeVelocityFieldCPU(p2, prm);
    float m2 = glm::length(v2);
    if (m2 < 1e-8f) return p + k1*h;
    glm::vec3 k2 = glm::normalize(v2);

    glm::vec3 p3 = p + k2 * (h*0.5f);
    glm::vec3 v3 = computeVelocityFieldCPU(p3, prm);
    float m3 = glm::length(v3);
    if (m3 < 1e-8f) return p + k2*h;
    glm::vec3 k3 = glm::normalize(v3);

    glm::vec3 p4 = p + k3 * h;
    glm::vec3 v4 = computeVelocityFieldCPU(p4, prm);
    float m4 = glm::length(v4);
    if (m4 < 1e-8f) return p + k3*h;
    glm::vec3 k4 = glm::normalize(v4);

    glm::vec3 dir = (k1 + 2.0f*k2 + 2.0f*k3 + k4) / 6.0f;
    float len = glm::length(dir);
    if (len < 1e-8f) dir = k1;
    else dir /= len;
    return p + dir * h;
}

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
            // LBM: если включен, окраска по завихренности/Q
            if (lbmParams.enabled && lbmInitialized) {
                float vort = 0;
                // примерная оценка завихренности из LBM если есть
                // используем vorticityMag из ближайшей ячейки
                glm::vec3 vLB = getLBMVelocityWorld(prev);
                c_prev = colorForPoint(vLB, d_prev, flowParams);
            }

            for (int s = 0; s < streamlineSteps; s++) {
                glm::vec3 v = computeVelocityFieldCPU(p, flowParams);
                float sp = glm::length(v);
                if (sp < 1e-6f) break;
                float distToCenter = glm::length(p - center);
                float stepLen = streamlineStepSize;
                // адаптивный шаг: ближе к модели — меньше
                if (distToCenter < maxDim*1.5f) stepLen *= 0.5f;
                if (distToCenter < maxDim*0.9f) stepLen *= 0.5f;
                if (lbmParams.enabled) stepLen *= 0.7f; // LBM поле более детальное — шаг меньше

                // RK4 интеграция
                glm::vec3 pNext = rk4Step(p, stepLen, flowParams);

                float bigMargin = maxDim * 2.0f;
                if (pNext.x < flowParams.minX - bigMargin || pNext.x > flowParams.maxX + bigMargin ||
                    pNext.y < flowParams.minY - bigMargin || pNext.y > flowParams.maxY + bigMargin ||
                    pNext.z < flowParams.minZ - bigMargin || pNext.z > flowParams.maxZ + bigMargin)
                    break;

                float d_p = sampleSDFCPU(pNext);
                glm::vec3 c_p = colorForPoint(v, d_p, flowParams);

                verts.push_back(prev.x); verts.push_back(prev.y); verts.push_back(prev.z);
                verts.push_back(c_prev.x); verts.push_back(c_prev.y); verts.push_back(c_prev.z);
                verts.push_back(pNext.x); verts.push_back(pNext.y); verts.push_back(pNext.z);
                verts.push_back(c_p.x); verts.push_back(c_p.y); verts.push_back(c_p.z);

                prev = pNext; p = pNext; c_prev = c_p;
            }
            linesDrawn++;
        }
    }

    std::cout << "Streamlines (RK4" << (lbmParams.enabled ? "+LBM" : "") << "): " << linesDrawn << " lines, " << (verts.size()/6) << " vertices" << std::endl;
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
