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
// Линии тока — v1.11.0 Physics Fix
// Исправлено:
// - RK4 теперь использует реальную скорость, а не нормированное направление
// - Проверка SDF на каждом подшаге чтобы не заходить в тело
// - Адаптивный шаг по скорости и кривизне
// =====================================================

inline glm::vec3 rk4StepPhys(const glm::vec3& p, float dt, const FlowParams& prm) {
    if (!std::isfinite(p.x) || dt < 1e-8f) return p;

    auto safeVel = [&](const glm::vec3& pos)->glm::vec3 {
        if (!std::isfinite(pos.x)) return glm::vec3(0);
        // Если внутри тела — 0
        if (!g_distanceField.empty()) {
            float d = sampleSDFCPU(pos);
            if (d <= 0.0f) return glm::vec3(0.0f);
        }
        glm::vec3 v = computeVelocityFieldCPU(pos, prm);
        if (!std::isfinite(v.x)) return glm::vec3(0);
        // Ограничиваем скорость чтобы не было взрыва
        float mag2 = glm::dot(v,v);
        float maxV = prm.maxSpeed * 2.0f;
        if (mag2 > maxV*maxV) v *= maxV / sqrtf(mag2);
        return v;
    };

    glm::vec3 v1 = safeVel(p);
    if (glm::length(v1) < 1e-8f) return p;
    glm::vec3 k1 = v1 * dt;

    glm::vec3 p2 = p + k1 * 0.5f;
    glm::vec3 v2 = safeVel(p2);
    if (glm::length(v2) < 1e-8f) return p + k1;
    glm::vec3 k2 = v2 * dt;

    glm::vec3 p3 = p + k2 * 0.5f;
    glm::vec3 v3 = safeVel(p3);
    if (glm::length(v3) < 1e-8f) return p + k2;
    glm::vec3 k3 = v3 * dt;

    glm::vec3 p4 = p + k3;
    glm::vec3 v4 = safeVel(p4);
    if (glm::length(v4) < 1e-8f) return p + k3;
    glm::vec3 k4 = v4 * dt;

    glm::vec3 res = p + (k1 + 2.0f*k2 + 2.0f*k3 + k4) * (1.0f/6.0f);
    if (!std::isfinite(res.x)) return p + k1;
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

    float startDist = maxDim * 0.9f;
    if (!std::isfinite(startDist)) startDist = 1.0f;
    glm::vec3 startPlaneCenter = center - flowDir * startDist;
    startPlaneCenter.x = glm::clamp(startPlaneCenter.x, flowParams.minX + 0.1f*maxDim, flowParams.maxX - 0.1f*maxDim);
    startPlaneCenter.y = glm::clamp(startPlaneCenter.y, flowParams.minY + 0.1f*maxDim, flowParams.maxY - 0.1f*maxDim);
    startPlaneCenter.z = glm::clamp(startPlaneCenter.z, flowParams.minZ + 0.1f*maxDim, flowParams.maxZ - 0.1f*maxDim);
    if (!std::isfinite(startPlaneCenter.x)) startPlaneCenter = center - flowDir*maxDim;

    float spread = maxDim * 0.7f;
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
            // Не стартуем внутри тела
            if (!g_distanceField.empty() && sampleSDFCPU(sp) <= 0.0f) continue;
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
            float speed = glm::length(v);
            if (speed < 1e-6f) break; // застойная точка — конец линии
            if (!std::isfinite(speed)) break;

            float distToCenter = glm::length(p - center);
            if (!std::isfinite(distToCenter)) break;

            // Адаптивный шаг: dt = stepSize / speed, чтобы шаг по длине был ~const
            float baseStep = streamlineStepSize;
            if (!std::isfinite(baseStep) || baseStep < 1e-6f) baseStep = 0.08f;
            // Уменьшаем шаг вблизи тела для точности
            if (distToCenter < maxDim*1.2f) baseStep *= 0.4f;
            if (distToCenter < maxDim*0.6f) baseStep *= 0.5f;
            if (lbmParams.enabled) baseStep *= 0.8f;

            float dt = baseStep / (speed + 0.1f*flowSpeed); // нормируем на скорость
            // Ограничиваем dt чтобы не было слишком больших прыжков
            if (dt > 0.1f) dt = 0.1f;
            if (dt < 0.001f) dt = 0.001f;

            glm::vec3 pNext = rk4StepPhys(p, dt, flowParams);
            if (!std::isfinite(pNext.x)) break;

            // Проверка на попадание в тело
            if (!g_distanceField.empty()) {
                float d = sampleSDFCPU(pNext);
                if (d <= 0.0f) {
                    // Скользим по поверхности
                    glm::vec3 n = sdfNormalCPU(pNext);
                    if (glm::length(n) > 1e-6f) {
                        n = glm::normalize(n);
                        // Выталкиваем наружу
                        pNext += n * (fabsf(d) + flowParams.cellSizeX*0.6f);
                        // Проверяем еще раз
                        if (sampleSDFCPU(pNext) <= 0.0f) break; // застряли — конец
                    } else {
                        break;
                    }
                }
            }

            if (aeroGroundEffect && pNext.y < groundY) {
                pNext.y = groundY + flowParams.cellSizeY*0.5f;
            }

            float bigMargin = maxDim * 2.5f;
            if (!std::isfinite(bigMargin)) bigMargin = 10.0f;
            if (pNext.x < flowParams.minX - bigMargin || pNext.x > flowParams.maxX + bigMargin ||
                pNext.y < flowParams.minY - bigMargin || pNext.y > flowParams.maxY + bigMargin ||
                pNext.z < flowParams.minZ - bigMargin || pNext.z > flowParams.maxZ + bigMargin)
                break;

            float speedNext = glm::length(computeVelocityFieldCPU(pNext, flowParams));
            if (!std::isfinite(speedNext)) speedNext = speed;

            glm::vec3 c_p;
            if (aeroColorStreamlinesByVelocity) {
                if (lbmParams.enabled && lbmInitialized) {
                    float velMag = getLBMVelocityMagWorld(pNext);
                    if (!std::isfinite(velMag)) velMag = speedNext;
                    c_p = getVelocityMagnitudeColor(velMag, maxSpeed);
                } else {
                    c_p = getVelocityMagnitudeColor(speedNext, maxSpeed);
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

    std::cout << "Streamlines v1.11.0 Physics Fix (RK4 phys" << (lbmParams.enabled ? "+LBM" : "") << (aeroColorStreamlinesByVelocity ? "+VelColor" : "") << "): " << numLines << " lines, " << (verts.size()/6) << " vertices in " << perfStreamlinesMs << " ms" << std::endl;
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
