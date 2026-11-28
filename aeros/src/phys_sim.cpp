// =====================================================
// Aeros Engine v1.39.0 — Двухэтапный конвейер физики (C++17)
// Этап 1: предсимуляция LBM (+ Thermal D3Q7) с записью временного ряда и
//         3D-снимков поля.
// Этап 2: постинспекция 3D-снимков (скруббинг во времени), спектральный
//         анализ DFT (частота срыва вихрей и число Струхаля), аудит законов
//         сохранения, классификация режима течения, хронология событий и
//         экспорт полного отчёта (.md / .csv / .json).
// =====================================================

#include "phys_sim.h"
#include "lbm.h"
#include "thermal_lbm.h"
#include "flow_field.h"
#include "atmosphere.h"
#include "globals.h"
#include "debug_log.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <thread>

#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#include <sys/types.h>
#endif

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace {

static PhysSimConfig s_cfg;
static std::vector<PhysSimSample> s_samples;
static std::vector<PhysSimSnapshot> s_snapshots;
static PhysBehaviorReport s_report;

static std::atomic<bool> s_running{false};
static std::atomic<bool> s_cancel{false};
static std::atomic<int>  s_stage{(int)PhysSimStage::Idle};
static std::atomic<int>  s_stepsDone{0};
static std::atomic<int>  s_stepsTarget{0};
static std::atomic<double> s_elapsedSec{0.0};
static std::atomic<double> s_mlups{0.0};
static std::atomic<int>  s_activeSnap{-1};

static std::mutex s_dataMutex;
static std::mutex s_statusMutex;
static std::string s_statusText = "Idle";
static std::thread s_worker;

static std::string s_lastMdPath;
static std::string s_lastCsvPath;
static std::string s_lastJsonPath;

static void setStatus(const std::string& msg) {
    std::lock_guard<std::mutex> lk(s_statusMutex);
    s_statusText = msg;
}

static void ensureDirExists(const std::string& dir) {
    if (dir.empty()) return;
    std::string cur;
    for (size_t i = 0; i < dir.size(); ++i) {
        char ch = dir[i];
        cur.push_back(ch);
        if (ch == '/' || ch == '\\' || i + 1 == dir.size()) {
            if (cur == "/" || cur == "\\") continue;
#ifdef _WIN32
            _mkdir(cur.c_str());
#else
            mkdir(cur.c_str(), 0755);
#endif
        }
    }
}

static std::string makeTimestampTag() {
    auto now = std::chrono::system_clock::now();
    std::time_t tt = std::chrono::system_clock::to_time_t(now);
    std::tm tmBuf{};
#ifdef _WIN32
    localtime_s(&tmBuf, &tt);
#else
    localtime_r(&tt, &tmBuf);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y%m%d_%H%M%S", &tmBuf);
    return std::string(buf);
}

static std::string jsonEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (unsigned char ch : s) {
        switch (ch) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (ch < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", (unsigned)ch);
                    out += buf;
                } else {
                    out.push_back((char)ch);
                }
        }
    }
    return out;
}

// Единичный вектор направления потока (аналогично forces.cpp)
static glm::vec3 computeFlowDirWorld() {
    float az = glm::radians(flowAzimuth);
    float el = glm::radians(flowElevation);
    glm::vec3 d(cosf(el) * cosf(az), sinf(el), cosf(el) * sinf(az));
    float len = glm::length(d);
    if (!std::isfinite(len) || len < 1e-6f) return glm::vec3(1.0f, 0.0f, 0.0f);
    return d / len;
}

// Чистый C++-расчёт аэродинамических сил (без OpenGL), согласованный с forces.cpp
static void computeForcesPureCPU(glm::vec3& outDragVec, glm::vec3& outLiftVec,
                                 glm::vec3& outMomentVec, glm::vec3& outCoP,
                                 float& outCd, float& outCl, float& outLD,
                                 float& outCpMin, float& outCpMax) {
    outDragVec = glm::vec3(0.0f);
    outLiftVec = glm::vec3(0.0f);
    outMomentVec = glm::vec3(0.0f);
    outCoP = center;
    outCd = 0.0f; outCl = 0.0f; outLD = 0.0f;
    outCpMin = 0.0f; outCpMax = 0.0f;

    if (!lbmInitialized) return;
    const float vinf = std::max(0.01f, flowSpeed);
    const float rho = std::max(0.01f, airDensity);
    const float mu = std::max(1e-7f, airSutherlandViscosity(airTemperature));
    const float q = 0.5f * rho * vinf * vinf;
    if (q < 1e-9f) return;

    const glm::vec3 flowDir = computeFlowDirWorld();
    const float csAvg = (lbmCellSizeX + lbmCellSizeY + lbmCellSizeZ) * (1.0f / 3.0f);
    const float wallOff = 0.6f * csAvg;

    if (!g_vertices.empty() && g_vertices.size() == g_normals.size()) {
        const int triCount = (int)g_vertices.size() / 9;
        const float* vertPtr = g_vertices.data();
        const float* normPtr = g_normals.data();

        float minAlong = 1e9f, maxAlong = -1e9f;
        for (int ti = 0; ti < triCount; ++ti) {
            int i = ti * 9;
            glm::vec3 v0(vertPtr[i], vertPtr[i + 1], vertPtr[i + 2]);
            float along = glm::dot(v0, flowDir);
            if (std::isfinite(along)) {
                if (along < minAlong) minAlong = along;
                if (along > maxAlong) maxAlong = along;
            }
        }
        if (!std::isfinite(minAlong) || minAlong > maxAlong) { minAlong = 0.0f; maxAlong = maxDim; }
        float chord = std::max(1e-4f, maxAlong - minAlong);
        glm::vec3 ac = flowDir * (minAlong + chord * 0.25f) + (center - flowDir * glm::dot(center, flowDir));

        const float machInf = vinf / (std::max(1.0f, speedOfSound));
        const bool doComp = (aeroMachEffects && machInf > 0.3f && machInf < 0.85f);
        const float betaPG = doComp ? std::max(0.2f, std::sqrt(1.0f - machInf * machInf)) : 1.0f;

        glm::vec3 totalForce(0.0f), cpSum(0.0f), momentSum(0.0f);
        float pressSum = 0.0f, frontalSum = 0.0f, areaSum = 0.0f;
        float cpMin = 1e9f, cpMax = -1e9f;

        #ifdef _OPENMP
        #pragma omp parallel
        #endif
        {
            glm::vec3 lForce(0.0f), lCpSum(0.0f), lMom(0.0f);
            float lPress = 0.0f, lFront = 0.0f, lArea = 0.0f;
            float lCpMin = 1e9f, lCpMax = -1e9f;

            #ifdef _OPENMP
            #pragma omp for nowait
            #endif
            for (int ti = 0; ti < triCount; ++ti) {
                int i = ti * 9;
                glm::vec3 v0(vertPtr[i],     vertPtr[i + 1], vertPtr[i + 2]);
                glm::vec3 v1(vertPtr[i + 3], vertPtr[i + 4], vertPtr[i + 5]);
                glm::vec3 v2(vertPtr[i + 6], vertPtr[i + 7], vertPtr[i + 8]);
                glm::vec3 n(normPtr[i],      normPtr[i + 1], normPtr[i + 2]);
                if (!std::isfinite(v0.x) || !std::isfinite(n.x)) continue;
                float nLen = glm::length(n);
                if (nLen < 1e-6f || !std::isfinite(nLen)) continue;
                n /= nLen;
                glm::vec3 triC = (v0 + v1 + v2) * (1.0f / 3.0f);
                float area = 0.5f * glm::length(glm::cross(v1 - v0, v2 - v0));
                if (!std::isfinite(area) || area < 1e-9f || area > 1e6f) continue;

                float pLBM = 0.0f;
                if (!getLBMPressureWorldChecked(triC + n * wallOff, &pLBM)) pLBM = 0.0f;
                float cp = pLBM / q;
                if (doComp) {
                    float denom = betaPG + (machInf * machInf / (1.0f + betaPG)) * cp * 0.5f;
                    cp = (std::fabs(denom) > 1e-6f) ? (cp / denom) : (cp / betaPG);
                }
                if (!std::isfinite(cp)) cp = 0.0f;
                if (cp < lCpMin) lCpMin = cp;
                if (cp > lCpMax) lCpMax = cp;

                glm::vec3 pForce = -cp * q * n * area;
                float flowDotN = glm::dot(flowDir, n);
                lFront += area * std::fabs(flowDotN);

                float xAlong = glm::clamp(glm::dot(triC, flowDir) - minAlong, 0.001f, chord * 1.5f);
                float Re_x = glm::clamp(rho * vinf * xAlong / mu, 1.0f, 1e9f);
                float Cf;
                if (Re_x < 5e5f) {
                    Cf = 0.664f / std::sqrt(Re_x);
                } else {
                    float lRe = std::max(1.0f, std::log10(Re_x));
                    Cf = 0.455f / std::pow(lRe, 2.58f);
                    if (Re_x < 1e9f) Cf = std::max(0.0005f, Cf - 1700.0f / Re_x);
                }
                Cf = glm::clamp(Cf * 1.12f, 0.0002f, 0.015f);
                float fricFac = 1.0f - std::fabs(flowDotN);
                if (flowDotN > 0.0f) fricFac *= 0.55f;
                glm::vec3 vForce = -flowDir * (Cf * q * area * fricFac);

                glm::vec3 fTot = pForce + vForce;
                lForce += fTot;
                float pw = std::fabs(cp) * area * (cp < 0.0f ? 0.8f : 1.0f);
                lCpSum += triC * pw;
                lPress += pw;
                lMom += glm::cross(triC - ac, fTot);
                lArea += area;
            }

            #ifdef _OPENMP
            #pragma omp critical
            #endif
            {
                totalForce += lForce;
                cpSum += lCpSum;
                momentSum += lMom;
                pressSum += lPress;
                frontalSum += lFront;
                areaSum += lArea;
                if (lCpMin < cpMin) cpMin = lCpMin;
                if (lCpMax > cpMax) cpMax = lCpMax;
            }
        }

        if (cpMin <= cpMax) { outCpMin = cpMin; outCpMax = cpMax; }
        float refA = (aeroRefArea > 1e-6f) ? aeroRefArea : std::max(1e-4f, frontalSum * 0.5f);
        float dragProj = glm::dot(totalForce, flowDir);
        outDragVec = flowDir * dragProj;
        outLiftVec = totalForce - outDragVec;
        outMomentVec = momentSum;
        outCoP = (pressSum > 1e-9f) ? (cpSum / pressSum) : center;
        outCd = dragProj / (q * refA);
        // Для знака Cl используем проекцию на ось Y (перпендикулярную потоку)
        glm::vec3 upRef(0.0f, 1.0f, 0.0f);
        if (std::fabs(glm::dot(upRef, flowDir)) > 0.95f) upRef = glm::vec3(0.0f, 0.0f, 1.0f);
        glm::vec3 liftAxis = glm::normalize(upRef - flowDir * glm::dot(upRef, flowDir));
        float liftProj = glm::dot(outLiftVec, liftAxis);
        outCl = liftProj / (q * refA);
        outLD = (std::fabs(outCd) > 1e-6f) ? (outCl / outCd) : 0.0f;
    } else if (!lbmIsSolid.empty() && lbmNx > 2 && lbmNy > 2 && lbmNz > 2) {
        // Запасной путь: интегрирование давления по границам воксельного твёрдого тела
        // (для чисто воксельных сцен и безмодельных тестов)
        const int Nx = lbmNx, Ny = lbmNy, Nz = lbmNz;
        const float ax = lbmCellSizeY * lbmCellSizeZ;
        const float ay = lbmCellSizeX * lbmCellSizeZ;
        const float az = lbmCellSizeX * lbmCellSizeY;
        glm::vec3 fTotal(0.0f);
        float cpMin = 1e9f, cpMax = -1e9f;
        int solidBoundaryFaces = 0;

        for (int z = 1; z < Nz - 1; ++z) {
            for (int y = 1; y < Ny - 1; ++y) {
                for (int x = 1; x < Nx - 1; ++x) {
                    int idx = (z * Ny + y) * Nx + x;
                    if (!lbmIsSolid[idx]) continue;
                    const int nb[6] = { idx - 1, idx + 1, idx - Nx, idx + Nx, idx - Nx * Ny, idx + Nx * Ny };
                    const float nrm[6][3] = { {-1,0,0}, {1,0,0}, {0,-1,0}, {0,1,0}, {0,0,-1}, {0,0,1} };
                    const float fa[6] = { ax, ax, ay, ay, az, az };
                    for (int k = 0; k < 6; ++k) {
                        int j = nb[k];
                        if (lbmIsSolid[j] || lbmIsGround[j]) continue;
                        float p = lbmPressure[j];
                        float cp = p / q;
                        if (cp < cpMin) cpMin = cp;
                        if (cp > cpMax) cpMax = cp;
                        glm::vec3 n(nrm[k][0], nrm[k][1], nrm[k][2]);
                        fTotal += -p * n * fa[k];
                        solidBoundaryFaces++;
                    }
                }
            }
        }
        if (solidBoundaryFaces > 0 && cpMin <= cpMax) {
            outCpMin = cpMin; outCpMax = cpMax;
            float projA = lbmProjectedAreaCells(0) * ax;
            float refA = (aeroRefArea > 1e-6f) ? aeroRefArea : std::max(1e-4f, projA);
            float dragProj = glm::dot(fTotal, flowDir);
            outDragVec = flowDir * dragProj;
            outLiftVec = fTotal - outDragVec;
            glm::vec3 upRef(0.0f, 1.0f, 0.0f);
            if (std::fabs(glm::dot(upRef, flowDir)) > 0.95f) upRef = glm::vec3(0.0f, 0.0f, 1.0f);
            glm::vec3 liftAxis = glm::normalize(upRef - flowDir * glm::dot(upRef, flowDir));
            outCd = dragProj / (q * refA);
            outCl = glm::dot(outLiftVec, liftAxis) / (q * refA);
            outLD = (std::fabs(outCd) > 1e-6f) ? (outCl / outCd) : 0.0f;
        }
    }
}

static void sampleWakeDeficits(float& d1L, float& d2L, float& d4L) {
    d1L = 0.0f; d2L = 0.0f; d4L = 0.0f;
    if (!lbmInitialized || flowSpeed < 1e-4f) return;
    float L = std::max(0.05f, maxDim);
    glm::vec3 dir = computeFlowDirWorld();
    auto deficitAt = [&](float mult) -> float {
        glm::vec3 pt = center + dir * (mult * L);
        if (pt.x < lbmMinX || pt.x > lbmMaxX ||
            pt.y < lbmMinY || pt.y > lbmMaxY ||
            pt.z < lbmMinZ || pt.z > lbmMaxZ) {
            return 0.0f;
        }
        glm::vec3 v = getLBMVelocityWorld(pt);
        float uAlong = glm::dot(v, dir);
        float def = 1.0f - uAlong / flowSpeed;
        if (!std::isfinite(def)) return 0.0f;
        return glm::clamp(def, -1.0f, 2.0f);
    };
    d1L = deficitAt(1.0f);
    d2L = deficitAt(2.0f);
    d4L = deficitAt(4.0f);
}

static PhysSimSnapshot captureSnapshot(int resTarget, const PhysSimSample& smp) {
    PhysSimSnapshot sn;
    sn.step = smp.step;
    sn.timePhys = smp.timePhys;
    sn.minX = lbmMinX; sn.minY = lbmMinY; sn.minZ = lbmMinZ;
    sn.maxX = lbmMaxX; sn.maxY = lbmMaxY; sn.maxZ = lbmMaxZ;
    sn.dragN = smp.dragN; sn.liftN = smp.liftN; sn.momentNm = smp.momentNm;
    sn.cd = smp.cd; sn.cl = smp.cl;
    sn.cop[0] = smp.cop[0]; sn.cop[1] = smp.cop[1]; sn.cop[2] = smp.cop[2];
    sn.cpMin = smp.cpMin; sn.cpMax = smp.cpMax;

    int res = std::max(8, std::min(32, resTarget));
    sn.rx = std::min(res, std::max(1, lbmNx));
    sn.ry = std::min(res, std::max(1, lbmNy));
    sn.rz = std::min(res, std::max(1, lbmNz));
    const int n = sn.rx * sn.ry * sn.rz;
    sn.solid.assign(n, 0);
    sn.p.assign(n, 0.0f);
    sn.ux.assign(n, 0.0f);
    sn.uy.assign(n, 0.0f);
    sn.uz.assign(n, 0.0f);
    sn.vort.assign(n, 0.0f);
    sn.qcrit.assign(n, 0.0f);

    const bool hasVort = ((int)lbmVorticityMag.size() == lbmNx * lbmNy * lbmNz);
    const bool hasQ    = ((int)lbmQCriterion.size() == lbmNx * lbmNy * lbmNz);
    float maxVort = 0.0f, maxQ = 0.0f;

    for (int sz = 0; sz < sn.rz; ++sz) {
        int iz = std::min(lbmNz - 1, (int)((sz + 0.5f) * lbmNz / (float)sn.rz));
        for (int sy = 0; sy < sn.ry; ++sy) {
            int iy = std::min(lbmNy - 1, (int)((sy + 0.5f) * lbmNy / (float)sn.ry));
            for (int sx = 0; sx < sn.rx; ++sx) {
                int ix = std::min(lbmNx - 1, (int)((sx + 0.5f) * lbmNx / (float)sn.rx));
                int src = (iz * lbmNy + iy) * lbmNx + ix;
                int dst = (sz * sn.ry + sy) * sn.rx + sx;
                if (lbmIsSolid[src] || lbmIsGround[src]) {
                    sn.solid[dst] = 1;
                    continue;
                }
                sn.p[dst]  = lbmPressure[src];
                sn.ux[dst] = lbmUxWorld[src];
                sn.uy[dst] = lbmUyWorld[src];
                sn.uz[dst] = lbmUzWorld[src];
                if (hasVort) {
                    float v = lbmVorticityMag[src];
                    sn.vort[dst] = v;
                    if (v > maxVort) maxVort = v;
                }
                if (hasQ) {
                    float qv = lbmQCriterion[src];
                    sn.qcrit[dst] = qv;
                    if (qv > maxQ) maxQ = qv;
                }
            }
        }
    }
    sn.maxVorticity = maxVort;
    sn.maxQCrit = maxQ;
    return sn;
}

static void recordOneSample(int stepRel, float dtStepPhys, std::vector<PhysSimSample>& outSamples) {
    PhysSimSample smp;
    smp.step = lbmCurrentStep;
    smp.timePhys = (float)stepRel * dtStepPhys;
    smp.avgRho = lbmAvgRho;
    smp.avgKineticLB = lbmAvgKineticEnergy;
    smp.maxVelLB = lbmMaxVelocityLB;
    smp.maxVelWorld = lbmMaxVelocityWorld;
    smp.enstrophy = lbmEnstrophy();
    smp.tke = lbmTKE;
    smp.maxNuTRatio = lbmMaxNuTRatio();
    smp.residualRho = lbmResidualRho;
    smp.residualU = lbmGetResidualU();
    smp.negPops = lbmNegativePops;
    smp.badPops = lbmBadPops;
    smp.uLimited = lbmULimited;
    smp.thermalMeanT = thermalParams.enabled ? thermalMeanT : 0.0f;
    smp.thermalNusselt = thermalParams.enabled ? thermalNusselt : 1.0f;

    glm::vec3 dV, lV, mV, copV;
    float cd = 0, cl = 0, ld = 0, cpMin = 0, cpMax = 0;
    computeForcesPureCPU(dV, lV, mV, copV, cd, cl, ld, cpMin, cpMax);
    smp.dragN = glm::length(dV);
    smp.liftN = glm::length(lV);
    smp.momentNm = glm::length(mV);
    smp.cd = cd;
    smp.cl = cl;
    smp.ld = ld;
    smp.cpMin = cpMin;
    smp.cpMax = cpMax;
    smp.cop[0] = copV.x; smp.cop[1] = copV.y; smp.cop[2] = copV.z;

    sampleWakeDeficits(smp.wakeDeficit1L, smp.wakeDeficit2L, smp.wakeDeficit4L);
    outSamples.push_back(smp);
}

// Этап 1: ядро предсимуляции (вызывается из blocking или worker thread)
static bool executeStage1(const PhysSimConfig& cfg) {
    lbmStopAsyncWorker();
    s_stepsDone = 0;
    s_stepsTarget = std::max(1, cfg.targetSteps);
    s_elapsedSec = 0.0;
    s_mlups = 0.0;
    s_activeSnap = -1;

    if (cfg.flagshipMode) {
        lbmApplyFlagshipPreset();
    }
    lbmParams.enabled = true;

    if (cfg.resetBeforeRun || !lbmInitialized) {
        setStatus("Initializing LBM domain & SDF...");
        initLBM();
    }
    if (!lbmInitialized || lbmNx <= 0 || lbmNy <= 0 || lbmNz <= 0) {
        setStatus("Error: LBM failed to initialize");
        s_stage = (int)PhysSimStage::Error;
        return false;
    }

    const int target = std::max(1, cfg.targetSteps);
    const int metricsEvery = std::max(1, cfg.metricsEvery);
    const int snapshotEvery = std::max(metricsEvery, cfg.snapshotEvery);
    float dtPhys = lbmLatticeDtWorld();
    if (!std::isfinite(dtPhys) || dtPhys <= 0.0f) {
        float csAvg = (lbmCellSizeX + lbmCellSizeY + lbmCellSizeZ) / 3.0f;
        dtPhys = (flowSpeed > 1e-4f) ? (csAvg * 0.08f / flowSpeed) : 1e-4f;
    }

    std::vector<PhysSimSample> localSamples;
    std::vector<PhysSimSnapshot> localSnapshots;
    localSamples.reserve((size_t)(target / metricsEvery) + 4);
    localSnapshots.reserve((size_t)(target / snapshotEvery) + 4);

    // Снимаем начальное состояние (шаг 0)
    stepLBMCPUEx(0, true);
    if (cfg.computeVorticity) computeLBMVorticityAndQ();
    recordOneSample(0, dtPhys, localSamples);
    localSnapshots.push_back(captureSnapshot(cfg.snapshotRes, localSamples.back()));

    auto tStart = std::chrono::high_resolution_clock::now();
    const double totalCellsD = (double)lbmNx * (double)lbmNy * (double)lbmNz;

    int done = 0;
    while (done < target && !s_cancel.load()) {
        int nextMetric = ((done / metricsEvery) + 1) * metricsEvery;
        int nextSnap   = ((done / snapshotEvery) + 1) * snapshotEvery;
        int nextStop   = std::min(target, std::min(nextMetric, nextSnap));
        int chunk      = std::max(1, nextStop - done);

        bool needSample = (nextStop == nextMetric || nextStop == target);
        bool needSnap   = (nextStop == nextSnap   || nextStop == target);

        stepLBMCPUEx(chunk, needSample || needSnap);
        done += chunk;

        if (needSnap && cfg.computeVorticity) {
            computeLBMVorticityAndQ();
        }
        if (needSample || needSnap) {
            recordOneSample(done, dtPhys, localSamples);
        }
        if (needSnap) {
            localSnapshots.push_back(captureSnapshot(cfg.snapshotRes, localSamples.back()));
        }

        auto tNow = std::chrono::high_resolution_clock::now();
        double elapsed = std::chrono::duration<double>(tNow - tStart).count();
        s_stepsDone = done;
        s_elapsedSec = elapsed;
        if (elapsed > 1e-6) {
            s_mlups = (totalCellsD * (double)done) / (elapsed * 1e6);
        }
        char stBuf[128];
        std::snprintf(stBuf, sizeof(stBuf), "Simulating %d/%d steps (%.1f MLUPS)",
                      done, target, s_mlups.load());
        setStatus(stBuf);
    }

    {
        std::lock_guard<std::mutex> lk(s_dataMutex);
        s_samples.swap(localSamples);
        s_snapshots.swap(localSnapshots);
        if (!s_snapshots.empty()) s_activeSnap = (int)s_snapshots.size() - 1;
    }

    if (s_cancel.load() && s_samples.size() < 2) {
        setStatus("Pre-simulation cancelled");
        s_stage = (int)PhysSimStage::Idle;
        return false;
    }

    if (cfg.autoAnalyze) {
        setStatus("Analyzing physical behavior...");
        physSimAnalyzeBehavior();
    }
    if (cfg.autoExport) {
        setStatus("Exporting behavior report...");
        physSimExportAll(cfg.outputDir, cfg.runTag);
    }

    s_stage = (int)PhysSimStage::InspectReady;
    setStatus("Stage 2 Ready: Inspection & Behavior Report");
    return true;
}

} // namespace

// =====================================================================
// Публичный API
// =====================================================================

PhysSimConfig& physSimConfig() { return s_cfg; }

bool physSimRunBlocking(const PhysSimConfig& cfg) {
    if (s_running.load()) return false;
    if (s_worker.joinable()) s_worker.join();
    s_cfg = cfg;
    s_cancel = false;
    s_running = true;
    s_stage = (int)PhysSimStage::Simulating;
    bool ok = executeStage1(cfg);
    s_running = false;
    return ok;
}

bool physSimStartAsync(const PhysSimConfig& cfg) {
    if (s_running.load()) return false;
    if (s_worker.joinable()) s_worker.join();
    s_cfg = cfg;
    s_cancel = false;
    s_running = true;
    s_stage = (int)PhysSimStage::Simulating;
    s_worker = std::thread([cfg]() {
        executeStage1(cfg);
        s_running = false;
    });
    return true;
}

void physSimCancel() {
    s_cancel = true;
}

bool physSimIsRunning() { return s_running.load(); }
PhysSimStage physSimStage() { return (PhysSimStage)s_stage.load(); }

float physSimProgress() {
    int tgt = s_stepsTarget.load();
    if (tgt <= 0) return 0.0f;
    return glm::clamp((float)s_stepsDone.load() / (float)tgt, 0.0f, 1.0f);
}

int physSimCurrentStepDone() { return s_stepsDone.load(); }
int physSimTargetSteps() { return s_stepsTarget.load(); }
double physSimElapsedSec() { return s_elapsedSec.load(); }
double physSimCurrentMLUPS() { return s_mlups.load(); }

std::string physSimStatusText() {
    std::lock_guard<std::mutex> lk(s_statusMutex);
    return s_statusText;
}

void physSimShutdown() {
    s_cancel = true;
    if (s_worker.joinable()) s_worker.join();
    s_running = false;
}

void physSimClear() {
    if (s_running.load()) return;
    std::lock_guard<std::mutex> lk(s_dataMutex);
    s_samples.clear();
    s_snapshots.clear();
    s_report = PhysBehaviorReport();
    s_activeSnap = -1;
    s_stepsDone = 0;
    s_stage = (int)PhysSimStage::Idle;
    setStatus("Idle");
}

int physSimSampleCount() {
    std::lock_guard<std::mutex> lk(s_dataMutex);
    return (int)s_samples.size();
}

bool physSimGetSample(int idx, PhysSimSample* out) {
    if (!out) return false;
    std::lock_guard<std::mutex> lk(s_dataMutex);
    if (idx < 0 || idx >= (int)s_samples.size()) return false;
    *out = s_samples[idx];
    return true;
}

const std::vector<PhysSimSample>& physSimSamples() { return s_samples; }

int physSimSnapshotCount() {
    std::lock_guard<std::mutex> lk(s_dataMutex);
    return (int)s_snapshots.size();
}

bool physSimGetSnapshot(int idx, PhysSimSnapshot* out) {
    if (!out) return false;
    std::lock_guard<std::mutex> lk(s_dataMutex);
    if (idx < 0 || idx >= (int)s_snapshots.size()) return false;
    *out = s_snapshots[idx];
    return true;
}

int physSimActiveSnapshotIndex() { return s_activeSnap.load(); }

bool physSimApplySnapshotToWorld(int snapIdx) {
    PhysSimSnapshot sn;
    {
        std::lock_guard<std::mutex> lk(s_dataMutex);
        if (snapIdx < 0 || snapIdx >= (int)s_snapshots.size()) return false;
        sn = s_snapshots[snapIdx];
    }
    if (!lbmInitialized || lbmNx <= 0 || lbmNy <= 0 || lbmNz <= 0) return false;
    if (sn.rx <= 0 || sn.ry <= 0 || sn.rz <= 0) return false;

    const int Nx = lbmNx, Ny = lbmNy, Nz = lbmNz;
    const int total = Nx * Ny * Nz;
    if ((int)lbmPressure.size() != total || (int)lbmUxWorld.size() != total) return false;

    auto sampleSnap = [&](const std::vector<float>& src, int x, int y, int z) -> float {
        float fx = ((x + 0.5f) / (float)Nx) * (float)sn.rx - 0.5f;
        float fy = ((y + 0.5f) / (float)Ny) * (float)sn.ry - 0.5f;
        float fz = ((z + 0.5f) / (float)Nz) * (float)sn.rz - 0.5f;
        int x0 = glm::clamp((int)std::floor(fx), 0, sn.rx - 1);
        int y0 = glm::clamp((int)std::floor(fy), 0, sn.ry - 1);
        int z0 = glm::clamp((int)std::floor(fz), 0, sn.rz - 1);
        int x1 = std::min(x0 + 1, sn.rx - 1);
        int y1 = std::min(y0 + 1, sn.ry - 1);
        int z1 = std::min(z0 + 1, sn.rz - 1);
        float tx = glm::clamp(fx - x0, 0.0f, 1.0f);
        float ty = glm::clamp(fy - y0, 0.0f, 1.0f);
        float tz = glm::clamp(fz - z0, 0.0f, 1.0f);

        double acc = 0.0, wsum = 0.0;
        const int xs[2] = {x0, x1}, ys[2] = {y0, y1}, zs[2] = {z0, z1};
        const float wx[2] = {1.0f - tx, tx};
        const float wy[2] = {1.0f - ty, ty};
        const float wz[2] = {1.0f - tz, tz};
        for (int k = 0; k < 2; ++k)
            for (int j = 0; j < 2; ++j)
                for (int i = 0; i < 2; ++i) {
                    int sidx = (zs[k] * sn.ry + ys[j]) * sn.rx + xs[i];
                    if (sn.solid[sidx]) continue;
                    double ww = (double)wx[i] * (double)wy[j] * (double)wz[k];
                    acc += ww * (double)src[sidx];
                    wsum += ww;
                }
        return (wsum > 1e-9) ? (float)(acc / wsum) : 0.0f;
    };

    const bool writeVort = ((int)lbmVorticityMag.size() == total);
    const bool writeQ    = ((int)lbmQCriterion.size() == total);
    float pMin = 1e30f, pMax = -1e30f;

    #ifdef _OPENMP
    #pragma omp parallel
    {
    float tPMin = 1e30f, tPMax = -1e30f;
    #pragma omp for nowait
    #else
    float tPMin = 1e30f, tPMax = -1e30f;
    #endif
    for (int z = 0; z < Nz; ++z) {
        for (int y = 0; y < Ny; ++y) {
            for (int x = 0; x < Nx; ++x) {
                int cell = (z * Ny + y) * Nx + x;
                if (lbmIsSolid[cell] || lbmIsGround[cell]) {
                    lbmPressure[cell] = 0.0f;
                    lbmUxWorld[cell] = 0.0f; lbmUyWorld[cell] = 0.0f; lbmUzWorld[cell] = 0.0f;
                    if (writeVort) lbmVorticityMag[cell] = 0.0f;
                    if (writeQ)    lbmQCriterion[cell] = 0.0f;
                    continue;
                }
                float p  = sampleSnap(sn.p, x, y, z);
                lbmPressure[cell] = p;
                if (p < tPMin) tPMin = p;
                if (p > tPMax) tPMax = p;
                lbmUxWorld[cell] = sampleSnap(sn.ux, x, y, z);
                lbmUyWorld[cell] = sampleSnap(sn.uy, x, y, z);
                lbmUzWorld[cell] = sampleSnap(sn.uz, x, y, z);
                if (writeVort) lbmVorticityMag[cell] = sampleSnap(sn.vort, x, y, z);
                if (writeQ)    lbmQCriterion[cell]    = sampleSnap(sn.qcrit, x, y, z);
            }
        }
    }
    #ifdef _OPENMP
    #pragma omp critical
    {
        if (tPMin < pMin) pMin = tPMin;
        if (tPMax > pMax) pMax = tPMax;
    }
    }
    #else
    pMin = tPMin; pMax = tPMax;
    #endif

    lbmPressureMin = (pMin <= pMax) ? pMin : 0.0f;
    lbmPressureMax = (pMin <= pMax) ? pMax : 0.0f;
    lbmCurrentStep = sn.step;
    glm::vec3 dir = computeFlowDirWorld();
    dragVector = dir * sn.dragN;
    liftVector = glm::vec3(0.0f, sn.liftN, 0.0f);
    dragMagnitude = sn.dragN;
    liftMagnitude = sn.liftN;
    momentMagnitude = sn.momentNm;
    centerOfPressure = glm::vec3(sn.cop[0], sn.cop[1], sn.cop[2]);
    liftToDragRatio = (sn.dragN > 1e-6f) ? (sn.liftN / sn.dragN) : 0.0f;

    s_activeSnap = snapIdx;
    return true;
}

// =====================================================================
// Этап 2: Чистый C++17 анализатор физического поведения и генератор отчёта
// =====================================================================
bool physSimAnalyzeBehavior() {
    std::vector<PhysSimSample> smp;
    int snapCount = 0;
    {
        std::lock_guard<std::mutex> lk(s_dataMutex);
        smp = s_samples;
        snapCount = (int)s_snapshots.size();
    }
    if (smp.size() < 2) return false;

    PhysBehaviorReport r;
    r.valid = true;
    r.gridNx = lbmNx; r.gridNy = lbmNy; r.gridNz = lbmNz;
    r.totalCells = (long long)lbmNx * lbmNy * lbmNz;
    r.fluidCells = lbmFluidCellCount();
    r.blockageRatio = lbmBlockageRatio;
    r.lChar = std::max(0.01f, maxDim);
    r.refArea = aeroRefArea;
    r.flowSpeed = flowSpeed;
    r.rePhys = lbmReynolds;
    r.reLB = lbmReynoldsLB;
    r.mach = (speedOfSound > 1.0f) ? (flowSpeed / speedOfSound) : 0.0f;
    r.tau0 = lbmParams.tau;
    r.collisionName = lbmCollisionModeName(lbmEffectiveCollisionMode());
    r.turbulenceName = lbmTurbulenceModelName(lbmEffectiveTurbulenceModel());
    r.curvedBFL = lbmParams.useCurvedBoundary;
    r.thermalEnabled = thermalParams.enabled;
    r.boundarySummary = lbmParams.periodicX
        ? "Periodic X"
        : (lbmParams.useFlagshipBoundaries
            ? ("NonEq Inlet + NonEq Outlet + Sponge(" + std::to_string(lbmParams.spongeLayers) + ") + Free-Slip Side")
            : "Equilibrium Inlet + Convective Outlet");

    r.stepsSimulated = smp.back().step - smp.front().step;
    r.totalPhysTimeS = smp.back().timePhys - smp.front().timePhys;
    r.wallTimeS = s_elapsedSec.load();
    r.achievedMLUPS = s_mlups.load();
    r.samplesCount = (int)smp.size();
    r.snapshotsCount = snapCount;

    // 1) Аудит сохранения массы и устойчивости
    r.rhoInitial = smp.front().avgRho;
    r.rhoFinal   = smp.back().avgRho;
    float rhoMin = 1e9f, rhoMax = -1e9f;
    double rhoSum = 0.0;
    float maxVelLB = 0.0f;
    float peakEnst = -1.0f; int peakEnstStep = 0;
    float peakTKE = 0.0f, peakNuT = 0.0f;
    float cpMinPeak = 1e9f, cpMaxStag = -1e9f;

    for (const auto& s : smp) {
        if (s.avgRho < rhoMin) rhoMin = s.avgRho;
        if (s.avgRho > rhoMax) rhoMax = s.avgRho;
        rhoSum += s.avgRho;
        if (s.maxVelLB > maxVelLB) maxVelLB = s.maxVelLB;
        if (s.enstrophy > peakEnst) { peakEnst = s.enstrophy; peakEnstStep = s.step; }
        if (s.tke > peakTKE) peakTKE = s.tke;
        if (s.maxNuTRatio > peakNuT) peakNuT = s.maxNuTRatio;
        if (s.cpMin < cpMinPeak) cpMinPeak = s.cpMin;
        if (s.cpMax > cpMaxStag) cpMaxStag = s.cpMax;
        r.totalNegPops += s.negPops;
        r.totalBadPops += s.badPops;
        r.totalULimited += s.uLimited;
        if (!std::isfinite(s.avgRho) || s.avgRho < 0.8f || s.avgRho > 1.2f) r.hadDivergence = true;
    }
    double rhoMean = rhoSum / (double)smp.size();
    r.rhoMin = rhoMin;
    r.rhoMax = rhoMax;
    r.massDriftRel = (rhoMean > 1e-6) ? (float)((rhoMax - rhoMin) / rhoMean) : 0.0f;
    r.maxMachLB = maxVelLB * std::sqrt(3.0f);
    r.peakEnstrophy = std::max(0.0f, peakEnst);
    r.peakEnstrophyStep = peakEnstStep;
    r.finalEnstrophy = smp.back().enstrophy;
    r.peakTKE = peakTKE;
    r.peakNuTRatio = peakNuT;
    r.cpMinPeak = (cpMinPeak < 1e8f) ? cpMinPeak : 0.0f;
    r.cpMaxStagnation = (cpMaxStag > -1e8f) ? cpMaxStag : 0.0f;
    r.finalWakeDeficit1L = smp.back().wakeDeficit1L;
    r.finalWakeDeficit2L = smp.back().wakeDeficit2L;
    r.finalWakeDeficit4L = smp.back().wakeDeficit4L;
    r.finalResidualRho = smp.back().residualRho;
    r.finalResidualU = smp.back().residualU;

    // 2) Определение конца начального импульсивного переходного процесса
    const int n = (int)smp.size();
    const int settledIdx = std::max(1, (int)(n * 0.60));
    r.settledStartStep = smp[settledIdx].step;
    r.settledStartTimeS = smp[settledIdx].timePhys;

    // Находим, когда Cd впервые вошёл в +-10% коридор от финального среднего
    double cdTailMean = 0.0;
    for (int i = settledIdx; i < n; ++i) cdTailMean += smp[i].cd;
    cdTailMean /= (double)std::max(1, n - settledIdx);

    int transIdx = settledIdx;
    for (int i = 1; i < settledIdx; ++i) {
        if (std::fabs(cdTailMean) > 1e-4) {
            bool staysInBand = true;
            for (int j = i; j < std::min(n, i + 4); ++j) {
                if (std::fabs(smp[j].cd - cdTailMean) > 0.12 * std::fabs(cdTailMean) + 1e-3) {
                    staysInBand = false; break;
                }
            }
            if (staysInBand) { transIdx = i; break; }
        } else if (smp[i].residualRho < smp[0].residualRho * 0.25f) {
            transIdx = i; break;
        }
    }
    r.transientEndStep = smp[transIdx].step;
    r.transientEndTimeS = smp[transIdx].timePhys;

    // 3) Статистика по установившемуся окну
    double sumCd = 0, sumCl = 0, sumDrag = 0, sumLift = 0, sumMom = 0;
    float cdMin = 1e9f, cdMax = -1e9f, clMin = 1e9f, clMax = -1e9f;
    const int m = n - settledIdx;
    for (int i = settledIdx; i < n; ++i) {
        const auto& s = smp[i];
        sumCd += s.cd; sumCl += s.cl;
        sumDrag += s.dragN; sumLift += s.liftN; sumMom += s.momentNm;
        if (s.cd < cdMin) cdMin = s.cd;
        if (s.cd > cdMax) cdMax = s.cd;
        if (s.cl < clMin) clMin = s.cl;
        if (s.cl > clMax) clMax = s.cl;
    }
    r.cdMean = (float)(sumCd / m); r.cdMin = cdMin; r.cdMax = cdMax;
    r.clMean = (float)(sumCl / m); r.clMin = clMin; r.clMax = clMax;
    r.dragMeanN = (float)(sumDrag / m);
    r.liftMeanN = (float)(sumLift / m);
    r.momentMeanNm = (float)(sumMom / m);
    r.ldMean = (std::fabs(r.cdMean) > 1e-6f) ? (r.clMean / r.cdMean) : 0.0f;

    double varCd = 0, varCl = 0;
    for (int i = settledIdx; i < n; ++i) {
        double dc = smp[i].cd - r.cdMean;
        double dl = smp[i].cl - r.clMean;
        varCd += dc * dc;
        varCl += dl * dl;
    }
    r.cdStd = (float)std::sqrt(varCd / m);
    r.clStd = (float)std::sqrt(varCl / m);

    // 4) Спектральный анализ DFT (чистый C++) по сигналу Cl(t) (или Cd(t), если Cl~0)
    const int specStart = std::max(1, (int)(n * 0.35));
    const int M = n - specStart;
    if (M >= 16) {
        double dtWindow = (smp.back().timePhys - smp[specStart].timePhys) / (double)(M - 1);
        if (dtWindow > 1e-9) {
            double meanSig = 0.0;
            bool useCl = (r.clStd > 1e-5f);
            for (int i = specStart; i < n; ++i) meanSig += useCl ? smp[i].cl : smp[i].cd;
            meanSig /= (double)M;

            double energy = 0.0;
            std::vector<double> x(M);
            for (int i = 0; i < M; ++i) {
                x[i] = (useCl ? smp[specStart + i].cl : smp[specStart + i].cd) - meanSig;
                energy += x[i] * x[i];
            }
            if (energy > 1e-12) {
                const int kMax = M / 2;
                double bestAmp = 0.0, sumAmp = 0.0;
                int bestK = 0;
                for (int k = 1; k <= kMax; ++k) {
                    double re = 0.0, im = 0.0;
                    double angStep = 2.0 * M_PI * (double)k / (double)M;
                    for (int j = 0; j < M; ++j) {
                        double a = angStep * (double)j;
                        re += x[j] * std::cos(a);
                        im -= x[j] * std::sin(a);
                    }
                    double amp = std::sqrt(re * re + im * im) * (2.0 / (double)M);
                    sumAmp += amp;
                    if (amp > bestAmp) { bestAmp = amp; bestK = k; }
                }
                double meanAmp = (kMax > 0) ? (sumAmp / (double)kMax) : 0.0;
                double prom = (meanAmp > 1e-12) ? (bestAmp / meanAmp) : 0.0;
                r.spectralProminence = (float)prom;
                if (bestK > 0 && prom >= 3.0 && bestAmp > 1e-4) {
                    double freq = (double)bestK / ((double)M * dtWindow);
                    r.hasDominantShedding = true;
                    r.dominantFreqHz = (float)freq;
                    r.sheddingPeriodS = (freq > 1e-9) ? (float)(1.0 / freq) : 0.0f;
                    r.strouhalNumber = (r.flowSpeed > 1e-4f)
                        ? (float)(freq * (double)r.lChar / (double)r.flowSpeed) : 0.0f;
                }
            }
        }
    }

    // 5) Классификация режима течения
    float cdRelOsc = (std::fabs(r.cdMean) > 1e-4f) ? (r.cdStd / std::fabs(r.cdMean)) : r.cdStd;
    if (r.hadDivergence) {
        r.regime = PhysFlowRegime::UnstableDiverged;
        r.regimeName = "Unstable / Diverged";
        r.regimeReason = "Density exceeded physical stability bounds [0.8, 1.2].";
    } else if (r.hasDominantShedding && r.spectralProminence >= 3.2f) {
        r.regime = PhysFlowRegime::PeriodicVortexShedding;
        r.regimeName = "Periodic Vortex Shedding (von Karman / Oscillatory Wake)";
        r.regimeReason = "Clear spectral peak detected in force history (Strouhal shedding active).";
    } else if (cdRelOsc < 0.008f && r.clStd < 0.005f && r.finalResidualRho < 5e-4f) {
        r.regime = PhysFlowRegime::SteadyLaminar;
        r.regimeName = "Steady / Asymptotically Converged";
        r.regimeReason = "Aerodynamic forces and density residual converged to steady equilibrium.";
    } else if (r.peakNuTRatio > 0.25f || cdRelOsc >= 0.015f || r.clStd >= 0.01f) {
        r.regime = PhysFlowRegime::TurbulentUnsteady;
        r.regimeName = "Turbulent / Unsteady Broadband";
        r.regimeReason = "Broadband unsteady wake fluctuations and active subgrid eddy viscosity.";
    } else {
        r.regime = PhysFlowRegime::TransitionalDeveloping;
        r.regimeName = "Transitional / Developing Flow";
        r.regimeReason = "Flow structures and wake are still developing across the simulated window.";
    }

    // 6) Хронологический журнал событий физического поведения
    r.events.clear();
    {
        PhysSimEvent ev;
        ev.step = smp.front().step;
        ev.timePhys = smp.front().timePhys;
        ev.phase = "Phase 1: Impulsive Initialization";
        ev.title = "Uniform freestream and boundary activation";
        std::ostringstream ds;
        ds << "Initialized " << r.gridNx << "x" << r.gridNy << "x" << r.gridNz
           << " (" << r.fluidCells << " fluid cells) at U=" << std::fixed << std::setprecision(2)
           << r.flowSpeed << " m/s, Re_phys=" << std::setprecision(0) << r.rePhys
           << ", Re_LB=" << std::setprecision(1) << r.reLB << ", operator=" << r.collisionName
           << ", turbulence=" << r.turbulenceName << ".";
        ev.detail = ds.str();
        r.events.push_back(ev);
    }

    // Пик точки торможения / начальной волны давления
    int stagIdx = 0;
    for (int i = 1; i < n; ++i) {
        if (smp[i].cpMax > smp[stagIdx].cpMax) stagIdx = i;
    }
    if (stagIdx > 0) {
        PhysSimEvent ev;
        ev.step = smp[stagIdx].step;
        ev.timePhys = smp[stagIdx].timePhys;
        ev.phase = "Phase 2: Acoustic & Stagnation Build-up";
        ev.title = "Peak stagnation pressure & bow compression";
        std::ostringstream ds;
        ds << "Surface stagnation Cp reached peak " << std::fixed << std::setprecision(3)
           << smp[stagIdx].cpMax << " (min suction Cp=" << smp[stagIdx].cpMin
           << "), Drag=" << std::setprecision(3) << smp[stagIdx].dragN << " N (Cd=" << smp[stagIdx].cd << ").";
        ev.detail = ds.str();
        r.events.push_back(ev);
    }

    // Пик завихренности / срыва сдвигового слоя
    int enstIdx = 0;
    for (int i = 1; i < n; ++i) {
        if (smp[i].enstrophy > smp[enstIdx].enstrophy) enstIdx = i;
    }
    if (enstIdx > 0 && smp[enstIdx].enstrophy > 1e-6f) {
        PhysSimEvent ev;
        ev.step = smp[enstIdx].step;
        ev.timePhys = smp[enstIdx].timePhys;
        ev.phase = "Phase 3: Shear-Layer Roll-up & Wake Separation";
        ev.title = "Peak domain enstrophy & vortex formation";
        std::ostringstream ds;
        ds << "Enstrophy peaked at " << std::scientific << std::setprecision(3) << smp[enstIdx].enstrophy
           << ", TKE=" << smp[enstIdx].tke << " m2/s2, eddy-viscosity ratio max(nu_t/nu0)="
           << std::fixed << std::setprecision(2) << smp[enstIdx].maxNuTRatio << ".";
        ev.detail = ds.str();
        r.events.push_back(ev);
    }

    // Конец переходного процесса
    if (transIdx > 0 && transIdx < n) {
        PhysSimEvent ev;
        ev.step = r.transientEndStep;
        ev.timePhys = r.transientEndTimeS;
        ev.phase = "Phase 4: Transition to Asymptotic State";
        ev.title = "Initial startup transient subsided";
        std::ostringstream ds;
        ds << "Drag coefficient entered +/-12% band of settled mean (Cd=" << std::fixed << std::setprecision(4)
           << smp[transIdx].cd << ", wake deficit at 1L=" << std::setprecision(2)
           << (smp[transIdx].wakeDeficit1L * 100.0f) << "%).";
        ev.detail = ds.str();
        r.events.push_back(ev);
    }

    // Финальное состояние
    {
        PhysSimEvent ev;
        ev.step = smp.back().step;
        ev.timePhys = smp.back().timePhys;
        ev.phase = "Phase 5: Final Regime Summary";
        ev.title = r.regimeName;
        std::ostringstream ds;
        ds << "Settled Cd=" << std::fixed << std::setprecision(4) << r.cdMean << " +/- " << r.cdStd
           << ", Cl=" << r.clMean << " +/- " << r.clStd
           << ", wake recovery (1L/2L/4L deficit)=" << std::setprecision(1)
           << (r.finalWakeDeficit1L * 100.0f) << "% / "
           << (r.finalWakeDeficit2L * 100.0f) << "% / "
           << (r.finalWakeDeficit4L * 100.0f) << "%.";
        if (r.hasDominantShedding) {
            ds << " Shedding f=" << std::setprecision(2) << r.dominantFreqHz
               << " Hz (St=" << std::setprecision(3) << r.strouhalNumber << ").";
        }
        ev.detail = ds.str();
        r.events.push_back(ev);
    }

    // 7) Генерация полного Markdown-аккаунта поведения системы
    std::ostringstream md;
    md << "# Aeros Engine — Physical Behavior & Pre-Simulation Report\n\n";
    md << "## 1. Executive Classification\n";
    md << "- **Flow Regime:** " << r.regimeName << "\n";
    md << "- **Diagnostic Basis:** " << r.regimeReason << "\n";
    md << "- **Simulated Horizon:** " << r.stepsSimulated << " LBM steps ("
       << std::fixed << std::setprecision(4) << r.totalPhysTimeS << " s physical time)\n";
    md << "- **Pre-Simulation Throughput:** " << std::setprecision(2) << r.wallTimeS
       << " s wall-clock, **" << r.achievedMLUPS << " MLUPS** ("
       << r.samplesCount << " scalar samples, " << r.snapshotsCount << " 3D snapshots)\n\n";

    md << "## 2. Solver & Physical Setup\n";
    md << "- **Grid:** " << r.gridNx << " x " << r.gridNy << " x " << r.gridNz
       << " (" << r.totalCells << " total, " << r.fluidCells << " fluid, blockage "
       << std::setprecision(2) << (r.blockageRatio * 100.0f) << "%)\n";
    md << "- **Freestream:** U = " << r.flowSpeed << " m/s, Mach = " << std::setprecision(3) << r.mach
       << ", Re_phys = " << std::setprecision(0) << r.rePhys
       << ", Re_LB = " << std::setprecision(1) << r.reLB << "\n";
    md << "- **Physics Kernels (C++17):** Collision = `" << r.collisionName
       << "`, Turbulence = `" << r.turbulenceName
       << "`, Curved Wall BFL = " << (r.curvedBFL ? "ON" : "OFF")
       << ", Thermal D3Q7 = " << (r.thermalEnabled ? "ON" : "OFF") << "\n";
    md << "- **Boundaries:** " << r.boundarySummary << "\n\n";

    md << "## 3. Conservation & Numerical Health Audit\n";
    md << "- **Mass Conservation:** rho0 = " << std::setprecision(6) << r.rhoInitial
       << ", rho_final = " << r.rhoFinal
       << ", range [" << r.rhoMin << ", " << r.rhoMax << "]"
       << ", relative drift = " << std::scientific << std::setprecision(3) << r.massDriftRel << "\n";
    md << "- **Compressibility Limit:** max Mach_LB = " << std::fixed << std::setprecision(3) << r.maxMachLB
       << " (stability limit < 0.52)\n";
    md << "- **Health Counters:** negPops = " << r.totalNegPops
       << ", badPops = " << r.totalBadPops
       << ", uLimited = " << r.totalULimited
       << ", divergence = " << (r.hadDivergence ? "YES" : "NO") << "\n";
    md << "- **Final Residuals:** RMS(drho) = " << std::scientific << std::setprecision(3)
       << r.finalResidualRho << ", |dU_max| = " << r.finalResidualU << "\n\n";

    md << "## 4. Aerodynamic Forces, Wake & Spectral Analysis (Settled Window)\n";
    md << "| Quantity | Mean | Std (RMS) | Min | Max |\n";
    md << "|---|---:|---:|---:|---:|\n";
    md << "| Drag Coefficient Cd | " << std::fixed << std::setprecision(4) << r.cdMean
       << " | " << r.cdStd << " | " << r.cdMin << " | " << r.cdMax << " |\n";
    md << "| Lift Coefficient Cl | " << r.clMean
       << " | " << r.clStd << " | " << r.clMin << " | " << r.clMax << " |\n";
    md << "| Drag Force (N) | " << std::setprecision(3) << r.dragMeanN << " | - | - | - |\n";
    md << "| Lift Force (N) | " << r.liftMeanN << " | - | - | - |\n";
    md << "| Moment (N*m) | " << r.momentMeanNm << " | - | - | - |\n\n";
    md << "- **L/D Ratio:** " << std::setprecision(3) << r.ldMean
       << " | **Peak Stagnation Cp:** " << r.cpMaxStagnation
       << " | **Peak Suction Cp:** " << r.cpMinPeak << "\n";
    md << "- **Wake Velocity Deficit (downstream):** 1L = " << std::setprecision(1)
       << (r.finalWakeDeficit1L * 100.0f) << "%, 2L = " << (r.finalWakeDeficit2L * 100.0f)
       << "%, 4L = " << (r.finalWakeDeficit4L * 100.0f) << "%\n";
    md << "- **Vorticity & Subgrid Turbulence:** Peak Enstrophy = " << std::scientific << std::setprecision(3)
       << r.peakEnstrophy << " (step " << r.peakEnstrophyStep << "), Peak TKE = "
       << r.peakTKE << " m2/s2, Peak nu_t/nu0 = " << std::fixed << std::setprecision(2)
       << r.peakNuTRatio << "\n";
    if (r.hasDominantShedding) {
        md << "- **Vortex Shedding (DFT):** f = " << std::setprecision(2) << r.dominantFreqHz
           << " Hz, Period T = " << std::setprecision(4) << r.sheddingPeriodS
           << " s, **Strouhal St = " << std::setprecision(3) << r.strouhalNumber
           << "** (spectral prominence = " << std::setprecision(1) << r.spectralProminence << "x)\n";
    } else {
        md << "- **Vortex Shedding (DFT):** No narrow-band periodic shedding peak detected (prominence = "
           << std::setprecision(2) << r.spectralProminence << "x)\n";
    }
    if (r.thermalEnabled) {
        md << "- **Thermal Transport (D3Q7):** Final Mean T = " << std::setprecision(4) << smp.back().thermalMeanT
           << ", Nusselt Nu = " << std::setprecision(3) << smp.back().thermalNusselt << "\n";
    }
    md << "\n## 5. Chronological Account of Physical Behavior\n";
    for (const auto& ev : r.events) {
        md << "- **[" << ev.phase << " @ step " << ev.step << ", t=" << std::fixed
           << std::setprecision(4) << ev.timePhys << " s] " << ev.title << ":** "
           << ev.detail << "\n";
    }

    r.markdownNarrative = md.str();
    {
        std::lock_guard<std::mutex> lk(s_dataMutex);
        s_report = r;
    }
    return true;
}

const PhysBehaviorReport& physSimReport() { return s_report; }

bool physSimExportAll(const std::string& outDir,
                      const std::string& tag,
                      std::string* outMdPath,
                      std::string* outCsvPath,
                      std::string* outJsonPath) {
    std::vector<PhysSimSample> smp;
    PhysBehaviorReport rep;
    {
        std::lock_guard<std::mutex> lk(s_dataMutex);
        smp = s_samples;
        rep = s_report;
    }
    if (smp.empty()) return false;
    if (!rep.valid) {
        physSimAnalyzeBehavior();
        std::lock_guard<std::mutex> lk(s_dataMutex);
        rep = s_report;
    }

    std::string dir = outDir.empty() ? "logs" : outDir;
    ensureDirExists(dir);
    std::string safeTag = tag.empty() ? "aeros_sim" : tag;
    std::string ts = makeTimestampTag();
    std::string base = dir + "/" + safeTag + "_" + ts;

    std::string mdPath   = base + "_behavior.md";
    std::string csvPath  = base + "_timeseries.csv";
    std::string jsonPath = base + "_summary.json";

    bool okAll = true;
    {
        std::ofstream f(mdPath);
        if (f) f << rep.markdownNarrative;
        else okAll = false;
    }
    {
        std::ofstream f(csvPath);
        if (f) {
            f << "step,time_s,avg_rho,avg_ke_lb,max_vel_lb,max_vel_ms,enstrophy,tke_m2s2,"
              << "nut_ratio,residual_rho,residual_u,drag_N,lift_N,moment_Nm,cd,cl,ld,"
              << "cp_min,cp_max,wake_def_1L,wake_def_2L,wake_def_4L,thermal_T,thermal_Nu,"
              << "neg_pops,bad_pops,u_limited\n";
            f << std::setprecision(7);
            for (const auto& s : smp) {
                f << s.step << "," << s.timePhys << "," << s.avgRho << ","
                  << s.avgKineticLB << "," << s.maxVelLB << "," << s.maxVelWorld << ","
                  << s.enstrophy << "," << s.tke << "," << s.maxNuTRatio << ","
                  << s.residualRho << "," << s.residualU << ","
                  << s.dragN << "," << s.liftN << "," << s.momentNm << ","
                  << s.cd << "," << s.cl << "," << s.ld << ","
                  << s.cpMin << "," << s.cpMax << ","
                  << s.wakeDeficit1L << "," << s.wakeDeficit2L << "," << s.wakeDeficit4L << ","
                  << s.thermalMeanT << "," << s.thermalNusselt << ","
                  << s.negPops << "," << s.badPops << "," << s.uLimited << "\n";
            }
        } else okAll = false;
    }
    {
        std::ofstream f(jsonPath);
        if (f) {
            f << "{\n";
            f << "  \"regime\": \"" << jsonEscape(rep.regimeName) << "\",\n";
            f << "  \"regime_reason\": \"" << jsonEscape(rep.regimeReason) << "\",\n";
            f << "  \"grid\": [" << rep.gridNx << ", " << rep.gridNy << ", " << rep.gridNz << "],\n";
            f << "  \"fluid_cells\": " << rep.fluidCells << ",\n";
            f << "  \"collision\": \"" << jsonEscape(rep.collisionName) << "\",\n";
            f << "  \"turbulence\": \"" << jsonEscape(rep.turbulenceName) << "\",\n";
            f << "  \"curved_bfl\": " << (rep.curvedBFL ? "true" : "false") << ",\n";
            f << "  \"steps_simulated\": " << rep.stepsSimulated << ",\n";
            f << "  \"phys_time_s\": " << rep.totalPhysTimeS << ",\n";
            f << "  \"wall_time_s\": " << rep.wallTimeS << ",\n";
            f << "  \"mlups\": " << rep.achievedMLUPS << ",\n";
            f << "  \"re_phys\": " << rep.rePhys << ",\n";
            f << "  \"re_lb\": " << rep.reLB << ",\n";
            f << "  \"mass_drift_rel\": " << rep.massDriftRel << ",\n";
            f << "  \"had_divergence\": " << (rep.hadDivergence ? "true" : "false") << ",\n";
            f << "  \"cd_mean\": " << rep.cdMean << ",\n";
            f << "  \"cd_std\": " << rep.cdStd << ",\n";
            f << "  \"cl_mean\": " << rep.clMean << ",\n";
            f << "  \"cl_std\": " << rep.clStd << ",\n";
            f << "  \"ld_mean\": " << rep.ldMean << ",\n";
            f << "  \"strouhal\": " << rep.strouhalNumber << ",\n";
            f << "  \"dominant_freq_hz\": " << rep.dominantFreqHz << ",\n";
            f << "  \"peak_enstrophy\": " << rep.peakEnstrophy << ",\n";
            f << "  \"peak_tke\": " << rep.peakTKE << ",\n";
            f << "  \"peak_nut_ratio\": " << rep.peakNuTRatio << ",\n";
            f << "  \"wake_deficit_1L\": " << rep.finalWakeDeficit1L << ",\n";
            f << "  \"wake_deficit_2L\": " << rep.finalWakeDeficit2L << ",\n";
            f << "  \"wake_deficit_4L\": " << rep.finalWakeDeficit4L << ",\n";
            f << "  \"events\": [\n";
            for (size_t i = 0; i < rep.events.size(); ++i) {
                const auto& ev = rep.events[i];
                f << "    {\"step\": " << ev.step << ", \"time_s\": " << ev.timePhys
                  << ", \"phase\": \"" << jsonEscape(ev.phase)
                  << "\", \"title\": \"" << jsonEscape(ev.title)
                  << "\", \"detail\": \"" << jsonEscape(ev.detail) << "\"}"
                  << (i + 1 < rep.events.size() ? "," : "") << "\n";
            }
            f << "  ]\n";
            f << "}\n";
        } else okAll = false;
    }

    s_lastMdPath = mdPath;
    s_lastCsvPath = csvPath;
    s_lastJsonPath = jsonPath;
    if (outMdPath) *outMdPath = mdPath;
    if (outCsvPath) *outCsvPath = csvPath;
    if (outJsonPath) *outJsonPath = jsonPath;

    if (okAll) {
        DBG_INFO("physSimExportAll: сохранено %s, %s, %s", mdPath.c_str(), csvPath.c_str(), jsonPath.c_str());
    }
    return okAll;
}

const std::string& physSimLastMdPath() { return s_lastMdPath; }
const std::string& physSimLastCsvPath() { return s_lastCsvPath; }
const std::string& physSimLastJsonPath() { return s_lastJsonPath; }
