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
// Линии тока — v1.17.0 Physics Logic Fix — аэродинамика
// - RK45 адаптивный с контролем ошибки
// - Сидинг по аэродинамике: LE + вихревые зоны
// - Терминация: стагнация, вихрь, выход, поверхность, отрыв
// - Использует данные: Re, Mach, Cp, vorticity
// =====================================================

inline glm::vec3 safeVelocity(const glm::vec3& pos, const FlowParams& prm) {
    if (!std::isfinite(pos.x)) return glm::vec3(0);
    if (!g_distanceField.empty()) {
        float d = sampleSDFCPU(pos);
        if (d <= 0.0f) return glm::vec3(0.0f);
    }
    glm::vec3 v = computeVelocityFieldCPU(pos, prm);
    if (!std::isfinite(v.x)) return glm::vec3(0);
    float mag2 = glm::dot(v,v);
    float maxV = prm.maxSpeed * 2.2f;
    if (mag2 > maxV*maxV) v *= maxV / sqrtf(mag2);
    return v;
}

// RK4 с физичной скоростью
inline glm::vec3 rk4StepPhys(const glm::vec3& p, float dt, const FlowParams& prm) {
    if (!std::isfinite(p.x) || dt < 1e-8f) return p;
    glm::vec3 v1 = safeVelocity(p, prm);
    if (glm::length(v1) < 1e-8f) return p;
    glm::vec3 k1 = v1 * dt;
    glm::vec3 p2 = p + k1 * 0.5f;
    glm::vec3 v2 = safeVelocity(p2, prm);
    if (glm::length(v2) < 1e-8f) return p + k1;
    glm::vec3 k2 = v2 * dt;
    glm::vec3 p3 = p + k2 * 0.5f;
    glm::vec3 v3 = safeVelocity(p3, prm);
    if (glm::length(v3) < 1e-8f) return p + k2;
    glm::vec3 k3 = v3 * dt;
    glm::vec3 p4 = p + k3;
    glm::vec3 v4 = safeVelocity(p4, prm);
    if (glm::length(v4) < 1e-8f) return p + k3;
    glm::vec3 k4 = v4 * dt;
    glm::vec3 res = p + (k1 + 2.0f*k2 + 2.0f*k3 + k4) * (1.0f/6.0f);
    if (!std::isfinite(res.x)) return p + k1;
    return res;
}

// RK45 с оценкой ошибки для адаптивного шага
inline std::pair<glm::vec3,float> rk45Step(const glm::vec3& p, float dt, const FlowParams& prm) {
    // RK4
    glm::vec3 v1 = safeVelocity(p, prm);
    glm::vec3 k1 = v1 * dt;
    glm::vec3 v2 = safeVelocity(p + k1*0.5f, prm);
    glm::vec3 k2 = v2 * dt;
    glm::vec3 v3 = safeVelocity(p + k2*0.5f, prm);
    glm::vec3 k3 = v3 * dt;
    glm::vec3 v4 = safeVelocity(p + k3, prm);
    glm::vec3 k4 = v4 * dt;
    glm::vec3 rk4 = p + (k1 + 2.0f*k2 + 2.0f*k3 + k4)*(1.0f/6.0f);

    // RK5 (Dormand-Prince упрощенный) — оценка через midpoint
    glm::vec3 vMid = safeVelocity(p + k1*0.5f + k2*0.25f, prm);
    glm::vec3 kMid = vMid * dt;
    glm::vec3 rk5 = p + (k1*0.1f + k2*0.2f + kMid*0.3f + k3*0.2f + k4*0.2f);

    float err = glm::length(rk4 - rk5);
    return {rk4, err};
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

    // Аэродинамический сидинг: передняя кромка + зоны отрыва
    float startDist = maxDim * 1.1f;
    if (!std::isfinite(startDist)) startDist = 1.0f;
    glm::vec3 startPlaneCenter = center - flowDir * startDist;
    startPlaneCenter.x = glm::clamp(startPlaneCenter.x, flowParams.minX + 0.1f*maxDim, flowParams.maxX - 0.1f*maxDim);
    startPlaneCenter.y = glm::clamp(startPlaneCenter.y, flowParams.minY + 0.1f*maxDim, flowParams.maxY - 0.1f*maxDim);
    startPlaneCenter.z = glm::clamp(startPlaneCenter.z, flowParams.minZ + 0.1f*maxDim, flowParams.maxZ - 0.1f*maxDim);
    if (!std::isfinite(startPlaneCenter.x)) startPlaneCenter = center - flowDir*maxDim;

    float spread = maxDim * 0.85f;
    if (!std::isfinite(spread) || spread < 1e-6f) spread = 1.0f;
    int grid = (int)ceilf(sqrtf((float)numStreamlines * 1.3f)); // +30% для отбраковки внутри
    if (grid < 1) grid = 1;
    if (grid > 40) grid = 40;

    std::vector<glm::vec3> startPoints;
    startPoints.reserve(numStreamlines);

    // Сидинг по аэродинамике: больше точек вблизи центра (где тело) и на краях для вихрей
    // Используем косинусное распределение для концентрации к центру
    for (int gy = 0; gy < grid && (int)startPoints.size() < numStreamlines*1.5f; gy++) {
        for (int gx = 0; gx < grid && (int)startPoints.size() < numStreamlines*1.5f; gx++) {
            float fx = (grid <= 1) ? 0.0f : ((float)gx/(grid-1) - 0.5f) * 2.0f;
            float fy = (grid <= 1) ? 0.0f : ((float)gy/(grid-1) - 0.5f) * 2.0f;
            // Косинусное сгущение к центру
            float fxCos = sinf(fx * 1.5707963f); // sin для сгущения
            float fyCos = sinf(fy * 1.5707963f);
            // Смешиваем равномерное и косинусное 50/50
            fx = fx*0.5f + fxCos*0.5f;
            fy = fy*0.5f + fyCos*0.5f;
            glm::vec3 sp = startPlaneCenter + right*(fx*spread) + up*(fy*spread);
            if (!std::isfinite(sp.x)) continue;
            if (!g_distanceField.empty() && sampleSDFCPU(sp) <= 0.0f) continue;
            // Дополнительный фильтр: не стартуем слишком далеко от оси тела (экономия)
            glm::vec3 toCenter = sp - center;
            float perpDist = glm::length(toCenter - flowDir * glm::dot(toCenter, flowDir));
            if (perpDist > maxDim*1.2f) {
                // 30% шанс оставить для вихрей на периферии
                float rnd = fabsf(sinf(gx*12.3f + gy*7.7f));
                if (rnd > 0.3f) continue;
            }
            startPoints.push_back(sp);
        }
    }

    // Если нужно больше — добавляем точки на передней кромке тела для детального обтекания
    if ((int)startPoints.size() < numStreamlines) {
        // Точки вокруг передней кромки
        glm::vec3 LE = center - flowDir * (maxDim*0.5f);
        int need = numStreamlines - (int)startPoints.size();
        int rings = (int)sqrtf((float)need) + 1;
        for (int r=0; r<rings && (int)startPoints.size() < numStreamlines; r++) {
            float radius = (float)(r+1)/rings * maxDim*0.4f;
            int pts = 8 + r*4;
            for (int a=0; a<pts && (int)startPoints.size() < numStreamlines; a++) {
                float ang = (float)a/pts * 6.2831853f;
                glm::vec3 offset = right * (cosf(ang)*radius) + up * (sinf(ang)*radius);
                glm::vec3 sp = LE + offset - flowDir * maxDim*0.3f;
                if (!std::isfinite(sp.x)) continue;
                if (!g_distanceField.empty() && sampleSDFCPU(sp) <= 0.0f) continue;
                startPoints.push_back(sp);
            }
        }
    }

    if ((int)startPoints.size() > numStreamlines) startPoints.resize(numStreamlines);
    int numLines = (int)startPoints.size();
    if (numLines == 0) return;
    std::vector<std::vector<float>> localVerts(numLines);
    float maxSpeed = flowParams.maxSpeed;
    if (!std::isfinite(maxSpeed) || maxSpeed < 1e-3f) maxSpeed = flowSpeed * 1.6f;
    if (maxSpeed < 1e-6f) maxSpeed = 1.0f;
    float groundY = g_voxMinY + aeroGroundHeight;
    float chord = maxDim; // для нормализации

    #ifdef _OPENMP
    #pragma omp parallel for schedule(dynamic)
    #endif
    for (int li = 0; li < numLines; ++li) {
        glm::vec3 start = startPoints[li];
        std::vector<float> verts;
        verts.reserve(streamlineSteps * 12);

        glm::vec3 p = start, prev = p;
        if (!std::isfinite(p.x)) continue;
        glm::vec3 v_prev = safeVelocity(prev, flowParams);
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

        float dt = streamlineStepSize;
        if (!std::isfinite(dt) || dt < 1e-6f) dt = 0.08f;

        int stagnationCounter = 0;
        for (int s = 0; s < streamlineSteps; s++) {
            glm::vec3 v = safeVelocity(p, flowParams);
            if (!std::isfinite(v.x)) break;
            float speed = glm::length(v);
            if (speed < 1e-5f) {
                stagnationCounter++;
                if (stagnationCounter > 5) break; // застой
            } else stagnationCounter = 0;
            if (!std::isfinite(speed)) break;

            // Адаптивный шаг по физике: dt ∝ 1/speed, уменьшаем вблизи тела и в зонах высокой завихренности
            float baseStep = streamlineStepSize;
            float distToCenter = glm::length(p - center);
            if (distToCenter < maxDim*1.2f) baseStep *= 0.35f;
            if (distToCenter < maxDim*0.6f) baseStep *= 0.45f;
            if (lbmParams.enabled) {
                float vort = 0.0f;
                if (lbmInitialized) vort = getLBMVorticityWorld(p);
                if (vort > 10.0f) baseStep *= 0.5f; // в вихре мельче
                if (vort > 30.0f) baseStep *= 0.5f;
            }
            // По кривизне: если скорость меняется быстро, уменьшаем шаг
            float accel = glm::length(v - v_prev) / (dt + 1e-6f);
            if (accel > flowSpeed*2.0f) baseStep *= 0.6f;

            float curDt = baseStep / (speed + 0.15f*flowSpeed);
            if (curDt > 0.12f) curDt = 0.12f;
            if (curDt < 0.0008f) curDt = 0.0008f;

            // RK45 с контролем ошибки
            auto [pNext, err] = rk45Step(p, curDt, flowParams);
            // Адаптация: если ошибка большая, уменьшаем dt и повторяем
            int adaptIter = 0;
            while (err > 0.01f && adaptIter < 3) {
                curDt *= 0.5f;
                auto res = rk45Step(p, curDt, flowParams);
                pNext = res.first;
                err = res.second;
                adaptIter++;
            }
            if (err < 0.001f && adaptIter==0) {
                // Можно увеличить шаг
                curDt *= 1.2f;
            }
            dt = curDt;

            if (!std::isfinite(pNext.x)) break;

            // Коллизия с телом — скольжение
            if (!g_distanceField.empty()) {
                float d = sampleSDFCPU(pNext);
                if (d <= 0.0f) {
                    glm::vec3 n = sdfNormalCPU(pNext);
                    float nl = glm::length(n);
                    if (nl > 1e-6f) {
                        n /= nl;
                        pNext += n * (fabsf(d) + flowParams.cellSizeX*0.7f);
                        if (sampleSDFCPU(pNext) <= 0.0f) {
                            // Если все еще внутри и на подветренной — возможно отрыв, терминируем
                            float flowDotN = glm::dot(flowDir, n);
                            if (flowDotN > 0) break; // отрыв на подветренной
                            // Иначе скользим
                            glm::vec3 tang = v - n * glm::dot(v,n);
                            if (glm::length(tang) > 1e-6f) {
                                pNext = p + glm::normalize(tang) * curDt * speed * 0.5f;
                                if (sampleSDFCPU(pNext) <= 0.0f) break;
                            } else break;
                        }
                    } else break;
                }
            }

            if (aeroGroundEffect && pNext.y < groundY) {
                pNext.y = groundY + flowParams.cellSizeY*0.6f;
            }

            float bigMargin = maxDim * 2.8f;
            if (!std::isfinite(bigMargin)) bigMargin = 10.0f;
            if (pNext.x < flowParams.minX - bigMargin || pNext.x > flowParams.maxX + bigMargin ||
                pNext.y < flowParams.minY - bigMargin || pNext.y > flowParams.maxY + bigMargin ||
                pNext.z < flowParams.minZ - bigMargin || pNext.z > flowParams.maxZ + bigMargin)
                break;

            // Терминация по вихрю: если в сильном вихре и скорость низкая — вихревая ловушка
            if (lbmParams.enabled && lbmInitialized) {
                float vort = getLBMVorticityWorld(pNext);
                float qCrit = getLBMQWorld(pNext);
                if (vort > 50.0f && speed < flowSpeed*0.3f && qCrit > 0) {
                    // В вихревом ядре — можно продолжить но с ограничением
                    if (s > streamlineSteps*0.7f) break;
                }
            }

            float speedNext = glm::length(safeVelocity(pNext, flowParams));
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

            prev = pNext; p = pNext; c_prev = c_p; v_prev = v;
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

    std::cout << "Streamlines v1.17.0 Physics Logic Fix (RK45 aero" << (lbmParams.enabled ? "+LBM" : "") << (aeroColorStreamlinesByVelocity ? "+VelColor" : "") << "): " << numLines << " lines, " << (verts.size()/6) << " vertices in " << perfStreamlinesMs << " ms" << std::endl;
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
