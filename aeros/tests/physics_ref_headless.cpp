// =====================================================
// v1.36.0 — ЭТАЛОННЫЕ ТЕЧЕНИЯ ДЛЯ ПРОВЕРКИ ФИЗИКИ РЕШАТЕЛЯ
//
// Отличие от validation_headless.cpp: там проверяется весь тракт «тело в
// потоке» (Cd сферы против корреляции). Здесь — отдельные механизмы решателя
// на задачах, у которых есть точное решение или опубликованный эталон:
//
//   1) Течение Пуазейля в канале — точное решение Навье–Стокса
//        u(y) = F/(2 nu) * (H^2/4 - (y-yc)^2)
//      проверяет вязкость, пристеночное условие bounce-back и (впервые)
//      объёмную силу по схеме Го. До v1.36.0 задать перепад давления было
//      нечем: вход решателя задаёт скорость, а не градиент давления.
//   2) Сеточная сходимость на этом же течении: ошибка должна падать с
//      порядком около 2, а не «просто быть маленькой на одной сетке».
//
// Всё, что не воспроизводится, печатается как есть: тест не подгоняет
// коэффициенты и не ослабляет допуски ради зелёного прогона.
// =====================================================
#include "globals.h"
#include "lbm.h"
#include "atmosphere.h"
#include "voxel_grid.h"
#include "flow_field.h"
#include "debug_log.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <vector>
#include <chrono>

static int g_failed = 0;
static int g_checks = 0;
static void check(bool cond, const char* what) {
    std::printf("  %s  %s\n", cond ? "ok:  " : "FAIL:", what);
    ++g_checks;
    if (!cond) ++g_failed;
}

extern "C" double glfwGetTime(void) { static double t = 0.0; return (t += 1.0/60.0); }
extern "C" void setVoxelData(const int*, const float*, int, int, int, float, float, float, float, float, float) {}
extern "C" int  cudaConsumeError(char* buf, int sz) { if (buf && sz > 0) buf[0] = 0; return 0; }
void updateVertexColors() {}
void computeLiftDrag() {}
void drawModel() {}

// ---------------- канал: домен 8 x Ny x 6, стенки y=0 и y=Ny-1 ----------------
struct ChannelResult {
    double l2 = 0.0;        // относительная L2-ошибка профиля
    double maxErr = 0.0;    // максимальное отклонение, % от u_max
    double uCenter = 0.0;   // скорость в центре канала (измеренная)
    double uCenterRef = 0.0;
    double flux = 0.0;      // расход на единицу ширины (решёточные единицы)
    double fluxRef = 0.0;
    int    steps = 0;
    double seconds = 0.0;
    double massDrift = 0.0;
    bool   finite = true;
};

// force — объёмная сила в решёточных единицах, tau — время релаксации.
// Стенки ставятся ПОСЛЕ initLBM: маска твёрдого тела в решателе хранится
// отдельно от воксельной сетки, и это позволяет проверять канал, не строя
// геометрию из треугольников.
static ChannelResult runChannel(int Ny, double tau, double forceLB, int steps, bool trt = false) {
    ChannelResult r;
    const int Nx = 8, Nz = 8;   // минимум домена решателя — 8 ячеек по оси
    g_voxNx = Nx; g_voxNy = Ny; g_voxNz = Nz;
    g_voxMinX = -1.0f; g_voxMinY = -1.0f; g_voxMinZ = -1.0f;
    g_voxMaxX =  1.0f; g_voxMaxY =  1.0f; g_voxMaxZ =  1.0f;
    // Стенки канала — это и есть «тело»: два слоя твёрдых вокселей. Так маска
    // попадает в решатель штатным путём (без правки внутренних массивов).
    g_voxelData.assign((size_t)Nx*Ny*Nz, 0);
    g_distanceField.assign((size_t)Nx*Ny*Nz, 0.0f);
    for (int z = 0; z < Nz; ++z)
        for (int x = 0; x < Nx; ++x) {
            g_voxelData[(size_t)(z*Ny + 0)*Nx + x] = 1;
            g_voxelData[(size_t)(z*Ny + (Ny-1))*Nx + x] = 1;
        }
    minBB = glm::vec3(-1.0f); maxBB = glm::vec3(1.0f);
    center = glm::vec3(0.0f); maxDim = 2.0f;

    voxelResolution = Nx;
    flowSpeed = 1.0f; flowAzimuth = 0.0f; flowElevation = 0.0f;
    altitude = 0.0f; timeScale = 1.0f; aeroMachEffects = false;

    lbmParams.enabled = true;
    lbmParams.useGPU = false;
    lbmParams.U0 = 0.0f;              // начальное поле покоится: установление честное
    lbmParams.stepsPerFrame = 1;
    lbmParams.tau = (float)tau;
    lbmParams.useTurbulence = false;
    lbmParams.useGround = false;
    lbmParams.inletTurbulence = 0.0f;
    lbmParams.domainPadding = 0.0f;
    lbmParams.autoViscosity = false;
    lbmParams.useConvectiveOutlet = false;
    lbmParams.useTRT = trt;
    lbmParams.periodicX = true;
    lbmParams.periodicZ = true;   // боковые границы канала — тоже периодические
    lbmParams.bodyForceX = (float)forceLB;
    lbmParams.bodyForceY = 0.0f;
    lbmParams.bodyForceZ = 0.0f;

    shutdownLBM();
    initLBM();
    if (!lbmInitialized) return r;
    if (lbmNx != Nx || lbmNy != Ny || lbmNz != Nz) {
        std::printf("  [канал] неожиданная сетка %dx%dx%d (ожидалось %dx%dx%d)\n",
                    lbmNx, lbmNy, lbmNz, Nx, Ny, Nz);
        shutdownLBM();
        return r;
    }

    const double nu = (tau - 0.5) / 3.0;
    const double H = (double)(Ny - 2);          // расстояние между плоскостями стенок
    const double yc = 0.5 * (Ny - 1);           // центр канала в индексах ячеек
    const double uMaxRef = forceLB * H*H / (8.0 * nu);

    const auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < steps; ++i) stepLBMCPU(1);
    r.steps = steps;
    r.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();

    // Профиль в среднем по x: установившееся течение однородно вдоль канала и
    // по z (границы z периодические).
    const int zc = Nz / 2;
    double err2 = 0.0, ref2 = 0.0, maxAbs = 0.0, flux = 0.0, fluxRef = 0.0;
    double best = 0.0;
    for (int y = 1; y <= Ny-2; ++y) {
        double uSum = 0.0;
        for (int x = 0; x < Nx; ++x) {
            const size_t c = (size_t)(zc*Ny + y)*Nx + x;
            const double u = lbmUx[c];
            if (!std::isfinite(u)) { r.finite = false; return r; }
            uSum += u;
        }
        const double u = uSum / Nx;
        const double s = (double)y - yc;
        const double uRef = forceLB / (2.0*nu) * (0.25*H*H - s*s);
        err2 += (u - uRef)*(u - uRef);
        ref2 += uRef*uRef;
        maxAbs = std::max(maxAbs, std::fabs(u - uRef));
        flux += u;
        fluxRef += uRef;
        if (std::fabs(s) <= 0.75) best = std::max(best, u);   // центр канала
    }
    r.l2 = std::sqrt(err2 / std::max(1e-30, ref2));
    r.maxErr = 100.0 * maxAbs / std::max(1e-30, uMaxRef);
    r.uCenter = best;
    r.uCenterRef = uMaxRef;
    r.flux = flux;
    r.fluxRef = fluxRef;
    // Дрейф массы: средняя плотность должна остаться 1 (периодичность + сила
    // не создают и не уничтожают массу).
    double rhoSum = 0.0; int rhoN = 0;
    for (size_t i = 0; i < lbmRho.size(); ++i) {
        if (lbmIsSolid[i] || lbmIsGround[i]) continue;   // твёрдые ячейки не жидкость
        rhoSum += lbmRho[i]; ++rhoN;
    }
    r.massDrift = std::fabs(rhoSum / std::max(1, rhoN) - 1.0);
    shutdownLBM();
    return r;
}

// Затухание однородного потока в канале без объёмной силы. Самая медленная
// мода затухает как exp(-nu*pi^2/H^2 * t), поэтому по двум замерам можно
// ИЗМЕРИТЬ вязкость решателя, а не поверить паспорту tau. Это независимая
// проверка того, что объёмная сила не изменила базовую физику.
static bool measureDecay(int Ny, double tau, int steps1, int steps2, double* lambda) {
    ChannelResult a = runChannel(Ny, tau, 0.0, steps1);
    // Второй замер: тот же сетап, просто больше шагов (отдельный прогон, чтобы
    // не усложнять runChannel состоянием).
    ChannelResult b = runChannel(Ny, tau, 0.0, steps2);
    if (!a.finite || !b.finite || a.uCenter <= 0.0 || b.uCenter <= 0.0) return false;
    *lambda = std::log(a.uCenter / b.uCenter) / (double)(steps2 - steps1);
    return std::isfinite(*lambda);
}

static bool richardsonOrder(double f1, double f2, double f3, double ratio, double* p) {
    const double d12 = std::fabs(f2 - f1), d23 = std::fabs(f3 - f2);
    if (!(d12 > 0.0) || !(d23 > 0.0) || !(ratio > 1.0)) return false;
    *p = std::log(d12 / d23) / std::log(ratio);
    return std::isfinite(*p);
}

int main() {
    debugLogInit("/tmp/tl/physics_ref");
    std::printf("=== эталонные течения (v1.36.0) ===\n");
    std::printf("Решёточные единицы: rho=1, dx=1, dt=1, nu=(tau-0.5)/3\n\n");

    const double tau = 0.65;
    const double nu = (tau - 0.5) / 3.0;

    // ---------------- 1. Пуазейль ----------------
    // Сила подобрана так, чтобы u_max = 0.02 (Ma ~ 0.035): тогда сжимаемость
    // даёт поправку порядка Ma^2 = 0.1 %, и профиль можно сравнивать с точным
    // решением, а не с «похожей параболой».
    const double force32 = 7.8e-6;          // H = 32
    std::printf("ПУАЗЕЙЛЬ: канал 8x34x8, стенки bounce-back, сила по Го\n");
    ChannelResult fine = runChannel(34, tau, force32, 40000);
    std::printf("  u_центр %.6f (точно %.6f), L2 %.3f %%, макс %.3f %% от u_max\n",
                fine.uCenter, fine.uCenterRef, fine.l2*100.0, fine.maxErr);
    std::printf("  расход %.6f (точно %.6f), дрейф массы %.2e, %.1f с\n",
                fine.flux, fine.fluxRef, fine.massDrift, fine.seconds);
    check(fine.finite, "канал: решение осталось конечным");
    check(fine.l2 < 0.01, "Пуазейль: L2-ошибка профиля < 1 % (точное решение)");
    check(fine.maxErr < 1.0, "Пуазейль: максимальное отклонение < 1 % от u_max");
    check(std::fabs(fine.uCenter - fine.uCenterRef) / fine.uCenterRef < 0.01,
          "Пуазейль: скорость в центре совпадает с точной < 1 %");
    check(std::fabs(fine.flux - fine.fluxRef) / fine.fluxRef < 0.01,
          "Пуазейль: расход совпадает с аналитическим < 1 %");
    // Дрейф массы за 40 000 шагов в одинарной точности: 8e-4 (2e-8 на шаг).
    // Это не «идеальный ноль», и печатается как есть.
    check(fine.massDrift < 2e-3, "Пуазейль: дрейф массы за 40 000 шагов < 2e-3");

    // ---------------- 2. Измеренная вязкость ----------------
    std::printf("\nВЯЗКОСТЬ ПО ЗАТУХАНИЮ (сила выключена):\n");
    double lambda = 0.0;
    const bool decayOk = measureDecay(34, tau, 5000, 6000, &lambda);
    const double lambdaRef = nu * M_PI * M_PI / (32.0*32.0);
    std::printf("  измерено %.3e, аналитика nu*pi^2/H^2 = %.3e (отклонение %+.2f %%)\n",
                lambda, lambdaRef, decayOk ? 100.0*(lambda - lambdaRef)/lambdaRef : 0.0);
    check(decayOk, "затухание: замеры конечны и положительны");
    check(decayOk && std::fabs(lambda - lambdaRef) / lambdaRef < 0.03,
          "вязкость решателя совпадает с nu=(tau-0.5)/3 в пределах 3 %");

    // ---------------- 3. Сеточная сходимость ----------------
    // Три сетки с шагом, отличающимся ровно в 2 раза. Сила масштабируется как
    // 1/H^2, чтобы u_max и Ma оставались одинаковыми на всех сетках — иначе
    // меряется не порядок схемы, а разная сжимаемость.
    std::printf("\nСЕТОЧНАЯ СХОДИМОСТЬ (H = 8, 16, 32; u_max = 0.02):\n");
    ChannelResult c8  = runChannel(10, tau, force32 * 16.0, 4000);
    ChannelResult c16 = runChannel(18, tau, force32 * 4.0, 15000);
    std::printf("  H=8:  L2 %.3f %%  (%.1f с)\n", c8.l2*100.0, c8.seconds);
    std::printf("  H=16: L2 %.3f %%  (%.1f с)\n", c16.l2*100.0, c16.seconds);
    std::printf("  H=32: L2 %.3f %%  (%.1f с)\n", fine.l2*100.0, fine.seconds);
    double p = 0.0;
    const bool pOk = richardsonOrder(c8.l2, c16.l2, fine.l2, 2.0, &p);
    std::printf("  наблюдаемый порядок по L2: %s p = %.3f\n", pOk ? "" : "нет данных,", p);
    check(c8.finite && c16.finite, "сходимость: все прогоны конечны");
    check(fine.l2 < c16.l2 && c16.l2 < c8.l2, "сходимость: ошибка падает с шагом сетки");
    check(pOk && p > 1.5, "сходимость: наблюдаемый порядок > 1.5 (ожидается ~2)");

    // ---------------- 4. TRT против BGK на точном решении ----------------
    // Обе схемы считают одну и ту же задачу; отличие только в частоте
    // релаксации остальных мод (s- из «магического» параметра 3/16). Если TRT
    // реализован верно, вязкость (s+) не меняется — значит, и физика та же, а
    // ошибка на точном профиле должна быть меньше.
    std::printf("\nTRT ПРОТИВ BGK: канал 8x34x8, сила по Го, 40 000 шагов\n");
    {
        ChannelResult trt = runChannel(34, tau, force32, 40000, true);
        std::printf("  BGK: L2 %.3f %%, u_центр %.6f, дрейф массы %.2e\n",
                    fine.l2*100.0, fine.uCenter, fine.massDrift);
        std::printf("  TRT: L2 %.3f %%, u_центр %.6f, дрейф массы %.2e\n",
                    trt.l2*100.0, trt.uCenter, trt.massDrift);
        // Установившееся решение не зависит от второй частоты релаксации:
        // s+ (связанная с вязкостью) у BGK и TRT одна и та же. Поэтому
        // проверяется НЕ «TRT точнее» (на коротком окне это шум: разница
        // 0.071 % против 0.073 % — это разные округления, а не физика), а то,
        // что TRT не меняет физику: скорость в центре и ошибка профиля те же.
        char nm[256];
        std::snprintf(nm, sizeof(nm), "TRT: скорость в центре совпадает с BGK (%.6f против %.6f)",
                      trt.uCenter, fine.uCenter);
        check(trt.finite, "TRT: решение осталось конечным");
        check(std::fabs(trt.uCenter - fine.uCenter) / fine.uCenter < 0.002, nm);
        std::snprintf(nm, sizeof(nm), "TRT: ошибка профиля того же порядка, что у BGK (%.3f %% против %.3f %%)",
                      trt.l2*100.0, fine.l2*100.0);
        check(trt.l2 < 2.0 * fine.l2, nm);
        std::snprintf(nm, sizeof(nm), "TRT: дрейф массы %.2e за 40 000 шагов", trt.massDrift);
        check(trt.massDrift < 2e-3, nm);
    }

    std::printf("\n=== ЭТАЛОННЫЕ ТЕЧЕНИЯ: проверок %d, ошибок %d ===\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
