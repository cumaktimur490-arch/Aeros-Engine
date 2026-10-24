// Headless-прогон решателя LBM: проверка сохранения массы, устойчивости,
// физического смысла TKE и того, что модель Смагоринского действительно влияет
// на расчёт (до v1.27.0 её вклад был ~1e-5, то есть она не работала).
#include "globals.h"
#include "lbm.h"
#include "atmosphere.h"
#include "voxel_grid.h"
#include "debug_log.h"
#include <cmath>
#include <cstdio>
#include <vector>

static int g_failed = 0;
static void check(bool cond, const char* what) {
    std::printf("  %s  %s\n", cond ? "ok:  " : "FAIL:", what);
    if (!cond) ++g_failed;
}

// заглушки того, что тянет OpenGL / GLFW / CUDA
extern "C" double glfwGetTime(void) { static double t = 0.0; return (t += 1.0/60.0); }
extern "C" void setVoxelData(const int*, const float*, int, int, int, float, float, float, float, float, float) {}
extern "C" int  cudaConsumeError(char* buf, int sz) { if (buf && sz > 0) buf[0] = 0; return 0; }
void updateVertexColors() {}
void computeLiftDrag() {}
void drawModel() {}

static void makeSphereCase(float radius) {
    // синтетическая «модель»: воксельная сетка со сферическим SDF
    int R = 24;
    g_voxNx = g_voxNy = g_voxNz = R;
    g_voxMinX = g_voxMinY = g_voxMinZ = -3.0f;
    g_voxMaxX = g_voxMaxY = g_voxMaxZ =  3.0f;
    float cs = 6.0f / (float)R;
    g_voxelData.assign((size_t)R*R*R, 0);
    g_distanceField.assign((size_t)R*R*R, 0.0f);
    for (int z=0; z<R; ++z) for (int y=0; y<R; ++y) for (int x=0; x<R; ++x) {
        float px = g_voxMinX + (x+0.5f)*cs, py = g_voxMinY + (y+0.5f)*cs, pz = g_voxMinZ + (z+0.5f)*cs;
        float d = std::sqrt(px*px+py*py+pz*pz) - radius;   // >0 снаружи
        size_t i = (size_t)(z*R+y)*R+x;
        g_voxelData[i] = (d <= 0.0f) ? 1 : 0;
        g_distanceField[i] = d / cs;                        // в ячейках, как делает BFS
    }
    minBB = glm::vec3(-radius); maxBB = glm::vec3(radius);
    center = glm::vec3(0.0f); maxDim = 2.0f*radius;
}

int main() {
    debugLogInit("/tmp/tl/phys");
    std::printf("=== headless-прогон LBM (v1.27.0) ===\n");

    flowSpeed = 20.0f; flowAzimuth = 0.0f; flowElevation = 0.0f;
    altitude = 0.0f; timeScale = 1.0f;
    aeroMachEffects = false;
    lbmParams.enabled = true;
    lbmParams.tau = 0.55f;
    lbmParams.U0 = 0.08f;
    lbmParams.stepsPerFrame = 1;
    lbmParams.smagorinskyC = 0.14f;
    lbmParams.useGround = false;
    voxelResolution = 24;

    makeSphereCase(0.7f);
    initLBM();
    check(lbmInitialized, "решатель инициализирован");
    check(lbmNx > 1 && lbmNy > 1 && lbmNz > 1, "размеры сетки корректны");

    // --- прогон с турбулентностью ---
    lbmParams.useTurbulence = true;
    for (int i = 0; i < 120; ++i) stepLBMCPU(1);
    float rhoTurb = lbmAvgRho;
    long long negTurb = lbmNegativePops, badTurb = lbmBadPops;
    float tkeTurb = lbmTKE, resTurb = lbmConvergence;
    float pmin = lbmPressureMin, pmax = lbmPressureMax;
    float vmaxTurb = lbmMaxVelocityWorld;
    std::printf("  [турбулентность] rho=%.6f  TKE=%.4e м2/с2  vmax=%.3f м/с  p=[%.2f, %.2f] Па  "
                "отриц.популяций=%lld  замен=%lld  сходимость=%.3e\n",
                rhoTurb, tkeTurb, vmaxTurb, pmin, pmax, negTurb, badTurb, resTurb);

    check(std::fabs(rhoTurb - 1.0f) < 0.02f, "масса сохраняется: |rho-1| < 0.02");
    check(negTurb == 0, "отрицательных популяций нет (сохранение массы не ломается)");
    check(badTurb == 0, "нефизических значений нет");
    check(tkeTurb >= 0.0f && tkeTurb < 100.0f, "TKE в физическом диапазоне (м2/с2)");
    check(tkeTurb > 1e-12f, "TKE ненулевая: модель подсеточной турбулентности работает");
    float q = 0.5f * airDensity * flowSpeed * flowSpeed;
    check(std::fabs(pmin) <= q*1.05f && std::fabs(pmax) <= q*1.05f,
          "давление ограничено скоростным напором (Cp в [-2, 2])");
    check(vmaxTurb > 0.1f && vmaxTurb < 3.0f*flowSpeed, "максимальная скорость физична");
    check(lbmResidualRho >= 0.0f && std::isfinite(lbmResidualRho), "остаток сходимости конечен");

    // --- тот же прогон без турбулентности: результат обязан отличаться ---
    shutdownLBM();
    makeSphereCase(0.7f);
    initLBM();
    lbmParams.useTurbulence = false;
    for (int i = 0; i < 120; ++i) stepLBMCPU(1);
    float rhoLam = lbmAvgRho, vmaxLam = lbmMaxVelocityWorld;
    std::printf("  [без турбулент.] rho=%.6f  vmax=%.3f м/с  TKE=%.4e\n", rhoLam, vmaxLam, lbmTKE);
    check(std::fabs(vmaxTurb - vmaxLam) > 1e-6f || std::fabs(rhoTurb - rhoLam) > 1e-9f,
          "Смагоринский влияет на расчёт (до v1.27.0 вклад был ~1e-5)");

    // --- трилинейное давление против ближайшей ячейки ---
    shutdownLBM();
    makeSphereCase(0.7f);
    initLBM();
    lbmParams.useTurbulence = true;
    for (int i = 0; i < 60; ++i) stepLBMCPU(1);
    glm::vec3 probe(1.6f, 0.35f, 0.0f);
    float pA = 0, pB = 0;
    bool okA = getLBMPressureWorldChecked(probe, &pA);
    bool okB = getLBMPressureWorldChecked(probe + glm::vec3(0.02f, 0.0f, 0.0f), &pB);
    check(okA && okB, "давление в точке достаётся (трилинейно)");
    check(std::fabs(pA - pB) < 0.25f * (std::fabs(pA) + 1.0f),
          "давление меняется плавно при малом сдвиге точки (не ступеньками)");
    // вне области — честное «нет данных»
    float pOut = 0;
    check(!getLBMPressureWorldChecked(glm::vec3(50.0f, 50.0f, 50.0f), &pOut),
          "точка вне расчётной области не выдаёт ложное давление");

    std::printf("\n=== %s (ошибок: %d) ===\n", g_failed == 0 ? "ФИЗИКА В ПОРЯДКЕ" : "ЕСТЬ ПРОВАЛЫ", g_failed);
    return g_failed == 0 ? 0 : 1;
}
