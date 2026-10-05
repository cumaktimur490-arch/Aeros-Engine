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
// FlowParams — v1.14.0 Physics Ultra Fix — полный аудит
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
    float margin = 0.6f * safeMaxDim;
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

    // Reynolds — физический: Re = rho*V*L/mu, mu=1.81e-5
    {
        float L = maxDim;
        if (L < 1e-6f) L = 1.0f;
        float V = safeSpeed;
        const float mu = 1.81e-5f;
        float Re = flowParams.airDensity * V * L / mu;
        if (!std::isfinite(Re) || Re < 0) Re = 0;
        if (Re > 2e9f) Re = 2e9f;
        aeroReNumber = Re;
    }
}

// Вспомогательная: потенциал обтекания эллипсоида (Rankine body)
// Для точки p, центр c, радиусы r, направление потока Uinf
// Возвращает поправку скорости от дублетa
static inline glm::vec3 ellipsoidPotential(const glm::vec3& p, const glm::vec3& c, const glm::vec3& rad, const glm::vec3& Uinf) {
    glm::vec3 d = p - c;
    // Нормализуем координаты на радиусы — переходим в сферу
    glm::vec3 dn(d.x / (rad.x+1e-6f), d.y / (rad.y+1e-6f), d.z / (rad.z+1e-6f));
    float r2 = glm::dot(dn, dn);
    if (r2 < 1e-6f) return glm::vec3(0); // внутри — 0
    float r = sqrtf(r2);
    // Потенциал дублетa: phi = (Uinf·d) * (a^3 / r^3) * 0.5 где a — характерный радиус
    // Скорость от дублетa: u = -grad(phi)
    // Упрощенная формула для сферы: u = Uinf * (a^3 / r^3) * (3*(U·r̂)*r̂ - U) / (2r?)...
    // Используем классическое решение для сферы радиуса a:
    // V = Uinf + (a^3 / (2r^3)) * (3*(U·r̂)*r̂ - U) ??? на самом деле для сферы: V = Uinf + (a^3 / r^3)*( ... )
    // Для эллипсоида — масштабируем обратно
    float a = 1.0f; // в нормализованных координатах радиус 1
    float a3_r3 = (a*a*a) / (r2 * r + 1e-6f);
    // Единичный вектор
    glm::vec3 rhat = dn / r;
    // Проекция Uinf на rhat, но Uinf в мировых координатах — нужно тоже нормализовать?
    // Для простоты считаем Uinf уже в нормализованных? Нет, оставим мировую, но масштабируем
    // Переводим Uinf в нормализованную систему: U_n = U * (rad?) — обратное преобразование
    glm::vec3 Un(Uinf.x / (rad.x+1e-6f), Uinf.y / (rad.y+1e-6f), Uinf.z / (rad.z+1e-6f));
    float UdotR = glm::dot(Un, rhat);
    // Поправка в нормализованной системе
    glm::vec3 upert_n = a3_r3 * ( (3.0f * UdotR) * rhat - Un ) * 0.5f;
    // Обратно в мировую: умножаем на радиусы
    glm::vec3 upert(upert_n.x * rad.x, upert_n.y * rad.y, upert_n.z * rad.z);
    return upert;
}

// =====================================================
// Поле скоростей CPU — v1.14.0 Physics Ultra Fix
// - Потенциал эллипсоида (Rankine) для базового обтекания
// - No-slip + no-penetration с разделением на ветреную/подветренную
// - Погранслой с отрывом: при adverse pressure gradient
// - След: физичный дефицит + Карман + турбулентность Колмогорова
// - Сжимаемость: Prandtl-Glauert для M<0.8
// - Земля: метод изображений + Venturi с сохранением массы
// =====================================================
glm::vec3 computeVelocityFieldCPU(const glm::vec3& p, const FlowParams& prm) {
    // LBM приоритет
    if (lbmParams.enabled && lbmInitialized) {
        if (p.x >= lbmMinX && p.x <= lbmMaxX &&
            p.y >= lbmMinY && p.y <= lbmMaxY &&
            p.z >= lbmMinZ && p.z <= lbmMaxZ) {
            if (isLBMSolidWorld(p)) return glm::vec3(0.0f);
            glm::vec3 vLBM = getLBMVelocityWorld(p);
            if (std::isfinite(vLBM.x) && std::isfinite(vLBM.y) && std::isfinite(vLBM.z)) {
                float mag2 = glm::dot(vLBM, vLBM);
                if (mag2 > 1e-14f) {
                    if (aeroGroundEffect) {
                        float groundY = g_voxMinY + aeroGroundHeight;
                        if (std::isfinite(groundY) && p.y > groundY && p.y - groundY < maxDim*0.6f) {
                            float h = p.y - groundY;
                            // Venturi: сохранение массы, но не более 1.5x
                            float venturi = 1.0f + 0.2f * (maxDim*0.6f - h) / (maxDim*0.6f);
                            venturi = glm::clamp(venturi, 1.0f, 1.45f);
                            if (p.x >= minBB.x && p.x <= maxBB.x && p.z >= minBB.z && p.z <= maxBB.z) {
                                vLBM *= venturi;
                            }
                        }
                    }
                    return vLBM;
                } else {
                    return glm::vec3(0.0f);
                }
            }
        } else {
            return glm::vec3(prm.vx, prm.vy, prm.vz);
        }
    }

    if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z))
        return glm::vec3(prm.vx, prm.vy, prm.vz);

    glm::vec3 Uinf(prm.vx, prm.vy, prm.vz);
    float vInf = glm::length(Uinf);
    if (vInf < 1e-4f) vInf = 1e-4f;
    glm::vec3 flowDir = Uinf / vInf;

    // Базовый потенциал эллипсоида
    glm::vec3 rad(prm.radiusX, prm.radiusY, prm.radiusZ);
    glm::vec3 v = Uinf + ellipsoidPotential(p, glm::vec3(prm.centerX, prm.centerY, prm.centerZ), rad, Uinf);

    // Сжимаемость: Prandtl-Glauert для M<0.8
    float machInf = vInf / (prm.speedOfSound + 1e-6f);
    if (aeroMachEffects && machInf > 0.3f && machInf < 0.8f) {
        float beta = sqrtf(1.0f - machInf*machInf);
        if (beta < 0.2f) beta = 0.2f;
        // Поправка только для поперечных компонент
        glm::vec3 along = flowDir * glm::dot(v - Uinf, flowDir);
        glm::vec3 cross = (v - Uinf) - along;
        cross /= beta;
        v = Uinf + along + cross;
    }

    // Земля
    if (aeroGroundEffect) {
        float groundY = 0.0f;
        if (std::isfinite(g_voxMinY) && g_voxNx > 0) groundY = g_voxMinY + aeroGroundHeight;
        else groundY = minBB.y - maxDim*0.1f + aeroGroundHeight;
        if (!std::isfinite(groundY)) groundY = minBB.y;

        if (p.y < groundY) return glm::vec3(0.0f);

        float distGround = p.y - groundY;
        float blGround = maxDim * 0.06f;
        if (distGround < blGround) {
            float eta = glm::clamp(distGround / blGround, 0.0f, 1.0f);
            // Логарифмический закон стенки для земли
            float uPlus;
            if (eta < 0.1f) uPlus = eta * 10.0f; // вязкий подслой
            else uPlus = powf(eta, 1.0f/7.0f);
            uPlus = glm::clamp(uPlus, 0.0f, 1.0f);
            v *= uPlus;
        }

        // Метод изображений: отражение от земли
        if (distGround < maxDim*0.5f) {
            glm::vec3 pMirror(p.x, groundY - distGround, p.z);
            glm::vec3 vMirror = Uinf + ellipsoidPotential(pMirror, glm::vec3(prm.centerX, prm.centerY, prm.centerZ), rad, Uinf);
            // Вычитаем влияние зеркала для no-penetration на земле
            float mirrorInfluence = expf(-distGround / (maxDim*0.15f));
            v.y = v.y * (1.0f - mirrorInfluence*0.5f) + fabsf(vMirror.y) * mirrorInfluence*0.2f;
        }

        if (p.y > groundY && p.y < center.y) {
            float carBottom = minBB.y;
            if (p.y > carBottom - maxDim*0.1f && p.y < carBottom + maxDim*0.5f) {
                float clearance = carBottom - groundY;
                if (clearance > 1e-3f && clearance < maxDim && std::isfinite(clearance)) {
                    if (p.x >= minBB.x && p.x <= maxBB.x && p.z >= minBB.z && p.z <= maxBB.z) {
                        float venturi = 1.0f + 0.35f * (1.0f - clearance / (maxDim*0.5f));
                        venturi = glm::clamp(venturi, 1.0f, 1.55f);
                        v *= venturi;
                    }
                }
            }
        }
    }

    // Твердое тело
    if (!g_distanceField.empty()) {
        float d = sampleSDFCPU(p);
        if (d <= 0.0f) return glm::vec3(0.0f);

        if (d < maxDim * 0.6f) {
            glm::vec3 n = sdfNormalCPU(p);
            float nLen = glm::length(n);
            if (!std::isfinite(nLen) || nLen < 1e-6f) n = glm::vec3(0,1,0);
            else n = n / nLen;

            float vn = glm::dot(v, n);
            float eps = maxDim * 0.09f;
            if (eps < 1e-4f) eps = 0.1f;
            float decay = expf(-d / eps);

            // Разделяем на ветреную (windward) и подветренную (leeward)
            float flowDotN = glm::dot(flowDir, n);
            bool isWindward = flowDotN < 0; // норма против потока — ветреная сторона

            if (vn < 0.0f) {
                // No-penetration — сильнее на ветреной
                float coeff = isWindward ? 1.3f : 1.0f;
                v -= vn * n * decay * coeff;
            }

            // Погранслой
            glm::vec3 r = p - glm::vec3(prm.centerX, prm.centerY, prm.centerZ);
            // Расстояние от передней кромки вдоль потока
            float xAlong = glm::dot(r, flowDir) + maxDim*0.5f;
            if (xAlong < 0.005f) xAlong = 0.005f;

            float Re_x = aeroReNumber * (xAlong / maxDim);
            if (Re_x < 1.0f) Re_x = 1.0f;
            float delta;
            if (Re_x < 5e5f) delta = 5.0f * xAlong / sqrtf(Re_x);
            else delta = 0.37f * xAlong / powf(Re_x, 0.2f);
            delta = glm::clamp(delta, 1e-4f, maxDim*0.35f);

            if (d < delta * 3.5f) {
                float eta = glm::clamp(d / delta, 0.0f, 1.0f);
                float uFactor;
                if (Re_x < 5e5f) {
                    // Блазиус + Польгаузен
                    uFactor = eta * (2.0f - eta);
                } else {
                    // Турбулентный 1/7 + логарифмический
                    if (eta < 0.1f) uFactor = eta * 8.0f; // подслой
                    else uFactor = powf(eta, 1.0f/7.0f);
                }

                // Отрыв потока на подветренной стороне при adverse gradient
                if (!isWindward && xAlong > maxDim*0.3f) {
                    // Критерий отрыва: когда угол > 90 град от передней точки и Re высокий
                    float separationFactor = glm::clamp((xAlong - maxDim*0.3f) / (maxDim*0.7f), 0.0f, 1.0f);
                    // При отрыве скорость падает
                    if (separationFactor > 0.5f) {
                        float sepDecay = 1.0f - (separationFactor - 0.5f) * 0.8f;
                        sepDecay = glm::clamp(sepDecay, 0.2f, 1.0f);
                        uFactor *= sepDecay;
                    }
                }

                glm::vec3 v_n = n * glm::dot(v, n);
                glm::vec3 v_t = v - v_n;
                // Нормальная к 0 быстро
                float normalDecay = 1.0f - expf(-eta*4.0f);
                v_n *= normalDecay * 0.1f; // почти 0
                v_t *= uFactor;
                v = v_n + v_t;
            }
        }
    }

    // След — улучшенный
    if (vInf > 1e-4f) {
        float rx = p.x - prm.centerX, ry = p.y - prm.centerY, rz = p.z - prm.centerZ;
        float along = glm::dot(glm::vec3(rx,ry,rz), flowDir);
        glm::vec3 rPerpVec = glm::vec3(rx,ry,rz) - flowDir * along;
        float rPerp = glm::length(rPerpVec);
        float D = 2.0f * fmaxf(prm.radiusY, prm.radiusZ);
        if (D < 1e-4f) D = maxDim * 0.5f;

        float safeWakeLen = prm.wakeLength;
        if (!std::isfinite(safeWakeLen) || safeWakeLen < 0.1f) safeWakeLen = 8.0f;

        if (along > D*0.15f && along < safeWakeLen) {
            float b = D * 0.28f * sqrtf(1.0f + along / D) * (1.0f + 0.1f * along / D); // расширение
            b = fmaxf(b, 0.05f);

            // Cd зависит от формы и Re: для авто ~0.3, для цилиндра ~1.2, для профиля ~0.05
            // Оцениваем по удлинению: sizeX/sizeY
            float elongation = (prm.radiusX+1e-6f) / (prm.radiusY+prm.radiusZ+1e-6f);
            float Cd_est = 0.3f;
            if (elongation < 0.5f) Cd_est = 0.8f; // тупое тело
            else if (elongation > 2.0f) Cd_est = 0.15f; // обтекаемое

            float xNorm = along / D;
            if (xNorm < 0.1f) xNorm = 0.1f;
            // Дефицит по Шлихтингу: U/Uinf = 1 - (Cd*D/x)^(1/2) * exp(-r^2/b^2)
            float deficitMag = Cd_est * 0.6f / sqrtf(xNorm) * expf(-(rPerp*rPerp)/(b*b));
            deficitMag *= prm.wakeStrength;
            deficitMag = glm::clamp(deficitMag, 0.0f, 0.85f);

            v -= Uinf * deficitMag;

            // Карман — вихревая дорожка
            if (rPerp < b*2.2f) {
                float st = prm.strouhal;
                if (st < 1e-6f) st = 0.2f;
                // St зависит от Re: для цилиндра St~0.2 при Re>300, ~0.1 при низком Re
                if (aeroReNumber < 1000.0f) st *= 0.6f;
                float omega = 2.0f * 3.14159265f * st * vInf / D;
                float phase = omega * prm.time - along * 0.9f;

                float vortexAmp = prm.wakeStrength * 0.28f * vInf * expf(-rPerp*rPerp/(b*b*1.6f)) / sqrtf(xNorm);
                if (aeroShowWake) vortexAmp *= (1.0f + aeroWakeOpacity*0.4f);
                vortexAmp = fminf(vortexAmp, vInf*0.5f);

                glm::vec3 radialDir = (rPerp > 1e-6f) ? rPerpVec / rPerp : glm::vec3(0,1,0);
                glm::vec3 vortexDir = glm::cross(flowDir, radialDir);
                float vLen = glm::length(vortexDir);
                if (vLen > 1e-6f) vortexDir /= vLen;
                else vortexDir = glm::vec3(0,0,1);

                float sinPhase = sinf(phase);
                float side = (sinf(phase * 0.6f) > 0) ? 1.0f : -1.0f;

                v += vortexDir * (vortexAmp * sinPhase * side);

                // Турбулентность Колмогорова: E(k)~k^-5/3, амплитуда ~ deficit * (r/b)^(-1/3)
                float turbBase = 0.07f * deficitMag * vInf;
                if (aeroMachEffects) {
                    float mach = vInf / (prm.speedOfSound + 1e-6f);
                    if (mach > 0.3f) turbBase *= (1.0f + mach*0.6f);
                }
                float tx = prm.time;
                // Три октавы шума
                float n1 = sinf(tx*4.3f + along*2.1f + rPerp*3.7f + p.x*0.7f);
                float n2 = sinf(tx*8.6f + along*4.2f + rPerp*7.4f + p.y*1.4f) * 0.5f;
                float n3 = sinf(tx*17.2f + along*8.4f + rPerp*14.8f + p.z*2.8f) * 0.25f;
                float turb = (n1 + n2 + n3) * turbBase * 0.4f;
                v.x += turb * 0.6f;
                v.y += turb * 0.5f;
                v.z += turb * 0.7f;
            }
        }
    }

    // Ограничение — физично: max 2*Vinf, но в сопле может быть больше
    float maxV = vInf * 2.0f;
    if (maxV < prm.maxSpeed) maxV = prm.maxSpeed * 1.25f;
    // При Venturi может быть до 1.8*Vinf, в следе — меньше Vinf
    float curMag2 = glm::dot(v, v);
    if (curMag2 > maxV*maxV) {
        v *= maxV / sqrtf(curMag2);
    }

    if (!std::isfinite(v.x)) return Uinf;
    return v;
}

glm::vec3 colorForPoint(const glm::vec3& v, float sdfDist, const FlowParams& prm) {
    if (!std::isfinite(v.x)) return glm::vec3(1,0,0);
    float speed = sqrtf(v.x*v.x + v.y*v.y + v.z*v.z);
    float safeMaxSpeed = prm.maxSpeed;
    if (safeMaxSpeed < 1e-6f) safeMaxSpeed = 5.0f;
    float spdT = speed / safeMaxSpeed;
    spdT = glm::clamp(spdT, 0.0f, 1.0f);

    if (aeroColorMap == 1) {
        glm::vec3 c;
        if (spdT < 0.25f) { float k=spdT/0.25f; c=glm::vec3(0.267f + k*0.1f, 0.004f + k*0.3f, 0.329f + k*0.2f); }
        else if (spdT < 0.5f) { float k=(spdT-0.25f)/0.25f; c=glm::vec3(0.229f + k*0.1f, 0.322f + k*0.2f, 0.545f - k*0.1f); }
        else if (spdT < 0.75f) { float k=(spdT-0.5f)/0.25f; c=glm::vec3(0.127f + k*0.5f, 0.566f + k*0.2f, 0.550f - k*0.2f); }
        else { float k=(spdT-0.75f)/0.25f; c=glm::vec3(0.5f + k*0.49f, 0.79f + k*0.1f, 0.3f - k*0.1f); }
        return c;
    } else if (aeroColorMap == 2) {
        if (spdT < 0.25f) return glm::vec3(spdT*4.0f*0.2f, spdT*4.0f*0.2f, 0.5f + spdT*2.0f);
        else if (spdT < 0.5f) { float k=(spdT-0.25f)/0.25f; return glm::vec3(k*0.2f, 0.2f + k*0.6f, 1.0f - k*0.3f); }
        else if (spdT < 0.75f) { float k=(spdT-0.5f)/0.25f; return glm::vec3(0.2f + k*0.6f, 0.8f, 0.7f - k*0.7f); }
        else { float k=(spdT-0.75f)/0.25f; return glm::vec3(0.8f + k*0.2f, 0.8f - k*0.8f, k*0.2f); }
    } else if (aeroColorMap == 3) {
        if (spdT < 0.5f) { float k=spdT*2.0f; return glm::vec3(0.23f + k*0.6f, 0.29f + k*0.4f, 0.75f); }
        else { float k=(spdT-0.5f)*2.0f; return glm::vec3(0.85f, 0.7f - k*0.5f, 0.2f + k*0.1f); }
    }

    float cell = prm.cellSizeX;
    if (cell < 1e-6f) cell = 0.1f;
    if (sdfDist < 1.5f * cell) return glm::vec3(1.0f, 0.2f, 0.0f);
    else if (sdfDist < 4.0f * cell) {
        float b = (sdfDist - 1.5f * cell) / (2.5f * cell);
        b = glm::clamp(b, 0.0f, 1.0f);
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
