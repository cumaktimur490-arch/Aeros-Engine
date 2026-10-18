#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>

#include <cmath>
#include <algorithm>

#include "globals.h"
#include "input.h"
#include "debug_log.h"
#include "timelapse.h"   // v1.26.0 — таймлапс физики (F7/F8)
#include "forces.h"
#include "streamlines.h"
#include "fsr.h"
#include "framegen.h"

// =====================================================
// Callbacks v1.9.0 Ultra Realistic+ — улучшено
// =====================================================
void framebuffer_size_callback(GLFWwindow* window, int width, int height) {
    (void)window;
    if (width < 1) width = 1;
    if (height < 1) height = 1;
    if (width > 16384) width = 16384;
    if (height > 16384) height = 16384;
    glViewport(0, 0, width, height);
    display_w = width;
    display_h = height;
    // v1.15.0 FSR resize
    if (fsrLowResFBO != 0) {
        resizeFSR(width, height);
    }
    // v1.16.0 FG resize
    if (fgRealFBO != 0) {
        resizeFrameGen(width, height);
    }
}

void mouse_callback(GLFWwindow* window, double xpos, double ypos) {
    if (!std::isfinite((float)xpos) || !std::isfinite((float)ypos)) return;

    // v1.9.0: improve mouse handling — ignore if ImGui wants capture (we don't have ImGui here, but keep safe)
    if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_MIDDLE) != GLFW_PRESS &&
        glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) != GLFW_PRESS) {
        lastX = (float)xpos;
        lastY = (float)ypos;
        firstMouse = false;
        return;
    }
    if (firstMouse) {
        lastX = (float)xpos;
        lastY = (float)ypos;
        firstMouse = false;
        return;
    }
    float xoffset = (float)xpos - lastX;
    float yoffset = lastY - (float)ypos;
    lastX = (float)xpos;
    lastY = (float)ypos;

    if (!std::isfinite(xoffset) || !std::isfinite(yoffset)) return;
    // Clamp large jumps (e.g., when window regains focus)
    if (fabsf(xoffset) > 200.0f) xoffset = 0;
    if (fabsf(yoffset) > 200.0f) yoffset = 0;

    xoffset *= mouseSensitivity;
    yoffset *= mouseSensitivity;

    yaw   += xoffset;
    pitch += yoffset;
    // Normalize yaw to [-360,360] to avoid overflow
    while (yaw > 360.0f) yaw -= 360.0f;
    while (yaw < -360.0f) yaw += 360.0f;
    if (pitch > 89.0f)  pitch = 89.0f;
    if (pitch < -89.0f) pitch = -89.0f;
    if (!std::isfinite(yaw)) yaw = -90.0f;
    if (!std::isfinite(pitch)) pitch = 0.0f;

    glm::vec3 front;
    front.x = cosf(glm::radians(yaw)) * cosf(glm::radians(pitch));
    front.y = sinf(glm::radians(pitch));
    front.z = sinf(glm::radians(yaw)) * cosf(glm::radians(pitch));
    if (!std::isfinite(front.x)) front = glm::vec3(0,0,-1);
    float fLen = glm::length(front);
    if (fLen > 1e-6f) front /= fLen;
    else front = glm::vec3(0,0,-1);
    cameraFront = front;
}

void scroll_callback(GLFWwindow* window, double xoffset, double yoffset) {
    (void)window; (void)xoffset;
    if (!std::isfinite((float)yoffset)) return;
    // Clamp scroll
    if (fabs(yoffset) > 10.0) yoffset = (yoffset > 0 ? 10.0 : -10.0);
    fov -= (float)yoffset * 2.0f;
    if (fov < 1.0f)  fov = 1.0f;
    if (fov > 90.0f) fov = 90.0f;
}

void processInput(GLFWwindow* window) {
    if (!window) return;
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
        glfwSetWindowShouldClose(window, true);

    float cameraSpeed = 10.0f * deltaTime * cameraSpeedMultiplier;
    if (!std::isfinite(cameraSpeed) || cameraSpeed < 0) cameraSpeed = 0.1f;
    if (cameraSpeed > 100.0f) cameraSpeed = 100.0f;
    bool ctrl = glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS;
    bool shift = glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS;
    if (ctrl) cameraSpeed *= 5.0f;
    if (shift) cameraSpeed *= 0.2f;

    glm::vec3 right = glm::cross(cameraFront, cameraUp);
    float rLen = glm::length(right);
    if (rLen < 1e-6f || !std::isfinite(rLen)) right = glm::vec3(1,0,0);
    else right = glm::normalize(right);

    // v1.9.0: ensure cameraUp stays orthogonal
    glm::vec3 up = glm::cross(right, cameraFront);
    float uLen = glm::length(up);
    if (uLen > 1e-6f && std::isfinite(uLen)) cameraUp = glm::normalize(up);
    else cameraUp = glm::vec3(0,1,0);

    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) cameraPos += cameraSpeed * cameraFront;
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) cameraPos -= cameraSpeed * cameraFront;
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) cameraPos -= right * cameraSpeed;
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) cameraPos += right * cameraSpeed;
    if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS && !ctrl) cameraPos += cameraSpeed * cameraUp;
    if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS) cameraPos -= cameraSpeed * cameraUp;
    if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS) cameraPos += cameraSpeed * cameraUp;

    // Быстрые клавиши — v1.9.0 добавлены новые
    static bool f1WasPressed = false;
    bool f1Pressed = glfwGetKey(window, GLFW_KEY_F1) == GLFW_PRESS;
    if (f1Pressed && !f1WasPressed) showModel = !showModel;
    f1WasPressed = f1Pressed;

    static bool f2WasPressed = false;
    bool f2Pressed = glfwGetKey(window, GLFW_KEY_F2) == GLFW_PRESS;
    if (f2Pressed && !f2WasPressed) {
        showPressure = !showPressure;
        if (showPressure) updateVertexColors();
    }
    f2WasPressed = f2Pressed;

    static bool f3WasPressed = false;
    bool f3Pressed = glfwGetKey(window, GLFW_KEY_F3) == GLFW_PRESS;
    if (f3Pressed && !f3WasPressed) {
        showStreamlines = !showStreamlines;
        if (showStreamlines) computeStreamlines();
    }
    f3WasPressed = f3Pressed;

    static bool f4WasPressed = false;
    bool f4Pressed = glfwGetKey(window, GLFW_KEY_F4) == GLFW_PRESS;
    if (f4Pressed && !f4WasPressed) {
        showParticles = !showParticles;
    }
    f4WasPressed = f4Pressed;

    static bool f5WasPressed = false;
    bool f5Pressed = glfwGetKey(window, GLFW_KEY_F5) == GLFW_PRESS;
    if (f5Pressed && !f5WasPressed) {
        aeroScreenshotRequested = true;
    }
    f5WasPressed = f5Pressed;

    static bool f6WasPressed = false;
    bool f6Pressed = glfwGetKey(window, GLFW_KEY_F6) == GLFW_PRESS;
    if (f6Pressed && !f6WasPressed) {
        aeroCSVExportRequested = true;
    }
    f6WasPressed = f6Pressed;

    // F7 — режим вычислений: запись таймлапса физики (v1.26.0)
    static bool f7WasPressed = false;
    bool f7Pressed = glfwGetKey(window, GLFW_KEY_F7) == GLFW_PRESS;
    if (f7Pressed && !f7WasPressed) {
        tlEnabled = !tlEnabled;
        if (tlEnabled) {
            tlPlaying = false;            // начинать просмотр во время записи нельзя
            timelapseClear();
        }
        debugLogAction("клавиша", "F7 — таймлапс физики: запись %s", tlEnabled ? "ВКЛ" : "ВЫКЛ");
    }
    f7WasPressed = f7Pressed;

    // F8 — воспроизведение таймлапса / пауза
    static bool f8WasPressed = false;
    bool f8Pressed = glfwGetKey(window, GLFW_KEY_F8) == GLFW_PRESS;
    if (f8Pressed && !f8WasPressed) {
        if (timelapseFrameCount() <= 0) {
            debugLogAction("клавиша", "F8 — таймлапс пуст, воспроизводить нечего (сначала F7)");
        } else {
            tlPlaying = !tlPlaying;
            if (tlPlaying && timelapsePlayIndex() >= timelapseFrameCount() - 1)
                timelapsePlayFromStart();     // повторное нажатие в конце — начать сначала
            debugLogAction("клавиша", "F8 — воспроизведение таймлапса: %s (скорость %.2fx)",
                           tlPlaying ? "СТАРТ" : "ПАУЗА", tlSpeed);
        }
    }
    f8WasPressed = f8Pressed;

    // F11 — самописец (таймлапс физики) на всё окно: графики и поле давления
    // разворачиваются во весь экран вместо маленькой плавающей панели.
    static bool f11WasPressed = false;
    bool f11Pressed = glfwGetKey(window, GLFW_KEY_F11) == GLFW_PRESS;
    if (f11Pressed && !f11WasPressed) {
        tlFullscreen = !tlFullscreen;
        if (tlFullscreen) tlShowWindow = true;
        debugLogAction("клавиша", "F11 — самописец на всё окно: %s", tlFullscreen ? "ВКЛ" : "ВЫКЛ");
    }
    f11WasPressed = f11Pressed;

    // F9 — режим отладки: включить/выключить журнал событий, действий и физики
    static bool f9WasPressed = false;
    bool f9Pressed = glfwGetKey(window, GLFW_KEY_F9) == GLFW_PRESS;
    if (f9Pressed && !f9WasPressed) {
        bool on = !debugLogEnabled();
        debugLogSetEnabled(on);
        debugLogSetPhysics(on);
        debugLogSetOverlay(on);
    }
    f9WasPressed = f9Pressed;

    // F10 — сохранить отладочный отчёт (файл, который можно прислать разработчику)
    static bool f10WasPressed = false;
    bool f10Pressed = glfwGetKey(window, GLFW_KEY_F10) == GLFW_PRESS;
    if (f10Pressed && !f10WasPressed) {
        g_lastDebugReportPath = debugLogSaveReport();
        if (g_lastDebugReportPath.empty())
            DBG_ERROR("не удалось сохранить отладочный отчёт");
    }
    f10WasPressed = f10Pressed;

    // Reset camera with R
    static bool rWasPressed = false;
    bool rPressed = glfwGetKey(window, GLFW_KEY_R) == GLFW_PRESS;
    if (rPressed && !rWasPressed) {
        if (ctrl) {
            // Ctrl+R reset camera to default
            cameraPos = center + glm::vec3(0, 0, maxDim * 2.5f);
            cameraFront = glm::normalize(center - cameraPos);
            yaw = -90.0f;
            pitch = 0.0f;
            fov = 45.0f;
        }
    }
    rWasPressed = rPressed;
}
