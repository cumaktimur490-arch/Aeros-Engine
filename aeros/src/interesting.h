#pragma once
// v1.18.0 Interesting Features — шлирен, объемный дым, скачки, вихревые трубки, полет, LIC, акустика
#include <glm/glm.hpp>
#include <vector>

// --- Schlieren / Shadowgraph ---
struct SchlierenData {
    float gradRhoMag;   // |∇ρ| — шлирен
    float laplRho;      // ∇²ρ — теневой
    glm::vec3 gradRho;  // вектор градиента
    float density;      // ρ
};

// Вычисляет градиент плотности в точке (через flow_field)
SchlierenData computeSchlierenAt(const glm::vec3& pos);
float computeDensityGradientMagnitude(const glm::vec3& pos);
glm::vec3 getSchlierenColor(float gradMag, float sensitivity, bool useColor);

// --- Oblique Shock Waves (supersonic) ---
struct ShockWaveData {
    bool hasShock;
    float shockAngle;       // β — угол скачка
    float deflectionAngle;  // θ — угол поворота потока
    float pressureRatio;    // p2/p1 через скачок
    float densityRatio;     // ρ2/ρ1
    float tempRatio;        // T2/T1
    float machNormal1;      // Mn1 = M1 sin β
    float machNormal2;      // Mn2 после скачка
    float mach2;            // M2 после скачка
    glm::vec3 shockNormal;  // нормаль к скачку
    glm::vec3 shockOrigin;  // точка начала
};

// Theta-Beta-Mach relation — решает уравнение для β по M и θ
// tanθ = 2 cotβ (M² sin²β -1)/(M²(γ+cos2β)+2)
float solveObliqueShockAngle(float mach, float deflectionDeg);
ShockWaveData computeObliqueShock(float machUpstream, float deflectionDeg, const glm::vec3& pos, const glm::vec3& flowDir);
float computeMachAngle(float mach); // μ = arcsin(1/M) — угол Маха
glm::vec3 getShockColor(float pressureRatio);

// Prandtl-Meyer expansion fan
struct ExpansionFanData {
    bool hasFan;
    float nu1, nu2;         // Prandtl-Meyer function до и после
    float expansionAngle;   // угол расширения
    float pressureRatio;
    float mach2;
};
ExpansionFanData computePrandtlMeyerFan(float mach1, float deflectionDeg);
float prandtlMeyerFunction(float mach); // ν(M) = sqrt((γ+1)/(γ-1)) atan(sqrt((γ-1)/(γ+1)(M²-1))) - atan(sqrt(M²-1))

// --- Volumetric Smoke Tunnel ---
struct SmokeParticle {
    glm::vec3 pos;
    glm::vec3 vel;
    float density;
    float temperature;
    float age;
    float lifetime;
};

extern std::vector<SmokeParticle> g_smokeParticles;
extern std::vector<float> g_smokeDensityField; // 3D grid
extern int g_smokeNx, g_smokeNy, g_smokeNz;
extern float g_smokeMinX, g_smokeMinY, g_smokeMinZ;
extern float g_smokeMaxX, g_smokeMaxY, g_smokeMaxZ;

void initSmokeTunnel();
void updateSmokeTunnel(float dt);
void shutdownSmokeTunnel();
glm::vec3 sampleSmokeDensity(const glm::vec3& pos); // returns density + illumination
float computeVolumetricTransmittance(const glm::vec3& start, const glm::vec3& end, int steps);

// --- Vortex Tubes ---
struct VortexCore {
    glm::vec3 position;
    glm::vec3 direction; // вихревая линия
    float strength;      // |ω|
    float helicity;      // H = v·ω
    float qCrit;
    float radius;
    std::vector<glm::vec3> linePoints; // линия вихря
};

extern std::vector<VortexCore> g_vortexCores;
void extractVortexCores();
void updateVortexTubes(float dt);
glm::vec3 getVortexTubeColor(float helicity, float strength);
std::vector<glm::vec3> traceVortexLine(const glm::vec3& seed, int maxSteps, float stepSize);

// --- Flight Dynamics 6DOF ---
struct FlightState {
    glm::vec3 position;     // мировая позиция
    glm::vec3 velocity;     // скорость
    glm::vec3 acceleration; // ускорение
    glm::vec3 angles;       // pitch, yaw, roll (rad)
    glm::vec3 angVel;       // угловая скорость
    glm::vec3 angAccel;     // угловое ускорение
    float mass;
    glm::mat3 inertia;      // тензор инерции
    float thrust;
    float altitude;
    bool onGround;
};

extern FlightState g_flightState;
void initFlightDynamics();
void updateFlightDynamics(float dt, const glm::vec3& lift, const glm::vec3& drag, const glm::vec3& moment, const glm::vec3& cop);
void resetFlightDynamics();
glm::mat4 getFlightModelMatrix();
bool isFlightFlying();

// --- LIC (Line Integral Convolution) ---
struct LICData {
    std::vector<float> noiseTex; // входной шум
    std::vector<float> licResult; // результат
    int width, height;
};
extern LICData g_licData;
void initLIC(int w, int h);
void computeLICOnSurface(); // LIC по skin friction lines
glm::vec3 getLICColor(float licValue, const glm::vec3& baseColor);

// --- Aeroacoustics (Lighthill) ---
struct AcousticSource {
    glm::vec3 position;
    float intensity;    // |T_ij| — тензор Лайтхилла
    float frequency;    // характерная частота
    glm::vec3 direction;
};

extern std::vector<AcousticSource> g_acousticSources;
void computeAeroacousticSources();
float computeAcousticIntensity(const glm::vec3& pos);
glm::vec3 getAcousticColor(float intensity);

// --- Temperature (compressible heating) ---
float computeTemperatureAt(const glm::vec3& pos, float mach, float pressureRatio);
glm::vec3 getTemperatureColor(float temp, float tempInf);

// --- Streaklines ---
struct StreakPoint {
    glm::vec3 pos;
    float age;
    int injectorId;
};
extern std::vector<std::vector<StreakPoint>> g_streaklines;
void initStreaklines();
void updateStreaklines(float dt);
void clearStreaklines();

// --- Performance ---
extern float g_perfInterestingMs;

// --- Init/Shutdown all ---
void initInterestingFeatures();
void shutdownInterestingFeatures();
void updateInterestingFeatures(float dt);
