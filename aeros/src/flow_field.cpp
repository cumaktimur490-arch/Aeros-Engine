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
// FlowParams — v1.4.0 с защитой от ошибок кода
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
    glm::vec3 dir(cosf(el) * cosf(az), sinf(el), cosf(el) * sinf(az));
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
}

// =====================================================
// Поле скоростей CPU — v1.5.0 LBM + защита
// =====================================================
glm::vec3 computeVelocityFieldCPU(const glm::vec3& p, const FlowParams& prm) {
    // LBM приоритет если включен
    if (lbmParams.enabled && lbmInitialized) {
        glm::vec3 vLBM = getLBMVelocityWorld(p);
        if (std::isfinite(vLBM.x) && std::isfinite(vLBM.y) && std::isfinite(vLBM.z)) {
            float mag = glm::length(vLBM);
            if (mag < 1e-6f) {
                // внутри твердого — небольшой поток для Cp
                return glm::vec3(prm.vx, prm.vy, prm.vz) * 0.1f;
            }
            return vLBM;
        }
    }

    if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)) {
        return glm::vec3(prm.vx, prm.vy, prm.vz);
    }
    if (!std::isfinite(prm.vx) || !std::isfinite(prm.vy) || !std::isfinite(prm.vz)) {
        return glm::vec3(0.0f);
    }
    glm::vec3 v(prm.vx, prm.vy, prm.vz);
    float vmag = sqrtf(prm.vx*prm.vx + prm.vy*prm.vy + prm.vz*prm.vz);
    if (!std::isfinite(vmag) || vmag < 1e-4f) vmag = 1e-4f;

    if (!g_distanceField.empty()) {
        float d = sampleSDFCPU(p);
        if (!std::isfinite(d)) d = 1000.0f;
        if (d > 0.0f && d < 100.0f) {
            glm::vec3 n = sdfNormalCPU(p);
            if (!std::isfinite(n.x) || !std::isfinite(n.y) || !std::isfinite(n.z)) n = glm::vec3(0,1,0);
            float csx = prm.cellSizeX;
            if (!std::isfinite(csx) || csx < 1e-6f) csx = 0.1f;
            float k = 2.5f * csx;
            if (k < 1e-6f) k = 0.1f;
            float factor = expf(-d / k);
            if (factor > 1e-4f) {
                float vn = glm::dot(v, n);
                if (vn < 0.0f) v -= factor * vn * n;
                else v -= factor * 0.3f * vn * n;
                if (d < 6.0f * prm.cellSizeX) {
                    float distNorm = d / (6.0f * prm.cellSizeX);
                    float boundaryFactor = 1.0f - 0.4f * expf(-distNorm * 3.0f);
                    glm::vec3 v_n = n * glm::dot(v, n);
                    glm::vec3 v_t = v - v_n;
                    float vtMag = glm::length(v_t);
                    if (vtMag > 1e-6f) {
                        float dTmp = (distNorm - 0.3f) * 2.5f;
                        float boostProfile = expf(-dTmp * dTmp);
                        float tangentialBoost = boostProfile * 0.6f;
                        v_t *= (1.0f + tangentialBoost);
                        v = v_n * boundaryFactor + v_t;
                    } else {
                        v *= boundaryFactor;
                    }
                }
            }
        } else if (d <= 0.0f) {
            v *= 0.1f;
        }
    }

    if (vmag > 1e-4f) {
        float dx = prm.vx/vmag, dy = prm.vy/vmag, dz = prm.vz/vmag;
        float rx = p.x - prm.centerX, ry = p.y - prm.centerY, rz = p.z - prm.centerZ;
        float along = rx*dx + ry*dy + rz*dz;
        float px = rx - along*dx, py = ry - along*dy, pz = rz - along*dz;
        float perp = sqrtf(px*px + py*py + pz*pz);
        if (perp < 1e-4f) perp = 1e-4f;
        float D = 2.0f * fmaxf(prm.radiusY, prm.radiusZ);
        if (D < 1e-4f) D = 0.5f;
        float safeWakeLen = prm.wakeLength;
        if (!std::isfinite(safeWakeLen) || safeWakeLen < 0.1f) safeWakeLen = 8.0f;
        if (along > D*0.3f && along < safeWakeLen) {
            float denomWL = safeWakeLen * 0.5f;
            if (denomWL < 1e-6f) denomWL = 0.5f;
            float decay = expf(-(along - D*0.3f) / denomWL);
            float wakeWidth = D * (0.5f + 0.5f * along / safeWakeLen);
            if (wakeWidth < 1e-6f) wakeWidth = 0.1f;
            float width = expf(-perp*perp / (wakeWidth*wakeWidth*1.2f));
            float pnx=px/perp, pny=py/perp, pnz=pz/perp;
            float vtx = dy*pnz - dz*pny;
            float vty = dz*pnx - dx*pnz;
            float vtz = dx*pny - dy*pnx;
            float st = prm.strouhal;
            if (!std::isfinite(st) || st < 1e-6f) st = 0.2f;
            float omega = 6.2831853f * st * vmag / D;
            float phase = omega * prm.time - along * 1.5f;
            float amp = prm.wakeStrength * decay * width * vmag * 0.8f;
            float sinPhase = sinf(phase);
            float side = (sinf(phase * 0.5f) > 0) ? 1.0f : -1.0f;
            v.x += amp * sinPhase * vtx * side;
            v.y += amp * sinPhase * vty * side;
            v.z += amp * sinPhase * vtz * side;
            float deficit = 0.3f * decay * width;
            v -= glm::vec3(prm.vx, prm.vy, prm.vz) * deficit;
            float lat = amp * 0.3f * cosf(phase);
            v.x += lat * pnx;
            v.y += lat * pny;
            v.z += lat * pnz;
            float turbScale = 0.15f * amp;
            float turbX = turbScale * sinf(prm.time*4.3f + along*2.1f + perp*3.7f + p.x*1.3f);
            float turbY = turbScale * sinf(prm.time*3.7f + along*2.8f + perp*4.1f + p.y*1.7f);
            float turbZ = turbScale * sinf(prm.time*5.1f + along*1.9f + perp*3.3f + p.z*1.1f);
            v.x += turbX;
            v.y += turbY;
            v.z += turbZ;
        }
    }
    return v;
}

glm::vec3 colorForPoint(const glm::vec3& v, float sdfDist, const FlowParams& prm) {
    if (!std::isfinite(v.x) || !std::isfinite(v.y) || !std::isfinite(v.z)) return glm::vec3(1,0,0);
    if (!std::isfinite(sdfDist)) sdfDist = 1000.0f;
    float speed = glm::length(v);
    if (!std::isfinite(speed)) speed = 0.0f;
    float safeMaxSpeed = prm.maxSpeed;
    if (!std::isfinite(safeMaxSpeed) || safeMaxSpeed < 1e-6f) safeMaxSpeed = 5.0f;
    float spdT = glm::clamp(speed / (safeMaxSpeed + 1e-6f), 0.0f, 1.0f);
    float cell = prm.cellSizeX;
    if (!std::isfinite(cell) || cell < 1e-6f) cell = 0.1f;
    if (sdfDist < 1.5f * cell) return glm::vec3(1.0f, 0.2f, 0.0f);
    else if (sdfDist < 4.0f * cell) {
        float b = glm::clamp((sdfDist - 1.5f * cell) / (2.5f * cell), 0.0f, 1.0f);
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
