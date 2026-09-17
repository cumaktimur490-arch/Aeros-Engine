#include <glad/glad.h>
#include <glm/glm.hpp>

#include <iostream>
#include <chrono>
#include <cmath>
#include <vector>

#include "globals.h"
#include "cuda_api.h"
#include "voxel_grid.h"

// =====================================================
// SDF sampling (CPU)
// =====================================================
float sampleSDFCPU(const glm::vec3& p) {
    if (g_distanceField.empty() || g_voxNx <= 0) return 1000.0f;
    int ix = (int)((p.x - g_voxMinX) / flowParams.cellSizeX);
    int iy = (int)((p.y - g_voxMinY) / flowParams.cellSizeY);
    int iz = (int)((p.z - g_voxMinZ) / flowParams.cellSizeZ);
    if (ix < 0 || ix >= g_voxNx || iy < 0 || iy >= g_voxNy || iz < 0 || iz >= g_voxNz)
        return 1000.0f;
    // Поле хранится в ВОКСЕЛЯХ (шаг BFS = 1), переводим в мировые единицы,
    // иначе зоны влияния зависят от разрешения сетки.
    return g_distanceField[(iz * g_voxNy + iy) * g_voxNx + ix] * flowParams.cellSizeX;
}

glm::vec3 sdfNormalCPU(const glm::vec3& p) {
    if (g_distanceField.empty()) return glm::vec3(0,1,0);
    int ix = (int)((p.x - g_voxMinX) / flowParams.cellSizeX);
    int iy = (int)((p.y - g_voxMinY) / flowParams.cellSizeY);
    int iz = (int)((p.z - g_voxMinZ) / flowParams.cellSizeZ);
    if (ix <= 0 || ix >= g_voxNx-1 || iy <= 0 || iy >= g_voxNy-1 || iz <= 0 || iz >= g_voxNz-1)
        return glm::vec3(0,1,0);
    float dx = g_distanceField[(iz*g_voxNy+iy)*g_voxNx + (ix+1)]
             - g_distanceField[(iz*g_voxNy+iy)*g_voxNx + (ix-1)];
    float dy = g_distanceField[(iz*g_voxNy+(iy+1))*g_voxNx + ix]
             - g_distanceField[(iz*g_voxNy+(iy-1))*g_voxNx + ix];
    float dz = g_distanceField[((iz+1)*g_voxNy+iy)*g_voxNx + ix]
             - g_distanceField[((iz-1)*g_voxNy+iy)*g_voxNx + ix];
    glm::vec3 n(dx, dy, dz);
    float len = glm::length(n);
    if (len < 1e-6f) return glm::vec3(0,1,0);
    return n / len;
}

// =====================================================
// Вокселизация
// =====================================================
static bool rayTri(const glm::vec3& orig, const glm::vec3& dir,
                   const glm::vec3& v0, const glm::vec3& v1, const glm::vec3& v2) {
    glm::vec3 e1 = v1-v0, e2 = v2-v0;
    glm::vec3 pv = glm::cross(dir, e2);
    float det = glm::dot(e1, pv);
    if (fabsf(det) < 1e-8f) return false;
    float inv = 1.0f/det;
    glm::vec3 tv = orig - v0;
    float u = glm::dot(tv, pv)*inv;
    if (u < 0.0f || u > 1.0f) return false;
    glm::vec3 qv = glm::cross(tv, e1);
    float v = glm::dot(dir, qv)*inv;
    if (v < 0.0f || u+v > 1.0f) return false;
    float t = glm::dot(e2, qv)*inv;
    return t > 1e-6f;
}

static bool insideMesh(const glm::vec3& p, const std::vector<float>& verts) {
    int hits = 0;
    glm::vec3 dir(1,0,0);
    for (size_t i = 0; i + 8 < verts.size(); i += 9) {
        glm::vec3 v0(verts[i],   verts[i+1], verts[i+2]);
        glm::vec3 v1(verts[i+3], verts[i+4], verts[i+5]);
        glm::vec3 v2(verts[i+6], verts[i+7], verts[i+8]);
        if (rayTri(p, dir, v0, v1, v2)) hits++;
    }
    return (hits % 2) == 1;
}

void buildVoxelGrid(const std::vector<float>& verts, int res) {
    std::cout << "Voxelizing at resolution " << res << "..." << std::endl;
    auto t0 = std::chrono::high_resolution_clock::now();

    float margin = 0.1f * maxDim;
    g_voxMinX = minBB.x - margin; g_voxMaxX = maxBB.x + margin;
    g_voxMinY = minBB.y - margin; g_voxMaxY = maxBB.y + margin;
    g_voxMinZ = minBB.z - margin; g_voxMaxZ = maxBB.z + margin;

    float sizeX = g_voxMaxX - g_voxMinX;
    float sizeY = g_voxMaxY - g_voxMinY;
    float sizeZ = g_voxMaxZ - g_voxMinZ;
    float m = fmaxf(sizeX, fmaxf(sizeY, sizeZ));

    g_voxNx = res;
    g_voxNy = (int)(res * sizeY / m); if (g_voxNy < 4) g_voxNy = 4;
    g_voxNz = (int)(res * sizeZ / m); if (g_voxNz < 4) g_voxNz = 4;

    float csx = sizeX / g_voxNx;
    float csy = sizeY / g_voxNy;
    float csz = sizeZ / g_voxNz;

    int total = g_voxNx * g_voxNy * g_voxNz;
    g_voxelData.assign(total, 0);

    for (int k = 0; k < g_voxNz; k++)
        for (int j = 0; j < g_voxNy; j++)
            for (int i = 0; i < g_voxNx; i++) {
                glm::vec3 p(g_voxMinX + (i+0.5f)*csx,
                            g_voxMinY + (j+0.5f)*csy,
                            g_voxMinZ + (k+0.5f)*csz);
                if (insideMesh(p, verts))
                    g_voxelData[(k*g_voxNy + j)*g_voxNx + i] = 1;
            }

    g_distanceField.assign(total, 1000.0f);
    std::vector<int> q;
    const int off[6][3] = {{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};

    for (int k = 0; k < g_voxNz; k++)
        for (int j = 0; j < g_voxNy; j++)
            for (int i = 0; i < g_voxNx; i++) {
                int idx = (k*g_voxNy + j)*g_voxNx + i;
                bool here = g_voxelData[idx] == 1;
                bool isSurf = false;
                for (auto& o : off) {
                    int ni=i+o[0], nj=j+o[1], nk=k+o[2];
                    if (ni<0||ni>=g_voxNx||nj<0||nj>=g_voxNy||nk<0||nk>=g_voxNz) continue;
                    int nidx = (nk*g_voxNy + nj)*g_voxNx + ni;
                    if ((g_voxelData[nidx]==1) != here) { isSurf = true; break; }
                }
                if (isSurf) {
                    g_distanceField[idx] = here ? -0.5f : 0.5f;
                    q.push_back(idx);
                }
            }

    size_t head = 0;
    while (head < q.size()) {
        int idx = q[head++];
        int i = idx % g_voxNx;
        int j = (idx / g_voxNx) % g_voxNy;
        int k = idx / (g_voxNx * g_voxNy);
        float d = g_distanceField[idx];
        for (auto& o : off) {
            int ni=i+o[0], nj=j+o[1], nk=k+o[2];
            if (ni<0||ni>=g_voxNx||nj<0||nj>=g_voxNy||nk<0||nk>=g_voxNz) continue;
            int nidx = (nk*g_voxNy + nj)*g_voxNx + ni;
            float nd = (d<0) ? (d-1.0f) : (d+1.0f);
            if (fabsf(nd) < fabsf(g_distanceField[nidx]) - 0.01f) {
                g_distanceField[nidx] = nd;
                q.push_back(nidx);
            }
        }
    }

    setVoxelData(g_voxelData.data(), g_distanceField.data(),
                 g_voxNx, g_voxNy, g_voxNz,
                 g_voxMinX, g_voxMinY, g_voxMinZ,
                 csx, csy, csz);

    auto t1 = std::chrono::high_resolution_clock::now();
    double ms = std::chrono::duration<double, std::milli>(t1-t0).count();
    std::cout << "Voxelized: " << g_voxNx << "x" << g_voxNy << "x" << g_voxNz
              << " in " << ms << " ms" << std::endl;
}
