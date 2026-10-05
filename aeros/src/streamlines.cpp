#include <glad/glad.h>
#include <glm/glm.hpp>

#include <cmath>
#include <vector>
#include <iostream>
#include <chrono>

#include "globals.h"
#include "flow_field.h"
#include "voxel_grid.h"
#include "streamlines.h"
#include "lbm.h"
#include "forces.h"

#ifdef _OPENMP
#include <omp.h>
#endif

// =====================================================
// Линии тока — v1.7.0 Realistic Aero как на фото
// =====================================================

inline glm::vec3 rk4StepOpt(const glm::vec3& p, float h, const FlowParams& prm) {
    glm::vec3 v1 = computeVelocityFieldCPU(p, prm);
    float m1 = v1.x*v1.x + v1.y*v1.y + v1.z*v1.z;
    if (m1 < 1e-16f) return p;
    glm::vec3 k1 = v1 * (1.0f / sqrtf(m1));

    glm::vec3 p2 = p + k1 * (h*0.5f);
    glm::vec3 v2 = computeVelocityFieldCPU(p2, prm);
    float m2 = v2.x*v2.x + v2.y*v2.y + v2.z*v2.z;
    if (m2 < 1e-16f) return p + k1*h;
    glm::vec3 k2 = v2 * (1.0f / sqrtf(m2));

    glm::vec3 p3 = p + k2 * (h*0.5f);
    glm::vec3 v3 = computeVelocityFieldCPU(p3, prm);
    float m3 = v3.x*v3.x + v3.y*v3.y + v3.z*v3.z;
    if (m3 < 1e-16f) return p + k2*h;
    glm::vec3 k3 = v3 * (1.0f / sqrtf(m3));

    glm::vec3 p4 = p + k3 * h;
    glm::vec3 v4 = computeVelocityFieldCPU(p4, prm);
    float m4 = v4.x*v4.x + v4.y*v4.y + v4.z*v4.z;
    if (m4 < 1e-16f) return p + k3*h;
    glm::vec3 k4 = v4 * (1.0f / sqrtf(m4));

    glm::vec3 dir = (k1 + 2.0f*k2 + 2.0f*k3 + k4) * (1.0f/6.0f);
    float len2 = dir.x*dir.x + dir.y*dir.y + dir.z*dir.z;
    if (len2 < 1e-16f) dir = k1;
    else dir *= 1.0f / sqrtf(len2);
    return p + dir * h;
}

void computeStreamlines() {
    updateFlowParams();
    if (maxDim < 0.001f) return;

    auto t0 = std::chrono::high_resolution_clock::now();

    glm::vec3 flowDir(flowParams.vx, flowParams.vy, flowParams.vz);
    float flowLen = glm::length(flowDir);
    if (flowLen < 1e-6f) flowDir = glm::vec3(1,0,0);
    else flowDir /= flowLen;

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

    std::vector<glm::vec3> startPoints;
    startPoints.reserve(numStreamlines);
    for (int gy = 0; gy < grid && (int)startPoints.size() < numStreamlines; gy++) {
        for (int gx = 0; gx < grid && (int)startPoints.size() < numStreamlines; gx++) {
            float fx = (grid <= 1) ? 0.0f : ((float)gx/(grid-1) - 0.5f) * 2.0f;
            float fy = (grid <= 1) ? 0.0f : ((float)gy/(grid-1) - 0.5f) * 2.0f;
            startPoints.push_back(startPlaneCenter + right*(fx*spread) + up*(fy*spread));
        }
    }

    int numLines = (int)startPoints.size();
    std::vector<std::vector<float>> localVerts(numLines);
    float maxSpeed = flowParams.maxSpeed;
    if (maxSpeed < 1e-3f) maxSpeed = flowSpeed * 1.5f;

    #ifdef _OPENMP
    #pragma omp parallel for
    #endif
    for (int li = 0; li < numLines; ++li) {
        glm::vec3 start = startPoints[li];
        std::vector<float> verts;
        verts.reserve(streamlineSteps * 12);

        glm::vec3 p = start, prev = p;
        glm::vec3 v_prev = computeVelocityFieldCPU(prev, flowParams);
        float speedPrev = glm::length(v_prev);
        glm::vec3 c_prev;
        if (aeroColorStreamlinesByVelocity) {
            // Как на фото 2 — цвет по скорости
            if (lbmParams.enabled && lbmInitialized) {
                float velMag = getLBMVelocityMagWorld(prev);
                c_prev = getVelocityMagnitudeColor(velMag, maxSpeed);
            } else {
                c_prev = getVelocityMagnitudeColor(speedPrev, maxSpeed);
            }
        } else {
            float d_prev = sampleSDFCPU(prev);
            c_prev = colorForPoint(v_prev, d_prev, flowParams);
        }

        for (int s = 0; s < streamlineSteps; s++) {
            glm::vec3 v = computeVelocityFieldCPU(p, flowParams);
            float sp2 = v.x*v.x + v.y*v.y + v.z*v.z;
            if (sp2 < 1e-12f) break;
            float distToCenter = glm::length(p - center);
            float stepLen = streamlineStepSize;
            if (distToCenter < maxDim*1.5f) stepLen *= 0.5f;
            if (distToCenter < maxDim*0.9f) stepLen *= 0.5f;
            if (lbmParams.enabled) stepLen *= 0.7f;

            glm::vec3 pNext = rk4StepOpt(p, stepLen, flowParams);

            float bigMargin = maxDim * 2.0f;
            if (pNext.x < flowParams.minX - bigMargin || pNext.x > flowParams.maxX + bigMargin ||
                pNext.y < flowParams.minY - bigMargin || pNext.y > flowParams.maxY + bigMargin ||
                pNext.z < flowParams.minZ - bigMargin || pNext.z > flowParams.maxZ + bigMargin)
                break;

            float speed = sqrtf(sp2);
            glm::vec3 c_p;
            if (aeroColorStreamlinesByVelocity) {
                if (lbmParams.enabled && lbmInitialized) {
                    float velMag = getLBMVelocityMagWorld(pNext);
                    // Для авто — подсветка ускорения под днищем как на фото 2
                    if (aeroGroundEffect && pNext.y < center.y) {
                        // Ускорение под авто — более яркий
                        velMag *= 1.2f;
                    }
                    c_p = getVelocityMagnitudeColor(velMag, maxSpeed);
                } else {
                    c_p = getVelocityMagnitudeColor(speed, maxSpeed);
                }
            } else {
                float d_p = sampleSDFCPU(pNext);
                c_p = colorForPoint(v, d_p, flowParams);
            }

            // Добавляем эффект затухания за моделью как на фото 5 (синий след)
            float along = glm::dot(pNext - center, flowDir);
            if (along > maxDim*0.5f) {
                // В следе — смешиваем с синим для визуализации следа
                float wakeFactor = glm::clamp((along - maxDim*0.5f) / (maxDim*2.0f), 0.0f, 0.6f);
                if (aeroVisMode == AeroVisMode::VelocityMagnitude) {
                    // В следе скорость ниже — уже синий
                }
            }

            verts.push_back(prev.x); verts.push_back(prev.y); verts.push_back(prev.z);
            verts.push_back(c_prev.x); verts.push_back(c_prev.y); verts.push_back(c_prev.z);
            verts.push_back(pNext.x); verts.push_back(pNext.y); verts.push_back(pNext.z);
            verts.push_back(c_p.x); verts.push_back(c_p.y); verts.push_back(c_p.z);

            prev = pNext; p = pNext; c_prev = c_p;
        }
        localVerts[li] = std::move(verts);
    }

    size_t totalFloats = 0;
    for (auto& lv : localVerts) totalFloats += lv.size();
    std::vector<float> verts;
    verts.reserve(totalFloats);
    for (auto& lv : localVerts) verts.insert(verts.end(), lv.begin(), lv.end());

    auto t1 = std::chrono::high_resolution_clock::now();
    perfStreamlinesMs = std::chrono::duration<float, std::milli>(t1-t0).count();

    std::cout << "Streamlines REALISTIC (RK4" << (lbmParams.enabled ? "+LBM" : "") << (aeroColorStreamlinesByVelocity ? "+VelColor" : "") << "): " << numLines << " lines, " << (verts.size()/6) << " vertices in " << perfStreamlinesMs << " ms" << std::endl;
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
