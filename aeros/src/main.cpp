// =====================================================
// AeroS Engine — точка входа, главный цикл и рендер v1.8.0
// =====================================================

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include <windows.h>

#include <iostream>
#include <thread>
#include <chrono>
#include <cmath>
#include <algorithm>

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

#ifdef _OPENMP
#include <omp.h>
#endif

static void APIENTRY glDebugCallback(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar* message, const void* userParam) {
    (void)source; (void)id; (void)length; (void)userParam;
    if (severity == GL_DEBUG_SEVERITY_NOTIFICATION) return;
    std::cerr << "[GL Debug] type=" << type << " severity=" << severity << " msg=" << message << std::endl;
}

int main() {
#ifdef _OPENMP
    perfOpenMPThreads = omp_get_max_threads();
    std::cout << "[Perf] OpenMP enabled with " << perfOpenMPThreads << " threads" << std::endl;
#else
    perfOpenMPThreads = 1;
    std::cout << "[Perf] OpenMP not enabled (single thread)" << std::endl;
#endif

    if (!glfwInit()) {
        MessageBoxA(nullptr, "Failed to init GLFW", "Error", MB_ICONERROR);
        return -1;
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_SAMPLES, 4); // MSAA
    glfwWindowHint(GLFW_DOUBLEBUFFER, GLFW_TRUE);

    GLFWwindow* window = glfwCreateWindow(SCR_WIDTH, SCR_HEIGHT, "Aeros Engine v1.8.0 Realistic Aero+", nullptr, nullptr);
    if (!window) {
        MessageBoxA(nullptr, "Failed to create GLFW window", "Error", MB_ICONERROR);
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
        MessageBoxA(nullptr, "Failed to init GLAD", "Error", MB_ICONERROR);
        glfwTerminate();
        return -1;
    }

    // OpenGL debug if available
    if (GLAD_GL_KHR_debug) {
        glEnable(GL_DEBUG_OUTPUT);
        glDebugMessageCallback(glDebugCallback, nullptr);
    }

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_MULTISAMPLE);
    glEnable(GL_PROGRAM_POINT_SIZE);

    std::cout << "[GL] Vendor: " << glGetString(GL_VENDOR) << " Renderer: " << glGetString(GL_RENDERER) << " Version: " << glGetString(GL_VERSION) << std::endl;

    createObstacleSphere(48, 48);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330 core");

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
        MessageBoxA(nullptr, ("Failed to load model: " + modelPath).c_str(), "Error", MB_ICONERROR);
    }

    glfwShowWindow(window);

    static float prevSpeed = flowSpeed;
    static float prevAz    = flowAzimuth;
    static float prevEl    = flowElevation;
    static float prevWake  = wakeStrength;
    static float prevStro  = strouhal;
    static float prevWL    = wakeLength;
    static float prevAlt   = 0.0f;
    static float prevGroundH = aeroGroundHeight;
    static bool prevGroundEn = aeroGroundEffect;

    float autoRotateAngle = 0.0f;

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
            autoRotateAngle += deltaTime * aeroAutoRotateSpeed;
            if (autoRotateAngle > 360.0f) autoRotateAngle -= 360.0f;
            flowAzimuth = autoRotateAngle;
        }

        processInput(window);

        auto frameStart = std::chrono::high_resolution_clock::now();

        auto tLBM0 = std::chrono::high_resolution_clock::now();
        if (lbmParams.enabled) {
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

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        drawUI();

        glClearColor(bgColor.x, bgColor.y, bgColor.z, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glm::mat4 view = glm::lookAt(cameraPos, cameraPos + cameraFront, cameraUp);
        glm::mat4 projection = glm::perspective(glm::radians(fov),
            (float)display_w/(float)display_h, 0.05f, maxDim*50.0f);
        glm::mat4 model = glm::mat4(1.0f);

        glm::vec3 lightPos = center + glm::vec3(maxDim*2.0f, maxDim*2.5f, maxDim*2.0f);
        glm::vec3 lightColor(1.0f);

        if (showGroundPlane || aeroGroundEffect) {
            float groundY = g_voxMinY + aeroGroundHeight;
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

            // Grid
            if (gridVAO != 0) {
                glUseProgram(lineShaderProgram);
                glUniformMatrix4fv(glGetUniformLocation(lineShaderProgram, "model"), 1, GL_FALSE, glm::value_ptr(groundModel));
                glUniformMatrix4fv(glGetUniformLocation(lineShaderProgram, "view"), 1, GL_FALSE, glm::value_ptr(view));
                glUniformMatrix4fv(glGetUniformLocation(lineShaderProgram, "projection"), 1, GL_FALSE, glm::value_ptr(projection));
                glUniform1i(glGetUniformLocation(lineShaderProgram, "useVertexColor"), 0);
                glUniform1f(glGetUniformLocation(lineShaderProgram, "alpha"), 0.15f);
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

    // Cleanup
    std::cout << "[Main] Cleaning up..." << std::endl;
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
    std::cout << "[Main] Exit OK" << std::endl;
    return 0;
}
