// =====================================================
// Aeros Engine v1.39.0 — Headless-тесты флагманской физики C++17 и
// двухэтапного конвейера (Stage 1: Pre-Sim -> Stage 2: Inspect & Report).
// =====================================================

#include "globals.h"
#include "lbm.h"
#include "thermal_lbm.h"
#include "voxel_grid.h"
#include "phys_sim.h"
#include "flow_field.h"
#include "atmosphere.h"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <chrono>
#include <vector>

extern "C" double glfwGetTime(void) { static double t = 0.0; return (t += 1.0 / 60.0); }
extern "C" void setVoxelData(const int*, const float*, int, int, int, float, float, float, float, float, float) {}
extern "C" int  cudaConsumeError(char* buf, int sz) { if (buf && sz > 0) buf[0] = 0; return 0; }
void updateVertexColors() {}
void computeLiftDrag() {}
void drawModel() {}

static int g_checks = 0, g_fail = 0;

static void check(bool ok, const char* msg) {
    ++g_checks;
    if (!ok) {
        ++g_fail;
        std::printf("  FAIL: %s\n", msg);
    } else {
        std::printf("  ok:   %s\n", msg);
    }
}

static void buildSphereMesh(float radius, int rings, int slices) {
    g_vertices.clear();
    g_normals.clear();
    auto pushTri = [&](glm::vec3 a, glm::vec3 b, glm::vec3 c) {
        glm::vec3 n = glm::normalize(glm::cross(b - a, c - a));
        g_vertices.insert(g_vertices.end(), {a.x,a.y,a.z, b.x,b.y,b.z, c.x,c.y,c.z});
        g_normals.insert(g_normals.end(),   {n.x,n.y,n.z, n.x,n.y,n.z, n.x,n.y,n.z});
    };
    for (int r = 0; r < rings; ++r) {
        float th0 = (float)r / rings * 3.14159265f;
        float th1 = (float)(r + 1) / rings * 3.14159265f;
        for (int s = 0; s < slices; ++s) {
            float ph0 = (float)s / slices * 6.2831853f;
            float ph1 = (float)(s + 1) / slices * 6.2831853f;
            auto pt = [&](float th, float ph) {
                return glm::vec3(radius * std::sin(th) * std::cos(ph),
                                 radius * std::cos(th),
                                 radius * std::sin(th) * std::sin(ph));
            };
                    glm::vec3 p00 = pt(th0, ph0), p10 = pt(th1, ph0);
                    glm::vec3 p11 = pt(th1, ph1), p01 = pt(th0, ph1);
                    if (r > 0) pushTri(p00, p11, p10);
                    if (r + 1 < rings) pushTri(p00, p01, p11);
        }
    }
    center = glm::vec3(0.0f);
    minBB = glm::vec3(-radius);
    maxBB = glm::vec3(radius);
    maxDim = radius * 2.0f;
    modelVertexCount = (int)g_vertices.size() / 3;
    aeroRefArea = 3.14159265f * radius * radius;
}

static bool fileNonEmpty(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    f.seekg(0, std::ios::end);
    return f.tellg() > 0;
}

int main() {
    std::printf("=== Flagship Physics & Two-Stage Pre-Simulation Tests (v1.39.0) ===\n");

    // 1) Проверка ортогональности матрицы MRT d'Humieres D3Q19
    check(lbmVerifyMRTOrthogonality(), "MRT D3Q19 M * diag(1/D) * M^T == I (orthogonality)");

    // Подготовка геометрии сферы R=0.5 (вокруг неё строим воксели и SDF)
    buildSphereMesh(0.5f, 14, 24);
    voxelResolution = 28;
    buildVoxelGrid(g_vertices, voxelResolution);
    check(g_voxNx > 0 && !g_distanceField.empty(), "Voxel grid & continuous SDF built for sphere");

    flowSpeed = 15.0f;
    flowAzimuth = 0.0f;
    flowElevation = 0.0f;
    airDensity = 1.225f;
    airTemperature = 288.15f;

    // 2) Проверка устойчивости сферы при низких tau (Re_LB=100 и Re_LB=500, где BGK расходился)
    lbmApplyFlagshipPreset();
    lbmParams.domainPadding = 0.28f;
    lbmParams.autoViscosity = false;
    lbmParams.U0 = 0.08f;
    lbmParams.tau = 0.545f; // Re_LB ~ 100
    initLBM();
    check(lbmInitialized && lbmNx > 16, "Flagship MRT+WALE+BFL initialized at tau=0.545");
    stepLBMCPUEx(150, true);
    check(std::isfinite(lbmAvgRho) && lbmAvgRho > 0.90f && lbmAvgRho < 1.10f,
          "Sphere MRT+WALE+BFL stable at tau=0.545 (Re_LB~100)");
    check(lbmBadPops == 0 && lbmRhoOutOfRange == 0, "Zero bad populations at tau=0.545");
    check(lbmMaxNuTRatio() >= 0.0f && std::isfinite(lbmEnstrophy()), "WALE nu_t ratio & enstrophy finite");

    // Сверхнизкая вязкость: tau = 0.509 (Re_LB ~ 500)
    lbmParams.tau = 0.509f;
    resetLBM();
    stepLBMCPUEx(150, true);
    check(std::isfinite(lbmAvgRho) && lbmAvgRho > 0.90f && lbmAvgRho < 1.10f,
          "Sphere MRT+WALE+BFL stable at extreme low viscosity tau=0.509");

    // 3) Проверка Regularized (2nd-order Hermite) оператора
    lbmParams.collisionMode = (int)LBMCollisionMode::Regularized;
    lbmParams.tau = 0.56f;
    resetLBM();
    stepLBMCPUEx(100, true);
    check(lbmEffectiveCollisionMode() == LBMCollisionMode::Regularized, "Effective mode == Regularized");
    check(std::isfinite(lbmAvgRho) && std::fabs(lbmAvgRho - 1.0f) < 0.05f,
          "Regularized Hermite collision stable");

    // 4) Двухэтапный конвейер: Этап 1 (Предсимуляция) -> Этап 2 (Инспекция и отчёт)
    lbmParams.tau = 0.56f;
    PhysSimConfig cfg;
    cfg.targetSteps = 450;
    cfg.metricsEvery = 5;
    cfg.snapshotEvery = 45;
    cfg.snapshotRes = 20;
    cfg.resetBeforeRun = true;
    cfg.flagshipMode = true;
    cfg.computeVorticity = true;
    cfg.autoAnalyze = true;
    cfg.autoExport = true;
    cfg.outputDir = "/tmp/aeros-presim-test";
    cfg.runTag = "sphere_test";

    bool runOk = physSimRunBlocking(cfg);
    check(runOk, "Stage 1: physSimRunBlocking completed");
    check(physSimStage() == PhysSimStage::InspectReady, "Stage transitioned to InspectReady");
    check(physSimSampleCount() >= 35, "Collected scalar timeline samples");
    check(physSimSnapshotCount() >= 8, "Collected 3D field snapshots for scrubbing");

    // Этап 2: Скруббинг 3D-снимков во времени
    check(physSimApplySnapshotToWorld(0), "Stage 2: applied snapshot 0 (initial) to 3D world fields");
    float pMax0 = lbmPressureMax;
    int midIdx = physSimSnapshotCount() / 2;
    check(physSimApplySnapshotToWorld(midIdx), "Stage 2: applied mid snapshot to 3D world fields");
    int lastIdx = physSimSnapshotCount() - 1;
    check(physSimApplySnapshotToWorld(lastIdx), "Stage 2: applied final snapshot to 3D world fields");
    check(std::isfinite(pMax0) && std::isfinite(lbmPressureMax), "Scrubbed 3D pressure fields finite");

    // Проверка отчёта о поведении
    const PhysBehaviorReport& rep = physSimReport();
    check(rep.valid, "Behavior report generated");
    check(rep.stepsSimulated == 450, "Report recorded exact 450 simulated steps");
    check(rep.massDriftRel < 0.02f && !rep.hadDivergence, "Conservation audit passed (mass drift < 2%)");
    check(std::isfinite(rep.cdMean) && rep.cdMean > 0.05f && rep.cdMean < 5.0f,
          "Mean drag coefficient Cd in physical range");
    check(!rep.events.empty() && rep.events.size() >= 3, "Chronological milestone events populated");
    check(rep.markdownNarrative.find("Physical Behavior") != std::string::npos,
          "Markdown behavior account generated");

    // Проверка выгрузки .md / .csv / .json
    check(fileNonEmpty(physSimLastMdPath()), "Exported .md behavior report exists and non-empty");
    check(fileNonEmpty(physSimLastCsvPath()), "Exported .csv time-series exists and non-empty");
    check(fileNonEmpty(physSimLastJsonPath()), "Exported .json summary exists and non-empty");

    // 5) Асинхронный запуск (worker thread)
    cfg.targetSteps = 60;
    cfg.autoExport = false;
    check(physSimStartAsync(cfg), "Async Stage 1 started in background thread");
    while (physSimIsRunning()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    check(physSimStage() == PhysSimStage::InspectReady && physSimCurrentStepDone() == 60,
          "Async Stage 1 finished cleanly with 60 steps");

    physSimShutdown();
    shutdownLBM();

    std::printf("=== Flagship & Pre-Sim Tests: checks %d, errors %d ===\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
