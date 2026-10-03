// =====================================================
// CPU stub for CUDA backend — используется когда CUDA не доступна
// или для сборки без nvcc (CI, x86 fallback)
// Реализует тот же интерфейс что и kernel.cu, но на CPU
// =====================================================

#include "cuda_api.h"
#include "flow_field.h"
#include "voxel_grid.h"
#include "globals.h"
#include <glm/glm.hpp>
#include <cmath>
#include <vector>

extern "C" void initParticlesCUDA(std::vector<float>& positions, std::vector<float>& colors,
                                  int numParticles, const FlowParams& params) {
    positions.resize(numParticles*3);
    colors.resize(numParticles*3);
    float z = params.minZ + 0.1f * (params.maxZ - params.minZ);
    int nSide = (int)sqrtf((float)numParticles) + 1;
    for (int i = 0; i < numParticles; i++) {
        float r1 = fabsf(sinf(i*12.9898f + 78.233f) * 43758.5453f); r1 -= floorf(r1);
        float r2 = fabsf(cosf(i*39.346f + 11.135f) * 24634.6345f); r2 -= floorf(r2);
        int ix = i % nSide, iy = i / nSide;
        float stX = (params.maxX - params.minX) / nSide;
        float stY = (params.maxY - params.minY) / nSide;
        positions[3*i]   = params.minX + ix*stX + (r1-0.5f)*stX*0.5f;
        positions[3*i+1] = params.minY + iy*stY + (r2-0.5f)*stY*0.5f;
        positions[3*i+2] = z;
        colors[3*i] = 0.3f;
        colors[3*i+1] = 0.8f;
        colors[3*i+2] = 1.0f;
    }
}

extern "C" void updateParticlesCUDA(std::vector<float>& positions, std::vector<float>& colors,
                                    int numParticles, const FlowParams& params, float dt) {
    for (int i = 0; i < numParticles; i++) {
        glm::vec3 p(positions[3*i], positions[3*i+1], positions[3*i+2]);
        glm::vec3 v = computeVelocityFieldCPU(p, params);
        glm::vec3 np = p + v * dt * params.timeScale;

        if (useVoxelCollision && !g_distanceField.empty()) {
            int ix = (int)((np.x - g_voxMinX) / params.cellSizeX);
            int iy = (int)((np.y - g_voxMinY) / params.cellSizeY);
            int iz = (int)((np.z - g_voxMinZ) / params.cellSizeZ);
            if (ix>=0 && ix<g_voxNx && iy>=0 && iy<g_voxNy && iz>=0 && iz<g_voxNz) {
                int idx = (iz*g_voxNy + iy)*g_voxNx + ix;
                float surfDist = g_distanceField[idx];
                if (surfDist < 0.0f) {
                    glm::vec3 nrm = sdfNormalCPU(np);
                    float push = fabsf(surfDist) + 0.5f * params.cellSizeX;
                    np += nrm * push;
                    float vmag = glm::length(glm::vec3(params.vx, params.vy, params.vz));
                    if (vmag < 1e-4f) vmag = 1e-4f;
                    glm::vec3 Vinf(params.vx, params.vy, params.vz);
                    float vinf_n = glm::dot(Vinf, nrm);
                    glm::vec3 vinf_t = Vinf - vinf_n * nrm;
                    float vn = glm::dot(v, nrm);
                    v -= vn * nrm;
                    float perpFactor = fabsf(vinf_n) / vmag;
                    float slideBoost = 1.2f + 0.8f * perpFactor;
                    v += vinf_t * slideBoost * 0.6f;
                }
            }
        }

        positions[3*i]   = np.x;
        positions[3*i+1] = np.y;
        positions[3*i+2] = np.z;

        float speed = glm::length(v);
        float t = speed / 5.0f;
        if (t > 1.0f) t = 1.0f;
        colors[3*i]   = t;
        colors[3*i+1] = 1.0f - t*0.5f;
        colors[3*i+2] = 1.0f;
    }
}

extern "C" void computeVertexPressureCUDA(const std::vector<float>& vertices,
                                          const std::vector<float>& normals,
                                          std::vector<float>& outColors,
                                          int numVertices, const FlowParams& params) {
    (void)normals;
    outColors.resize(numVertices*3);
    for (int i = 0; i < numVertices; i++) {
        glm::vec3 p(vertices[3*i], vertices[3*i+1], vertices[3*i+2]);
        glm::vec3 v = computeVelocityFieldCPU(p, params);
        float speed = glm::length(v);
        float denom = params.vx*params.vx + params.vy*params.vy + params.vz*params.vz + 1e-6f;
        float cp = 1.0f - (speed*speed) / denom;
        if (cp > 1.0f) cp = 1.0f;
        if (cp < -3.0f) cp = -3.0f;
        float t = (cp + 3.0f) / 4.0f;
        outColors[3*i]   = t;
        outColors[3*i+1] = 1.0f - fabsf(t-0.5f)*2.0f;
        outColors[3*i+2] = 1.0f - t;
    }
}

extern "C" void setVoxelData(const int* voxel, const float* dist,
                             int nx, int ny, int nz,
                             float mnX, float mnY, float mnZ,
                             float csX, float csY, float csZ) {
    (void)voxel; (void)dist; (void)nx; (void)ny; (void)nz;
    (void)mnX; (void)mnY; (void)mnZ; (void)csX; (void)csY; (void)csZ;
}
