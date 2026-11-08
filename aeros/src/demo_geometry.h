#pragma once
// =====================================================
// v1.34.0 — демонстрационные тела для главного меню.
//
// Зачем: при первом запуске программы в сцене нет модели, а единственный способ
// что-то увидеть — внешний STL-файл. Главное меню теперь предлагает готовые
// тела, чтобы проверить обтекание можно было в два щелчка, не ища файл на диске.
//
// КАК. Все демо-тела — эллипсоиды (аналитическая поверхность, замкнутая,
// с согласованными нормалями). Треугольники строит та же функция, что и
// геометрию сцены Vulkan (`scenegem::ellipsoidTriangles`), поэтому формат
// вершин совпадает с тем, что ждёт модель: 3 float позиции + 3 float нормали на
// вершину. Никаких «специальных» загрузчиков в обход voxel/SDF/LBM нет:
// демо-тело проходит тот же путь, что STL-файл.
//
// Ориентация: поток по умолчанию идёт вдоль +X, поэтому:
//   * крыло — тонкое по Y, размах по Z;
//   * дирижабль — вытянутый по X (по потоку).
// =====================================================

#include <glm/glm.hpp>
#include <cmath>
#include <vector>

#include "scene_geometry.h"

namespace demogeom {

enum class Body {
    Sphere = 0,     // сфера r = 1
    Wing   = 1,     // крыло-эллипсоид 2.4 x 0.18 x 6.0 (хорда x толщина x размах)
    Airship = 2     // дирижабль 8 x 1.4 x 1.4 (вытянут по потоку)
};

inline int count() { return 3; }

inline const char* nameRu(Body b) {
    switch (b) {
        case Body::Sphere:  return "Сфера";
        case Body::Wing:    return "Крыло";
        case Body::Airship: return "Дирижабль";
    }
    return "Тело";
}

inline const char* nameEn(Body b) {
    switch (b) {
        case Body::Sphere:  return "Sphere";
        case Body::Wing:    return "Wing";
        case Body::Airship: return "Airship";
    }
    return "Body";
}

// Габариты (полуоси). Значения — в метрах: интерфейс движка тоже в метрах.
inline glm::vec3 radii(Body b) {
    switch (b) {
        case Body::Sphere:  return glm::vec3(1.0f, 1.0f, 1.0f);
        case Body::Wing:    return glm::vec3(2.4f, 0.18f, 6.0f);
        case Body::Airship: return glm::vec3(8.0f, 1.4f, 1.4f);
    }
    return glm::vec3(1.0f);
}

// Построить вершины и нормали. Формат — как у g_vertices/g_normals: по 3 float
// на вершину в каждом массиве, треугольники подряд (9 float на треугольник).
// Возвращает false только при вырожденных параметрах — тогда массивы пусты и
// вызывающий код обязан не подменять сцену пустой моделью.
inline bool build(Body b, std::vector<float>& positions, std::vector<float>& normals,
                  int stacks = 28, int slices = 40) {
    positions.clear();
    normals.clear();

    const glm::vec3 r = radii(b);
    for (int i = 0; i < 3; ++i)
        if (!(r[i] > 1e-6f) || !std::isfinite(r[i])) return false;

    // scenegem отдаёт 6 float на вершину: позиция + нормаль.
    const std::vector<float> inter = scenegem::ellipsoidTriangles(r, stacks, slices);
    if (inter.empty() || inter.size() % 6 != 0) return false;

    const size_t verts = inter.size() / 6;
    positions.reserve(verts * 3);
    normals.reserve(verts * 3);
    for (size_t v = 0; v < verts; ++v) {
        positions.push_back(inter[v * 6 + 0]);
        positions.push_back(inter[v * 6 + 1]);
        positions.push_back(inter[v * 6 + 2]);
        normals.push_back(inter[v * 6 + 3]);
        normals.push_back(inter[v * 6 + 4]);
        normals.push_back(inter[v * 6 + 5]);
    }
    return !positions.empty();
}

}  // namespace demogeom
