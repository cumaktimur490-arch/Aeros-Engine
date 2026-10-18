#include <cuda_runtime.h>
#include <device_launch_parameters.h>
#include <vector>
#include <iostream>
#include <cmath>
#include <cstdio>
#include <cstring>
#include "flow_params.h"
#include "cuda_api.h"

// =====================================================
// v1.25.0 — диагностика CUDA. Любая ошибка теперь запоминается и отдаётся
// наружу через cudaConsumeError(), чтобы main.cpp/particles.cpp могли
// переключиться на CPU и записать причину в отладочный журнал.
// =====================================================
static char g_cudaErrMsg[768] = {0};
static int  g_cudaErrCode = 0;

// Подсказка по-русски для типовых кодов ошибок CUDA
static const char* cudaHintRu(cudaError_t e) {
    switch (e) {
        case cudaErrorNoDevice:
            return "Система не видит GPU NVIDIA. Проверьте, что карта установлена и стоит драйвер с nvidia.com (а не базовый драйвер Microsoft).";
        case cudaErrorInsufficientDriver:
            return "Драйвер NVIDIA слишком старый для этой сборки CUDA. Обновите драйвер (для CUDA 13.x нужна ветка 580.xx и новее).";
        case cudaErrorCallRequiresNewerDriver:
            return "Этот вызов CUDA требует более нового драйвера NVIDIA. Обновите драйвер с сайта nvidia.com.";
        case cudaErrorStubLibrary:
            return "Загружена библиотека-заглушка CUDA, а не настоящий драйвер. Переустановите драйвер NVIDIA (чистая установка).";
        case cudaErrorNoKernelImageForDevice:
            return "GPU старше Turing (compute capability ниже 7.5): в этой сборке нет машинного кода для такой архитектуры, только PTX для 7.5+. Работает CPU-бэкенд.";
        case cudaErrorDevicesUnavailable:
            return "GPU занят другим процессом или переведён в режим Exclusive. Закройте программы, использующие видеокарту.";
        case cudaErrorMemoryAllocation:
            return "Не хватило памяти GPU для выделения буфера.";
        case cudaErrorInitializationError:
            return "Инициализация CUDA не удалась. Помогает чистая переустановка драйвера NVIDIA (галочка Perform clean installation).";
        case cudaErrorIllegalAddress:
            return "Ядро GPU обратилось к недопустимому адресу. Расчёт прерван, работает CPU-бэкенд — пришлите отладочный отчёт (F10).";
        case cudaErrorLaunchTimeout:
            return "Ядро GPU не завершилось вовремя (сработал TDR Windows). Уменьшите число частиц или разрешение сетки LBM.";
        case cudaErrorLaunchOutOfResources:
            return "Не хватило ресурсов GPU для запуска ядра (память или регистры). Уменьшите число частиц/разрешение сетки.";
        case cudaErrorAssert:
            return "Сработала проверка внутри ядра GPU: физика получила недопустимые данные. Работает CPU-бэкенд.";
        case cudaErrorInvalidDevicePointer:
            return "Передан неверный указатель на память GPU (буфер освобождён или принадлежит другому контексту).";
        case cudaErrorLaunchFailure:
            return "Сбой ядра GPU при выполнении (некорректный адрес или перегрев).";
        case cudaErrorInvalidValue:
            return "Передан неверный параметр в вызов CUDA (обычно нулевой размер буфера).";
        default:
            return "";
    }
}

static void cudaRecordErrorAt(cudaError_t e, const char* file, int line) {
    g_cudaErrCode = (int)e;
    const char* hint = cudaHintRu(e);
    if (hint && hint[0])
        std::snprintf(g_cudaErrMsg, sizeof(g_cudaErrMsg), "%s:%d — %s (код %d). %s",
                      file ? file : "?", line, cudaGetErrorString(e), (int)e, hint);
    else
        std::snprintf(g_cudaErrMsg, sizeof(g_cudaErrMsg), "%s:%d — %s (код %d)",
                      file ? file : "?", line, cudaGetErrorString(e), (int)e);
    std::cerr << "CUDA " << g_cudaErrMsg << std::endl;
}

extern "C" int cudaConsumeError(char* buf, int bufSize) {
    if (g_cudaErrCode == 0) return 0;
    if (buf && bufSize > 0) std::snprintf(buf, (size_t)bufSize, "%s", g_cudaErrMsg);
    int code = g_cudaErrCode;
    g_cudaErrCode = 0;
    g_cudaErrMsg[0] = '\0';
    return code;
}

#define CUDA_CHECK(err) do { cudaError_t e=(err); if(e!=cudaSuccess){ \
    cudaRecordErrorAt(e, __FILE__, __LINE__); return; } } while(0)

#define CUDA_CHECK_RET0(err) do { cudaError_t e=(err); if(e!=cudaSuccess){ \
    cudaRecordErrorAt(e, __FILE__, __LINE__); return 0.0f; } } while(0)

__global__ void addOneKernel(float* d, int n) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i < n) d[i] += 1.0f;
}

extern "C" float runCudaTest(const std::vector<float>& in) {
    int n = (int)in.size();
    if (n == 0) return 0.0f;
    float* d;
    CUDA_CHECK_RET0(cudaMalloc(&d, n * sizeof(float)));
    CUDA_CHECK_RET0(cudaMemcpy(d, in.data(), n * sizeof(float), cudaMemcpyHostToDevice));
    int bs = 256, nb = (n + bs - 1) / bs;
    addOneKernel<<<nb, bs>>>(d, n);
    CUDA_CHECK_RET0(cudaGetLastError());
    CUDA_CHECK_RET0(cudaDeviceSynchronize());
    std::vector<float> r(n);
    CUDA_CHECK_RET0(cudaMemcpy(r.data(), d, n * sizeof(float), cudaMemcpyDeviceToHost));
    cudaFree(d);
    float s = 0.0f;
    for (float v : r) s += v;
    return s;
}

// ============ Проба бэкенда (v1.25.0) ============
// Отвечает на вопрос "почему CUDA не работает" честно: перечисляет устройства,
// читает их свойства и выполняет реальное ядро. Без запуска ядра ошибка 209
// (нет кода для этой архитектуры GPU) проявилась бы только внутри физики.
extern "C" void cudaProbeBackend(CudaBackendInfo* out) {
    if (!out) return;
    std::memset(out, 0, sizeof(*out));
    out->compiledWithCuda = 1;
    out->lastError = 0;

    int drv = 0, rt = 0;
    cudaDriverGetVersion(&drv);
    cudaRuntimeGetVersion(&rt);
    out->driverVersion = drv;
    out->runtimeVersion = rt;

    // v1.26.0 — проверка версии драйвера ДО запуска тестового ядра.
    // Сборка сделана на CUDA 13.x: приложения этого семейства работают на
    // драйверах ветки 580 и новее (совместимость внутри мажорной версии).
    // Драйвер, сообщающий CUDA 12.x, не запустит наши ядра, и без этой проверки
    // пользователь видел бы просто «код 35/36» вместо понятной причины.
    if (drv > 0 && drv < 13000) {
        out->usable = 0;
        out->lastError = (int)cudaErrorInsufficientDriver;
        std::snprintf(out->status, sizeof(out->status),
                      "Драйвер NVIDIA поддерживает CUDA %d.%d, а эта сборка требует CUDA 13.x "
                      "(драйвер ветки 580.xx и новее). Обновите драйвер с nvidia.com — "
                      "после этого CUDA заработает без переустановки программы. "
                      "Сейчас расчёт идёт на CPU.",
                      drv / 1000, (drv % 1000) / 10);
        return;
    }

    cudaError_t e = cudaGetDeviceCount(&out->deviceCount);
    out->lastError = (int)e;
    if (e != cudaSuccess) {
        const char* hint = cudaHintRu(e);
        std::snprintf(out->status, sizeof(out->status),
                      "CUDA недоступна: cudaGetDeviceCount вернула %s (код %d). %s",
                      cudaGetErrorString(e), (int)e, hint);
        return;
    }
    if (out->deviceCount <= 0) {
        std::snprintf(out->status, sizeof(out->status),
                      "Устройств NVIDIA не найдено (deviceCount = 0). Драйвер установлен, но GPU не виден: "
                      "проверьте подключение карты и версию драйвера.");
        return;
    }

    cudaDeviceProp prop;
    std::memset(&prop, 0, sizeof(prop));
    e = cudaGetDeviceProperties(&prop, 0);
    if (e == cudaSuccess) {
        out->ccMajor = prop.major;
        out->ccMinor = prop.minor;
        out->totalMemoryMB = (long long)(prop.totalGlobalMem / (1024 * 1024));
        std::snprintf(out->deviceName, sizeof(out->deviceName), "%s", prop.name);
    }

    // Контрольный запуск ядра
    const int n = 4;
    float h[n] = {1.0f, 2.0f, 3.0f, 4.0f};
    float* d = nullptr;
    e = cudaMalloc(&d, n * sizeof(float));
    if (e == cudaSuccess) e = cudaMemcpy(d, h, n * sizeof(float), cudaMemcpyHostToDevice);
    if (e == cudaSuccess) { addOneKernel<<<1, n>>>(d, n); e = cudaGetLastError(); }
    if (e == cudaSuccess) e = cudaDeviceSynchronize();
    if (e == cudaSuccess) e = cudaMemcpy(h, d, n * sizeof(float), cudaMemcpyDeviceToHost);
    if (d) cudaFree(d);
    out->lastError = (int)e;

    if (e == cudaSuccess && h[0] == 2.0f && h[3] == 5.0f) {
        out->usable = 1;
        std::snprintf(out->status, sizeof(out->status),
                      "CUDA работает: %s, compute %d.%d, %lld МБ, GPU в системе: %d, "
                      "драйвер CUDA %d.%d, среда выполнения %d.%d.",
                      out->deviceName, out->ccMajor, out->ccMinor, out->totalMemoryMB,
                      out->deviceCount, drv / 1000, (drv % 1000) / 10, rt / 1000, (rt % 1000) / 10);
    } else {
        out->usable = 0;
        const char* hint = cudaHintRu(e);
        std::snprintf(out->status, sizeof(out->status),
                      "GPU найден (%s, compute %d.%d), но тестовое ядро не выполнилось: %s (код %d). %s",
                      out->deviceName[0] ? out->deviceName : "имя неизвестно",
                      out->ccMajor, out->ccMinor, cudaGetErrorString(e), (int)e, hint);
    }
}

// ============ SDF на GPU ============
float* g_dDist = nullptr;

// ============ SDF-семплинг ============
__device__ bool sampleSDF(float3 p, float* dist, int nx, int ny, int nz,
                          float mnX, float mnY, float mnZ,
                          float csX, float csY, float csZ, float* outD)
{
    if (!dist) { *outD = 1000.0f; return false; }
    // v1.26.0: floorf, а не усечение. При (int)(-0.4) = 0 частица вне области
    // читала ячейку у противоположного края и получала ложное столкновение —
    // та же ошибка, что была в CPU-коде (particles.cpp, voxel_grid.cpp).
    int ix = (int)floorf((p.x - mnX) / csX);
    int iy = (int)floorf((p.y - mnY) / csY);
    int iz = (int)floorf((p.z - mnZ) / csZ);
    if (ix < 0 || ix >= nx || iy < 0 || iy >= ny || iz < 0 || iz >= nz) {
        *outD = 1000.0f;
        return false;
    }
    // Поле хранится в ВОКСЕЛЯХ (шаг BFS = 1), переводим в мировые единицы,
    // иначе зоны влияния зависят от разрешения сетки.
    *outD = dist[(iz*ny + iy)*nx + ix] * csX;
    return true;
}

__device__ float3 sdfNormal(float3 p, float* dist, int nx, int ny, int nz,
                            float mnX, float mnY, float mnZ,
                            float csX, float csY, float csZ)
{
    if (!dist) return make_float3(0, 1, 0);
    int ix = (int)((p.x - mnX) / csX);
    int iy = (int)((p.y - mnY) / csY);
    int iz = (int)((p.z - mnZ) / csZ);
    if (ix <= 0 || ix >= nx-1 || iy <= 0 || iy >= ny-1 || iz <= 0 || iz >= nz-1)
        return make_float3(0, 1, 0);

    float dx = dist[(iz*ny + iy)*nx + (ix+1)] - dist[(iz*ny + iy)*nx + (ix-1)];
    float dy = dist[(iz*ny + (iy+1))*nx + ix] - dist[(iz*ny + (iy-1))*nx + ix];
    float dz = dist[((iz+1)*ny + iy)*nx + ix] - dist[((iz-1)*ny + iy)*nx + ix];
    float len = sqrtf(dx*dx + dy*dy + dz*dz);
    if (len < 1e-6f) return make_float3(0, 1, 0);
    return make_float3(dx/len, dy/len, dz/len);
}

// ============ Поле скоростей на основе SDF — улучшено v1.1.0 ============
__device__ float3 baseFlowSDF(float3 p, FlowParams prm, float* dDist) {
    float3 v = make_float3(prm.vx, prm.vy, prm.vz);
    float vmag = sqrtf(prm.vx*prm.vx + prm.vy*prm.vy + prm.vz*prm.vz);
    if (vmag < 1e-4f) vmag = 1e-4f;

    if (!dDist || prm.gridNx <= 0) return v;

    float d;
    bool ok = sampleSDF(p, dDist, prm.gridNx, prm.gridNy, prm.gridNz,
                       prm.gridMinX, prm.gridMinY, prm.gridMinZ,
                       prm.cellSizeX, prm.cellSizeY, prm.cellSizeZ, &d);
    if (!ok) return v;
    if (d <= 0.0f) {
        // Внутри — сильно уменьшаем
        return make_float3(v.x*0.1f, v.y*0.1f, v.z*0.1f);
    }

    float3 n = sdfNormal(p, dDist, prm.gridNx, prm.gridNy, prm.gridNz,
                        prm.gridMinX, prm.gridMinY, prm.gridMinZ,
                        prm.cellSizeX, prm.cellSizeY, prm.cellSizeZ);

    float k = 2.5f * prm.cellSizeX;
    float factor = expf(-d / k);
    if (factor < 1e-4f) return v;

    float vn = v.x*n.x + v.y*n.y + v.z*n.z;
    if (vn < 0.0f) {
        v.x -= factor * vn * n.x;
        v.y -= factor * vn * n.y;
        v.z -= factor * vn * n.z;
    } else {
        v.x -= factor * 0.3f * vn * n.x;
        v.y -= factor * 0.3f * vn * n.y;
        v.z -= factor * 0.3f * vn * n.z;
    }

    if (d < 6.0f * prm.cellSizeX) {
        float distNorm = d / (6.0f * prm.cellSizeX);
        float boundaryFactor = 1.0f - 0.4f * expf(-distNorm * 3.0f);

        float3 v_n = make_float3(n.x * (v.x*n.x + v.y*n.y + v.z*n.z),
                                 n.y * (v.x*n.x + v.y*n.y + v.z*n.z),
                                 n.z * (v.x*n.x + v.y*n.y + v.z*n.z));
        float3 v_t = make_float3(v.x - v_n.x, v.y - v_n.y, v.z - v_n.z);
        float vtMag = sqrtf(v_t.x*v_t.x + v_t.y*v_t.y + v_t.z*v_t.z);
        if (vtMag > 1e-6f) {
            float dTmp = (distNorm - 0.3f) * 2.5f;
            float boostProfile = expf(-dTmp * dTmp);
            float tangentialBoost = boostProfile * 0.6f;
            v_t.x *= (1.0f + tangentialBoost);
            v_t.y *= (1.0f + tangentialBoost);
            v_t.z *= (1.0f + tangentialBoost);
            v.x = v_n.x * boundaryFactor + v_t.x;
            v.y = v_n.y * boundaryFactor + v_t.y;
            v.z = v_n.z * boundaryFactor + v_t.z;
        } else {
            v.x *= boundaryFactor;
            v.y *= boundaryFactor;
            v.z *= boundaryFactor;
        }
    }

    return v;
}

// ============ Вихревая дорожка — улучшено v1.1.0 ============
__device__ float3 wakeField(float3 p, FlowParams prm) {
    float vmag = sqrtf(prm.vx*prm.vx + prm.vy*prm.vy + prm.vz*prm.vz);
    float3 w = make_float3(0.0f, 0.0f, 0.0f);
    if (vmag < 1e-4f) return w;

    float dx = prm.vx / vmag, dy = prm.vy / vmag, dz = prm.vz / vmag;
    float rx = p.x - prm.centerX;
    float ry = p.y - prm.centerY;
    float rz = p.z - prm.centerZ;
    float along = rx*dx + ry*dy + rz*dz;
    float px = rx - along*dx, py = ry - along*dy, pz = rz - along*dz;
    float perp = sqrtf(px*px + py*py + pz*pz);
    if (perp < 1e-4f) perp = 1e-4f;

    float D = 2.0f * fmaxf(prm.radiusY, prm.radiusZ);
    if (D < 1e-4f) D = 0.5f;
    if (along < D * 0.3f || along > prm.wakeLength) return w;

    float decay = expf(-(along - D*0.3f) / (prm.wakeLength * 0.5f));
    float wakeWidth = D * (0.5f + 0.5f * along / prm.wakeLength);
    float width = expf(-perp*perp / (wakeWidth*wakeWidth*1.2f));
    float pnx = px/perp, pny = py/perp, pnz = pz/perp;

    float vtx = dy*pnz - dz*pny;
    float vty = dz*pnx - dx*pnz;
    float vtz = dx*pny - dy*pnx;

    float omega = 6.2831853f * prm.strouhal * vmag / D;
    float phase = omega * prm.time - along * 1.5f;
    float amp = prm.wakeStrength * decay * width * vmag * 0.8f;

    float sinPhase = sinf(phase);
    float side = (sinf(phase * 0.5f) > 0) ? 1.0f : -1.0f;
    w.x += amp * sinPhase * vtx * side;
    w.y += amp * sinPhase * vty * side;
    w.z += amp * sinPhase * vtz * side;

    // Дефицит скорости
    float deficit = 0.3f * decay * width;
    w.x -= prm.vx * deficit;
    w.y -= prm.vy * deficit;
    w.z -= prm.vz * deficit;

    float lat = amp * 0.3f * cosf(phase);
    w.x += lat * pnx;
    w.y += lat * pny;
    w.z += lat * pnz;

    float turbScale = 0.15f * amp;
    float turbX = turbScale * sinf(prm.time*4.3f + along*2.1f + perp*3.7f + p.x*1.3f);
    float turbY = turbScale * sinf(prm.time*3.7f + along*2.8f + perp*4.1f + p.y*1.7f);
    float turbZ = turbScale * sinf(prm.time*5.1f + along*1.9f + perp*3.3f + p.z*1.1f);
    w.x += turbX;
    w.y += turbY;
    w.z += turbZ;

    return w;
}

__device__ float3 fullField(float3 p, FlowParams prm, float* dDist) {
    float3 v = baseFlowSDF(p, prm, dDist);
    float3 w = wakeField(p, prm);
    return make_float3(v.x + w.x, v.y + w.y, v.z + w.z);
}

// ============ Ядра ============
__global__ void initParticlesKernel(float3* pos, float3* col, int n, FlowParams prm) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= n) return;

    float r1 = fabsf(sinf(i * 12.9898f + 78.233f) * 43758.5453f);
    r1 -= floorf(r1);
    float r2 = fabsf(cosf(i * 39.346f + 11.135f) * 24634.6345f);
    r2 -= floorf(r2);

    float vmag = sqrtf(prm.vx*prm.vx + prm.vy*prm.vy + prm.vz*prm.vz);
    float3 flowDir = make_float3(1, 0, 0);
    if (vmag > 1e-4f) flowDir = make_float3(prm.vx/vmag, prm.vy/vmag, prm.vz/vmag);

    float offset = 0.8f * (prm.maxX - prm.minX);
    float3 startCenter = make_float3(prm.centerX - flowDir.x * offset,
                                     prm.centerY - flowDir.y * offset,
                                     prm.centerZ - flowDir.z * offset);

    float upRef[3] = {0, 1, 0};
    if (fabsf(flowDir.y) > 0.95f) { upRef[0] = 1; upRef[1] = 0; upRef[2] = 0; }
    float3 right = make_float3(flowDir.y*upRef[2] - flowDir.z*upRef[1],
                               flowDir.z*upRef[0] - flowDir.x*upRef[2],
                               flowDir.x*upRef[1] - flowDir.y*upRef[0]);
    float rl = sqrtf(right.x*right.x + right.y*right.y + right.z*right.z);
    if (rl > 1e-6f) { right.x/=rl; right.y/=rl; right.z/=rl; }
    float3 up = make_float3(right.y*flowDir.z - right.z*flowDir.y,
                            right.z*flowDir.x - right.x*flowDir.z,
                            right.x*flowDir.y - right.y*flowDir.x);

    int side = (int)sqrtf((float)n) + 1;
    int ix = i % side, iy = i / side;
    float fx = (side <= 1) ? 0.0f : ((float)ix / (side - 1) - 0.5f) * 2.0f;
    float fy = (side <= 1) ? 0.0f : ((float)iy / (side - 1) - 0.5f) * 2.0f;

    float spread = (prm.maxX - prm.minX);
    float x = startCenter.x + right.x*fx*spread + up.x*fy*spread + (r1 - 0.5f) * spread * 0.1f;
    float y = startCenter.y + right.y*fx*spread + up.y*fy*spread + (r2 - 0.5f) * spread * 0.1f;
    float z = startCenter.z + right.z*fx*spread + up.z*fy*spread;

    pos[i] = make_float3(x, y, z);
    col[i] = make_float3(0.3f, 0.8f, 1.0f);
}

__global__ void updateParticlesKernel(float3* pos, float3* col, int n,
                                       FlowParams prm, float dt, float* dDist)
{
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= n) return;

    float3 p = pos[i];
    float3 v = fullField(p, prm, dDist);

    float3 np = make_float3(p.x + v.x * dt * prm.timeScale,
                            p.y + v.y * dt * prm.timeScale,
                            p.z + v.z * dt * prm.timeScale);

    bool collided = false;
    float surfDist = 1000.0f;

    float d;
    bool ok = sampleSDF(np, dDist, prm.gridNx, prm.gridNy, prm.gridNz,
                       prm.gridMinX, prm.gridMinY, prm.gridMinZ,
                       prm.cellSizeX, prm.cellSizeY, prm.cellSizeZ, &d);
    if (ok) surfDist = d;

    if (ok && d < 0.0f) {
        float3 nrm = sdfNormal(np, dDist, prm.gridNx, prm.gridNy, prm.gridNz,
                              prm.gridMinX, prm.gridMinY, prm.gridMinZ,
                              prm.cellSizeX, prm.cellSizeY, prm.cellSizeZ);
        float push = fabsf(d) + 0.5f * prm.cellSizeX;
        np.x += nrm.x * push;
        np.y += nrm.y * push;
        np.z += nrm.z * push;
        collided = true;

        // Улучшенное скольжение вдоль поверхности
        float vmag = sqrtf(prm.vx*prm.vx + prm.vy*prm.vy + prm.vz*prm.vz);
        if (vmag < 1e-4f) vmag = 1e-4f;

        // Нормальная компонента потока
        float vinf_n = prm.vx*nrm.x + prm.vy*nrm.y + prm.vz*nrm.z;
        // Тангенциальная составляющая потока (вдоль поверхности)
        float3 vinf_t = make_float3(prm.vx - vinf_n*nrm.x,
                                    prm.vy - vinf_n*nrm.y,
                                    prm.vz - vinf_n*nrm.z);

        // Убираем нормальную составляющую скорости частицы
        float vn = v.x*nrm.x + v.y*nrm.y + v.z*nrm.z;
        v.x -= vn * nrm.x;
        v.y -= vn * nrm.y;
        v.z -= vn * nrm.z;

        // При ударе под 90° (перпендикулярно) даём сильный тангенциальный импульс
        float perpFactor = fabsf(vinf_n) / (vmag + 1e-6f);
        float slideBoost = 1.2f + 0.8f * perpFactor;

        v.x += vinf_t.x * slideBoost * 0.6f;
        v.y += vinf_t.y * slideBoost * 0.6f;
        v.z += vinf_t.z * slideBoost * 0.6f;

        // Если скорость частицы совсем мала — полностью заменяем на касательный поток
        float spd = sqrtf(v.x*v.x + v.y*v.y + v.z*v.z);
        if (spd < 0.3f * vmag) {
            v.x = vinf_t.x * slideBoost;
            v.y = vinf_t.y * slideBoost;
            v.z = vinf_t.z * slideBoost;
        }
    } else if (ok && d < 1.5f * prm.cellSizeX) {
        collided = true;
    }

    pos[i] = np;

    float speed = sqrtf(v.x*v.x + v.y*v.y + v.z*v.z);
    float spdT = fminf(speed / (prm.maxSpeed + 1e-6f), 1.0f);

    float3 c;
    if (collided && surfDist < 1.5f * prm.cellSizeX) {
        c = make_float3(1.0f, 0.15f, 0.0f);
    } else if (surfDist < 3.0f * prm.cellSizeX) {
        float blend = (surfDist - 1.5f * prm.cellSizeX) / (1.5f * prm.cellSizeX);
        if (blend < 0.0f) blend = 0.0f;
        if (blend > 1.0f) blend = 1.0f;
        float3 hot = make_float3(1.0f, 0.5f, 0.0f);
        float3 cold;
        if (spdT < 0.5f) cold = make_float3(1.0f, spdT*2.0f, 0.0f);
        else             cold = make_float3(1.0f - (spdT-0.5f)*2.0f, 1.0f, 0.0f);
        c = make_float3(hot.x*blend + cold.x*(1.0f-blend),
                        hot.y*blend + cold.y*(1.0f-blend),
                        hot.z*blend + cold.z*(1.0f-blend));
    } else {
        if (spdT < 0.5f) c = make_float3(1.0f, spdT*2.0f, 0.0f);
        else             c = make_float3(1.0f - (spdT-0.5f)*2.0f, 1.0f, 0.0f);
    }
    col[i] = c;

    if (np.x < prm.minX || np.x > prm.maxX ||
        np.y < prm.minY || np.y > prm.maxY ||
        np.z < prm.minZ || np.z > prm.maxZ)
    {
        float r1 = fabsf(sinf(i * 12.9898f + prm.time * 10.0f) * 43758.5453f);
        r1 -= floorf(r1);
        float r2 = fabsf(cosf(i * 39.346f + prm.time * 15.0f) * 24634.6345f);
        r2 -= floorf(r2);
        pos[i] = make_float3(
            prm.minX + (prm.maxX - prm.minX) * (0.05f + 0.9f * r1),
            prm.minY + (prm.maxY - prm.minY) * (0.05f + 0.9f * r2),
            prm.minZ + 0.05f);
    }
}

// ============ Давление (с эффектом разрежения за кормой) ============
__global__ void pressureKernel(float3* verts, float3* nrms, float3* outCol,
                                int n, FlowParams prm, float* dDist)
{
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= n) return;
    float3 p = verts[i];
    float3 v = fullField(p, prm, dDist);
    float speed = sqrtf(v.x*v.x + v.y*v.y + v.z*v.z);
    float vinf = sqrtf(prm.vx*prm.vx + prm.vy*prm.vy + prm.vz*prm.vz);
    if (vinf < 1e-4f) vinf = 1e-4f;

    float speedRatio = speed / vinf;
    float cp = 1.0f - speedRatio*speedRatio;

    // Разрежение за кормой (wake suction)
    float rx = p.x - prm.centerX;
    float ry = p.y - prm.centerY;
    float rz = p.z - prm.centerZ;
    float fl = 1.0f / vinf;
    float along = rx*(prm.vx*fl) + ry*(prm.vy*fl) + rz*(prm.vz*fl);
    float D = 2.0f * fmaxf(prm.radiusY, prm.radiusZ);
    if (along > D * 0.3f) {
        float w = (along - D*0.3f) / D;
        if (w > 1.0f) w = 1.0f;
        cp -= 0.8f * w * w;
    }
    // v1.26.0: ограничение и раскраска — те же, что на CPU (aeroClampCp и
    // getRealisticPressureColor). До этого у ядра была своя зашитая шкала
    // (cp+1)/2 и свой набор цветов, поэтому при переключении CUDA/CPU модель
    // меняла окраску, а легенда в панели не соответствовала картинке.
    if (cp > 2.0f) cp = 2.0f;
    if (cp < -4.0f) cp = -4.0f;
    outCol[i] = pressureColorD(cp, prm.cpLo, prm.cpHi, prm.colorMap);
}

// =====================================================
// v1.26.0 — цветовая карта давления на устройстве.
// Точная копия getRealisticPressureColor() из lbm.cpp: устройство не может
// вызывать хост-функции, поэтому логика продублирована, но параметры
// (диапазон Cp и номер схемы) приходят в FlowParams — значит CPU и GPU
// красят одинаково при любом бэкенде.
// =====================================================
__device__ inline float3 rampColorD(float t, const float3* stops, int n) {
    if (n <= 0) return make_float3(0.5f, 0.5f, 0.5f);
    if (n == 1) return stops[0];
    if (!(t > 0.0f)) return stops[0];
    if (!(t < 1.0f)) return stops[n-1];
    float x = t * (float)(n - 1);
    int i = (int)x;
    if (i >= n - 1) i = n - 2;
    if (i < 0) i = 0;
    float k = x - (float)i;
    return make_float3(stops[i].x*(1.0f-k) + stops[i+1].x*k,
                       stops[i].y*(1.0f-k) + stops[i+1].y*k,
                       stops[i].z*(1.0f-k) + stops[i+1].z*k);
}

__device__ inline float3 pressureColorD(float cp, float lo, float hi, int colorMap) {
    if (!(cp == cp)) cp = 0.0f;                    // NaN
    if (!(hi - lo > 1e-4f)) { lo = -2.0f; hi = 2.0f; }

    if (colorMap == 1 || colorMap == 2) {
        const float3 viridis[5] = {
            make_float3(0.267f, 0.004f, 0.329f), make_float3(0.229f, 0.322f, 0.545f),
            make_float3(0.127f, 0.566f, 0.550f), make_float3(0.550f, 0.790f, 0.350f),
            make_float3(0.993f, 0.906f, 0.144f)
        };
        const float3 plasma[5] = {
            make_float3(0.050f, 0.030f, 0.528f), make_float3(0.494f, 0.012f, 0.658f),
            make_float3(0.798f, 0.280f, 0.470f), make_float3(0.973f, 0.585f, 0.254f),
            make_float3(0.940f, 0.975f, 0.131f)
        };
        float t = fminf(fmaxf((cp - lo) / (hi - lo), 0.0f), 1.0f);
        return rampColorD(t, (colorMap == 1) ? viridis : plasma, 5);
    }

    const float3 neutral = make_float3(0.93f, 0.93f, 0.95f);
    const float3 coldRainbow[4] = {
        neutral, make_float3(0.55f, 0.82f, 0.94f),
        make_float3(0.15f, 0.45f, 0.85f), make_float3(0.03f, 0.10f, 0.60f)
    };
    const float3 hotRainbow[4] = {
        neutral, make_float3(1.00f, 0.85f, 0.35f),
        make_float3(0.96f, 0.45f, 0.10f), make_float3(0.72f, 0.05f, 0.05f)
    };
    const float3 coolCold[4] = {
        neutral, make_float3(0.60f, 0.79f, 0.92f),
        make_float3(0.26f, 0.45f, 0.80f), make_float3(0.09f, 0.15f, 0.58f)
    };
    const float3 coolWarm[4] = {
        neutral, make_float3(0.96f, 0.70f, 0.60f),
        make_float3(0.86f, 0.30f, 0.22f), make_float3(0.55f, 0.02f, 0.10f)
    };
    const bool coolwarm = (colorMap == 3);
    if (cp <= 0.0f) {
        float s = (lo < -1e-6f) ? fminf(fmaxf(cp / lo, 0.0f), 1.0f) : 0.0f;
        return rampColorD(s, coolwarm ? coolCold : coldRainbow, 4);
    }
    float s = (hi > 1e-6f) ? fminf(fmaxf(cp / hi, 0.0f), 1.0f) : 0.0f;
    return rampColorD(s, coolwarm ? coolWarm : hotRainbow, 4);
}

// ============ Host API ============
// Сколько ячеек SDF реально лежит в g_dDist. Нужно, чтобы ядро не читало поле
// по размерам новой сетки, если загрузить старое/новое поле не удалось.
static long long g_dDistCount = 0;

extern "C" void setVoxelData(const int* voxel, const float* dist,
                             int nx, int ny, int nz,
                             float mnX, float mnY, float mnZ,
                             float csX, float csY, float csZ)
{
    (void)voxel; (void)mnX; (void)mnY; (void)mnZ; (void)csX; (void)csY; (void)csZ;
    long long total = (long long)nx * (long long)ny * (long long)nz;
    if (total <= 0 || !dist) {
        if (g_dDist) { cudaFree(g_dDist); g_dDist = nullptr; }
        g_dDistCount = 0;
        return;
    }
    // v1.26.0 — раньше ошибки cudaMalloc/cudaMemcpy не проверялись: при неудаче
    // g_dDist оставался освобождённым или мусорным, а ядра продолжали его
    // разыменовывать. Теперь новый буфер готовится отдельно и подменяет старый
    // только после успешной загрузки, а причина уходит в журнал.
    float* tmp = nullptr;
    cudaError_t e = cudaMalloc(&tmp, (size_t)total * sizeof(float));
    if (e != cudaSuccess) { cudaRecordErrorAt(e, __FILE__, __LINE__); return; }
    e = cudaMemcpy(tmp, dist, (size_t)total * sizeof(float), cudaMemcpyHostToDevice);
    if (e != cudaSuccess) { cudaRecordErrorAt(e, __FILE__, __LINE__); cudaFree(tmp); return; }
    if (g_dDist) cudaFree(g_dDist);
    g_dDist = tmp;
    g_dDistCount = total;
}

// Поле SDF обязано соответствовать текущей сетке из FlowParams, иначе ядро
// читало бы память за пределами буфера (сетка перестраивается на лету).
static void validateDistField(const FlowParams& prm) {
    if (!g_dDist) return;
    long long need = (long long)prm.gridNx * (long long)prm.gridNy * (long long)prm.gridNz;
    if (need <= 0 || need != g_dDistCount) {
        cudaFree(g_dDist);
        g_dDist = nullptr;
        g_dDistCount = 0;
    }
}

// =====================================================
// v1.26.0 — постоянные буферы устройства.
//
// Раньше каждый вызов (то есть каждый кадр) делал 2-3 cudaMalloc/cudaFree и
// cudaDeviceSynchronize после запуска ядра. Аллокации на устройстве дороги и
// фрагментируют память, а синхронизация дублировалась: cudaMemcpy D2H на потоке
// по умолчанию и так блокирующая. Теперь буферы живут между кадрами и растут с
// запасом, лишних синхронизаций нет, память освобождается один раз — при
// завершении (cudaReleaseAll).
// =====================================================
static float3* g_dPos   = nullptr; static size_t g_capPos   = 0;
static float3* g_dCol   = nullptr; static size_t g_capCol   = 0;
static float3* g_dVerts = nullptr; static size_t g_capVerts = 0;
static float3* g_dNorms = nullptr; static size_t g_capNorms = 0;

static bool ensureBuffer(float3** pp, size_t* cap, size_t need) {
    if (!pp || !cap || need == 0) return false;
    if (*pp && *cap >= need) return true;
    size_t want = need + need / 4 + 1024;   // запас 25%, чтобы не расти каждый кадр
    float3* tmp = nullptr;
    cudaError_t e = cudaMalloc(&tmp, want * sizeof(float3));
    if (e != cudaSuccess) { cudaRecordErrorAt(e, __FILE__, __LINE__); return false; }
    if (*pp) cudaFree(*pp);
    *pp = tmp;
    *cap = want;
    return true;
}

extern "C" void cudaReleaseAll() {
    if (g_dPos)   { cudaFree(g_dPos);   g_dPos = nullptr;   g_capPos = 0; }
    if (g_dCol)   { cudaFree(g_dCol);   g_dCol = nullptr;   g_capCol = 0; }
    if (g_dVerts) { cudaFree(g_dVerts); g_dVerts = nullptr; g_capVerts = 0; }
    if (g_dNorms) { cudaFree(g_dNorms); g_dNorms = nullptr; g_capNorms = 0; }
    if (g_dDist)  { cudaFree(g_dDist);  g_dDist = nullptr;  g_dDistCount = 0; }
}

extern "C" void initParticlesCUDA(std::vector<float>& pos, std::vector<float>& col,
                                   int n, const FlowParams& prm)
{
    if (n <= 0) return;
    try {
        pos.resize((size_t)n * 3);
        col.resize((size_t)n * 3);
    } catch (...) { return; }
    if (!ensureBuffer(&g_dPos, &g_capPos, (size_t)n)) return;
    if (!ensureBuffer(&g_dCol, &g_capCol, (size_t)n)) return;
    int bs = 256, nb = (n + bs - 1) / bs;
    initParticlesKernel<<<nb, bs>>>(g_dPos, g_dCol, n, prm);
    CUDA_CHECK(cudaGetLastError());
    CUDA_CHECK(cudaMemcpy(pos.data(), g_dPos, (size_t)n * 3 * sizeof(float), cudaMemcpyDeviceToHost));
    CUDA_CHECK(cudaMemcpy(col.data(), g_dCol, (size_t)n * 3 * sizeof(float), cudaMemcpyDeviceToHost));
}

extern "C" void updateParticlesCUDA(std::vector<float>& pos, std::vector<float>& col,
                                     int n, const FlowParams& prm, float dt)
{
    if (n <= 0) return;
    if ((int)pos.size() < n * 3 || (int)col.size() < n * 3) return;
    if (!ensureBuffer(&g_dPos, &g_capPos, (size_t)n)) return;
    if (!ensureBuffer(&g_dCol, &g_capCol, (size_t)n)) return;
    validateDistField(prm);
    CUDA_CHECK(cudaMemcpy(g_dPos, pos.data(), (size_t)n * 3 * sizeof(float), cudaMemcpyHostToDevice));
    CUDA_CHECK(cudaMemcpy(g_dCol, col.data(), (size_t)n * 3 * sizeof(float), cudaMemcpyHostToDevice));
    int bs = 256, nb = (n + bs - 1) / bs;
    updateParticlesKernel<<<nb, bs>>>(g_dPos, g_dCol, n, prm, dt, g_dDist);
    CUDA_CHECK(cudaGetLastError());
    CUDA_CHECK(cudaMemcpy(pos.data(), g_dPos, (size_t)n * 3 * sizeof(float), cudaMemcpyDeviceToHost));
    CUDA_CHECK(cudaMemcpy(col.data(), g_dCol, (size_t)n * 3 * sizeof(float), cudaMemcpyDeviceToHost));
}

extern "C" void computeVertexPressureCUDA(const std::vector<float>& verts,
                                           const std::vector<float>& nrms,
                                           std::vector<float>& outCol,
                                           int n, const FlowParams& prm)
{
    if (n <= 0) return;
    if ((int)verts.size() < n * 3 || (int)nrms.size() < n * 3) return;
    try { outCol.resize((size_t)n * 3); } catch (...) { return; }
    if (!ensureBuffer(&g_dVerts, &g_capVerts, (size_t)n)) return;
    if (!ensureBuffer(&g_dNorms, &g_capNorms, (size_t)n)) return;
    if (!ensureBuffer(&g_dCol,   &g_capCol,   (size_t)n)) return;
    validateDistField(prm);
    CUDA_CHECK(cudaMemcpy(g_dVerts, verts.data(), (size_t)n * 3 * sizeof(float), cudaMemcpyHostToDevice));
    CUDA_CHECK(cudaMemcpy(g_dNorms, nrms.data(),  (size_t)n * 3 * sizeof(float), cudaMemcpyHostToDevice));
    int bs = 256, nb = (n + bs - 1) / bs;
    pressureKernel<<<nb, bs>>>(g_dVerts, g_dNorms, g_dCol, n, prm, g_dDist);
    CUDA_CHECK(cudaGetLastError());
    CUDA_CHECK(cudaMemcpy(outCol.data(), g_dCol, (size_t)n * 3 * sizeof(float), cudaMemcpyDeviceToHost));
}