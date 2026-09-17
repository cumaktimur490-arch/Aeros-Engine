#ifndef FLOW_FIELD_H
#define FLOW_FIELD_H
// =====================================================
// Поле скоростей (CPU) и сборка FlowParams
// =====================================================

#include <glm/glm.hpp>

#include "flow_params.h"

void updateFlowParams();
glm::vec3 computeVelocityFieldCPU(const glm::vec3& p, const FlowParams& prm);
glm::vec3 colorForPoint(const glm::vec3& v, float sdfDist, const FlowParams& prm);

#endif // FLOW_FIELD_H
