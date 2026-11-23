// =====================================================
// CPU stub for CUDA backend — используется когда CUDA не доступна
// или для сборки без nvcc (CI, x86 fallback)
// Реализует тот же интерфейс что и kernel.cu, но на CPU
// Физика должна полностью совпадать с CPU версией из flow_field.cpp / particles.cpp / forces.cpp
// =====================================================

#include "cuda_api.h"
#include "flow_field.h"
#include "voxel_grid.h"
#include "globals.h"
#include "lbm.h"        // v1.26.0 — общая цветовая карта давления
#include <glm/glm.hpp>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

// v1.25.0 — диагностика: в сборке без nvcc CUDA-бэкенда нет физически,
// поэтому честно сообщаем об этом вместо молчаливого "не работает".
extern "C" void cudaProbeBackend(CudaBackendInfo* out) {
    if (!out) return;
    std::memset(out, 0, sizeof(*out));
    out->compiledWithCuda = 0;
    out->deviceCount = 0;
    out->usable = 0;
    out->lastError = 0;
    std::snprintf(out->deviceName, sizeof(out->deviceName), "нет (CPU-сборка)");
    std::snprintf(out->status, sizeof(out->status),
                  "Этот бинарь собран БЕЗ CUDA (CPU_ONLY): nvcc не найден при сборке "
                  "или сборка предназначена для x86/arm64. Расчёт идёт на CPU — "
                  "это нормальный режим, видеокарта NVIDIA здесь не задействуется.");
}

extern "C" int cudaConsumeError(char* buf, int bufSize) {
    if (buf && bufSize > 0) buf[0] = '\0';
    return 0;
}

extern "C" void computeVertexPressureCUDA(const std::vector<float>& vertices,
                                          const std::vector<float>& normals,
                                          std::vector<float>& outColors,
                                          int numVertices, const FlowParams& params) {
    (void)normals;
    outColors.resize(numVertices*3);
    float vinf = sqrtf(params.vx*params.vx + params.vy*params.vy + params.vz*params.vz);
    if (vinf < 1e-4f) vinf = 1e-4f;
    for (int i = 0; i < numVertices; i++) {
        glm::vec3 p(vertices[3*i], vertices[3*i+1], vertices[3*i+2]);
        glm::vec3 v = computeVelocityFieldCPU(p, params);
        float speed = glm::length(v);
        float speedRatio = speed / vinf;
        float cp = 1.0f - speedRatio*speedRatio;

        float rx = p.x - params.centerX;
        float ry = p.y - params.centerY;
        float rz = p.z - params.centerZ;
        float fl = 1.0f / vinf;
        float along = rx*(params.vx*fl) + ry*(params.vy*fl) + rz*(params.vz*fl);
        float D = 2.0f * fmaxf(params.radiusY, params.radiusZ);
        if (along > D * 0.3f) {
            float w = (along - D*0.3f) / D;
            if (w > 1.0f) w = 1.0f;
            cp -= 0.8f * w * w;
        }
        // v1.26.0 — заглушка красит той же функцией, что и настоящий расчёт:
        // единая карта давления (общий диапазон и общая схема), вместо своей
        // зашитой шкалы (cp+1)/2. Иначе CPU-сборка и CUDA-сборка показывали
        // разные цвета для одного и того же Cp.
        // ИСПРАВЛЕНО: Cp не обрезается — диапазон для картинки ограничивает сама
        // карта цвета (pressureColorInRange -> glm::clamp), а статистика
        // авто-масштаба обязана видеть настоящие значения.
        if (!std::isfinite(cp)) cp = 0.0f;
        glm::vec3 col = getRealisticPressureColor(cp);
        outColors[3*i]   = col.x;
        outColors[3*i+1] = col.y;
        outColors[3*i+2] = col.z;
    }
}

extern "C" void setVoxelData(const int* voxel, const float* dist,
                             int nx, int ny, int nz,
                             float mnX, float mnY, float mnZ,
                             float csX, float csY, float csZ) {
    // CPU stub: данные уже в g_distanceField / g_voxelData, но для совместимости ничего не делаем
    (void)voxel; (void)dist; (void)nx; (void)ny; (void)nz;
    (void)mnX; (void)mnY; (void)mnZ; (void)csX; (void)csY; (void)csZ;
}

// v1.26.0 — в CPU-сборке буферов устройства нет, освобождать нечего.
extern "C" void cudaReleaseAll() {}
