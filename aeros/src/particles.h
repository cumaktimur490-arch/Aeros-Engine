#ifndef PARTICLES_H
#define PARTICLES_H
// =====================================================
// Частицы: инициализация и обновление (CUDA или CPU)
// =====================================================

#include <glm/glm.hpp>

void initParticles();
void updateParticles(float dt);

// v1.34.0 — шаг одной аэрозольной частицы в поле скорости vFluid.
//
// Уравнение движения:  m dv/dt = F_d(v, vFluid) + (m - m_f) g
// Сопротивление линеаризуется по текущей относительной скорости:
//     F_d = (m / tau) (vFluid - v),   tau = m / (0.5 Cd(Re_p) rho_f A |v_rel|),
// что для сферы в стоксовом режиме даёт ровно tau_p = rho_p d²/(18 mu).
// Решение этого уравнения на шаге dt записывается аналитически:
//     v+ = v_eq + (v - v_eq) e^(-dt/tau),      v_eq = vFluid + tau g_eff
//     x+ = x + v_eq dt + (v - v_eq) tau (1 - e^(-dt/tau))
// Схема устойчива при любом dt: для аэрозоля 10–30 мкм tau_p ~ 1 мс, а кадр
// 4 мс (240 Гц), и явный RK4 при таком шаге расходится (|R| > 2).
// Функция вынесена сюда, чтобы физику шага можно было проверить headless-тестом.
void particleAdvanceAnalytic(const glm::vec3& pos, const glm::vec3& vel,
                             const glm::vec3& vFluid,
                             float d_p, float rho_p, float rho_f, float mu,
                             float dt, glm::vec3& outPos, glm::vec3& outVel);

#endif // PARTICLES_H
