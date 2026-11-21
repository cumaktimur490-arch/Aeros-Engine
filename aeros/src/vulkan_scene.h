#pragma once
// =====================================================
// v1.33.0 — сборка сцены для Vulkan-бэкенда.
//
// OpenGL-ветка рисует сцену прямо в main.cpp своими программами. Для второго
// бэкенда нужен тот же набор объектов из тех же данных, поэтому сборка вынесена
// сюда: main.cpp передаёт только камеру и матрицы, а всё остальное (земля,
// сетка, модель, частицы, линии тока, габарит, оси, срез, стрелки) берётся из
// общих данных движка. Ничего не считается заново — используются те же массивы
// вершин и те же параметры, что и в OpenGL-ветке.
// =====================================================

#include <glm/glm.hpp>

class IRenderer;

struct VulkanSceneFrame {
    glm::mat4 view{1.0f};
    glm::mat4 projection{1.0f};
    glm::mat4 model{1.0f};
    glm::vec3 cameraPos{0.0f};
    glm::vec3 lightPos{0.0f};
    float maxDim = 1.0f;
    float groundY = 0.0f;                     // высота земли (уже посчитана в main.cpp)
    int effectiveParticleCount = 0;           // с учётом LOD, как в OpenGL-ветке
    bool modelVisibleInFrustum = true;        // отсечение по пирамиде видимости
};

// Записать сцену в бэкенд. Вызывается каждый кадр перед flushScene().
void buildVulkanScene(IRenderer* renderer, const VulkanSceneFrame& frame);
