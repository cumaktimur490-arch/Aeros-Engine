#pragma once
// =====================================================
// v1.36.0 — BVH над треугольниками для вокселизации.
//
// Зачем. До этой версии `insideMeshOptimized()` перебирал ВСЕ треугольники для
// КАЖДОГО вокселя (с отсечением только по AABB вдоль одной оси луча). На сетке
// 48³ это 110 592 вокселя × N треугольников; для модели на 500 000
// треугольников — ~5.5·10¹⁰ проверок, то есть часы на одном потоке и
// «программа не отвечает» у пользователя.
//
// Здесь строится BVH (median split по самой длинной оси центров, листья по
// 4 треугольника): запрос луча — O(log N) в среднем. Обход идёт с
// упорядочиванием детей по расстоянию до входа в узел, поэтому счётчик
// пересечений не требует полного обхода дерева.
//
// Отдельно — устойчивость теста «внутри/снаружи». Один луч вдоль +X, попавший
// ровно в ребро, засчитывается либо дважды, либо ни разу; на мешах с миллионом
// мелких треугольников это постоянное явление. Поэтому `inside()` бросает ТРИ
// луча по разным осям из точек, сдвинутых на иррациональную долю ячейки, и
// решает большинством голосов. Сдвиг выводит луч из вырожденной конфигурации
// (ребро/вершина), голосование страхует от оставшихся совпадений.
// =====================================================

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <vector>

#include <glm/glm.hpp>

namespace meshbvh {

struct Triangle {
    glm::vec3 v0, v1, v2;
    glm::vec3 minB, maxB;
    glm::vec3 centroid;
};

inline Triangle makeTriangle(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c) {
    Triangle t;
    t.v0 = a; t.v1 = b; t.v2 = c;
    t.minB = glm::min(a, glm::min(b, c));
    t.maxB = glm::max(a, glm::max(b, c));
    t.centroid = (a + b + c) * (1.0f / 3.0f);
    return t;
}

// Площадь удвоенного треугольника — чтобы отбрасывать вырожденные.
inline bool isDegenerate(const Triangle& t) {
    const float area2 = glm::length(glm::cross(t.v1 - t.v0, t.v2 - t.v0));
    return !(area2 > 1e-14f) || !std::isfinite(area2);
}

// -----------------------------------------------------
// Пересечение луча с треугольником (Мёллера–Трумбора).
// Считаем в double: на больших координатах (модель в мм) float даёт заметную
// погрешность в барицентрике. Тест двусторонний — ориентация нормалей в STL
// после CAD-экспорта не гарантирована.
// Возвращает true, если попадание ближе maxT и не ближе minT (t > minT).
// -----------------------------------------------------
inline bool rayTriangle(const glm::vec3& orig, const glm::vec3& dir,
                        const glm::vec3& v0, const glm::vec3& v1, const glm::vec3& v2,
                        double minT, double maxT, double* outT) {
    const double e1x = (double)v1.x - v0.x, e1y = (double)v1.y - v0.y, e1z = (double)v1.z - v0.z;
    const double e2x = (double)v2.x - v0.x, e2y = (double)v2.y - v0.y, e2z = (double)v2.z - v0.z;
    const double dx = dir.x, dy = dir.y, dz = dir.z;

    const double px = dy * e2z - dz * e2y;
    const double py = dz * e2x - dx * e2z;
    const double pz = dx * e2y - dy * e2x;
    const double det = e1x * px + e1y * py + e1z * pz;
    // epsilon относительный: у мелких треугольников det маленький законно
    const double scale = std::fabs(e1x * e2y - e1y * e2x) + std::fabs(e1y * e2z - e1z * e2y) +
                         std::fabs(e1z * e2x - e1x * e2z) + 1e-30;
    if (std::fabs(det) < 1e-12 * scale) return false;
    const double inv = 1.0 / det;

    const double tx = (double)orig.x - v0.x;
    const double ty = (double)orig.y - v0.y;
    const double tz = (double)orig.z - v0.z;

    const double u = (tx * px + ty * py + tz * pz) * inv;
    if (u < 0.0 || u > 1.0) return false;

    const double qx = ty * e1z - tz * e1y;
    const double qy = tz * e1x - tx * e1z;
    const double qz = tx * e1y - ty * e1x;
    const double v = (dx * qx + dy * qy + dz * qz) * inv;
    if (v < 0.0 || u + v > 1.0) return false;

    const double t = (e2x * qx + e2y * qy + e2z * qz) * inv;
    if (!(t > minT) || t >= maxT) return false;
    if (outT) *outT = t;
    return true;
}

// -----------------------------------------------------
// BVH. Узлы в плоском массиве, дети лежат подряд (left, left+1).
// -----------------------------------------------------
class BVH {
public:
    void build(std::vector<Triangle> tris) {
        tris_ = std::move(tris);
        nodes_.clear();
        if (tris_.empty()) return;
        nodes_.reserve(tris_.size() * 2 / kLeafSize + 2);
        buildRecursive(0, (int)tris_.size(), 0);
    }

    bool empty() const { return nodes_.empty(); }
    std::size_t triangleCount() const { return tris_.size(); }
    std::size_t nodeCount() const { return nodes_.size(); }

    // Число пересечений луча с мешем (t > 0).
    int countIntersections(const glm::vec3& origin, const glm::vec3& dir) const {
        if (nodes_.empty()) return 0;
        int hits = 0;
        const glm::vec3 invDir(
            (dir.x != 0.0f) ? 1.0f / dir.x : std::numeric_limits<float>::infinity(),
            (dir.y != 0.0f) ? 1.0f / dir.y : std::numeric_limits<float>::infinity(),
            (dir.z != 0.0f) ? 1.0f / dir.z : std::numeric_limits<float>::infinity());

        struct Entry { int node; float tNear; };
        Entry stack[96];
        int sp = 0;
        float t0, t1;
        if (!slab(origin, invDir, nodes_[0], t0, t1)) return 0;
        stack[sp++] = {0, t0};

        while (sp > 0) {
            const Entry e = stack[--sp];
            const Node& n = nodes_[e.node];
            if (n.count > 0) {
                for (int i = 0; i < n.count; ++i) {
                    const Triangle& t = tris_[n.start + i];
                    if (rayTriangle(origin, dir, t.v0, t.v1, t.v2, 1e-7, 1e30, nullptr)) ++hits;
                }
                continue;
            }
            float a0, a1, b0, b1;
            const bool hitA = slab(origin, invDir, nodes_[n.left], a0, a1);
            const bool hitB = slab(origin, invDir, nodes_[n.right], b0, b1);
            // Дальнего кладём первым — ближний обработается следующим.
            if (hitA && hitB) {
                if (a0 <= b0) {
                    stack[sp++] = {n.right, b0};
                    stack[sp++] = {n.left, a0};
                } else {
                    stack[sp++] = {n.left, a0};
                    stack[sp++] = {n.right, b0};
                }
            } else if (hitA) {
                stack[sp++] = {n.left, a0};
            } else if (hitB) {
                stack[sp++] = {n.right, b0};
            }
        }
        return hits;
    }

    // -----------------------------------------------------
    // Внутри ли точка. Три луча по осям, сдвиги — на иррациональные доли
    // размера ячейки (передаётся caller'ом, т.к. сетка бывает неравномерной).
    // Решение большинством: вырожденное попадание в ребро портит максимум один
    // голос из трёх.
    // -----------------------------------------------------
    bool inside(const glm::vec3& p, const glm::vec3& cell) const {
        if (nodes_.empty()) return false;
        // Сдвиг нужен ровно для одного: вывести луч из ТОЧНОГО совпадения с
        // ребром/вершиной (там чётность пересечений не определена). Поэтому он
        // на три порядка меньше ячейки: больше — и луч уходит из тонкой
        // пластины (толщиной 1/100 габарита) в пустоту, меньше — и сдвиг
        // теряется в округлении float. Коэффициенты разные, чтобы три луча не
        // сдвигались одинаково.
        const glm::vec3 j(0.731f * 1e-3f * cell.x,
                          0.377f * 1e-3f * cell.y,
                          0.557f * 1e-3f * cell.z);

        int votes = 0;
        if ((countIntersections(glm::vec3(p.x, p.y + j.y, p.z + j.z), glm::vec3(1, 0, 0)) & 1) != 0) ++votes;
        if ((countIntersections(glm::vec3(p.x + j.z, p.y, p.z + j.x), glm::vec3(0, 1, 0)) & 1) != 0) ++votes;
        if ((countIntersections(glm::vec3(p.x + j.y, p.y + j.x, p.z), glm::vec3(0, 0, 1)) & 1) != 0) ++votes;
        return votes >= 2;
    }

private:
    static const int kLeafSize = 4;

    struct Node {
        glm::vec3 minB{0.0f}, maxB{0.0f};
        int left = -1;    // дети (если count == 0); в массиве они не обязаны идти подряд
        int right = -1;
        int start = 0;    // первый треугольник листа
        int count = 0;    // >0 — лист
    };

    static bool slab(const glm::vec3& o, const glm::vec3& invD, const Node& n,
                     float& tNear, float& tFar) {
        float t0 = -std::numeric_limits<float>::infinity();
        float t1 = std::numeric_limits<float>::infinity();
        for (int a = 0; a < 3; ++a) {
            const float oa = o[a], ia = invD[a];
            const float lo = n.minB[a], hi = n.maxB[a];
            float tn = (lo - oa) * ia;
            float tf = (hi - oa) * ia;
            if (tn > tf) std::swap(tn, tf);
            // NaN (0 * inf) — луч в плоскости грани: считаем попаданием.
            if (std::isnan(tn) || std::isnan(tf)) continue;
            if (tn > t0) t0 = tn;
            if (tf < t1) t1 = tf;
            if (t1 < t0) return false;
        }
        tNear = t0;
        tFar = t1;
        return t1 >= 0.0f;
    }

    int buildRecursive(int start, int count, int depth) {
        const int index = (int)nodes_.size();
        nodes_.push_back(Node{});

        glm::vec3 lo = tris_[start].minB, hi = tris_[start].maxB;
        glm::vec3 cmin = tris_[start].centroid, cmax = tris_[start].centroid;
        for (int i = start + 1; i < start + count; ++i) {
            lo = glm::min(lo, tris_[i].minB);
            hi = glm::max(hi, tris_[i].maxB);
            cmin = glm::min(cmin, tris_[i].centroid);
            cmax = glm::max(cmax, tris_[i].centroid);
        }

        nodes_[index].minB = lo;
        nodes_[index].maxB = hi;

        if (count <= kLeafSize || depth >= 48) {
            nodes_[index].start = start;
            nodes_[index].count = count;
            return index;
        }

        const glm::vec3 extent = cmax - cmin;
        int axis = 0;
        if (extent.y > extent.x) axis = 1;
        if (extent.z > extent[axis]) axis = 2;
        if (!(extent[axis] > 1e-12f)) {   // все центры совпали — дальше делить нечем
            nodes_[index].start = start;
            nodes_[index].count = count;
            return index;
        }

        const int mid = count / 2;
        std::nth_element(tris_.begin() + start, tris_.begin() + start + mid,
                         tris_.begin() + start + count,
                         [axis](const Triangle& a, const Triangle& b) {
                             return a.centroid[axis] < b.centroid[axis];
                         });

        // buildRecursive дописывает узлы в конец массива, поэтому индексы
        // детей запоминаем явно: они не лежат подряд.
        const int left = buildRecursive(start, mid, depth + 1);
        const int right = buildRecursive(start + mid, count - mid, depth + 1);
        nodes_[index].left = left;
        nodes_[index].right = right;
        nodes_[index].count = 0;
        return index;
    }

    std::vector<Node> nodes_;
    std::vector<Triangle> tris_;
};

} // namespace meshbvh
