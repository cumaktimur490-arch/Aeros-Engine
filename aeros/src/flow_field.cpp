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
// Поле скоростей CPU — улучшенная физика v1.1.0
// Более точное обтекание, пограничный слой, след
// =====================================================
glm::vec3 computeVelocityFieldCPU(const glm::vec3& p, const FlowParams& prm) {
    glm::vec3 v(prm.vx, prm.vy, prm.vz);
    float vmag = sqrtf(prm.vx*prm.vx + prm.vy*prm.vy + prm.vz*prm.vz);
    if (vmag < 1e-4f) vmag = 1e-4f;

    // --- Обтекание через SDF ---
    if (!g_distanceField.empty()) {
        float d = sampleSDFCPU(p); // мировые единицы
        if (d > 0.0f && d < 100.0f) {
            glm::vec3 n = sdfNormalCPU(p);
            float k = 2.5f * prm.cellSizeX; // толщина влияния
            float factor = expf(-d / k);
            if (factor > 1e-4f) {
                float vn = glm::dot(v, n);
                // Убираем нормальную компоненту (непротекание)
                if (vn < 0.0f) {
                    v -= factor * vn * n;
                } else {
                    // Если поток от поверхности — меньше коррекции
                    v -= factor * 0.3f * vn * n;
                }

                // Пограничный слой и ускорение на боках (Бернулли)
                if (d < 6.0f * prm.cellSizeX) {
                    float distNorm = d / (6.0f * prm.cellSizeX);
                    // Пограничный слой: ближе к поверхности скорость падает из-за вязкости
                    float boundaryFactor = 1.0f - 0.4f * expf(-distNorm * 3.0f);
                    // Но на боках — ускорение из-за сужения струек
                    float tangentialBoost = 0.0f;
                    // Вычисляем тангенциальную компоненту
                    glm::vec3 v_n = n * glm::dot(v, n);
                    glm::vec3 v_t = v - v_n;
                    float vtMag = glm::length(v_t);
                    if (vtMag > 1e-6f) {
                        // Ускорение максимально на расстоянии ~1-2 ячейки
                        float boostProfile = expf(-powf((distNorm - 0.3f) * 2.5f, 2.0f));
                        tangentialBoost = boostProfile * 0.6f;
                        v_t *= (1.0f + tangentialBoost);
                        v = v_n * boundaryFactor + v_t;
                    } else {
                        v *= boundaryFactor;
                    }
                }
            }
        } else if (d <= 0.0f) {
            // Внутри объекта — нулевая скорость (для давления, частицы обрабатываются отдельно)
            // Не возвращаем 0 сразу, чтобы сохранить направление для Cp
            // Но уменьшаем сильно
            v *= 0.1f;
        }
    }

    // --- Вихревой след ---
    if (vmag > 1e-4f) {
        float dx = prm.vx/vmag, dy = prm.vy/vmag, dz = prm.vz/vmag;
        float rx = p.x - prm.centerX, ry = p.y - prm.centerY, rz = p.z - prm.centerZ;
        float along = rx*dx + ry*dy + rz*dz;
        float px = rx - along*dx, py = ry - along*dy, pz = rz - along*dz;
        float perp = sqrtf(px*px + py*py + pz*pz);
        if (perp < 1e-4f) perp = 1e-4f;

        float D = 2.0f * fmaxf(prm.radiusY, prm.radiusZ);
        if (D < 1e-4f) D = 0.5f;

        // След только за объектом
        if (along > D*0.3f && along < prm.wakeLength) {
            float decay = expf(-(along - D*0.3f) / (prm.wakeLength * 0.5f));
            // Ширина следа растёт с расстоянием
            float wakeWidth = D * (0.5f + 0.5f * along / prm.wakeLength);
            float width = expf(-perp*perp / (wakeWidth*wakeWidth*1.2f));
            float pnx=px/perp, pny=py/perp, pnz=pz/perp;

            // Вихревая компонента (перпендикулярно потоку и радиусу)
            float vtx = dy*pnz - dz*pny;
            float vty = dz*pnx - dx*pnz;
            float vtz = dx*pny - dy*pnx;

            // Частота схода вихрей (Strouhal)
            float omega = 6.2831853f * prm.strouhal * vmag / D;
            float phase = omega * prm.time - along * 1.5f;

            // Амплитуда вихрей
            float amp = prm.wakeStrength * decay * width * vmag * 0.8f;

            // Основной вихрь Кармана — попеременные вихри
            float sinPhase = sinf(phase);
            // Чётные/нечётные вихри с разным знаком для реализма
            float side = (sinf(phase * 0.5f) > 0) ? 1.0f : -1.0f;
            v.x += amp * sinPhase * vtx * side;
            v.y += amp * sinPhase * vty * side;
            v.z += amp * sinPhase * vtz * side;

            // Дефицит скорости в следе (тень)
            float deficit = 0.3f * decay * width;
            v -= glm::vec3(prm.vx, prm.vy, prm.vz) * deficit;

            // Поперечные колебания следа
            float lat = amp * 0.3f * cosf(phase);
            v.x += lat * pnx;
            v.y += lat * pny;
            v.z += lat * pnz;

            // Мелкомасштабная турбулентность — более физичная
            float turbScale = 0.15f * amp;
            // Используем разные частоты для каждой компоненты
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
