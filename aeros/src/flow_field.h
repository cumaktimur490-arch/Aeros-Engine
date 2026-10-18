#ifndef FLOW_FIELD_H
#define FLOW_FIELD_H
// =====================================================
// Поле скоростей (CPU) и сборка FlowParams
// =====================================================

#include <cmath>

#include <glm/glm.hpp>

#include "flow_params.h"

// v1.26.0 — единое ограничение Cp.
// Раньше карта давления ограничивала Cp до [-4, 1.8], а расчёт сил — до [-4, 2.0]:
// цвет поверхности и коэффициент давления слегка расходились на одном и том же
// треугольнике. Теперь правило одно для всех потребителей.
inline float aeroClampCp(float cp) {
    if (!std::isfinite(cp)) return 0.0f;
    if (cp < -4.0f) return -4.0f;
    if (cp >  2.0f) return  2.0f;
    return cp;
}

// Пересчёт диапазона Cp по фактическим значениям (для авто-масштаба карты давления).
// Диапазон симметричный и сглаженный: ноль всегда попадает в середину цветовой
// схемы, иначе невозмущённый поток рисовался «горячим», а сжатие сразу упиралось
// в максимум. cpValues — массив Cp по вершинам модели, count — их число.
void aeroUpdateCpRange(const float* cpValues, int count);

void updateFlowParams();
glm::vec3 computeVelocityFieldCPU(const glm::vec3& p, const FlowParams& prm);
glm::vec3 colorForPoint(const glm::vec3& v, float sdfDist, const FlowParams& prm);

#endif // FLOW_FIELD_H
