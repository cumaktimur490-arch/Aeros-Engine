#include "globals.h"

const unsigned int SCR_WIDTH  = 1280;
const unsigned int SCR_HEIGHT = 720;

int display_w = SCR_WIDTH;
int display_h = SCR_HEIGHT;

glm::vec3 cameraPos   = glm::vec3(0.0f, 0.0f, 5.0f);
glm::vec3 cameraFront = glm::vec3(0.0f, 0.0f, -1.0f);
glm::vec3 cameraUp    = glm::vec3(0.0f, 1.0f, 0.0f);

float yaw   = -90.0f;
float pitch =  0.0f;
float fov   =  45.0f;

float lastX = SCR_WIDTH / 2.0f;
float lastY = SCR_HEIGHT / 2.0f;
bool firstMouse = true;

float deltaTime = 0.0f;
float lastFrame = 0.0f;

bool showModel       = true;
bool showBoundingBox = false;
bool showAxes        = true;
bool lightingEnabled = true;
bool showParticles   = true;
bool showStreamlines = true;
bool showPressure    = false;
bool showLiftDrag    = true;
bool showObstacle    = false;
bool showGroundPlane = false;
bool showSlicePlane  = false;
bool showColorLegend = true;
bool autoRotate      = false;

glm::vec3 bgColor        = glm::vec3(0.05f, 0.06f, 0.09f);
glm::vec3 bboxColor      = glm::vec3(0.3f, 0.3f, 0.3f);
glm::vec3 obstacleColor  = glm::vec3(0.2f, 0.5f, 0.9f);
glm::vec3 groundColor    = glm::vec3(0.15f, 0.15f, 0.18f);
float obstacleAlpha    = 0.12f;
float streamlineAlpha  = 0.85f;
float streamlineWidth  = 1.5f;
float groundAlpha      = 0.6f;

bool  vsyncEnabled = true;
bool  limitFPS     = false;
float maxFPS       = 60.0f;
float cameraSpeedMultiplier = 1.0f;
float mouseSensitivity = 0.3f;

#ifdef CPU_ONLY
int useCUDA = 0;
#else
int useCUDA = 1;
#endif

float flowSpeed     = 2.0f;
float flowAzimuth   = 0.0f;
float flowElevation = 0.0f;
float timeScale     = 1.0f;
float strouhal      = 0.2f;
float wakeStrength  = 0.4f;
float wakeLength    = 8.0f;

int   numParticles     = 15000;
float particleSize     = 2.0f;
float maxSpeedForColor = 5.0f;
unsigned int particleVAO = 0;
unsigned int particleVBO_pos = 0, particleVBO_col = 0;
std::vector<float> particlePositions;
std::vector<float> particleColors;
int particleDrawCount = 0;

int   numStreamlines     = 24;
int   streamlineSteps    = 300;
float streamlineStepSize = 0.08f;
unsigned int streamlineVAO = 0, streamlineVBO = 0;
int streamlineVertexCount  = 0;

unsigned int modelVAO = 0, modelVBO_vertices = 0, modelVBO_normals = 0, modelVBO_colors = 0;
int modelVertexCount = 0;
std::vector<float> g_vertices;
std::vector<float> g_normals;
std::vector<float> g_vertexColors;
glm::vec3 modelColor = glm::vec3(0.75f, 0.75f, 0.78f);

unsigned int bboxVAO = 0, bboxVBO = 0;
unsigned int axesVAO = 0, axesVBO = 0;

unsigned int obstacleVAO = 0, obstacleVBO = 0, obstacleEBO = 0;
int obstacleIndexCount = 0;

unsigned int groundVAO = 0, groundVBO = 0, groundEBO = 0;
int groundIndexCount = 0;

unsigned int sliceVAO = 0, sliceVBO = 0;
unsigned int gridVAO = 0, gridVBO = 0;

glm::vec3 liftVector(0.0f);
glm::vec3 dragVector(0.0f);
float liftMagnitude = 0.0f;
float dragMagnitude = 0.0f;
float liftToDragRatio = 0.0f;
float momentMagnitude = 0.0f;
glm::vec3 momentVector(0.0f);
glm::vec3 centerOfPressure(0.0f);
unsigned int liftDragVAO = 0, liftDragVBO = 0;
bool liftDragDirty = true;

glm::vec3 minBB, maxBB, center;
float maxDim = 1.0f;

FlowParams flowParams;

std::vector<int>   g_voxelData;
std::vector<float> g_distanceField;
int   g_voxNx = 0, g_voxNy = 0, g_voxNz = 0;
float g_voxMinX = 0, g_voxMinY = 0, g_voxMinZ = 0;
float g_voxMaxX = 0, g_voxMaxY = 0, g_voxMaxZ = 0;
int   voxelResolution = 48;
bool  useVoxelCollision = true;

float perfFrameMs = 0.0f;
float perfLBMms = 0.0f;
float perfParticlesMs = 0.0f;
float perfStreamlinesMs = 0.0f;
float perfForcesMs = 0.0f;
float perfVoxelMs = 0.0f;
int   perfOpenMPThreads = 0;

AeroVisMode aeroVisMode = AeroVisMode::Pressure;
bool aeroGroundEffect = false;
float aeroGroundHeight = 0.0f;
bool aeroShowSlice = false;
int aeroSliceAxis = 1; // Y
float aeroSlicePos = 0.5f;
bool aeroColorStreamlinesByVelocity = true;
bool aeroShowSeparation = true;
float aeroRefArea = 1.0f;
bool aeroAutoRefArea = true;
bool aeroShowBoundaryLayer = false;
float aeroBoundaryLayerScale = 1.0f;
bool aeroMachEffects = false;
float aeroReNumber = 0.0f;

// v1.8.0
bool aeroShowWake = true;
float aeroWakeOpacity = 0.5f;
bool aeroShowVortices = true;
bool aeroUseRealisticLighting = true;
int aeroColorMap = 0; // 0 rainbow
bool aeroExportEnabled = false;
float aeroAutoRotateSpeed = 10.0f;

// v1.9.0 Ultra Realistic+
bool aeroShowParticleTrails = false;
float aeroTrailLength = 0.5f;
bool aeroSurfaceStreamlines = false;
bool aeroAdaptiveLBM = true;
bool aeroShowHelicity = false;
float aeroHelicityScale = 1.0f;
bool aeroShowMach = false;
bool aeroExportCSV = false;
bool aeroSaveSettings = true;
bool aeroShowPerfGraph = false;
float aeroMachThreshold = 0.3f;
bool aeroUseRK4Particles = true;
bool aeroShowTotalPressure = false;
int aeroScreenshotFormat = 0;
bool aeroShowMemoryUsage = false;
float aeroParticleTrailOpacity = 0.6f;

bool aeroScreenshotRequested = false;
bool aeroCSVExportRequested = false;
std::string aeroLastScreenshotPath = "";
std::string aeroLastCSVPath = "";

// v1.15.0 FSR & Optimizations
FSRMode fsrMode = FSRMode::Quality;
bool fsrEnabled = false;
float fsrSharpness = 0.6f;
float fsrRenderScale = 0.67f;
bool fsrUseRCAS = true;
bool fsrDynamicRes = false;
float fsrTargetFPS = 60.0f;
float fsrCurrentScale = 0.67f;
bool fsrShowDebug = false;

unsigned int fsrLowResFBO = 0;
unsigned int fsrLowResColorTex = 0;
unsigned int fsrLowResDepthRBO = 0;
unsigned int fsrIntermediateFBO = 0;
unsigned int fsrIntermediateTex = 0;
unsigned int fsrEASUProgram = 0;
unsigned int fsrRCASProgram = 0;
unsigned int fsrQuadVAO = 0;
unsigned int fsrQuadVBO = 0;

bool optFrustumCulling = true;
bool optOcclusionCulling = false;
bool optLOD = true;
bool optEarlyZ = true;
bool optDynamicParticles = true;
bool optVRS = false;
bool optAsyncCompute = true;
int  optParticleLOD = 0;
float optLODDistance = 5.0f;
bool optMeshletCulling = true;
bool optFramePacing = false;
float optTargetFPS = 60.0f;

float perfFSRms = 0.0f;
float perfCullingMs = 0.0f;
int perfCulledParticles = 0;
int perfCulledTriangles = 0;

// v1.16.0 Frame Generation
FGMode fgMode = FGMode::FG_2x;
bool fgEnabled = false;
float fgInterpolationFactor = 0.5f;
bool fgUseMotionVectors = true;
bool fgUseOpticalFlow = false;
bool fgAsync = false;
bool fgShowDebug = false;
bool fgLowLatency = true;
float fgBlendStrength = 0.5f;
int fgGeneratedCount = 0;
int fgRealCount = 0;

unsigned int fgRealFBO = 0;
unsigned int fgRealColorTex = 0;
unsigned int fgRealDepthTex = 0;
unsigned int fgPrevColorTex = 0;
unsigned int fgPrevDepthTex = 0;
unsigned int fgMotionFBO = 0;
unsigned int fgMotionTex = 0;
unsigned int fgInterpFBO = 0;
unsigned int fgInterpTex = 0;
unsigned int fgMotionProgram = 0;
unsigned int fgInterpProgram = 0;
unsigned int fgOpticalFlowProgram = 0;
unsigned int fgQuadVAO = 0;
unsigned int fgQuadVBO = 0;

glm::mat4 fgPrevView = glm::mat4(1.0f);
glm::mat4 fgPrevProj = glm::mat4(1.0f);
glm::mat4 fgCurrView = glm::mat4(1.0f);
glm::mat4 fgCurrProj = glm::mat4(1.0f);
glm::vec3 fgPrevCameraPos = glm::vec3(0.0f);
glm::vec3 fgCurrCameraPos = glm::vec3(0.0f);
bool fgHasHistory = false;
float fgLastRealFrameTime = 0.0f;

float perfFGms = 0.0f;
float perfMotionMs = 0.0f;
float perfInterpMs = 0.0f;
float fgEffectiveFPS = 0.0f;

// v1.18.0 Interesting Features
bool aeroShowVortexTubes = true;
bool aeroShowShockWaves = true;
bool aeroShowLIC = false;
bool aeroVolumetricEnabled = false;
bool aeroSchlierenEnabled = false;
bool aeroFlightMode = false;
bool aeroShowAeroAcoustic = false;
bool aeroShowTemperature = false;

float aeroVortexTubeRadius = 0.02f;
float aeroVortexTubeOpacity = 0.8f;
int aeroVortexTubeCount = 16;
float aeroVortexHelicityScale = 1.0f;

float aeroShockOpacity = 0.6f;
float aeroShockAngle = 0.0f;
bool aeroShowMachCone = true;
bool aeroShowExpansionFans = true;

float aeroSchlierenSensitivity = 1.0f;
float aeroSchlierenCutoff = 0.5f;
bool aeroSchlierenColor = true;
int aeroSchlierenMode = 0;

float aeroSmokeDensity = 1.0f;
float aeroSmokeBuoyancy = 0.1f;
float aeroSmokeDissipation = 0.98f;
int aeroSmokeInjectors = 3;
float aeroSmokeOpacity = 0.7f;
bool aeroSmokeVolumetricLight = true;
float aeroSmokeTurbulence = 0.3f;

float aeroFlightMass = 1.0f;
float aeroFlightThrust = 0.0f;
float aeroFlightVelocity = 0.0f;
glm::vec3 aeroFlightPos = glm::vec3(0.0f);
glm::vec3 aeroFlightVel = glm::vec3(0.0f);
glm::vec3 aeroFlightAngVel = glm::vec3(0.0f);
glm::vec3 aeroFlightAngles = glm::vec3(0.0f);
bool aeroFlightAutoTrim = true;
float aeroFlightInertia = 1.0f;
float aeroFlightAltitude = 0.0f;

float aeroLICStrength = 1.0f;
int aeroLICSteps = 30;
float aeroLICOpacity = 0.8f;

float aeroAcousticFreq = 1000.0f;
float aeroAcousticOpacity = 0.6f;

bool aeroShowStreaklines = false;
int aeroStreakHistory = 50;
float aeroStreakOpacity = 0.7f;

float perfVolumetricMs = 0.0f;
float perfVortexMs = 0.0f;
float perfSchlierenMs = 0.0f;

bool g_isLiteMode = false;
bool g_litePowerSaving = true;
float g_liteTargetFPS = 30.0f;
int g_liteMaxThreads = 2;

// v1.20.1 Lite+ extra
bool g_isUltraLiteMode = false;
bool g_autoQualityScaling = true;
bool g_batterySaver = true;
float g_currentFPSAverage = 30.0f;

// v1.21.0 SD662/Adreno 610
bool g_isSD662Device = false;
bool g_isAdreno610 = false;
bool g_is90Hz = false;
