#include <glad/glad.h>
#include <glm/glm.hpp>

#include <cmath>
#include <vector>
#include <iostream>
#include <chrono>
#include <algorithm>

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
// Линии тока — v1.8.0 Realistic Aero + фиксы
// =====================================================

inline glm::vec3 rk4StepOpt(const glm::vec3& p, float h, const FlowParams& prm) {
    if (!std::isfinite(p.x) || h < 1e-8f) return p;
    glm::vec3 v1 = computeVelocityFieldCPU(p, prm);
    float m1 = v1.x*v1.x + v1.y*v1.y + v1.z*v1.z;
    if (m1 < 1e-16f || !std::isfinite(m1)) return p;
    float inv1 = 1.0f / sqrtf(m1);
    if (!std::isfinite(inv1)) return p;
    glm::vec3 k1 = v1 * inv1;

    glm::vec3 p2 = p + k1 * (h*0.5f);
    if (!std::isfinite(p2.x)) return p + k1*h;
    glm::vec3 v2 = computeVelocityFieldCPU(p2, prm);
    float m2 = v2.x*v2.x + v2.y*v2.y + v2.z*v2.z;
    if (m2 < 1e-16f || !std::isfinite(m2)) return p + k1*h;
    float inv2 = 1.0f / sqrtf(m2);
    if (!std::isfinite(inv2)) return p + k1*h;
    glm::vec3 k2 = v2 * inv2;

    glm::vec3 p3 = p + k2 * (h*0.5f);
    if (!std::isfinite(p3.x)) return p + k2*h;
    glm::vec3 v3 = computeVelocityFieldCPU(p3, prm);
    float m3 = v3.x*v3.x + v3.y*v3.y + v3.z*v3.z;
    if (m3 < 1e-16f || !std::isfinite(m3)) return p + k2*h;
    float inv3 = 1.0f / sqrtf(m3);
    if (!std::isfinite(inv3)) return p + k2*h;
    glm::vec3 k3 = v3 * inv3;

    glm::vec3 p4 = p + k3 * h;
    if (!std::isfinite(p4.x)) return p + k3*h;
    glm::vec3 v4 = computeVelocityFieldCPU(p4, prm);
    float m4 = v4.x*v4.x + v4.y*v4.y + v4.z*v4.z;
    if (m4 < 1e-16f || !std::isfinite(m4)) return p + k3*h;
    float inv4 = 1.0f / sqrtf(m4);
    if (!std::isfinite(inv4)) return p + k3*h;
    glm::vec3 k4 = v4 * inv4;

    glm::vec3 dir = (k1 + 2.0f*k2 + 2.0f*k3 + k4) * (1.0f/6.0f);
    float len2 = dir.x*dir.x + dir.y*dir.y + dir.z*dir.z;
    if (len2 < 1e-16f || !std::isfinite(len2)) dir = k1;
    else {
        float inv = 1.0f / sqrtf(len2);
        if (!std::isfinite(inv)) dir = k1;
        else dir *= inv;
    }
    glm::vec3 res = p + dir * h;
    if (!std::isfinite(res.x)) return p;
    return res;
}

void computeStreamlines() {
    updateFlowParams();
    if (maxDim < 0.001f || !std::isfinite(maxDim)) return;

    auto t0 = std::chrono::high_resolution_clock::now();

    glm::vec3 flowDir(flowParams.vx, flowParams.vy, flowParams.vz);
    float flowLen = glm::length(flowDir);
    if (flowLen < 1e-6f || !std::isfinite(flowLen)) flowDir = glm::vec3(1,0,0);
    else flowDir /= flowLen;

    glm::vec3 upRef(0.0f, 1.0f, 0.0f);
    if (fabs(glm::dot(upRef, flowDir)) > 0.95f) upRef = glm::vec3(1.0f, 0.0f, 0.0f);
    glm::vec3 right = glm::cross(flowDir, upRef);
    float rl = glm::length(right);
    if (rl < 1e-6f || !std::isfinite(rl)) right = glm::vec3(0,0,1);
    else right = glm::normalize(right);
    glm::vec3 up = glm::cross(right, flowDir);
    float ul = glm::length(up);
    if (ul < 1e-6f || !std::isfinite(ul)) up = glm::vec3(0,1,0);
    else up = glm::normalize(up);

    float startDist = maxDim * 0.8f;
    if (!std::isfinite(startDist)) startDist = 1.0f;
    glm::vec3 startPlaneCenter = center - flowDir * startDist;
    startPlaneCenter.x = glm::clamp(startPlaneCenter.x, flowParams.minX + 0.1f*maxDim, flowParams.maxX - 0.1f*maxDim);
    startPlaneCenter.y = glm::clamp(startPlaneCenter.y, flowParams.minY + 0.1f*maxDim, flowParams.maxY - 0.1f*maxDim);
    startPlaneCenter.z = glm::clamp(startPlaneCenter.z, flowParams.minZ + 0.1f*maxDim, flowParams.maxZ - 0.1f*maxDim);
    if (!std::isfinite(startPlaneCenter.x)) startPlaneCenter = center - flowDir*maxDim;

    float spread = maxDim * 0.8f;
    if (!std::isfinite(spread) || spread < 1e-6f) spread = 1.0f;
    int grid = (int)ceilf(sqrtf((float)numStreamlines));
    if (grid < 1) grid = 1;
    if (grid > 30) grid = 30;

    std::vector<glm::vec3> startPoints;
    startPoints.reserve(numStreamlines);
    for (int gy = 0; gy < grid && (int)startPoints.size() < numStreamlines; gy++) {
        for (int gx = 0; gx < grid && (int)startPoints.size() < numStreamlines; gx++) {
            float fx = (grid <= 1) ? 0.0f : ((float)gx/(grid-1) - 0.5f) * 2.0f;
            float fy = (grid <= 1) ? 0.0f : ((float)gy/(grid-1) - 0.5f) * 2.0f;
            glm::vec3 sp = startPlaneCenter + right*(fx*spread) + up*(fy*spread);
            if (!std::isfinite(sp.x)) continue;
            startPoints.push_back(sp);
        }
    }

    int numLines = (int)startPoints.size();
    if (numLines == 0) return;
    std::vector<std::vector<float>> localVerts(numLines);
    float maxSpeed = flowParams.maxSpeed;
    if (!std::isfinite(maxSpeed) || maxSpeed < 1e-3f) maxSpeed = flowSpeed * 1.5f;
    if (maxSpeed < 1e-6f) maxSpeed = 1.0f;
    float groundY = g_voxMinY + aeroGroundHeight;

    #ifdef _OPENMP
    #pragma omp parallel for schedule(dynamic)
    #endif
    for (int li = 0; li < numLines; ++li) {
        glm::vec3 start = startPoints[li];
        std::vector<float> verts;
        verts.reserve(streamlineSteps * 12);

        glm::vec3 p = start, prev = p;
        if (!std::isfinite(p.x)) continue;
        glm::vec3 v_prev = computeVelocityFieldCPU(prev, flowParams);
        if (!std::isfinite(v_prev.x)) v_prev = flowDir * flowSpeed;
        float speedPrev = glm::length(v_prev);
        if (!std::isfinite(speedPrev)) speedPrev = flowSpeed;
        glm::vec3 c_prev;
        if (aeroColorStreamlinesByVelocity) {
            if (lbmParams.enabled && lbmInitialized) {
                float velMag = getLBMVelocityMagWorld(prev);
                if (!std::isfinite(velMag)) velMag = speedPrev;
                c_prev = getVelocityMagnitudeColor(velMag, maxSpeed);
            } else {
                c_prev = getVelocityMagnitudeColor(speedPrev, maxSpeed);
            }
        } else {
            float d_prev = sampleSDFCPU(prev);
            c_prev = colorForPoint(v_prev, d_prev, flowParams);
        }
        if (!std::isfinite(c_prev.x)) c_prev = glm::vec3(0.3f,0.8f,1.0f);

        for (int s = 0; s < streamlineSteps; s++) {
            glm::vec3 v = computeVelocityFieldCPU(p, flowParams);
            if (!std::isfinite(v.x)) break;
            float sp2 = v.x*v.x + v.y*v.y + v.z*v.z;
            if (sp2 < 1e-12f || !std::isfinite(sp2)) break;
            float distToCenter = glm::length(p - center);
            if (!std::isfinite(distToCenter)) break;
            float stepLen = streamlineStepSize;
            if (!std::isfinite(stepLen) || stepLen < 1e-6f) stepLen = 0.08f;
            if (distToCenter < maxDim*1.5f) stepLen *= 0.5f;
            if (distToCenter < maxDim*0.9f) stepLen *= 0.5f;
            if (lbmParams.enabled) stepLen *= 0.7f;

            glm::vec3 pNext = rk4StepOpt(p, stepLen, flowParams);
            if (!std::isfinite(pNext.x)) break;

            // Ground collision for streamlines
            if (aeroGroundEffect && pNext.y < groundY) {
                pNext.y = groundY + flowParams.cellSizeY;
            }

            float bigMargin = maxDim * 2.0f;
            if (!std::isfinite(bigMargin)) bigMargin = 10.0f;
            if (pNext.x < flowParams.minX - bigMargin || pNext.x > flowParams.maxX + bigMargin ||
                pNext.y < flowParams.minY - bigMargin || pNext.y > flowParams.maxY + bigMargin ||
                pNext.z < flowParams.minZ - bigMargin || pNext.z > flowParams.maxZ + bigMargin)
                break;

            float speed = sqrtf(sp2);
            if (!std::isfinite(speed)) speed = flowSpeed;
            glm::vec3 c_p;
            if (aeroColorStreamlinesByVelocity) {
                if (lbmParams.enabled && lbmInitialized) {
                    float velMag = getLBMVelocityMagWorld(pNext);
                    if (!std::isfinite(velMag)) velMag = speed;
                    if (aeroGroundEffect && pNext.y < center.y) {
                        velMag *= 1.2f;
                    }
                    c_p = getVelocityMagnitudeColor(velMag, maxSpeed);
                } else {
                    float velMag = speed;
                    if (aeroGroundEffect && pNext.y < center.y) velMag *= 1.2f;
                    c_p = getVelocityMagnitudeColor(velMag, maxSpeed);
                }
            } else {
                float d_p = sampleSDFCPU(pNext);
                c_p = colorForPoint(v, d_p, flowParams);
            }
            if (!std::isfinite(c_p.x)) c_p = glm::vec3(0.5f);

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

    std::cout << "Streamlines v1.8.0 REALISTIC (RK4" << (lbmParams.enabled ? "+LBM" : "") << (aeroColorStreamlinesByVelocity ? "+VelColor" : "") << "): " << numLines << " lines, " << (verts.size()/6) << " vertices in " << perfStreamlinesMs << " ms" << std::endl;
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
