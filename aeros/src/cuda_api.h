#ifndef CUDA_API_H
#define CUDA_API_H
// =====================================================
// Интерфейс CUDA-бэкенда (реализация в kernel.cu)
// =====================================================

#include <vector>

#include "flow_params.h"

extern "C" void initParticlesCUDA(std::vector<float>& positions, std::vector<float>& colors,
                                  int numParticles, const FlowParams& params);
extern "C" void updateParticlesCUDA(std::vector<float>& positions, std::vector<float>& colors,
                                    int numParticles, const FlowParams& params, float dt);
extern "C" void computeVertexPressureCUDA(const std::vector<float>& vertices,
                                          const std::vector<float>& normals,
                                          std::vector<float>& outColors,
                                          int numVertices, const FlowParams& params);
extern "C" void setVoxelData(const int* voxel, const float* dist,
                             int nx, int ny, int nz,
                             float mnX, float mnY, float mnZ,
                             float csX, float csY, float csZ);

// =====================================================
// Диагностика CUDA-бэкенда (v1.25.0, режим отладки)
//
// Раньше любая ошибка CUDA печаталась в std::cerr, которого в сборке
// /SUBSYSTEM:WINDOWS не существует, и функция просто возвращалась: физика
// молча оставалась без результатов, а пользователь видел лишь "CUDA не работает".
// Теперь причина фиксируется и попадает в отладочный отчёт.
// =====================================================

struct CudaBackendInfo {
    int       compiledWithCuda;  // 1 = бинарь собран nvcc, 0 = CPU-заглушка (CPU_ONLY)
    int       deviceCount;       // сколько GPU увидела среда выполнения CUDA
    int       usable;            // 1 = тестовое ядро реально выполнилось на GPU
    int       ccMajor;           // compute capability (7.5 = Turing, 8.6 = Ampere)
    int       ccMinor;
    int       driverVersion;     // версия драйверного API CUDA, например 13020 = 13.2
    int       runtimeVersion;    // версия среды выполнения CUDA, которой собран бинарь
    long long totalMemoryMB;     // память GPU в МБ
    int       lastError;         // код последней ошибки (0 = успех)
    char      deviceName[128];   // имя GPU
    char      status[768];       // человекочитаемый вывод по-русски (для UI и отчёта)
};

// Полная проверка бэкенда: перечисляет GPU, читает свойства и ЗАПУСКАЕТ тестовое
// ядро (единственный честный способ поймать ошибку 209 "нет кода для этой
// архитектуры" до того, как она испортит физику).
extern "C" void cudaProbeBackend(CudaBackendInfo* out);

// v1.26.0 — освободить все буферы устройства. Вызывается при завершении работы:
// раньше поле SDF (g_dDist) не освобождалось никогда, а буферы частиц и вершин
// выделялись и освобождались каждый кадр.
extern "C" void cudaReleaseAll();

// Забрать описание последней ошибки CUDA (и сбросить его).
// Возвращает код ошибки, 0 = ошибок не было. Используется частицами/силами,
// чтобы при сбое GPU мгновенно переключиться на CPU и записать причину в журнал.
extern "C" int cudaConsumeError(char* buf, int bufSize);

#endif // CUDA_API_H
