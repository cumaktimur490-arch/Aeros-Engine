#include <glad/glad.h>
#include <glm/glm.hpp>

#include <cmath>
#include <vector>

#include "globals.h"
#include "cuda_api.h"
#include "flow_field.h"
#include "voxel_grid.h"
#include "particles.h"

// =====================================================
// Частицы
// =====================================================
void initParticles() {
    updateFlowParams();
    particleDrawCount = numParticles;
    if (useCUDA == 1) {
        initParticlesCUDA(particlePositions, particleColors, numParticles, flowParams);
    } else {
        particlePositions.resize(numParticles*3);
        particleColors.resize(numParticles*3);
        float z = flowParams.minZ + 0.1f * (flowParams.maxZ - flowParams.minZ);
        int nSide = (int)sqrtf((float)numParticles) + 1;
        for (int i = 0; i < numParticles; i++) {
            float r1 = fabsf(sinf(i*12.9898f + 78.233f) * 43758.5453f); r1 -= floorf(r1);
            float r2 = fabsf(cosf(i*39.346f + 11.135f) * 24634.6345f); r2 -= floorf(r2);
            int ix = i % nSide, iy = i / nSide;
            float stX = (flowParams.maxX - flowParams.minX) / nSide;
            float stY = (flowParams.maxY - flowParams.minY) / nSide;
            particlePositions[3*i]   = flowParams.minX + ix*stX + (r1-0.5f)*stX*0.5f;
            particlePositions[3*i+1] = flowParams.minY + iy*stY + (r2-0.5f)*stY*0.5f;
            particlePositions[3*i+2] = z;
            particleColors[3*i] = 0.3f;
            particleColors[3*i+1] = 0.8f;
            particleColors[3*i+2] = 1.0f;
        }
    }
    if (particleVAO == 0) glGenVertexArrays(1, &particleVAO);
    if (particleVBO_pos == 0) glGenBuffers(1, &particleVBO_pos);
    if (particleVBO_col == 0) glGenBuffers(1, &particleVBO_col);
    glBindVertexArray(particleVAO);
    glBindBuffer(GL_ARRAY_BUFFER, particleVBO_pos);
    glBufferData(GL_ARRAY_BUFFER, particlePositions.size()*sizeof(float), particlePositions.data(), GL_DYNAMIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3*sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, particleVBO_col);
    glBufferData(GL_ARRAY_BUFFER, particleColors.size()*sizeof(float), particleColors.data(), GL_DYNAMIC_DRAW);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 3*sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);
}

void updateParticles(float dt) {
    updateFlowParams();
    // Работаем только с аллоцированным количеством: слайдер Count меняет
    // numParticles, а перевыделение происходит в initParticles() (по отпусканию слайдера).
    const int n = particleDrawCount;
    if (n <= 0) return;
    if (useCUDA == 1) {
        updateParticlesCUDA(particlePositions, particleColors, n, flowParams, dt);
    } else {
        for (int i = 0; i < n; i++) {
            glm::vec3 p(particlePositions[3*i], particlePositions[3*i+1], particlePositions[3*i+2]);
            glm::vec3 v = computeVelocityFieldCPU(p, flowParams);
            glm::vec3 np = p + v * dt * flowParams.timeScale;

            bool collided = false;
            float surfDist = 1000.0f;

            if (useVoxelCollision && !g_distanceField.empty()) {
                int ix = (int)((np.x - g_voxMinX) / flowParams.cellSizeX);
                int iy = (int)((np.y - g_voxMinY) / flowParams.cellSizeY);
                int iz = (int)((np.z - g_voxMinZ) / flowParams.cellSizeZ);
                if (ix>=0 && ix<g_voxNx && iy>=0 && iy<g_voxNy && iz>=0 && iz<g_voxNz) {
                    int idx = (iz*g_voxNy + iy)*g_voxNx + ix;
                    surfDist = g_distanceField[idx];
                    if (surfDist < 0.0f) {
                        glm::vec3 nrm = sdfNormalCPU(np);
                        float push = fabsf(surfDist) + 0.5f * flowParams.cellSizeX;
                        np += nrm * push;
                        collided = true;

                        float vmag = glm::length(glm::vec3(flowParams.vx, flowParams.vy, flowParams.vz));
                        if (vmag < 1e-4f) vmag = 1e-4f;
                        glm::vec3 Vinf(flowParams.vx, flowParams.vy, flowParams.vz);

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
                    } else if (surfDist < 1.5f * flowParams.cellSizeX) {
                        collided = true;
                    }
                }
            }

            if (np.x < flowParams.minX || np.x > flowParams.maxX ||
                np.y < flowParams.minY || np.y > flowParams.maxY ||
                np.z < flowParams.minZ || np.z > flowParams.maxZ) {
                float r1 = fabsf(sinf(i*12.9898f + flowParams.time*10.0f) * 43758.5453f); r1 -= floorf(r1);
                float r2 = fabsf(cosf(i*39.346f + flowParams.time*15.0f) * 24634.6345f); r2 -= floorf(r2);
                np.x = flowParams.minX + (flowParams.maxX - flowParams.minX)*(0.05f + 0.9f*r1);
                np.y = flowParams.minY + (flowParams.maxY - flowParams.minY)*(0.05f + 0.9f*r2);
                np.z = flowParams.minZ + 0.05f;
            }

            particlePositions[3*i] = np.x;
            particlePositions[3*i+1] = np.y;
            particlePositions[3*i+2] = np.z;

            glm::vec3 c = colorForPoint(v, surfDist, flowParams);
            particleColors[3*i] = c.x;
            particleColors[3*i+1] = c.y;
            particleColors[3*i+2] = c.z;
        }
    }
    glBindBuffer(GL_ARRAY_BUFFER, particleVBO_pos);
    glBufferSubData(GL_ARRAY_BUFFER, 0, particlePositions.size()*sizeof(float), particlePositions.data());
    glBindBuffer(GL_ARRAY_BUFFER, particleVBO_col);
    glBufferSubData(GL_ARRAY_BUFFER, 0, particleColors.size()*sizeof(float), particleColors.data());
}
