#include "interesting.h"
#include "globals.h"
#include "flow_field.h"
#include "forces.h"
#include "voxel_grid.h"
#include "atmosphere.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <algorithm>
#include <iostream>
#include <random>
#include <chrono>

#ifdef _OPENMP
#include <omp.h>
#endif

// Globals
std::vector<SmokeParticle> g_smokeParticles;
std::vector<float> g_smokeDensityField;
int g_smokeNx = 32, g_smokeNy = 24, g_smokeNz = 32;
float g_smokeMinX = -2, g_smokeMinY = -1, g_smokeMinZ = -2;
float g_smokeMaxX = 2, g_smokeMaxY = 1, g_smokeMaxZ = 2;

std::vector<VortexCore> g_vortexCores;
FlightState g_flightState;
LICData g_licData;
std::vector<AcousticSource> g_acousticSources;
std::vector<std::vector<StreakPoint>> g_streaklines;
float g_perfInterestingMs = 0.0f;

static std::mt19937 rng(42);

// =====================================================
// Schlieren — градиент плотности |∇ρ|
// В сжимаемом течении ρ ~ p/(R T), p = p_inf + q*Cp
// ∇ρ ~ ∇p / (R T) — через градиент Cp и скорости
// Реальный шлирен: яркость ∝ ∂ρ/∂y (вертикальный нож)
// =====================================================
SchlierenData computeSchlierenAt(const glm::vec3& pos) {
    SchlierenData data{};
    float eps = maxDim * 0.015f;
    if (eps < 0.001f) eps = 0.01f;

    // Центральная точка
    glm::vec3 v0 = computeVelocityFieldCPU(pos, flowParams);
    float vMag0 = glm::length(v0);
    float p0 = 101325.0f; // упрощенно, реальное через Cp
    // Cp = 1 - (V/Vinf)², p = p_inf + q*Cp
    float Vinf = flowSpeed;
    if (Vinf < 0.1f) Vinf = 2.0f;
    float Cp0 = 1.0f - (vMag0*vMag0)/(Vinf*Vinf);
    float qInf = 0.5f * airDensity * Vinf*Vinf;
    p0 = airPressure + qInf * Cp0;
    float rho0 = p0 / (287.05f * airTemperature);

    // Градиенты по 6 соседям
    glm::vec3 posX = pos + glm::vec3(eps,0,0), posY = pos + glm::vec3(0,eps,0), posZ = pos + glm::vec3(0,0,eps);
    glm::vec3 posXm = pos - glm::vec3(eps,0,0), posYm = pos - glm::vec3(0,eps,0), posZm = pos - glm::vec3(0,0,eps);

    auto getRho = [&](glm::vec3 pp)->float {
        glm::vec3 vv = computeVelocityFieldCPU(pp, flowParams);
        float vm = glm::length(vv);
        float cp = 1.0f - (vm*vm)/(Vinf*Vinf);
        cp = std::max(-3.0f, std::min(cp, 1.0f));
        float pp_ = airPressure + qInf * cp;
        float rr = pp_ / (287.05f * airTemperature);
        if (!std::isfinite(rr)) rr = airDensity;
        return rr;
    };

    float rhoXp = getRho(posX), rhoXm = getRho(posXm);
    float rhoYp = getRho(posY), rhoYm = getRho(posYm);
    float rhoZp = getRho(posZ), rhoZm = getRho(posZm);

    float dRhoDx = (rhoXp - rhoXm) / (2.0f*eps);
    float dRhoDy = (rhoYp - rhoYm) / (2.0f*eps);
    float dRhoDz = (rhoZp - rhoZm) / (2.0f*eps);

    data.gradRho = glm::vec3(dRhoDx, dRhoDy, dRhoDz);
    data.gradRhoMag = glm::length(data.gradRho);
    data.density = rho0;

    // Лапласиан для shadowgraph ∇²ρ
    float lapl = (rhoXp + rhoXm + rhoYp + rhoYm + rhoZp + rhoZm - 6.0f*rho0) / (eps*eps);
    data.laplRho = lapl;

    if (!std::isfinite(data.gradRhoMag)) data.gradRhoMag = 0;
    if (!std::isfinite(data.laplRho)) data.laplRho = 0;
    return data;
}

float computeDensityGradientMagnitude(const glm::vec3& pos) {
    return computeSchlierenAt(pos).gradRhoMag;
}

glm::vec3 getSchlierenColor(float gradMag, float sensitivity, bool useColor) {
    // Нормализуем: типичный |∇ρ| ~ 0.1-10 kg/m⁴
    float t = gradMag * sensitivity * 5.0f;
    t = glm::clamp(t, 0.0f, 1.0f);
    // Шлирен — обычно черно-белый с ножом, но цветной красивее
    if (!useColor) {
        // Grayscale: 0.5 + k*grad, нож вертикальный — зависит от dρ/dy
        float gray = 0.5f + (t-0.5f)*0.8f;
        return glm::vec3(gray);
    } else {
        // Цветной шлирен: радужный по величине градиента
        if (t < 0.25f) {
            float k = t/0.25f;
            return glm::vec3(0.0f, k*0.5f, 0.5f+0.5f*k);
        } else if (t < 0.5f) {
            float k = (t-0.25f)/0.25f;
            return glm::vec3(0.0f, 0.5f+0.5f*k, 1.0f-k*0.3f);
        } else if (t < 0.75f) {
            float k = (t-0.5f)/0.25f;
            return glm::vec3(k*0.8f, 1.0f, 0.7f-0.7f*k);
        } else {
            float k = (t-0.75f)/0.25f;
            return glm::vec3(0.8f+0.2f*k, 1.0f-0.5f*k, 0.0f);
        }
    }
}

// =====================================================
// Oblique Shock — θ-β-M relation
// tanθ = 2 cotβ (M1² sin²β -1) / (M1²(γ+cos2β)+2)
// Решаем численно для β по M и θ
// =====================================================
float prandtlMeyerFunction(float mach) {
    if (mach < 1.0f) return 0.0f;
    const float gamma = 1.4f;
    float term1 = sqrtf((gamma+1.0f)/(gamma-1.0f));
    float term2 = sqrtf((gamma-1.0f)/(gamma+1.0f)*(mach*mach-1.0f));
    float term3 = sqrtf(mach*mach-1.0f);
    float nu = term1 * atanf(term2) - atanf(term3);
    return nu; // радианы
}

float solveObliqueShockAngle(float mach, float deflectionDeg) {
    if (mach < 1.01f) return 0.0f;
    if (fabsf(deflectionDeg) < 0.01f) return asinf(1.0f/mach) * 57.29578f; // Mach angle

    const float gamma = 1.4f;
    float theta = glm::radians(deflectionDeg);
    float M1 = mach;

    // Диапазон β: от μ = arcsin(1/M) до 90°
    float mu = asinf(1.0f/M1);
    float betaLow = mu + 0.001f;
    float betaHigh = glm::radians(89.0f);

    // Функция f(β) = tanθ - 2cotβ(M²sin²β-1)/(M²(γ+cos2β)+2)
    auto f = [&](float beta)->float {
        float sinB = sinf(beta), cosB = cosf(beta);
        float sin2B = sinB*sinB;
        float M1n2 = M1*M1*sin2B;
        if (M1n2 <= 1.0f) return -1.0f; // нет решения, дозвуковой нормальный
        float tanTheta = 2.0f * (cosB/sinB) * (M1n2 - 1.0f) / (M1*M1*(gamma + cosf(2.0f*beta)) + 2.0f);
        return tanTheta - tanf(theta);
    };

    // Бисекция для слабого скачка (обычно используется)
    float fLow = f(betaLow);
    float fHigh = f(betaHigh);
    // Ищем первый корень от mu вверх
    float bestBeta = mu;
    bool found = false;
    // Сканируем для поиска смены знака
    int steps = 100;
    float prevBeta = betaLow;
    float prevF = fLow;
    for (int i=1; i<=steps; ++i) {
        float b = betaLow + (betaHigh-betaLow)*i/steps;
        float fv = f(b);
        if (prevF * fv < 0) {
            // Бисекция между prevBeta и b
            float lo = prevBeta, hi = b;
            for (int iter=0; iter<30; ++iter) {
                float mid = (lo+hi)*0.5f;
                float fm = f(mid);
                if (fabsf(fm) < 1e-5f) { bestBeta = mid; found = true; break; }
                float flo = f(lo);
                if (flo * fm < 0) hi = mid;
                else lo = mid;
            }
            if (!found) bestBeta = (lo+hi)*0.5f;
            found = true;
            break; // слабый скачок
        }
        prevBeta = b;
        prevF = fv;
    }

    if (!found) return mu * 57.29578f; // если не нашли, возвращаем угол Маха
    return bestBeta * 57.29578f;
}

ShockWaveData computeObliqueShock(float machUpstream, float deflectionDeg, const glm::vec3& pos, const glm::vec3& flowDir) {
    ShockWaveData data{};
    data.hasShock = false;
    if (machUpstream < 1.05f) return data;

    const float gamma = 1.4f;
    float betaDeg = solveObliqueShockAngle(machUpstream, deflectionDeg);
    float beta = glm::radians(betaDeg);
    float theta = glm::radians(deflectionDeg);

    float sinBeta = sinf(beta);
    float M1n = machUpstream * sinBeta;
    if (M1n < 1.0f) return data;

    // Нормальный скачок соотношения
    float M1n2 = M1n*M1n;
    float pRatio = 1.0f + 2.0f*gamma/(gamma+1.0f)*(M1n2-1.0f);
    float rhoRatio = ((gamma+1.0f)*M1n2) / ((gamma-1.0f)*M1n2 + 2.0f);
    float tRatio = pRatio / rhoRatio;
    float M2n2 = (M1n2 + 2.0f/(gamma-1.0f)) / (2.0f*gamma/(gamma-1.0f)*M1n2 - 1.0f);
    float M2n = sqrtf(M2n2);
    float M2 = M2n / sinf(beta - theta);

    data.hasShock = true;
    data.shockAngle = betaDeg;
    data.deflectionAngle = deflectionDeg;
    data.pressureRatio = pRatio;
    data.densityRatio = rhoRatio;
    data.tempRatio = tRatio;
    data.machNormal1 = M1n;
    data.machNormal2 = M2n;
    data.mach2 = M2;
    data.shockOrigin = pos;
    // Нормаль к скачку: повернута от flowDir на β
    glm::vec3 up(0,1,0);
    if (fabsf(glm::dot(flowDir, up)) > 0.9f) up = glm::vec3(0,0,1);
    glm::vec3 perp = glm::normalize(glm::cross(flowDir, up));
    data.shockNormal = glm::normalize(flowDir * sinf(beta) + perp * cosf(beta));

    return data;
}

float computeMachAngle(float mach) {
    if (mach <= 1.0f) return 90.0f;
    return asinf(1.0f/mach) * 57.29578f;
}

glm::vec3 getShockColor(float pressureRatio) {
    float t = glm::clamp((pressureRatio-1.0f)/4.0f, 0.0f, 1.0f);
    if (t < 0.5f) {
        float k = t*2.0f;
        return glm::vec3(0.2f+k*0.8f, 0.4f+k*0.3f, 1.0f-k*0.5f);
    } else {
        float k = (t-0.5f)*2.0f;
        return glm::vec3(1.0f, 0.7f-k*0.5f, 0.5f-k*0.5f);
    }
}

ExpansionFanData computePrandtlMeyerFan(float mach1, float deflectionDeg) {
    ExpansionFanData data{};
    data.hasFan = false;
    if (mach1 < 1.01f) return data;
    if (deflectionDeg <= 0) return data; // расширение только при повороте от потока

    float nu1 = prandtlMeyerFunction(mach1);
    float nu2 = nu1 + glm::radians(deflectionDeg);
    // Максимальный ν_max = π/2*(√((γ+1)/(γ-1))-1) ≈ 130.45° для γ=1.4
    const float nuMax = 2.27685f; // 130.45° в рад
    if (nu2 > nuMax) nu2 = nuMax;

    // Обратная функция ν->M численно
    float mach2 = mach1;
    // Бисекция по M
    float lo = mach1, hi = 10.0f;
    for (int i=0; i<40; ++i) {
        float mid = (lo+hi)*0.5f;
        float nuMid = prandtlMeyerFunction(mid);
        if (nuMid < nu2) lo = mid;
        else hi = mid;
        mach2 = mid;
    }

    // Изоэнтропические соотношения
    const float gamma = 1.4f;
    float term1 = 1.0f + (gamma-1.0f)/2.0f*mach1*mach1;
    float term2 = 1.0f + (gamma-1.0f)/2.0f*mach2*mach2;
    float pRatio = powf(term1/term2, gamma/(gamma-1.0f));

    data.hasFan = true;
    data.nu1 = nu1 * 57.29578f;
    data.nu2 = nu2 * 57.29578f;
    data.expansionAngle = deflectionDeg;
    data.pressureRatio = pRatio;
    data.mach2 = mach2;
    return data;
}

// =====================================================
// Volumetric Smoke Tunnel — эйлерова сетка + лагранжевы частицы
// =====================================================
void initSmokeTunnel() {
    g_smokeMinX = minBB.x - maxDim*0.5f;
    g_smokeMaxX = maxBB.x + maxDim*3.0f;
    g_smokeMinY = minBB.y - maxDim*0.3f;
    g_smokeMaxY = maxBB.y + maxDim*1.0f;
    g_smokeMinZ = minBB.z - maxDim*0.8f;
    g_smokeMaxZ = maxBB.z + maxDim*0.8f;

    g_smokeNx = 48; g_smokeNy = 32; g_smokeNz = 32;
    g_smokeDensityField.assign(g_smokeNx*g_smokeNy*g_smokeNz, 0.0f);
    g_smokeParticles.clear();
    g_smokeParticles.reserve(20000);

    // Инжекторы — перед моделью
    std::uniform_real_distribution<float> distY(g_smokeMinY, g_smokeMaxY);
    std::uniform_real_distribution<float> distZ(g_smokeMinZ, g_smokeMaxZ);
    glm::vec3 flowDir = glm::vec3(flowParams.vx, flowParams.vy, flowParams.vz);
    float fl = glm::length(flowDir);
    if (fl < 1e-6f) flowDir = glm::vec3(1,0,0); else flowDir /= fl;
    glm::vec3 injectorBase = center - flowDir * maxDim * 1.2f;

    for (int inj=0; inj<aeroSmokeInjectors; ++inj) {
        for (int i=0; i<200; ++i) {
            SmokeParticle p;
            float yOff = (inj - aeroSmokeInjectors/2) * 0.15f * maxDim + (distY(rng)-center.y)*0.1f;
            float zOff = (rng()%100/100.0f - 0.5f) * maxDim*0.3f;
            p.pos = injectorBase + glm::vec3(0, yOff, zOff);
            p.vel = flowDir * flowSpeed;
            p.density = 1.0f;
            p.temperature = airTemperature;
            p.age = 0;
            p.lifetime = 8.0f + rng()%100/100.0f*4.0f;
            g_smokeParticles.push_back(p);
        }
    }
    std::cout << "[Interesting] Smoke tunnel init " << g_smokeNx << "x" << g_smokeNy << "x" << g_smokeNz
              << " particles=" << g_smokeParticles.size() << std::endl;
}

void updateSmokeTunnel(float dt) {
    if (g_smokeParticles.empty()) initSmokeTunnel();
    auto t0 = std::chrono::high_resolution_clock::now();

    float diss = aeroSmokeDissipation;
    if (diss < 0.8f) diss = 0.8f; if (diss > 0.999f) diss = 0.999f;
    float turb = aeroSmokeTurbulence;

    // Обновление частиц
    #ifdef _OPENMP
    #pragma omp parallel for
    #endif
    for (int i=0; i<(int)g_smokeParticles.size(); ++i) {
        auto& p = g_smokeParticles[i];
        glm::vec3 vFluid = computeVelocityFieldCPU(p.pos, flowParams);
        // Добавляем турбулентность
        if (turb > 0.001f) {
            float t = flowParams.time + p.age*0.5f;
            float nx = sinf(p.pos.y*2.3f + t*1.1f) * cosf(p.pos.z*1.7f + t*0.9f);
            float ny = sinf(p.pos.z*2.1f + t*1.2f) * cosf(p.pos.x*1.9f + t*0.8f);
            float nz = sinf(p.pos.x*2.0f + t*1.0f) * cosf(p.pos.y*1.8f + t*1.1f);
            vFluid += glm::vec3(nx,ny,nz) * turb * flowSpeed * 0.3f;
        }
        // Плавучесть
        if (aeroSmokeBuoyancy > 0.001f) {
            vFluid.y += aeroSmokeBuoyancy * (p.temperature - airTemperature) / airTemperature;
        }

        p.vel = glm::mix(p.vel, vFluid, 0.15f); // инерция
        p.pos += p.vel * dt;
        p.age += dt;
        p.density *= diss;

        // Респавн если вышел или умер
        if (p.age > p.lifetime || p.pos.x > g_smokeMaxX || p.density < 0.01f ||
            p.pos.y < g_smokeMinY || p.pos.y > g_smokeMaxY ||
            p.pos.z < g_smokeMinZ || p.pos.z > g_smokeMaxZ) {
            glm::vec3 flowDir = glm::vec3(flowParams.vx, flowParams.vy, flowParams.vz);
            float fl = glm::length(flowDir);
            if (fl < 1e-6f) flowDir = glm::vec3(1,0,0); else flowDir /= fl;
            glm::vec3 injectorBase = center - flowDir * maxDim * 1.2f;
            float yOff = (i % aeroSmokeInjectors - aeroSmokeInjectors/2) * 0.15f * maxDim;
            float zOff = (rng()%100/100.0f - 0.5f) * maxDim*0.3f;
            p.pos = injectorBase + glm::vec3(0, yOff, zOff) + glm::vec3((rng()%100/100.0f-0.5f)*0.1f,0,0);
            p.vel = flowDir * flowSpeed;
            p.density = 0.8f + rng()%100/100.0f*0.4f;
            p.age = 0;
        }
    }

    // Обновление эйлеровой сетки плотности (для volumetric ray marching)
    std::fill(g_smokeDensityField.begin(), g_smokeDensityField.end(), 0.0f);
    float csx = (g_smokeMaxX - g_smokeMinX)/g_smokeNx;
    float csy = (g_smokeMaxY - g_smokeMinY)/g_smokeNy;
    float csz = (g_smokeMaxZ - g_smokeMinZ)/g_smokeNz;
    for (auto& p : g_smokeParticles) {
        int ix = (int)((p.pos.x - g_smokeMinX)/csx);
        int iy = (int)((p.pos.y - g_smokeMinY)/csy);
        int iz = (int)((p.pos.z - g_smokeMinZ)/csz);
        if (ix<0||ix>=g_smokeNx||iy<0||iy>=g_smokeNy||iz<0||iz>=g_smokeNz) continue;
        int idx = (iz*g_smokeNy+iy)*g_smokeNx+ix;
        g_smokeDensityField[idx] += p.density * 0.1f;
    }
    // Сглаживание
    for (int iter=0; iter<2; ++iter) {
        std::vector<float> tmp = g_smokeDensityField;
        #ifdef _OPENMP
        #pragma omp parallel for
        #endif
        for (int z=1; z<g_smokeNz-1; ++z)
            for (int y=1; y<g_smokeNy-1; ++y)
                for (int x=1; x<g_smokeNx-1; ++x) {
                    int idx = (z*g_smokeNy+y)*g_smokeNx+x;
                    float sum = tmp[idx]*0.5f;
                    sum += (tmp[idx-1]+tmp[idx+1]+tmp[idx-g_smokeNx]+tmp[idx+g_smokeNx]+tmp[idx-g_smokeNx*g_smokeNy]+tmp[idx+g_smokeNx*g_smokeNy])*0.5f/6.0f;
                    g_smokeDensityField[idx] = sum;
                }
    }

    auto t1 = std::chrono::high_resolution_clock::now();
    perfVolumetricMs = std::chrono::duration<float, std::milli>(t1-t0).count();
}

void shutdownSmokeTunnel() {
    g_smokeParticles.clear();
    g_smokeDensityField.clear();
}

glm::vec3 sampleSmokeDensity(const glm::vec3& pos) {
    if (g_smokeDensityField.empty()) return glm::vec3(0);
    float csx = (g_smokeMaxX - g_smokeMinX)/g_smokeNx;
    float csy = (g_smokeMaxY - g_smokeMinY)/g_smokeNy;
    float csz = (g_smokeMaxZ - g_smokeMinZ)/g_smokeNz;
    float fx = (pos.x - g_smokeMinX)/csx - 0.5f;
    float fy = (pos.y - g_smokeMinY)/csy - 0.5f;
    float fz = (pos.z - g_smokeMinZ)/csz - 0.5f;
    int ix = (int)fx, iy = (int)fy, iz = (int)fz;
    if (ix<0||ix>=g_smokeNx-1||iy<0||iy>=g_smokeNy-1||iz<0||iz>=g_smokeNz-1) return glm::vec3(0);
    float tx = fx-ix, ty = fy-iy, tz = fz-iz;
    int idx000 = (iz*g_smokeNy+iy)*g_smokeNx+ix;
    int idx100 = idx000+1, idx010 = idx000+g_smokeNx, idx110 = idx010+1;
    int idx001 = idx000+g_smokeNx*g_smokeNy, idx101 = idx001+1, idx011 = idx001+g_smokeNx, idx111 = idx011+1;
    float c00 = g_smokeDensityField[idx000]*(1-tx)+g_smokeDensityField[idx100]*tx;
    float c01 = g_smokeDensityField[idx001]*(1-tx)+g_smokeDensityField[idx101]*tx;
    float c10 = g_smokeDensityField[idx010]*(1-tx)+g_smokeDensityField[idx110]*tx;
    float c11 = g_smokeDensityField[idx011]*(1-tx)+g_smokeDensityField[idx111]*tx;
    float c0 = c00*(1-ty)+c10*ty;
    float c1 = c01*(1-ty)+c11*ty;
    float dens = c0*(1-tz)+c1*tz;
    return glm::vec3(dens);
}

float computeVolumetricTransmittance(const glm::vec3& start, const glm::vec3& end, int steps) {
    glm::vec3 dir = end - start;
    float len = glm::length(dir);
    if (len < 1e-6f) return 1.0f;
    dir /= len;
    float stepLen = len/steps;
    float tau = 0.0f;
    for (int i=0; i<steps; ++i) {
        glm::vec3 p = start + dir * (i+0.5f)*stepLen;
        float d = sampleSmokeDensity(p).x;
        tau += d * stepLen * aeroSmokeOpacity * 2.0f;
    }
    return expf(-tau);
}

// =====================================================
// Vortex Tubes — извлечение вихревых ядер по Q-критерию и λ2
// =====================================================
void extractVortexCores() {
    auto t0 = std::chrono::high_resolution_clock::now();
    g_vortexCores.clear();

    // Сетка для поиска — вокруг модели
    int res = 24;
    float minX = center.x - maxDim*0.8f, maxX = center.x + maxDim*2.5f;
    float minY = center.y - maxDim*0.8f, maxY = center.y + maxDim*0.8f;
    float minZ = center.z - maxDim*0.8f, maxZ = center.z + maxDim*0.8f;

    std::vector<glm::vec3> candidates;

    #ifdef _OPENMP
    #pragma omp parallel
    {
        std::vector<glm::vec3> local;
        #pragma omp for nowait
        for (int k=0; k<res; ++k)
            for (int j=0; j<res; ++j)
                for (int i=0; i<res; ++i) {
                    float x = minX + (maxX-minX)*i/(res-1);
                    float y = minY + (maxY-minY)*j/(res-1);
                    float z = minZ + (maxZ-minZ)*k/(res-1);
                    glm::vec3 pos(x,y,z);
                    if (sampleSDFCPU(pos) < 0.05f) continue; // внутри
                    // Вычисляем Q и вихрь
                    float eps = maxDim*0.02f;
                    glm::vec3 vx = computeVelocityFieldCPU(pos + glm::vec3(eps,0,0) , flowParams) - computeVelocityFieldCPU(pos - glm::vec3(eps,0,0), flowParams);
                    glm::vec3 vy = computeVelocityFieldCPU(pos + glm::vec3(0,eps,0) , flowParams) - computeVelocityFieldCPU(pos - glm::vec3(0,eps,0), flowParams);
                    glm::vec3 vz = computeVelocityFieldCPU(pos + glm::vec3(0,0,eps) , flowParams) - computeVelocityFieldCPU(pos - glm::vec3(0,0,eps), flowParams);
                    vx /= (2*eps); vy /= (2*eps); vz /= (2*eps);
                    // Градиент скорости
                    float dudx = vx.x, dudy = vy.x, dudz = vz.x;
                    float dvdx = vx.y, dvdy = vy.y, dvdz = vz.y;
                    float dwdx = vx.z, dwdy = vy.z, dwdz = vz.z;

                    float S2 = dudx*dudx + dvdy*dvdy + dwdz*dwdz + 0.5f*((dudy+dvdx)*(dudy+dvdx)+(dudz+dwdx)*(dudz+dwdx)+(dvdz+dwdy)*(dvdz+dwdy));
                    float vortx = dwdy - dvdz, vorty = dudz - dwdx, vortz = dvdx - dudy;
                    float vort2 = vortx*vortx + vorty*vorty + vortz*vortz;
                    float Q = 0.5f*(0.5f*vort2 - S2);

                    if (Q > 5.0f && vort2 > 10.0f) {
                        local.push_back(pos);
                    }
                }
        #pragma omp critical
        candidates.insert(candidates.end(), local.begin(), local.end());
    }
    #else
    for (int k=0; k<res; ++k)
        for (int j=0; j<res; ++j)
            for (int i=0; i<res; ++i) {
                float x = minX + (maxX-minX)*i/(res-1);
                float y = minY + (maxY-minY)*j/(res-1);
                float z = minZ + (maxZ-minZ)*k/(res-1);
                glm::vec3 pos(x,y,z);
                if (sampleSDFCPU(pos) < 0.05f) continue;
                float eps = maxDim*0.02f;
                glm::vec3 vx = computeVelocityFieldCPU(pos + glm::vec3(eps,0,0) , flowParams) - computeVelocityFieldCPU(pos - glm::vec3(eps,0,0), flowParams);
                glm::vec3 vy = computeVelocityFieldCPU(pos + glm::vec3(0,eps,0) , flowParams) - computeVelocityFieldCPU(pos - glm::vec3(0,eps,0), flowParams);
                glm::vec3 vz = computeVelocityFieldCPU(pos + glm::vec3(0,0,eps) , flowParams) - computeVelocityFieldCPU(pos - glm::vec3(0,0,eps), flowParams);
                vx /= (2*eps); vy /= (2*eps); vz /= (2*eps);
                float dudx = vx.x, dudy = vy.x, dudz = vz.x;
                float dvdx = vx.y, dvdy = vy.y, dvdz = vz.y;
                float dwdx = vx.z, dwdy = vy.z, dwdz = vz.z;
                float S2 = dudx*dudx + dvdy*dvdy + dwdz*dwdz + 0.5f*((dudy+dvdx)*(dudy+dvdx)+(dudz+dwdx)*(dudz+dwdx)+(dvdz+dwdy)*(dvdz+dwdy));
                float vortx = dwdy - dvdz, vorty = dudz - dwdx, vortz = dvdx - dudy;
                float vort2 = vortx*vortx + vorty*vorty + vortz*vortz;
                float Q = 0.5f*(0.5f*vort2 - S2);
                if (Q > 5.0f && vort2 > 10.0f) candidates.push_back(pos);
            }
    #endif

    // Кластеризация кандидатов в ядра
    int maxCores = aeroVortexTubeCount;
    if ((int)candidates.size() > maxCores*10) {
        // Выбираем случайные
        std::shuffle(candidates.begin(), candidates.end(), rng);
        candidates.resize(maxCores*10);
    }

    // Простая кластеризация по расстоянию
    std::vector<bool> used(candidates.size(), false);
    for (size_t i=0; i<candidates.size() && (int)g_vortexCores.size()<maxCores; ++i) {
        if (used[i]) continue;
        glm::vec3 cluster = candidates[i];
        int count = 1;
        for (size_t j=i+1; j<candidates.size(); ++j) {
            if (used[j]) continue;
            if (glm::length(candidates[j]-cluster) < maxDim*0.15f) {
                cluster = (cluster*float(count) + candidates[j])/(float)(count+1);
                count++; used[j]=true;
            }
        }
        used[i]=true;

        // Вычисляем свойства ядра
        glm::vec3 v = computeVelocityFieldCPU(cluster, flowParams);
        float eps = maxDim*0.02f;
        glm::vec3 vx = computeVelocityFieldCPU(cluster + glm::vec3(eps,0,0) , flowParams) - computeVelocityFieldCPU(cluster - glm::vec3(eps,0,0), flowParams);
        glm::vec3 vy = computeVelocityFieldCPU(cluster + glm::vec3(0,eps,0) , flowParams) - computeVelocityFieldCPU(cluster - glm::vec3(0,eps,0), flowParams);
        glm::vec3 vz = computeVelocityFieldCPU(cluster + glm::vec3(0,0,eps) , flowParams) - computeVelocityFieldCPU(cluster - glm::vec3(0,0,eps), flowParams);
        vx /= (2*eps); vy /= (2*eps); vz /= (2*eps);
        float dwdy = vy.z, dvdz = vz.y, dudz = vz.x, dwdx = vx.z, dvdx = vx.y, dudy = vy.x;
        glm::vec3 vort(dwdy - dvdz, dudz - dwdx, dvdx - dudy);
        float vortMag = glm::length(vort);
        float helicity = glm::dot(v, vort);
        float dudx = vx.x, dvdy = vy.y, dwdz = vz.z;
        float S2 = dudx*dudx + dvdy*dvdy + dwdz*dwdz + 0.5f*((dudy+dvdx)*(dudy+dvdx)+(dudz+dwdx)*(dudz+dwdx)+(dvdz+dwdy)*(dvdz+dwdy));
        float Q = 0.5f*(0.5f*vortMag*vortMag - S2);

        VortexCore core;
        core.position = cluster;
        core.direction = vortMag>1e-6f ? vort/vortMag : glm::vec3(1,0,0);
        core.strength = vortMag;
        core.helicity = helicity;
        core.qCrit = Q;
        core.radius = aeroVortexTubeRadius * (1.0f + vortMag*0.02f);
        core.linePoints = traceVortexLine(cluster, 80, maxDim*0.04f);
        if (core.linePoints.size() > 5) g_vortexCores.push_back(core);
    }

    auto t1 = std::chrono::high_resolution_clock::now();
    perfVortexMs = std::chrono::duration<float, std::milli>(t1-t0).count();
}

std::vector<glm::vec3> traceVortexLine(const glm::vec3& seed, int maxSteps, float stepSize) {
    std::vector<glm::vec3> line;
    line.reserve(maxSteps);
    glm::vec3 pos = seed;
    line.push_back(pos);
    for (int i=0; i<maxSteps; ++i) {
        if (sampleSDFCPU(pos) < 0.02f) break;
        float eps = maxDim*0.02f;
        glm::vec3 vx = computeVelocityFieldCPU(pos + glm::vec3(eps,0,0) , flowParams) - computeVelocityFieldCPU(pos - glm::vec3(eps,0,0), flowParams);
        glm::vec3 vy = computeVelocityFieldCPU(pos + glm::vec3(0,eps,0) , flowParams) - computeVelocityFieldCPU(pos - glm::vec3(0,eps,0), flowParams);
        glm::vec3 vz = computeVelocityFieldCPU(pos + glm::vec3(0,0,eps) , flowParams) - computeVelocityFieldCPU(pos - glm::vec3(0,0,eps), flowParams);
        vx /= (2*eps); vy /= (2*eps); vz /= (2*eps);
        float dwdy = vy.z, dvdz = vz.y, dudz = vz.x, dwdx = vx.z, dvdx = vx.y, dudy = vy.x;
        glm::vec3 vort(dwdy - dvdz, dudz - dwdx, dvdx - dudy);
        float vm = glm::length(vort);
        if (vm < 1e-6f) break;
        vort /= vm;
        // Идем вдоль вихря
        pos += vort * stepSize;
        // Проверка выхода
        if (glm::length(pos - center) > maxDim*4.0f) break;
        line.push_back(pos);
    }
    return line;
}

void updateVortexTubes(float dt) {
    // Обновляем радиус по helicity
    for (auto& core : g_vortexCores) {
        core.radius = aeroVortexTubeRadius * (1.0f + fabsf(core.helicity)*0.01f);
    }
}

glm::vec3 getVortexTubeColor(float helicity, float strength) {
    // Helicity coloring: правый вихрь — красный, левый — синий, сила — яркость
    float t = glm::clamp(helicity * 0.05f * aeroVortexHelicityScale, -1.0f, 1.0f);
    float s = glm::clamp(strength * 0.05f, 0.0f, 1.0f);
    if (t > 0) {
        return glm::vec3(0.8f+0.2f*s, 0.2f+0.3f*s*t, 0.2f);
    } else {
        return glm::vec3(0.2f, 0.3f+0.2f*s, 0.8f+0.2f*s);
    }
}

// =====================================================
// Flight Dynamics 6DOF — модель летит на основе сил
// =====================================================
void initFlightDynamics() {
    g_flightState.position = center + glm::vec3(0, maxDim*1.5f, 0);
    g_flightState.velocity = glm::vec3(flowSpeed, 0, 0);
    g_flightState.acceleration = glm::vec3(0);
    g_flightState.angles = glm::vec3(0);
    g_flightState.angVel = glm::vec3(0);
    g_flightState.angAccel = glm::vec3(0);
    g_flightState.mass = aeroFlightMass;
    if (g_flightState.mass < 0.1f) g_flightState.mass = 1.0f;
    g_flightState.inertia = glm::mat3(1.0f) * aeroFlightInertia;
    g_flightState.thrust = aeroFlightThrust;
    g_flightState.altitude = g_flightState.position.y;
    g_flightState.onGround = false;
    aeroFlightPos = g_flightState.position;
    aeroFlightVel = g_flightState.velocity;
    aeroFlightAngles = g_flightState.angles;
    std::cout << "[Interesting] Flight dynamics init mass=" << g_flightState.mass << " pos=" << g_flightState.position.x << "," << g_flightState.position.y << "," << g_flightState.position.z << std::endl;
}

void updateFlightDynamics(float dt, const glm::vec3& lift, const glm::vec3& drag, const glm::vec3& moment, const glm::vec3& cop) {
    if (!aeroFlightMode) return;
    if (dt > 0.1f) dt = 0.1f;

    // Силы: lift + drag + thrust + gravity + ground reaction
    glm::vec3 gravity(0, -9.81f * g_flightState.mass, 0);
    glm::vec3 thrustDir = glm::vec3(1,0,0); // вдоль тела, упрощенно
    // Поворачиваем thrust по yaw
    float yaw = g_flightState.angles.y;
    thrustDir = glm::vec3(cosf(yaw), 0, sinf(yaw));
    glm::vec3 thrust = thrustDir * g_flightState.thrust;

    glm::vec3 totalForce = lift + drag + thrust + gravity;

    // Ground collision
    float groundY = g_voxMinY + aeroGroundHeight;
    if (g_flightState.position.y < groundY + maxDim*0.2f) {
        g_flightState.onGround = true;
        if (g_flightState.velocity.y < 0) {
            totalForce.y += -g_flightState.velocity.y * g_flightState.mass * 20.0f; // упругая реакция
            g_flightState.velocity.y *= 0.3f; // демпфирование
        }
        // Трение
        g_flightState.velocity.x *= 0.98f;
        g_flightState.velocity.z *= 0.98f;
    } else {
        g_flightState.onGround = false;
    }

    // Линейная динамика F=ma
    g_flightState.acceleration = totalForce / g_flightState.mass;
    g_flightState.velocity += g_flightState.acceleration * dt;
    // Ограничение скорости
    float maxV = 50.0f;
    if (glm::length(g_flightState.velocity) > maxV) {
        g_flightState.velocity = glm::normalize(g_flightState.velocity) * maxV;
    }
    g_flightState.position += g_flightState.velocity * dt;

    // Угловая динамика M = I α
    // Момент относительно центра масс
    glm::vec3 rCop = cop - center;
    glm::vec3 momentFromForces = glm::cross(rCop, lift + drag);
    glm::vec3 totalMoment = moment + momentFromForces;

    // Автотриммирование — стабилизация
    if (aeroFlightAutoTrim) {
        totalMoment -= g_flightState.angVel * 2.0f; // демпфирование
        totalMoment.x -= g_flightState.angles.x * 5.0f; // pitch стабилизация
        totalMoment.z -= g_flightState.angles.z * 5.0f; // roll стабилизация
    }

    g_flightState.angAccel = totalMoment / aeroFlightInertia;
    g_flightState.angVel += g_flightState.angAccel * dt;
    g_flightState.angVel *= 0.98f; // демпфирование
    g_flightState.angles += g_flightState.angVel * dt;

    // Ограничение углов
    g_flightState.angles.x = glm::clamp(g_flightState.angles.x, glm::radians(-30.0f), glm::radians(30.0f));
    g_flightState.angles.z = glm::clamp(g_flightState.angles.z, glm::radians(-45.0f), glm::radians(45.0f));

    g_flightState.altitude = g_flightState.position.y - groundY;

    // Обновляем глобальные для UI
    aeroFlightPos = g_flightState.position;
    aeroFlightVel = g_flightState.velocity;
    aeroFlightAngles = g_flightState.angles;
    aeroFlightVelocity = glm::length(g_flightState.velocity);
    aeroFlightAltitude = g_flightState.altitude;
}

void resetFlightDynamics() {
    initFlightDynamics();
}

glm::mat4 getFlightModelMatrix() {
    if (!aeroFlightMode) return glm::mat4(1.0f);
    glm::mat4 trans = glm::translate(glm::mat4(1.0f), g_flightState.position - center);
    glm::mat4 rotY = glm::rotate(glm::mat4(1.0f), g_flightState.angles.y, glm::vec3(0,1,0));
    glm::mat4 rotX = glm::rotate(glm::mat4(1.0f), g_flightState.angles.x, glm::vec3(1,0,0));
    glm::mat4 rotZ = glm::rotate(glm::mat4(1.0f), g_flightState.angles.z, glm::vec3(0,0,1));
    return trans * rotY * rotX * rotZ;
}

bool isFlightFlying() {
    return aeroFlightMode && !g_flightState.onGround && g_flightState.altitude > maxDim*0.2f;
}

// =====================================================
// LIC — Line Integral Convolution для поверхности
// =====================================================
void initLIC(int w, int h) {
    g_licData.width = w;
    g_licData.height = h;
    g_licData.noiseTex.resize(w*h);
    g_licData.licResult.resize(w*h);
    std::uniform_real_distribution<float> dist(0,1);
    for (int i=0; i<w*h; ++i) g_licData.noiseTex[i] = dist(rng);
    std::cout << "[Interesting] LIC init " << w << "x" << h << std::endl;
}

void computeLICOnSurface() {
    if (g_licData.noiseTex.empty()) return;
    int w = g_licData.width, h = g_licData.height;
    int steps = aeroLICSteps;
    float strength = aeroLICStrength;

    #ifdef _OPENMP
    #pragma omp parallel for
    #endif
    for (int y=0; y<h; ++y) {
        for (int x=0; x<w; ++x) {
            // Позиция на поверхности — упрощенно, используем UV как мировые координаты
            float u = float(x)/w, v = float(y)/h;
            glm::vec3 pos = center + glm::vec3((u-0.5f)*maxDim*2.0f, (v-0.5f)*maxDim, 0);
            // Skin friction direction — касательная компонента скорости у поверхности
            glm::vec3 vel = computeVelocityFieldCPU(pos, flowParams);
            glm::vec3 normal = glm::vec3(0,0,1); // упрощенно
            glm::vec3 tang = vel - normal * glm::dot(vel, normal);
            float len = glm::length(tang);
            if (len < 1e-6f) {
                g_licData.licResult[y*w+x] = g_licData.noiseTex[y*w+x];
                continue;
            }
            tang /= len;

            float acc = 0.0f;
            float weightSum = 0.0f;
            glm::vec3 pFwd = pos, pBwd = pos;
            for (int s=0; s<steps; ++s) {
                float wgt = expf(-float(s*s)/(steps*steps*0.5f));
                // Вперед
                pFwd += tang * maxDim*0.02f;
                glm::vec3 vf = computeVelocityFieldCPU(pFwd, flowParams);
                glm::vec3 tf = vf - normal * glm::dot(vf, normal);
                float lf = glm::length(tf);
                if (lf > 1e-6f) tang = tf/lf;

                int ix = int((pFwd.x - (center.x-maxDim))/ (2*maxDim) * w);
                int iy = int((pFwd.y - (center.y-maxDim*0.5f))/maxDim * h);
                ix = std::max(0, std::min(w-1, ix));
                iy = std::max(0, std::min(h-1, iy));
                acc += g_licData.noiseTex[iy*w+ix] * wgt;
                weightSum += wgt;

                // Назад
                pBwd -= tang * maxDim*0.02f;
                glm::vec3 vb = computeVelocityFieldCPU(pBwd, flowParams);
                glm::vec3 tb = vb - normal * glm::dot(vb, normal);
                float lb = glm::length(tb);
                if (lb > 1e-6f) {
                    // для backward идем против
                }
                int ixb = int((pBwd.x - (center.x-maxDim))/ (2*maxDim) * w);
                int iyb = int((pBwd.y - (center.y-maxDim*0.5f))/maxDim * h);
                ixb = std::max(0, std::min(w-1, ixb));
                iyb = std::max(0, std::min(h-1, iyb));
                acc += g_licData.noiseTex[iyb*w+ixb] * wgt;
                weightSum += wgt;
            }
            if (weightSum > 0) acc /= weightSum;
            else acc = g_licData.noiseTex[y*w+x];
            g_licData.licResult[y*w+x] = acc * strength + g_licData.noiseTex[y*w+x]*(1.0f-strength);
        }
    }
}

glm::vec3 getLICColor(float licValue, const glm::vec3& baseColor) {
    float t = glm::clamp(licValue, 0.0f, 1.0f);
    return baseColor * (0.3f + 0.7f*t);
}

// =====================================================
// Aeroacoustics — Lighthill analogy
// T_ij = ρ v_i v_j + (p - c²ρ)δ_ij - τ_ij
// Источник шума ∝ ∂²T_ij/∂x_i∂x_j
// =====================================================
void computeAeroacousticSources() {
    g_acousticSources.clear();
    int res = 20;
    float minX = center.x - maxDim*0.5f, maxX = center.x + maxDim*2.0f;
    float minY = center.y - maxDim*0.5f, maxY = center.y + maxDim*0.5f;
    float minZ = center.z - maxDim*0.5f, maxZ = center.z + maxDim*0.5f;

    #ifdef _OPENMP
    #pragma omp parallel
    {
        std::vector<AcousticSource> local;
        #pragma omp for nowait
        for (int k=0; k<res; ++k)
            for (int j=0; j<res; ++j)
                for (int i=0; i<res; ++i) {
                    float x = minX + (maxX-minX)*i/(res-1);
                    float y = minY + (maxY-minY)*j/(res-1);
                    float z = minZ + (maxZ-minZ)*k/(res-1);
                    glm::vec3 pos(x,y,z);
                    if (sampleSDFCPU(pos) < 0.1f) continue;

                    glm::vec3 v = computeVelocityFieldCPU(pos, flowParams);
                    float vMag = glm::length(v);
                    float eps = maxDim*0.02f;
                    glm::vec3 vx = computeVelocityFieldCPU(pos + glm::vec3(eps,0,0) , flowParams) - computeVelocityFieldCPU(pos - glm::vec3(eps,0,0), flowParams);
                    glm::vec3 vy = computeVelocityFieldCPU(pos + glm::vec3(0,eps,0) , flowParams) - computeVelocityFieldCPU(pos - glm::vec3(0,eps,0), flowParams);
                    glm::vec3 vz = computeVelocityFieldCPU(pos + glm::vec3(0,0,eps) , flowParams) - computeVelocityFieldCPU(pos - glm::vec3(0,0,eps), flowParams);
                    vx /= (2*eps); vy /= (2*eps); vz /= (2*eps);

                    // Тензор Лайтхилла приближенно: T ~ ρ v⊗v
                    float rho = airDensity;
                    float Txx = rho * v.x * v.x, Tyy = rho * v.y * v.y, Tzz = rho * v.z * v.z;
                    float Txy = rho * v.x * v.y, Txz = rho * v.x * v.z, Tyz = rho * v.y * v.z;
                    float TijMag = sqrtf(Txx*Txx+Tyy*Tyy+Tzz*Tzz+2*(Txy*Txy+Txz*Txz+Tyz*Tyz));

                    // Вторая производная — источник
                    float divT = vx.x + vy.y + vz.z; // упрощенно
                    float intensity = TijMag * fabsf(divT) * 0.01f;

                    if (intensity > 0.1f) {
                        AcousticSource src;
                        src.position = pos;
                        src.intensity = intensity;
                        src.frequency = vMag / (maxDim*0.1f + 0.01f); // Strouhal-like
                        src.direction = vMag>1e-6f ? v/vMag : glm::vec3(1,0,0);
                        local.push_back(src);
                    }
                }
        #pragma omp critical
        g_acousticSources.insert(g_acousticSources.end(), local.begin(), local.end());
    }
    #else
    for (int k=0; k<res; ++k)
        for (int j=0; j<res; ++j)
            for (int i=0; i<res; ++i) {
                float x = minX + (maxX-minX)*i/(res-1);
                float y = minY + (maxY-minY)*j/(res-1);
                float z = minZ + (maxZ-minZ)*k/(res-1);
                glm::vec3 pos(x,y,z);
                if (sampleSDFCPU(pos) < 0.1f) continue;
                glm::vec3 v = computeVelocityFieldCPU(pos, flowParams);
                float vMag = glm::length(v);
                float eps = maxDim*0.02f;
                glm::vec3 vx = computeVelocityFieldCPU(pos + glm::vec3(eps,0,0) , flowParams) - computeVelocityFieldCPU(pos - glm::vec3(eps,0,0), flowParams);
                vx /= (2*eps);
                float intensity = airDensity * vMag * vMag * glm::length(vx) * 0.01f;
                if (intensity > 0.1f) {
                    AcousticSource src; src.position=pos; src.intensity=intensity; src.frequency=vMag/(maxDim*0.1f+0.01f); src.direction=vMag>1e-6f?v/vMag:glm::vec3(1,0,0);
                    g_acousticSources.push_back(src);
                }
            }
    #endif

    // Сортируем по интенсивности
    std::sort(g_acousticSources.begin(), g_acousticSources.end(), [](const AcousticSource& a, const AcousticSource& b){ return a.intensity > b.intensity; });
    if (g_acousticSources.size() > 100) g_acousticSources.resize(100);
}

float computeAcousticIntensity(const glm::vec3& pos) {
    float total = 0.0f;
    for (auto& src : g_acousticSources) {
        float dist = glm::length(pos - src.position);
        if (dist < 1e-3f) dist = 1e-3f;
        // Интенсивность убывает как 1/r²
        total += src.intensity / (dist*dist + 0.01f);
    }
    return total;
}

glm::vec3 getAcousticColor(float intensity) {
    float t = glm::clamp(intensity * 0.1f, 0.0f, 1.0f);
    // От синего (тихо) к красному (громко)
    if (t < 0.5f) {
        float k = t*2.0f;
        return glm::vec3(k*0.5f, k*0.5f, 0.5f+0.5f*k);
    } else {
        float k = (t-0.5f)*2.0f;
        return glm::vec3(0.5f+0.5f*k, 0.5f-k*0.3f, 1.0f-k);
    }
}

// =====================================================
// Temperature — нагрев от сжатия
// T0/T = 1 + (γ-1)/2 M², T2/T1 через скачок
// =====================================================
float computeTemperatureAt(const glm::vec3& pos, float mach, float pressureRatio) {
    const float gamma = 1.4f;
    float T_inf = airTemperature;
    // Адиабатический нагрев торможения
    float T0 = T_inf * (1.0f + (gamma-1.0f)/2.0f * mach*mach);
    // Если есть скачок, дополнительный нагрев
    float T = T0;
    if (pressureRatio > 1.0f) {
        // Через скачок T2/T1 уже учтено в pressureRatio/rhoRatio, упрощенно
        T *= (1.0f + (pressureRatio-1.0f)*0.2f);
    }
    // Вблизи поверхности — нагрев от трения
    float sdf = sampleSDFCPU(pos);
    if (sdf < maxDim*0.2f && sdf > 0) {
        float frictionHeating = (1.0f - sdf/(maxDim*0.2f)) * mach*mach * 5.0f;
        T += frictionHeating;
    }
    return T;
}

glm::vec3 getTemperatureColor(float temp, float tempInf) {
    float delta = temp - tempInf;
    float t = glm::clamp(delta / 50.0f, 0.0f, 1.0f); // 0-50K нагрев
    if (t < 0.25f) {
        float k = t/0.25f;
        return glm::vec3(0.0f, 0.2f*k, 0.8f+0.2f*k);
    } else if (t < 0.5f) {
        float k = (t-0.25f)/0.25f;
        return glm::vec3(k*0.8f, 0.2f+0.5f*k, 1.0f-k*0.5f);
    } else if (t < 0.75f) {
        float k = (t-0.5f)/0.25f;
        return glm::vec3(0.8f+0.2f*k, 0.7f+0.2f*k, 0.5f-k*0.5f);
    } else {
        float k = (t-0.75f)/0.25f;
        return glm::vec3(1.0f, 0.9f-k*0.4f, k*0.3f);
    }
}

// =====================================================
// Streaklines — линии, выпущенные из одной точки в разное время
// =====================================================
void initStreaklines() {
    g_streaklines.clear();
    g_streaklines.resize(aeroSmokeInjectors);
}

void updateStreaklines(float dt) {
    if ((int)g_streaklines.size() != aeroSmokeInjectors) initStreaklines();

    glm::vec3 flowDir = glm::vec3(flowParams.vx, flowParams.vy, flowParams.vz);
    float fl = glm::length(flowDir);
    if (fl < 1e-6f) flowDir = glm::vec3(1,0,0); else flowDir /= fl;
    glm::vec3 base = center - flowDir * maxDim * 1.0f;

    // Добавляем новые точки
    for (int inj=0; inj<aeroSmokeInjectors; ++inj) {
        float yOff = (inj - aeroSmokeInjectors/2) * 0.2f * maxDim;
        glm::vec3 injectPos = base + glm::vec3(0, yOff, 0);
        StreakPoint pt;
        pt.pos = injectPos;
        pt.age = 0;
        pt.injectorId = inj;
        g_streaklines[inj].push_back(pt);
    }

    // Адвектирование
    #ifdef _OPENMP
    #pragma omp parallel for
    #endif
    for (int inj=0; inj<(int)g_streaklines.size(); ++inj) {
        for (auto& pt : g_streaklines[inj]) {
            glm::vec3 v = computeVelocityFieldCPU(pt.pos, flowParams);
            pt.pos += v * dt;
            pt.age += dt;
        }
        // Удаляем старые
        auto& line = g_streaklines[inj];
        line.erase(std::remove_if(line.begin(), line.end(), [](const StreakPoint& p){
            return p.age > 10.0f || glm::length(p.pos - center) > maxDim*5.0f;
        }), line.end());
        // Ограничиваем историю
        if ((int)line.size() > aeroStreakHistory) {
            line.erase(line.begin(), line.begin() + (line.size() - aeroStreakHistory));
        }
    }
}

void clearStreaklines() {
    for (auto& line : g_streaklines) line.clear();
}

// =====================================================
// Init/Shutdown all
// =====================================================
void initInterestingFeatures() {
    std::cout << "[Interesting] Init v1.18.0 — schlieren, smoke, shocks, vortex, flight, LIC, acoustic" << std::endl;
    initSmokeTunnel();
    initStreaklines();
    initFlightDynamics();
    initLIC(256, 256);
    extractVortexCores();
    computeAeroacousticSources();
}

void shutdownInterestingFeatures() {
    shutdownSmokeTunnel();
    g_vortexCores.clear();
    g_acousticSources.clear();
    g_streaklines.clear();
    g_licData.noiseTex.clear();
    g_licData.licResult.clear();
}

void updateInterestingFeatures(float dt) {
    auto t0 = std::chrono::high_resolution_clock::now();
    if (aeroVolumetricEnabled) updateSmokeTunnel(dt);
    if (aeroShowStreaklines) updateStreaklines(dt);
    if (aeroFlightMode) {
        // flight update needs forces — called from forces.cpp
    }
    // Периодически обновляем тяжелые вычисления
    static float accum = 0;
    accum += dt;
    if (accum > 1.0f) {
        accum = 0;
        if (aeroShowVortexTubes) extractVortexCores();
        if (aeroShowAeroAcoustic) computeAeroacousticSources();
    }
    auto t1 = std::chrono::high_resolution_clock::now();
    g_perfInterestingMs = std::chrono::duration<float, std::milli>(t1-t0).count();
}
