#pragma once
// =====================================================
// v1.33.0 — чистая геометрия сцены (без OpenGL и без Vulkan).
//
// OpenGL-ветка строит ограничивающий параллелепипед, оси, эллипсоид-препятствие
// и плоскость среза прямо в видеокарте (glBufferData в gl_utils.cpp). Для
// Vulkan-бэкенда эти же данные нужны на процессоре, поэтому здесь они собираются
// функциями без побочных эффектов — их можно проверить headless-тестом.
//
// Форматы:
//   линии     — 6 float на вершину: позиция (3) + цвет (3);
//   треугольники — 6 float на вершину: позиция (3) + нормаль (3).
// =====================================================

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <cmath>
#include <vector>

namespace scenegem {

// 12 рёбер ограничивающего параллелепипеда (24 вершины).
inline std::vector<float> boundingBoxLines(glm::vec3 bmin, glm::vec3 bmax, glm::vec3 color) {
    std::vector<float> out;
    if (!std::isfinite(bmin.x) || !std::isfinite(bmax.x) || glm::length(bmax - bmin) < 1e-6f) {
        bmin = glm::vec3(-1.0f);
        bmax = glm::vec3(1.0f);
    }
    const glm::vec3 corners[8] = {
        {bmin.x, bmin.y, bmin.z}, {bmax.x, bmin.y, bmin.z},
        {bmax.x, bmax.y, bmin.z}, {bmin.x, bmax.y, bmin.z},
        {bmin.x, bmin.y, bmax.z}, {bmax.x, bmin.y, bmax.z},
        {bmax.x, bmax.y, bmax.z}, {bmin.x, bmax.y, bmax.z}};
    // Пары индексов рёбер — тот же порядок, что в createBoundingBoxVAO().
    const int edges[12][2] = {{0, 1}, {1, 2}, {2, 3}, {3, 0},
                              {4, 5}, {5, 6}, {6, 7}, {7, 4},
                              {0, 4}, {1, 5}, {2, 6}, {3, 7}};
    out.reserve(24 * 6);
    for (const auto& e : edges) {
        for (int k = 0; k < 2; ++k) {
            const glm::vec3& p = corners[e[k]];
            out.insert(out.end(), {p.x, p.y, p.z, color.r, color.g, color.b});
        }
    }
    return out;
}

// Три оси из начала координат: 6 вершин, у каждой оси свой цвет.
inline std::vector<float> axisLines(float size, glm::vec3 xColor, glm::vec3 yColor, glm::vec3 zColor) {
    if (!(size > 1e-6f) || !std::isfinite(size)) size = 1.0f;
    const glm::vec3 ends[3] = {{size, 0.0f, 0.0f}, {0.0f, size, 0.0f}, {0.0f, 0.0f, size}};
    const glm::vec3 colors[3] = {xColor, yColor, zColor};
    std::vector<float> out;
    out.reserve(18);
    for (int i = 0; i < 3; ++i) {
        out.insert(out.end(), {0.0f, 0.0f, 0.0f, colors[i].r, colors[i].g, colors[i].b});
        out.insert(out.end(), {ends[i].x, ends[i].y, ends[i].z, colors[i].r, colors[i].g, colors[i].b});
    }
    return out;
}

// Эллипсоид препятствия: единичная сфера, растянутая радиусами (как
// createObstacleSphere + масштаб в матрице модели). Возвращает позиции и
// нормали (6 float на вершину), треугольниками без индексов.
inline std::vector<float> ellipsoidTriangles(glm::vec3 radii, int stacks, int slices) {
    if (stacks < 3) stacks = 3;
    if (slices < 3) slices = 3;
    if (stacks > 96) stacks = 96;
    if (slices > 96) slices = 96;
    for (int i = 0; i < 3; ++i)
        if (!(radii[i] > 1e-6f) || !std::isfinite(radii[i])) radii[i] = 1.0f;

    // Сетка точек сферы: нормаль = сама точка, позиция = точка * радиусы.
    std::vector<glm::vec3> unit((size_t)(stacks + 1) * (slices + 1));
    for (int i = 0; i <= stacks; ++i) {
        const float phi = glm::pi<float>() * (float)i / (float)stacks;
        const float y = std::cos(phi);
        const float r = std::sin(phi);
        for (int j = 0; j <= slices; ++j) {
            const float theta = 2.0f * glm::pi<float>() * (float)j / (float)slices;
            unit[(size_t)i * (slices + 1) + j] = glm::vec3(r * std::cos(theta), y, r * std::sin(theta));
        }
    }
    auto emit = [&](std::vector<float>& out, const glm::vec3& n) {
        out.insert(out.end(), {n.x * radii.x, n.y * radii.y, n.z * radii.z, n.x, n.y, n.z});
    };

    std::vector<float> out;
    out.reserve((size_t)stacks * slices * 6 * 6);
    for (int i = 0; i < stacks; ++i) {
        for (int j = 0; j < slices; ++j) {
            const glm::vec3& a = unit[(size_t)i * (slices + 1) + j];
            const glm::vec3& b = unit[(size_t)(i + 1) * (slices + 1) + j];
            const glm::vec3& a1 = unit[(size_t)i * (slices + 1) + j + 1];
            const glm::vec3& b1 = unit[(size_t)(i + 1) * (slices + 1) + j + 1];
            // На полюсах две вершины из четырёх совпадают (все точки кольца
            // i == 0 — одна и та же точка полюса), поэтому треугольник надо
            // строить по трём РАЗНЫМ вершинам, иначе он вырождается в линию и
            // на экране появляется дырка.
            if (i == 0) {
                emit(out, a); emit(out, b); emit(out, b1);
            } else if (i == stacks - 1) {
                emit(out, a); emit(out, a1); emit(out, b);
            } else {
                emit(out, a); emit(out, b); emit(out, a1);
                emit(out, a1); emit(out, b); emit(out, b1);
            }
        }
    }
    return out;
}

// Контур плоскости среза: 4 угла (8 вершин линий), как createSlicePlane().
inline std::vector<float> sliceOutlineLines(glm::vec3 bmin, glm::vec3 bmax, int axis, float pos01,
                                            glm::vec3 color) {
    if (!std::isfinite(bmin.x) || glm::length(bmax - bmin) < 1e-6f) {
        bmin = glm::vec3(-1.0f);
        bmax = glm::vec3(1.0f);
    }
    if (!std::isfinite(pos01)) pos01 = 0.5f;
    pos01 = glm::clamp(pos01, 0.0f, 1.0f);
    const glm::vec3 p = bmin + (bmax - bmin) * pos01;
    glm::vec3 c[4];
    if (axis == 0) {
        const float x = p.x;
        c[0] = {x, bmin.y, bmin.z}; c[1] = {x, bmax.y, bmin.z};
        c[2] = {x, bmax.y, bmax.z}; c[3] = {x, bmin.y, bmax.z};
    } else if (axis == 1) {
        const float y = p.y;
        c[0] = {bmin.x, y, bmin.z}; c[1] = {bmax.x, y, bmin.z};
        c[2] = {bmax.x, y, bmax.z}; c[3] = {bmin.x, y, bmax.z};
    } else {
        const float z = p.z;
        c[0] = {bmin.x, bmin.y, z}; c[1] = {bmax.x, bmin.y, z};
        c[2] = {bmax.x, bmax.y, z}; c[3] = {bmin.x, bmax.y, z};
    }
    std::vector<float> out;
    out.reserve(8 * 6);
    for (int i = 0; i < 4; ++i) {
        const glm::vec3& a = c[i];
        const glm::vec3& b = c[(i + 1) % 4];
        out.insert(out.end(), {a.x, a.y, a.z, color.r, color.g, color.b});
        out.insert(out.end(), {b.x, b.y, b.z, color.r, color.g, color.b});
    }
    return out;
}

// Стрелки подъёмной силы, сопротивления и момента (как updateLiftDragArrows).
inline std::vector<float> liftDragLines(glm::vec3 origin, glm::vec3 drag, float dragMag,
                                        glm::vec3 lift, float liftMag, glm::vec3 moment,
                                        float momentMag, float maxDim) {
    std::vector<float> out;
    if (!std::isfinite(maxDim) || maxDim < 1e-6f) return out;
    if (!std::isfinite(origin.x)) return out;
    const float scale = 0.5f / (maxDim + 1e-6f);
    auto push = [&](glm::vec3 end, glm::vec3 color) {
        if (!std::isfinite(end.x) || !std::isfinite(end.y) || !std::isfinite(end.z)) return;
        out.insert(out.end(), {origin.x, origin.y, origin.z, color.r, color.g, color.b});
        out.insert(out.end(), {end.x, end.y, end.z, color.r, color.g, color.b});
    };
    if (std::fabs(dragMag) > 1e-6f)
        push(origin + drag * scale, glm::vec3(1.0f, 0.35f, 0.15f));
    if (std::fabs(liftMag) > 1e-6f)
        push(origin + lift * scale, glm::vec3(0.3f, 1.0f, 0.3f));
    if (std::fabs(momentMag) > 1e-9f) {
        const float len = glm::length(moment);
        if (len > 1e-9f) push(origin + (moment / len) * maxDim * 0.3f, glm::vec3(0.8f, 0.3f, 1.0f));
    }
    return out;
}

}  // namespace scenegem
