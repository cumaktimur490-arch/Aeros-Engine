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
#include "lang.h"
#include "cuda_api.h"   // CudaBackendInfo — диагностика CUDA (v1.25.0)

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

// --- Realistic Aero (v1.7.0+) + v1.9.0 + v1.18.0 интересные ---
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
    TotalPressure,      // Total Pressure — полное давление (v1.9.0)
    Schlieren,          // Шлирен — градиент плотности |∇ρ| (v1.18.0) — как в реальных трубах
    Shadowgraph,        // Теневой — лапласиан плотности ∇²ρ (v1.18.0)
    VolumetricSmoke,    // Объемный дым — ray marching плотности (v1.18.0)
    ShockWaves,         // Скачки уплотнения — ударные волны (v1.18.0) supersonic
    AeroAcoustic,       // Аэроакустика — источники шума Lighthill (v1.18.0)
    Temperature,        // Температура — нагрев от сжатия (v1.18.0)
    LIC                 // LIC — Line Integral Convolution на поверхности (v1.18.0)
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

// --- FSR & Optimizations v1.15.0 ---
enum class FSRMode {
    Off = 0,
    UltraQuality = 1,   // 0.77x  — 77% render scale
    Quality = 2,        // 0.67x  — 67%
    Balanced = 3,       // 0.59x  — 59%
    Performance = 4,    // 0.50x  — 50%
    UltraPerformance = 5 // 0.33x — 33%
};
extern FSRMode fsrMode;
extern bool fsrEnabled;
extern float fsrSharpness;          // 0.0-1.0 RCAS sharpness
extern float fsrRenderScale;        // computed
extern bool fsrUseRCAS;
extern bool fsrDynamicRes;
extern float fsrTargetFPS;
extern float fsrCurrentScale;       // dynamic
extern bool fsrShowDebug;

// FSR GL objects
extern unsigned int fsrLowResFBO;
extern unsigned int fsrLowResColorTex;
extern unsigned int fsrLowResDepthRBO;
extern unsigned int fsrIntermediateFBO;
extern unsigned int fsrIntermediateTex;
extern unsigned int fsrEASUProgram;
extern unsigned int fsrRCASProgram;
extern unsigned int fsrQuadVAO;
extern unsigned int fsrQuadVBO;

// General optimizations
extern bool optFrustumCulling;
extern bool optOcclusionCulling;
extern bool optLOD;
extern bool optEarlyZ;
extern bool optDynamicParticles;
extern bool optVRS;
extern bool optAsyncCompute;
extern int  optParticleLOD;         // 0=full, 1=half, 2=quarter
extern float optLODDistance;
extern bool optMeshletCulling;
extern bool optFramePacing;
extern float optTargetFPS;

// Perf
extern float perfFSRms;
extern float perfCullingMs;
extern int perfCulledParticles;
extern int perfCulledTriangles;

// --- Frame Generation v1.16.0 — собственная генерация кадров ---
enum class FGMode {
    Off = 0,
    FG_2x = 1,   // 1 сгенерированный между 2 реальными — 2x FPS
    FG_3x = 2,   // 2 сгенерированных — 3x FPS
    FG_4x = 3    // 3 сгенерированных — 4x FPS
};
extern FGMode fgMode;
extern bool fgEnabled;
extern float fgInterpolationFactor; // 0.5 для 2x
extern bool fgUseMotionVectors;
extern bool fgUseOpticalFlow;
extern bool fgAsync;
extern bool fgShowDebug;
extern bool fgLowLatency;           // Reflex-like — снижает задержку
extern float fgBlendStrength;       // сила бленда
extern int fgGeneratedCount;        // сколько сгенерировано
extern int fgRealCount;

// FG GL objects
extern unsigned int fgRealFBO;
extern unsigned int fgRealColorTex;
extern unsigned int fgRealDepthTex;
extern unsigned int fgPrevColorTex;
extern unsigned int fgPrevDepthTex;
extern unsigned int fgMotionFBO;
extern unsigned int fgMotionTex;
extern unsigned int fgInterpFBO;
extern unsigned int fgInterpTex;
extern unsigned int fgMotionProgram;
extern unsigned int fgInterpProgram;
extern unsigned int fgOpticalFlowProgram;
extern unsigned int fgQuadVAO;
extern unsigned int fgQuadVBO;

// FG history
extern glm::mat4 fgPrevView;
extern glm::mat4 fgPrevProj;
extern glm::mat4 fgCurrView;
extern glm::mat4 fgCurrProj;
extern glm::vec3 fgPrevCameraPos;
extern glm::vec3 fgCurrCameraPos;
extern bool fgHasHistory;
extern float fgLastRealFrameTime;

// FG perf
extern float perfFGms;
extern float perfMotionMs;
extern float perfInterpMs;
extern float fgEffectiveFPS;

// --- v1.18.0 Interesting Features ---
extern bool aeroShowVortexTubes;
extern bool aeroShowShockWaves;
extern bool aeroShowLIC;
extern bool aeroVolumetricEnabled;
extern bool aeroSchlierenEnabled;
extern bool aeroFlightMode;
extern bool aeroShowAeroAcoustic;
extern bool aeroShowTemperature;

// Vortex tubes
extern float aeroVortexTubeRadius;
extern float aeroVortexTubeOpacity;
extern int aeroVortexTubeCount;
extern float aeroVortexHelicityScale;

// Shock waves
extern float aeroShockOpacity;
extern float aeroShockAngle;
extern bool aeroShowMachCone;
extern bool aeroShowExpansionFans;

// Schlieren / Shadowgraph
extern float aeroSchlierenSensitivity;
extern float aeroSchlierenCutoff;
extern bool aeroSchlierenColor;
extern int aeroSchlierenMode; // 0=schlieren, 1=shadowgraph

// Volumetric smoke
extern float aeroSmokeDensity;
extern float aeroSmokeBuoyancy;
extern float aeroSmokeDissipation;
extern int aeroSmokeInjectors;
extern float aeroSmokeOpacity;
extern bool aeroSmokeVolumetricLight;
extern float aeroSmokeTurbulence;

// Flight dynamics 6DOF
extern float aeroFlightMass;
extern float aeroFlightThrust;
extern float aeroFlightVelocity;
extern glm::vec3 aeroFlightPos;
extern glm::vec3 aeroFlightVel;
extern glm::vec3 aeroFlightAngVel;
extern glm::vec3 aeroFlightAngles; // pitch,yaw,roll
extern bool aeroFlightAutoTrim;
extern float aeroFlightInertia;
extern float aeroFlightAltitude;

// LIC
extern float aeroLICStrength;
extern int aeroLICSteps;
extern float aeroLICOpacity;

// Aeroacoustic
extern float aeroAcousticFreq;
extern float aeroAcousticOpacity;

// Interesting - particle streaks
extern bool aeroShowStreaklines;
extern int aeroStreakHistory;
extern float aeroStreakOpacity;

// Performance v1.18.0
extern float perfVolumetricMs;
extern float perfVortexMs;
extern float perfSchlierenMs;

// --- v1.20.0 Lite ---
extern bool g_isLiteMode;
extern bool g_litePowerSaving;
extern float g_liteTargetFPS;
extern int g_liteMaxThreads;

// --- v1.20.1 Lite+ extra ---
extern bool g_isUltraLiteMode;
extern bool g_autoQualityScaling;
extern bool g_batterySaver;
extern float g_currentFPSAverage;

// --- v1.25.0 Режим отладки и диагностика CUDA ---
// Результат проверки бэкенда: почему CUDA работает или не работает.
extern CudaBackendInfo g_cudaInfo;
extern bool            g_cudaProbeDone;
extern std::string     g_cudaStatusText;      // одна строка для UI и отчёта
extern std::string     g_lastDebugReportPath; // куда сохранили последний отчёт
extern int             g_debugCudaFallbacks;  // сколько раз физика откатилась с GPU на CPU
extern int             g_debugPhysicsEveryNFrames; // шаг записи журнала физики (1 = каждый кадр)

// --- v1.26.0 Режим вычислений: таймлапс физики ---
// Запись истории расчёта и её просмотр (F7 — запись, F8 — воспроизведение).
extern bool  tlEnabled;        // идёт ли запись
extern bool  tlPlaying;        // воспроизведение/пауза
extern bool  tlLoop;           // зациклить воспроизведение
extern bool  tlShowField;      // показывать срез поля давления при просмотре
extern bool  tlShowWindow;     // показывать отдельное окно таймлапса (графики и поле)
extern bool  tlFullscreen;     // самописец на всё окно (F11), а не маленькая табличка
extern float uiScale;          // масштаб шрифта панелей (0.8..1.8), сохраняется в настройках
extern bool  tlAutoRecord;     // писать постоянно (кольцо), чтобы история была к моменту сбоя
extern float tlSpeed;          // 1.0 = реальное время записи, 8.0 = в 8 раз быстрее
extern int   tlCapacity;       // кадров в кольцевом буфере
extern int   tlFieldEvery;     // снимать поле каждый N-й кадр (0 = не снимать)
extern int   tlChartFrames;    // сколько кадров истории рисовать в графиках
extern std::string g_lastTimelapsePath;  // куда сохранили историю (CSV)

// --- v1.28.0 — компоновка интерфейса как в инженерных САПР/CFD-пакетах ---
// Панели закреплены по краям экрана: дерево узлов расчёта слева, панель свойств
// выбранного узла справа, консоль и мониторинг остатков внизу, 3D-вид в центре.
extern bool uiDocked;        // закреплённая компоновка (F12 — скрыть/показать панели)
extern bool uiShowTree;      // показывать дерево слева
extern bool uiShowProps;     // показывать панель свойств справа
extern bool uiShowConsole;   // показывать консоль и мониторинг внизу
extern int  uiSelectedLeaf;  // выбранный узел дерева (панель свойств)
extern int  uiTreeW;         // ширина дерева, px
extern int  uiPropsW;        // ширина панели свойств, px
extern int  uiBottomH;       // высота нижней полосы, px

// --- v1.26.0 Карта давления: диапазон Cp ---
extern bool  aeroCpAutoRange;  // брать диапазон из фактических данных, а не фиксированный
extern float aeroCpMin;        // ручной диапазон (когда авто выключен)
extern float aeroCpMax;
extern float g_cpLo;           // фактический диапазон, по которому идёт раскраска
extern float g_cpHi;
extern float lbmPressureMin;   // мин/макс давления (Па) в сетке LBM за последний шаг
extern float lbmPressureMax;
extern float lbmResidualRho;   // сходимость LBM: RMS изменения плотности за шаг
extern float lbmResidualU;     // сходимость LBM: max изменения скорости за шаг

#endif // GLOBALS_H
