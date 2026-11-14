// =====================================================
// Замер производительности шага LBM (не проверка, а измерительный стенд).
//
//   make bench            # по умолчанию 64^3, 300 шагов, все потоки
//   OMP_NUM_THREADS=1 make bench
//
// Печатает млн обновлений ячеек в секунду (Mcell/s) и мс на шаг. Порогов не
// ставит: скорость зависит от машины, а гонять флейки в CI бессмысленно.
// Числа отсюда попадают в docs/OPTIMIZATION-*.md и CHANGELOG.
// =====================================================
#include "globals.h"
#include "lbm.h"
#include "thermal_lbm.h"
#include "atmosphere.h"
#include "voxel_grid.h"
#include "debug_log.h"
#include "version.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>
#ifdef _OPENMP
#include <omp.h>
#endif

extern "C" double glfwGetTime(void) { static double t = 0.0; return (t += 1.0/60.0); }
extern "C" void setVoxelData(const int*, const float*, int, int, int, float, float, float, float, float, float) {}
extern "C" int  cudaConsumeError(char* buf, int sz) { if (buf && sz > 0) buf[0] = 0; return 0; }
void updateVertexColors() {}
void computeLiftDrag() {}
void drawModel() {}

int main(int argc, char** argv) {
    const int N = (argc > 1) ? std::atoi(argv[1]) : 64;
    const int steps = (argc > 2) ? std::atoi(argv[2]) : 300;
    const bool thermal = (argc > 3) ? std::atoi(argv[3]) != 0 : false;
    const bool les = (argc > 4) ? std::atoi(argv[4]) != 0 : true;

    debugLogInit("/tmp/tl/bench");

    // Сфера радиуса N/5 в кубе N^3 — та же постановка, что в валидации по
    // форме, но без входов/выходов: мерим сам шаг, а не граничные условия.
    g_voxNx = g_voxNy = g_voxNz = N;
    g_voxMinX = g_voxMinY = g_voxMinZ = -1.0f;
    g_voxMaxX = g_voxMaxY = g_voxMaxZ =  1.0f;
    g_voxelData.assign((size_t)N*N*N, 0);
    g_distanceField.assign((size_t)N*N*N, 0.0f);
    const float R = 0.35f;                      // доля полукуба
    const float cs = 2.0f / (float)N;
    for (int z = 0; z < N; ++z) for (int y = 0; y < N; ++y) for (int x = 0; x < N; ++x) {
        const float px = -1.0f + (x + 0.5f)*cs;
        const float py = -1.0f + (y + 0.5f)*cs;
        const float pz = -1.0f + (z + 0.5f)*cs;
        const float d = std::sqrt(px*px + py*py + pz*pz) - R;
        const size_t i = ((size_t)z*N + y)*N + x;
        g_voxelData[i] = (d <= 0.0f) ? 1 : 0;
        g_distanceField[i] = d / cs;
    }
    minBB = glm::vec3(-R); maxBB = glm::vec3(R);
    center = glm::vec3(0.0f); maxDim = 2.0f*R;
    voxelResolution = N;

    flowSpeed = 20.0f; flowAzimuth = 0.0f; flowElevation = 0.0f;
    altitude = 0.0f; timeScale = 1.0f; aeroMachEffects = false;
    lbmParams.enabled = true; lbmParams.useGPU = false;
    lbmParams.U0 = 0.08f; lbmParams.stepsPerFrame = 1;
    lbmParams.tau = 0.6f; lbmParams.useTurbulence = les;
    lbmParams.useGround = false; lbmParams.inletTurbulence = 0.0f;
    lbmParams.domainPadding = 0.25f; lbmParams.autoViscosity = false;
    lbmParams.useConvectiveOutlet = true; lbmParams.periodicX = false;
    lbmParams.bodyForceX = lbmParams.bodyForceY = lbmParams.bodyForceZ = 0.0f;
    thermalParams.enabled = thermal;
    thermalParams.rayleigh = 2500.0f;
    thermalParams.prandtl = 1.0f;

    shutdownLBM();
    initLBM();
    if (!lbmInitialized) { std::printf("init failed\n"); return 1; }
    const int cells = lbmNx*lbmNy*lbmNz;

    for (int i = 0; i < 30; ++i) stepLBMCPU(1);      // прогрев кэша и ветвей

    const auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < steps; ++i) stepLBMCPU(1);
    const double sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();

    const double msStep = 1000.0 * sec / steps;
    const double mcells = (double)cells * steps / sec / 1.0e6;
#ifdef _OPENMP
    const int threads = omp_get_max_threads();
#else
    const int threads = 1;
#endif
    std::printf("Aeros %s | сетка %dx%dx%d = %.2f млн ячеек | потоков %d | тепло %s\n",
                AEROS_VERSION_STRING, lbmNx, lbmNy, lbmNz, cells/1e6, threads, thermal ? "да" : "нет");
    std::printf("  %.3f мс/шаг, %.2f млн обновлений ячеек/с\n", msStep, mcells);

    // Контрольная сумма поля: решает, воспроизводим ли результат. Если она
    // меняется от запуска к запуску при одном и том же числе потоков, в
    // решателе есть гонка (race), и «проверенные» числа ничего не стоят.
    double sRho = 0.0, sU = 0.0;
    for (size_t i = 0; i < lbmRho.size(); ++i) {
        sRho += lbmRho[i];
        sU += lbmUx[i] + lbmUy[i] + lbmUz[i];
    }
    std::printf("  checksum: rho %.12g  u %.12g\n", sRho, sU);

    shutdownLBM();
    return 0;
}
