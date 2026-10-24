#pragma once
// =====================================================
// Aeros Engine — FSR 1.0 + Optimizations v1.15.0 GoGonam AoS.
// - FSR-like upscaling: EASU + RCAS
// - Dynamic resolution
// - Frustum / Occlusion culling
// - LOD, VRS, Early-Z, Async
// =====================================================

#include <glm/glm.hpp>

float getFSRScale(int mode);
const char* getFSRModeName(int mode);

bool initFSR(int displayW, int displayH);
void shutdownFSR();
void resizeFSR(int displayW, int displayH);
void beginFSRRender(int displayW, int displayH);
void endFSRRenderAndUpscale(int displayW, int displayH);

bool isBoxInFrustum(const glm::vec3& minBB, const glm::vec3& maxBB, const glm::mat4& vp);
int computeLODLevel(float distance, float maxDim);
bool shouldCullParticle(const glm::vec3& pos, const glm::mat4& vp, int displayW, int displayH);
