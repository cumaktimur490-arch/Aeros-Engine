// =====================================================
// v1.37.0 — ТЕПЛОПЕРЕНОС И ПЛАВУЧЕСТЬ: ПРОВЕРКА ПО ТОЧНЫМ РЕШЕНИЯМ
//
// Модуль thermal_lbm.{h,cpp} считает отдельный скаляр (температуру) на D3Q7
// и возвращает в решатель силу плавучести по Буссинеску. Здесь проверяется
// не «красиво выглядит», а совпадение с решениями, которые можно выписать
// формулой:
//
//   1) чистая диффузия: T = A exp(-alpha k^2 t) cos(kx);
//   2) диффузия + равномерный снос: фаза движется со скоростью u, амплитуда
//      затухает с тем же alpha;
//   3) установившаяся проводимость: линейный профиль между стенками;
//   4) порог конвекции Рэлея–Бенара: при Ra ниже 1707.76 возмущение гаснет,
//      выше — растёт (классическое значение для жёстких границ).
//
// Всё печатается как есть; допуски выбраны по измеренному, а не подогнаны.
// =====================================================
#include "globals.h"
#include "lbm.h"
#include "thermal_lbm.h"
#include "atmosphere.h"
#include "voxel_grid.h"
#include "debug_log.h"
#include "version.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>
#include <algorithm>

static int g_checks = 0, g_failed = 0;
static void check(bool cond, const char* what) {
    std::printf("  %s  %s\n", cond ? "ok:  " : "FAIL:", what);
    ++g_checks;
    if (!cond) ++g_failed;
}
static void checkf(bool cond, const char* fmt, double a, double b) {
    char buf[512];
    std::snprintf(buf, sizeof(buf), fmt, a, b);
    check(cond, buf);
}

extern "C" double glfwGetTime(void) { static double t = 0.0; return (t += 1.0/60.0); }
extern "C" void setVoxelData(const int*, const float*, int, int, int, float, float, float, float, float, float) {}
extern "C" int  cudaConsumeError(char* buf, int sz) { if (buf && sz > 0) buf[0] = 0; return 0; }
void updateVertexColors() {}
void computeLiftDrag() {}
void drawModel() {}

// ---------------------------------------------------------------------------
// Общий сет-ап: коробка Nx x Ny x Nz, при желании — твёрдые стенки по Y.
// ---------------------------------------------------------------------------
struct Setup {
    int nx = 32, ny = 16, nz = 8;
    bool wallsY = false;      // стенки y=0 и y=Ny-1 (твёрдые воксели)
    float u0 = 0.0f;          // равномерная скорость вдоль X
    float tau = 0.65f;
    bool periodicZ = true;
};

static void setupBox(const Setup& s) {
    g_voxNx = s.nx; g_voxNy = s.ny; g_voxNz = s.nz;
    g_voxMinX = g_voxMinY = g_voxMinZ = -1.0f;
    g_voxMaxX = g_voxMaxY = g_voxMaxZ =  1.0f;
    g_voxelData.assign((size_t)s.nx*s.ny*s.nz, 0);
    g_distanceField.assign((size_t)s.nx*s.ny*s.nz, 0.0f);
    if (s.wallsY) {
        for (int z = 0; z < s.nz; ++z)
            for (int x = 0; x < s.nx; ++x) {
                g_voxelData[(size_t)(z*s.ny + 0)*s.nx + x] = 1;
                g_voxelData[(size_t)(z*s.ny + (s.ny-1))*s.nx + x] = 1;
            }
    }
    minBB = glm::vec3(-1.0f); maxBB = glm::vec3(1.0f);
    center = glm::vec3(0.0f); maxDim = 2.0f;
    voxelResolution = s.nx;

    flowSpeed = 1.0f; flowAzimuth = 0.0f; flowElevation = 0.0f;
    altitude = 0.0f; timeScale = 1.0f; aeroMachEffects = false;

    lbmParams.enabled = true;
    lbmParams.useGPU = false;
    lbmParams.U0 = s.u0;
    lbmParams.stepsPerFrame = 1;
    lbmParams.tau = s.tau;
    lbmParams.useTurbulence = false;
    lbmParams.useGround = false;
    lbmParams.inletTurbulence = 0.0f;
    lbmParams.domainPadding = 0.0f;
    lbmParams.autoViscosity = false;
    lbmParams.useConvectiveOutlet = false;
    lbmParams.periodicX = true;          // коробка замкнута по потоку
    lbmParams.periodicZ = s.periodicZ;
    lbmParams.bodyForceX = lbmParams.bodyForceY = lbmParams.bodyForceZ = 0.0f;

    shutdownLBM();
    initLBM();
}

static void runSteps(int n) {
    for (int i = 0; i < n; ++i) stepLBMCPU(1);
}

// Амплитуда первой гармоники по X: |sum T exp(-i k x)| * 2 / N.
static double harmonicX(int y, int z) {
    const double k = 2.0 * M_PI / (double)lbmNx;
    double re = 0.0, im = 0.0;
    for (int x = 0; x < lbmNx; ++x) {
        const size_t c = ((size_t)z*lbmNy + y)*lbmNx + x;
        const double a = k * x;
        re += thermalT[c] * std::cos(a);
        im -= thermalT[c] * std::sin(a);
    }
    return 2.0 * std::sqrt(re*re + im*im) / (double)lbmNx;
}

static double harmonicPhaseX(int y, int z) {
    const double k = 2.0 * M_PI / (double)lbmNx;
    double re = 0.0, im = 0.0;
    for (int x = 0; x < lbmNx; ++x) {
        const size_t c = ((size_t)z*lbmNy + y)*lbmNx + x;
        const double a = k * x;
        re += thermalT[c] * std::cos(a);
        im -= thermalT[c] * std::sin(a);
    }
    return std::atan2(im, re);
}

// RMS отклонения от профиля проводимости (для Рэлея–Бенара).
static double perturbationRMS() {
    const double Th = thermalParams.hotTemp, Tc = thermalParams.coldTemp;
    const double span = (double)(lbmNy - 2);
    double acc = 0.0; long n = 0;
    for (int z = 0; z < lbmNz; ++z)
        for (int y = 1; y < lbmNy-1; ++y)
            for (int x = 0; x < lbmNx; ++x) {
                const size_t c = ((size_t)z*lbmNy + y)*lbmNx + x;
                if (lbmIsSolid[c]) continue;
                const double s = ((double)y - 0.5) / std::max(1.0, span);
                const double lin = Th + (Tc - Th) * s;
                const double d = thermalT[c] - lin;
                acc += d*d; ++n;
            }
    return n ? std::sqrt(acc / (double)n) : 0.0;
}

int main() {
    debugLogInit("/tmp/tl/thermal");
    std::printf("=== тепло и плавучесть (%s) ===\n", AEROS_VERSION_STRING);
    std::printf("D3Q7, alpha = cs^2 (tau_T - 1/2), сила плавучести по Буссинеску\n\n");

    // ---------------- 1. Чистая диффузия ----------------
    std::printf("ДИФФУЗИЯ: коробка %dx%dx%d, T = 0.5 + 0.5 cos(2 pi x / L)\n", 32, 16, 8);
    double alphaMeasured = 0.0, alphaExpected = 0.0;
    {
        Setup s; s.nx = 32; s.ny = 16; s.nz = 8; s.u0 = 0.0f;
        thermalParams.enabled = true;
        thermalParams.rayleigh = 0.0f;      // без плавучести: проверяем только диффузию
        thermalParams.prandtl = 1.0f;
        thermalParams.fixedWallTemps = false;
        thermalParams.perturbation = 0.0f;
        setupBox(s);

        std::vector<float> T((size_t)lbmNx*lbmNy*lbmNz, 0.0f);
        const double k = 2.0 * M_PI / (double)lbmNx;
        for (int z = 0; z < lbmNy*0 + lbmNz; ++z)
            for (int y = 0; y < lbmNy; ++y)
                for (int x = 0; x < lbmNx; ++x)
                    T[((size_t)z*lbmNy + y)*lbmNx + x] = (float)(0.5 + 0.5*std::cos(k*x));
        thermalUpload(T);

        const int steps = 1500;
        const double A0 = harmonicX(lbmNy/2, lbmNz/2);
        runSteps(steps);
        const double A1 = harmonicX(lbmNy/2, lbmNz/2);

        alphaExpected = thermalAlpha;
        alphaMeasured = -std::log(std::max(1e-12, A1/A0)) / (k*k*steps);
        std::printf("  A0 = %.6f, A1 = %.6f за %d шагов; alpha измеренная %.6f, заданная %.6f\n",
                    A0, A1, steps, alphaMeasured, alphaExpected);
        checkf(std::fabs(alphaMeasured/alphaExpected - 1.0) < 0.03,
               "диффузия: alpha из затухания совпадает с заданной < 3 %% (%.5f против %.5f)",
               alphaMeasured, alphaExpected);
        shutdownLBM();
    }

    // ---------------- 2. Диффузия + равномерный снос ----------------
    std::printf("\nСНОС: та же волна в равномерном потоке u = 0.05\n");
    {
        Setup s; s.nx = 32; s.ny = 16; s.nz = 8; s.u0 = 0.05f;
        thermalParams.enabled = true;
        thermalParams.rayleigh = 0.0f;
        thermalParams.prandtl = 1.0f;
        thermalParams.fixedWallTemps = false;
        thermalParams.perturbation = 0.0f;
        setupBox(s);

        std::vector<float> T((size_t)lbmNx*lbmNy*lbmNz, 0.0f);
        const double k = 2.0 * M_PI / (double)lbmNx;
        for (int z = 0; z < lbmNz; ++z)
            for (int y = 0; y < lbmNy; ++y)
                for (int x = 0; x < lbmNx; ++x)
                    T[((size_t)z*lbmNy + y)*lbmNx + x] = (float)(0.5 + 0.5*std::cos(k*x));
        thermalUpload(T);

        const int steps = 1200;
        const double A0 = harmonicX(lbmNy/2, lbmNz/2);
        const double ph0 = harmonicPhaseX(lbmNy/2, lbmNz/2);
        runSteps(steps);
        const double A1 = harmonicX(lbmNy/2, lbmNz/2);
        const double ph1 = harmonicPhaseX(lbmNy/2, lbmNz/2);

        double uActual = 0.0;
        {   // средняя скорость по X из поля (а не из настройки)
            double sum = 0.0; long n = 0;
            for (size_t i = 0; i < lbmUx.size(); ++i) {
                if (lbmIsSolid[i]) continue;
                sum += lbmUx[i]; ++n;
            }
            uActual = n ? sum / n : 0.0;
        }
        // Учесть заворачивание: за 1200 шагов волна уходит на u*t = 60 ячеек
        // при длине волны 32, то есть на 1.875 периода. Сравниваем измеренную
        // фазу с ожидаемой, а не «разность фаз», которая читается только по
        // модулю 2 pi.
        double dphi = ph1 - (ph0 - k * uActual * steps);
        while (dphi >  M_PI) dphi -= 2.0*M_PI;
        while (dphi < -M_PI) dphi += 2.0*M_PI;
        const double uMeasured = uActual * (1.0 + dphi / (k * uActual * steps));
        std::printf("  скорость по фазе %.6f, средняя по полю %.6f (задано %.3f); "
                    "остаток фазы %.5f рад; амплитуда %.5f -> %.5f\n",
                    uMeasured, uActual, (double)s.u0, dphi, A0, A1);
        checkf(std::fabs(dphi) < 0.05,
               "снос: фаза совпадает с u*t с точностью 0.05 рад (остаток %.5f, порог %.2f)",
               dphi, 0.05);
        const double aMeasured = -std::log(std::max(1e-12, A1/A0)) / (k*k*steps);
        checkf(std::fabs(aMeasured/alphaExpected - 1.0) < 0.05,
               "снос: затухание не изменилось от адвекции < 5 %% (%.5f против %.5f)",
               aMeasured, alphaExpected);
        shutdownLBM();
    }

    // ---------------- 3. Установившаяся проводимость ----------------
    std::printf("\nПРОВОДИМОСТЬ: стенки 1.0 (низ) и 0.0 (верх), течение отсутствует\n");
    {
        Setup s; s.nx = 24; s.ny = 18; s.nz = 8; s.wallsY = true; s.u0 = 0.0f;
        thermalParams.enabled = true;
        thermalParams.rayleigh = 0.0f;
        thermalParams.prandtl = 1.0f;
        thermalParams.fixedWallTemps = true;
        thermalParams.hotTemp = 1.0f;
        thermalParams.coldTemp = 0.0f;
        thermalParams.perturbation = 0.0f;
        setupBox(s);

        // Старт с однородной температурой 0.5: решение должно само выйти на
        // линейный профиль, а не получить его в подарок на инициализации.
        std::vector<float> T((size_t)lbmNx*lbmNy*lbmNz, 0.5f);
        thermalUpload(T);
        runSteps(6000);

        // Стенка в LBM стоит на границе ячеек, то есть на пол-ячейки от
        // центра крайней жидкости (y = 0.5), а не в центре твёрдой ячейки.
        // Первая версия теста считала профиль от y = 1 — отсюда ровно 1/32 расхождения.
        double maxErr = 0.0;
        const double span = (double)(lbmNy - 2);
        for (int z = 0; z < lbmNz; ++z)
            for (int y = 1; y < lbmNy-1; ++y) {
                const size_t c = ((size_t)z*lbmNy + y)*lbmNx + lbmNx/2;
                if (lbmIsSolid[c]) continue;
                const double sy = ((double)y - 0.5) / span;
                const double lin = 1.0 + (0.0 - 1.0) * sy;
                maxErr = std::max(maxErr, std::fabs(thermalT[c] - lin));
            }
        std::printf("  максимальное отклонение от линейного профиля %.5f, Nu = %.4f\n",
                    maxErr, (double)thermalNusselt);
        checkf(maxErr < 0.01, "проводимость: линейный профиль с точностью 1 %% (отклонение %.5f, порог %.2f)",
               maxErr, 0.01);
        checkf(std::fabs(thermalNusselt - 1.0) < 0.05,
               "проводимость: Nu = 1 (%.4f против %.2f)", (double)thermalNusselt, 1.0);
        shutdownLBM();
    }

    // ---------------- 4. Порог Рэлея–Бенара ----------------
    // Классическое значение Ra_c = 1707.76 для жёстких границ. Проверяем не
    // «совпало число», а знак: ниже порога возмущение гаснет, выше — растёт.
    std::printf("\nРЭЛЕЙ–БЕНАР: коробка 32x22x8, Pr = 1, H = 20 ячеек\n");
    {
        // Мерить «скорость роста» на коротком окне нельзя: поле стартует из
        // покоя, и первый этап — это переходный процесс, а не линейная мода
        // (на 1000 шагах при времени диффузии H^2/alpha = 5120 шагов).
        // Поэтому сравниваем установившиеся амплитуды: ниже порога возмущение
        // уходит в ноль, выше — выходит на конечную амплитуду.
        auto amplitude = [&](float Ra, double* aEarly, double* aLate, double* nuOut) {
            Setup s; s.nx = 32; s.ny = 18; s.nz = 8; s.wallsY = true; s.u0 = 0.0f;
            thermalParams.enabled = true;
            thermalParams.rayleigh = Ra;
            thermalParams.prandtl = 1.0f;
            thermalParams.fixedWallTemps = true;
            thermalParams.hotTemp = 1.0f;
            thermalParams.coldTemp = 0.0f;
            thermalParams.perturbation = 0.02f;
            setupBox(s);      // thermalInit -> thermalResetField (линейный профиль + возмущение)
            runSteps(6000);
            *aEarly = perturbationRMS();
            runSteps(18000);
            *aLate = perturbationRMS();
            *nuOut = (double)thermalNusselt;
            std::printf("  Ra = %6.0f: амплитуда %.5f (6000 шагов) -> %.5f (24000), Nu = %.3f\n",
                        (double)Ra, *aEarly, *aLate, *nuOut);
            shutdownLBM();
        };

        double e1 = 0, l1 = 0, e2 = 0, l2 = 0, e3 = 0, l3 = 0, nu1 = 0, nu2 = 0, nu3 = 0;
        amplitude(1000.0f, &e1, &l1, &nu1);
        amplitude(1700.0f, &e2, &l2, &nu2);
        amplitude(2500.0f, &e3, &l3, &nu3);
        char nm[256];
        std::snprintf(nm, sizeof(nm),
                      "Рэлей–Бенар: Ra = 1000 (ниже 1707.76) — возмущение гаснет до нуля (%.5f -> %.5f)",
                      e1, l1);
        check(l1 <= 1e-4, nm);
        std::snprintf(nm, sizeof(nm),
                      "Рэлей–Бенар: Ra = 1700 (в 0.5 %% ниже порога) — всё ещё гаснет (%.5f -> %.5f)",
                      e2, l2);
        check(l2 < e2, nm);
        std::snprintf(nm, sizeof(nm),
                      "Рэлей–Бенар: Ra = 2500 (выше порога) — конвекция держится (%.5f -> %.5f)",
                      e3, l3);
        check(l3 > 0.9 * e3, nm);
        std::snprintf(nm, sizeof(nm),
                      "Рэлей–Бенар: ниже порога Nu = 1, выше — Nu = %.3f (порог 1.2)", nu3, 0.0);
        check(nu1 < 1.01 && nu2 < 1.01 && nu3 > 1.2, nm);

        // Знак силы плавучести: горячее (T > Tref) должно толкать вверх.
        Setup s; s.nx = 8; s.ny = 8; s.nz = 8; s.u0 = 0.0f;
        thermalParams.enabled = true;
        thermalParams.rayleigh = 1.0f;
        thermalParams.prandtl = 1.0f;
        thermalParams.fixedWallTemps = false;
        thermalParams.perturbation = 0.0f;
        setupBox(s);
        std::vector<float> T((size_t)lbmNx*lbmNy*lbmNz, 0.5f);
        T[((size_t)0*lbmNy + 5)*lbmNx + 4] = 1.0f;   // горячая точка
        T[((size_t)0*lbmNy + 2)*lbmNx + 4] = 0.0f;   // холодная точка
        thermalUpload(T);
        runSteps(1);
        const float hotF = thermalBuoyancyY[((size_t)0*lbmNy + 5)*lbmNx + 4];
        const float coldF = thermalBuoyancyY[((size_t)0*lbmNy + 2)*lbmNx + 4];
        char nm2[256];
        std::snprintf(nm2, sizeof(nm2),
                      "плавучесть направлена вверх: горячая ячейка %+.3e, холодная %+.3e",
                      hotF, coldF);
        check(hotF > 0.0f && coldF < 0.0f, nm2);
        shutdownLBM();
    }

    std::printf("\n=== ТЕПЛО И ПЛАВУЧЕСТЬ: проверок %d, ошибок %d ===\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
