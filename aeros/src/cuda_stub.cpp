// =====================================================
// CPU stub for CUDA backend — используется когда CUDA не доступна
// или для сборки без nvcc (CI, x86 fallback)
// Реализует тот же интерфейс что и kernel.cu, но на CPU
// Физика должна полностью совпадать с CPU версией из flow_field.cpp / particles.cpp / forces.cpp
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

        bool collided = false;
        float surfDist = 1000.0f;

        if (useVoxelCollision && !g_distanceField.empty()) {
            int ix = (int)((np.x - g_voxMinX) / params.cellSizeX);
            int iy = (int)((np.y - g_voxMinY) / params.cellSizeY);
            int iz = (int)((np.z - g_voxMinZ) / params.cellSizeZ);
            if (ix>=0 && ix<g_voxNx && iy>=0 && iy<g_voxNy && iz>=0 && iz<g_voxNz) {
                int idx = (iz*g_voxNy + iy)*g_voxNx + ix;
                // g_distanceField хранится в вокселях, переводим в мировые единицы
                float rawDist = g_distanceField[idx];
                surfDist = rawDist * params.cellSizeX;
                if (rawDist < 0.0f) {
                    glm::vec3 nrm = sdfNormalCPU(np);
                    float push = fabsf(surfDist) + 0.5f * params.cellSizeX;
                    np += nrm * push;
                    collided = true;

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

                    float spd = glm::length(v);
                    if (spd < 0.3f * vmag) {
                        v = vinf_t * slideBoost;
                    }
                } else if (rawDist < 1.5f) {
                    collided = true;
                }
            }
        }

        if (np.x < params.minX || np.x > params.maxX ||
            np.y < params.minY || np.y > params.maxY ||
            np.z < params.minZ || np.z > params.maxZ) {
            float r1 = fabsf(sinf(i*12.9898f + params.time*10.0f) * 43758.5453f); r1 -= floorf(r1);
            float r2 = fabsf(cosf(i*39.346f + params.time*15.0f) * 24634.6345f); r2 -= floorf(r2);
            np.x = params.minX + (params.maxX - params.minX)*(0.05f + 0.9f*r1);
            np.y = params.minY + (params.maxY - params.minY)*(0.05f + 0.9f*r2);
            np.z = params.minZ + 0.05f;
        }

        positions[3*i]   = np.x;
        positions[3*i+1] = np.y;
        positions[3*i+2] = np.z;

        glm::vec3 c = colorForPoint(v, surfDist, params);
        colors[3*i]   = c.x;
        colors[3*i+1] = c.y;
        colors[3*i+2] = c.z;
    }
}

extern "C" void computeVertexPressureCUDA(const std::vector<float>& vertices,
                                          const std::vector<float>& normals,
                                          std::vector<float>& outColors,
                                          int numVertices, const FlowParams& params) {
    (void)normals;
    outColors.resize(numVertices*3);
    float vinf = sqrtf(params.vx*params.vx + params.vy*params.vy + params.vz*params.vz);
    if (vinf < 1e-4f) vinf = 1e-4f;
    for (int i = 0; i < numVertices; i++) {
        glm::vec3 p(vertices[3*i], vertices[3*i+1], vertices[3*i+2]);
        glm::vec3 v = computeVelocityFieldCPU(p, params);
        float speed = glm::length(v);
        float speedRatio = speed / vinf;
        float cp = 1.0f - speedRatio*speedRatio;

        float rx = p.x - params.centerX;
        float ry = p.y - params.centerY;
        float rz = p.z - params.centerZ;
        float fl = 1.0f / vinf;
        float along = rx*(params.vx*fl) + ry*(params.vy*fl) + rz*(params.vz*fl);
        float D = 2.0f * fmaxf(params.radiusY, params.radiusZ);
        if (along > D * 0.3f) {
            float w = (along - D*0.3f) / D;
            if (w > 1.0f) w = 1.0f;
            cp -= 0.8f * w * w;
        }
        if (cp > 1.0f)  cp = 1.0f;
        if (cp < -3.0f) cp = -3.0f;

        float t = (cp + 1.0f) / 2.0f;
        t = glm::clamp(t, 0.0f, 1.0f);
        glm::vec3 col;
        if (t < 0.25f) { float k = t/0.25f; col = glm::vec3(0,k,1); }
        else if (t < 0.5f) { float k = (t-0.25f)/0.25f; col = glm::vec3(0,1,1-k); }
        else if (t < 0.75f) { float k = (t-0.5f)/0.25f; col = glm::vec3(k,1,0); }
        else { float k = (t-0.75f)/0.25f; col = glm::vec3(1,1-k,0); }
        outColors[3*i]   = col.x;
        outColors[3*i+1] = col.y;
        outColors[3*i+2] = col.z;
    }
}

extern "C" void setVoxelData(const int* voxel, const float* dist,
                             int nx, int ny, int nz,
                             float mnX, float mnY, float mnZ,
                             float csX, float csY, float csZ) {
    // CPU stub: данные уже в g_distanceField / g_voxelData, но для совместимости ничего не делаем
    (void)voxel; (void)dist; (void)nx; (void)ny; (void)nz;
    (void)mnX; (void)mnY; (void)mnZ; (void)csX; (void)csY; (void)csZ;
}
