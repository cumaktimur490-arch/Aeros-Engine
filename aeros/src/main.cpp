// =====================================================
// AeroS Engine — точка входа, главный цикл и рендер v1.10.0 Multilingual Ultra+
// =====================================================

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include <iostream>
#include <thread>
#include <chrono>
#include <cmath>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <ctime>

#include "globals.h"
#include "shaders.h"
#include "gl_utils.h"
#include "input.h"
#include "ui.h"
#include "model.h"
#include "stl_loader.h"
#include "particles.h"
#include "streamlines.h"
#include "forces.h"
#include "atmosphere.h"
#include "test_mode.h"
#include "lbm.h"
#include "lang.h"

#ifdef _OPENMP
#include <omp.h>
#endif

// GL debug callback — v1.10.0 fixed to use GLAD_GL_VERSION_4_3
static void APIENTRY glDebugCallback(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar* message, const void* userParam) {
    (void)source; (void)id; (void)length; (void)userParam;
    if (severity == GL_DEBUG_SEVERITY_NOTIFICATION) return;
    const char* sevStr = "UNKNOWN";
    if (severity == GL_DEBUG_SEVERITY_HIGH) sevStr = "HIGH";
    else if (severity == GL_DEBUG_SEVERITY_MEDIUM) sevStr = "MEDIUM";
    else if (severity == GL_DEBUG_SEVERITY_LOW) sevStr = "LOW";
    if (type == GL_DEBUG_TYPE_ERROR) {
        std::cerr << "[GL ERROR] [" << sevStr << "] " << message << std::endl;
    } else {
        std::cout << "[GL Debug] type=" << type << " severity=" << sevStr << " msg=" << message << std::endl;
    }
}

// Screenshot BMP export — v1.9.0 new feature
static bool saveScreenshotBMP(const std::string& path) {
    if (display_w <= 0 || display_h <= 0) return false;
    int w = display_w;
    int h = display_h;
    std::vector<unsigned char> pixels(3 * w * h);
    glReadPixels(0, 0, w, h, GL_BGR, GL_UNSIGNED_BYTE, pixels.data());
    // Flip vertically
    std::vector<unsigned char> flipped(3 * w * h);
    for (int y = 0; y < h; ++y) {
        memcpy(flipped.data() + y * 3 * w, pixels.data() + (h - 1 - y) * 3 * w, 3 * w);
    }
    // BMP header
    int filesize = 54 + 3 * w * h;
    unsigned char header[54] = {0};
    header[0] = 'B'; header[1] = 'M';
    header[2] = filesize & 0xFF; header[3] = (filesize >> 8) & 0xFF; header[4] = (filesize >> 16) & 0xFF; header[5] = (filesize >> 24) & 0xFF;
    header[10] = 54;
    header[14] = 40;
    header[18] = w & 0xFF; header[19] = (w >> 8) & 0xFF; header[20] = (w >> 16) & 0xFF; header[21] = (w >> 24) & 0xFF;
    header[22] = h & 0xFF; header[23] = (h >> 8) & 0xFF; header[24] = (h >> 16) & 0xFF; header[25] = (h >> 24) & 0xFF;
    header[26] = 1; header[28] = 24;
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    f.write((char*)header, 54);
    f.write((char*)flipped.data(), 3 * w * h);
    return f.good();
}

static std::string generateTimestampFilename(const std::string& prefix, const std::string& ext) {
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm;
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    std::ostringstream oss;
    oss << prefix << "_" << std::put_time(&tm, "%Y%m%d_%H%M%S") << ext;
    return oss.str();
}

// Export forces to CSV — v1.9.0 new
static bool exportForcesCSV(const std::string& path) {
    std::ofstream f(path);
    if (!f) return false;
    f << "Parameter,Value,Unit\n";
    f << "FlowSpeed," << flowSpeed << ",m/s\n";
    f << "Altitude," << altitude << ",m\n";
    f << "AirDensity," << airDensity << ",kg/m3\n";
    f << "AirPressure," << airPressure << ",Pa\n";
    f << "Temperature," << airTemperature << ",K\n";
    f << "SpeedOfSound," << speedOfSound << ",m/s\n";
    f << "Mach," << (flowSpeed / (speedOfSound + 1e-6f)) << ",\n";
    f << "Reynolds," << aeroReNumber << ",\n";
    f << "RefArea," << aeroRefArea << ",m2\n";
    float q = 0.5f * airDensity * flowSpeed * flowSpeed;
    f << "DynamicPressure," << q << ",Pa\n";
    f << "Drag," << dragMagnitude << ",N\n";
    f << "Lift," << liftMagnitude << ",N\n";
    float Cd = (q > 1e-6f && aeroRefArea > 1e-6f) ? dragMagnitude / (q * aeroRefArea) : 0;
    float Cl = (q > 1e-6f && aeroRefArea > 1e-6f) ? liftMagnitude / (q * aeroRefArea) : 0;
    f << "Cd," << Cd << ",\n";
    f << "Cl," << Cl << ",\n";
    f << "L/D," << liftToDragRatio << ",\n";
    f << "Moment," << momentMagnitude << ",Nm\n";
    f << "CoP_X," << centerOfPressure.x << ",m\n";
    f << "CoP_Y," << centerOfPressure.y << ",m\n";
    f << "CoP_Z," << centerOfPressure.z << ",m\n";
    f << "Azimuth," << flowAzimuth << ",deg\n";
    f << "Elevation," << flowElevation << ",deg\n";
    f << "LBM_Enabled," << (lbmParams.enabled ? 1 : 0) << ",\n";
    f << "LBM_Steps," << lbmCurrentStep << ",\n";
    f << "LBM_Reynolds," << lbmReynolds << ",\n";
    f << "LBM_TKE," << lbmTKE << ",\n";
    return f.good();
}

// Settings save/load — v1.10.0 Multilingual (non-static for UI)
bool saveSettings(const std::string& path) {
    std::ofstream f(path);
    if (!f) return false;
    f << "# Aeros Engine v1.10.0 Settings\n";
    f << "flowSpeed=" << flowSpeed << "\n";
    f << "flowAzimuth=" << flowAzimuth << "\n";
    f << "flowElevation=" << flowElevation << "\n";
    f << "altitude=" << altitude << "\n";
    f << "timeScale=" << timeScale << "\n";
    f << "strouhal=" << strouhal << "\n";
    f << "wakeStrength=" << wakeStrength << "\n";
    f << "wakeLength=" << wakeLength << "\n";
    f << "aeroVisMode=" << (int)aeroVisMode << "\n";
    f << "aeroColorMap=" << aeroColorMap << "\n";
    f << "aeroGroundEffect=" << (aeroGroundEffect ? 1 : 0) << "\n";
    f << "aeroGroundHeight=" << aeroGroundHeight << "\n";
    f << "showParticles=" << (showParticles ? 1 : 0) << "\n";
    f << "showStreamlines=" << (showStreamlines ? 1 : 0) << "\n";
    f << "showPressure=" << (showPressure ? 1 : 0) << "\n";
    f << "numParticles=" << numParticles << "\n";
    f << "numStreamlines=" << numStreamlines << "\n";
    f << "voxelResolution=" << voxelResolution << "\n";
    f << "lbmEnabled=" << (lbmParams.enabled ? 1 : 0) << "\n";
    f << "lbmTau=" << lbmParams.tau << "\n";
    f << "lbmStepsPerFrame=" << lbmParams.stepsPerFrame << "\n";
    f << "language=" << (int)currentLanguage << "\n";
    f << "aeroShowParticleTrails=" << (aeroShowParticleTrails ? 1 : 0) << "\n";
    f << "aeroAdaptiveLBM=" << (aeroAdaptiveLBM ? 1 : 0) << "\n";
    f << "aeroUseRK4Particles=" << (aeroUseRK4Particles ? 1 : 0) << "\n";
    return f.good();
}

bool loadSettings(const std::string& path) {
    std::ifstream f(path);
    if (!f) return false;
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;
        auto pos = line.find('=');
        if (pos == std::string::npos) continue;
        std::string key = line.substr(0, pos);
        std::string val = line.substr(pos + 1);
        try {
            if (key == "flowSpeed") flowSpeed = std::stof(val);
            else if (key == "flowAzimuth") flowAzimuth = std::stof(val);
            else if (key == "flowElevation") flowElevation = std::stof(val);
            else if (key == "altitude") altitude = std::stof(val);
            else if (key == "timeScale") timeScale = std::stof(val);
            else if (key == "strouhal") strouhal = std::stof(val);
            else if (key == "wakeStrength") wakeStrength = std::stof(val);
            else if (key == "wakeLength") wakeLength = std::stof(val);
            else if (key == "aeroVisMode") aeroVisMode = (AeroVisMode)std::stoi(val);
            else if (key == "aeroColorMap") aeroColorMap = std::stoi(val);
            else if (key == "aeroGroundEffect") aeroGroundEffect = (std::stoi(val) != 0);
            else if (key == "aeroGroundHeight") aeroGroundHeight = std::stof(val);
            else if (key == "showParticles") showParticles = (std::stoi(val) != 0);
            else if (key == "showStreamlines") showStreamlines = (std::stoi(val) != 0);
            else if (key == "showPressure") showPressure = (std::stoi(val) != 0);
            else if (key == "numParticles") numParticles = std::stoi(val);
            else if (key == "numStreamlines") numStreamlines = std::stoi(val);
            else if (key == "voxelResolution") voxelResolution = std::stoi(val);
            else if (key == "lbmEnabled") lbmParams.enabled = (std::stoi(val) != 0);
            else if (key == "lbmTau") lbmParams.tau = std::stof(val);
            else if (key == "lbmStepsPerFrame") lbmParams.stepsPerFrame = std::stoi(val);
            else if (key == "language") currentLanguage = (Language)std::stoi(val);
            else if (key == "aeroShowParticleTrails") aeroShowParticleTrails = (std::stoi(val) != 0);
            else if (key == "aeroAdaptiveLBM") aeroAdaptiveLBM = (std::stoi(val) != 0);
            else if (key == "aeroUseRK4Particles") aeroUseRK4Particles = (std::stoi(val) != 0);
        } catch (...) { /* ignore parse errors */ }
    }
    return true;
}

int main() {
#ifdef _OPENMP
    perfOpenMPThreads = omp_get_max_threads();
    std::cout << "[Perf] OpenMP enabled with " << perfOpenMPThreads << " threads (v1.10.0 Multilingual)" << std::endl;
#else
    perfOpenMPThreads = 1;
    std::cout << "[Perf] OpenMP not enabled (single thread)" << std::endl;
#endif

    // Try load settings
    if (aeroSaveSettings) {
        loadSettings("aeros_settings.ini");
        std::cout << "[Settings] Loaded aeros_settings.ini if exists" << std::endl;
    }

    if (!glfwInit()) {
#ifdef _WIN32
        MessageBoxA(nullptr, "Failed to init GLFW", "Error", MB_ICONERROR);
#else
        std::cerr << "Failed to init GLFW" << std::endl;
#endif
        return -1;
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_SAMPLES, 4);
    glfwWindowHint(GLFW_DOUBLEBUFFER, GLFW_TRUE);
#ifdef _DEBUG
    glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GLFW_TRUE);
#endif

    GLFWwindow* window = glfwCreateWindow(SCR_WIDTH, SCR_HEIGHT, "Aeros Engine v1.10.0 Multilingual Ultra+", nullptr, nullptr);
    if (!window) {
#ifdef _WIN32
        MessageBoxA(nullptr, "Failed to create GLFW window", "Error", MB_ICONERROR);
#else
        std::cerr << "Failed to create GLFW window" << std::endl;
#endif
        glfwTerminate();
        return -1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(vsyncEnabled ? 1 : 0);
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
    glfwSetCursorPosCallback(window, mouse_callback);
    glfwSetScrollCallback(window, scroll_callback);
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
#ifdef _WIN32
        MessageBoxA(nullptr, "Failed to init GLAD", "Error", MB_ICONERROR);
#else
        std::cerr << "Failed to init GLAD" << std::endl;
#endif
        glfwTerminate();
        return -1;
    }

    // OpenGL debug — v1.9.0 fixed: use GLAD_GL_VERSION_4_3
    if (GLAD_GL_VERSION_4_3) {
        glEnable(GL_DEBUG_OUTPUT);
        glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
        glDebugMessageCallback(glDebugCallback, nullptr);
        glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DEBUG_SEVERITY_NOTIFICATION, 0, nullptr, GL_FALSE);
        std::cout << "[GL] Debug output enabled (GL 4.3+)" << std::endl;
    } else {
        std::cout << "[GL] Debug output not available (GL < 4.3)" << std::endl;
    }

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_MULTISAMPLE);
    glEnable(GL_PROGRAM_POINT_SIZE);

    // Check MSAA support
    GLint msaaSamples = 0;
    glGetIntegerv(GL_SAMPLES, &msaaSamples);
    std::cout << "[GL] Vendor: " << glGetString(GL_VENDOR) << " Renderer: " << glGetString(GL_RENDERER) << " Version: " << glGetString(GL_VERSION) << " MSAA: " << msaaSamples << std::endl;

    createObstacleSphere(48, 48);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    // Docking disabled for old ImGui version (no DockingEnable flag) — v1.10.0 still compatible
    // io.ConfigFlags |= ImGuiConfigFlags_DockingEnable; // old ImGui doesn't have this

    // v1.10.0: Initialize localization with Cyrillic font support
    initLocalization();

    ImGui::StyleColorsDark();
    // Improve ImGui style for v1.10.0
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 4.0f;
    style.FrameRounding = 3.0f;
    style.GrabRounding = 3.0f;
    style.ScrollbarRounding = 3.0f;

    if (!ImGui_ImplGlfw_InitForOpenGL(window, true)) {
        std::cerr << "[ImGui] Failed to init GLFW backend" << std::endl;
        glfwTerminate();
        return -1;
    }
    if (!ImGui_ImplOpenGL3_Init("#version 330 core")) {
        std::cerr << "[ImGui] Failed to init OpenGL3 backend" << std::endl;
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        glfwTerminate();
        return -1;
    }

    unsigned int modelShaderProgram    = compileProgram(vertexShaderSource, fragmentShaderSource);
    unsigned int particleShaderProgram = compileProgram(particleVertexShaderSource, particleFragmentShaderSource);
    unsigned int lineShaderProgram     = compileProgram(lineVertexShaderSource, lineFragmentShaderSource);
    unsigned int groundShaderProgram   = compileProgram(groundVertexShaderSource, groundFragmentShaderSource);
    if (modelShaderProgram == 0 || particleShaderProgram == 0 || lineShaderProgram == 0 || groundShaderProgram == 0) {
        std::cerr << "[Main] Shader compile failed" << std::endl;
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        glfwTerminate();
        return -1;
    }

    std::string modelPath = openFileDialog();
    if (modelPath.empty()) {
        std::cout << "[Main] No file selected, exiting" << std::endl;
        glDeleteProgram(modelShaderProgram);
        glDeleteProgram(particleShaderProgram);
        glDeleteProgram(lineShaderProgram);
        glDeleteProgram(groundShaderProgram);
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        glfwTerminate();
        return 0;
    }
    if (!loadModel(modelPath)) {
#ifdef _WIN32
        MessageBoxA(nullptr, ("Failed to load model: " + modelPath).c_str(), "Error", MB_ICONERROR);
#else
        std::cerr << "Failed to load model: " << modelPath << std::endl;
#endif
    }

    glfwShowWindow(window);

    float prevSpeed = flowSpeed;
    float prevAz    = flowAzimuth;
    float prevEl    = flowElevation;
    float prevWake  = wakeStrength;
    float prevStro  = strouhal;
    float prevWL    = wakeLength;
    float prevAlt   = altitude;
    float prevGroundH = aeroGroundHeight;
    bool prevGroundEn = aeroGroundEffect;

    float autoRotateAngle = 0.0f;
    float lastInteractionTime = (float)glfwGetTime();

    while (!glfwWindowShouldClose(window)) {
        float currentFrame = (float)glfwGetTime();
        deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;
        if (deltaTime > 0.1f)   deltaTime = 0.1f;
        if (deltaTime <= 0.0f)  deltaTime = 0.0001f;
        if (!std::isfinite(deltaTime)) deltaTime = 0.016f;

        if (limitFPS) {
            double target = 1.0 / maxFPS;
            double elapsed = glfwGetTime() - currentFrame;
            if (elapsed < target) {
                int sleepMs = (int)((target - elapsed)*1000.0);
                if (sleepMs > 0) std::this_thread::sleep_for(std::chrono::milliseconds(sleepMs));
            }
        }

        if (autoRotate) {
            // Pause auto-rotate if user interacted recently (v1.9.0 improvement)
            float timeSinceInteraction = currentFrame - lastInteractionTime;
            if (timeSinceInteraction > 2.0f) {
                autoRotateAngle += deltaTime * aeroAutoRotateSpeed;
                if (autoRotateAngle > 360.0f) autoRotateAngle -= 360.0f;
                flowAzimuth = autoRotateAngle;
            }
        }

        processInput(window);
        // Detect interaction for auto-rotate pause
        if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS ||
            glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS ||
            glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS ||
            glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_MIDDLE) == GLFW_PRESS) {
            lastInteractionTime = currentFrame;
        }

        auto frameStart = std::chrono::high_resolution_clock::now();

        auto tLBM0 = std::chrono::high_resolution_clock::now();
        if (lbmParams.enabled) {
            // v1.9.0 adaptive LBM stepping
            if (aeroAdaptiveLBM) {
                // Adjust steps based on frame time
                if (perfFrameMs > 33.0f && lbmParams.stepsPerFrame > 1) {
                    lbmParams.stepsPerFrame = std::max(1, lbmParams.stepsPerFrame - 1);
                } else if (perfFrameMs < 16.0f && lbmParams.stepsPerFrame < 20) {
                    lbmParams.stepsPerFrame++;
                }
            }
            updateLBM(deltaTime);
        }
        auto tLBM1 = std::chrono::high_resolution_clock::now();
        perfLBMms = std::chrono::duration<float, std::milli>(tLBM1-tLBM0).count();

        auto tPart0 = std::chrono::high_resolution_clock::now();
        if (showParticles) updateParticles(deltaTime);
        auto tPart1 = std::chrono::high_resolution_clock::now();
        perfParticlesMs = std::chrono::duration<float, std::milli>(tPart1-tPart0).count();

        auto tForce0 = std::chrono::high_resolution_clock::now();
        if (showPressure)  updateVertexColors();
        computeLiftDrag();
        if (lbmParams.enabled && lbmInitialized) computeLBMForcesFromLBM();
        updateLiftDragArrows();
        auto tForce1 = std::chrono::high_resolution_clock::now();
        perfForcesMs = std::chrono::duration<float, std::milli>(tForce1-tForce0).count();

        // Streamlines need recompute on flow change or ground change
        bool needStreamlines = false;
        if (showStreamlines) {
            if (fabs(prevSpeed-flowSpeed) > 1e-3f ||
                fabs(prevAz-flowAzimuth) > 0.5f ||
                fabs(prevEl-flowElevation) > 0.5f ||
                fabs(prevWake-wakeStrength) > 1e-3f ||
                fabs(prevStro-strouhal) > 1e-3f ||
                fabs(prevWL-wakeLength) > 1e-3f ||
                fabs(prevAlt-altitude) > 10.0f ||
                fabs(prevGroundH-aeroGroundHeight) > 1e-3f ||
                prevGroundEn != aeroGroundEffect) {
                needStreamlines = true;
            }
        }
        if (needStreamlines) {
            auto tSL0 = std::chrono::high_resolution_clock::now();
            prevSpeed = flowSpeed;
            prevAz = flowAzimuth;
            prevEl = flowElevation;
            prevWake = wakeStrength;
            prevStro = strouhal;
            prevWL = wakeLength;
            prevAlt = altitude;
            prevGroundH = aeroGroundHeight;
            prevGroundEn = aeroGroundEffect;
            computeStreamlines();
            auto tSL1 = std::chrono::high_resolution_clock::now();
            perfStreamlinesMs = std::chrono::duration<float, std::milli>(tSL1-tSL0).count();
        }

        auto frameEnd = std::chrono::high_resolution_clock::now();
        perfFrameMs = std::chrono::duration<float, std::milli>(frameEnd-frameStart).count();

        if (testContinuous) validateFrame();

        // Handle screenshot and CSV export requests (v1.9.0)
        if (aeroScreenshotRequested) {
            aeroScreenshotRequested = false;
            std::string fname = generateTimestampFilename("screenshot", ".bmp");
            if (saveScreenshotBMP(fname)) {
                aeroLastScreenshotPath = fname;
                std::cout << "[Export] Screenshot saved: " << fname << std::endl;
            } else {
                std::cerr << "[Export] Failed to save screenshot" << std::endl;
            }
        }
        if (aeroCSVExportRequested) {
            aeroCSVExportRequested = false;
            std::string fname = generateTimestampFilename("forces", ".csv");
            if (exportForcesCSV(fname)) {
                aeroLastCSVPath = fname;
                std::cout << "[Export] CSV saved: " << fname << std::endl;
            } else {
                std::cerr << "[Export] Failed to save CSV" << std::endl;
            }
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        drawUI();

        glClearColor(bgColor.x, bgColor.y, bgColor.z, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glm::mat4 view = glm::lookAt(cameraPos, cameraPos + cameraFront, cameraUp);
        float aspect = (display_h > 0) ? (float)display_w/(float)display_h : 1.0f;
        glm::mat4 projection = glm::perspective(glm::radians(fov), aspect, 0.05f, maxDim*50.0f);
        glm::mat4 model = glm::mat4(1.0f);

        glm::vec3 lightPos = center + glm::vec3(maxDim*2.0f, maxDim*2.5f, maxDim*2.0f);
        glm::vec3 lightColor(1.0f);

        if (showGroundPlane || aeroGroundEffect) {
            float groundY = g_voxMinY + aeroGroundHeight;
            if (!std::isfinite(groundY)) groundY = minBB.y - maxDim*0.1f;
            glm::mat4 groundModel = glm::translate(glm::mat4(1.0f), glm::vec3(center.x, groundY, center.z));
            groundModel = glm::scale(groundModel, glm::vec3(maxDim*2.5f, 1.0f, maxDim*2.5f));
            glUseProgram(groundShaderProgram);
            glUniformMatrix4fv(glGetUniformLocation(groundShaderProgram, "model"), 1, GL_FALSE, glm::value_ptr(groundModel));
            glUniformMatrix4fv(glGetUniformLocation(groundShaderProgram, "view"), 1, GL_FALSE, glm::value_ptr(view));
            glUniformMatrix4fv(glGetUniformLocation(groundShaderProgram, "projection"), 1, GL_FALSE, glm::value_ptr(projection));
            glUniform3fv(glGetUniformLocation(groundShaderProgram, "lightPos"), 1, &lightPos[0]);
            glUniform3fv(glGetUniformLocation(groundShaderProgram, "viewPos"), 1, &cameraPos[0]);
            glUniform3fv(glGetUniformLocation(groundShaderProgram, "lightColor"), 1, &lightColor[0]);
            glUniform3fv(glGetUniformLocation(groundShaderProgram, "groundColor"), 1, &groundColor[0]);
            glUniform1f(glGetUniformLocation(groundShaderProgram, "alpha"), groundAlpha);
            glBindVertexArray(groundVAO);
            glDrawElements(GL_TRIANGLES, groundIndexCount, GL_UNSIGNED_INT, 0);
            glBindVertexArray(0);

            // Grid — v1.9.0 improved with better alpha
            if (gridVAO != 0) {
                glUseProgram(lineShaderProgram);
                glUniformMatrix4fv(glGetUniformLocation(lineShaderProgram, "model"), 1, GL_FALSE, glm::value_ptr(groundModel));
                glUniformMatrix4fv(glGetUniformLocation(lineShaderProgram, "view"), 1, GL_FALSE, glm::value_ptr(view));
                glUniformMatrix4fv(glGetUniformLocation(lineShaderProgram, "projection"), 1, GL_FALSE, glm::value_ptr(projection));
                glUniform1i(glGetUniformLocation(lineShaderProgram, "useVertexColor"), 0);
                glUniform1f(glGetUniformLocation(lineShaderProgram, "alpha"), 0.18f);
                glUniform3f(glGetUniformLocation(lineShaderProgram, "lineColor"), 0.3f, 0.3f, 0.35f);
                glBindVertexArray(gridVAO);
                glDrawArrays(GL_LINES, 0, 82*2);
                glBindVertexArray(0);
            }
        }

        if (showModel) {
            glUseProgram(modelShaderProgram);
            glUniformMatrix4fv(glGetUniformLocation(modelShaderProgram, "model"), 1, GL_FALSE, glm::value_ptr(model));
            glUniformMatrix4fv(glGetUniformLocation(modelShaderProgram, "view"), 1, GL_FALSE, glm::value_ptr(view));
            glUniformMatrix4fv(glGetUniformLocation(modelShaderProgram, "projection"), 1, GL_FALSE, glm::value_ptr(projection));
            glUniform3fv(glGetUniformLocation(modelShaderProgram, "lightPos"), 1, &lightPos[0]);
            glUniform3fv(glGetUniformLocation(modelShaderProgram, "viewPos"), 1, &cameraPos[0]);
            glUniform3fv(glGetUniformLocation(modelShaderProgram, "lightColor"), 1, &lightColor[0]);
            glUniform3fv(glGetUniformLocation(modelShaderProgram, "objectColor"), 1, &modelColor[0]);
            glUniform1i(glGetUniformLocation(modelShaderProgram, "useLighting"), lightingEnabled ? 1 : 0);
            glUniform1i(glGetUniformLocation(modelShaderProgram, "useVertexColor"), showPressure ? 1 : 0);
            glUniform1i(glGetUniformLocation(modelShaderProgram, "useRealisticLighting"), aeroUseRealisticLighting ? 1 : 0);
            glUniform1f(glGetUniformLocation(modelShaderProgram, "alpha"), 1.0f);
            glBindVertexArray(modelVAO);
            glDrawArrays(GL_TRIANGLES, 0, modelVertexCount);
            glBindVertexArray(0);
        }

        if (showObstacle) {
            glDepthMask(GL_FALSE);
            glUseProgram(modelShaderProgram);
            glm::mat4 om = glm::translate(glm::mat4(1.0f), center) *
                           glm::scale(glm::mat4(1.0f), glm::vec3(flowParams.radiusX, flowParams.radiusY, flowParams.radiusZ));
            glUniformMatrix4fv(glGetUniformLocation(modelShaderProgram, "model"), 1, GL_FALSE, glm::value_ptr(om));
            glUniformMatrix4fv(glGetUniformLocation(modelShaderProgram, "view"), 1, GL_FALSE, glm::value_ptr(view));
            glUniformMatrix4fv(glGetUniformLocation(modelShaderProgram, "projection"), 1, GL_FALSE, glm::value_ptr(projection));
            glUniform3fv(glGetUniformLocation(modelShaderProgram, "lightPos"), 1, &lightPos[0]);
            glUniform3fv(glGetUniformLocation(modelShaderProgram, "viewPos"), 1, &cameraPos[0]);
            glUniform3fv(glGetUniformLocation(modelShaderProgram, "lightColor"), 1, &lightColor[0]);
            glUniform3fv(glGetUniformLocation(modelShaderProgram, "objectColor"), 1, &obstacleColor[0]);
            glUniform1i(glGetUniformLocation(modelShaderProgram, "useLighting"), 1);
            glUniform1i(glGetUniformLocation(modelShaderProgram, "useVertexColor"), 0);
            glUniform1i(glGetUniformLocation(modelShaderProgram, "useRealisticLighting"), 0);
            glUniform1f(glGetUniformLocation(modelShaderProgram, "alpha"), obstacleAlpha);
            glBindVertexArray(obstacleVAO);
            glDrawElements(GL_TRIANGLES, obstacleIndexCount, GL_UNSIGNED_INT, 0);
            glBindVertexArray(0);
            glDepthMask(GL_TRUE);
        }

        // Slice plane
        if (showSlicePlane && aeroShowSlice) {
            createSlicePlane(aeroSliceAxis, aeroSlicePos);
            glUseProgram(lineShaderProgram);
            glUniformMatrix4fv(glGetUniformLocation(lineShaderProgram, "model"), 1, GL_FALSE, glm::value_ptr(model));
            glUniformMatrix4fv(glGetUniformLocation(lineShaderProgram, "view"), 1, GL_FALSE, glm::value_ptr(view));
            glUniformMatrix4fv(glGetUniformLocation(lineShaderProgram, "projection"), 1, GL_FALSE, glm::value_ptr(projection));
            glUniform1i(glGetUniformLocation(lineShaderProgram, "useVertexColor"), 0);
            glUniform1f(glGetUniformLocation(lineShaderProgram, "alpha"), 0.4f);
            glUniform3f(glGetUniformLocation(lineShaderProgram, "lineColor"), 0.2f, 0.8f, 1.0f);
            glLineWidth(2.0f);
            glBindVertexArray(sliceVAO);
            glDrawArrays(GL_LINE_LOOP, 0, 4);
            glBindVertexArray(0);
            glLineWidth(1.0f);
        }

        glUseProgram(lineShaderProgram);
        glUniformMatrix4fv(glGetUniformLocation(lineShaderProgram, "model"), 1, GL_FALSE, glm::value_ptr(model));
        glUniformMatrix4fv(glGetUniformLocation(lineShaderProgram, "view"), 1, GL_FALSE, glm::value_ptr(view));
        glUniformMatrix4fv(glGetUniformLocation(lineShaderProgram, "projection"), 1, GL_FALSE, glm::value_ptr(projection));
        glUniform1f(glGetUniformLocation(lineShaderProgram, "alpha"), 1.0f);
        glUniform1i(glGetUniformLocation(lineShaderProgram, "useVertexColor"), 0);

        if (showBoundingBox) {
            glUniform3fv(glGetUniformLocation(lineShaderProgram, "lineColor"), 1, &bboxColor[0]);
            glBindVertexArray(bboxVAO);
            glDrawArrays(GL_LINES, 0, 24);
            glBindVertexArray(0);
        }
        if (showAxes) {
            glBindVertexArray(axesVAO);
            glUniform3f(glGetUniformLocation(lineShaderProgram, "lineColor"), 1,0,0);
            glDrawArrays(GL_LINES, 0, 2);
            glUniform3f(glGetUniformLocation(lineShaderProgram, "lineColor"), 0,1,0);
            glDrawArrays(GL_LINES, 2, 2);
            glUniform3f(glGetUniformLocation(lineShaderProgram, "lineColor"), 0,0,1);
            glDrawArrays(GL_LINES, 4, 2);
            glBindVertexArray(0);
        }

        if (showStreamlines && streamlineVertexCount > 0) {
            glUseProgram(lineShaderProgram);
            glUniformMatrix4fv(glGetUniformLocation(lineShaderProgram, "model"), 1, GL_FALSE, glm::value_ptr(model));
            glUniformMatrix4fv(glGetUniformLocation(lineShaderProgram, "view"), 1, GL_FALSE, glm::value_ptr(view));
            glUniformMatrix4fv(glGetUniformLocation(lineShaderProgram, "projection"), 1, GL_FALSE, glm::value_ptr(projection));
            glUniform1i(glGetUniformLocation(lineShaderProgram, "useVertexColor"), 1);
            glUniform1f(glGetUniformLocation(lineShaderProgram, "alpha"), streamlineAlpha);
            glLineWidth(streamlineWidth);
            glBindVertexArray(streamlineVAO);
            glDrawArrays(GL_LINES, 0, streamlineVertexCount);
            glBindVertexArray(0);
            glLineWidth(1.0f);
        }

        if (showParticles) {
            glUseProgram(particleShaderProgram);
            glUniformMatrix4fv(glGetUniformLocation(particleShaderProgram, "model"), 1, GL_FALSE, glm::value_ptr(model));
            glUniformMatrix4fv(glGetUniformLocation(particleShaderProgram, "view"), 1, GL_FALSE, glm::value_ptr(view));
            glUniformMatrix4fv(glGetUniformLocation(particleShaderProgram, "projection"), 1, GL_FALSE, glm::value_ptr(projection));
            glUniform1f(glGetUniformLocation(particleShaderProgram, "pointSize"), particleSize);
            glBindVertexArray(particleVAO);
            glDrawArrays(GL_POINTS, 0, particleDrawCount);
            glBindVertexArray(0);
        }

        if (showLiftDrag) {
            glUseProgram(lineShaderProgram);
            glUniformMatrix4fv(glGetUniformLocation(lineShaderProgram, "model"), 1, GL_FALSE, glm::value_ptr(model));
            glUniformMatrix4fv(glGetUniformLocation(lineShaderProgram, "view"), 1, GL_FALSE, glm::value_ptr(view));
            glUniformMatrix4fv(glGetUniformLocation(lineShaderProgram, "projection"), 1, GL_FALSE, glm::value_ptr(projection));
            glUniform1i(glGetUniformLocation(lineShaderProgram, "useVertexColor"), 0);
            glUniform1f(glGetUniformLocation(lineShaderProgram, "alpha"), 1.0f);
            glLineWidth(3.0f);
            if (fabs(dragMagnitude) > 1e-6f) {
                glUniform3f(glGetUniformLocation(lineShaderProgram, "lineColor"), 1,0.35f,0.15f);
                glBindVertexArray(liftDragVAO);
                glDrawArrays(GL_LINES, 0, 2);
                glBindVertexArray(0);
            }
            if (fabs(liftMagnitude) > 1e-6f) {
                glUniform3f(glGetUniformLocation(lineShaderProgram, "lineColor"), 0.3f,1,0.3f);
                glBindVertexArray(liftDragVAO);
                glDrawArrays(GL_LINES, 2, 2);
                glBindVertexArray(0);
            }
            if (fabs(momentMagnitude) > 1e-9f) {
                glUniform3f(glGetUniformLocation(lineShaderProgram, "lineColor"), 0.8f,0.3f,1.0f);
                glBindVertexArray(liftDragVAO);
                glDrawArrays(GL_LINES, 4, 2);
                glBindVertexArray(0);
            }
            glLineWidth(1.0f);
        }

        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
        glfwPollEvents();

        // VSync toggle handling
        static bool lastVsync = vsyncEnabled;
        if (lastVsync != vsyncEnabled) {
            glfwSwapInterval(vsyncEnabled ? 1 : 0);
            lastVsync = vsyncEnabled;
        }
    }

    // Save settings on exit
    if (aeroSaveSettings) {
        saveSettings("aeros_settings.ini");
        std::cout << "[Settings] Saved to aeros_settings.ini" << std::endl;
    }

    // Cleanup
    std::cout << "[Main] Cleaning up v1.9.0..." << std::endl;
    glDeleteProgram(modelShaderProgram);
    glDeleteProgram(particleShaderProgram);
    glDeleteProgram(lineShaderProgram);
    glDeleteProgram(groundShaderProgram);
    cleanupGLResources();
    shutdownLBM();

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwTerminate();
    std::cout << "[Main] Exit OK v1.9.0" << std::endl;
    return 0;
}
