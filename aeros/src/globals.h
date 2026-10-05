#ifndef GLOBALS_H
#define GLOBALS_H
// =====================================================
// Общее состояние приложения (единый источник для всех модулей).
// v1.9.0 Ultra Realistic+ — расширено новыми режимами и фиксами
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
extern bool showGroundPlane;
extern bool showSlicePlane;
extern bool showColorLegend;
extern bool autoRotate;

extern glm::vec3 bgColor;
extern glm::vec3 bboxColor;
extern glm::vec3 obstacleColor;
extern glm::vec3 groundColor;
extern float obstacleAlpha;
extern float streamlineAlpha;
extern float streamlineWidth;
extern glm::vec3 modelColor;
extern float groundAlpha;

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
extern unsigned int groundVAO;
extern unsigned int groundVBO;
extern unsigned int groundEBO;
extern int   groundIndexCount;
extern unsigned int sliceVAO;
extern unsigned int sliceVBO;
extern unsigned int gridVAO;
extern unsigned int gridVBO;

// --- Подъёмная сила / сопротивление ---
extern glm::vec3 liftVector;
extern glm::vec3 dragVector;
extern float liftMagnitude;
extern float dragMagnitude;
extern float liftToDragRatio;
extern float momentMagnitude;
extern glm::vec3 momentVector;
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

// --- Performance metrics (v1.6.0+) ---
extern float perfFrameMs;
extern float perfLBMms;
extern float perfParticlesMs;
extern float perfStreamlinesMs;
extern float perfForcesMs;
extern float perfVoxelMs;
extern int   perfOpenMPThreads;

// --- Realistic Aero (v1.7.0+) + v1.9.0 новые режимы ---
enum class AeroVisMode {
    Pressure,           // Cp — как на фото 4 (NASCAR rainbow)
    VelocityMagnitude,  // |U| — как на фото 1 (U Magnitude)
    Vorticity,          // |ω| — завихренность
    QCriterion,         // Q-критерий — вихревые структуры
    TurbulentKE,        // TKE — турбулентность
    SkinFriction,       // Cf — трение
    BoundaryLayer,      // толщина погранслоя
    MachNumber,         // Mach — сжимаемость (v1.9.0)
    Helicity,           // Helicity — спиральность (v1.9.0)
    TotalPressure       // Total Pressure — полное давление (v1.9.0)
};
extern AeroVisMode aeroVisMode;
extern bool aeroGroundEffect;       // земля для авто (фото 2,5)
extern float aeroGroundHeight;      // высота земли относительно minBB
extern bool aeroShowSlice;          // срез скорости как на фото 5 (Cybertruck)
extern int aeroSliceAxis;           // 0=X,1=Y,2=Z
extern float aeroSlicePos;          // позиция среза 0-1
extern bool aeroColorStreamlinesByVelocity; // окраска линий тока по скорости как на фото 2
extern bool aeroShowSeparation;     // подсветка отрыва потока
extern float aeroRefArea;           // референсная площадь для Cd/Cl
extern bool aeroAutoRefArea;        // авто расчет ref area
extern bool aeroShowBoundaryLayer;
extern float aeroBoundaryLayerScale;
extern bool aeroMachEffects;
extern float aeroReNumber;          // Reynolds number (вычисляется)

// --- v1.8.0 новые ---
extern bool aeroShowWake;
extern float aeroWakeOpacity;
extern bool aeroShowVortices;
extern bool aeroUseRealisticLighting;
extern int aeroColorMap;            // 0=rainbow,1=viridis,2=parula,3=coolwarm
extern bool aeroExportEnabled;
extern float aeroAutoRotateSpeed;

// --- v1.9.0 новые ---
extern bool aeroShowParticleTrails;
extern float aeroTrailLength;
extern bool aeroSurfaceStreamlines;
extern bool aeroAdaptiveLBM;
extern bool aeroShowHelicity;
extern float aeroHelicityScale;
extern bool aeroShowMach;
extern bool aeroExportCSV;
extern bool aeroSaveSettings;
extern bool aeroShowPerfGraph;
extern float aeroMachThreshold;
extern bool aeroUseRK4Particles;
extern bool aeroShowTotalPressure;
extern int aeroScreenshotFormat; // 0=BMP,1=CSV forces
extern bool aeroShowMemoryUsage;
extern float aeroParticleTrailOpacity;

// --- Screenshot & Export ---
extern bool aeroScreenshotRequested;
extern bool aeroCSVExportRequested;
extern std::string aeroLastScreenshotPath;
extern std::string aeroLastCSVPath;

#endif // GLOBALS_H
