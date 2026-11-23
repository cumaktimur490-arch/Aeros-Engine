#pragma once
// =====================================================
// Aeros Engine — Собственная генерация кадров v1.16.0 GoGonam AoS.
// - Frame Generation: интерполяция между реальными кадрами
// - Motion vectors из камеры + depth
// - Optical flow fallback
// - 2x/3x/4x режимы
// - Async, Low Latency (Reflex-like)
// =====================================================

#include <glm/glm.hpp>

float getFGScale(int mode);
const char* getFGModeName(int mode);
int getFGMultiplier(int mode);

bool initFrameGen(int displayW, int displayH);
void shutdownFrameGen();
void resizeFrameGen(int displayW, int displayH);

void beginRealFrame(const glm::mat4& view, const glm::mat4& proj, const glm::vec3& camPos);
void endRealFrame();

bool hasFGHistory();
void generateInterpolatedFrame(float alpha, int displayW, int displayH);
void presentInterpolatedFrame(int displayW, int displayH);
void presentRealFrame(int displayW, int displayH);

void updateFGHistory(const glm::mat4& view, const glm::mat4& proj, const glm::vec3& camPos);

// Для интеграции с FSR
void fgBindRealFBO();
void fgUnbindFBO(int displayW, int displayH);
unsigned int fgGetRealColorTex();
