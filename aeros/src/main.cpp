// =====================================================
// AeroS Engine — точка входа, главный цикл и рендер
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
#include "lbm.h"

int main() {
    if (!glfwInit()) {
        MessageBoxA(nullptr, "Failed to init GLFW", "Error", MB_ICONERROR);
        return -1;
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);

    GLFWwindow* window = glfwCreateWindow(SCR_WIDTH, SCR_HEIGHT, "AeroS Engine", nullptr, nullptr);
    if (!window) {
        MessageBoxA(nullptr, "Failed to create GLFW window", "Error", MB_ICONERROR);
        glfwTerminate();
        return -1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
    glfwSetCursorPosCallback(window, mouse_callback);
    glfwSetScrollCallback(window, scroll_callback);
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        MessageBoxA(nullptr, "Failed to init GLAD", "Error", MB_ICONERROR);
        glfwTerminate();
        return -1;
    }
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    createObstacleSphere(48, 48);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330 core");

    unsigned int modelShaderProgram    = compileProgram(vertexShaderSource, fragmentShaderSource);
    unsigned int particleShaderProgram = compileProgram(particleVertexShaderSource, particleFragmentShaderSource);
    unsigned int lineShaderProgram     = compileProgram(lineVertexShaderSource, lineFragmentShaderSource);
    if (modelShaderProgram == 0 || particleShaderProgram == 0 || lineShaderProgram == 0) {
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        glfwTerminate();
        return -1;
    }

    std::string modelPath = openFileDialog();
    if (modelPath.empty()) {
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        glfwTerminate();
        return 0;
    }
    if (!loadModel(modelPath)) {
        MessageBoxA(nullptr, "Failed to load model.", "Error", MB_ICONERROR);
    }

    glfwShowWindow(window);

    static float prevSpeed = flowSpeed;
    static float prevAz    = flowAzimuth;
    static float prevEl    = flowElevation;
    static float prevWake  = wakeStrength;
    static float prevStro  = strouhal;
    static float prevWL    = wakeLength;

    while (!glfwWindowShouldClose(window)) {
        float currentFrame = glfwGetTime();
        deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;
        // Ограничиваем dt: на первом кадре (после диалога/вокселизации) он может
        // исчисляться секундами, что телепортирует все частицы.
        if (deltaTime > 0.1f)   deltaTime = 0.1f;
        if (deltaTime <= 0.0f)  deltaTime = 0.0001f;

        if (limitFPS) {
            double target = 1.0 / maxFPS;
            while (glfwGetTime() - currentFrame < target)
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        processInput(window);

        // ---- физика: LBM-солвер или синтетическое поле ----
        bool lbmActive = (physicsMode == 1 && useCUDA == 1 && lbmReady());
        if (lbmActive) {
            float cd = 0.0f, cl = 0.0f;
            stepLBM(lbmSubsteps, flowParams, lbmNu, &cd, &cl);
            lbmCd = cd; lbmCl = cl;
            dragMagnitude = cd; liftMagnitude = cl;

            // оценка Re для UI: U*D/nu (в ячейках решётки)
            float cm = (flowParams.cellSizeX + flowParams.cellSizeY + flowParams.cellSizeZ) / 3.0f;
            float Dcells = 2.0f * fmaxf(flowParams.radiusY, flowParams.radiusZ) / (cm + 1e-9f);
            float Ulat = 0.02f + 0.013f * fmaxf(flowSpeed, 0.0f);
            if (Ulat > 0.15f) Ulat = 0.15f;
            lbmRe = Ulat * Dcells / (lbmNu + 1e-6f);
        } else {
            computeLiftDrag();
        }
        updateLiftDragArrows();

                if (showParticles) {
            if (lbmActive) {
                updateParticlesLBM(particlePositions, particleColors,
                                   particleDrawCount, flowParams, deltaTime);
                // GL-буферы обновляет CPU-путь; для LBM делаем это здесь
                if (particleVAO != 0) {
                    glBindBuffer(GL_ARRAY_BUFFER, particleVBO_pos);
                    glBufferSubData(GL_ARRAY_BUFFER, 0, particlePositions.size()*sizeof(float), particlePositions.data());
                    glBindBuffer(GL_ARRAY_BUFFER, particleVBO_col);
                    glBufferSubData(GL_ARRAY_BUFFER, 0, particleColors.size()*sizeof(float), particleColors.data());
                    glBindBuffer(GL_ARRAY_BUFFER, 0);
                }
            } else {
                updateParticles(deltaTime);
            }
        }
        if (showPressure) {
            if (lbmActive) surfacePressureLBM(g_vertices, g_vertexColors,
                                              modelVertexCount, flowParams);
            else           updateVertexColors();
        }

        if (showStreamlines && (fabs(prevSpeed-flowSpeed) > 1e-3f ||
                                fabs(prevAz-flowAzimuth) > 1e-3f ||
                                fabs(prevEl-flowElevation) > 1e-3f ||
                                fabs(prevWake-wakeStrength) > 1e-3f ||
                                fabs(prevStro-strouhal) > 1e-3f ||
                                fabs(prevWL-wakeLength) > 1e-3f)) {
            prevSpeed = flowSpeed;
            prevAz = flowAzimuth;
            prevEl = flowElevation;
            prevWake = wakeStrength;
            prevStro = strouhal;
            prevWL = wakeLength;
            computeStreamlines();
        }

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
            glUniform1f(glGetUniformLocation(modelShaderProgram, "alpha"), obstacleAlpha);
            glBindVertexArray(obstacleVAO);
            glDrawElements(GL_TRIANGLES, obstacleIndexCount, GL_UNSIGNED_INT, 0);
            glBindVertexArray(0);
            glDepthMask(GL_TRUE);
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
            glPointSize(particleSize);
            glBindVertexArray(particleVAO);
            glDrawArrays(GL_POINTS, 0, particleDrawCount);
            glBindVertexArray(0);
            glPointSize(1.0f);
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
            glLineWidth(1.0f);
        }

        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwTerminate();
    return 0;
}