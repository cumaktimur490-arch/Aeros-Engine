// Headless-проверки физики частиц (v1.34.0).
//
// Что здесь проверяется и почему именно это:
//  1) Устойчивость шага. Аэрозоль 20 мкм имеет tau_p ~ 1.2 мс, а шаг физики
//     4.17 мс (240 Гц): отношение ~3.4. Явный RK4 при таком шаге расходится
//     (|R| = 2.26 > 1), и частицы «звенели». Аналитическая схема обязана быть
//     устойчивой при любом шаге и не давать скорость больше установившейся.
//  2) Терминальная скорость. В покоящемся воздухе частица должна оседать со
//     скоростью tau_p*g (стоксов предел), а не с чем-то ещё.
//  3) Предел трассера: при очень большом шаге скорость равна скорости потока.
//  4) Единицы SDF. g_distanceField хранится в МЕТРАХ; раньше и в CPU-пути, и в
//     CUDA-ядре это расстояние ещё раз умножалось на размер ячейки, из-за чего
//     выталкивание из поверхности было в 1/cs раз меньше нужного.
#include "globals.h"
#include "particles.h"
#include "voxel_grid.h"
#include "flow_field.h"
#include "atmosphere.h"
#include "debug_log.h"
#include <cmath>
#include <cstdio>
#include <vector>

static int g_failed = 0;
static int g_checks = 0;
static void check(bool cond, const char* what) {
    std::printf("  %s  %s\n", cond ? "ok:  " : "FAIL:", what);
    ++g_checks;
    if (!cond) ++g_failed;
}

// Заглушки OpenGL/GLFW/CUDA — тесту нужна только физика шага.
extern "C" double glfwGetTime(void) { static double t = 0.0; return (t += 1.0/60.0); }
extern "C" void setVoxelData(const int*, const float*, int, int, int, float, float, float, float, float, float) {}
extern "C" int  cudaConsumeError(char* buf, int sz) { if (buf && sz > 0) buf[0] = 0; return 0; }
extern "C" void initParticlesCUDA(std::vector<float>&, std::vector<float>&, int, const FlowParams&) {}
extern "C" void updateParticlesCUDA(std::vector<float>&, std::vector<float>&, int, const FlowParams&, float) {}

bool isRendererVulkan() { return false; }
// Указатели glad: в headless-тесте GL-контекста нет, glBackendActive() = true,
// поэтому вызовы обязаны быть заглушками (иначе — нулевой указатель).
extern "C" {
unsigned int (*glad_glGenVertexArrays)(int, unsigned int*) = nullptr;
unsigned int (*glad_glGenBuffers)(int, unsigned int*) = nullptr;
void (*glad_glBindVertexArray)(unsigned int) = nullptr;
void (*glad_glBindBuffer)(unsigned int, unsigned int) = nullptr;
void (*glad_glBufferData)(unsigned int, long, const void*, unsigned int) = nullptr;
void (*glad_glBufferSubData)(unsigned int, long, long, const void*) = nullptr;
void (*glad_glVertexAttribPointer)(unsigned int, int, unsigned int, unsigned char, int, const void*) = nullptr;
void (*glad_glEnableVertexAttribArray)(unsigned int) = nullptr;
}

void updateVertexColors() {}
void computeLiftDrag() {}
void drawModel() {}

int main() {
    debugLogInit("/tmp/tl/particles");
    std::printf("=== физика частиц (v1.34.0) ===\n");

    const float d_p   = 20e-6f;      // 20 мкм
    const float rho_p = 1000.0f;     // вода
    const float rho_f = 1.225f;
    const float mu    = 1.7894e-5f;  // Sutherland при 288.15 K
    const float tau_p = rho_p * d_p * d_p / (18.0f * mu);
    const float dt    = 1.0f / 240.0f;

    std::printf("\n--- устойчивость и релаксация (tau_p = %.4f мс, dt = %.3f мс) ---\n",
                tau_p * 1e3f, dt * 1e3f);
    {
        const glm::vec3 vFluid(20.0f, 0.0f, 0.0f);
        glm::vec3 p(0.0f), v(0.0f), np, nv;
        float vmax = 0.0f;
        for (int i = 0; i < 200; ++i) {
            particleAdvanceAnalytic(p, v, vFluid, d_p, rho_p, rho_f, mu, dt, np, nv);
            p = np; v = nv;
            if (std::fabs(v.x) > vmax) vmax = std::fabs(v.x);
        }
        char nm[256];
        std::snprintf(nm, sizeof(nm),
                      "скорость не выходит за установившуюся: max %.4f <= 20.0000 м/с", vmax);
        check(vmax <= 20.0001f, nm);
        std::snprintf(nm, sizeof(nm), "за 200 шагов частица увлечена потоком: u = %.4f м/с (нужно 20)", v.x);
        check(std::fabs(v.x - 20.0f) < 0.05f, nm);
        // За один шаг при dt/tau = 3.36 аналитика даёт почти установившуюся
        // скорость, но не перелетает её — это и есть устойчивость.
        glm::vec3 v1p, v1v;
        particleAdvanceAnalytic(glm::vec3(0.0f), glm::vec3(0.0f), vFluid,
                                d_p, rho_p, rho_f, mu, dt, v1p, v1v);
        std::snprintf(nm, sizeof(nm), "один шаг dt > tau_p не перелетает: u = %.4f (0..20)", v1v.x);
        check(v1v.x > 0.0f && v1v.x < 20.0f, nm);
    }

    std::printf("\n--- терминальная скорость в покоящемся воздухе ---\n");
    {
        glm::vec3 p(0.0f, 10.0f, 0.0f), v(0.0f), np, nv;
        for (int i = 0; i < 2000; ++i) {
            particleAdvanceAnalytic(p, v, glm::vec3(0.0f), d_p, rho_p, rho_f, mu, dt, np, nv);
            p = np; v = nv;
        }
        const float vtExpected = -(1.0f - rho_f / rho_p) * 9.81f * tau_p;
        char nm[256];
        std::snprintf(nm, sizeof(nm),
                      "скорость оседания %.6f м/с против tau_p*g = %.6f м/с",
                      v.y, vtExpected);
        check(std::fabs(v.y - vtExpected) < 0.05f * std::fabs(vtExpected), nm);
        std::snprintf(nm, sizeof(nm), "частица оседает, а не всплывает (vy = %.6f < 0)", v.y);
        check(v.y < 0.0f, nm);
    }

    std::printf("\n--- предел трассера и точность позиции ---\n");
    {
        // Малые скорости — стоксов режим (Re_p << 0.1), где время релаксации
        // аналитически равно tau_p и эталон считается без повторения кода.
        const glm::vec3 vFluid(2e-5f, 1e-5f, -1e-5f);
        glm::vec3 p(0.0f), v(0.0f), np, nv;
        particleAdvanceAnalytic(p, v, vFluid, d_p, rho_p, rho_f, mu, 1.0f, np, nv);
        const float gEff = -(1.0f - rho_f / rho_p) * 9.81f;
        // При шаге >> tau_p частица идёт со скоростью равновесия: поток плюс
        // скорость оседания. Именно она, а не сама скорость потока.
        const glm::vec3 vEq = vFluid + glm::vec3(0.0f, gEff * tau_p, 0.0f);
        char nm[256];
        std::snprintf(nm, sizeof(nm),
                      "при шаге 1 с скорость равна установившейся: (%.4f %.4f %.4f) против (%.4f %.4f %.4f)",
                      nv.x, nv.y, nv.z, vEq.x, vEq.y, vEq.z);
        check(glm::length(nv - vEq) < 1e-3f, nm);

        // Точное решение в стоксовом пределе (малые скорости, Re_p << 1):
        // x = vEq*dt + (v0-vEq)*tau_p*(1-E).
        const glm::vec3 v0(1e-4f, 0.0f, 0.0f);
        particleAdvanceAnalytic(glm::vec3(0.0f), v0, glm::vec3(0.0f),
                                d_p, rho_p, rho_f, mu, dt, np, nv);
        const glm::vec3 vEqS(0.0f, gEff * tau_p, 0.0f);
        const float E = std::exp(-dt / tau_p);
        const glm::vec3 dv = v0 - vEqS;
        const glm::vec3 xExact = vEqS * dt + dv * (tau_p * (1.0f - E));
        std::snprintf(nm, sizeof(nm), "позиция совпадает с аналитическим решением: (%.6f %.6f %.6f) против (%.6f %.6f %.6f)",
                      np.x, np.y, np.z, xExact.x, xExact.y, xExact.z);
        check(glm::length(np - xExact) < 1e-5f, nm);

        // Вырожденные входные данные не должны давать NaN.
        particleAdvanceAnalytic(glm::vec3(0.0f), glm::vec3(1.0f), vFluid, 0.0f, 0.0f, 0.0f, 0.0f, dt, np, nv);
        check(std::isfinite(np.x) && std::isfinite(nv.x), "вырожденные d_p/rho_p не дают NaN");
    }

    std::printf("\n--- единицы поля расстояний (метры, а не ячейки) ---\n");
    {
        // Плоскость x = 0 в кубе [-1;1]^3 с шагом 0.1: SDF = x.
        const int N = 20;
        g_voxNx = g_voxNy = g_voxNz = N;
        g_voxMinX = g_voxMinY = g_voxMinZ = -1.0f;
        g_voxMaxX = g_voxMaxY = g_voxMaxZ = 1.0f;
        g_distanceField.assign((size_t)N * N * N, 0.0f);
        const float cs = 2.0f / (float)N;
        for (int k = 0; k < N; ++k)
            for (int j = 0; j < N; ++j)
                for (int i = 0; i < N; ++i)
                    g_distanceField[((size_t)k * N + j) * N + i] = (float)i * cs - 1.0f + 0.5f * cs;
        flowParams.cellSizeX = flowParams.cellSizeY = flowParams.cellSizeZ = cs;
        flowParams.gridNx = flowParams.gridNy = flowParams.gridNz = N;
        flowParams.gridMinX = flowParams.gridMinY = flowParams.gridMinZ = -1.0f;
        flowParams.gridMaxX = flowParams.gridMaxY = flowParams.gridMaxZ = 1.0f;

        // Точка на 3.5 ячейки правее плоскости: расстояние 0.35 м.
        const float d = sampleSDFCPU(glm::vec3(0.35f, 0.0f, 0.0f));
        char nm[256];
        std::snprintf(nm, sizeof(nm), "SDF на 3.5 ячейки от плоскости = %.4f м (нужно ~0.35, не 0.035)", d);
        check(std::fabs(d - 0.35f) < 0.06f, nm);
        const glm::vec3 n = sdfNormalCPU(glm::vec3(0.35f, 0.0f, 0.0f));
        std::snprintf(nm, sizeof(nm), "нормаль к плоскости: (%.3f %.3f %.3f)", n.x, n.y, n.z);
        check(n.x > 0.9f && std::fabs(n.y) < 0.1f && std::fabs(n.z) < 0.1f, nm);
    }

    std::printf("\n=== %s (проверок: %d, ошибок: %d) ===\n", g_failed == 0 ? "ФИЗИКА В ПОРЯДКЕ" : "ЕСТЬ ПРОВАЛЫ", g_checks, g_failed);
    return g_failed == 0 ? 0 : 1;
}
