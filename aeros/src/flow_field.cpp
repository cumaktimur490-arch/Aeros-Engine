#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>

#include <cmath>

#include "globals.h"
#include "voxel_grid.h"
#include "flow_field.h"

// =====================================================
// FlowParams
// =====================================================
void updateFlowParams() {
    flowParams.centerX = center.x;
    flowParams.centerY = center.y;
    flowParams.centerZ = center.z;

    flowParams.radiusX = (maxBB.x - minBB.x) * 0.5f;
    flowParams.radiusY = (maxBB.y - minBB.y) * 0.5f;
    flowParams.radiusZ = (maxBB.z - minBB.z) * 0.5f;
    if (flowParams.radiusX < 0.001f) flowParams.radiusX = 0.1f;
    if (flowParams.radiusY < 0.001f) flowParams.radiusY = 0.1f;
    if (flowParams.radiusZ < 0.001f) flowParams.radiusZ = 0.1f;

    float az = glm::radians(flowAzimuth);
    float el = glm::radians(flowElevation);
    glm::vec3 dir(cosf(el) * cosf(az), sinf(el), cosf(el) * sinf(az));
    dir = glm::normalize(dir);

    flowParams.vx = dir.x * flowSpeed;
    flowParams.vy = dir.y * flowSpeed;
    flowParams.vz = dir.z * flowSpeed;

    flowParams.time      = (float)glfwGetTime();
    flowParams.timeScale = timeScale;
    flowParams.strouhal  = strouhal;
    flowParams.wakeStrength = wakeStrength;
    flowParams.wakeLength   = wakeLength;

    float margin = 0.5f * maxDim;
    flowParams.minX = minBB.x - margin;
    flowParams.maxX = maxBB.x + margin;
    flowParams.minY = minBB.y - margin;
    flowParams.maxY = maxBB.y + margin;
    flowParams.minZ = minBB.z - margin;
    flowParams.maxZ = maxBB.z + margin;

    flowParams.maxSpeed = maxSpeedForColor;

    flowParams.gridNx = g_voxNx;
    flowParams.gridNy = g_voxNy;
    flowParams.gridNz = g_voxNz;
    flowParams.gridMinX = g_voxMinX;
    flowParams.gridMinY = g_voxMinY;
    flowParams.gridMinZ = g_voxMinZ;
    flowParams.gridMaxX = g_voxMaxX;
    flowParams.gridMaxY = g_voxMaxY;
    flowParams.gridMaxZ = g_voxMaxZ;
    flowParams.cellSizeX = (g_voxMaxX - g_voxMinX) / fmaxf((float)g_voxNx, 1.0f);
    flowParams.cellSizeY = (g_voxMaxY - g_voxMinY) / fmaxf((float)g_voxNy, 1.0f);
    flowParams.cellSizeZ = (g_voxMaxZ - g_voxMinZ) / fmaxf((float)g_voxNz, 1.0f);
    flowParams.gridCellCount = g_voxNx * g_voxNy * g_voxNz;
}

// =====================================================
// Поле скоростей CPU (усиленный boost)
// =====================================================
glm::vec3 computeVelocityFieldCPU(const glm::vec3& p, const FlowParams& prm) {
    glm::vec3 v(prm.vx, prm.vy, prm.vz);

    if (!g_distanceField.empty()) {
        float d = sampleSDFCPU(p);
        if (d > 0.0f && d < 100.0f) {
            glm::vec3 n = sdfNormalCPU(p);
            float k = 3.0f * prm.cellSizeX;
            float factor = expf(-d / k);
            if (factor > 1e-4f) {
                float vn = glm::dot(v, n);
                v -= factor * vn * n;

                if (d < 8.0f * prm.cellSizeX) {
                    float boost = expf(-d / (k * 2.0f)) * 0.7f;
                    v += boost * v;
                }
            }
        }
    }

    float vmag = sqrtf(prm.vx*prm.vx + prm.vy*prm.vy + prm.vz*prm.vz);
    if (vmag > 1e-4f) {
        float dx = prm.vx/vmag, dy = prm.vy/vmag, dz = prm.vz/vmag;
        float rx = p.x - prm.centerX, ry = p.y - prm.centerY, rz = p.z - prm.centerZ;
        float along = rx*dx + ry*dy + rz*dz;
        float px = rx - along*dx, py = ry - along*dy, pz = rz - along*dz;
        float perp = sqrtf(px*px + py*py + pz*pz);
        if (perp < 1e-4f) perp = 1e-4f;

        float D = 2.0f * fmaxf(prm.radiusY, prm.radiusZ);
        if (along > D*0.5f && along < prm.wakeLength) {
            float decay = expf(-(along - D*0.5f) / (prm.wakeLength * 0.4f));
            float width = expf(-perp*perp / (D*D*1.5f));
            float pnx=px/perp, pny=py/perp, pnz=pz/perp;

            float vtx = dy*pnz - dz*pny;
            float vty = dz*pnx - dx*pnz;
            float vtz = dx*pny - dy*pnx;

            float omega = 6.2831853f * prm.strouhal * vmag / D;
            float phase = omega * prm.time - along * 2.0f;
            float amp = prm.wakeStrength * decay * width * vmag;

            v.x += amp * sinf(phase) * vtx;
            v.y += amp * sinf(phase) * vty;
            v.z += amp * sinf(phase) * vtz;

            float lat = amp * 0.5f;
            v.x += lat * cosf(phase) * pnx;
            v.y += lat * cosf(phase) * pny;
            v.z += lat * cosf(phase) * pnz;

            float turb = 0.3f * amp * sinf(prm.time*3.0f + along*3.0f + perp*5.0f);
            v.x += turb; v.y += turb*0.5f; v.z += turb*0.5f;
        }
    }
    return v;
}

glm::vec3 colorForPoint(const glm::vec3& v, float sdfDist, const FlowParams& prm) {
    float speed = glm::length(v);
    float spdT = glm::clamp(speed / (prm.maxSpeed + 1e-6f), 0.0f, 1.0f);
    float cell = prm.cellSizeX;

    if (sdfDist < 1.5f * cell) {
        return glm::vec3(1.0f, 0.2f, 0.0f);
    } else if (sdfDist < 4.0f * cell) {
        float b = glm::clamp((sdfDist - 1.5f * cell) / (2.5f * cell), 0.0f, 1.0f);
        glm::vec3 hot(1.0f, 0.5f, 0.0f);
        glm::vec3 cold;
        if (spdT < 0.5f) cold = glm::vec3(1.0f, spdT*2.0f, 0.0f);
        else             cold = glm::vec3(1.0f-(spdT-0.5f)*2.0f, 1.0f, 0.0f);
        return hot * (1.0f - b) + cold * b;
    } else {
        if (spdT < 0.5f) return glm::vec3(1.0f, spdT*2.0f, 0.0f);
        else             return glm::vec3(1.0f-(spdT-0.5f)*2.0f, 1.0f, 0.0f);
    }
}
