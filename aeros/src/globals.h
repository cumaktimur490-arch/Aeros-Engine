#ifndef GLOBALS_H
#define GLOBALS_H
// =====================================================
// Общее состояние приложения (единый источник для всех модулей).
// Определения — в globals.cpp.
// =====================================================

#include <glm/glm.hpp>
#include <vector>
#include <string>

#include "flow_params.h"

// --- Окно ---
extern const unsigned int SCR_WIDTH;
extern const unsigned int SCR_HEIGHT;
extern int display_w;
extern int display_h;

// --- Камера ---
extern glm::vec3 cameraPos;
extern glm::vec3 cameraFront;
extern glm::vec3 cameraUp;
extern float yaw;
extern float pitch;
extern float fov;
extern float lastX;
extern float lastY;
extern bool  firstMouse;

extern float deltaTime;
extern float lastFrame;

// --- Видимость и настройки отображения ---
extern bool showModel;
extern bool showBoundingBox;
extern bool showAxes;
extern bool lightingEnabled;
extern bool showParticles;
extern bool showStreamlines;
extern bool showPressure;
extern bool showLiftDrag;
extern bool showObstacle;

extern glm::vec3 bgColor;
extern glm::vec3 bboxColor;
extern glm::vec3 obstacleColor;
extern float obstacleAlpha;
extern float streamlineAlpha;
extern float streamlineWidth;
extern glm::vec3 modelColor;

extern bool  vsyncEnabled;
extern bool  limitFPS;
extern float maxFPS;
extern float cameraSpeedMultiplier;
extern float mouseSensitivity;

// --- Бэкенд вычислений: 1 = CUDA, 0 = CPU ---
extern int useCUDA;

// --- Параметры потока (UI) ---
extern float flowSpeed;
extern float flowAzimuth;
extern float flowElevation;
extern float timeScale;
extern float strouhal;
extern float wakeStrength;
extern float wakeLength;

// --- Частицы ---
extern int   numParticles;
extern float particleSize;
extern float maxSpeedForColor;
extern unsigned int particleVAO;
extern unsigned int particleVBO_pos;
extern unsigned int particleVBO_col;
extern std::vector<float> particlePositions;
extern std::vector<float> particleColors;
// Сколько частиц реально аллоцировано в буферах/векторах.
// Слайдер Count меняет numParticles, но буферы перевыделяются только в initParticles().
extern int particleDrawCount;

// --- Линии тока ---
extern int   numStreamlines;
extern int   streamlineSteps;
extern float streamlineStepSize;
extern unsigned int streamlineVAO;
extern unsigned int streamlineVBO;
extern int   streamlineVertexCount;

// --- Модель ---
extern unsigned int modelVAO;
extern unsigned int modelVBO_vertices;
extern unsigned int modelVBO_normals;
extern unsigned int modelVBO_colors;
extern int   modelVertexCount;
extern std::vector<float> g_vertices;
extern std::vector<float> g_normals;
extern std::vector<float> g_vertexColors;

// --- Служебная геометрия ---
extern unsigned int bboxVAO;
extern unsigned int bboxVBO;
extern unsigned int axesVAO;
extern unsigned int axesVBO;
extern unsigned int obstacleVAO;
extern unsigned int obstacleVBO;
extern unsigned int obstacleEBO;
extern int   obstacleIndexCount;

// --- Подъёмная сила / сопротивление ---
extern glm::vec3 liftVector;
extern glm::vec3 dragVector;
extern float liftMagnitude;
extern float dragMagnitude;
extern glm::vec3 centerOfPressure;
extern unsigned int liftDragVAO;
extern unsigned int liftDragVBO;
extern bool liftDragDirty;

// --- Bounding box модели ---
extern glm::vec3 minBB;
extern glm::vec3 maxBB;
extern glm::vec3 center;
extern float maxDim;

// --- Параметры потока (для GPU/CPU физики) ---
extern FlowParams flowParams;

// --- Воксельная сетка и SDF ---
extern std::vector<int>   g_voxelData;
extern std::vector<float> g_distanceField;
extern int   g_voxNx;
extern int   g_voxNy;
extern int   g_voxNz;
extern float g_voxMinX;
extern float g_voxMinY;
extern float g_voxMinZ;
extern float g_voxMaxX;
extern float g_voxMaxY;
extern float g_voxMaxZ;
extern int   voxelResolution;
extern bool  useVoxelCollision;

#endif // GLOBALS_H
