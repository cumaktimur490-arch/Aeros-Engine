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

glm::vec3 bgColor        = glm::vec3(0.05f, 0.06f, 0.09f);
glm::vec3 bboxColor      = glm::vec3(0.3f, 0.3f, 0.3f);
glm::vec3 obstacleColor  = glm::vec3(0.2f, 0.5f, 0.9f);
float obstacleAlpha    = 0.12f;
float streamlineAlpha  = 0.85f;
float streamlineWidth  = 1.5f;

bool  vsyncEnabled = true;
bool  limitFPS     = false;
float maxFPS       = 60.0f;
float cameraSpeedMultiplier = 1.0f;
float mouseSensitivity = 0.3f;

int useCUDA = 1;

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
// Сколько частиц реально аллоцировано в буферах/векторах.
// Слайдер Count меняет numParticles, но буферы перевыделяются только в initParticles().
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

glm::vec3 liftVector(0.0f);
glm::vec3 dragVector(0.0f);
float liftMagnitude = 0.0f;
float dragMagnitude = 0.0f;
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
