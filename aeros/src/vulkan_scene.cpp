// =====================================================
// v1.33.0 — сборка сцены для Vulkan-бэкенда (см. vulkan_scene.h).
//
// Важно: здесь нет ни одного вызова OpenGL. Всё, что нужно, уже лежит в
// общих массивах движка (g_vertices/g_normals/g_vertexColors, particlePositions,
// streamlineCpuVerts) — те же данные, что загружаются в VBO для OpenGL.
// =====================================================

#include "vulkan_scene.h"

#include "globals.h"
#include "flow_params.h"
#include "forces.h"
#include "scene_geometry.h"
#include "vulkan_renderer.h"

#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <vector>

namespace {

// Сколько делений у сетки на земле: в OpenGL-ветке рисуется 82 линии
// (glDrawArrays(GL_LINES, 0, 82*2)), то есть 41 деление в каждую сторону.
constexpr int kGridDivisions = 41;

glm::vec3 safeColor(glm::vec3 c, glm::vec3 fallback) {
    if (!std::isfinite(c.x) || !std::isfinite(c.y) || !std::isfinite(c.z)) return fallback;
    return glm::clamp(c, glm::vec3(0.0f), glm::vec3(1.0f));
}

}  // namespace

void buildVulkanScene(IRenderer* renderer, const VulkanSceneFrame& frame) {
    if (!renderer || !renderer->isVulkanBackend()) return;

    const glm::mat4 viewProj = frame.projection * frame.view;
    const glm::vec3 lightDir = frame.lightPos - frame.cameraPos;  // направление «к свету»
    renderer->beginScene(viewProj, frame.model, frame.cameraPos, lightDir, lightingEnabled);

    // ---------- земля и сетка ----------
    if (showGroundPlane || aeroGroundEffect) {
        float groundY = frame.groundY;
        if (!std::isfinite(groundY)) groundY = minBB.y - frame.maxDim * 0.1f;
        // В OpenGL-ветке земля — квадрат -1..1, растянутый на maxDim*2.5,
        // значит полуразмер равен maxDim*2.5.
        const float half = std::max(frame.maxDim * 2.5f, 1e-3f);
        const float groundA = std::isfinite(groundAlpha) ? glm::clamp(groundAlpha, 0.0f, 1.0f) : 1.0f;
        renderer->sceneGroundPlane(center, half, groundY, safeColor(groundColor, glm::vec3(0.25f)),
                                   groundA);
        renderer->sceneGrid(center, half, groundY, kGridDivisions, glm::vec3(0.3f, 0.3f, 0.35f), 0.18f);
    }

    // ---------- модель ----------
    if (showModel && frame.modelVisibleInFrustum && modelVertexCount > 0) {
        const size_t needed = (size_t)modelVertexCount * 3;
        if (g_vertices.size() >= needed && g_normals.size() >= needed) {
            // Цвета вершин (карта давления или поле МКЭ) используются только когда
            // они действительно есть и включён соответствующий режим — иначе
            // множитель цвета, как в OpenGL-ветке при useVertexColor = 0.
            const bool wantVertexColors = (showPressure || caeColorOverride) &&
                                          g_vertexColors.size() >= needed;
            const float* colors = wantVertexColors ? g_vertexColors.data() : nullptr;
            const glm::vec3 tint = wantVertexColors ? glm::vec3(1.0f)
                                                    : safeColor(modelColor, glm::vec3(0.7f));
            renderer->sceneModel(g_vertices.data(), g_normals.data(), colors, modelVertexCount, tint);
        }
    }

    // ---------- препятствие (эллипсоид) ----------
    if (showObstacle) {
        const glm::vec3 radii(std::isfinite(flowParams.radiusX) ? flowParams.radiusX : 1.0f,
                              std::isfinite(flowParams.radiusY) ? flowParams.radiusY : 1.0f,
                              std::isfinite(flowParams.radiusZ) ? flowParams.radiusZ : 1.0f);
        // Геометрия не меняется, пока не изменятся радиусы: считаем один раз.
        static std::vector<float> cached;
        static glm::vec3 cachedRadii(0.0f);
        if (cached.empty() || cachedRadii != radii) {
            cached = scenegem::ellipsoidTriangles(radii, 24, 32);
            cachedRadii = radii;
        }
        if (!cached.empty()) {
            // Позиции уже умножены на радиусы, поэтому матрица — только перенос.
            const glm::mat4 placement = glm::translate(glm::mat4(1.0f), center);
            renderer->sceneTriangles(cached.data(), cached.data() + 3, nullptr,
                                     (int)(cached.size() / 6), placement,
                                     safeColor(obstacleColor, glm::vec3(0.4f, 0.6f, 1.0f)),
                                     std::isfinite(obstacleAlpha) ? glm::clamp(obstacleAlpha, 0.0f, 1.0f) : 1.0f);
        }
    }

    // ---------- линии тока ----------
    if (showStreamlines && streamlineVertexCount > 0 &&
        streamlineCpuVerts.size() >= (size_t)streamlineVertexCount * 6) {
        renderer->sceneLines(streamlineCpuVerts.data(), streamlineVertexCount, glm::vec3(1.0f),
                             std::isfinite(streamlineAlpha) ? glm::clamp(streamlineAlpha, 0.0f, 1.0f) : 1.0f,
                             std::isfinite(streamlineWidth) ? streamlineWidth : 1.0f, frame.model);
    }

    // ---------- частицы ----------
    if (showParticles && frame.effectiveParticleCount > 0) {
        const size_t needed = (size_t)frame.effectiveParticleCount * 3;
        if (particlePositions.size() >= needed) {
            const float* colors = particleColors.size() >= needed ? particleColors.data() : nullptr;
            renderer->sceneParticles(particlePositions.data(), colors, frame.effectiveParticleCount,
                                     std::isfinite(particleSize) ? particleSize : 1.0f);
        }
    }

    // ---------- габарит, оси, срез, стрелки ----------
    if (showBoundingBox) {
        // Кэш пересобирается при смене габарита: после загрузки другой модели
        // старый параллелепипед показывать нельзя.
        static std::vector<float> cachedLines;
        static glm::vec3 cachedMin(0.0f), cachedMax(0.0f);
        static glm::vec3 cachedColor(0.0f);
        const glm::vec3 boxColor = safeColor(bboxColor, glm::vec3(1.0f));
        if (cachedLines.empty() || cachedMin != minBB || cachedMax != maxBB || cachedColor != boxColor) {
            cachedLines = scenegem::boundingBoxLines(minBB, maxBB, boxColor);
            cachedMin = minBB;
            cachedMax = maxBB;
            cachedColor = boxColor;
        }
        if (!cachedLines.empty())
            renderer->sceneLines(cachedLines.data(), (int)(cachedLines.size() / 6), glm::vec3(1.0f),
                                 1.0f, 1.0f, frame.model);
    }
    if (showAxes) {
        // Длина осей в OpenGL-ветке — maxDim * 0.6 (createAxesVAO при загрузке модели).
        static std::vector<float> cachedAxes;
        static float cachedAxisSize = 0.0f;
        const float axisSize = frame.maxDim * 0.6f;
        if (cachedAxes.empty() || cachedAxisSize != axisSize) {
            cachedAxes = scenegem::axisLines(axisSize, glm::vec3(1.0f, 0.0f, 0.0f),
                                             glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f));
            cachedAxisSize = axisSize;
        }
        renderer->sceneLines(cachedAxes.data(), (int)(cachedAxes.size() / 6), glm::vec3(1.0f),
                             1.0f, 1.0f, frame.model);
    }
    if (showSlicePlane && aeroShowSlice) {
        const std::vector<float> outline = scenegem::sliceOutlineLines(
            minBB, maxBB, aeroSliceAxis, aeroSlicePos, glm::vec3(0.2f, 0.8f, 1.0f));
        renderer->sceneLines(outline.data(), (int)(outline.size() / 6), glm::vec3(1.0f), 0.4f, 2.0f,
                             frame.model);
    }
    if (showLiftDrag) {
        const std::vector<float> arrows = scenegem::liftDragLines(
            centerOfPressure, dragVector, dragMagnitude, liftVector, liftMagnitude, momentVector,
            momentMagnitude, frame.maxDim);
        if (!arrows.empty())
            renderer->sceneLines(arrows.data(), (int)(arrows.size() / 6), glm::vec3(1.0f), 1.0f, 3.0f,
                                 frame.model);
    }

    renderer->flushScene();
}
