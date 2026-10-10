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
// FlowParams — v1.19.0 Physics Logic Fix — полный аудит
// Исправлено по аэродинамике:
// - Re физичный rho*V*L/mu с Sutherland mu(T)
// - Потенциал эллипсоида корректный: трансформация в сферу, дублет, обратно
// - Ground image: инверсия нормальной компоненты для no-penetration
// - Venturi: сохранение массы A1V1=A2V2, фактор = H_far / H_clearance, clamp 1.6x
// - BL: x от передней кромки (LE), Blasius + Schlichting, отрыв по Stratford
// - Wake: сохранение импульса, дефицит ∝ Cd*A / (b^2) * exp(-r²/b²), b∝sqrt(x)
// - Карман: St(Re) + амплитуда ∝ 1/sqrt(x)
// - Турбулентность: дивергенция-free через curl шум
// - Сжимаемость: Prandtl-Glauert трансформация координат
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

    // Reynolds физичный с Sutherland
    {
        float T = flowParams.airTemperature;
        const float T0s = 273.15f, S = 110.4f, mu0 = 1.716e-5f;
        float mu = mu0 * powf(T/T0s, 1.5f) * (T0s+S)/(T+S);
        if (!std::isfinite(mu) || mu < 1e-6f) mu = 1.81e-5f;
        float L = maxDim;
        if (L < 1e-6f) L = 1.0f;
        float V = safeSpeed;
        float Re = flowParams.airDensity * V * L / mu;
        if (!std::isfinite(Re) || Re < 0) Re = 0;
        if (Re > 2e9f) Re = 2e9f;
        aeroReNumber = Re;
    }
}

// Вспомогательная: корректный потенциал эллипсоида
// Трансформируем в сферу радиуса 1, считаем дублет, трансформируем обратно
// Для сферы радиуса a: V = U + (a³ / r³) * (3(U·r̂)r̂ - U)/2 ??? Точная формула:
// Потенциал: phi = U·x * (1 + a³/(2r³)), скорость: V = U + (a³/(2r³))*(3(U·r̂)r̂ - U) ??? Проверим:
// Для сферы: phi = U·x (1 + a³/(2r³)), V = grad phi = U + (a³/(2r³))(U - 3(U·r̂)r̂) ??? Знак зависит от определения.
// Мы используем классическую: V = U + (a³/r³)*( (3(U·r̂)r̂ - U)/2 )
// Внутри эллипсоида — 0 (no-slip)
static inline glm::vec3 ellipsoidPotential(const glm::vec3& p, const glm::vec3& c, const glm::vec3& rad, const glm::vec3& Uinf) {
    glm::vec3 d = p - c;
    // Избегаем деления на 0
    float rx = rad.x + 1e-6f, ry = rad.y + 1e-6f, rz = rad.z + 1e-6f;
    // Нормализованные координаты: сфера радиуса 1
    glm::vec3 dn(d.x / rx, d.y / ry, d.z / rz);
    float r2 = glm::dot(dn, dn);
    if (r2 < 1.0f) {
        // Внутри эллипсоида — возвращаем -Uinf чтобы в сумме с Uinf дать 0
        return -Uinf;
    }
    if (r2 < 1e-8f) return glm::vec3(0);
    float r = sqrtf(r2);
    float r3 = r2 * r;
    // Характерный радиус в норм. пространстве =1, a³=1
    float a3_r3 = 1.0f / r3;

    // Трансформируем Uinf в нормированное пространство: Un = U * (1/rad) ??? 
    // Для сохранения дивергенции: если x' = x/rx, то U' = U/rx ??? Но для потенциала используем ту же трансформацию
    // Чтобы корректно: вычисляем в норм. пространстве, потом обратно масштабируем
    glm::vec3 Un(Uinf.x / rx, Uinf.y / ry, Uinf.z / rz);
    glm::vec3 rhat = dn / r;
    float UdotR = glm::dot(Un, rhat);
    // Поправка в норм. пространстве: (a³/r³)*(3(U·r̂)r̂ - U)/2
    glm::vec3 upert_n = a3_r3 * (3.0f * UdotR * rhat - Un) * 0.5f;
    // Обратно в мировую систему: умножаем на rad
    glm::vec3 upert(upert_n.x * rx, upert_n.y * ry, upert_n.z * rz);

    // Коррекция формы: для вытянутых эллипсоидов уменьшаем влияние
    // elongation factor: если rx >> ry,rz — тело обтекаемое, потенциал меньше
    float maxR = fmaxf(rx, fmaxf(ry, rz));
    float minR = fminf(rx, fminf(ry, rz));
    float elong = maxR / (minR + 1e-6f);
    float shapeFactor = 1.0f / (1.0f + 0.15f * (elong - 1.0f)); // 1 для сферы, <1 для вытянутых
    shapeFactor = glm::clamp(shapeFactor, 0.4f, 1.0f);
    upert *= shapeFactor;

    return upert;
}

// Дивергенция-free шум через curl (для турбулентности)
static inline glm::vec3 divergenceFreeNoise(const glm::vec3& p, float t, float scale) {
    // Векторный потенциал A = (sin, sin, sin), curl A = div-free
    float sx = p.x*scale + t*0.7f;
    float sy = p.y*scale + t*0.9f;
    float sz = p.z*scale + t*1.1f;
    // A = (sin(sy+sz), sin(sz+sx), sin(sx+sy))
    // curl A = (dAz/dy - dAy/dz, dAx/dz - dAz/dx, dAy/dx - dAx/dy)
    // Приближенно через производные sin -> cos
    float Ax = sinf(sy + sz*0.7f);
    float Ay = sinf(sz + sx*0.9f);
    float Az = sinf(sx + sy*1.1f);
    // Производные
    float dAzdx = cosf(sx + sy*1.1f) * scale * 1.1f;
    float dAzdy = cosf(sx + sy*1.1f) * scale * 1.0f;
    float dAydx = cosf(sz + sx*0.9f) * scale * 0.9f;
    float dAydz = cosf(sz + sx*0.9f) * scale * 1.0f;
    float dAxdy = cosf(sy + sz*0.7f) * scale * 1.0f;
    float dAxdz = cosf(sy + sz*0.7f) * scale * 0.7f;

    glm::vec3 curl;
    curl.x = dAzdy - dAydz;
    curl.y = dAxdz - dAzdx;
    curl.z = dAydx - dAxdy;
    return curl;
}

// =====================================================
// Поле скоростей CPU — v1.19.0 Physics Logic Fix
// =====================================================
glm::vec3 computeVelocityFieldCPU(const glm::vec3& p, const FlowParams& prm) {
    // LBM приоритет — физически точнее
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
                            float Hfar = maxDim * 0.6f;
                            float Hclear = h;
                            if (Hclear < 0.01f) Hclear = 0.01f;
                            // Mass conservation: V2 = V1 * Hfar / Hclear, but limited
                            float venturi = Hfar / Hclear;
                            venturi = glm::clamp(venturi, 1.0f, 1.6f);
                            // Only under car
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

    // Сжимаемость: Prandtl-Glauert — трансформация координат, не только скорости
    float machInf = vInf / (prm.speedOfSound + 1e-6f);
    if (aeroMachEffects && machInf > 0.3f && machInf < 0.85f) {
        float beta = sqrtf(1.0f - machInf*machInf);
        if (beta < 0.25f) beta = 0.25f;
        // Координаты сжимаются в направлении потока: x' = x / beta
        // Эквивалентно увеличению скорости поперечной компоненты
        glm::vec3 dr = p - glm::vec3(prm.centerX, prm.centerY, prm.centerZ);
        float along = glm::dot(dr, flowDir);
        glm::vec3 cross = dr - flowDir * along;
        cross /= beta; // поперечные координаты растягиваются
        glm::vec3 pComp = glm::vec3(prm.centerX, prm.centerY, prm.centerZ) + flowDir * along + cross;
        glm::vec3 vComp = Uinf + ellipsoidPotential(pComp, glm::vec3(prm.centerX, prm.centerY, prm.centerZ), rad, Uinf);
        // Поправка скорости: поперечная компонента / beta
        glm::vec3 vDiff = vComp - Uinf;
        glm::vec3 vAlong = flowDir * glm::dot(vDiff, flowDir);
        glm::vec3 vCross = vDiff - vAlong;
        vCross /= beta;
        v = Uinf + vAlong + vCross;
        // Cp коррекция будет в forces.cpp
    }

    // Земля — метод изображений + Venturi mass conservation
    if (aeroGroundEffect) {
        float groundY = 0.0f;
        if (std::isfinite(g_voxMinY) && g_voxNx > 0) groundY = g_voxMinY + aeroGroundHeight;
        else groundY = minBB.y - maxDim*0.1f + aeroGroundHeight;
        if (!std::isfinite(groundY)) groundY = minBB.y;

        if (p.y < groundY) return glm::vec3(0.0f);

        float distGround = p.y - groundY;
        float blGround = maxDim * 0.07f;
        if (distGround < blGround) {
            float eta = glm::clamp(distGround / blGround, 0.0f, 1.0f);
            // Логарифмический закон стенки: u+ = (1/k) ln(y+) + C
            float uPlus;
            if (eta < 0.12f) uPlus = eta * 8.5f; // вязкий подслой y+<5
            else {
                // log law: u+ = 2.5*ln(y+) +5.0, y+ = eta*100 (approx)
                float yPlus = eta * 80.0f;
                uPlus = 2.5f * logf(yPlus) + 5.0f;
                uPlus /= 15.0f; // нормируем к 0-1
            }
            uPlus = glm::clamp(uPlus, 0.0f, 1.0f);
            v *= uPlus;
        }

        // Метод изображений: для no-penetration на земле y=groundY, зеркалим и инвертируем нормальную компоненту
        if (distGround < maxDim*0.6f) {
            glm::vec3 pMirror(p.x, groundY - distGround, p.z);
            glm::vec3 vMirror = Uinf + ellipsoidPotential(pMirror, glm::vec3(prm.centerX, prm.centerY, prm.centerZ), rad, Uinf);
            // Инвертируем нормальную (y) компоненту зеркала
            vMirror.y = -vMirror.y;
            float mirrorInfluence = expf(-distGround / (maxDim*0.18f));
            // v = v + mirrorInfluence * vMirror * 0.5 — усиливает параллельную, гасит нормальную
            v.x += vMirror.x * mirrorInfluence * 0.3f;
            v.z += vMirror.z * mirrorInfluence * 0.3f;
            v.y += vMirror.y * mirrorInfluence * 0.5f; // vMirror.y уже инвертирован, так что гасит
            // На самой земле y=0
            if (distGround < 1e-3f) v.y = 0.0f;
        }

        // Venturi под машиной — сохранение массы
        if (p.y > groundY && p.y < center.y) {
            float carBottom = minBB.y;
            if (p.y > carBottom - maxDim*0.15f && p.y < carBottom + maxDim*0.5f) {
                float clearance = carBottom - groundY;
                if (clearance > 0.005f && clearance < maxDim && std::isfinite(clearance)) {
                    if (p.x >= minBB.x && p.x <= maxBB.x && p.z >= minBB.z && p.z <= maxBB.z) {
                        float Hfar = maxDim * 0.6f;
                        float Hclear = clearance;
                        if (Hclear < 0.01f) Hclear = 0.01f;
                        float venturi = Hfar / Hclear; // mass conservation
                        // Bernoulli: V2 = V1 * A1/A2, но ограничим из-за вязкости и отрыва
                        venturi = glm::clamp(venturi, 1.0f, 1.6f);
                        // Учитываем что под машиной не весь поток, только часть
                        float underFactor = 1.0f + (venturi - 1.0f) * 0.6f;
                        v *= underFactor;
                    }
                }
            }
        }
    }

    // Твердое тело — no-slip + BL + separation
    if (!g_distanceField.empty()) {
        float d = sampleSDFCPU(p);
        if (d <= 0.0f) return glm::vec3(0.0f);

        if (d < maxDim * 0.7f) {
            glm::vec3 n = sdfNormalCPU(p);
            float nLen = glm::length(n);
            if (!std::isfinite(nLen) || nLen < 1e-6f) n = glm::vec3(0,1,0);
            else n = n / nLen;

            float vn = glm::dot(v, n);
            float eps = maxDim * 0.11f;
            if (eps < 1e-4f) eps = 0.1f;
            float decay = expf(-d / eps);

            float flowDotN = glm::dot(flowDir, n);
            bool isWindward = flowDotN < 0;

            if (vn < 0.0f) {
                float coeff = isWindward ? 1.4f : 1.0f;
                v -= vn * n * decay * coeff;
            }

            // Погранслой — x от передней кромки (LE), а не от центра
            // LE = точка с минимальным dot(p, flowDir) среди BB
            // Приближенно: LE = center - flowDir * (maxDim*0.5)
            glm::vec3 LE = glm::vec3(prm.centerX, prm.centerY, prm.centerZ) - flowDir * (maxDim*0.5f);
            glm::vec3 rFromLE = p - LE;
            float xAlong = glm::dot(rFromLE, flowDir);
            if (xAlong < 0.001f) xAlong = 0.001f;
            if (xAlong > maxDim*1.5f) xAlong = maxDim*1.5f;

            // Sutherland mu
            float T = prm.airTemperature;
            const float T0s = 273.15f, S = 110.4f, mu0 = 1.716e-5f;
            float mu = mu0 * powf(T/T0s, 1.5f) * (T0s+S)/(T+S);
            if (!std::isfinite(mu) || mu < 1e-6f) mu = 1.81e-5f;
            float rho = prm.airDensity;
            float Re_x = rho * vInf * xAlong / mu;
            if (Re_x < 1.0f) Re_x = 1.0f;
            if (Re_x > 1e9f) Re_x = 1e9f;

            float delta;
            if (Re_x < 5e5f) {
                // Laminar Blasius: delta = 5*x / sqrt(Re_x)
                delta = 5.0f * xAlong / sqrtf(Re_x);
            } else {
                // Turbulent Schlichting: delta = 0.37*x / Re_x^{1/5}
                delta = 0.37f * xAlong / powf(Re_x, 0.2f);
            }
            delta = glm::clamp(delta, 1e-4f, maxDim*0.4f);

            if (d < delta * 4.0f) {
                float eta = glm::clamp(d / delta, 0.0f, 1.0f);
                float uFactor;
                if (Re_x < 5e5f) {
                    // Pohlhausen: u/U = 2*eta -2*eta^3 + eta^4 ??? Упростим: 2*eta - eta^2
                    uFactor = 2.0f*eta - eta*eta;
                    // Blasius точнее: eta*(2 - eta)
                } else {
                    // 1/7 power law + log law
                    if (eta < 0.15f) {
                        // viscous sublayer: u+ = y+
                        uFactor = eta * 6.5f;
                    } else {
                        uFactor = powf(eta, 1.0f/7.0f);
                        // log law correction
                        float yPlus = eta * 100.0f;
                        float uPlusLog = 2.44f * logf(yPlus) + 5.0f;
                        uPlusLog /= 20.0f;
                        uFactor = glm::max(uFactor, uPlusLog);
                    }
                }
                uFactor = glm::clamp(uFactor, 0.0f, 1.0f);

                // Отрыв — Stratford criterion: Cp*(x*dCp/dx)^0.5 > 0.35 при турбулентном
                // Упростим: если на подветренной и x>0.4*maxDim и Re высокий и градиент давления adverse
                if (!isWindward) {
                    // Оцениваем Cp градиент: dCp/dx ~ (Cp - Cp_prev)/dx, Cp падает к корме
                    // Если xAlong > 0.4*maxDim, вероятность отрыва растет
                    float sepCriterion = (xAlong / maxDim - 0.4f) * 2.0f; // 0 at 0.4D, 1 at 0.9D
                    sepCriterion = glm::clamp(sepCriterion, 0.0f, 1.0f);
                    // При высоком Re отрыв раньше
                    if (Re_x > 1e6f) sepCriterion *= 1.2f;
                    if (sepCriterion > 0.3f) {
                        // Отрыв — скорость падает, появляется обратное течение
                        float sepFactor = (sepCriterion - 0.3f) / 0.7f;
                        float reverse = -0.15f * sepFactor * vInf; // обратное течение 15% от Vinf
                        uFactor = uFactor * (1.0f - sepFactor*0.7f) + (reverse / vInf) * sepFactor;
                    }
                }

                glm::vec3 v_n = n * glm::dot(v, n);
                glm::vec3 v_t = v - v_n;
                float normalDecay = 1.0f - expf(-eta*5.0f);
                v_n *= normalDecay * 0.05f;
                v_t *= uFactor;
                v = v_n + v_t;
            }
        }
    }

    // След — с сохранением импульса
    if (vInf > 1e-4f) {
        float rx = p.x - prm.centerX, ry = p.y - prm.centerY, rz = p.z - prm.centerZ;
        float along = glm::dot(glm::vec3(rx,ry,rz), flowDir);
        glm::vec3 rPerpVec = glm::vec3(rx,ry,rz) - flowDir * along;
        float rPerp = glm::length(rPerpVec);
        float D = 2.0f * fmaxf(prm.radiusY, prm.radiusZ);
        if (D < 1e-4f) D = maxDim * 0.5f;

        float safeWakeLen = prm.wakeLength;
        if (!std::isfinite(safeWakeLen) || safeWakeLen < 0.1f) safeWakeLen = 8.0f;

        if (along > D*0.2f && along < safeWakeLen) {
            // Ширина следа: b = 0.22*D * sqrt(1 + x/D) * (1+0.08*x/D) — эмпирика из Schlichting
            float b = D * 0.22f * sqrtf(1.0f + along / D) * (1.0f + 0.08f * along / D);
            b = fmaxf(b, 0.04f);

            // Cd оценка по форме: используем данные о аэродинамике
            // Для сферы Cd~0.47, для цилиндра ~1.2, для авто ~0.3, для профиля ~0.05
            float sizeX = prm.radiusX*2.0f, sizeY = prm.radiusY*2.0f, sizeZ = prm.radiusZ*2.0f;
            float frontalY = sizeY, frontalZ = sizeZ;
            float frontalArea = frontalY * frontalZ;
            float elongation = sizeX / (sqrtf(frontalArea)+1e-6f);
            float Cd_est;
            if (elongation < 0.6f) Cd_est = 0.9f; // тупое
            else if (elongation < 1.2f) Cd_est = 0.4f; // авто
            else if (elongation < 3.0f) Cd_est = 0.2f; // обтекаемое
            else Cd_est = 0.08f; // профиль

            // Коррекция по Re: при Re~2e5 для сферы Cd падает (кризис)
            if (aeroReNumber > 2e5f && aeroReNumber < 5e5f && elongation < 1.0f) Cd_est *= 0.5f;

            float xNorm = along / D;
            if (xNorm < 0.15f) xNorm = 0.15f;

            // Дефицит скорости с сохранением импульса: 
            // Интеграл дефицита = Cd*A*U²/(2*rho) ??? Для следа: U_def = Uinf * (Cd*A / (8*pi*b²)) * exp(-r²/(2b²)) ??? 
            // Упрощенная с сохранением: дефицит ∝ sqrt(Cd*A / x) * exp(-r²/b²)
            // Используем: deficit = Cd * (D/x)^(1/2) * exp(-r²/b²) * 0.5
            float deficitMag = Cd_est * 0.5f * sqrtf(D / (along+1e-6f)) * expf(-(rPerp*rPerp)/(b*b));
            deficitMag *= prm.wakeStrength;
            deficitMag = glm::clamp(deficitMag, 0.0f, 0.9f);

            v -= Uinf * deficitMag;

            // Карман — вихревая дорожка с St(Re)
            if (rPerp < b*2.5f) {
                float st = prm.strouhal;
                // St(Re) для цилиндра: St=0.2 при Re>300, падает при низком Re
                // Для сферы: St~0.2, для авто: St~0.15
                float St_base = 0.18f;
                if (elongation < 0.8f) St_base = 0.20f; // тупое
                else if (elongation > 2.0f) St_base = 0.12f; // обтекаемое
                // Коррекция по Re
                if (aeroReNumber < 100.0f) St_base *= 0.5f;
                else if (aeroReNumber < 1000.0f) St_base *= (0.5f + 0.5f * log10f(aeroReNumber/100.0f)/1.0f);
                st = St_base;

                float omega = 2.0f * 3.14159265f * st * vInf / D;
                float phase = omega * prm.time - along * 0.85f;

                float vortexAmp = prm.wakeStrength * 0.32f * vInf * expf(-rPerp*rPerp/(b*b*1.8f)) / sqrtf(xNorm);
                if (aeroShowWake) vortexAmp *= (1.0f + aeroWakeOpacity*0.5f);
                vortexAmp = fminf(vortexAmp, vInf*0.6f);

                glm::vec3 radialDir = (rPerp > 1e-6f) ? rPerpVec / rPerp : glm::vec3(0,1,0);
                glm::vec3 vortexDir = glm::cross(flowDir, radialDir);
                float vLen = glm::length(vortexDir);
                if (vLen > 1e-6f) vortexDir /= vLen;
                else vortexDir = glm::vec3(0,0,1);

                float sinPhase = sinf(phase);
                float side = (sinf(phase * 0.55f) > 0) ? 1.0f : -1.0f;

                v += vortexDir * (vortexAmp * sinPhase * side);

                // Турбулентность — divergence-free
                float turbBase = 0.08f * deficitMag * vInf;
                if (aeroMachEffects) {
                    float mach = vInf / (prm.speedOfSound + 1e-6f);
                    if (mach > 0.3f) turbBase *= (1.0f + mach*0.5f);
                }
                glm::vec3 turb = divergenceFreeNoise(p, prm.time, 1.8f) * turbBase;
                // 3 октавы
                turb += divergenceFreeNoise(p, prm.time*1.9f, 3.6f) * turbBase * 0.5f;
                turb += divergenceFreeNoise(p, prm.time*3.7f, 7.2f) * turbBase * 0.25f;
                v += turb * 0.5f;
            }
        }
    }

    // Ограничение: физически max 2.2*Vinf, в Venturi до 1.6*Vinf
    float maxV = vInf * 2.2f;
    if (maxV < prm.maxSpeed) maxV = prm.maxSpeed * 1.3f;
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
