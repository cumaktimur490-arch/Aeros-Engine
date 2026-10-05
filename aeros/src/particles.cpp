#include <glad/glad.h>
#include <glm/glm.hpp>

#include <cmath>
#include <vector>

#include "globals.h"
#include "cuda_api.h"
#include "flow_field.h"
#include "voxel_grid.h"
#include "particles.h"
#include "lbm.h"

#ifdef _OPENMP
#include <omp.h>
#endif

// =====================================================
// Частицы — v1.7.0 Realistic Aero как на фото
// =====================================================
void initParticles() {
    updateFlowParams();
    if (numParticles <= 0) numParticles = 100;
    if (numParticles > 500000) numParticles = 500000;
    if (!std::isfinite(flowParams.minX) || !std::isfinite(flowParams.maxX) || flowParams.minX >= flowParams.maxX) { flowParams.minX = -5.0f; flowParams.maxX = 5.0f; }
    if (!std::isfinite(flowParams.minY) || !std::isfinite(flowParams.maxY) || flowParams.minY >= flowParams.maxY) { flowParams.minY = -5.0f; flowParams.maxY = 5.0f; }
    if (!std::isfinite(flowParams.minZ) || !std::isfinite(flowParams.maxZ) || flowParams.minZ >= flowParams.maxZ) { flowParams.minZ = -5.0f; flowParams.maxZ = 5.0f; }

    particleDrawCount = numParticles;
    if (useCUDA == 1) {
        initParticlesCUDA(particlePositions, particleColors, numParticles, flowParams);
    } else {
        try {
            particlePositions.resize(numParticles*3);
            particleColors.resize(numParticles*3);
        } catch (const std::bad_alloc&) {
            numParticles = 1000;
            particleDrawCount = numParticles;
            particlePositions.resize(numParticles*3);
            particleColors.resize(numParticles*3);
        }
        float z = flowParams.minZ + 0.1f * (flowParams.maxZ - flowParams.minZ);
        if (!std::isfinite(z)) z = flowParams.minZ;
        int nSide = (int)sqrtf((float)numParticles) + 1;
        if (nSide <= 0) nSide = 1;
        float rangeX = flowParams.maxX - flowParams.minX;
        float rangeY = flowParams.maxY - flowParams.minY;
        if (!std::isfinite(rangeX) || rangeX < 1e-6f) rangeX = 10.0f;
        if (!std::isfinite(rangeY) || rangeY < 1e-6f) rangeY = 10.0f;
        float stX = rangeX / nSide;
        float stY = rangeY / nSide;
        float minX = flowParams.minX;
        float minY = flowParams.minY;

        #ifdef _OPENMP
        #pragma omp parallel for
        #endif
        for (int i = 0; i < numParticles; i++) {
            float r1 = fabsf(sinf(i*12.9898f + 78.233f) * 43758.5453f); r1 -= floorf(r1);
            float r2 = fabsf(cosf(i*39.346f + 11.135f) * 24634.6345f); r2 -= floorf(r2);
            if (!std::isfinite(r1)) r1 = 0.5f;
            if (!std::isfinite(r2)) r2 = 0.5f;
            int ix = i % nSide, iy = i / nSide;
            particlePositions[3*i]   = minX + ix*stX + (r1-0.5f)*stX*0.5f;
            particlePositions[3*i+1] = minY + iy*stY + (r2-0.5f)*stY*0.5f;
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
    if (!std::isfinite(dt) || dt <= 0.0f || dt > 1.0f) dt = 0.016f;
    const int n = particleDrawCount;
    if (n <= 0) return;
    if (n > (int)(particlePositions.size()/3)) return;
    if (particlePositions.size() != particleColors.size()) return;
    if (useCUDA == 1) {
        try { updateParticlesCUDA(particlePositions, particleColors, n, flowParams, dt); }
        catch (...) {}
    } else {
        float csx = flowParams.cellSizeX, csy = flowParams.cellSizeY, csz = flowParams.cellSizeZ;
        if (!std::isfinite(csx) || csx < 1e-8f) csx = 0.1f;
        if (!std::isfinite(csy) || csy < 1e-8f) csy = 0.1f;
        if (!std::isfinite(csz) || csz < 1e-8f) csz = 0.1f;
        float minX = flowParams.minX, maxX = flowParams.maxX;
        float minY = flowParams.minY, maxY = flowParams.maxY;
        float minZ = flowParams.minZ, maxZ = flowParams.maxZ;
        float time = flowParams.time;
        float timeScale = flowParams.timeScale;
        if (!std::isfinite(timeScale) || timeScale < 0) timeScale = 1.0f;
        float vxInf = flowParams.vx, vyInf = flowParams.vy, vzInf = flowParams.vz;
        float vInfMag = sqrtf(vxInf*vxInf + vyInf*vyInf + vzInf*vzInf);
        if (!std::isfinite(vInfMag) || vInfMag < 1e-4f) vInfMag = 1.0f;
        float maxSpeed = maxSpeedForColor;
        if (maxSpeed < 1e-3f) maxSpeed = vInfMag * 1.5f;

        float* posPtr = particlePositions.data();
        float* colPtr = particleColors.data();
        const float* distField = g_distanceField.empty() ? nullptr : g_distanceField.data();
        int voxNx = g_voxNx, voxNy = g_voxNy, voxNz = g_voxNz;
        float voxMinX = g_voxMinX, voxMinY = g_voxMinY, voxMinZ = g_voxMinZ;
        bool useColl = useVoxelCollision && distField && voxNx > 0;
        int distSize = (int)g_distanceField.size();

        #ifdef _OPENMP
        #pragma omp parallel for
        #endif
        for (int i = 0; i < n; i++) {
            float px = posPtr[3*i];
            float py = posPtr[3*i+1];
            float pz = posPtr[3*i+2];

            if (!std::isfinite(px)) {
                float r1 = fabsf(sinf(i*12.9898f + time*10.0f) * 43758.5453f); r1 -= floorf(r1);
                float r2 = fabsf(cosf(i*39.346f + time*15.0f) * 24634.6345f); r2 -= floorf(r2);
                px = minX + (maxX - minX)*(0.05f + 0.9f*r1);
                py = minY + (maxY - minY)*(0.05f + 0.9f*r2);
                pz = minZ + 0.05f;
            }

            glm::vec3 p(px, py, pz);
            glm::vec3 v = computeVelocityFieldCPU(p, flowParams);
            if (!std::isfinite(v.x)) v = glm::vec3(vxInf, vyInf, vzInf);

            glm::vec3 np = p + v * dt * timeScale;
            float surfDist = 1000.0f;

            if (useColl) {
                int ix = (int)((np.x - voxMinX) / csx);
                int iy = (int)((np.y - voxMinY) / csy);
                int iz = (int)((np.z - voxMinZ) / csz);
                if (ix>=0 && ix<voxNx && iy>=0 && iy<voxNy && iz>=0 && iz<voxNz) {
                    int idx = (iz*voxNy + iy)*voxNx + ix;
                    if (idx >=0 && idx < distSize) {
                        float rawDist = distField[idx];
                        if (!std::isfinite(rawDist)) rawDist = 1000.0f;
                        surfDist = rawDist * csx;
                        if (rawDist < 0.0f) {
                            glm::vec3 nrm = sdfNormalCPU(np);
                            if (!std::isfinite(nrm.x)) nrm = glm::vec3(0,1,0);
                            float push = fabsf(surfDist) + 0.5f * csx;
                            np += nrm * push;
                            float vmag = vInfMag;
                            glm::vec3 Vinf(vxInf, vyInf, vzInf);
                            float vinf_n = glm::dot(Vinf, nrm);
                            glm::vec3 vinf_t = Vinf - vinf_n * nrm;
                            float vn = glm::dot(v, nrm);
                            v -= vn * nrm;
                            float perpFactor = fabsf(vinf_n) / vmag;
                            float slideBoost = 1.2f + 0.8f * perpFactor;
                            v += vinf_t * slideBoost * 0.6f;
                            float spd = glm::length(v);
                            if (spd < 0.3f * vmag) v = vinf_t * slideBoost;
                        }
                    }
                }
            }

            // Ground effect — отталкивание от земли
            if (aeroGroundEffect) {
                float groundY = g_voxMinY + aeroGroundHeight;
                if (np.y < groundY + csy) {
                    np.y = groundY + csy;
                    v.y = fabsf(v.y) * 0.5f;
                }
            }

            if (!std::isfinite(np.x) || np.x < minX || np.x > maxX ||
                np.y < minY || np.y > maxY || np.z < minZ || np.z > maxZ) {
                float r1 = fabsf(sinf(i*12.9898f + time*10.0f) * 43758.5453f); r1 -= floorf(r1);
                float r2 = fabsf(cosf(i*39.346f + time*15.0f) * 24634.6345f); r2 -= floorf(r2);
                if (!std::isfinite(r1)) r1 = 0.5f;
                if (!std::isfinite(r2)) r2 = 0.5f;
                float rangeX = maxX - minX, rangeY = maxY - minY;
                if (!std::isfinite(rangeX) || rangeX < 1e-6f) rangeX = 10.0f;
                if (!std::isfinite(rangeY) || rangeY < 1e-6f) rangeY = 10.0f;
                np.x = minX + rangeX*(0.05f + 0.9f*r1);
                np.y = minY + rangeY*(0.05f + 0.9f*r2);
                np.z = minZ + 0.05f;
            }

            if (!std::isfinite(np.x)) continue;
            posPtr[3*i] = np.x;
            posPtr[3*i+1] = np.y;
            posPtr[3*i+2] = np.z;

            // Реалистичная окраска как на фото
            glm::vec3 c;
            if (lbmParams.enabled && lbmInitialized) {
                if (aeroVisMode == AeroVisMode::VelocityMagnitude || aeroColorStreamlinesByVelocity) {
                    float velMag = glm::length(v);
                    // Ускорение под днищем авто как на фото 2
                    if (aeroGroundEffect && np.y < center.y) velMag *= 1.15f;
                    c = getVelocityMagnitudeColor(velMag, maxSpeed);
                } else if (aeroVisMode == AeroVisMode::Vorticity) {
                    float vort = getLBMVorticityWorld(np);
                    c = getVorticityColor(vort);
                } else if (aeroVisMode == AeroVisMode::QCriterion) {
                    float q = getLBMQWorld(np);
                    if (q > 0) c = glm::vec3(1, 0.3f, 0); // вихрь — красный/оранжевый
                    else c = glm::vec3(0, 0.5f, 1); // деформация — синий
                } else {
                    // По умолчанию — по скорости как на фото
                    float velMag = glm::length(v);
                    c = getVelocityMagnitudeColor(velMag, maxSpeed);
                }
            } else {
                c = colorForPoint(v, surfDist, flowParams);
                if (aeroColorStreamlinesByVelocity) {
                    float velMag = glm::length(v);
                    c = getVelocityMagnitudeColor(velMag, maxSpeed);
                }
            }
            if (!std::isfinite(c.x)) c = glm::vec3(0.3f,0.8f,1.0f);
            colPtr[3*i] = c.x;
            colPtr[3*i+1] = c.y;
            colPtr[3*i+2] = c.z;
        }
    }
    if (particleVBO_pos != 0 && particleVBO_col != 0) {
        glBindBuffer(GL_ARRAY_BUFFER, particleVBO_pos);
        glBufferSubData(GL_ARRAY_BUFFER, 0, particlePositions.size()*sizeof(float), particlePositions.data());
        glBindBuffer(GL_ARRAY_BUFFER, particleVBO_col);
        glBufferSubData(GL_ARRAY_BUFFER, 0, particleColors.size()*sizeof(float), particleColors.data());
    }
}
