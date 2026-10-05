#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>

#include <cmath>

#include "globals.h"
#include "voxel_grid.h"
#include "flow_field.h"
#include "atmosphere.h"
#include "lbm.h"

// =====================================================
// FlowParams — v1.11.0 Physics Fix — корректный Re, ISA
// =====================================================
void updateFlowParams() {
    updateAtmosphereParams();

    if (!std::isfinite(center.x) || !std::isfinite(center.y) || !std::isfinite(center.z)) return;

    flowParams.centerX = center.x;
    flowParams.centerY = center.y;
    flowParams.centerZ = center.z;

    float sizeX = maxBB.x - minBB.x;
    float sizeY = maxBB.y - minBB.y;
    float sizeZ = maxBB.z - minBB.z;
    if (!std::isfinite(sizeX) || sizeX < 0.001f) sizeX = 0.2f;
    if (!std::isfinite(sizeY) || sizeY < 0.001f) sizeY = 0.2f;
    if (!std::isfinite(sizeZ) || sizeZ < 0.001f) sizeZ = 0.2f;

    flowParams.radiusX = sizeX * 0.5f;
    flowParams.radiusY = sizeY * 0.5f;
    flowParams.radiusZ = sizeZ * 0.5f;
    if (flowParams.radiusX < 0.001f) flowParams.radiusX = 0.1f;
    if (flowParams.radiusY < 0.001f) flowParams.radiusY = 0.1f;
    if (flowParams.radiusZ < 0.001f) flowParams.radiusZ = 0.1f;

    float safeSpeed = std::isfinite(flowSpeed) ? flowSpeed : 2.0f;
    if (safeSpeed < 0) safeSpeed = 0;
    if (safeSpeed > 1000.0f) safeSpeed = 1000.0f;
    float safeAz = std::isfinite(flowAzimuth) ? flowAzimuth : 0.0f;
    float safeEl = std::isfinite(flowElevation) ? flowElevation : 0.0f;
    if (safeEl < -89.0f) safeEl = -89.0f;
    if (safeEl > 89.0f) safeEl = 89.0f;

    float az = glm::radians(safeAz);
    float el = glm::radians(safeEl);
    float ce = cosf(el), se = sinf(el);
    float ca = cosf(az), sa = sinf(az);
    glm::vec3 dir(ce*ca, se, ce*sa);
    float dirLen = glm::length(dir);
    if (dirLen < 1e-6f || !std::isfinite(dirLen)) dir = glm::vec3(1,0,0);
    else dir = dir / dirLen;

    flowParams.vx = dir.x * safeSpeed;
    flowParams.vy = dir.y * safeSpeed;
    flowParams.vz = dir.z * safeSpeed;

    flowParams.time = (float)glfwGetTime();
    if (!std::isfinite(flowParams.time) || flowParams.time < 0) flowParams.time = 0.0f;
    flowParams.timeScale = std::isfinite(timeScale) && timeScale > 0 ? timeScale : 1.0f;
    if (flowParams.timeScale > 10.0f) flowParams.timeScale = 10.0f;
    flowParams.strouhal = std::isfinite(strouhal) && strouhal > 0 ? strouhal : 0.2f;
    if (flowParams.strouhal > 1.0f) flowParams.strouhal = 1.0f;
    flowParams.wakeStrength = std::isfinite(wakeStrength) && wakeStrength >=0 ? wakeStrength : 0.4f;
    if (flowParams.wakeStrength > 5.0f) flowParams.wakeStrength = 5.0f;
    flowParams.wakeLength = std::isfinite(wakeLength) && wakeLength >0 ? wakeLength : 8.0f;
    if (flowParams.wakeLength > 100.0f) flowParams.wakeLength = 100.0f;

    float safeMaxDim = std::isfinite(maxDim) && maxDim > 1e-6f ? maxDim : 1.0f;
    float margin = 0.5f * safeMaxDim;
    flowParams.minX = minBB.x - margin;
    flowParams.maxX = maxBB.x + margin;
    flowParams.minY = minBB.y - margin;
    flowParams.maxY = maxBB.y + margin;
    flowParams.minZ = minBB.z - margin;
    flowParams.maxZ = maxBB.z + margin;
    if (flowParams.minX >= flowParams.maxX) { flowParams.minX -= 0.5f; flowParams.maxX += 0.5f; }
    if (flowParams.minY >= flowParams.maxY) { flowParams.minY -= 0.5f; flowParams.maxY += 0.5f; }
    if (flowParams.minZ >= flowParams.maxZ) { flowParams.minZ -= 0.5f; flowParams.maxZ += 0.5f; }

    flowParams.maxSpeed = std::isfinite(maxSpeedForColor) && maxSpeedForColor > 1e-6f ? maxSpeedForColor : 5.0f;

    flowParams.gridNx = g_voxNx;
    flowParams.gridNy = g_voxNy;
    flowParams.gridNz = g_voxNz;
    flowParams.gridMinX = g_voxMinX;
    flowParams.gridMinY = g_voxMinY;
    flowParams.gridMinZ = g_voxMinZ;
    flowParams.gridMaxX = g_voxMaxX;
    flowParams.gridMaxY = g_voxMaxY;
    flowParams.gridMaxZ = g_voxMaxZ;
    float denomX = fmaxf((float)g_voxNx, 1.0f);
    float denomY = fmaxf((float)g_voxNy, 1.0f);
    float denomZ = fmaxf((float)g_voxNz, 1.0f);
    float szX = g_voxMaxX - g_voxMinX;
    float szY = g_voxMaxY - g_voxMinY;
    float szZ = g_voxMaxZ - g_voxMinZ;
    if (!std::isfinite(szX) || fabsf(szX) < 1e-8f) szX = denomX * 0.1f;
    if (!std::isfinite(szY) || fabsf(szY) < 1e-8f) szY = denomY * 0.1f;
    if (!std::isfinite(szZ) || fabsf(szZ) < 1e-8f) szZ = denomZ * 0.1f;
    flowParams.cellSizeX = szX / denomX;
    flowParams.cellSizeY = szY / denomY;
    flowParams.cellSizeZ = szZ / denomZ;
    if (flowParams.cellSizeX < 1e-6f) flowParams.cellSizeX = 0.1f;
    if (flowParams.cellSizeY < 1e-6f) flowParams.cellSizeY = 0.1f;
    if (flowParams.cellSizeZ < 1e-6f) flowParams.cellSizeZ = 0.1f;
    flowParams.gridCellCount = g_voxNx * g_voxNy * g_voxNz;

    flowParams.altitude = std::isfinite(altitude) ? altitude : 0.0f;
    flowParams.airDensity = std::isfinite(airDensity) && airDensity > 1e-6f ? airDensity : 1.225f;
    flowParams.airPressure = std::isfinite(airPressure) && airPressure > 0.1f ? airPressure : 101325.0f;
    flowParams.airTemperature = std::isfinite(airTemperature) && airTemperature > 10.0f ? airTemperature : 288.15f;
    flowParams.speedOfSound = std::isfinite(speedOfSound) && speedOfSound > 1.0f ? speedOfSound : 340.3f;

    // Reynolds number — физический: Re = rho*V*L / mu, mu=1.81e-5 для воздуха
    {
        float L = maxDim;
        if (L < 1e-6f) L = 1.0f;
        float V = safeSpeed;
        const float mu = 1.81e-5f; // динамическая вязкость воздуха
        float Re = flowParams.airDensity * V * L / mu;
        if (!std::isfinite(Re) || Re < 0) Re = 0;
        if (Re > 1e9f) Re = 1e9f;
        aeroReNumber = Re;
    }
}

// =====================================================
// Поле скоростей CPU — v1.11.0 Physics Fix
// Исправлено:
// - No-slip на поверхности (v=0 внутри)
// - No-penetration с экспонентой от maxDim, а не cellSize
// - Погранслой 1/7 закон с толщиной delta = 0.37*x/Re^(1/5)
// - След — гауссов дефицит скорости, ширина растет как sqrt(x)
// - Вихревая дорожка Кармана с затуханием 1/sqrt(x)
// - Метод отражений для земли
// - Сохранение массы: масштабирование чтобы не превышало Vinf*2
// =====================================================
glm::vec3 computeVelocityFieldCPU(const glm::vec3& p, const FlowParams& prm) {
    // LBM приоритет если включен
    if (lbmParams.enabled && lbmInitialized) {
        if (p.x >= lbmMinX && p.x <= lbmMaxX &&
            p.y >= lbmMinY && p.y <= lbmMaxY &&
            p.z >= lbmMinZ && p.z <= lbmMaxZ) {
            glm::vec3 vLBM = getLBMVelocityWorld(p);
            if (std::isfinite(vLBM.x) && std::isfinite(vLBM.y) && std::isfinite(vLBM.z)) {
                float mag2 = vLBM.x*vLBM.x + vLBM.y*vLBM.y + vLBM.z*vLBM.z;
                if (mag2 > 1e-12f) {
                    // Земля уже в LBM, но добавляем отражение для реализма
                    if (aeroGroundEffect) {
                        float groundY = g_voxMinY + aeroGroundHeight;
                        if (std::isfinite(groundY) && p.y > groundY && p.y - groundY < maxDim*0.5f) {
                            float h = p.y - groundY;
                            float clearance = center.y - groundY;
                            if (clearance > 1e-3f) {
                                // Venturi под днищем — ускорение обратно пропорционально клиренсу
                                float venturi = 1.0f + 0.25f * (maxDim*0.5f - h) / (maxDim*0.5f);
                                venturi = glm::clamp(venturi, 1.0f, 1.5f);
                                if (p.x >= minBB.x && p.x <= maxBB.x && p.z >= minBB.z && p.z <= maxBB.z) {
                                    vLBM *= venturi;
                                }
                            }
                        }
                    }
                    return vLBM;
                } else {
                    // Внутри твердого тела — no-slip 0
                    return glm::vec3(0.0f);
                }
            }
        } else {
            // Вне LBM — freestream
            return glm::vec3(prm.vx, prm.vy, prm.vz);
        }
    }

    if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z))
        return glm::vec3(prm.vx, prm.vy, prm.vz);

    glm::vec3 v(prm.vx, prm.vy, prm.vz);
    float vInf = sqrtf(prm.vx*prm.vx + prm.vy*prm.vy + prm.vz*prm.vz);
    if (vInf < 1e-4f) vInf = 1e-4f;

    // --- Ground effect: no-slip на земле + метод отражений ---
    if (aeroGroundEffect) {
        float groundY = 0.0f;
        if (std::isfinite(g_voxMinY) && g_voxNx > 0) groundY = g_voxMinY + aeroGroundHeight;
        else groundY = minBB.y - maxDim*0.1f + aeroGroundHeight;
        if (!std::isfinite(groundY)) groundY = minBB.y;

        if (p.y < groundY) {
            return glm::vec3(0.0f); // под землей — 0
        }
        float distGround = p.y - groundY;
        float blGround = maxDim * 0.05f; // толщина погранслоя у земли 5% maxDim
        if (distGround < blGround) {
            // Турбулентный погранслой 1/7
            float eta = distGround / blGround;
            eta = glm::clamp(eta, 0.0f, 1.0f);
            float uPlus = powf(eta, 1.0f/7.0f);
            v *= uPlus;
        }
        // Venturi под днищем для авто — физично: скорость ~ 1/h
        if (p.y > groundY && p.y < center.y) {
            float carBottom = minBB.y;
            if (p.y > carBottom - maxDim*0.1f && p.y < carBottom + maxDim*0.5f) {
                float clearance = carBottom - groundY;
                if (clearance > 1e-3f && clearance < maxDim && std::isfinite(clearance)) {
                    if (p.x >= minBB.x && p.x <= maxBB.x && p.z >= minBB.z && p.z <= maxBB.z) {
                        // Сохранение массы: A1*V1 = A2*V2, V2 = V1 * (H / h)
                        float venturi = 1.0f + 0.3f * (1.0f - clearance / (maxDim*0.5f));
                        venturi = glm::clamp(venturi, 1.0f, 1.6f);
                        v *= venturi;
                    }
                }
            }
        }
    }

    // --- Твердое тело: SDF + no-penetration + no-slip ---
    if (!g_distanceField.empty()) {
        float d = sampleSDFCPU(p); // в мировых единицах, >0 снаружи, <0 внутри
        if (d <= 0.0f) {
            // Внутри тела — строго 0 (no-slip)
            return glm::vec3(0.0f);
        }
        if (d < maxDim * 0.5f) { // только вблизи тела
            glm::vec3 n = sdfNormalCPU(p);
            if (!std::isfinite(n.x) || glm::length(n) < 1e-6f) n = glm::vec3(0,1,0);
            n = glm::normalize(n);

            // No-penetration: убираем компоненту в тело с экспоненциальным затуханием
            float vn = glm::dot(v, n);
            // Если поток в тело (vn<0 и снаружи), убираем
            // Характерная длина влияния — 10% maxDim
            float eps = maxDim * 0.08f;
            if (eps < 1e-4f) eps = 0.1f;
            float decay = expf(-d / eps); // 1 у поверхности, 0 далеко

            if (vn < 0.0f) {
                // Полное отражение нормальной компоненты с затуханием
                v -= vn * n * decay * 1.2f; // 1.2 — небольшая сверхкомпенсация для предотвращения проникновения
            }

            // Погранслой: толщина растет как 0.37*x/Re^(1/5)
            // x — расстояние вдоль потока от передней кромки
            float invVinf = 1.0f / vInf;
            glm::vec3 flowDir = glm::vec3(prm.vx, prm.vy, prm.vz) * invVinf;
            // Проекция точки на направление потока относительно центра — грубая оценка x
            glm::vec3 r = p - glm::vec3(prm.centerX, prm.centerY, prm.centerZ);
            float xAlong = glm::dot(r, flowDir) + maxDim*0.5f; // от передней кромки
            if (xAlong < 0.01f) xAlong = 0.01f;

            float Re_x = aeroReNumber * (xAlong / maxDim);
            if (Re_x < 1.0f) Re_x = 1.0f;
            float delta = 0.0f;
            if (Re_x < 5e5f) {
                // Ламинарный Блазиус: delta ~ 5*x/sqrt(Re_x)
                delta = 5.0f * xAlong / sqrtf(Re_x);
            } else {
                // Турбулентный: delta = 0.37*x / Re_x^(1/5)
                delta = 0.37f * xAlong / powf(Re_x, 0.2f);
            }
            if (delta < 1e-4f) delta = 1e-4f;
            if (delta > maxDim*0.3f) delta = maxDim*0.3f;

            if (d < delta * 3.0f) {
                // Внутри погранслоя — профиль 1/7
                float eta = d / delta;
                eta = glm::clamp(eta, 0.0f, 1.0f);
                // Скорость в погранслое: u/U = eta^(1/7) для турбулентного, eta*(2-eta) для ламинарного
                float uFactor;
                if (Re_x < 5e5f) {
                    // Параболический профиль Польгаузена для ламинарного
                    uFactor = eta * (2.0f - eta);
                } else {
                    uFactor = powf(eta, 1.0f/7.0f);
                }
                // Разделяем на нормальную и касательную
                glm::vec3 v_n = n * glm::dot(v, n);
                glm::vec3 v_t = v - v_n;
                // Нормальная стремится к 0, касательная — к uFactor*Vinf_t
                v_n *= (1.0f - expf(-eta*3.0f)); // быстро к 0 у стенки
                v_t *= uFactor;
                v = v_n + v_t;
            }
        }
    }

    // --- След за телом: гауссов дефицит + расширение ---
    if (vInf > 1e-4f) {
        float invMag = 1.0f / vInf;
        float dx = prm.vx*invMag, dy = prm.vy*invMag, dz = prm.vz*invMag;
        float rx = p.x - prm.centerX, ry = p.y - prm.centerY, rz = p.z - prm.centerZ;
        float along = rx*dx + ry*dy + rz*dz; // расстояние за телом вдоль потока
        float px = rx - along*dx, py = ry - along*dy, pz = rz - along*dz;
        float rPerp = sqrtf(px*px + py*py + pz*pz);
        float D = 2.0f * fmaxf(prm.radiusY, prm.radiusZ);
        if (D < 1e-4f) D = maxDim * 0.5f;
        if (D < 1e-4f) D = 0.5f;

        float safeWakeLen = prm.wakeLength;
        if (!std::isfinite(safeWakeLen) || safeWakeLen < 0.1f) safeWakeLen = 8.0f;

        if (along > D*0.2f && along < safeWakeLen) {
            // Ширина следа растет как sqrt(x) — турбулентное расширение
            // b(x) = 0.2*D * sqrt(1 + x/D)
            float b = D * 0.25f * sqrtf(1.0f + along / D);
            if (b < 1e-6f) b = 0.1f;

            // Дефицит скорости: U_deficit = Uinf * Cd * (D/x)^(1/2) * exp(-r^2/b^2)
            // Cd ~ 0.4 для цилиндра, 0.2 для авто
            float Cd_est = 0.3f;
            float xNorm = along / D;
            if (xNorm < 0.1f) xNorm = 0.1f;
            float deficitMag = Cd_est * 0.5f / sqrtf(xNorm) * expf(-(rPerp*rPerp)/(b*b));
            deficitMag *= prm.wakeStrength; // пользовательский множитель
            if (deficitMag > 0.9f) deficitMag = 0.9f;
            if (deficitMag < 0) deficitMag = 0;

            // Вычитаем из потока
            v -= glm::vec3(prm.vx, prm.vy, prm.vz) * deficitMag;

            // Вихревая дорожка Кармана — поперечные колебания
            if (rPerp < b*2.0f) {
                float st = prm.strouhal;
                if (st < 1e-6f) st = 0.2f;
                float omega = 2.0f * 3.14159265f * st * vInf / D;
                float phase = omega * prm.time - along * 0.8f;

                // Амплитуда вихрей затухает как 1/sqrt(x)
                float vortexAmp = prm.wakeStrength * 0.25f * vInf * expf(-rPerp*rPerp/(b*b*1.5f)) / sqrtf(xNorm);
                if (aeroShowWake) vortexAmp *= (1.0f + aeroWakeOpacity*0.5f);

                // Поперечное направление — перпендикулярно потоку и радиусу
                float invPerp = (rPerp > 1e-6f) ? 1.0f / rPerp : 0.0f;
                float pnx = px*invPerp, pny = py*invPerp, pnz = pz*invPerp;
                // Вихревое направление = flowDir x radial
                float vtx = dy*pnz - dz*pny;
                float vty = dz*pnx - dx*pnz;
                float vtz = dx*pny - dy*pnx;
                float vtxLen = sqrtf(vtx*vtx + vty*vty + vtz*vtz);
                if (vtxLen > 1e-6f) { vtx/=vtxLen; vty/=vtxLen; vtz/=vtxLen; }

                float sinPhase = sinf(phase);
                // Чередующиеся вихри по сторонам
                float side = (sinf(phase * 0.5f) > 0) ? 1.0f : -1.0f;

                v.x += vortexAmp * sinPhase * vtx * side;
                v.y += vortexAmp * sinPhase * vty * side;
                v.z += vortexAmp * sinPhase * vtz * side;

                // Турбулентные флуктуации — колмогоровский спектр ~ k^-5/3, упрощенно 10% от дефицита
                if (aeroMachEffects) {
                    float mach = vInf / (prm.speedOfSound + 1e-6f);
                    if (mach > 0.3f) vortexAmp *= (1.0f + mach*0.5f);
                }
                float turbAmp = 0.08f * deficitMag * vInf;
                float tx = prm.time;
                // Детерминированный шум с разными частотами
                v.x += turbAmp * 0.5f * sinf(tx*4.3f + along*2.1f + rPerp*3.7f + p.x*0.7f);
                v.y += turbAmp * 0.5f * sinf(tx*3.7f + along*2.8f + rPerp*4.1f + p.y*0.9f);
                v.z += turbAmp * 0.5f * sinf(tx*5.1f + along*1.9f + rPerp*3.3f + p.z*0.6f);
            }
        }
    }

    // Ограничиваем скорость физически: не более 1.8*Vinf (ускорение в сопле не более)
    // и не менее 0
    float maxV = vInf * 1.8f;
    if (maxV < prm.maxSpeed) maxV = prm.maxSpeed * 1.2f;
    float curMag2 = v.x*v.x + v.y*v.y + v.z*v.z;
    if (curMag2 > maxV*maxV) {
        float s = maxV / sqrtf(curMag2);
        v *= s;
    }

    if (!std::isfinite(v.x)) return glm::vec3(prm.vx, prm.vy, prm.vz);
    return v;
}

glm::vec3 colorForPoint(const glm::vec3& v, float sdfDist, const FlowParams& prm) {
    if (!std::isfinite(v.x)) return glm::vec3(1,0,0);
    float speed = sqrtf(v.x*v.x + v.y*v.y + v.z*v.z);
    float safeMaxSpeed = prm.maxSpeed;
    if (safeMaxSpeed < 1e-6f) safeMaxSpeed = 5.0f;
    float spdT = speed / safeMaxSpeed;
    if (spdT < 0) spdT = 0; if (spdT > 1) spdT = 1;

    // Цветовые карты — без изменений, но с clamp
    if (aeroColorMap == 1) { // viridis
        glm::vec3 c;
        if (spdT < 0.25f) { float k=spdT/0.25f; c=glm::vec3(0.267f + k*0.1f, 0.004f + k*0.3f, 0.329f + k*0.2f); }
        else if (spdT < 0.5f) { float k=(spdT-0.25f)/0.25f; c=glm::vec3(0.229f + k*0.1f, 0.322f + k*0.2f, 0.545f - k*0.1f); }
        else if (spdT < 0.75f) { float k=(spdT-0.5f)/0.25f; c=glm::vec3(0.127f + k*0.5f, 0.566f + k*0.2f, 0.550f - k*0.2f); }
        else { float k=(spdT-0.75f)/0.25f; c=glm::vec3(0.5f + k*0.49f, 0.79f + k*0.1f, 0.3f - k*0.1f); }
        return c;
    } else if (aeroColorMap == 2) { // parula
        if (spdT < 0.25f) return glm::vec3(spdT*4.0f*0.2f, spdT*4.0f*0.2f, 0.5f + spdT*2.0f);
        else if (spdT < 0.5f) { float k=(spdT-0.25f)/0.25f; return glm::vec3(k*0.2f, 0.2f + k*0.6f, 1.0f - k*0.3f); }
        else if (spdT < 0.75f) { float k=(spdT-0.5f)/0.25f; return glm::vec3(0.2f + k*0.6f, 0.8f, 0.7f - k*0.7f); }
        else { float k=(spdT-0.75f)/0.25f; return glm::vec3(0.8f + k*0.2f, 0.8f - k*0.8f, k*0.2f); }
    } else if (aeroColorMap == 3) { // coolwarm
        if (spdT < 0.5f) { float k=spdT*2.0f; return glm::vec3(0.23f + k*0.6f, 0.29f + k*0.4f, 0.75f); }
        else { float k=(spdT-0.5f)*2.0f; return glm::vec3(0.85f, 0.7f - k*0.5f, 0.2f + k*0.1f); }
    }

    // Default rainbow
    float cell = prm.cellSizeX;
    if (cell < 1e-6f) cell = 0.1f;
    if (sdfDist < 1.5f * cell) return glm::vec3(1.0f, 0.2f, 0.0f);
    else if (sdfDist < 4.0f * cell) {
        float b = (sdfDist - 1.5f * cell) / (2.5f * cell);
        if (b < 0) b = 0; if (b > 1) b = 1;
        glm::vec3 hot(1.0f, 0.5f, 0.0f);
        glm::vec3 cold;
        if (spdT < 0.5f) cold = glm::vec3(1.0f, spdT*2.0f, 0.0f);
        else cold = glm::vec3(1.0f-(spdT-0.5f)*2.0f, 1.0f, 0.0f);
        return hot * (1.0f - b) + cold * b;
    } else {
        if (spdT < 0.5f) return glm::vec3(1.0f, spdT*2.0f, 0.0f);
        else return glm::vec3(1.0f-(spdT-0.5f)*2.0f, 1.0f, 0.0f);
    }
}
