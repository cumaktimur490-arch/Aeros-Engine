#include <glad/glad.h>
#include <glm/glm.hpp>

#include <iostream>
#include <chrono>
#include <cmath>
#include <vector>
#include <algorithm>
#include <limits>
#include <queue>

#include "globals.h"
#include "cuda_api.h"
#include "debug_log.h"   // v1.25.0 — журнал отладки
#include "voxel_grid.h"
#include "mesh_bvh.h"
#include "mem_budget.h"
#include "debug_log.h"   // v1.36.0 — BVH вместо перебора всех треугольников

#ifdef _OPENMP
#include <omp.h>
#endif

// =====================================================
// SDF sampling (CPU) — v1.19.0 Physics Logic Fix
// - Трилинейная интерполяция для гладкости
// - Проверка границ
// =====================================================
static inline bool isValidFloatSafe(float v) { return std::isfinite(v) && fabsf(v) < 1e6f; }
static inline bool isValidVec3Safe(const glm::vec3& v) { return isValidFloatSafe(v.x) && isValidFloatSafe(v.y) && isValidFloatSafe(v.z); }

float sampleSDFCPU(const glm::vec3& p) {
    if (g_distanceField.empty() || g_voxNx <= 0 || g_voxNy <= 0 || g_voxNz <= 0) return 1000.0f;
    if (!isValidVec3Safe(p)) return 1000.0f;
    float csx = flowParams.cellSizeX;
    float csy = flowParams.cellSizeY;
    float csz = flowParams.cellSizeZ;
    if (!isValidFloatSafe(csx) || fabsf(csx) < 1e-8f) return 1000.0f;
    if (!isValidFloatSafe(csy) || fabsf(csy) < 1e-8f) csy = csx;
    if (!isValidFloatSafe(csz) || fabsf(csz) < 1e-8f) csz = csx;

    // Трилинейная интерполяция — гладкий SDF
    float fx = (p.x - g_voxMinX) / csx - 0.5f;
    float fy = (p.y - g_voxMinY) / csy - 0.5f;
    float fz = (p.z - g_voxMinZ) / csz - 0.5f;
    int ix0 = (int)floorf(fx), iy0 = (int)floorf(fy), iz0 = (int)floorf(fz);
    int ix1 = ix0+1, iy1 = iy0+1, iz1 = iz0+1;
    float tx = fx - ix0, ty = fy - iy0, tz = fz - iz0;
    tx = glm::clamp(tx, 0.0f, 1.0f);
    ty = glm::clamp(ty, 0.0f, 1.0f);
    tz = glm::clamp(tz, 0.0f, 1.0f);

    auto get = [&](int x,int y,int z)->float {
        if (x<0||x>=g_voxNx||y<0||y>=g_voxNy||z<0||z>=g_voxNz) return 1000.0f;
        int id = (z*g_voxNy + y)*g_voxNx + x;
        if (id<0||id>=(int)g_distanceField.size()) return 1000.0f;
        float v = g_distanceField[id];
        return isValidFloatSafe(v) ? v : 1000.0f;
    };

    // Если вне — возвращаем ближайший
    if (ix0<0||iy0<0||iz0<0||ix1>=g_voxNx||iy1>=g_voxNy||iz1>=g_voxNz) {
        int ix = (int)std::floor((p.x - g_voxMinX)/csx);
        int iy = (int)std::floor((p.y - g_voxMinY)/csy);
        int iz = (int)std::floor((p.z - g_voxMinZ)/csz);
        if (ix<0||ix>=g_voxNx||iy<0||iy>=g_voxNy||iz<0||iz>=g_voxNz) return 1000.0f;
        int idx = (iz*g_voxNy+iy)*g_voxNx+ix;
        if (idx<0||idx>=(int)g_distanceField.size()) return 1000.0f;
        return g_distanceField[idx];
    }

    float c000 = get(ix0,iy0,iz0), c100 = get(ix1,iy0,iz0), c010 = get(ix0,iy1,iz0), c110 = get(ix1,iy1,iz0);
    float c001 = get(ix0,iy0,iz1), c101 = get(ix1,iy0,iz1), c011 = get(ix0,iy1,iz1), c111 = get(ix1,iy1,iz1);

    float c00 = c000*(1-tx) + c100*tx;
    float c10 = c010*(1-tx) + c110*tx;
    float c01 = c001*(1-tx) + c101*tx;
    float c11 = c011*(1-tx) + c111*tx;
    float c0 = c00*(1-ty) + c10*ty;
    float c1 = c01*(1-ty) + c11*ty;
    float c = c0*(1-tz) + c1*tz;
    return c;
}

glm::vec3 sdfNormalCPU(const glm::vec3& p) {
    if (g_distanceField.empty() || g_voxNx <= 1 || g_voxNy <= 1 || g_voxNz <= 1) return glm::vec3(0,1,0);
    if (!isValidVec3Safe(p)) return glm::vec3(0,1,0);
    float csx = flowParams.cellSizeX;
    float csy = flowParams.cellSizeY;
    float csz = flowParams.cellSizeZ;
    if (!isValidFloatSafe(csx) || fabsf(csx) < 1e-8f) return glm::vec3(0,1,0);
    if (!isValidFloatSafe(csy) || fabsf(csy) < 1e-8f) csy = csx;
    if (!isValidFloatSafe(csz) || fabsf(csz) < 1e-8f) csz = csx;

    // Центральные разности с трилинейным SDF — более точные нормали
    float eps = fminf(csx, fminf(csy, csz)) * 0.5f;
    if (eps < 1e-4f) eps = 0.01f;
    float dx = sampleSDFCPU(glm::vec3(p.x+eps, p.y, p.z)) - sampleSDFCPU(glm::vec3(p.x-eps, p.y, p.z));
    float dy = sampleSDFCPU(glm::vec3(p.x, p.y+eps, p.z)) - sampleSDFCPU(glm::vec3(p.x, p.y-eps, p.z));
    float dz = sampleSDFCPU(glm::vec3(p.x, p.y, p.z+eps)) - sampleSDFCPU(glm::vec3(p.x, p.y, p.z-eps));
    glm::vec3 n(dx, dy, dz);
    float len = glm::length(n);
    if (!isValidFloatSafe(len) || len < 1e-6f) {
        // Fallback к воксельному градиенту
        int ix = (int)std::floor((p.x - g_voxMinX)/csx);
        int iy = (int)std::floor((p.y - g_voxMinY)/csy);
        int iz = (int)std::floor((p.z - g_voxMinZ)/csz);
        if (ix<=0||ix>=g_voxNx-1||iy<=0||iy>=g_voxNy-1||iz<=0||iz>=g_voxNz-1) return glm::vec3(0,1,0);
        auto safeGet = [&](int x,int y,int z)->float {
            if (x<0||x>=g_voxNx||y<0||y>=g_voxNy||z<0||z>=g_voxNz) return 0.0f;
            int id = (z*g_voxNy+y)*g_voxNx+x;
            if (id<0||id>=(int)g_distanceField.size()) return 0.0f;
            float v = g_distanceField[id];
            return isValidFloatSafe(v)?v:0.0f;
        };
        float ddx = safeGet(ix+1,iy,iz) - safeGet(ix-1,iy,iz);
        float ddy = safeGet(ix,iy+1,iz) - safeGet(ix,iy-1,iz);
        float ddz = safeGet(ix,iy,iz+1) - safeGet(ix,iy,iz-1);
        n = glm::vec3(ddx,ddy,ddz);
        len = glm::length(n);
        if (len < 1e-6f) return glm::vec3(0,1,0);
    }
    return n / len;
}

// =====================================================
// Вокселизация — v1.19.0 Physics Logic Fix — Евклидов SDF
// - 26 соседей с евклидовыми весами, а не 6
// - Fast Sweeping с 8 проходами для точного Евклида
// - Гауссовское сглаживание вместо box blur
// - Сохранение острых углов
// =====================================================
// =====================================================
// v1.36.0 — тест «внутри/снаружи» через BVH (meshbvh) и голосование трёх лучей.
//
// Было: полный перебор всех треугольников для каждого вокселя + ОДИН луч вдоль
// +X. Первое давало часы на большой модели, второе — дырки в теле там, где луч
// проходит ровно по ребру (на 500k треугольников это не редкость, а норма).
// Стало: BVH (O(log N) на запрос) + три луча по осям со сдвигом начала на
// иррациональную долю ячейки и решение большинством.
// =====================================================

// -----------------------------------------------------
// v1.36.0 — состояние последней вокселизации (см. voxel_grid.h).
// -----------------------------------------------------
std::string g_voxelBuildError;
std::string g_voxelThinWarning;
int g_voxelSuggestedRes = 0;
long long g_voxelSolidCells = 0;
int g_voxelUsedRes = 0;
bool g_voxelDialogPending = false;
double g_voxelLastMs = 0.0;

namespace {
constexpr unsigned long long kBytesPerCell = 224;   // см. комментарий в voxel_grid.h
constexpr int kMinRes = 8;
constexpr int kMaxRes = 512;

// Объём замкнутого меша по формуле дивергенции.
double meshVolumeOf(const std::vector<float>& v) {
    double sum = 0.0;
    for (size_t i = 0; i + 8 < v.size(); i += 9) {
        const double ax = v[i],   ay = v[i+1], az = v[i+2];
        const double bx = v[i+3], by = v[i+4], bz = v[i+5];
        const double cx = v[i+6], cy = v[i+7], cz = v[i+8];
        sum += ax * (by*cz - bz*cy) - ay * (bx*cz - bz*cx) + az * (bx*cy - by*cx);
    }
    return std::fabs(sum) / 6.0;
}
} // namespace

VoxelPlan voxelPlan(const glm::vec3& size, int res, unsigned long long budgetBytes) {
    VoxelPlan p;
    p.requestedRes = res;
    int r = res;
    if (r < kMinRes) r = kMinRes;
    if (r > kMaxRes) r = kMaxRes;

    float sx = size.x, sy = size.y, sz = size.z;
    if (!(sx > 1e-6f) || !std::isfinite(sx)) sx = 1.0f;
    if (!(sy > 1e-6f) || !std::isfinite(sy)) sy = 1.0f;
    if (!(sz > 1e-6f) || !std::isfinite(sz)) sz = 1.0f;
    const float m = std::max(sx, std::max(sy, sz));

    auto dimensions = [&](int rr, int& nx, int& ny, int& nz) {
        nx = rr;
        ny = (int)std::floor(rr * sy / m); if (ny < 4) ny = 4;
        nz = (int)std::floor(rr * sz / m); if (nz < 4) nz = 4;
    };

    dimensions(r, p.nx, p.ny, p.nz);
    p.cells = (long long)p.nx * p.ny * p.nz;
    p.bytes = (unsigned long long)p.cells * kBytesPerCell;
    p.fits = (budgetBytes == 0) || (p.bytes <= budgetBytes);

    // Подбираем разрешение, при котором план влезает: кубический корень из
    // отношения бюджет/нужно, с запасом 0.95 и повторной проверкой (ny/nz
    // округляются вниз, поэтому оценка не точная).
    if (!p.fits && budgetBytes > 0) {
        double scale = std::cbrt((double)budgetBytes / (double)p.bytes) * 0.95;
        int candidate = (int)(r * scale);
        for (int guard = 0; guard < 64 && candidate >= kMinRes; ++guard, --candidate) {
            int nx, ny, nz;
            dimensions(candidate, nx, ny, nz);
            const unsigned long long bytes =
                (unsigned long long)nx * ny * nz * kBytesPerCell;
            if (bytes <= budgetBytes) {
                p.suggestedRes = candidate;
                break;
            }
        }
        if (p.suggestedRes < kMinRes) p.suggestedRes = 0;
    }
    return p;
}

double voxelizeEstimateMs(int triangles, long long cells) {
    if (triangles < 1) triangles = 1;
    if (cells < 1) cells = 1;
    const double l = std::log2((double)std::max(2, triangles));
    const double ms = 6.7 + 7.138e-5 * (double)cells * l + 2.017e-5 * (double)triangles * l;
    return std::max(1.0, ms);
}

void buildVoxelGrid(const std::vector<float>& verts, int res) {
    if (verts.empty()) {
        std::cerr << "[Voxel] Empty vertices" << std::endl;
        return;
    }
    if (res < 8) res = 8;
    if (res > 256) res = 256;

    g_voxelBuildError.clear();
    g_voxelThinWarning.clear();
    g_voxelSuggestedRes = 0;
    g_voxelSolidCells = 0;
    g_voxelUsedRes = 0;
    g_voxelDialogPending = false;
    std::cout << "Voxelizing at resolution " << res << " (BVH)" << std::endl;
    auto t0 = std::chrono::high_resolution_clock::now();

    if (!std::isfinite(minBB.x) || !std::isfinite(maxBB.x) || glm::length(maxBB-minBB) < 1e-6f) {
        std::cerr << "[Voxel] Invalid BB" << std::endl;
        return;
    }

    float margin = 0.14f * maxDim;
    if (!std::isfinite(margin) || margin < 0.001f) margin = 0.1f;
    g_voxMinX = minBB.x - margin; g_voxMaxX = maxBB.x + margin;
    g_voxMinY = minBB.y - margin; g_voxMaxY = maxBB.y + margin;
    g_voxMinZ = minBB.z - margin; g_voxMaxZ = maxBB.z + margin;

    float sizeX = g_voxMaxX - g_voxMinX;
    float sizeY = g_voxMaxY - g_voxMinY;
    float sizeZ = g_voxMaxZ - g_voxMinZ;
    if (sizeX < 1e-6f) sizeX = 1.0f;
    if (sizeY < 1e-6f) sizeY = 1.0f;
    if (sizeZ < 1e-6f) sizeZ = 1.0f;

    // v1.36.0 — план сетки и ЯВНЫЙ отказ вместо молчаливого уменьшения.
    const glm::vec3 boxSize(sizeX, sizeY, sizeZ);
    const unsigned long long budget = membudget::budgetBytes();
    const VoxelPlan plan = voxelPlan(boxSize, res, budget);
    if (!plan.fits) {
        g_voxelSuggestedRes = plan.suggestedRes;
        char msg[512];
        std::snprintf(msg, sizeof(msg),
                      "нужно %s памяти на сетку %dx%dx%d (%lld ячеек), доступно %s",
                      membudget::formatBytes(plan.bytes).c_str(),
                      plan.nx, plan.ny, plan.nz, plan.cells,
                      membudget::formatBytes(budget).c_str());
        g_voxelBuildError = msg;
        std::cerr << "[Voxel] " << g_voxelBuildError << " — сетка не построена" << std::endl;
        if (plan.suggestedRes > 0) {
            std::cerr << "[Voxel] предложение: уменьшить разрешение до "
                      << plan.suggestedRes << std::endl;
            g_voxelBuildError += "; уменьшите разрешение до " +
                                 std::to_string(plan.suggestedRes);
        }
        DBG_ERROR("вокселизация отменена: %s (бюджет памяти %s)", g_voxelBuildError.c_str(),
                  membudget::formatBytes(budget).c_str());
        g_voxelDialogPending = true;   // интерфейс покажет выбор: уменьшить или отменить
        return;
    }
    g_voxNx = plan.nx;
    g_voxNy = plan.ny;
    g_voxNz = plan.nz;


    float csx = sizeX / g_voxNx;
    float csy = sizeY / g_voxNy;
    float csz = sizeZ / g_voxNz;
    if (csx < 1e-6f) csx = 0.1f;
    if (csy < 1e-6f) csy = 0.1f;
    if (csz < 1e-6f) csz = 0.1f;

    int total = g_voxNx * g_voxNy * g_voxNz;
    if (total <= 0 || total > 20*1024*1024) {
        g_voxelBuildError = "недопустимое число ячеек: " + std::to_string(total);
        std::cerr << "[Voxel] " << g_voxelBuildError << std::endl;
        DBG_ERROR("вокселизация: %s", g_voxelBuildError.c_str());
        return;
    }
    g_voxelUsedRes = g_voxNx;

    try {
        g_voxelData.assign(total, 0);
    } catch (const std::bad_alloc& e) {
        std::cerr << "[Voxel] Alloc failed: " << e.what() << std::endl;
        return;
    }

    std::vector<meshbvh::Triangle> tris;
    tris.reserve(verts.size()/9 + 1);
    for (size_t i = 0; i + 8 < verts.size(); i += 9) {
        const glm::vec3 a(verts[i],   verts[i+1], verts[i+2]);
        const glm::vec3 b(verts[i+3], verts[i+4], verts[i+5]);
        const glm::vec3 c(verts[i+6], verts[i+7], verts[i+8]);
        if (!std::isfinite(a.x) || !std::isfinite(a.y) || !std::isfinite(a.z) ||
            !std::isfinite(b.x) || !std::isfinite(b.y) || !std::isfinite(b.z) ||
            !std::isfinite(c.x) || !std::isfinite(c.y) || !std::isfinite(c.z)) continue;
        meshbvh::Triangle t = meshbvh::makeTriangle(a, b, c);
        if (meshbvh::isDegenerate(t)) continue;
        tris.push_back(t);
    }

    auto tBvh0 = std::chrono::high_resolution_clock::now();
    meshbvh::BVH bvh;
    if (!tris.empty()) bvh.build(tris);
    auto tBvh1 = std::chrono::high_resolution_clock::now();
    const double bvhMs = std::chrono::duration<double, std::milli>(tBvh1 - tBvh0).count();

    const glm::vec3 cell(csx, csy, csz);
    if (tris.empty()) {
        std::cerr << "[Voxel] No valid triangles for voxelization" << std::endl;
    } else {
        #ifdef _OPENMP
        #pragma omp parallel for collapse(3) schedule(dynamic, 4)
        #endif
        for (int k = 0; k < g_voxNz; k++) {
            for (int j = 0; j < g_voxNy; j++) {
                for (int i = 0; i < g_voxNx; i++) {
                    const glm::vec3 p(g_voxMinX + (i+0.5f)*csx,
                                      g_voxMinY + (j+0.5f)*csy,
                                      g_voxMinZ + (k+0.5f)*csz);
                    if (bvh.inside(p, cell))
                        g_voxelData[(k*g_voxNy + j)*g_voxNx + i] = 1;
                }
            }
        }
    }

    // v1.36.0 — предупреждение о тонких элементах. Сравниваем объём
    // многогранника (по формуле дивергенции) с объёмом воксельного тела:
    // если в сетке оказалось меньше 60 % объёма модели, элементы тоньше
    // ячейки не попали в расчёт. Раньше это выглядело как «Cd = 0» без причины.
    {
        long long solid = 0;
        for (int v : g_voxelData) if (v) ++solid;
        g_voxelSolidCells = solid;
        const double meshVol = meshVolumeOf(verts);
        const double voxVol = (double)solid * (double)csx * (double)csy * (double)csz;
        if (meshVol > 1e-12 && solid > 0 && voxVol < 0.6 * meshVol) {
            char msg[512];
            std::snprintf(msg, sizeof(msg),
                          "объём тела в сетке %.0f %% от исходного (%.2f из %.2f м³) — "
                          "элементы тоньше ячейки %.4f м не попали в расчёт; увеличьте разрешение",
                          100.0 * voxVol / meshVol, voxVol, meshVol,
                          std::fmin(csx, std::fmin(csy, csz)));
            g_voxelThinWarning = msg;
            std::cerr << "[Voxel] " << msg << std::endl;
            DBG_WARN("вокселизация: %s", msg);
        } else if (meshVol > 1e-12 && solid == 0) {
            char msg[512];
            std::snprintf(msg, sizeof(msg),
                          "тело не попало в сетку: все элементы тоньше ячейки %.4f м "
                          "(модель %.2f м³) — увеличьте разрешение",
                          std::fmin(csx, std::fmin(csy, csz)), meshVol);
            g_voxelThinWarning = msg;
            std::cerr << "[Voxel] " << msg << std::endl;
            DBG_WARN("вокселизация: %s", msg);
        }
    }

    try {
        g_distanceField.assign(total, 1e6f);
    } catch (...) {
        std::cerr << "[Voxel] Distance field alloc failed" << std::endl;
        return;
    }

    // v1.19.0: Евклидов SDF — Fast Sweeping с 26 соседями и евклидовыми весами
    // Инициализация поверхности
    std::vector<int> q;
    q.reserve(total/3 + 1);
    const int off6[6][3] = {{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};

    for (int k = 0; k < g_voxNz; k++)
        for (int j = 0; j < g_voxNy; j++)
            for (int i = 0; i < g_voxNx; i++) {
                int idx = (k*g_voxNy + j)*g_voxNx + i;
                bool here = g_voxelData[idx] == 1;
                bool isSurf = false;
                for (auto& o : off6) {
                    int ni=i+o[0], nj=j+o[1], nk=k+o[2];
                    if (ni<0||ni>=g_voxNx||nj<0||nj>=g_voxNy||nk<0||nk>=g_voxNz) continue;
                    int nidx = (nk*g_voxNy + nj)*g_voxNx + ni;
                    if (nidx <0 || nidx >= total) continue;
                    if ((g_voxelData[nidx]==1) != here) { isSurf = true; break; }
                }
                if (isSurf) {
                    g_distanceField[idx] = here ? -0.5f * fminf(csx, fminf(csy,csz)) : 0.5f * fminf(csx, fminf(csy,csz));
                    q.push_back(idx);
                }
            }

    // 26 соседей с евклидовыми весами
    struct Neighbor { int dx,dy,dz; float w; };
    std::vector<Neighbor> neigh;
    for (int dz=-1; dz<=1; ++dz)
        for (int dy=-1; dy<=1; ++dy)
            for (int dx=-1; dx<=1; ++dx) {
                if (dx==0&&dy==0&&dz==0) continue;
                float wx = dx*csx, wy = dy*csy, wz = dz*csz;
                float w = sqrtf(wx*wx + wy*wy + wz*wz);
                neigh.push_back({dx,dy,dz,w});
            }

    // BFS с приоритетом по расстоянию — Dijkstra-like для точного Евклида
    // Используем очередь, но с сортировкой по |d| для лучшего приближения
    size_t head = 0;
    // Для скорости — 2 прохода: сначала прямой, потом обратный (Fast Sweeping 8 направлений)
    // Упростим: 8 sweep проходов
    for (int sweep=0; sweep<4; ++sweep) {
        // 8 направлений sweeping
        int iStart,iEnd,iStep,jStart,jEnd,jStep,kStart,kEnd,kStep;
        if (sweep==0) { iStart=0; iEnd=g_voxNx; iStep=1; jStart=0; jEnd=g_voxNy; jStep=1; kStart=0; kEnd=g_voxNz; kStep=1; }
        else if (sweep==1) { iStart=g_voxNx-1; iEnd=-1; iStep=-1; jStart=0; jEnd=g_voxNy; jStep=1; kStart=0; kEnd=g_voxNz; kStep=1; }
        else if (sweep==2) { iStart=0; iEnd=g_voxNx; iStep=1; jStart=g_voxNy-1; jEnd=-1; jStep=-1; kStart=0; kEnd=g_voxNz; kStep=1; }
        else { iStart=g_voxNx-1; iEnd=-1; iStep=-1; jStart=g_voxNy-1; jEnd=-1; jStep=-1; kStart=g_voxNz-1; kEnd=-1; kStep=-1; }

        for (int k=kStart; k!=kEnd; k+=kStep) {
            for (int j=jStart; j!=jEnd; j+=jStep) {
                for (int i=iStart; i!=iEnd; i+=iStep) {
                    int idx = (k*g_voxNy + j)*g_voxNx + i;
                    float curD = g_distanceField[idx];
                    if (fabsf(curD) > 1e5f) continue;
                    for (auto& nb : neigh) {
                        int ni=i+nb.dx, nj=j+nb.dy, nk=k+nb.dz;
                        if (ni<0||ni>=g_voxNx||nj<0||nj>=g_voxNy||nk<0||nk>=g_voxNz) continue;
                        int nidx = (nk*g_voxNy + nj)*g_voxNx + ni;
                        if (nidx<0||nidx>=total) continue;
                        float nd = (curD < 0) ? (curD - nb.w) : (curD + nb.w);
                        if (fabsf(nd) < fabsf(g_distanceField[nidx]) - 1e-4f) {
                            g_distanceField[nidx] = nd;
                        }
                    }
                }
            }
        }
    }

    // Дополнительный BFS для гарантии
    head = 0;
    while (head < q.size()) {
        int idx = q[head++];
        int i = idx % g_voxNx;
        int j = (idx / g_voxNx) % g_voxNy;
        int k = idx / (g_voxNx * g_voxNy);
        float d = g_distanceField[idx];
        if (!isValidFloatSafe(d)) continue;
        for (auto& nb : neigh) {
            int ni=i+nb.dx, nj=j+nb.dy, nk=k+nb.dz;
            if (ni<0||ni>=g_voxNx||nj<0||nj>=g_voxNy||nk<0||nk>=g_voxNz) continue;
            int nidx = (nk*g_voxNy + nj)*g_voxNx + ni;
            if (nidx <0 || nidx >= total) continue;
            float nd = (d < 0) ? (d - nb.w) : (d + nb.w);
            if (fabsf(nd) < fabsf(g_distanceField[nidx]) - 0.001f) {
                g_distanceField[nidx] = nd;
                q.push_back(nidx);
            }
        }
    }

    // v1.34.0 — сглаживание больше не трогает окрестность поверхности.
    //
    // Раньше пропускались только ячейки с |d| < 1.2*ячейки. Но сглаживание
    // усредняет ЗНАЧЕНИЯ расстояния, а не положение нулевого уровня: у
    // линейного поля d = x усреднение с ядром (0.25, 0.5, 0.25) сохраняет
    // значение, а у изогнутого (сфера, кромка) — занижает |d| на 1-3%. Возле
    // тела это именно те ячейки, по которым считается выталкивание частиц,
    // нормаль (центральная разность) и подсветка отрыва, поэтому их трогать
    // нельзя. Дальше 3 ячеек сглаживание только убирает артефакты свипов и
    // ни на что физическое не влияет.
    {
        std::vector<float> temp = g_distanceField;
        // 2 итерации гауссовского с sigma=1.0
        const float kernel[3] = {0.25f, 0.5f, 0.25f}; // 1D Gaussian
        for (int iter=0; iter<2; ++iter) {
            // X pass
            #ifdef _OPENMP
            #pragma omp parallel for collapse(3)
            #endif
            for (int k=1; k<g_voxNz-1; ++k) {
                for (int j=1; j<g_voxNy-1; ++j) {
                    for (int i=1; i<g_voxNx-1; ++i) {
                        int idx = (k*g_voxNy + j)*g_voxNx + i;
                        if (fabsf(temp[idx]) < csx*3.0f) continue; // не трогаем окрестность поверхности
                        float sum = temp[idx-1]*kernel[0] + temp[idx]*kernel[1] + temp[idx+1]*kernel[2];
                        g_distanceField[idx] = sum*0.35f + temp[idx]*0.65f;
                    }
                }
            }
            temp = g_distanceField;
            // Y pass
            #ifdef _OPENMP
            #pragma omp parallel for collapse(3)
            #endif
            for (int k=1; k<g_voxNz-1; ++k) {
                for (int j=1; j<g_voxNy-1; ++j) {
                    for (int i=1; i<g_voxNx-1; ++i) {
                        int idx = (k*g_voxNy + j)*g_voxNx + i;
                        if (fabsf(temp[idx]) < csy*3.0f) continue;
                        int idxYm = (k*g_voxNy + (j-1))*g_voxNx + i;
                        int idxYp = (k*g_voxNy + (j+1))*g_voxNx + i;
                        float sum = temp[idxYm]*kernel[0] + temp[idx]*kernel[1] + temp[idxYp]*kernel[2];
                        g_distanceField[idx] = sum*0.35f + temp[idx]*0.65f;
                    }
                }
            }
            temp = g_distanceField;
            // Z pass
            #ifdef _OPENMP
            #pragma omp parallel for collapse(3)
            #endif
            for (int k=1; k<g_voxNz-1; ++k) {
                for (int j=1; j<g_voxNy-1; ++j) {
                    for (int i=1; i<g_voxNx-1; ++i) {
                        int idx = (k*g_voxNy + j)*g_voxNx + i;
                        if (fabsf(temp[idx]) < csz*3.0f) continue; // не трогаем окрестность поверхности
                        int idxZm = ((k-1)*g_voxNy + j)*g_voxNx + i;
                        int idxZp = ((k+1)*g_voxNy + j)*g_voxNx + i;
                        float sum = temp[idxZm]*kernel[0] + temp[idx]*kernel[1] + temp[idxZp]*kernel[2];
                        g_distanceField[idx] = sum*0.35f + temp[idx]*0.65f;
                    }
                }
            }
            temp = g_distanceField;
        }
    }

    setVoxelData(g_voxelData.data(), g_distanceField.data(),
                 g_voxNx, g_voxNy, g_voxNz,
                 g_voxMinX, g_voxMinY, g_voxMinZ,
                 csx, csy, csz);

    // v1.25.0 — если передача SDF на GPU не удалась, физика на CUDA считает
    // по пустому полю. Пишем причину и уходим на CPU.
    {
        char cudaErr[900]; cudaErr[0] = '\0';
        int code = cudaConsumeError(cudaErr, sizeof(cudaErr));
        if (code != 0) {
            g_debugCudaFallbacks++;
            g_cudaStatusText = cudaErr;
            DBG_ERROR("передача воксельных данных на GPU не удалась (код %d): %s", code, cudaErr);
            if (useCUDA) {
                useCUDA = 0;
                DBG_WARN("расчёт переключён на CPU-бэкенд");
            }
        }
    }

    auto t1 = std::chrono::high_resolution_clock::now();
    double ms = std::chrono::duration<double, std::milli>(t1-t0).count();
    std::cout << "Voxelized: " << g_voxNx << "x" << g_voxNy << "x" << g_voxNz
              << " (" << total << " cells, " << tris.size() << " tris, BVH "
              << bvh.nodeCount() << " nodes) in " << ms << " ms"
#ifdef _OPENMP
              << " [OpenMP]"
#endif
              << " [BVH, 3-ray vote]" << std::endl;
    // v1.36.0 — BVH отдельно от вокселизации: по этим двум числам видно, что
    // именно ускорилось (дерево строится один раз на модель).
    std::cout << "[Voxel] BVH built in " << bvhMs << " ms" << std::endl;
    g_voxelLastMs = ms;
    DBG_PHYS("вокселизация: сетка %dx%dx%d, ячеек %d, треугольников %zu, BVH %zu узлов "
             "(%.1f мс), всего %.1f мс",
             g_voxNx, g_voxNy, g_voxNz, total, tris.size(), bvh.nodeCount(), bvhMs, ms);
}
