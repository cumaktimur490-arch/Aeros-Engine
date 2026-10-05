#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>

#include <cmath>

#include "globals.h"
#include "input.h"
#include "forces.h"

// =====================================================
// Callbacks v1.8.0 — улучшено
// =====================================================
void framebuffer_size_callback(GLFWwindow* window, int width, int height) {
    (void)window;
    if (width < 1) width = 1;
    if (height < 1) height = 1;
    glViewport(0, 0, width, height);
    display_w = width;
    display_h = height;
}

void mouse_callback(GLFWwindow* window, double xpos, double ypos) {

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

    xoffset *= mouseSensitivity;
    yoffset *= mouseSensitivity;

    yaw   += xoffset;
    pitch += yoffset;
    if (pitch > 89.0f)  pitch = 89.0f;
    if (pitch < -89.0f) pitch = -89.0f;
    if (!std::isfinite(yaw)) yaw = -90.0f;
    if (!std::isfinite(pitch)) pitch = 0.0f;

    glm::vec3 front;
    front.x = cosf(glm::radians(yaw)) * cosf(glm::radians(pitch));
    front.y = sinf(glm::radians(pitch));
    front.z = sinf(glm::radians(yaw)) * cosf(glm::radians(pitch));
    if (!std::isfinite(front.x)) front = glm::vec3(0,0,-1);
    cameraFront = glm::normalize(front);
}

void scroll_callback(GLFWwindow* window, double xoffset, double yoffset) {
    (void)window; (void)xoffset;
    if (!std::isfinite((float)yoffset)) return;
    fov -= (float)yoffset * 2.0f;
    if (fov < 1.0f)  fov = 1.0f;
    if (fov > 90.0f) fov = 90.0f;
}

void processInput(GLFWwindow* window) {
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
        glfwSetWindowShouldClose(window, true);

    float cameraSpeed = 10.0f * deltaTime * cameraSpeedMultiplier;
    if (!std::isfinite(cameraSpeed) || cameraSpeed < 0) cameraSpeed = 0.1f;
    if (cameraSpeed > 100.0f) cameraSpeed = 100.0f;
    if (glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS ||
        glfwGetKey(window, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS)
        cameraSpeed *= 5.0f;
    if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS)
        cameraSpeed *= 0.2f;

    glm::vec3 right = glm::normalize(glm::cross(cameraFront, cameraUp));
    if (!std::isfinite(right.x)) right = glm::vec3(1,0,0);

    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) cameraPos += cameraSpeed * cameraFront;
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) cameraPos -= cameraSpeed * cameraFront;
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) cameraPos -= right * cameraSpeed;
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) cameraPos += right * cameraSpeed;
    if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS) cameraPos += cameraSpeed * cameraUp;
    if (glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS && glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS) {
        // Ctrl+Space уже обработан, но оставляем
    }
    if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS) cameraPos -= cameraSpeed * cameraUp;
    if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS) cameraPos += cameraSpeed * cameraUp;

    // Быстрые клавиши
    static bool f1WasPressed = false;
    bool f1Pressed = glfwGetKey(window, GLFW_KEY_F1) == GLFW_PRESS;
    if (f1Pressed && !f1WasPressed) {
        showModel = !showModel;
    }
    f1WasPressed = f1Pressed;

    static bool f2WasPressed = false;
    bool f2Pressed = glfwGetKey(window, GLFW_KEY_F2) == GLFW_PRESS;
    if (f2Pressed && !f2WasPressed) {
        showPressure = !showPressure;
        if (showPressure) updateVertexColors();
    }
    f2WasPressed = f2Pressed;
}
