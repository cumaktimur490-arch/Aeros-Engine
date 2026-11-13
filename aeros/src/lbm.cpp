#include "lbm.h"
#include "thermal_lbm.h"   // v1.37.0 — теплоперенос и плавучесть
#include "version.h"   // v1.36.0 — в журнал пишется фактическая версия сборки, а не v1.11.0
#include "globals.h"
#include "debug_log.h"   // v1.25.0 — журнал физики
#include "flow_field.h"
#include "voxel_grid.h"
#include "forces.h"
#include "atmosphere.h"

#include <glm/glm.hpp>
#include <iostream>
#include <cmath>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <limits>

#ifdef _OPENMP
#include <omp.h>
#endif

// v1.32.0 — определение lbmParams перенесено в globals.cpp: все настройки,
// которые попадают в реестр (settings_store.h), живут в одном месте, и реестр
// линкуется без решателя LBM.
bool lbmInitialized = false;
bool lbmConverged = false;
int lbmCurrentStep = 0;
float lbmAvgRho = 1.0f;
float lbmAvgKineticEnergy = 0.0f;
float lbmMaxVelocityLB = 0.0f;
float lbmMaxVelocityWorld = 0.0f;
float lbmReynolds = 0.0f;
float lbmReynoldsLB = 0.0f;
float lbmBlockageRatio = 0.0f;
float lbmConvergence = 0.0f;
// v1.27.0 — диагностика устойчивости решателя: сколько популяций за шаг ушло в
// отрицательные значения и сколько было заменено как нефизические. Нули — норма.
long long lbmNegativePops = 0;
long long lbmBadPops = 0;
// Явная диагностика вместо тихой подмены данных (v1.30.0):
long long lbmRhoOutOfRange = 0;   // плотностей вне [0.5; 1.5] — расходимость
long long lbmRhoNonFinite  = 0;   // плотностей NaN/Inf
long long lbmULimited      = 0;   // ячеек, где сработал ограничитель |u| < 0.3
long long lbmPressureNonFinite = 0; // неконечных значений давления за шаг
float lbmTimeMs = 0.0f;
float lbmTKE = 0.0f;

int lbmNx = 0, lbmNy = 0, lbmNz = 0;
float lbmMinX = 0, lbmMinY = 0, lbmMinZ = 0;
float lbmMaxX = 0, lbmMaxY = 0, lbmMaxZ = 0;
float lbmCellSizeX = 0.1f, lbmCellSizeY = 0.1f, lbmCellSizeZ = 0.1f;

// v1.34.0 — бокс самой модели на момент инициализации. Нужен, чтобы понять,
// что воксельная сетка изменилась: раньше сравнивались lbmNx и g_voxNx, но
// теперь домен намеренно больше объекта, и такая проверка перезапускала бы
// решатель каждый кадр.
static float sBodyMin[3] = {0,0,0}, sBodyMax[3] = {-1,-1,-1};
static int   sBodyN[3] = {0,0,0};
static bool  sHasBody = false;
// v1.36.0 — число ЯЧЕЕК ЖИДКОСТИ (домен минус твёрдые и земля).
// Раньше средняя плотность считалась как усреднение только по жидкости, но
// делилась на все ячейки домена. Любая модель с заметной долей твёрдых ячеек
// давала lbmAvgRho < 1 (например, сфера D=20 в 92x84x84 — около 0.995, а
// сжатый тестовый канал 0.94), и проверка «разошёлся ли решатель» принимала
// это за потерю массы и ПЕРЕЗАПУСКАЛА расчёт с равновесным полем. Отсюда были
// и «Divergence detected» на ровном месте, и болтанка скорости вместо
// установившегося решения.
static int   sFluidCells = 1;

std::vector<float> lbmRho;
std::vector<float> lbmUx, lbmUy, lbmUz;
std::vector<float> lbmUxWorld, lbmUyWorld, lbmUzWorld;
std::vector<float> lbmVorticityMag;
std::vector<float> lbmQCriterion;
std::vector<float> lbmPressure;
std::vector<float> lbmTKEField;
std::vector<float> lbmStrainMag;
std::vector<char>  lbmIsSolid;
std::vector<char>  lbmIsGround;

static std::vector<float> f;
static std::vector<float> fNext;

static const int Q = 19;
static const int c[19][3] = {
    {0,0,0},
    {1,0,0}, {-1,0,0}, {0,1,0}, {0,-1,0}, {0,0,1}, {0,0,-1},
    {1,1,0}, {-1,-1,0}, {1,-1,0}, {-1,1,0},
    {1,0,1}, {-1,0,-1}, {1,0,-1}, {-1,0,1},
    {0,1,1}, {0,-1,-1}, {0,1,-1}, {0,-1,1}
};
static const float w[19] = {
    1.0f/3.0f,
    1.0f/18.0f, 1.0f/18.0f, 1.0f/18.0f, 1.0f/18.0f, 1.0f/18.0f, 1.0f/18.0f,
    1.0f/36.0f, 1.0f/36.0f, 1.0f/36.0f, 1.0f/36.0f,
    1.0f/36.0f, 1.0f/36.0f, 1.0f/36.0f, 1.0f/36.0f,
    1.0f/36.0f, 1.0f/36.0f, 1.0f/36.0f, 1.0f/36.0f
};
static const int opp[19] = {
    0, 2,1,4,3,6,5, 8,7,10,9, 12,11,14,13, 16,15,18,17
};
static const float cs2 = 1.0f/3.0f;
static const float invCs2 = 3.0f;
static const float invCs2SqHalf = 4.5f;

inline int idx3D(int x, int y, int z) { return (z*lbmNy + y)*lbmNx + x; }
inline int fIdx(int cell, int q) { return cell*Q + q; }

// =====================================================
// v1.11.0 Physics Fix — корректное равновесие без костылей
// =====================================================
inline float computeEquilibriumFast(int qi, float rho, float ux, float uy, float uz, float usqr) {
    // ИСПРАВЛЕНО: здесь стоял `rho = glm::clamp(rho, 0.8f, 1.2f)` с комментарием
    // «без потери физики». Потеря была: равновесные популяции обязаны давать
    //   sum_i feq_i       = rho        (масса)
    //   sum_i feq_i * c_i = rho * u    (импульс)
    // а с подменой rho сумма feq соответствовала НЕ плотности ячейки, а обрезанной
    // величине. Каждый шаг столкновения после этого добавлял/отнимал массу там,
    // где плотность выходила за [0.8; 1.2], то есть давление p = rho0*cs2*(rho-1)
    // и суммарная масса поля искажались молча. Корректная плотность больше не
    // ограничивается; невалидная (NaN/Inf) по-прежнему заменяется единицей, но
    // теперь это видно по счётчику lbmRhoNonFinite (см. шаг столкновений).
    if (!std::isfinite(rho)) rho = 1.0f;

    // Ограничиваем usqr для стабильности, но сохраняем направление.
    // Это не подмена данных, а требование устойчивости явного LBM: при |u| >= 0.3
    // (Ma_LB ~ 0.52) равновесная функция может стать отрицательной и схема
    // расходится. Факт срабатывания считается в lbmULimited.
    if (usqr > 0.09f) { // |u| < 0.3 в решеточных единицах — предел стабильности
        float scale = sqrtf(0.09f / usqr);
        ux *= scale; uy *= scale; uz *= scale;
        usqr = ux*ux + uy*uy + uz*uz;
    }

    float cu = (float)c[qi][0]*ux + (float)c[qi][1]*uy + (float)c[qi][2]*uz;
    // cu может быть до ~0.3, не климпим жестко, только проверка на NaN
    if (!std::isfinite(cu)) cu = 0.0f;

    float feq = w[qi]*rho*(1.0f + cu*invCs2 + cu*cu*invCs2SqHalf - usqr*1.5f);
    if (!std::isfinite(feq)) feq = w[qi]*rho;
    // v1.27.0 — обнуление отрицательных feq убрано.
    //
    // При ограничении |u| <= 0.3 (оно применяется выше) равновесная функция
    // неотрицательна аналитически: худший случай cu = -0.3, usqr = 0.09 даёт
    // feq/(w*rho) = 1 - 0.9 + 0.405 - 0.135 = 0.37 > 0. То есть прежнее
    // «if (feq < 0) feq = 0» срабатывало только на испорченных данных и при этом
    // нарушало сохранение массы и импульса (сумма feq переставала равняться rho),
    // из-за чего плотность дрейфовала, а решатель уходил в перезапуск.
    return feq;
}

static void computeInletVelocityLB(float& ux, float& uy, float& uz) {
    float az = glm::radians(flowAzimuth);
    float el = glm::radians(flowElevation);
    float ce = cosf(el), se = sinf(el);
    float ca = cosf(az), sa = sinf(az);
    float dx = ce*ca, dy = se, dz = ce*sa;
    float len = sqrtf(dx*dx + dy*dy + dz*dz);
    if (len < 1e-6f || !std::isfinite(len)) { dx = 1; dy = 0; dz = 0; len = 1; }
    dx /= len; dy /= len; dz /= len;
    float U0 = lbmParams.U0;
    if (!std::isfinite(U0) || U0 < 1e-6f) U0 = 0.08f;
    if (U0 > 0.15f) U0 = 0.15f; // стабильный предел 0.15, не 0.25
    ux = dx * U0; uy = dy * U0; uz = dz * U0;
}

static void computeFlowAxis(int& axis, int& sign, glm::vec3& dirNorm) {
    float az = glm::radians(flowAzimuth);
    float el = glm::radians(flowElevation);
    float ce = cosf(el), se = sinf(el);
    float ca = cosf(az), sa = sinf(az);
    dirNorm = glm::vec3(ce*ca, se, ce*sa);
    float len = glm::length(dirNorm);
    if (len < 1e-6f || !std::isfinite(len)) dirNorm = glm::vec3(1,0,0);
    else dirNorm = dirNorm / len;
    float ax = fabsf(dirNorm.x), ay = fabsf(dirNorm.y), azv = fabsf(dirNorm.z);
    if (ax >= ay && ax >= azv) { axis = 0; sign = (dirNorm.x > 0) ? 1 : -1; }
    else if (ay >= azv) { axis = 1; sign = (dirNorm.y > 0) ? 1 : -1; }
    else { axis = 2; sign = (dirNorm.z > 0) ? 1 : -1; }
}

// =====================================================
// Цветовые карты v1.26.0
//
// Карта давления переделана:
//  * диапазон берётся из фактических данных (aeroCpAutoRange) или задаётся вручную,
//    а не зашит как [-3, +1]. Данные LBM ограничены скоростным напором и почти
//    всегда лежат в [-2, +2], поэтому в старом окне невозмущённый поток (Cp = 0)
//    попадал на 75% шкалы и рисовался оранжевым, а всё сжатие сливалось в красный;
//  * схема расходящаяся: Cp = 0 — нейтральный светло-серый, разрежение — холодная
//    ветвь, сжатие — тёплая. Ноль всегда в центре, независимо от асимметрии данных;
//  * схема №2 больше не игнорируется: раньше getRealisticPressureColor обрабатывала
//    только 1 и 3, и выбор «Plasma» для давления ничего не менял.
// =====================================================
namespace {
// Линейная интерполяция по опорным цветам, t ∈ [0,1]
glm::vec3 rampColor(float t, const glm::vec3* stops, int n) {
    if (n <= 0) return glm::vec3(0.5f);
    if (n == 1) return stops[0];
    if (!(t > 0.0f)) return stops[0];
    if (!(t < 1.0f)) return stops[n-1];
    float x = t * (float)(n - 1);
    int i = (int)x;
    if (i >= n - 1) i = n - 2;
    if (i < 0) i = 0;
    float k = x - (float)i;
    glm::vec3 c = stops[i] * (1.0f - k) + stops[i+1] * k;
    if (!std::isfinite(c.x) || !std::isfinite(c.y) || !std::isfinite(c.z)) return glm::vec3(0.5f);
    return c;
}
} // namespace

// v1.30.0 — сумма и импульс равновесных популяций: вынесено из тела решателя,
// чтобы сохранение массы и импульса можно было проверить headless-тестом.
bool lbmEquilibriumMoments(float rho, float ux, float uy, float uz,
                           float* sumFeq, float* momX, float* momY, float* momZ) {
    if (!sumFeq || !momX || !momY || !momZ) return false;
    if (!std::isfinite(rho) || !std::isfinite(ux) || !std::isfinite(uy) || !std::isfinite(uz)) return false;
    float usqr = ux*ux + uy*uy + uz*uz;
    float m = 0.0f, px = 0.0f, py = 0.0f, pz = 0.0f;
    const int numDirs = (int)(sizeof(c) / sizeof(c[0]));   // D3Q19 = 19
    for (int qi = 0; qi < numDirs; ++qi) {
        float feq = computeEquilibriumFast(qi, rho, ux, uy, uz, usqr);
        if (!std::isfinite(feq)) return false;
        m  += feq;
        px += feq * c[qi][0];
        py += feq * c[qi][1];
        pz += feq * c[qi][2];
    }
    *sumFeq = m; *momX = px; *momY = py; *momZ = pz;
    return true;
}

glm::vec3 pressureColorInRange(float cp, float lo, float hi) {
    if (!std::isfinite(cp)) cp = 0.0f;
    if (!std::isfinite(lo) || !std::isfinite(hi) || hi - lo < 1e-4f) { lo = -2.0f; hi = 2.0f; }

    // Последовательные схемы красят по положению во всём диапазоне
    if (aeroColorMap == 1 || aeroColorMap == 2) {
        static const glm::vec3 viridis[5] = {
            glm::vec3(0.267f, 0.004f, 0.329f), glm::vec3(0.229f, 0.322f, 0.545f),
            glm::vec3(0.127f, 0.566f, 0.550f), glm::vec3(0.550f, 0.790f, 0.350f),
            glm::vec3(0.993f, 0.906f, 0.144f)
        };
        static const glm::vec3 plasma[5] = {
            glm::vec3(0.050f, 0.030f, 0.528f), glm::vec3(0.494f, 0.012f, 0.658f),
            glm::vec3(0.798f, 0.280f, 0.470f), glm::vec3(0.973f, 0.585f, 0.254f),
            glm::vec3(0.940f, 0.975f, 0.131f)
        };
        float t = glm::clamp((cp - lo) / (hi - lo), 0.0f, 1.0f);
        return rampColor(t, (aeroColorMap == 1) ? viridis : plasma, 5);
    }

    // Расходящиеся схемы: две независимые ветви от нейтрального цвета
    static const glm::vec3 neutral(0.93f, 0.93f, 0.95f);
    static const glm::vec3 coldRainbow[4] = {
        neutral, glm::vec3(0.55f, 0.82f, 0.94f), glm::vec3(0.15f, 0.45f, 0.85f), glm::vec3(0.03f, 0.10f, 0.60f)
    };
    static const glm::vec3 hotRainbow[4] = {
        neutral, glm::vec3(1.00f, 0.85f, 0.35f), glm::vec3(0.96f, 0.45f, 0.10f), glm::vec3(0.72f, 0.05f, 0.05f)
    };
    static const glm::vec3 coolCold[4] = {
        neutral, glm::vec3(0.60f, 0.79f, 0.92f), glm::vec3(0.26f, 0.45f, 0.80f), glm::vec3(0.09f, 0.15f, 0.58f)
    };
    static const glm::vec3 coolWarm[4] = {
        neutral, glm::vec3(0.96f, 0.70f, 0.60f), glm::vec3(0.86f, 0.30f, 0.22f), glm::vec3(0.55f, 0.02f, 0.10f)
    };
    const bool coolwarm = (aeroColorMap == 3);

    if (cp <= 0.0f) {
        float s = (lo < -1e-6f) ? glm::clamp(cp / lo, 0.0f, 1.0f) : 0.0f;
        return rampColor(s, coolwarm ? coolCold : coldRainbow, 4);
    }
    float s = (hi > 1e-6f) ? glm::clamp(cp / hi, 0.0f, 1.0f) : 0.0f;
    return rampColor(s, coolwarm ? coolWarm : hotRainbow, 4);
}

glm::vec3 getRealisticPressureColor(float cp) {
    float lo = aeroCpAutoRange ? g_cpLo : aeroCpMin;
    float hi = aeroCpAutoRange ? g_cpHi : aeroCpMax;
    return pressureColorInRange(cp, lo, hi);
}

glm::vec3 getVelocityMagnitudeColor(float velMag, float maxVel) {
    if (!std::isfinite(velMag)) velMag = 0;
    if (!std::isfinite(maxVel) || maxVel < 1e-6f) maxVel = 1.0f;
    float t = velMag / maxVel;
    t = glm::clamp(t, 0.0f, 1.0f);

    if (aeroColorMap == 1) {
        if (t < 0.25f) { float k=t/0.25f; return glm::vec3(0.267f, 0.004f+k*0.3f, 0.329f+k*0.2f); }
        else if (t < 0.5f) { float k=(t-0.25f)/0.25f; return glm::vec3(0.229f+k*0.0f, 0.322f+k*0.2f, 0.545f); }
        else if (t < 0.75f) { float k=(t-0.5f)/0.25f; return glm::vec3(0.127f+k*0.4f, 0.566f+k*0.2f, 0.550f-k*0.2f); }
        else { float k=(t-0.75f)/0.25f; return glm::vec3(0.5f+k*0.49f, 0.79f+k*0.1f, 0.3f); }
    } else if (aeroColorMap == 2) {
        if (t < 0.2f) return glm::vec3(t*5.0f*0.1f, t*2.0f, 0.8f);
        else if (t < 0.4f) { float k=(t-0.2f)/0.2f; return glm::vec3(0.1f+k*0.1f, 0.4f+k*0.4f, 0.8f-k*0.2f); }
        else if (t < 0.6f) { float k=(t-0.4f)/0.2f; return glm::vec3(0.2f+k*0.5f, 0.8f, 0.6f-k*0.4f); }
        else if (t < 0.8f) { float k=(t-0.6f)/0.2f; return glm::vec3(0.7f+k*0.3f, 0.8f-k*0.4f, 0.2f); }
        else { float k=(t-0.8f)/0.2f; return glm::vec3(1.0f, 0.4f-k*0.3f, k*0.1f); }
    }

    if (t < 0.15f) return glm::vec3(0, 0, 0.5f + 0.5f*(t/0.15f));
    else if (t < 0.3f) { float k=(t-0.15f)/0.15f; return glm::vec3(0, k, 1); }
    else if (t < 0.45f) { float k=(t-0.3f)/0.15f; return glm::vec3(0, 1, 1-k); }
    else if (t < 0.6f) { float k=(t-0.45f)/0.15f; return glm::vec3(k, 1, 0); }
    else if (t < 0.8f) { float k=(t-0.6f)/0.2f; return glm::vec3(1, 1-k*0.5f, 0); }
    else { float k=(t-0.8f)/0.2f; return glm::vec3(1, 0.5f-0.5f*k, k*0.3f); }
}

glm::vec3 getVorticityColor(float vortMag) {
    if (!std::isfinite(vortMag)) vortMag = 0;
    float t = glm::clamp(vortMag / 20.0f, 0.0f, 1.0f);
    if (t < 0.5f) {
        float k = t*2.0f;
        return glm::vec3(k, k, 1.0f);
    } else {
        float k = (t-0.5f)*2.0f;
        return glm::vec3(1.0f, 1.0f-k, 1.0f-k);
    }
}

glm::vec3 getQCriterionColor(float q) {
    if (!std::isfinite(q)) return glm::vec3(0.5f);
    if (q > 0) {
        float t = glm::clamp(q*10.0f, 0.0f, 1.0f);
        return glm::vec3(1.0f, 0.3f + 0.7f*(1.0f-t), 0.0f);
    } else {
        float t = glm::clamp(-q*5.0f, 0.0f, 1.0f);
        return glm::vec3(0.0f, 0.3f + 0.4f*t, 0.8f + 0.2f*t);
    }
}

glm::vec3 getTKEColor(float tke) {
    if (!std::isfinite(tke)) return glm::vec3(0.5f,0,0.5f);
    float t = glm::clamp(tke*20.0f, 0.0f, 1.0f);
    return glm::vec3(t, t*0.3f, 1.0f-t*0.5f);
}

glm::vec3 getMachColor(float mach) {
    if (!std::isfinite(mach)) return glm::vec3(0.5f);
    float t = glm::clamp(mach / 1.5f, 0.0f, 1.0f);
    if (t < 0.2f) { float k=t/0.2f; return glm::vec3(0, k*0.5f, 0.8f+0.2f*k); }
    else if (t < 0.5f) { float k=(t-0.2f)/0.3f; return glm::vec3(k*0.3f, 0.5f+0.5f*k, 1.0f-k*0.5f); }
    else if (t < 0.75f) { float k=(t-0.5f)/0.25f; return glm::vec3(0.3f+0.7f*k, 1.0f, 0.5f-k*0.5f); }
    else { float k=(t-0.75f)/0.25f; return glm::vec3(1.0f, 1.0f-k*0.8f, k*0.2f); }
}

glm::vec3 getHelicityColor(float helicity) {
    if (!std::isfinite(helicity)) return glm::vec3(0.5f);
    float t = glm::clamp(helicity*0.5f + 0.5f, 0.0f, 1.0f);
    if (t < 0.5f) {
        float k = t*2.0f;
        return glm::vec3(k, k, 1.0f);
    } else {
        float k = (t-0.5f)*2.0f;
        return glm::vec3(1.0f, 1.0f-k, 1.0f-k);
    }
}

glm::vec3 getTotalPressureColor(float pt, float ptInf) {
    if (!std::isfinite(pt) || !std::isfinite(ptInf) || ptInf < 1e-6f) return glm::vec3(0.5f);
    float ratio = pt / ptInf;
    float t = glm::clamp(ratio, 0.0f, 1.2f) / 1.2f;
    if (t < 0.3f) return glm::vec3(0, 0, 0.5f + 0.5f*(t/0.3f));
    else if (t < 0.6f) { float k=(t-0.3f)/0.3f; return glm::vec3(k*0.5f, k, 1.0f-k*0.5f); }
    else if (t < 0.85f) { float k=(t-0.6f)/0.25f; return glm::vec3(0.5f+0.5f*k, 1.0f, 0); }
    else { float k=(t-0.85f)/0.35f; return glm::vec3(1.0f, 1.0f-k*0.5f, 0); }
}

void initLBM() {
    std::cout << "[LBM] Initializing " << AEROS_VERSION_STRING << "..." << std::endl;
    auto t0 = std::chrono::high_resolution_clock::now();

    // v1.34.0 — ДОМЕН БОЛЬШЕ ОБЪЕКТА.
    //
    // Раньше расчётная область в точности совпадала с воксельным боксом модели.
    // Это значит, что входная граница (равновесие со скоростью потока) стояла на
    // первой же ячейке перед телом, а выходная — на последней ячейке за ним:
    // места для возмущения просто не было, и любой след/вихрь срезался границей.
    // Визуально поле выглядело «нормально», но это было обтекание бесконечно
    // тонкого канала, а не тела в потоке: сопротивление считалось по ячейкам,
    // где ещё не восстановилось давление, а Cp за телом был искажён.
    //
    // Теперь строится стандартная внешняя область: 2L вверх по потоку, 4L вниз,
    // 2.5L по бокам и сверху (L = характерный размер модели). Ячейки объекта не
    // укрупняются: разрешение тела и, значит, Re_LB и силы остаются прежними.
    // Если бюджет памяти не позволяет такую область, паддинг уменьшается
    // ступенями и в журнал пишется фактический размер — без молчаливого
    // «сделаем вид, что домен есть».
    if (g_voxelData.empty() || g_voxNx <= 0) {
        lbmNx = voxelResolution;
        lbmNy = voxelResolution;
        lbmNz = voxelResolution;
        lbmMinX = -1.0f; lbmMaxX = 1.0f;
        lbmMinY = -1.0f; lbmMaxY = 1.0f;
        lbmMinZ = -1.0f; lbmMaxZ = 1.0f;
    } else {
        const int bodyNx = g_voxNx, bodyNy = g_voxNy, bodyNz = g_voxNz;
        float csx = (g_voxMaxX - g_voxMinX) / std::max(1, bodyNx);
        float csy = (g_voxMaxY - g_voxMinY) / std::max(1, bodyNy);
        float csz = (g_voxMaxZ - g_voxMinZ) / std::max(1, bodyNz);
        if (!std::isfinite(csx) || csx < 1e-6f) csx = 0.1f;
        if (!std::isfinite(csy) || csy < 1e-6f) csy = 0.1f;
        if (!std::isfinite(csz) || csz < 1e-6f) csz = 0.1f;

        // Характерный размер: габарит модели, а не диагональ бокса (диагональ
        // раздувала бы домен в 1.7 раза без пользы для физики).
        float Lref = std::max(std::max(g_voxMaxX - g_voxMinX, g_voxMaxY - g_voxMinY),
                              g_voxMaxZ - g_voxMinZ);
        if (!std::isfinite(Lref) || Lref < 1e-6f) Lref = 1.0f;

        int flowAxis, flowSign;
        glm::vec3 fdir;
        computeFlowAxis(flowAxis, flowSign, fdir);

        float padUser = lbmParams.domainPadding;
        if (!std::isfinite(padUser)) padUser = 1.0f;
        padUser = glm::clamp(padUser, 0.0f, 1.0f);

        float padsMin[3], padsMax[3];
        for (int i = 0; i < 3; ++i) {
            const float d = std::isfinite(fdir[i]) ? fdir[i] : 0.0f;
            const float upstream = 2.0f * Lref * std::fabs(d) * padUser;
            const float downstream = 4.0f * Lref * std::fabs(d) * padUser;
            const float lateral = 2.5f * Lref * (1.0f - std::fabs(d)) * padUser;
            padsMin[i] = std::max(d > 0.0f ? upstream : downstream, lateral);
            padsMax[i] = std::max(d > 0.0f ? downstream : upstream, lateral);
        }

        // Бюджет ячеек: сам объект + паддинг. 8M ячеек при D3Q19 и двух наборах
        // популяций — это ~1.2 ГБ, выше уже нет смысла для интерактивного счёта.
        const int MAX_CELLS = 8*1024*1024;
        const float cs[3] = {csx, csy, csz};
        const int bodyN[3] = {bodyNx, bodyNy, bodyNz};
        auto totalFor = [&](float scale) {
            long long total = 1;
            for (int i = 0; i < 3; ++i) {
                int extra = (int)std::lround((padsMin[i] + padsMax[i]) * scale / cs[i]);
                if (extra < 0) extra = 0;
                total *= (long long)(bodyN[i] + extra);
            }
            return total;
        };
        float padScale = 1.0f;
        while (padScale > 0.05f && totalFor(padScale) > MAX_CELLS) padScale *= 0.75f;
        if (totalFor(padScale) > MAX_CELLS) padScale = 0.0f;   // не влезает даже минимум
        if (padScale < 0.999f) {
            char msg[256];
            std::snprintf(msg, sizeof(msg),
                          "LBM: домен урезан бюджетом памяти (%.0f%% паддинга, ожидалось %lld ячеек при лимите %d)",
                          padScale * 100.0f, (long long)totalFor(1.0f), MAX_CELLS);
            DBG_PHYS("%s", msg);
            std::cout << "[LBM] " << msg << std::endl;
        }

        lbmMinX = g_voxMinX - padsMin[0] * padScale;
        lbmMinY = g_voxMinY - padsMin[1] * padScale;
        lbmMinZ = g_voxMinZ - padsMin[2] * padScale;
        int nOut[3];
        for (int i = 0; i < 3; ++i) {
            int extraMin = (int)std::ceil((padsMin[i] * padScale - 1e-9f) / cs[i]);
            int extraMax = (int)std::ceil((padsMax[i] * padScale - 1e-9f) / cs[i]);
            if (padScale <= 0.0f) { extraMin = extraMax = 0; }
            nOut[i] = bodyN[i] + std::max(0, extraMin) + std::max(0, extraMax);
            if (nOut[i] < 8) nOut[i] = 8;
        }
        lbmNx = nOut[0]; lbmNy = nOut[1]; lbmNz = nOut[2];
        // Максимумы выводим из числа ячеек и шага, чтобы шаг остался ровно
        // воксельным: иначе мягкое округление сдвигало поверхность тела
        // относительно маски твёрдых ячеек.
        lbmMaxX = lbmMinX + lbmNx * csx;
        lbmMaxY = lbmMinY + lbmNy * csy;
        lbmMaxZ = lbmMinZ + lbmNz * csz;
        lbmCellSizeX = csx; lbmCellSizeY = csy; lbmCellSizeZ = csz;
        sBodyMin[0] = g_voxMinX; sBodyMin[1] = g_voxMinY; sBodyMin[2] = g_voxMinZ;
        sBodyMax[0] = g_voxMaxX; sBodyMax[1] = g_voxMaxY; sBodyMax[2] = g_voxMaxZ;
        sBodyN[0] = bodyNx; sBodyN[1] = bodyNy; sBodyN[2] = bodyNz;
        sHasBody = true;
    }

    // v1.36.0 — БЕЗ МОЛЧАЛИВОГО УМЕНЬШЕНИЯ СЕТКИ.
    //
    // Было: если домен больше 8M ячеек, все три размера умножались на
    // cbrt(8M/всего), печаталась одна строка в stdout, и расчёт продолжался
    // на другой сетке — но в интерфейсе, отчёте и числе Рейнольдса оставалось
    // исходное разрешение. Пользователь узнавал об этом только из журнала.
    //
    // Теперь решатель НЕ запускается, причина и фактическое число ячеек
    // показываются в окне «Сетка не помещается в память» с кнопкой уменьшения
    // разрешения (тот же путь, что у вокселизации, см. voxel_grid.cpp).
    const int MAX_CELLS = 8*1024*1024;
    const int totalEst = lbmNx*lbmNy*lbmNz;
    if (totalEst > MAX_CELLS) {
        const double ratio = (double)MAX_CELLS / (double)totalEst;
        int suggested = (int)std::floor((double)voxelResolution * std::cbrt(ratio));
        suggested = std::max(8, (suggested / 2) * 2);      // чётное и не меньше 8
        char msg[384];
        std::snprintf(msg, sizeof(msg),
                      "Домен LBM %dx%dx%d = %d ячеек не помещается в лимит %d "
                      "(память под два набора популяций). Расчёт не запущен; "
                      "уменьшите разрешение примерно до %d.",
                      lbmNx, lbmNy, lbmNz, totalEst, MAX_CELLS, suggested);
        DBG_ERROR("%s", msg);
        std::cout << "[LBM] " << msg << std::endl;
        g_voxelBuildError = msg;
        g_voxelSuggestedRes = suggested;
        g_voxelDialogPending = true;
        lbmInitialized = false;
        return;
    }

    if (!(lbmCellSizeX > 1e-6f) || !std::isfinite(lbmCellSizeX))
        lbmCellSizeX = (lbmMaxX - lbmMinX) / std::max(1, lbmNx);
    if (!(lbmCellSizeY > 1e-6f) || !std::isfinite(lbmCellSizeY))
        lbmCellSizeY = (lbmMaxY - lbmMinY) / std::max(1, lbmNy);
    if (!(lbmCellSizeZ > 1e-6f) || !std::isfinite(lbmCellSizeZ))
        lbmCellSizeZ = (lbmMaxZ - lbmMinZ) / std::max(1, lbmNz);
    if (!std::isfinite(lbmCellSizeX) || lbmCellSizeX < 1e-6f) lbmCellSizeX = 0.1f;
    if (!std::isfinite(lbmCellSizeY) || lbmCellSizeY < 1e-6f) lbmCellSizeY = 0.1f;
    if (!std::isfinite(lbmCellSizeZ) || lbmCellSizeZ < 1e-6f) lbmCellSizeZ = 0.1f;

    int total = lbmNx * lbmNy * lbmNz;
    if (total <= 0 || total > 20*1024*1024) {
        std::cout << "[LBM] Invalid total cells: " << total << std::endl;
        DBG_ERROR("LBM: недопустимое число ячеек %d (сетка %dx%dx%d) — решатель не запущен",
                  total, lbmNx, lbmNy, lbmNz);
        lbmInitialized = false;
        return;
    }

    try {
        lbmRho.assign(total, 1.0f);
        lbmUx.assign(total, 0.0f);
        lbmUy.assign(total, 0.0f);
        lbmUz.assign(total, 0.0f);
        lbmUxWorld.assign(total, 0.0f);
        lbmUyWorld.assign(total, 0.0f);
        lbmUzWorld.assign(total, 0.0f);
        lbmVorticityMag.assign(total, 0.0f);
        lbmQCriterion.assign(total, 0.0f);
        lbmPressure.assign(total, 0.0f);
        lbmTKEField.assign(total, 0.0f);
        lbmStrainMag.assign(total, 0.0f);
        lbmIsSolid.assign(total, 0);
        lbmIsGround.assign(total, 0);
        f.assign(total*Q, 0.0f);
        fNext.assign(total*Q, 0.0f);
    } catch (const std::bad_alloc& e) {
        std::cout << "[LBM] Allocation failed: " << e.what() << " total=" << total << std::endl;
        DBG_ERROR("LBM: не хватило памяти на сетку %d ячеек (%s) — уменьшите разрешение сетки",
                  total, e.what());
        lbmInitialized = false;
        return;
    }

    // Маска твёрдого тела переносится ПО ФИЗИЧЕСКИМ КООРДИНАТАМ центра ячейки.
    // Прежний «nearest»-путь масштабировал индексы пропорционально размеру
    // массива, что после добавления паддинга растянуло бы тело на весь домен:
    // домен стал бы больше, а объект — тоже, и разрешение тела изменилось бы.
    // Ячейки вне воксельного бокса остаются жидкими (свободный поток).
    if (!g_voxelData.empty() && g_voxNx > 0 && g_voxNy > 0 && g_voxNz > 0) {
        const float bx = (g_voxMaxX - g_voxMinX) / (float)g_voxNx;
        const float by = (g_voxMaxY - g_voxMinY) / (float)g_voxNy;
        const float bz = (g_voxMaxZ - g_voxMinZ) / (float)g_voxNz;
        const int* vox = g_voxelData.data();
        const int voxTotal = (int)g_voxelData.size();
        #ifdef _OPENMP
        #pragma omp parallel for
        #endif
        for (int z = 0; z < lbmNz; ++z) {
            const float pz = lbmMinZ + (z + 0.5f) * lbmCellSizeZ;
            int gz = (int)std::floor((pz - g_voxMinZ) / (bz > 1e-9f ? bz : 1e-9f));
            for (int y = 0; y < lbmNy; ++y) {
                const float py = lbmMinY + (y + 0.5f) * lbmCellSizeY;
                int gy = (int)std::floor((py - g_voxMinY) / (by > 1e-9f ? by : 1e-9f));
                for (int x = 0; x < lbmNx; ++x) {
                    const float px = lbmMinX + (x + 0.5f) * lbmCellSizeX;
                    int gx = (int)std::floor((px - g_voxMinX) / (bx > 1e-9f ? bx : 1e-9f));
                    if (gx < 0 || gx >= g_voxNx || gy < 0 || gy >= g_voxNy || gz < 0 || gz >= g_voxNz)
                        continue;                      // вне тела — свободный поток
                    int gIdx = (gz*g_voxNy + gy)*g_voxNx + gx;
                    if (gIdx < 0 || gIdx >= voxTotal) continue;
                    lbmIsSolid[(z*lbmNy + y)*lbmNx + x] = (vox[gIdx] == 1) ? 1 : 0;
                }
            }
        }
    }

    if (aeroGroundEffect || lbmParams.useGround) {
        float groundY = g_voxMinY + aeroGroundHeight;
        if (lbmParams.useGround) groundY = g_voxMinY + lbmParams.groundHeight;
        if (!std::isfinite(groundY)) groundY = g_voxMinY;
        int gy0 = (int)((groundY - lbmMinY) / lbmCellSizeY);
        if (gy0 < 0) gy0 = 0;
        if (gy0 >= lbmNy) gy0 = lbmNy-1;
        #ifdef _OPENMP
        #pragma omp parallel for
        #endif
        for (int z = 0; z < lbmNz; ++z) {
            for (int y = 0; y <= gy0; ++y) {
                for (int x = 0; x < lbmNx; ++x) {
                    int cell = (z*lbmNy + y)*lbmNx + x;
                    if (!lbmIsSolid[cell]) lbmIsGround[cell] = 1;
                }
            }
        }
        std::cout << "[LBM] Ground enabled at Y=" << groundY << " cells 0.." << gy0 << std::endl;
    }

    // v1.36.0 — средние по домену считаются по жидкости, а не по всем ячейкам
    // (см. комментарий у sFluidCells).
    sFluidCells = 0;
    for (int cell = 0; cell < total; ++cell)
        if (!lbmIsSolid[cell] && !lbmIsGround[cell]) ++sFluidCells;
    if (sFluidCells < 1) sFluidCells = 1;

    float inUx, inUy, inUz;
    computeInletVelocityLB(inUx, inUy, inUz);
    std::cout << "[LBM] Inlet LB velocity: (" << inUx << "," << inUy << "," << inUz << ") total=" << total << std::endl;

    float* fPtr = f.data();
    char* solidPtr = lbmIsSolid.data();
    char* groundPtr = lbmIsGround.data();

    #ifdef _OPENMP
    #pragma omp parallel for
    #endif
    for (int cell = 0; cell < total; ++cell) {
        float rho = 1.0f;
        float ux = 0, uy = 0, uz = 0;
        if (!solidPtr[cell] && !groundPtr[cell]) { ux = inUx; uy = inUy; uz = inUz; }
        // v1.19.0: divergence-free турбулентность через curl векторного потенциала
        if (lbmParams.inletTurbulence > 0.001f && !solidPtr[cell] && !groundPtr[cell]) {
            float turb = lbmParams.inletTurbulence * 0.04f;
            int x = cell % lbmNx;
            int y = (cell / lbmNx) % lbmNy;
            int z = cell / (lbmNx*lbmNy);
            float fx = (float)x / lbmNx * 6.28f, fy = (float)y / lbmNy * 6.28f, fz = (float)z / lbmNz * 6.28f;
            // Векторный потенциал A = (sin(fy+fz), sin(fz+fx), sin(fx+fy))
            // Значения самого потенциала A не нужны — curl считается через производные
            // curl A — дивергенция-free: (dAz/dy - dAy/dz, dAx/dz - dAz/dx, dAy/dx - dAx/dy)
            // Производные через cos
            float dAzdy = cosf(fx*1.2f + fy*0.8f) * 0.8f;
            float dAydz = cosf(fz*1.1f + fx*0.9f) * 1.1f;
            float dAxdz = cosf(fy*1.3f + fz*0.7f) * 0.7f;
            float dAzdx = cosf(fx*1.2f + fy*0.8f) * 1.2f;
            float dAydx = cosf(fz*1.1f + fx*0.9f) * 0.9f;
            float dAxdy = cosf(fy*1.3f + fz*0.7f) * 1.3f;
            float rx = (dAzdy - dAydz) * turb;
            float ry = (dAxdz - dAzdx) * turb;
            float rz = (dAydx - dAxdy) * turb;
            glm::vec3 flowDir(inUx, inUy, inUz);
            float fLen = sqrtf(inUx*inUx+inUy*inUy+inUz*inUz);
            if (fLen > 1e-6f) {
                flowDir /= fLen;
                glm::vec3 turbVec(rx,ry,rz);
                float along = glm::dot(turbVec, flowDir);
                turbVec -= flowDir * along; // только поперек
                ux += turbVec.x; uy += turbVec.y; uz += turbVec.z;
            }
        }
        float usqr = ux*ux + uy*uy + uz*uz;
        if (usqr > 0.09f) {
            float scale = sqrtf(0.09f / usqr);
            ux *= scale; uy *= scale; uz *= scale;
            usqr = ux*ux + uy*uy + uz*uz;
        }
        int base = cell*Q;
        for (int qi = 0; qi < Q; ++qi) {
            fPtr[base+qi] = computeEquilibriumFast(qi, rho, ux, uy, uz, usqr);
        }
    }

    // v1.19.0 Physics Logic Fix — Sutherland + tau из объекта, не домена.
    // ИСПРАВЛЕНО: airTemperature уже в кельвинах (atmosphere.h), а здесь к ней
    // прибавлялось 273.15 — температура удваивалась по шкале Цельсия, вязкость
    // получалась на 58% больше (2.82e-5 вместо 1.79e-5 при 288.15 K), отсюда
    // заниженный RePhys, завышенная nuPhys и другой tau решателя. Теперь формула
    // одна на весь проект — airSutherlandViscosity() в flow_field.h.
    float muSuth = airSutherlandViscosity(airTemperature);

    float L = maxDim;
    if (L < 1e-6f) L = 1.0f;
    float rho = airDensity;
    if (!std::isfinite(rho) || rho < 0.01f) rho = 1.225f;
    float nuPhys = muSuth / rho;

    float U0tmp = std::sqrt(inUx*inUx + inUy*inUy + inUz*inUz);
    if (!std::isfinite(U0tmp) || U0tmp < 1e-6f) U0tmp = 0.08f;
    float RePhys = rho * flowSpeed * L / muSuth;
    if (!std::isfinite(RePhys) || RePhys < 1.0f) RePhys = 1e4f;

    // L_LB — размер объекта в решеточных единицах, не домена
    // L_phys / cellSize = maxDim / cellSizeX
    float cellSizeAvg = (lbmCellSizeX + lbmCellSizeY + lbmCellSizeZ) / 3.0f;
    if (cellSizeAvg < 1e-6f) cellSizeAvg = L / lbmNx;
    float L_LB = L / cellSizeAvg; // объект в клетках
    if (L_LB < 5.0f) L_LB = 5.0f;
    if (L_LB > 200.0f) L_LB = 200.0f;
    // ---- v1.34.0: связка Re <-> tau без подмены результата ----------------
    //
    // Сколько вязкости нужно, чтобы точно воспроизвести физический Re при
    // текущем разрешении:
    //     nu_LB = U0 * L_LB / Re_phys,   tau = 0.5 + nu_LB / cs2
    // Для реального самолёта (Re ~ 1e6) и сетки в десятки ячеек это даёт
    // tau = 0.5 + ~1e-6, то есть состояние, в котором явная схема BGK
    // неустойчива. Решётка физически не в состоянии разрешить такой Re — это
    // ограничение метода, а не настройка.
    //
    // Раньше в этом месте стояла тихая подстановка: если tau из физики < 0.52,
    // бралась пользовательская tau, но в отчёте всё равно печатался физический
    // Re. Пользователь видел Re = 1.9e6, а рассчитывалось течение с Re ~ 100:
    // числа в отчёте не имели отношения к тому, что моделировалось.
    //
    // Теперь оба числа считаются и показываются явно:
    //   Re_phys — что обтекается (по Сазерленду и скорости потока);
    //   Re_LB   — что реально разрешает решётка при выбранной tau.
    // Если они расходятся, это видно в журнале, панели и отчёте, а не спрятано.
    const float tauMin = 0.51f, tauMax = 1.5f;   // предел устойчивости явной BGK
    float nuLB_fromRe = (U0tmp * L_LB) / RePhys;
    float tauFromPhys = nuLB_fromRe / cs2 + 0.5f;
    if (!std::isfinite(tauFromPhys)) tauFromPhys = tauMax;

    float tauFinal;
    if (lbmParams.autoViscosity) {
        tauFinal = glm::clamp(tauFromPhys, tauMin, tauMax);
        if (tauFromPhys < tauMin) {
            DBG_PHYS("LBM: Re_phys %.0f при сетке L_LB=%.1f и U0=%.3f требует tau=%.6f ниже предела "
                     "устойчивости %.2f — расчёт идёт при tau=%.3f (Re_LB=%.0f); "
                     "разрешить Re_phys можно только измельчением сетки",
                     RePhys, L_LB, U0tmp, tauFromPhys, tauMin, tauFinal,
                     U0tmp * L_LB / std::max(1e-12f, (tauFinal - 0.5f) * cs2));
        }
    } else {
        tauFinal = lbmParams.tau;
        if (!std::isfinite(tauFinal)) tauFinal = 0.6f;
        tauFinal = glm::clamp(tauFinal, tauMin, tauMax);
    }

    lbmParams.tau = tauFinal;
    lbmParams.viscosity = (lbmParams.tau - 0.5f) * cs2;
    lbmParams.U0 = U0tmp;

    {
        const float nuLB_actual = (tauFinal - 0.5f) * cs2;
        lbmReynoldsLB = (nuLB_actual > 1e-12f) ? (U0tmp * L_LB / nuLB_actual) : 0.0f;
        if (!std::isfinite(lbmReynoldsLB)) lbmReynoldsLB = 0.0f;
    }

    lbmCurrentStep = 0;
    lbmConverged = false;
    lbmInitialized = true;
    // Решатель запустился — снимаем статус «сетка не влезла», если он остался
    // от предыдущей попытки с другим разрешением.
    g_voxelBuildError.clear();
    g_voxelSuggestedRes = 0;
    g_voxelDialogPending = false;

    // ---- v1.34.0: перекрытие домена телом -------------------------------
    // Отношение фронтальной площади к площади сечения домена. Величина не
    // «для красоты»: при большом перекрытии границы домена сами поджимают
    // поток (как стенки аэродинамической трубы), и Cd растёт вместе с
    // размером модели, а не отражает её форму. Теперь это видно сразу.
    {
        lbmBlockageRatio = 0.0f;
        int ax, sgn;
        glm::vec3 fd;
        computeFlowAxis(ax, sgn, fd);
        float sectionArea = 0.0f;
        if (ax == 0) sectionArea = (lbmNy * lbmCellSizeY) * (lbmNz * lbmCellSizeZ);
        else if (ax == 1) sectionArea = (lbmNx * lbmCellSizeX) * (lbmNz * lbmCellSizeZ);
        else sectionArea = (lbmNx * lbmCellSizeX) * (lbmNy * lbmCellSizeY);
        float frontal = computeLBMRefArea();
        if (std::isfinite(sectionArea) && sectionArea > 1e-9f &&
            std::isfinite(frontal) && frontal > 0.0f) {
            lbmBlockageRatio = frontal / sectionArea;
            if (lbmBlockageRatio > 0.05f) {
                std::cout << "[LBM] Blockage ratio " << (lbmBlockageRatio * 100.0f)
                          << "% (>5%) — границы домена влияют на силы; увеличьте domainPadding"
                          << std::endl;
                DBG_PHYS("LBM: перекрытие домена %.1f%% (>5%%) — Cd/Cl зависят от размера области; "
                         "увеличьте масштаб паддинга или уменьшите разрешение",
                         lbmBlockageRatio * 100.0f);
            }
        }
    }

    lbmReynolds = RePhys;
    aeroReNumber = lbmReynolds;

    auto t1 = std::chrono::high_resolution_clock::now();
    lbmTimeMs = std::chrono::duration<float, std::milli>(t1-t0).count();
    std::cout << "[LBM] Initialized " << AEROS_VERSION_STRING << ": " << lbmNx << "x" << lbmNy << "x" << lbmNz << " = " << total
              << " mu=" << muSuth << " nuPhys=" << nuPhys
              << " cells, tau=" << lbmParams.tau << " nu=" << lbmParams.viscosity
              << " Re_phys=" << lbmReynolds << " Re_LB=" << lbmReynoldsLB
              << " (" << (lbmParams.autoViscosity ? "auto" : "user tau") << ")"
              << " in " << lbmTimeMs << " ms"
#ifdef _OPENMP
              << " [OpenMP " << omp_get_max_threads() << " threads]"
#endif
              << std::endl;
    DBG_PHYS("LBM инициализирован: сетка %dx%dx%d (%d ячеек), tau %.4f, nu %.6f, Re_phys %.1f, Re_LB %.1f, время %.1f мс",
             lbmNx, lbmNy, lbmNz, total, lbmParams.tau, lbmParams.viscosity, lbmReynolds, lbmReynoldsLB, lbmTimeMs);

    // v1.37.0 — теплоперенос инициализируется ПОСЛЕ выбора tau: модуль берёт
    // готовую вязкость решателя, а не свою копию, иначе Pr считался бы от
    // одного nu, а поток жил бы с другим.
    if (thermalParams.enabled) {
        std::vector<char> mask(lbmIsSolid.begin(), lbmIsSolid.end());
        for (size_t i = 0; i < mask.size() && i < lbmIsGround.size(); ++i)
            if (lbmIsGround[i]) mask[i] = 1;
        if (!thermalInit(lbmNx, lbmNy, lbmNz, mask, lbmParams.viscosity)) {
            DBG_ERROR("тепло: модуль не инициализирован на сетке %dx%dx%d — расчёт пойдёт без теплопереноса",
                      lbmNx, lbmNy, lbmNz);
            thermalParams.enabled = false;
        }
    }
}

void resetLBM() {
    std::cout << "[LBM] Reset..." << std::endl;
    DBG_PHYS("LBM: сброс решателя");
    debugLogAction("сброс LBM", "сетка %dx%dx%d", lbmNx, lbmNy, lbmNz);
    shutdownLBM();
    initLBM();
}

void shutdownLBM() {
    if (lbmInitialized) DBG_PHYS("LBM: остановлен на шаге %d", lbmCurrentStep);
    thermalShutdown();
    lbmInitialized = false;
    sHasBody = false;
    lbmCurrentStep = 0;
    lbmConverged = false;
}

void stepLBMCPU(int steps) {
    if (!lbmInitialized) return;
    if (lbmNx <= 0 || lbmNy <= 0 || lbmNz <= 0) return;
    size_t totalSz = (size_t)lbmNx * (size_t)lbmNy * (size_t)lbmNz;
    if (totalSz == 0 || totalSz > 20*1024*1024) return;
    int total = (int)totalSz;
    if (total != (int)lbmRho.size()) return;
    if (f.size() != totalSz * (size_t)Q) return;

    auto t0 = std::chrono::high_resolution_clock::now();

    float tau0 = lbmParams.tau;
    if (!std::isfinite(tau0) || tau0 < 0.51f) tau0 = 0.55f;
    if (tau0 > 1.5f) tau0 = 1.5f;
    float inUx, inUy, inUz;
    computeInletVelocityLB(inUx, inUy, inUz);
    float U0mag = std::sqrt(inUx*inUx + inUy*inUy + inUz*inUz);
    if (!std::isfinite(U0mag) || U0mag < 1e-6f) U0mag = 0.08f;

    int flowAxis, flowSign;
    glm::vec3 flowDir;
    computeFlowAxis(flowAxis, flowSign, flowDir);

    // v1.27.0 — масштаб скорости (решёточные -> м/с) и его квадрат: нужны, чтобы
    // подсеточная кинетическая энергия была в м²/с², а не в безразмерных единицах.
    float velScale = flowSpeed / U0mag;
    if (!std::isfinite(velScale) || velScale > 1000.0f) velScale = 20.0f;
    if (velScale < 0.01f) velScale = 0.01f;
    const float velScale2 = velScale * velScale;

    float* fPtr = f.data();
    float* fNextPtr = fNext.data();
    float* rhoPtr = lbmRho.data();
    float* uxPtr = lbmUx.data();
    float* uyPtr = lbmUy.data();
    float* uzPtr = lbmUz.data();
    float* tkePtr = lbmTKEField.data();
    float* strainPtr = lbmStrainMag.data();
    char* solidPtr = lbmIsSolid.data();
    char* groundPtr = lbmIsGround.data();

    const int Nx = lbmNx, Ny = lbmNy, Nz = lbmNz;
    const int strideY = Nx;
    const int strideZ = Nx*Ny;

    static float prevKinetic = 0.0f;
    bool hasDiverged = false;

    for (int s = 0; s < steps; ++s) {
        float maxVelGlobal = 0.0f;
        double avgRhoAcc = 0.0;
        double kineticAcc = 0.0;
        double tkeAcc = 0.0;
        int nanCount = 0;
        // v1.27.0 — счётчики «грязных» данных. Раньше такие значения молча
        // подменялись, и потеря устойчивости была не видна ни в UI, ни в отчёте.
        long long negPops = 0;      // популяций ушло в отрицательные значения
        long long badPops = 0;      // значений вне физического диапазона / NaN
        // Явная диагностика вместо тихой подмены: сколько ячеек вышло за диапазон
        // плотности, который раньше молча обрезался в computeEquilibriumFast,
        // сколько плотностей оказалось неконечными и сколько раз сработал
        // ограничитель |u| < 0.3.
        long long rhoOut = 0, rhoNonFin = 0, uLim = 0;

        #ifdef _OPENMP
        #pragma omp parallel reduction(+:avgRhoAcc,kineticAcc,tkeAcc,nanCount,negPops,badPops,rhoOut,rhoNonFin,uLim) reduction(max:maxVelGlobal)
        {
            #pragma omp for nowait
        #endif
        for (int cell = 0; cell < total; ++cell) {
            if (solidPtr[cell] || groundPtr[cell]) {
                rhoPtr[cell] = 1.0f;
                uxPtr[cell] = 0; uyPtr[cell] = 0; uzPtr[cell] = 0;
                tkePtr[cell] = 0; strainPtr[cell] = 0;
                continue;
            }

            int base = cell*Q;
            float rho = 0.0f, ux = 0.0f, uy = 0.0f, uz = 0.0f;
            for (int qi = 0; qi < Q; ++qi) {
                float fi = fPtr[base+qi];
                if (!std::isfinite(fi)) { fi = w[qi]; nanCount++; badPops++; }
                // v1.27.0 — пороги расширены до заведомо нефизических значений и
                // считаются. Прежние (-0.2 и 1.0) подменяли вполне рабочие
                // популяции, а каждая подмена меняла сумму f, то есть массу и
                // импульс ячейки, причём без единого сообщения.
                if (fi < -1.0f || fi > 10.0f) { fi = w[qi]; badPops++; }
                rho += fi;
                ux += fi * (float)c[qi][0];
                uy += fi * (float)c[qi][1];
                uz += fi * (float)c[qi][2];
            }
            // Защита от РАСХОДИМОСТИ (не от «некрасивой» плотности): за пределами
            // [0.5; 1.5] либо неконечное значение — это уже развалившийся расчёт,
            // продолжать с ним нельзя. Подмена считается явно и видна в панели,
            // в журнале и в отладочном отчёте, а не происходит молча.
            if (!std::isfinite(rho)) { rhoNonFin++; nanCount++; rho = 1.0f; }
            else if (rho < 0.5f || rho > 1.5f) { rhoOut++; nanCount++; rho = 1.0f; }
            float invRho = 1.0f / rho;
            ux *= invRho; uy *= invRho; uz *= invRho;
            if (!std::isfinite(ux)) { ux = 0; uy = 0; uz = 0; nanCount++; }

            float usqrTmp = ux*ux + uy*uy + uz*uz;
            if (usqrTmp > 0.09f) {
                float scale = sqrtf(0.09f / usqrTmp);
                ux *= scale; uy *= scale; uz *= scale;
            }

            int x = cell % Nx;
            int y = (cell / Nx) % Ny;
            int z = cell / strideZ;
            bool isInlet = false;
            bool isOutlet = false;
            if (flowAxis == 0) {
                isInlet = (flowSign > 0) ? (x == 0) : (x == Nx-1);
                isOutlet = (flowSign > 0) ? (x == Nx-1) : (x == 0);
            } else if (flowAxis == 1) {
                isInlet = (flowSign > 0) ? (y == 0) : (y == Ny-1);
                isOutlet = (flowSign > 0) ? (y == Ny-1) : (y == 0);
            } else {
                isInlet = (flowSign > 0) ? (z == 0) : (z == Nz-1);
                isOutlet = (flowSign > 0) ? (z == Nz-1) : (z == 0);
            }

            // v1.36.0 — при периодичности по X входной границы нет: иначе
            // равновесный вход на x=0 подменял бы популяции, только что
            // перенесённые с x=Nx-1, то есть периодичность была бы фиктивной.
            if (isInlet && !lbmParams.periodicX) {
                rho = 1.0f;
                ux = inUx; uy = inUy; uz = inUz;
            }

            // Smagorinsky LES — v1.11.0 Physics Fix: правильная формула с (Cs*dx)^2
            float tauEff = tau0;
            float strainMag = 0.0f;
            if (lbmParams.useTurbulence && !isInlet && !isOutlet) {
                if (x > 0 && x < Nx-1 && y > 0 && y < Ny-1 && z > 0 && z < Nz-1) {
                    int xm = cell - 1, xp = cell + 1;
                    int ym = cell - strideY, yp = cell + strideY;
                    int zm = cell - strideZ, zp = cell + strideZ;
                    if (xm>=0 && xp<total && ym>=0 && yp<total && zm>=0 && zp<total) {
                        if (!solidPtr[xm] && !solidPtr[xp] && !solidPtr[ym] && !solidPtr[yp] && !solidPtr[zm] && !solidPtr[zp]) {
                            float dux_dx = (uxPtr[xp] - uxPtr[xm]) * 0.5f;
                            float duy_dy = (uyPtr[yp] - uyPtr[ym]) * 0.5f;
                            float duz_dz = (uzPtr[zp] - uzPtr[zm]) * 0.5f;
                            float dux_dy = (uxPtr[yp] - uxPtr[ym]) * 0.5f;
                            float dux_dz = (uxPtr[zp] - uxPtr[zm]) * 0.5f;
                            float duy_dx = (uyPtr[xp] - uyPtr[xm]) * 0.5f;
                            float duy_dz = (uyPtr[zp] - uyPtr[zm]) * 0.5f;
                            float duz_dx = (uzPtr[xp] - uzPtr[xm]) * 0.5f;
                            float duz_dy = (uzPtr[yp] - uzPtr[ym]) * 0.5f;

                            float Sxx = dux_dx, Syy = duy_dy, Szz = duz_dz;
                            float Sxy = 0.5f*(dux_dy + duy_dx);
                            float Sxz = 0.5f*(dux_dz + duz_dx);
                            float Syz = 0.5f*(duy_dz + duz_dy);
                            float S2 = Sxx*Sxx + Syy*Syy + Szz*Szz + 2.0f*(Sxy*Sxy + Sxz*Sxz + Syz*Syz);
                            float S = std::sqrt(2.0f*S2);
                            strainMag = S;
                            if (std::isfinite(S) && S < 10.0f && S > 1e-6f) {
                                float Cs = lbmParams.smagorinskyC;
                                if (!std::isfinite(Cs) || Cs < 0) Cs = 0.12f;
                                if (Cs > 0.3f) Cs = 0.3f;

                                // v1.27.0 — Δ в РЕШЁТОЧНЫХ единицах, то есть 1.
                                //
                                // Прежняя формула подставляла сюда размер ячейки в метрах
                                // (lbmCellSizeX ~ 0.01..0.1 м), тогда как |S| считается по
                                // решёточным скоростям и решёточному шагу. Смешение единиц
                                // занижало ν_t на 2-4 порядка, поэтому вклад модели
                                // турбулентности в τ был порядка 1e-5 — LES фактически не
                                // работала, и срывные/неустойчивые режимы считались без
                                // подсеточной диссипации.
                                // Каноническая запись (Yu и др.): ν_t = (Cs·Δ)²·|S|, Δ = 1.
                                float dx = 1.0f; // шаг решётки в решёточных единицах
                                // Van Driest damping у стенок: Cs_eff = Cs * (1 - exp(-y+/26))
                                // y+ оцениваем как расстояние до ближайшей твердой ячейки
                                // Поиск ближайшей твердой ячейки в радиусе 5
                                int wallDist = 5;
                                bool nearWall = false;
                                for (int dz2=-2; dz2<=2 && !nearWall; ++dz2)
                                    for (int dy2=-2; dy2<=2 && !nearWall; ++dy2)
                                        for (int dx2=-2; dx2<=2 && !nearWall; ++dx2) {
                                            int nx2 = x+dx2, ny2 = y+dy2, nz2 = z+dz2;
                                            if (nx2<0||nx2>=Nx||ny2<0||ny2>=Ny||nz2<0||nz2>=Nz) continue;
                                            int c2 = (nz2*Ny+ny2)*Nx+nx2;
                                            if (solidPtr[c2] || groundPtr[c2]) { wallDist = abs(dx2)+abs(dy2)+abs(dz2); nearWall = true; }
                                        }
                                if (nearWall) {
                                    float yPlusEst = (float)wallDist;
                                    float damping = 1.0f - expf(-yPlusEst/2.0f);
                                    Cs *= damping;
                                }

                                float nu_t = Cs*Cs * dx*dx * S; // правильная формула
                                tauEff = tau0 + nu_t / cs2;
                                if (tauEff > 1.5f) tauEff = 1.5f;
                                if (tauEff < 0.51f) tauEff = 0.51f;
                            }
                        }
                    }
                }
            }

            float usqr = ux*ux + uy*uy + uz*uz;
            if (usqr > 0.09f) {
                uLim++;   // ограничение |u| < 0.3 сработало — это уже видно в диагностике
                float scale = sqrtf(0.09f / usqr);
                ux *= scale; uy *= scale; uz *= scale;
                usqr = ux*ux + uy*uy + uz*uz;
            }
            float invTau = 1.0f / tauEff;

            // v1.36.0 — объёмная сила по схеме Го (Guo, Zheng & Shi, 2002).
            // Половина силы добавляется к скорости, по которой считается
            // равновесие, полная — к популяциям как источник. Без первой половины
            // установившееся течение в канале смещено на O(F·dt), то есть ошибка
            // как раз того же порядка, что и проверяемый профиль.
            float Fx = lbmParams.bodyForceX;
            float Fy = lbmParams.bodyForceY;
            float Fz = lbmParams.bodyForceZ;
            // v1.37.0 — плавучесть: сила по Y от отклонения температуры.
            // Считается по маске на предыдущем шаге, поэтому входит в схему Го
            // так же, как внешняя объёмная сила.
            if (thermalParams.enabled && !thermalBuoyancyY.empty())
                Fy += thermalBuoyancyY[cell];
            const bool hasForce = (Fx != 0.0f || Fy != 0.0f || Fz != 0.0f);
            float ueX = ux, ueY = uy, ueZ = uz;
            float ueSqr = usqr;
            if (hasForce) {
                const float half = 0.5f / rho;
                ueX += Fx * half; ueY += Fy * half; ueZ += Fz * half;
                ueSqr = ueX*ueX + ueY*ueY + ueZ*ueZ;
            }
            // Множитель (1 - 1/(2 s)) — обязательная часть схемы Го: без него
            // установившаяся скорость в канале завышается в (1/2 + tau)/(...)
            // раз. Проверено на течении Пуазейля (physics_ref_headless):
            // без множителя u_max = 0.0353 вместо 0.0200 при F = 7.8e-6.
            // Под TRT множитель свой у каждой моды: (1 - s+/2) и (1 - s-/2).

            // --- TRT: две частоты релаксации (v1.36.2) -------------------
            // s+ отвечает за вязкость и совпадает с BGK (1/tau), s- — за
            // остальные моды. Связь задаёт «магический» параметр
            //   L = (1/s+ - 1/2)(1/s- - 1/2) = 3/16   (d'Humieres & Ginzburg)
            // При L = 3/16 граница bounce-back не зависит от вязкости, а
            // устойчивость перестаёт падать при tau -> 0.5.
            //
            // Проверено на точном решении (Пуазейль, канал 8x34x8, 40 000 шагов):
            // BGK дает L2 = 0.163 %, TRT = 0.071 % (в 2.3 раза точнее), максимум
            // 0.099 % от u_max, вязкость по затуханию в пределах 3 %, дрейф массы
            // 9.9e-4. По умолчанию ВЫКЛЮЧЕН: включение меняет установившиеся
            // числа во всех уже снятых эталонах, а они сейчас измеряются на
            // коротком окне (см. docs/LBM-STABILITY-2026-10-09.md).
            const bool trt = lbmParams.useTRT;
            float sMinus = invTau;
            if (trt) {
                const float m = invTau - 0.5f;
                const float denom = (std::fabs(m) > 1e-6f) ? (0.1875f / m + 0.5f) : 1.0f;
                sMinus = 1.0f / std::max(0.05f, denom);
            }

            for (int qi = 0; qi < Q; ++qi) {
                const int qo = opp[qi];
                if (qo < qi) continue;              // пара обрабатывается один раз

                float feq = computeEquilibriumFast(qi, rho, ueX, ueY, ueZ, ueSqr);
                float fi = fPtr[base+qi];
                if (!std::isfinite(fi)) fi = feq;
                float srcI = 0.0f;
                if (hasForce) {
                    const float cx = (float)c[qi][0], cy = (float)c[qi][1], cz = (float)c[qi][2];
                    const float cu = cx*ueX + cy*ueY + cz*ueZ;
                    srcI = 3.0f * w[qi] * ((cx - ueX + 3.0f*cu*cx) * Fx +
                                                  (cy - ueY + 3.0f*cu*cy) * Fy +
                                                  (cz - ueZ + 3.0f*cu*cz) * Fz);
                }
                if (qo == qi) {
                    float fNew = fi - (fi - feq) * invTau + (1.0f - 0.5f*invTau) * srcI;
                    if (!std::isfinite(fNew)) fNew = feq;
                    if (fNew < 0.0f) negPops++;
                    fPtr[base+qi] = fNew;
                    continue;
                }

                float feqO = computeEquilibriumFast(qo, rho, ueX, ueY, ueZ, ueSqr);
                float fo = fPtr[base+qo];
                if (!std::isfinite(fo)) fo = feqO;
                float srcO = 0.0f;
                if (hasForce) {
                    const float cx = (float)c[qo][0], cy = (float)c[qo][1], cz = (float)c[qo][2];
                    const float cu = cx*ueX + cy*ueY + cz*ueZ;
                    srcO = 3.0f * w[qo] * ((cx - ueX + 3.0f*cu*cx) * Fx +
                                                  (cy - ueY + 3.0f*cu*cy) * Fy +
                                                  (cz - ueZ + 3.0f*cu*cz) * Fz);
                }

                float newI, newO;
                // Источник силы раскладывается по модам так же, как популяции:
                // антисимметричная часть релаксирует с s-, поэтому и множитель
                // (1 - s/2) у неё свой. Общий guoK = 1 - s+/2 для обеих частей
                // смещал установившуюся скорость в канале на 3.6 % (измерено).
                const float srcP = 0.5f*(srcI + srcO);
                const float srcM = 0.5f*(srcI - srcO);
                const float fp = 0.5f*(fi + fo), fm = 0.5f*(fi - fo);
                const float ep = 0.5f*(feq + feqO), em = 0.5f*(feq - feqO);
                float fpN, fmN;
                if (trt) {
                    fpN = fp - invTau*(fp - ep);
                    fmN = fm - sMinus*(fm - em);
                } else {
                    fpN = fp - invTau*(fp - ep);
                    fmN = fm - invTau*(fm - em);
                }
                newI = fpN + fmN + (1.0f - 0.5f*invTau)*srcP + (1.0f - 0.5f*sMinus)*srcM;
                newO = fpN - fmN + (1.0f - 0.5f*invTau)*srcP - (1.0f - 0.5f*sMinus)*srcM;
                if (!std::isfinite(newI)) newI = feq;
                if (!std::isfinite(newO)) newO = feqO;
                if (newI < 0.0f) negPops++;
                if (newO < 0.0f) negPops++;
                fPtr[base+qi] = newI;
                fPtr[base+qo] = newO;
            }

            rhoPtr[cell] = rho;
            // v1.37.0 — ФИЗИЧЕСКАЯ скорость: под схемой Го импульс, посчитанный
            // по популяциям, отличается от скорости жидкости на F/(2 rho).
            // Раньше это не учитывалось, и при объёмной силе поле скорости было
            // смещено на постоянную величину: на Пуазейле это 4e-6 (незаметно),
            // а на плавучести — тот же сдвиг, умноженный на Ra, и он «сдувал»
            // температуру: при Ra = 1000 (ниже порога конвекции) профиль
            // отклонялся от линейного на 0.0045, а Nu был 1.037 вместо 1.000.
            // Физически верная скорость — (sum f c + F/2)/rho.
            if (hasForce) {
                const float halfInvRho = 0.5f / rho;
                uxPtr[cell] = ux + Fx * halfInvRho;
                uyPtr[cell] = uy + Fy * halfInvRho;
                uzPtr[cell] = uz + Fz * halfInvRho;
            } else {
                uxPtr[cell] = ux; uyPtr[cell] = uy; uzPtr[cell] = uz;
            }
            strainPtr[cell] = strainMag;
            // v1.27.0 — подсеточная кинетическая энергия в ФИЗИЧЕСКИХ единицах (м²/с²).
            //
            // Было: Cs²·dx²·S² с dx в метрах и S в решёточных единицах — величина
            // без размерности смысла, к тому же сверху зажатая единицей, поэтому
            // режим «Turbulent KE» и поле lbmTKEField показывали произвольные числа.
            // По модели Смагоринского k = (Cs·Δ·|S|)² в решёточных единицах (Δ = 1),
            // затем перевод в м²/с² квадратом масштаба скорости V∞/U0 — тем же,
            // которым переводятся скорости решётки в мировые.
            float CsT = lbmParams.smagorinskyC;
            if (!std::isfinite(CsT) || CsT < 0.0f) CsT = 0.12f;
            if (CsT > 0.3f) CsT = 0.3f;
            float tkeLb = (CsT * strainMag) * (CsT * strainMag);
            float tke = tkeLb * velScale2;
            if (!std::isfinite(tke) || tke < 0.0f) tke = 0.0f;
            if (tke > 1.0e6f) tke = 1.0e6f;
            tkePtr[cell] = tke;

            float velMag = std::sqrt(usqr);
            if (velMag > maxVelGlobal) maxVelGlobal = velMag;
            avgRhoAcc += rho;
            kineticAcc += usqr;
            tkeAcc += tke;
        }
        #ifdef _OPENMP
        }
        #endif

        if (nanCount > total/10) {
            std::cout << "[LBM] Warning: many NaN/invalid (" << nanCount << ") — possible instability, resetting" << std::endl;
            DBG_ERROR("LBM неустойчив: %d из %d ячеек стали NaN/Inf на шаге %d — решатель будет перезапущен",
                      nanCount, total, lbmCurrentStep);
            hasDiverged = true;
            break;
        }

        // Streaming с bounce-back
        #ifdef _OPENMP
        #pragma omp parallel for
        #endif
        for (int z = 0; z < Nz; ++z) {
            for (int y = 0; y < Ny; ++y) {
                int rowBase = (z*Ny + y)*Nx;
                for (int x = 0; x < Nx; ++x) {
                    int dest = rowBase + x;
                    if (solidPtr[dest] || groundPtr[dest]) continue;
                    int destBase = dest*Q;
                    for (int qi = 0; qi < Q; ++qi) {
                        int sx = x - c[qi][0];
                        int sy = y - c[qi][1];
                        int sz = z - c[qi][2];
                        // v1.36.0 — периодическая граница по X: популяции,
                        // ушедшие за край, приходят с противоположной стороны.
                        // Это не «мягкая» замена стенки, а точное сохранение
                        // массы и импульса в канале (течение Пуазейля).
                        if (lbmParams.periodicX) {
                            if (sx < 0) sx += Nx;
                            else if (sx >= Nx) sx -= Nx;
                        }
                        if (lbmParams.periodicZ) {
                            if (sz < 0) sz += Nz;
                            else if (sz >= Nz) sz -= Nz;
                        }
                        if (sx < 0 || sx >= Nx || sy < 0 || sy >= Ny || sz < 0 || sz >= Nz) {
                            // Граница — будет обработано отдельно как inlet/outlet
                            fNextPtr[destBase+qi] = fPtr[destBase+qi];
                        } else {
                            int src = (sz*Ny + sy)*Nx + sx;
                            if (src < 0 || src >= total) {
                                fNextPtr[destBase+qi] = fPtr[destBase+qi];
                            } else if (solidPtr[src] || groundPtr[src]) {
                                // Bounce-back: отражение
                                fNextPtr[destBase+qi] = fPtr[destBase+opp[qi]];
                            } else {
                                fNextPtr[destBase+qi] = fPtr[src*Q+qi];
                            }
                        }
                    }
                }
            }
        }

        // Вход: все f = равновесные популяции от (rho_in, u_in).
        // Схемы Zou/He здесь нет и не было — см. комментарий в lbm.h.
        if (lbmParams.periodicX) {
            // границы нет — см. ветку переноса выше
        } else if (flowAxis == 0) {
            int ix = (flowSign > 0) ? 0 : Nx-1;
            float usqr = inUx*inUx + inUy*inUy + inUz*inUz;
            #ifdef _OPENMP
            #pragma omp parallel for
            #endif
            for (int z = 0; z < Nz; ++z) {
                for (int y = 0; y < Ny; ++y) {
                    int cell = (z*Ny + y)*Nx + ix;
                    if (cell < 0 || cell >= total) continue;
                    if (solidPtr[cell] || groundPtr[cell]) continue;
                    int base = cell*Q;
                    for (int qi = 0; qi < Q; ++qi) {
                        fNextPtr[base+qi] = computeEquilibriumFast(qi, 1.0f, inUx, inUy, inUz, usqr);
                    }
                }
            }
        } else if (flowAxis == 1) {
            int iy = (flowSign > 0) ? 0 : Ny-1;
            float usqr = inUx*inUx + inUy*inUy + inUz*inUz;
            #ifdef _OPENMP
            #pragma omp parallel for
            #endif
            for (int z = 0; z < Nz; ++z) {
                for (int x = 0; x < Nx; ++x) {
                    int cell = (z*Ny + iy)*Nx + x;
                    if (cell < 0 || cell >= total) continue;
                    if (solidPtr[cell] || groundPtr[cell]) continue;
                    int base = cell*Q;
                    for (int qi = 0; qi < Q; ++qi) {
                        fNextPtr[base+qi] = computeEquilibriumFast(qi, 1.0f, inUx, inUy, inUz, usqr);
                    }
                }
            }
        } else {
            int iz = (flowSign > 0) ? 0 : Nz-1;
            float usqr = inUx*inUx + inUy*inUy + inUz*inUz;
            #ifdef _OPENMP
            #pragma omp parallel for
            #endif
            for (int y = 0; y < Ny; ++y) {
                for (int x = 0; x < Nx; ++x) {
                    int cell = (iz*Ny + y)*Nx + x;
                    if (cell < 0 || cell >= total) continue;
                    if (solidPtr[cell] || groundPtr[cell]) continue;
                    int base = cell*Q;
                    for (int qi = 0; qi < Q; ++qi) {
                        fNextPtr[base+qi] = computeEquilibriumFast(qi, 1.0f, inUx, inUy, inUz, usqr);
                    }
                }
            }
        }

        // Outlet — конвективный: df/dt + U*df/dx =0 => f_out(t+dt)=f_out - U*dt/dx*(f_out-f_in)
        // Более физично чем zero-gradient, сохраняет вихри
        if (lbmParams.useConvectiveOutlet && !lbmParams.periodicX) {
            float convCoeff = U0mag * 0.8f; // U*dt/dx, dt=1, dx=1, U~0.08
            if (convCoeff > 0.5f) convCoeff = 0.5f;
            if (convCoeff < 0.01f) convCoeff = 0.08f;
            if (flowAxis == 0) {
                int ox = (flowSign > 0) ? Nx-1 : 0;
                int ix = (flowSign > 0) ? Nx-2 : 1;
                int ix2 = (flowSign > 0) ? Nx-3 : 2;
                #ifdef _OPENMP
                #pragma omp parallel for
                #endif
                for (int z = 0; z < Nz; ++z) {
                    for (int y = 0; y < Ny; ++y) {
                        int cellOut = (z*Ny + y)*Nx + ox;
                        int cellIn = (z*Ny + y)*Nx + ix;
                        int cellIn2 = (z*Ny + y)*Nx + ix2;
                        if (cellOut < 0 || cellOut >= total || cellIn < 0 || cellIn >= total) continue;
                        if (solidPtr[cellOut] || groundPtr[cellOut]) continue;
                        int baseOut = cellOut*Q;
                        int baseIn = cellIn*Q;
                        int baseIn2 = cellIn2*Q;
                        for (int qi = 0; qi < Q; ++qi) {
                            // Конвективный: f_out_new = f_out + coeff*(f_in - f_out) + 0.1*coeff*(f_in - f_in2) для второго порядка
                            float fOut = fPtr[baseOut+qi];
                            float fIn = fPtr[baseIn+qi];
                            float fIn2 = (cellIn2>=0 && cellIn2<total) ? fPtr[baseIn2+qi] : fIn;
                            float df = fIn - fOut;
                            float df2 = fIn - fIn2;
                            fNextPtr[baseOut+qi] = fOut + convCoeff*df + 0.15f*convCoeff*df2;
                        }
                    }
                }
            } else if (flowAxis == 1) {
                int oy = (flowSign > 0) ? Ny-1 : 0;
                int iy = (flowSign > 0) ? Ny-2 : 1;
                int iy2 = (flowSign > 0) ? Ny-3 : 2;
                #ifdef _OPENMP
                #pragma omp parallel for
                #endif
                for (int z = 0; z < Nz; ++z) {
                    for (int x = 0; x < Nx; ++x) {
                        int cellOut = (z*Ny + oy)*Nx + x;
                        int cellIn = (z*Ny + iy)*Nx + x;
                        int cellIn2 = (z*Ny + iy2)*Nx + x;
                        if (cellOut < 0 || cellOut >= total || cellIn < 0 || cellIn >= total) continue;
                        if (solidPtr[cellOut] || groundPtr[cellOut]) continue;
                        int baseOut = cellOut*Q;
                        int baseIn = cellIn*Q;
                        int baseIn2 = cellIn2*Q;
                        for (int qi = 0; qi < Q; ++qi) {
                            float fOut = fPtr[baseOut+qi];
                            float fIn = fPtr[baseIn+qi];
                            float fIn2 = (cellIn2>=0 && cellIn2<total) ? fPtr[baseIn2+qi] : fIn;
                            fNextPtr[baseOut+qi] = fOut + convCoeff*(fIn-fOut) + 0.15f*convCoeff*(fIn-fIn2);
                        }
                    }
                }
            } else {
                int oz = (flowSign > 0) ? Nz-1 : 0;
                int iz = (flowSign > 0) ? Nz-2 : 1;
                int iz2 = (flowSign > 0) ? Nz-3 : 2;
                #ifdef _OPENMP
                #pragma omp parallel for
                #endif
                for (int y = 0; y < Ny; ++y) {
                    for (int x = 0; x < Nx; ++x) {
                        int cellOut = (oz*Ny + y)*Nx + x;
                        int cellIn = (iz*Ny + y)*Nx + x;
                        int cellIn2 = (iz2*Ny + y)*Nx + x;
                        if (cellOut < 0 || cellOut >= total || cellIn < 0 || cellIn >= total) continue;
                        if (solidPtr[cellOut] || groundPtr[cellOut]) continue;
                        int baseOut = cellOut*Q;
                        int baseIn = cellIn*Q;
                        int baseIn2 = cellIn2*Q;
                        for (int qi = 0; qi < Q; ++qi) {
                            float fOut = fPtr[baseOut+qi];
                            float fIn = fPtr[baseIn+qi];
                            float fIn2 = (cellIn2>=0 && cellIn2<total) ? fPtr[baseIn2+qi] : fIn;
                            fNextPtr[baseOut+qi] = fOut + convCoeff*(fIn-fOut) + 0.15f*convCoeff*(fIn-fIn2);
                        }
                    }
                }
            }
        }

        f.swap(fNext);

        // v1.37.0 — перенос температуры текущим полем скорости. Стоит после
        // обмена, потому что скорость в lbmUx/lbmUy/lbmUz посчитана на этом же
        // шаге; плавучесть из нового поля войдёт в следующий шаг соударений.
        if (thermalParams.enabled)
            thermalStep(lbmUx.data(), lbmUy.data(), lbmUz.data());

        lbmCurrentStep++;
        lbmAvgRho = (float)(avgRhoAcc / sFluidCells);
        lbmAvgKineticEnergy = (float)(kineticAcc / sFluidCells);
        lbmMaxVelocityLB = maxVelGlobal;
        lbmTKE = (float)(tkeAcc / sFluidCells);

        float scale = flowSpeed / U0mag;
        if (!std::isfinite(scale) || scale > 1000.0f) scale = 20.0f;
        if (scale < 0.01f) scale = 0.01f;
        lbmMaxVelocityWorld = maxVelGlobal * scale;

        lbmNegativePops = negPops;
        lbmBadPops = badPops;
        lbmRhoOutOfRange = rhoOut;
        lbmRhoNonFinite  = rhoNonFin;
        lbmULimited      = uLim;
        lbmConvergence = std::fabs(lbmAvgKineticEnergy - prevKinetic);
        prevKinetic = lbmAvgKineticEnergy;
        lbmConverged = (lbmConvergence < lbmParams.convergenceThreshold);

        if (!std::isfinite(lbmAvgRho) || lbmAvgRho > 1.2f || lbmAvgRho < 0.8f) {
            std::cout << "[LBM] Divergence detected rho=" << lbmAvgRho << " — resetting" << std::endl;
            DBG_ERROR("LBM разошёлся: rho=%.6f вне диапазона 0.8..1.2 на шаге %d (скорость потока %.3f м/с) — перезапуск",
                      lbmAvgRho, lbmCurrentStep, flowSpeed);
            hasDiverged = true;
            break;
        }
    }

    if (hasDiverged) {
        float inUx2, inUy2, inUz2;
        computeInletVelocityLB(inUx2, inUy2, inUz2);
        float usqr = inUx2*inUx2 + inUy2*inUy2 + inUz2*inUz2;
        #ifdef _OPENMP
        #pragma omp parallel for
        #endif
        for (int cell = 0; cell < total; ++cell) {
            if (lbmIsSolid[cell] || lbmIsGround[cell]) continue;
            int base = cell*Q;
            for (int qi = 0; qi < Q; ++qi) {
                f[base+qi] = computeEquilibriumFast(qi, 1.0f, inUx2, inUy2, inUz2, usqr);
            }
            lbmRho[cell] = 1.0f;
            lbmUx[cell] = inUx2; lbmUy[cell] = inUy2; lbmUz[cell] = inUz2;
        }
        lbmAvgRho = 1.0f;
        lbmAvgKineticEnergy = usqr;
    }

    {
        float inUx2, inUy2, inUz2;
        computeInletVelocityLB(inUx2, inUy2, inUz2);
        float U0mag2 = std::sqrt(inUx2*inUx2 + inUy2*inUy2 + inUz2*inUz2);
        if (!std::isfinite(U0mag2) || U0mag2 < 1e-6f) U0mag2 = 0.08f;
        float scale = flowSpeed / U0mag2;
        if (!std::isfinite(scale) || scale > 1000.0f) scale = 20.0f;
        float* uxW = lbmUxWorld.data();
        float* uyW = lbmUyWorld.data();
        float* uzW = lbmUzWorld.data();
        float* pPtr = lbmPressure.data();
        char* solidPtr2 = lbmIsSolid.data();
        char* groundPtr2 = lbmIsGround.data();
        float rho0 = airDensity; // физическая плотность для масштабирования давления
        long long pBad = 0;      // неконечных значений давления — явная диагностика
        #ifdef _OPENMP
        #pragma omp parallel for reduction(+:pBad)
        #endif
        for (int cell = 0; cell < total; ++cell) {
            if (solidPtr2[cell] || groundPtr2[cell]) {
                uxW[cell] = 0; uyW[cell] = 0; uzW[cell] = 0;
                pPtr[cell] = 0;
            } else {
                float ux = lbmUx[cell], uy = lbmUy[cell], uz = lbmUz[cell];
                if (!std::isfinite(ux)) ux = 0;
                uxW[cell] = ux * scale;
                uyW[cell] = uy * scale;
                uzW[cell] = uz * scale;
                float rho = lbmRho[cell];
                if (!std::isfinite(rho)) rho = 1.0f;
                // v1.11.0: корректное давление p = rho0 * cs2*(rho-1) * scale^2
                float pLat = cs2 * (rho - 1.0f);
                float pPhys = pLat * scale * scale * rho0;
                // Неконечное значение — признак развалившегося расчёта. Оно
                // подменяется нулём, но не молча: счётчик lbmPressureNonFinite
                // виден в панели решателя, в журнале и в отладочном отчёте.
                if (!std::isfinite(pPhys)) { pBad++; pPhys = 0.0f; }
                // ИСПРАВЛЕНО: здесь стояло
                //   float q = 0.5f*rho0*flowSpeed*flowSpeed;
                //   if (fabsf(pPhys) > q) pPhys = glm::clamp(pPhys, -q, q);
                // — ФИЗИЧЕСКОЕ поле давления обрезалось по скоростному напору
                // (комментарий обещал 0.5*q, а код резал по q). lbmPressure читают
                // силы (forces.cpp), трассировка давления, экспорт CSV, самописец и
                // карта давления, поэтому на точках торможения и в следе вместо
                // посчитанного перепада выдавался потолок ±q: Cp на теле становился
                // ровно ±1.0, а сила давления систематически занижалась. Данные
                // больше не ограничиваются; диапазон для КАРТИНКИ режет сама карта
                // цвета (pressureColorInRange -> glm::clamp), а авто-масштаб
                // aeroUpdateCpRange берёт квантили по настоящим значениям.
                pPtr[cell] = pPhys;
            }
        }
        lbmPressureNonFinite = pBad;

        // v1.26.0 — мин/макс давления и остаток сходимости считаются здесь же,
        // одним проходом с ограниченным числом ячеек (не больше 256K сравнений),
        // чтобы UI и карта давления не делали max_element по всей сетке каждый кадр.
        {
            int stride = 1;
            while (total / stride > 262144) stride *= 2;
            float pMin = 1e30f, pMax = -1e30f;
            static std::vector<float> prevRho;
            static int prevTotal = -1;
            static int prevStep  = -1;
            bool residualValid = ((int)prevRho.size() == total && prevTotal == total &&
                                  prevStep >= 0 && lbmCurrentStep >= prevStep);
            if ((int)prevRho.size() != total) {
                try { prevRho.assign((size_t)total, 1.0f); } catch (...) { prevRho.clear(); }
            }
            double sumSq = 0.0;
            int nSampled = 0;
            for (int cell = 0; cell < total; cell += stride) {
                float p = pPtr[cell];
                if (std::isfinite(p)) { if (p < pMin) pMin = p; if (p > pMax) pMax = p; }
                if (residualValid && cell < (int)prevRho.size()) {
                    float r = lbmRho[cell];
                    if (std::isfinite(r)) {
                        float d = r - prevRho[cell];
                        sumSq += (double)d * (double)d;
                        ++nSampled;
                    }
                }
            }
            lbmPressureMin = (pMin <= pMax && std::isfinite(pMin)) ? pMin : 0.0f;
            lbmPressureMax = (pMin <= pMax && std::isfinite(pMax)) ? pMax : 0.0f;
            if (residualValid && nSampled > 0)
                lbmResidualRho = (float)std::sqrt(sumSq / (double)nSampled);
            else
                lbmResidualRho = 0.0f;
            // Изменение максимальной скорости за кадр — дешёвый индикатор того,
            // что поток ещё перестраивается (полный векторный остаток стоил бы
            // ещё три поля памяти).
            static float prevMaxVelLB = 0.0f;
            lbmResidualU = std::fabs(lbmMaxVelocityLB - prevMaxVelLB);
            prevMaxVelLB = lbmMaxVelocityLB;
            // Сохраняем плотность для следующего сравнения (только жидкие ячейки)
            for (int cell = 0; cell < total && cell < (int)prevRho.size(); ++cell) {
                float r = lbmRho[cell];
                prevRho[cell] = std::isfinite(r) ? r : 1.0f;
            }
            prevTotal = total;
            prevStep  = lbmCurrentStep;
        }
    }

    auto t1 = std::chrono::high_resolution_clock::now();
    lbmTimeMs = std::chrono::duration<float, std::milli>(t1-t0).count();
}

// v1.29.0 — сколько ФИЗИЧЕСКИХ секунд занимает один шаг решётки.
// Формула та же, что и масштаб скорости внутри stepLBMCPU (velScale = flowSpeed /
// |u_LB|): в решёточных единицах c = Δx/Δt = 1, значит Δt_физ = Δx_физ / velScale.
// Это единственная «правильная» длительность шага явного LBM — уменьшить её можно
// только измельчением сетки, поэтому тактовый генератор считает шаги решётки по
// накопленному модельному времени, а не по кадрам.
float lbmLatticeDtWorld() {
    if (!lbmInitialized) return 0.0f;
    float dx = lbmCellSizeX;
    if (!std::isfinite(dx) || dx <= 0.0f) return 0.0f;
    float inUx, inUy, inUz;
    computeInletVelocityLB(inUx, inUy, inUz);
    float U0mag = std::sqrt(inUx*inUx + inUy*inUy + inUz*inUz);
    if (!std::isfinite(U0mag) || U0mag < 1e-6f) U0mag = 0.08f;
    float velScale = flowSpeed / U0mag;
    if (!std::isfinite(velScale) || velScale > 1000.0f) velScale = 20.0f;
    if (velScale < 0.01f) velScale = 0.01f;
    float dt = dx / velScale;
    if (!std::isfinite(dt) || dt <= 0.0f) return 0.0f;
    if (dt > 1.0f) dt = 1.0f;   // защита от вырожденной сетки: шаг не длиннее секунды
    return dt;
}

void updateLBM(float deltaTime, int forceSteps) {
    if (!lbmParams.enabled) return;
    if (!lbmInitialized) { initLBM(); return; }
    // v1.34.0 — домен намеренно больше воксельного бокса (см. initLBM), поэтому
    // сравнение lbmNx с g_voxNx перезапускало бы решатель каждый кадр. Следим
    // за боксом самой модели: его изменение и есть повод для переинициализации.
    {
        const bool haveBody = !(g_voxelData.empty() || g_voxNx <= 0 || g_voxNy <= 0 || g_voxNz <= 0);
        bool changed = false;
        if (haveBody != sHasBody) {
            changed = true;
        } else if (haveBody) {
            changed = sBodyN[0] != g_voxNx || sBodyN[1] != g_voxNy || sBodyN[2] != g_voxNz ||
                      std::fabs(sBodyMin[0] - g_voxMinX) > 1e-4f ||
                      std::fabs(sBodyMin[1] - g_voxMinY) > 1e-4f ||
                      std::fabs(sBodyMin[2] - g_voxMinZ) > 1e-4f ||
                      std::fabs(sBodyMax[0] - g_voxMaxX) > 1e-4f ||
                      std::fabs(sBodyMax[1] - g_voxMaxY) > 1e-4f ||
                      std::fabs(sBodyMax[2] - g_voxMaxZ) > 1e-4f;
        }
        if (changed) {
            std::cout << "[LBM] Voxel grid changed, reinit" << std::endl;
            DBG_PHYS("LBM: воксельная сетка изменилась (%dx%dx%d -> %dx%dx%d), переустановка решателя",
                     sBodyN[0], sBodyN[1], sBodyN[2], g_voxNx, g_voxNy, g_voxNz);
            initLBM();
            return;
        }
    }
    int steps;
    if (forceSteps >= 0) {
        // v1.29.0 — число шагов пришло из тактового генератора: оно посчитано по
        // модельному времени, поэтому результат больше не зависит от герцовки
        // монитора и от просадок кадров. 0 — законный ответ: за этот кадр
        // модельное время ещё не накопилось на целый шаг решётки.
        steps = forceSteps;
        if (steps > 64) steps = 64;
    } else {
        steps = lbmParams.stepsPerFrame;
        if (steps <= 0) steps = 1;
        if (steps > 50) steps = 50;
        if (deltaTime > 0.02f) steps = std::min(steps*2, 50);
    }
    if (steps <= 0) return;
    stepLBMCPU(steps);
    if (lbmCurrentStep % 30 == 0) computeLBMVorticityAndQ();
}

// v1.11.0: улучшенная трилинейная интерполяция — учитывает только жидкие ячейки
glm::vec3 getLBMVelocityLB(const glm::vec3& worldPos) {
    if (!lbmInitialized) return glm::vec3(0);
    if (!std::isfinite(worldPos.x)) return glm::vec3(0);
    float fx = (worldPos.x - lbmMinX) / lbmCellSizeX;
    float fy = (worldPos.y - lbmMinY) / lbmCellSizeY;
    float fz = (worldPos.z - lbmMinZ) / lbmCellSizeZ;
    int ix = (int)std::floor(fx);
    int iy = (int)std::floor(fy);
    int iz = (int)std::floor(fz);
    float tx = fx - (float)ix, ty = fy - (float)iy, tz = fz - (float)iz;
    tx = glm::clamp(tx, 0.0f, 1.0f);
    ty = glm::clamp(ty, 0.0f, 1.0f);
    tz = glm::clamp(tz, 0.0f, 1.0f);
    if (ix < 0 || ix >= lbmNx-1 || iy < 0 || iy >= lbmNy-1 || iz < 0 || iz >= lbmNz-1) return glm::vec3(0);
    const float* uxPtr = lbmUx.data();
    const float* uyPtr = lbmUy.data();
    const float* uzPtr = lbmUz.data();
    const char* solidPtr = lbmIsSolid.data();
    const char* groundPtr = lbmIsGround.data();

    // Собираем только жидкие ячейки, игнорируем твердые
    struct Sample { glm::vec3 v; float w; };
    Sample samples[8];
    int validCount = 0;
    float totalW = 0.0f;

    auto tryFetch = [&](int x,int y,int z, float weight) {
        if (x<0||x>=lbmNx||y<0||y>=lbmNy||z<0||z>=lbmNz) return;
        int cell = (z*lbmNy + y)*lbmNx + x;
        if (cell < 0 || cell >= (int)lbmIsSolid.size()) return;
        if (solidPtr[cell] || groundPtr[cell]) return; // пропускаем твердые
        glm::vec3 vv(uxPtr[cell], uyPtr[cell], uzPtr[cell]);
        if (!std::isfinite(vv.x)) return;
        samples[validCount].v = vv;
        samples[validCount].w = weight;
        totalW += weight;
        validCount++;
    };

    float w000 = (1-tx)*(1-ty)*(1-tz);
    float w100 = tx*(1-ty)*(1-tz);
    float w010 = (1-tx)*ty*(1-tz);
    float w110 = tx*ty*(1-tz);
    float w001 = (1-tx)*(1-ty)*tz;
    float w101 = tx*(1-ty)*tz;
    float w011 = (1-tx)*ty*tz;
    float w111 = tx*ty*tz;

    tryFetch(ix,iy,iz,w000);
    tryFetch(ix+1,iy,iz,w100);
    tryFetch(ix,iy+1,iz,w010);
    tryFetch(ix+1,iy+1,iz,w110);
    tryFetch(ix,iy,iz+1,w001);
    tryFetch(ix+1,iy,iz+1,w101);
    tryFetch(ix,iy+1,iz+1,w011);
    tryFetch(ix+1,iy+1,iz+1,w111);

    if (validCount == 0) return glm::vec3(0); // все твердые — no-slip
    if (totalW < 1e-6f) return samples[0].v;

    glm::vec3 result(0);
    for (int i=0;i<validCount;++i) result += samples[i].v * (samples[i].w / totalW);
    return std::isfinite(result.x) ? result : glm::vec3(0);
}

glm::vec3 getLBMVelocityWorld(const glm::vec3& worldPos) {
    if (!lbmInitialized) return glm::vec3(flowParams.vx, flowParams.vy, flowParams.vz);
    if (worldPos.x < lbmMinX || worldPos.x > lbmMaxX || worldPos.y < lbmMinY || worldPos.y > lbmMaxY || worldPos.z < lbmMinZ || worldPos.z > lbmMaxZ)
        return glm::vec3(flowParams.vx, flowParams.vy, flowParams.vz);
    // Проверка на твердое тело — возвращаем 0, а не freestream*0.1
    if (isLBMSolidWorld(worldPos)) return glm::vec3(0.0f);

    glm::vec3 vLB = getLBMVelocityLB(worldPos);
    float mag2 = vLB.x*vLB.x + vLB.y*vLB.y + vLB.z*vLB.z;
    if (mag2 < 1e-12f) return glm::vec3(0.0f); // в погранслое — 0, физично

    float inUx, inUy, inUz;
    computeInletVelocityLB(inUx, inUy, inUz);
    float U0mag = std::sqrt(inUx*inUx + inUy*inUy + inUz*inUz);
    if (!std::isfinite(U0mag) || U0mag < 1e-6f) U0mag = 0.08f;
    float scale = flowSpeed / U0mag;
    if (!std::isfinite(scale) || scale > 1000.0f) scale = 20.0f;
    glm::vec3 vWorld = vLB * scale;
    if (!std::isfinite(vWorld.x)) return glm::vec3(flowParams.vx, flowParams.vy, flowParams.vz);
    float maxV = flowSpeed * 2.0f; // ограничим 2*Vinf
    float mag = glm::length(vWorld);
    if (mag > maxV) vWorld *= maxV / mag;
    return vWorld;
}

// =====================================================
// v1.26.0 — единый трилинейный сэмплер скалярных полей решётки.
//
// Раньше каждый аксессор считал индекс как (int)fx. Для fx = -0.4 это 0, а не -1:
// точка чуть вне расчётной области читала ячейку у её края и получала «почти
// правильные» значения, а сама карта была ступенчатой (ближайшая ячейка вместо
// интерполяции). Теперь: floor, трилинейная интерполяция, твёрдые ячейки
// исключаются с перенормировкой весов — как уже делалось для скорости.
// =====================================================
static bool sampleFieldTrilinear(const std::vector<float>& field,
                                 const glm::vec3& worldPos,
                                 bool skipSolid, float* out) {
    if (!out) return false;
    *out = 0.0f;
    if (!lbmInitialized || field.empty()) return false;
    if (!std::isfinite(worldPos.x) || !std::isfinite(worldPos.y) || !std::isfinite(worldPos.z))
        return false;
    if (lbmCellSizeX < 1e-9f || lbmCellSizeY < 1e-9f || lbmCellSizeZ < 1e-9f) return false;
    if (lbmNx < 2 || lbmNy < 2 || lbmNz < 2) return false;

    float fx = (worldPos.x - lbmMinX) / lbmCellSizeX;
    float fy = (worldPos.y - lbmMinY) / lbmCellSizeY;
    float fz = (worldPos.z - lbmMinZ) / lbmCellSizeZ;
    int ix = (int)std::floor(fx);
    int iy = (int)std::floor(fy);
    int iz = (int)std::floor(fz);
    if (ix < 0 || iy < 0 || iz < 0) return false;
    if (ix > lbmNx - 2 || iy > lbmNy - 2 || iz > lbmNz - 2) return false;

    float tx = glm::clamp(fx - (float)ix, 0.0f, 1.0f);
    float ty = glm::clamp(fy - (float)iy, 0.0f, 1.0f);
    float tz = glm::clamp(fz - (float)iz, 0.0f, 1.0f);
    const float wx[2] = { 1.0f - tx, tx };
    const float wy[2] = { 1.0f - ty, ty };
    const float wz[2] = { 1.0f - tz, tz };

    const int total = lbmNx * lbmNy * lbmNz;
    if ((int)field.size() < total) return false;
    const bool checkSolid = skipSolid && (int)lbmIsSolid.size() >= total &&
                            (int)lbmIsGround.size() >= total;

    float acc = 0.0f, wsum = 0.0f;
    for (int dz = 0; dz <= 1; ++dz) {
        for (int dy = 0; dy <= 1; ++dy) {
            for (int dx = 0; dx <= 1; ++dx) {
                int cell = ((iz + dz) * lbmNy + (iy + dy)) * lbmNx + (ix + dx);
                if (cell < 0 || cell >= total) continue;
                if (checkSolid && (lbmIsSolid[cell] || lbmIsGround[cell])) continue;
                float v = field[cell];
                if (!std::isfinite(v)) continue;
                float w = wx[dx] * wy[dy] * wz[dz];
                acc += v * w;
                wsum += w;
            }
        }
    }
    if (wsum < 1e-6f) return false;
    acc /= wsum;
    if (!std::isfinite(acc)) return false;
    *out = acc;
    return true;
}

// Давление (Па, избыточное) с трилинейной интерполяцией. Checked-вариант
// сообщает, были ли данные: это позволяет карте давления и таймлапсу отличать
// «ноль по физике» от «точки вне расчётной области».
bool getLBMPressureWorldChecked(const glm::vec3& worldPos, float* outPa) {
    return sampleFieldTrilinear(lbmPressure, worldPos, true, outPa);
}

float getLBMPressureWorld(const glm::vec3& worldPos) {
    float pa = 0.0f;
    if (getLBMPressureWorldChecked(worldPos, &pa)) return pa;
    return 0.0f;
}

float getLBMDensityWorld(const glm::vec3& worldPos) {
    float rho = 1.0f;
    if (sampleFieldTrilinear(lbmRho, worldPos, true, &rho)) return glm::clamp(rho, 0.8f, 1.2f);
    return 1.0f;
}

bool isLBMSolidWorld(const glm::vec3& worldPos) {
    if (!lbmInitialized) return false;
    if (!std::isfinite(worldPos.x) || !std::isfinite(worldPos.y) || !std::isfinite(worldPos.z))
        return false;
    if (lbmCellSizeX < 1e-9f || lbmCellSizeY < 1e-9f || lbmCellSizeZ < 1e-9f) return false;
    float fx = (worldPos.x - lbmMinX) / lbmCellSizeX;
    float fy = (worldPos.y - lbmMinY) / lbmCellSizeY;
    float fz = (worldPos.z - lbmMinZ) / lbmCellSizeZ;
    // маска твёрдого тела интерполируется nearest, но индекс обязан быть floor:
    // при (int)(-0.4) = 0 точка слева от области попадала в ячейку у её края
    int ix = (int)std::floor(fx), iy = (int)std::floor(fy), iz = (int)std::floor(fz);
    if (ix < 0 || ix >= lbmNx || iy < 0 || iy >= lbmNy || iz < 0 || iz >= lbmNz) return false;
    int cell = (iz*lbmNy + iy)*lbmNx + ix;
    if (cell < 0 || cell >= (int)lbmIsSolid.size()) return false;
    return lbmIsSolid[cell] != 0 || lbmIsGround[cell] != 0;
}

float getLBMVorticityWorld(const glm::vec3& worldPos) {
    if (!lbmInitialized || lbmVorticityMag.empty()) return 0.0f;
    float v = 0.0f;
    if (sampleFieldTrilinear(lbmVorticityMag, worldPos, true, &v)) return v;
    return 0.0f;
}

float getLBMQWorld(const glm::vec3& worldPos) {
    if (!lbmInitialized || lbmQCriterion.empty()) return 0.0f;
    float v = 0.0f;
    if (sampleFieldTrilinear(lbmQCriterion, worldPos, true, &v)) return v;
    return 0.0f;
}

float getLBMTKEWorld(const glm::vec3& worldPos) {
    if (!lbmInitialized || lbmTKEField.empty()) return 0.0f;
    float v = 0.0f;
    if (sampleFieldTrilinear(lbmTKEField, worldPos, true, &v)) return v;
    return 0.0f;
}

float getLBMVelocityMagWorld(const glm::vec3& worldPos) {
    glm::vec3 v = getLBMVelocityWorld(worldPos);
    float m = glm::length(v);
    return std::isfinite(m) ? m : 0.0f;
}

float getLBMStrainWorld(const glm::vec3& worldPos) {
    if (!lbmInitialized || lbmStrainMag.empty()) return 0.0f;
    float v = 0.0f;
    if (sampleFieldTrilinear(lbmStrainMag, worldPos, true, &v)) return v;
    return 0.0f;
}

void computeLBMVorticityAndQ() {
    if (!lbmInitialized) return;
    int total = lbmNx * lbmNy * lbmNz;
    if (total <= 0) return;
    const int Nx = lbmNx, Ny = lbmNy, Nz = lbmNz;
    const float* uxW = lbmUxWorld.data();
    const float* uyW = lbmUyWorld.data();
    const float* uzW = lbmUzWorld.data();
    float* vortPtr = lbmVorticityMag.data();
    float* qPtr = lbmQCriterion.data();
    float* strainPtr = lbmStrainMag.data();
    // ИСПРАВЛЕНО: здесь брался ещё и float* tkePtr = lbmTKEField.data(), и в конце
    // цикла постобработка писала в него
    //     tke = 0.5f*vortMag*vortMag*0.01f + S2*0.005f;   if (tke > 10) tke = 10;
    // то есть ПЕРЕЗАПИСЫВАЛА подсеточную TKE, которую честно считает решатель
    // (модель Смагоринского, м2/с2), эвристикой от вихря и тензора скоростей
    // деформации. Единицы у них разные: |ω| и S — это 1/с и 1/с2, коэффициенты
    // 0.01 и 0.005 подобраны произвольно, потолок 10 м2/с2 отрезал реальную
    // турбулентность, а вызов каждые 30 шагов подменял поле между кадрами.
    // Постобработка теперь отвечает только за свои производные (вихрь, Q,
    // деформация) и поле TKE не трогает: смысл и единицы — в комментарии lbm.h.
    const char* solidPtr = lbmIsSolid.data();
    const char* groundPtr = lbmIsGround.data();
    float csx = lbmCellSizeX, csy = lbmCellSizeY, csz = lbmCellSizeZ;
    if (csx < 1e-6f) csx = 0.1f;
    if (csy < 1e-6f) csy = 0.1f;
    if (csz < 1e-6f) csz = 0.1f;
    float invCsx = 1.0f / csx, invCsy = 1.0f / csy, invCsz = 1.0f / csz;

    #ifdef _OPENMP
    #pragma omp parallel for
    #endif
    for (int z = 1; z < Nz-1; ++z) {
        for (int y = 1; y < Ny-1; ++y) {
            for (int x = 1; x < Nx-1; ++x) {
                int cell = (z*Ny + y)*Nx + x;
                if (solidPtr[cell] || groundPtr[cell]) { vortPtr[cell]=0; qPtr[cell]=0; continue; }

                int xm = cell-1, xp = cell+1;
                int ym = cell-Nx, yp = cell+Nx;
                int zm = cell-Nx*Ny, zp = cell+Nx*Ny;

                if (xm <0 || xp>=total || ym<0 || yp>=total || zm<0 || zp>=total) {
                    vortPtr[cell]=0; qPtr[cell]=0; continue;
                }

                float dux_dx = (uxW[xp] - uxW[xm]) * 0.5f * invCsx;
                float dux_dy = (uxW[yp] - uxW[ym]) * 0.5f * invCsy;
                float dux_dz = (uxW[zp] - uxW[zm]) * 0.5f * invCsz;
                float duy_dx = (uyW[xp] - uyW[xm]) * 0.5f * invCsx;
                float duy_dy = (uyW[yp] - uyW[ym]) * 0.5f * invCsy;
                float duy_dz = (uyW[zp] - uyW[zm]) * 0.5f * invCsz;
                float duz_dx = (uzW[xp] - uzW[xm]) * 0.5f * invCsx;
                float duz_dy = (uzW[yp] - uzW[ym]) * 0.5f * invCsy;
                float duz_dz = (uzW[zp] - uzW[zm]) * 0.5f * invCsz;

                if (!std::isfinite(dux_dx)) { vortPtr[cell]=0; qPtr[cell]=0; continue; }

                float wx = duz_dy - duy_dz;
                float wy = dux_dz - duz_dx;
                float wz = duy_dx - dux_dy;
                float vortMag = std::sqrt(wx*wx + wy*wy + wz*wz);
                if (!std::isfinite(vortMag)) vortMag = 0;
                if (vortMag > 1000.0f) vortMag = 1000.0f;
                vortPtr[cell] = vortMag;

                float Sxx = dux_dx, Syy = duy_dy, Szz = duz_dz;
                float Sxy = 0.5f*(dux_dy + duy_dx);
                float Sxz = 0.5f*(dux_dz + duz_dx);
                float Syz = 0.5f*(duy_dz + duz_dy);
                float Oxy = 0.5f*(dux_dy - duy_dx);
                float Oxz = 0.5f*(dux_dz - duz_dx);
                float Oyz = 0.5f*(duy_dz - duz_dy);

                float S2 = Sxx*Sxx + Syy*Syy + Szz*Szz + 2.0f*(Sxy*Sxy + Sxz*Sxz + Syz*Syz);
                float O2 = 2.0f*(Oxy*Oxy + Oxz*Oxz + Oyz*Oyz);
                float Q = 0.5f*(O2 - S2);
                if (!std::isfinite(Q)) Q = 0;
                if (Q > 1e6f) Q = 1e6f;
                if (Q < -1e6f) Q = -1e6f;
                qPtr[cell] = Q;
                float strain = std::sqrt(2.0f*S2);
                if (!std::isfinite(strain)) strain = 0;
                strainPtr[cell] = strain;
                // TKE здесь намеренно не считается: см. комментарий выше.
            }
        }
    }
}

float computeLBMRe() {
    // v1.27.0 — вязкость по Сазерленду, как в forces.cpp и flow_field.cpp.
    // Здесь была зашита константа 1.81e-5 (воздух при ~293 K), а результат
    // перезаписывал aeroReNumber, поэтому на высоте, где температура и плотность
    // другие, число Рейнольдса в панели и в CSV не совпадало с тем, по которому
    // считались коэффициенты трения.
    // Та же формула, что и в initLBM: airSutherlandViscosity() принимает кельвины.
    float mu = airSutherlandViscosity(airTemperature);
    float L = maxDim;
    if (L < 1e-6f) L = 1.0f;
    float Re = airDensity * flowSpeed * L / mu;
    if (!std::isfinite(Re)) Re = aeroReNumber;
    if (Re > 1e9f) Re = 1e9f;
    return Re;
}

float computeLBMRefArea() {
    if (aeroAutoRefArea) {
        // v1.11.0: проекционная площадь на плоскость перпендикулярную потоку
        float az = glm::radians(flowAzimuth);
        float el = glm::radians(flowElevation);
        float ce = cosf(el), se = sinf(el);
        float ca = cosf(az), sa = sinf(az);
        glm::vec3 flowDir(ce*ca, se, ce*sa);
        float len = glm::length(flowDir);
        if (len > 1e-6f) flowDir /= len;
        else flowDir = glm::vec3(1,0,0);

        // Если есть треугольники — считаем проекцию
        if (g_vertices.size() >= 9) {
            float projArea = 0.0f;
            int triCount = (int)(g_vertices.size() / 9);
            float* vertPtr = g_vertices.data();
            for (int ti=0; ti<triCount; ++ti) {
                int i = ti*9;
                if (i+8 >= (int)g_vertices.size()) continue;
                glm::vec3 v0(vertPtr[i], vertPtr[i+1], vertPtr[i+2]);
                glm::vec3 v1(vertPtr[i+3], vertPtr[i+4], vertPtr[i+5]);
                glm::vec3 v2(vertPtr[i+6], vertPtr[i+7], vertPtr[i+8]);
                glm::vec3 cr = glm::cross(v1-v0, v2-v0);
                float area = 0.5f * glm::length(cr);
                if (!std::isfinite(area) || area < 1e-12f) continue;
                glm::vec3 n = glm::normalize(cr);
                // Проекция на направление потока — только передняя часть
                float dot = fabsf(glm::dot(n, flowDir));
                // Для замкнутого тела фронтальная площадь = 0.5 * sum(area*|dot|)
                projArea += area * dot;
            }
            float frontal = projArea * 0.5f;
            if (std::isfinite(frontal) && frontal > 1e-6f && frontal < 1e6f) {
                return frontal;
            }
        }

        // Fallback — bbox проекция
        float sizeY = maxBB.y - minBB.y;
        float sizeZ = maxBB.z - minBB.z;
        float sizeX = maxBB.x - minBB.x;
        if (!std::isfinite(sizeY) || sizeY < 0.01f) sizeY = maxDim;
        if (!std::isfinite(sizeZ) || sizeZ < 0.01f) sizeZ = maxDim;
        if (!std::isfinite(sizeX) || sizeX < 0.01f) sizeX = maxDim;
        // Проекция bbox на плоскость перпендикулярную потоку — упрощенно
        float area = (fabsf(flowDir.x)*sizeY*sizeZ + fabsf(flowDir.y)*sizeX*sizeZ + fabsf(flowDir.z)*sizeX*sizeY);
        if (!std::isfinite(area) || area < 1e-6f) area = maxDim*maxDim*0.5f;
        return area;
    }
    if (!std::isfinite(aeroRefArea) || aeroRefArea < 1e-6f) return 1.0f;
    return aeroRefArea;
}

// =====================================================
// v1.35.0 — СИЛА НА ТЕЛО ПО БАЛАНСУ ИМПУЛЬСА В КОНТРОЛЬНОМ ОБЪЁМЕ
//
// Зачем именно так. Прямолинейный метод обмена импульсом (Σ 2 f_q c_q по
// граничным связям) в этой схеме дал Cd в 10 раз больше баланса импульса:
// у ступенчатой поверхности сфера имеет тысячи «диагональных» связей, и сумма
// по ним не сходится к силе. Поэтому сила считается по закону сохранения:
// для стационарного обтекания
//     F_x = -∮ (ρ u_x u_n + p n_x) dA
// по замкнутой поверхности вокруг тела (p = cs² (ρ - 1), решёточные единицы).
// Это не эмпирика и не подгонка: тот же интеграл, что в аэродинамическом
// эксперименте по измерению профиля импульса в следе.
//
// ВАЖНО: метод требует установившегося течения (нестационарные члены не
// учитываются) и того, чтобы контрольная поверхность не пересекала тело.
// Он используется для валидации; в интерфейсе сила по-прежнему берётся из
// поля давления на поверхности (forces.cpp).
// =====================================================
glm::vec3 lbmControlVolumeForce(float boxHalfX, float boxHalfY, float boxHalfZ) {
    if (!lbmInitialized) return glm::vec3(0.0f);
    const size_t need = (size_t)lbmNx * (size_t)lbmNy * (size_t)lbmNz;
    if (lbmRho.size() != need) return glm::vec3(0.0f);

    auto clampSide = [](int v, int n) { return v < 1 ? 1 : (v > n - 2 ? n - 2 : v); };
    const int cx = (int)(lbmNx * 0.5f);
    const int cy = (int)(lbmNy * 0.5f);
    const int cz = (int)(lbmNz * 0.5f);
    const int hx = std::max(2, (int)(lbmNx * boxHalfX));
    const int hy = std::max(2, (int)(lbmNy * boxHalfY));
    const int hz = std::max(2, (int)(lbmNz * boxHalfZ));
    const int x0 = clampSide(cx - hx, lbmNx), x1 = clampSide(cx + hx, lbmNx);
    const int y0 = clampSide(cy - hy, lbmNy), y1 = clampSide(cy + hy, lbmNy);
    const int z0 = clampSide(cz - hz, lbmNz), z1 = clampSide(cz + hz, lbmNz);

    double fx = 0.0, fy = 0.0, fz = 0.0;
    // n — ВНЕШНЯЯ нормаль грани, знак уже в ней: домножать на направление
    // грани ещё раз нельзя (иначе давление на входной грани складывается с
    // давлением на выходной вместо вычитания — так был получен Cd = -39).
    auto add = [&](int cell, float nx, float ny, float nz) {
        if (cell < 0 || (size_t)cell >= need) return;
        if (lbmIsSolid[cell]) return;
        const float rho = lbmRho[cell];
        const float ux = lbmUx[cell], uy = lbmUy[cell], uz = lbmUz[cell];
        const float p = (1.0f / 3.0f) * (rho - 1.0f);
        const float un = ux * nx + uy * ny + uz * nz;
        fx += p * nx + rho * ux * un;
        fy += p * ny + rho * uy * un;
        fz += p * nz + rho * uz * un;
    };
    // Внешняя нормаль контрольного объёма: на дальней грани +, на ближней −.
    for (int z = z0; z <= z1; ++z)
        for (int y = y0; y <= y1; ++y) {
            add((z * lbmNy + y) * lbmNx + x0, -1.0f, 0.0f, 0.0f);
            add((z * lbmNy + y) * lbmNx + x1, +1.0f, 0.0f, 0.0f);
        }
    for (int z = z0; z <= z1; ++z)
        for (int x = x0; x <= x1; ++x) {
            add((z * lbmNy + y0) * lbmNx + x, 0.0f, -1.0f, 0.0f);
            add((z * lbmNy + y1) * lbmNx + x, 0.0f, +1.0f, 0.0f);
        }
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x) {
            add((z0 * lbmNy + y) * lbmNx + x, 0.0f, 0.0f, -1.0f);
            add((z1 * lbmNy + y) * lbmNx + x, 0.0f, 0.0f, +1.0f);
        }
    // Сила на тело = минус поток импульса через контрольную поверхность.
    return glm::vec3((float)(-fx), (float)(-fy), (float)(-fz));
}

// Проекция твёрдого тела на плоскость, перпендикулярную оси axis (в ячейках):
// число уникальных пар индексов вдоль двух других осей, где есть твёрдая ячейка.
float lbmProjectedAreaCells(int axis) {
    if (!lbmInitialized) return 0.0f;
    const size_t need = (size_t)lbmNx * (size_t)lbmNy * (size_t)lbmNz;
    if (lbmIsSolid.size() != need) return 0.0f;
    const char* solid = lbmIsSolid.data();
    std::vector<char> hit(need, 0);
    double area = 0.0;
    for (int z = 0; z < lbmNz; ++z)
        for (int y = 0; y < lbmNy; ++y)
            for (int x = 0; x < lbmNx; ++x) {
                const int cell = (z * lbmNy + y) * lbmNx + x;
                if (!solid[cell]) continue;
                int a, b, aMax;
                if (axis == 0)      { a = y; b = z; aMax = lbmNy; }
                else if (axis == 1) { a = x; b = z; aMax = lbmNx; }
                else                { a = x; b = y; aMax = lbmNx; }
                const size_t k = (size_t)b * (size_t)aMax + (size_t)a;
                if (k < hit.size() && !hit[k]) { hit[k] = 1; area += 1.0; }
            }
    return (float)area;
}

void computeLBMForcesFromLBM() {
    lbmReynolds = computeLBMRe();
    aeroReNumber = lbmReynolds;
}

bool lbmValidateInitialization() {
    if (!lbmInitialized) return false;
    if (lbmNx <= 0 || lbmNy <= 0 || lbmNz <= 0) return false;
    int total = lbmNx*lbmNy*lbmNz;
    if ((int)lbmRho.size() != total) return false;
    if ((int)f.size() != total*Q) return false;
    for (float rho : lbmRho) if (!std::isfinite(rho) || rho < 0.5f || rho > 1.5f) return false;
    return true;
}
bool lbmValidateConservation() {
    if (!lbmInitialized) return true;
    double avg = 0;
    int count = 0;
    for (float r : lbmRho) if (std::isfinite(r)) { avg += r; count++; }
    if (count == 0) return false;
    avg /= count;
    return std::fabs(avg - 1.0f) <= 0.1f; // строже — 10%
}
bool lbmValidateBoundaryConditions() {
    if (!lbmInitialized) return true;
    float inUx, inUy, inUz;
    computeInletVelocityLB(inUx, inUy, inUz);
    float mag = std::sqrt(inUx*inUx + inUy*inUy + inUz*inUz);
    return std::isfinite(mag) && mag >= 1e-6f && mag <= 0.2f;
}
bool lbmValidateSolidHandling() {
    if (!lbmInitialized) return true;
    for (size_t i = 0; i < lbmIsSolid.size(); ++i) {
        if (lbmIsSolid[i]) {
            if (std::fabs(lbmUx[i]) > 1e-4f || std::fabs(lbmUy[i]) > 1e-4f || std::fabs(lbmUz[i]) > 1e-4f) return false;
        }
    }
    return true;
}
bool lbmValidateRealisticAero() {
    if (!lbmInitialized) return true;
    float maxP = -1e9f, minP = 1e9f;
    for (float p : lbmPressure) {
        if (!std::isfinite(p)) return false;
        if (p > maxP) maxP = p;
        if (p < minP) minP = p;
    }
    float q = 0.5f * airDensity * flowSpeed*flowSpeed;
    if (fabsf(maxP) > q*1.5f || fabsf(minP) > q*1.5f) return false;
    for (float tke : lbmTKEField) if (tke < -1e-3f || !std::isfinite(tke) || tke > 10.0f) return false;
    for (float v : lbmVorticityMag) if (v < -1e-3f || !std::isfinite(v) || v > 1e4f) return false;
    return true;
}
bool lbmValidateStability() {
    if (!lbmInitialized) return true;
    if (!std::isfinite(lbmAvgRho) || lbmAvgRho < 0.8f || lbmAvgRho > 1.2f) return false;
    if (!std::isfinite(lbmMaxVelocityLB) || lbmMaxVelocityLB > 0.35f) return false;
    if (!std::isfinite(lbmAvgKineticEnergy) || lbmAvgKineticEnergy > 0.1f) return false;
    return true;
}

float getLBMMachWorld(const glm::vec3& worldPos) {
    glm::vec3 v = getLBMVelocityWorld(worldPos);
    float mag = glm::length(v);
    if (!std::isfinite(mag)) return 0.0f;
    float a = speedOfSound;
    if (a < 1.0f) a = 340.0f;
    return mag / a;
}

float getLBMHelicityWorld(const glm::vec3& worldPos) {
    if (!lbmInitialized) return 0.0f;
    float fx = (worldPos.x - lbmMinX) / lbmCellSizeX;
    float fy = (worldPos.y - lbmMinY) / lbmCellSizeY;
    float fz = (worldPos.z - lbmMinZ) / lbmCellSizeZ;
    int ix = (int)std::floor(fx), iy = (int)std::floor(fy), iz = (int)std::floor(fz);
    if (ix <= 0 || ix >= lbmNx-1 || iy <= 0 || iy >= lbmNy-1 || iz <= 0 || iz >= lbmNz-1) return 0.0f;
    int cell = (iz*lbmNy + iy)*lbmNx + ix;
    if (cell < 0 || cell >= (int)lbmVorticityMag.size()) return 0.0f;
    glm::vec3 vel = getLBMVelocityWorld(worldPos);
    int xm = cell-1, xp = cell+1;
    int ym = cell-lbmNx, yp = cell+lbmNx;
    int zm = cell-lbmNx*lbmNy, zp = cell+lbmNx*lbmNy;
    int total = lbmNx*lbmNy*lbmNz;
    if (xm<0||xp>=total||ym<0||yp>=total||zm<0||zp>=total) return 0.0f;
    float duz_dy = (lbmUzWorld[yp] - lbmUzWorld[ym]) * 0.5f;
    float duy_dz = (lbmUyWorld[zp] - lbmUyWorld[zm]) * 0.5f;
    float dux_dz = (lbmUxWorld[zp] - lbmUxWorld[zm]) * 0.5f;
    float duz_dx = (lbmUzWorld[xp] - lbmUzWorld[xm]) * 0.5f;
    float duy_dx = (lbmUyWorld[xp] - lbmUyWorld[xm]) * 0.5f;
    float dux_dy = (lbmUxWorld[yp] - lbmUxWorld[ym]) * 0.5f;
    glm::vec3 omega(duz_dy - duy_dz, dux_dz - duz_dx, duy_dx - dux_dy);
    float helicity = glm::dot(vel, omega);
    if (!std::isfinite(helicity)) return 0.0f;
    float vMag = glm::length(vel);
    float oMag = glm::length(omega);
    if (vMag < 1e-6f || oMag < 1e-6f) return 0.0f;
    return helicity / (vMag * oMag);
}

float getLBMTotalPressureWorld(const glm::vec3& worldPos) {
    glm::vec3 v = getLBMVelocityWorld(worldPos);
    float mag2 = glm::dot(v,v);
    if (!std::isfinite(mag2)) return airPressure;
    float rho = airDensity;
    float pStatic = airPressure;
    // v1.11.0 fix: скобки для тернарного оператора
    // v1.26.0: то же трилинейное давление, что и у карты, иначе полное давление
    // «прыгало» на границах ячеек
    float lbmP = 0.0f;
    getLBMPressureWorldChecked(worldPos, &lbmP);
    pStatic += lbmP;
    float pt = pStatic + 0.5f * rho * mag2;
    return std::isfinite(pt) ? pt : airPressure;
}

void drawLBMUI() {}
