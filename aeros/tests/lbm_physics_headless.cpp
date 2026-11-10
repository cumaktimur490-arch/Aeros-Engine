// Headless-прогон решателя LBM: проверка сохранения массы, устойчивости,
// физического смысла TKE и того, что модель Смагоринского действительно влияет
// на расчёт (до v1.27.0 её вклад был ~1e-5, то есть она не работала).
//
// v1.30.0 — добавлены проверки на исправленные ошибки физики:
//   1) вязкость по Сазерленду получает температуру в кельвинах (в initLBM к уже
//      кельвиновому airTemperature прибавлялось 273.15, вязкость выходила на 61%
//      больше, а Re — в 1.6 раза меньше);
//   2) равновесные популяции сохраняют массу и импульс при любой плотности —
//      раньше rho молча обрезалась до [0.8; 1.2] и sum(feq) != rho;
//   3) физическое давление не обрезается по скоростному напору (проверка
//      линейности p(rho) вместо прежнего «|p| <= 1.05*q»);
//   4) источник скорости для частиц выбирает отдельная тестируемая функция;
//   5) постобработка вихрь/Q/деформация не перезаписывает TKE решателя;
//   6) не реализованные настройки (MRT / Zou-He / регуляризация) перечисляются
//      в диагностике и действительно не влияют на решение.
#include "globals.h"
#include "lbm.h"
#include "atmosphere.h"
#include "voxel_grid.h"
#include "flow_field.h"
#include "potential_flow.h"
#include "debug_log.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <vector>

static int g_failed = 0;
static int g_checks = 0;
static void check(bool cond, const char* what) {
    std::printf("  %s  %s\n", cond ? "ok:  " : "FAIL:", what);
    ++g_checks;
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
    std::printf("=== headless-прогон LBM (v1.34.0) ===\n");

    flowSpeed = 20.0f; flowAzimuth = 0.0f; flowElevation = 0.0f;
    altitude = 0.0f; timeScale = 1.0f;
    aeroMachEffects = false;
    lbmParams.enabled = true;
    lbmParams.tau = 0.55f;
    // v1.34.0 — фиксируем tau пользователя: проверки ниже относятся к решателю,
    // а не к выбору вязкости. Авто-вязкость из Re проверяется отдельным тестом.
    lbmParams.autoViscosity = false;
    lbmParams.U0 = 0.08f;
    lbmParams.stepsPerFrame = 1;
    lbmParams.smagorinskyC = 0.14f;
    lbmParams.useGround = false;
    // v1.34.0: полный внешний домен (2L/4L/2.5L) для этого теста — это 3.5 млн
    // ячеек вместо 14 тыс. и минуты счёта на каждый прогон. Для проверок самого
    // решателя масштаб уменьшен, а размеры домена проверяются отдельным тестом
    // ниже с полным масштабом (без шагов счёта).
    lbmParams.domainPadding = 0.25f;
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
    // ИСПРАВЛЕНО: прежняя проверка требовала |p| <= 1.05*q и тем самым защищала
    // обрезку ФИЗИЧЕСКОГО поля давления по скоростному напору (lbm.cpp:
    // `if (fabsf(pPhys) > q) pPhys = clamp(pPhys, -q, q)`). Тест теперь проверяет
    // физическую согласованность и конечность, а не искусственный потолок.
    check(std::isfinite(pmin) && std::isfinite(pmax), "мин/макс давления конечны");
    check(pmin < 0.0f, "в поле есть разрежение (p < 0)");
    check(pmax > 0.0f, "в поле есть подпор (p > 0)");
    check(pmin > -50.0f*q && pmax < 50.0f*q, "давление в разумных физических пределах");
    check(lbmPressureNonFinite == 0, "неконечных значений давления за шаг нет");
    {
        // Если бы поле резали по +-q, экстремальные ячейки навалились бы ровно на
        // потолок: считаем, сколько значений совпадает с q с точностью до 1e-3.
        int atCeiling = 0;
        for (size_t k = 0; k < lbmPressure.size(); ++k) {
            if (lbmIsSolid[k] || lbmIsGround[k]) continue;
            if (std::fabs(std::fabs(lbmPressure[k]) - q) < 1e-3f) atCeiling++;
        }
        char nm[160];
        std::snprintf(nm, sizeof(nm),
                      "давление не свалено на потолок +-%.1f Па (ячеек на потолке: %d)", q, atCeiling);
        check(atCeiling == 0, nm);
    }
    {
        // p = rho0*cs2*scale^2*(rho-1) — линейная функция плотности с ОДНИМ
        // коэффициентом на всё поле. Обрезка по диапазону сломала бы линейность.
        int iA = -1, iB = -1, iC = -1;
        float rA = -1e30f, rB = 1e30f, rC = -1e30f;
        for (size_t k = 0; k < lbmRho.size(); ++k) {
            if (lbmIsSolid[k] || lbmIsGround[k]) continue;
            if (!std::isfinite(lbmRho[k])) continue;
            if (lbmRho[k] > rA) { rA = lbmRho[k]; iA = (int)k; }
            if (lbmRho[k] < rB) { rB = lbmRho[k]; iB = (int)k; }
        }
        // третья точка — с плотностью посередине
        for (size_t k = 0; k < lbmRho.size(); ++k) {
            if ((int)k == iA || (int)k == iB) continue;
            if (lbmIsSolid[k] || lbmIsGround[k] || !std::isfinite(lbmRho[k])) continue;
            float mid = std::fabs(lbmRho[k] - 0.5f*(rA + rB));
            if (iC < 0 || mid < rC) { rC = mid; iC = (int)k; }
        }
        bool found = (iA >= 0 && iB >= 0 && iC >= 0 && std::fabs(rA - rB) > 1e-6f);
        check(found, "есть три ячейки с разной плотностью для проверки p(rho)");
        if (found) {
            float k1 = (lbmPressure[iA] - lbmPressure[iB]) / (lbmRho[iA] - lbmRho[iB]);
            float k2 = (lbmPressure[iA] - lbmPressure[iC]) / (lbmRho[iA] - lbmRho[iC]);
            check(k1 > 0.0f, "рост плотности даёт рост давления (знак коэффициента физический)");
            check(std::fabs(k1 - k2) < 2e-3f * std::fabs(k1),
                  "давление линейно по плотности одним коэффициентом (обрезки по диапазону нет)");
            float pExpect = k1 * (rA - 1.0f) + 0.0f;   // p(rho=1) = 0 по построению
            check(std::fabs(lbmPressure[iA] - pExpect) < 2e-3f * (std::fabs(pExpect) + 1.0f),
                  "сохранённое давление соответствует формуле p ~ (rho - 1)");
        }
    }
    check(lbmRhoOutOfRange == 0, "плотность не выходила за пределы устойчивости [0.5; 1.5]");
    check(lbmRhoNonFinite == 0, "неконечных плотностей нет");
    check(lbmULimited == 0, "ограничитель |u| < 0.3 не срабатывал (режим допустимый)");
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
    // ---- v1.34.0: след за телом внутри домена ---------------------------
    // Раньше это было невозможно проверить: домен кончался на границе бокса
    // (x = +3 при теле радиусом 0.7), и «след» сразу упирался в выходную
    // границу. Теперь есть 4L вниз по потоку, и поле должно восстанавливаться
    // постепенно, а не обрываться.
    {
        const float uNear = glm::length(getLBMVelocityWorld(glm::vec3(1.0f, 0.0f, 0.0f)));
        const float u12   = glm::length(getLBMVelocityWorld(glm::vec3(1.2f, 0.0f, 0.0f)));
        const float u2    = glm::length(getLBMVelocityWorld(glm::vec3(2.0f, 0.0f, 0.0f)));
        const float u3    = glm::length(getLBMVelocityWorld(glm::vec3(3.0f, 0.0f, 0.0f)));
        const float u5    = glm::length(getLBMVelocityWorld(glm::vec3(5.0f, 0.0f, 0.0f)));
        char nm[512];
        std::snprintf(nm, sizeof(nm),
                      "след: за телом скорость %.2f м/с против %.0f в потоке (дефицит)", uNear, flowSpeed);
        check(uNear < 0.5f * flowSpeed, nm);
        std::snprintf(nm, sizeof(nm),
                      "след восстанавливается монотонно: %.2f < %.2f < %.2f < %.2f", u12, u2, u3, u5);
        check(u12 < u2 && u2 < u3 && u3 < u5, nm);
        std::snprintf(nm, sizeof(nm),
                      "на 4L вниз по потоку скорость %.2f м/с стремится к %.0f", u5, flowSpeed);
        check(std::fabs(u5 - flowSpeed) < 0.05f * flowSpeed, nm);
        // Точка, лежавшая вне старого домена (x > 3), теперь имеет данные.
        float pFar = 0.0f;
        check(getLBMPressureWorldChecked(glm::vec3(4.0f, 0.0f, 0.0f), &pFar),
              "точка за прежней границей бокса теперь внутри домена");
        check(!getLBMPressureWorldChecked(glm::vec3(20.0f, 0.0f, 0.0f), &pFar),
              "и всё ещё честно сообщает о выходе за домен");
    }

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

    // =====================================================
    // 1) ВЯЗКОСТЬ ПО САЗЕРЛЕНДУ: температура обязана быть в кельвинах
    // =====================================================
    std::printf("\n--- вязкость по Сазерленду (единицы температуры) ---\n");
    {
        const float mu273 = airSutherlandViscosity(273.15f);
        const float mu288 = airSutherlandViscosity(288.15f);
        const float mu300 = airSutherlandViscosity(300.0f);
        char nm[192];
        std::snprintf(nm, sizeof(nm), "mu(273.15 K) = %.4e Па*с = mu0 (контрольная точка формулы)", mu273);
        check(std::fabs(mu273 - 1.716e-5f) < 1e-9f, nm);
        std::snprintf(nm, sizeof(nm), "mu(288.15 K) = %.4e Па*с (ISA, справочно 1.789e-5)", mu288);
        check(std::fabs(mu288 - 1.7894e-5f) < 2e-8f, nm);
        check(mu300 > mu288 && mu288 > mu273, "вязкость монотонно растёт с температурой");
        // Ошибка была такой: к уже кельвиновой температуре прибавляли 273.15.
        const float muBug = airSutherlandViscosity(288.15f + 273.15f);
        std::snprintf(nm, sizeof(nm),
                      "повторное +273.15 меняет вязкость (%.4e вместо %.4e) — ошибка была бы видна",
                      muBug, mu288);
        check(muBug > mu288 * 1.3f, nm);
        // защита от передачи градусов Цельсия вместо кельвинов
        check(std::fabs(airSutherlandViscosity(15.0f) - mu288) < 1e-9f,
              "15 K не принимается за расчётную температуру: срабатывает защита (ISA)");
        check(std::isfinite(airSutherlandViscosity(NAN)), "NaN на входе не даёт NaN вязкости");
    }
    {
        // Re из initLBM обязан равняться rho*V*L/mu(T[K]) — сквозная проверка единиц
        altitude = 0.0f; updateAtmosphereParams();
        shutdownLBM(); makeSphereCase(0.7f); initLBM();
        const float muRef = airSutherlandViscosity(airTemperature);
        const float reRef = airDensity * flowSpeed * maxDim / muRef;
        char nm[192];
        std::snprintf(nm, sizeof(nm),
                      "initLBM: Re=%.0f совпадает с rho*V*L/mu(T=%.2f K)=%.0f (T=%.2f K, mu=%.4e)",
                      lbmReynolds, airTemperature, reRef, airTemperature, muRef);
        check(std::fabs(lbmReynolds - reRef) < 1e-3f * reRef, nm);
        check(std::fabs(aeroReNumber - lbmReynolds) < 1e-3f * reRef, "aeroReNumber согласован с lbmReynolds");
        // при той же ошибке Re отличался бы в ~1.6 раза
        const float reBug = airDensity * flowSpeed * maxDim / airSutherlandViscosity(airTemperature + 273.15f);
        std::snprintf(nm, sizeof(nm), "Re не равен значению с двойным переводом в кельвины (%.0f)", reBug);
        check(std::fabs(lbmReynolds - reBug) > 0.2f * reRef, nm);

        // и на высоте, где температура другая: связь обязана сохраняться
        altitude = 5000.0f; updateAtmosphereParams();
        shutdownLBM(); makeSphereCase(0.7f); initLBM();
        const float muHi = airSutherlandViscosity(airTemperature);
        const float reHi = airDensity * flowSpeed * maxDim / muHi;
        std::snprintf(nm, sizeof(nm),
                      "на высоте 5000 м: T=%.2f K, mu=%.4e, Re=%.0f (ожидание %.0f)",
                      airTemperature, muHi, lbmReynolds, reHi);
        check(std::fabs(lbmReynolds - reHi) < 1e-3f * reHi, nm);
        check(airTemperature < 280.0f && airTemperature > 200.0f,
              "температура на высоте в кельвинах (255..280 K), а не 0 +/- 20");
        altitude = 0.0f; updateAtmosphereParams();
    }

    // =====================================================
    // 2) РАВНОВЕСНЫЕ ПОПУЛЯЦИИ: масса и импульс при любой плотности
    // =====================================================
    std::printf("\n--- равновесные популяции: sum(feq) = rho, sum(feq*c) = rho*u ---\n");
    {
        struct MomCase { float rho, ux, uy, uz; };
        const MomCase cases[] = {
            { 1.00f,  0.00f,  0.00f,  0.00f },
            { 1.00f,  0.05f,  0.00f,  0.00f },
            { 0.70f,  0.04f, -0.03f,  0.02f },   // НИЖЕ прежнего клампа 0.8
            { 1.35f, -0.02f,  0.05f,  0.01f },   // ВЫШЕ прежнего клампа 1.2
            { 0.55f,  0.10f,  0.10f,  0.00f },
            { 1.60f,  0.00f,  0.00f, -0.08f },
            { 1.10f,  0.28f,  0.00f,  0.00f },   // у предела устойчивости |u| = 0.3
        };
        char nm[192];
        for (const MomCase& cs : cases) {
            float m = 0, px = 0, py = 0, pz = 0;
            const bool ok = lbmEquilibriumMoments(cs.rho, cs.ux, cs.uy, cs.uz, &m, &px, &py, &pz);
            std::snprintf(nm, sizeof(nm), "конечность при rho=%.2f u=(%.2f,%.2f,%.2f)", cs.rho, cs.ux, cs.uy, cs.uz);
            check(ok, nm);
            if (!ok) continue;
            const float tolM = 3e-5f * std::max(1.0f, cs.rho);
            std::snprintf(nm, sizeof(nm), "масса: sum(feq) = %.6f ~ rho = %.3f", m, cs.rho);
            check(std::fabs(m - cs.rho) <= tolM, nm);
            const float tolP = 3e-5f;
            std::snprintf(nm, sizeof(nm), "импульс: sum(feq*c) = (%.5f,%.5f,%.5f) ~ rho*u = (%.5f,%.5f,%.5f)",
                          px, py, pz, cs.rho*cs.ux, cs.rho*cs.uy, cs.rho*cs.uz);
            check(std::fabs(px - cs.rho*cs.ux) <= tolP &&
                  std::fabs(py - cs.rho*cs.uy) <= tolP &&
                  std::fabs(pz - cs.rho*cs.uz) <= tolP, nm);
        }
        // регрессия: плотность больше не подменяется границами [0.8; 1.2]
        float mHi = 0, x = 0, y = 0, z = 0;
        lbmEquilibriumMoments(1.35f, 0.0f, 0.0f, 0.0f, &mHi, &x, &y, &z);
        float mLo = 0;
        lbmEquilibriumMoments(0.70f, 0.0f, 0.0f, 0.0f, &mLo, &x, &y, &z);
        std::snprintf(nm, sizeof(nm), "rho=1.35 даёт массу %.4f, а не 1.2 (прежний потолок)", mHi);
        check(std::fabs(mHi - 1.2f) > 1e-3f && std::fabs(mHi - 1.35f) < 3e-5f, nm);
        std::snprintf(nm, sizeof(nm), "rho=0.70 даёт массу %.4f, а не 0.8 (прежний пол)", mLo);
        check(std::fabs(mLo - 0.8f) > 1e-3f && std::fabs(mLo - 0.70f) < 3e-5f, nm);
        // неконечные входные данные отвергаются, а не подменяются молча
        check(!lbmEquilibriumMoments(1.0f, NAN, 0.0f, 0.0f, &mHi, &x, &y, &z),
              "NaN-скорость отвергается (не считается физической)");
        check(!lbmEquilibriumMoments(NAN, 0.0f, 0.0f, 0.0f, &mHi, &x, &y, &z),
              "NaN-плотность отвергается (не считается физической)");
        check(!lbmEquilibriumMoments(1.0f, 0.0f, 0.0f, 0.0f, nullptr, &x, &y, &z),
              "нулевой указатель результата не разыменовывается");
    }

    // =====================================================
    // 4) ИСТОЧНИК СКОРОСТИ ДЛЯ ЧАСТИЦ выбирается явной функцией
    // =====================================================
    std::printf("\n--- источник поля скорости (частицы, линии тока, цвет) ---\n");
    {
        shutdownLBM(); makeSphereCase(0.7f); initLBM();
        for (int i = 0; i < 30; ++i) stepLBMCPU(1);
        FlowParams prm = flowParams;   // заполнение не важно: источник его не читает
        lbmParams.enabled = true;
        const glm::vec3 inBody(0.0f, 0.0f, 0.0f);      // центр сферы — твёрдое тело
        const glm::vec3 inDomain(1.2f, 0.5f, 0.0f);    // внутри домена, вне тела
        const glm::vec3 outside(50.0f, 50.0f, 50.0f);  // вне домена решателя
        check(aeroVelocitySourceAt(inBody, prm) == AeroVelocitySource::LBMSolid,
              "внутри тела решателя источник — твёрдое тело (v = 0)");
        check(aeroVelocitySourceAt(inDomain, prm) == AeroVelocitySource::LBM,
              "внутри домена источник — поле, посчитанное решателем LBM");
        check(aeroVelocitySourceAt(outside, prm) == AeroVelocitySource::LBMFreestream,
              "вне домена источник — набегающий поток (данных решателя там нет)");
        check(std::strcmp(aeroVelocitySourceName(AeroVelocitySource::LBM), "") != 0,
              "у источника есть человекочитаемое имя для журнала и отчёта");
        // значение скорости берётся из того же источника
        glm::vec3 vIn = computeVelocityFieldCPU(inDomain, prm);
        check(std::isfinite(vIn.x) && std::isfinite(vIn.y) && std::isfinite(vIn.z), "скорость внутри домена конечна");
        check(glm::length(computeVelocityFieldCPU(inBody, prm)) < 1e-6f, "внутри тела скорость равна нулю");
        lbmParams.enabled = false;
        check(aeroVelocitySourceAt(inDomain, prm) == AeroVelocitySource::Analytical,
              "с выключенным LBM источник — аналитическое поле");
        glm::vec3 vAn = computeVelocityFieldCPU(inDomain, prm);
        lbmParams.enabled = true;
        check(std::isfinite(vAn.x) && std::isfinite(vAn.y) && std::isfinite(vAn.z),
              "аналитическое поле тоже конечно (режим без LBM работает)");
        check(glm::length(vIn - vAn) > 1e-6f,
              "поле LBM отличается от аналитического — источник действительно переключается");
    }

    // =====================================================
    // 5) ПОСТОБРАБОТКА НЕ ПЕРЕЗАПИСЫВАЕТ TKE РЕШАТЕЛЯ
    // =====================================================
    std::printf("\n--- TKE: решатель против постобработки ---\n");
    {
        shutdownLBM(); makeSphereCase(0.7f); initLBM();
        lbmParams.useTurbulence = true;
        for (int i = 0; i < 60; ++i) stepLBMCPU(1);   // TKE считает модель Смагоринского
        const std::vector<float> tkeBefore = lbmTKEField;
        std::fill(lbmVorticityMag.begin(), lbmVorticityMag.end(), 0.0f);
        std::fill(lbmQCriterion.begin(), lbmQCriterion.end(), 0.0f);
        std::fill(lbmStrainMag.begin(), lbmStrainMag.end(), 0.0f);
        computeLBMVorticityAndQ();
        size_t diffTke = 0;
        for (size_t k = 0; k < lbmTKEField.size() && k < tkeBefore.size(); ++k)
            if (lbmTKEField[k] != tkeBefore[k]) diffTke++;
        char nm[192];
        std::snprintf(nm, sizeof(nm),
                      "computeLBMVorticityAndQ() не тронула TKE (изменилось ячеек: %zu)", diffTke);
        check(diffTke == 0, nm);
        size_t nz = 0;
        for (size_t k = 0; k < lbmVorticityMag.size(); ++k)
            if (lbmVorticityMag[k] != 0.0f) nz++;
        check(nz > 0, "при этом вихрь пересчитан (функция не стала пустой)");
        size_t nzQ = 0;
        for (size_t k = 0; k < lbmQCriterion.size(); ++k) if (lbmQCriterion[k] != 0.0f) nzQ++;
        check(nzQ > 0, "Q-критерий пересчитан");
        // смысл и единицы: TKE из решателя — м2/с2, неотрицательная
        float tkeMax = 0.0f; size_t tkeNeg = 0, tkeNan = 0;
        for (size_t k = 0; k < lbmTKEField.size(); ++k) {
            float t = lbmTKEField[k];
            if (!std::isfinite(t)) { tkeNan++; continue; }
            if (t < 0.0f) tkeNeg++;
            if (t > tkeMax) tkeMax = t;
        }
        std::snprintf(nm, sizeof(nm), "поле TKE: max=%.3e м2/с2, отрицательных %zu, неконечных %zu",
                      tkeMax, tkeNeg, tkeNan);
        check(tkeNeg == 0 && tkeNan == 0 && tkeMax < 1000.0f, nm);
        // прежняя эвристика 0.5*|w|^2*0.01 + S^2*0.005 с потолком 10 не вернулась
        check(tkeMax != 10.0f, "TKE не упирается в прежний эвристический потолок 10");
    }

    // =====================================================
    // 6) ВИДИМЫЕ НАСТРОЙКИ РЕШАТЕЛЯ РЕАЛЬНО МЕНЯЮТ РЕШЕНИЕ
    // =====================================================
    // v1.36.0 — раздел переписан. Раньше здесь проверялись три флага
    // (useMRT / useZouHeBC / useRegularized), которые были объявлены, но ни
    // разу не читались решателем, — то есть тест закреплял существование
    // мёртвых опций и «отсутствие эффекта» как норму. Флаги удалены; теперь
    // проверяется обратное: у каждой видимой настройки граничных условий есть
    // измеримый эффект, иначе она декоративная.
    std::printf("\n--- настройки граничных условий действительно работают ---\n");
    {
        auto run60 = [&](void (*setup)(), float* rho, float* vmax) {
            shutdownLBM();
            makeSphereCase(0.7f);
            setup();
            initLBM();
            for (int i = 0; i < 60; ++i) stepLBMCPU(1);
            *rho = lbmAvgRho;
            *vmax = lbmMaxVelocityWorld;
        };
        float rhoA = 0, vA = 0, rhoB = 0, vB = 0;
        char nm[256];

        // Конвективный выход против простого bounce-back на выходной плоскости.
        run60([] { lbmParams.useConvectiveOutlet = false; }, &rhoA, &vA);
        run60([] { lbmParams.useConvectiveOutlet = true; },  &rhoB, &vB);
        std::snprintf(nm, sizeof(nm),
                      "конвективный выход меняет поле: rho %.6f против %.6f, vmax %.4f против %.4f",
                      rhoA, rhoB, vA, vB);
        check(std::fabs(rhoA - rhoB) > 1e-7f || std::fabs(vA - vB) > 1e-4f, nm);

        // Земля: слой твёрдых ячеек срезает нижнюю половину модели.
        run60([] { lbmParams.useGround = false; }, &rhoA, &vA);
        run60([] { lbmParams.useGround = true; lbmParams.groundHeight = 0.0f; }, &rhoB, &vB);
        std::snprintf(nm, sizeof(nm),
                      "земля меняет решение: vmax %.4f против %.4f", vA, vB);
        check(std::fabs(vA - vB) > 1e-3f, nm);

        lbmParams.useGround = false;
        lbmParams.useConvectiveOutlet = true;
    }

    // ---- v1.34.0: внешняя область вокруг модели --------------------------
    // До этой версии расчётная область LBM совпадала с воксельным боксом: вход
    // стоял на первой ячейке перед телом, выход — на последней за ним, то есть
    // ни следа, ни восстановления давления за телом не существовало в принципе.
    // Проверяем именно геометрию домена и то, что маска тела не «растянулась».
    {
        shutdownLBM();
        lbmParams.domainPadding = 1.0f;
        makeSphereCase(0.7f);
        initLBM();

        const float Lref = 6.0f;   // габарит бокса [-3..3]
        char nm[512];
        std::snprintf(nm, sizeof(nm),
                      "домен: вход отстоит на %.2f L (нужно 2), выход на %.2f L (нужно 4)",
                      (g_voxMinX - lbmMinX) / Lref, (lbmMaxX - g_voxMaxX) / Lref);
        check(std::fabs((g_voxMinX - lbmMinX) - 2.0f * Lref) < 0.3f &&
              std::fabs((lbmMaxX - g_voxMaxX) - 4.0f * Lref) < 0.3f, nm);

        std::snprintf(nm, sizeof(nm),
                      "домен: по бокам %.2f L с каждой стороны (нужно 2.5)",
                      (g_voxMinY - lbmMinY) / Lref);
        check(std::fabs((g_voxMinY - lbmMinY) - 2.5f * Lref) < 0.3f &&
              std::fabs((lbmMaxY - g_voxMaxY) - 2.5f * Lref) < 0.3f &&
              std::fabs((g_voxMinZ - lbmMinZ) - 2.5f * Lref) < 0.3f, nm);

        // Ключевая регрессия: ячейки объекта не должны масштабироваться на весь
        // домен. Число твёрдых ячеек обязано совпасть с числом твёрдых вокселей
        // (шаг сетки одинаковый и бокс выровнен).
        long voxSolid = 0, lbmSolid = 0;
        for (int i = 0; i < (int)g_voxelData.size(); ++i) if (g_voxelData[i] == 1) ++voxSolid;
        for (int i = 0; i < (int)lbmIsSolid.size(); ++i) if (lbmIsSolid[i]) ++lbmSolid;
        std::snprintf(nm, sizeof(nm),
                      "домен: маска тела сохранена (%ld твёрдых ячеек против %ld вокселей)",
                      lbmSolid, voxSolid);
        check(voxSolid > 0 && lbmSolid == voxSolid, nm);

        std::snprintf(nm, sizeof(nm), "домен: размер %dx%dx%d, ожидался больше исходных 24x24x24",
                      lbmNx, lbmNy, lbmNz);
        check(lbmNx >= 160 && lbmNy >= 140 && lbmNz >= 140, nm);

        const float blockageFull = lbmBlockageRatio;
        std::snprintf(nm, sizeof(nm), "перекрытие домена телом %.2f %% (< 5 %% у внешнего обтекания)",
                      blockageFull * 100.0f);
        check(blockageFull > 0.0f && blockageFull < 0.05f, nm);

        shutdownLBM();
        lbmParams.domainPadding = 0.25f;
        makeSphereCase(0.7f);
        initLBM();
        std::snprintf(nm, sizeof(nm),
                      "при уменьшенном домене перекрытие растёт: %.2f %% > %.2f %%",
                      lbmBlockageRatio * 100.0f, blockageFull * 100.0f);
        check(lbmBlockageRatio > blockageFull, nm);
    }

    // ---- v1.34.0: аналитический потенциал эллипсоида --------------------
    // Раньше здесь не было ни одной проверки физики поля: тесты смотрели только
    // на LBM. Между тем в flow_field.cpp возмущение складывалось с набегающим
    // потоком (знак дублета перевёрнут), из-за чего на поверхности сферы
    // нормальная скорость равнялась 2U, а в носовой точке скорость была 2U
    // вместо 0. Условие непротекания — это определение обтекания, и оно
    // проверяется здесь напрямую.
    {
        const float U[3] = {10.0f, 0.0f, 0.0f};
        const float c[3] = {0.0f, 0.0f, 0.0f};
        float v[3];

        // (а) шар: носовая точка торможения u = 0, экватор u = 1.5U.
        //     Сэмплируем чуть снаружи (k = 1.001): ровно на поверхности
        //     функция обязана вернуть покой, это отдельная проверка ниже.
        const float sph[3] = {1.0f, 1.0f, 1.0f};
        const float kOut = 1.001f;
        // Обычные массивы, а не compound literal: `(const float[3]){...}` —
        // расширение C, и GCC 13+ отвергает взятие адреса временного массива.
        const float nose[3] = {-kOut, 0.0f, 0.0f};
        aeros_potential::ellipsoidVelocity(nose, c, sph, U, v);
        char nm[512];
        std::snprintf(nm, sizeof(nm), "потенциал: точка торможения сферы u = %.4f (нужно 0)", v[0]);
        check(std::fabs(v[0]) < 0.02f * 10.0f, nm);
        const float side[3] = {0.0f, kOut, 0.0f};
        aeros_potential::ellipsoidVelocity(side, c, sph, U, v);
        std::snprintf(nm, sizeof(nm), "потенциал: экватор сферы u = %.4f (нужно 15)", v[0]);
        check(std::fabs(v[0] - 15.0f) < 0.05f, nm);

        // (б) непротекание на поверхности: перебираем направления по сфере,
        //     включая вытянутый эллипсоид 4:0.5:0.5, на котором подгоночный
        //     shapeFactor раньше давал до 51 % U нормальной скорости.
        const float shapes[3][3] = {{1.0f, 1.0f, 1.0f}, {4.0f, 0.5f, 0.5f}, {2.0f, 1.0f, 0.3f}};
        const char* names[3] = {"шар 1:1:1", "эллипсоид 4:0.5:0.5", "эллипсоид 2:1:0.3"};
        for (int si = 0; si < 3; ++si) {
            float worst = 0.0f;
            for (int i = 0; i < 200; ++i) {
                const float th = 3.14159265f * (float)i / 199.0f;
                const float ph = 2.39996323f * (float)i;      // золотое сечение по азимуту
                const float dir[3] = {std::sin(th) * std::cos(ph),
                                      std::cos(th),
                                      std::sin(th) * std::sin(ph)};
                float un = aeros_potential::surfaceNormalVelocity(dir, c, shapes[si], U) / 10.0f;
                if (std::fabs(un) > std::fabs(worst)) worst = un;
            }
            std::snprintf(nm, sizeof(nm), "непротекание (%s): max|u·n|/U = %.4f", names[si], std::fabs(worst));
            check(std::fabs(worst) < 0.01f, nm);
        }

        // (в) вдали от тела поле стремится к набегающему потоку
        const float farPt[3] = {0.0f, 400.0f, 0.0f};
        aeros_potential::ellipsoidVelocity(farPt, c, sph, U, v);
        std::snprintf(nm, sizeof(nm), "потенциал: вдали от тела u = %.4f (нужно 10)", v[0]);
        check(std::fabs(v[0] - 10.0f) < 0.01f, nm);

        // (г) внутри тела скорость покоя, а не «минус набегающий поток»
        const float centre[3] = {0.0f, 0.0f, 0.0f};
        aeros_potential::ellipsoidVelocity(centre, c, sph, U, v);
        check(v[0] == 0.0f && v[1] == 0.0f && v[2] == 0.0f, "внутри тела скорость = 0");
    }

    std::printf("\n=== %s (проверок: %d, ошибок: %d) ===\n", g_failed == 0 ? "ФИЗИКА В ПОРЯДКЕ" : "ЕСТЬ ПРОВАЛЫ", g_checks, g_failed);
    return g_failed == 0 ? 0 : 1;
}
