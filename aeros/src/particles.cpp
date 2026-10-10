#include <glad/glad.h>
#include <glm/glm.hpp>

#include <cmath>
#include <vector>
#include <iostream>

#include "globals.h"
#include "cuda_api.h"
#include "flow_field.h"
#include "voxel_grid.h"
#include "particles.h"
#include "lbm.h"
#include "atmosphere.h"

#ifdef _OPENMP
#include <omp.h>
#endif

// =====================================================
// Частицы — v1.19.0 Physics Logic Fix — аэродинамика
// - Уравнение движения: m*dv/dt = Fd + Fg + Fb
// - Fd = 0.5*Cd*rho*A*|Vrel|*Vrel, Cd(Re_p) Whitaker
// - Re_p = rho*|Vrel|*d / mu, mu Sutherland
// - Stokes: tau_p = rho_p*d²/(18*mu), St = tau_p / tau_f
// - RK4 для скорости и позиции, коллизии с SDF
// - Испарение/оседание по размеру
// =====================================================

// Cd для сферы по Whitaker + Clift-Gauvin
static inline float sphereCd(float Re_p) {
    if (Re_p < 1e-6f) return 0.0f;
    if (Re_p < 0.1f) return 24.0f / Re_p; // Stokes
    else if (Re_p < 1.0f) return (24.0f/Re_p)*(1.0f + 0.15f*powf(Re_p,0.687f));
    else if (Re_p < 1000.0f) return (24.0f/Re_p)*(1.0f + 0.15f*powf(Re_p,0.687f)) + 0.42f/(1.0f+42500.0f*powf(Re_p,-1.16f));
    else if (Re_p < 2e5f) return 0.44f;
    else return 0.1f; // drag crisis
}

static inline glm::vec3 particleAcceleration(const glm::vec3& pPos, const glm::vec3& pVel, const FlowParams& prm, float d_p, float rho_p) {
    glm::vec3 vFluid = computeVelocityFieldCPU(pPos, prm);
    if (!std::isfinite(vFluid.x)) vFluid = glm::vec3(prm.vx, prm.vy, prm.vz);
    glm::vec3 vRel = vFluid - pVel;
    float vRelMag = glm::length(vRel);
    if (vRelMag < 1e-6f) {
        // Только гравитация + buoyancy
        float rho_f = prm.airDensity;
        float g = 9.81f;
        float vol = 3.14159265f/6.0f * d_p*d_p*d_p;
        glm::vec3 Fg = glm::vec3(0, -rho_p*vol*g, 0);
        glm::vec3 Fb = glm::vec3(0, rho_f*vol*g, 0);
        float m_p = rho_p * vol;
        return (Fg+Fb)/m_p;
    }

    float rho_f = prm.airDensity;
    // Sutherland mu
    float T = prm.airTemperature;
    const float T0s = 273.15f, S = 110.4f, mu0 = 1.716e-5f;
    float mu = mu0 * powf(T/T0s, 1.5f) * (T0s+S)/(T+S);
    if (!std::isfinite(mu) || mu < 1e-6f) mu = 1.81e-5f;

    float Re_p = rho_f * vRelMag * d_p / mu;
    Re_p = glm::clamp(Re_p, 0.01f, 1e6f);
    float Cd = sphereCd(Re_p);
    float A = 3.14159265f*0.25f * d_p*d_p;
    float FdMag = 0.5f * Cd * rho_f * A * vRelMag * vRelMag;
    glm::vec3 Fd = glm::vec3(0);
    if (vRelMag > 1e-9f) Fd = (vRel / vRelMag) * FdMag;

    // Gravity + buoyancy
    float vol = 3.14159265f/6.0f * d_p*d_p*d_p;
    float g = 9.81f;
    glm::vec3 Fg(0, -rho_p*vol*g, 0);
    glm::vec3 Fb(0, rho_f*vol*g, 0);

    float m_p = rho_p * vol;
    if (m_p < 1e-12f) m_p = 1e-12f;
    glm::vec3 acc = (Fd + Fg + Fb) / m_p;
    return acc;
}

struct ParticleState {
    glm::vec3 pos;
    glm::vec3 vel;
};

static inline ParticleState particleRK4(const ParticleState& s, float dt, const FlowParams& prm, float d_p, float rho_p) {
    if (dt < 1e-9f) return s;
    auto accel = [&](const glm::vec3& pos, const glm::vec3& vel)->glm::vec3 {
        // Check collision — if inside, return 0 accel and push out handled outside
        if (!g_distanceField.empty()) {
            float d = sampleSDFCPU(pos);
            if (d <= 0.0f) return glm::vec3(0);
        }
        return particleAcceleration(pos, vel, prm, d_p, rho_p);
    };

    glm::vec3 a1 = accel(s.pos, s.vel);
    glm::vec3 v1 = s.vel;
    ParticleState s2; s2.pos = s.pos + v1*dt*0.5f; s2.vel = s.vel + a1*dt*0.5f;
    glm::vec3 a2 = accel(s2.pos, s2.vel);
    glm::vec3 v2 = s2.vel;
    ParticleState s3; s3.pos = s.pos + v2*dt*0.5f; s3.vel = s.vel + a2*dt*0.5f;
    glm::vec3 a3 = accel(s3.pos, s3.vel);
    glm::vec3 v3 = s3.vel;
    ParticleState s4; s4.pos = s.pos + v3*dt; s4.vel = s.vel + a3*dt;
    glm::vec3 a4 = accel(s4.pos, s4.vel);
    glm::vec3 v4 = s4.vel;

    ParticleState res;
    res.pos = s.pos + (v1 + 2.0f*v2 + 2.0f*v3 + v4)*(dt/6.0f);
    res.vel = s.vel + (a1 + 2.0f*a2 + 2.0f*a3 + a4)*(dt/6.0f);
    if (!std::isfinite(res.pos.x)) res = s;
    return res;
}

void initParticles() {
    updateFlowParams();
    if (numParticles <= 0) numParticles = 100;
    if (numParticles > 500000) numParticles = 500000;
    if (!std::isfinite(flowParams.minX) || !std::isfinite(flowParams.maxX) || flowParams.minX >= flowParams.maxX) { flowParams.minX = -5.0f; flowParams.maxX = 5.0f; }
    if (!std::isfinite(flowParams.minY) || !std::isfinite(flowParams.maxY) || flowParams.minY >= flowParams.maxY) { flowParams.minY = -5.0f; flowParams.maxY = 5.0f; }
    if (!std::isfinite(flowParams.minZ) || !std::isfinite(flowParams.maxZ) || flowParams.minZ >= flowParams.maxZ) { flowParams.minZ = -5.0f; flowParams.maxZ = 5.0f; }

    particleDrawCount = numParticles;
    if (useCUDA == 1) {
        try {
            initParticlesCUDA(particlePositions, particleColors, numParticles, flowParams);
        } catch (...) {
            useCUDA = 0;
        }
    }
    if (useCUDA == 0) {
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
    std::cout << "[Particles] Initialized " << numParticles << " v1.19.0 Physics Logic Fix" << std::endl;
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
        catch (...) { useCUDA = 0; }
    }
    if (useCUDA == 0) {
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
        if (!std::isfinite(maxSpeed) || maxSpeed < 1e-3f) maxSpeed = vInfMag * 1.6f;

        float* posPtr = particlePositions.data();
        float* colPtr = particleColors.data();
        const float* distField = g_distanceField.empty() ? nullptr : g_distanceField.data();
        int voxNx = g_voxNx, voxNy = g_voxNy, voxNz = g_voxNz;
        float voxMinX = g_voxMinX, voxMinY = g_voxMinY, voxMinZ = g_voxMinZ;
        bool useColl = useVoxelCollision && distField && voxNx > 0;
        int distSize = (int)g_distanceField.size();
        float groundY = g_voxMinY + aeroGroundHeight;

        // Particle properties — аэрозоль, плотность ~1000 кг/м3, диаметр 10-100 мкм
        float d_p_base = 20e-6f; // 20 мкм базовый
        float rho_p = 1000.0f; // вода

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
                if (!std::isfinite(r1)) r1 = 0.5f;
                if (!std::isfinite(r2)) r2 = 0.5f;
                px = minX + (maxX - minX)*(0.05f + 0.9f*r1);
                py = minY + (maxY - minY)*(0.05f + 0.9f*r2);
                pz = minZ + 0.05f;
            }

            // Индивидуальный размер частицы — логнормальное распределение
            float sizeRand = fabsf(sinf(i*73.123f)*43758.5f); sizeRand -= floorf(sizeRand);
            float d_p = d_p_base * (0.5f + sizeRand*1.0f); // 10-30 мкм

            glm::vec3 pPos(px, py, pz);
            // Оценка скорости частицы из предыдущего кадра через цвет? Упростим — скорость = скорость потока
            glm::vec3 vFluid = computeVelocityFieldCPU(pPos, flowParams);
            if (!std::isfinite(vFluid.x)) vFluid = glm::vec3(vxInf, vyInf, vzInf);
            // Предполагаем что частица уже имеет скорость близкую к потоку, но с инерцией
            glm::vec3 pVel = vFluid * 0.9f; // начальная

            ParticleState state; state.pos = pPos; state.vel = pVel;
            ParticleState newState;
            if (aeroUseRK4Particles) {
                newState = particleRK4(state, dt * timeScale, flowParams, d_p, rho_p);
            } else {
                glm::vec3 acc = particleAcceleration(pPos, pVel, flowParams, d_p, rho_p);
                newState.vel = pVel + acc * dt * timeScale;
                newState.pos = pPos + newState.vel * dt * timeScale;
            }

            glm::vec3 np = newState.pos;
            glm::vec3 nv = newState.vel;
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
                        if (rawDist > 1e4f || rawDist < -1e4f) rawDist = 1000.0f;
                        surfDist = rawDist * csx;
                        if (rawDist < 0.0f) {
                            glm::vec3 nrm = sdfNormalCPU(np);
                            if (!std::isfinite(nrm.x)) nrm = glm::vec3(0,1,0);
                            nrm = glm::normalize(nrm);
                            float push = fabsf(surfDist) + 0.7f * csx;
                            if (push > csx*2.5f) push = csx*2.5f;
                            np += nrm * push;

                            float vn = glm::dot(nv, nrm);
                            if (vn < 0) {
                                glm::vec3 v_n = nrm * vn;
                                glm::vec3 v_t = nv - v_n;
                                // Коэффициент восстановления: нормальная 0.1 (почти прилипание), касательная 0.85 трение
                                float restitutionNormal = 0.1f;
                                float frictionTang = 0.82f;
                                // Для аэрозоля — почти прилипание, но скольжение вдоль потока
                                nv = v_t * frictionTang - v_n * restitutionNormal;
                                float vtMag = glm::length(nv);
                                if (vtMag < 0.08f * vInfMag) {
                                    glm::vec3 flowDir(vxInf, vyInf, vzInf);
                                    float fl = glm::length(flowDir);
                                    if (fl > 1e-6f) flowDir /= fl; else flowDir = glm::vec3(1,0,0);
                                    glm::vec3 flowTang = flowDir - nrm * glm::dot(flowDir, nrm);
                                    float ftl = glm::length(flowTang);
                                    if (ftl > 1e-6f) {
                                        flowTang /= ftl;
                                        nv += flowTang * (0.18f * vInfMag);
                                    }
                                }
                            }
                        }
                    }
                }
            }

            if (aeroGroundEffect) {
                if (np.y < groundY + csy*0.6f) {
                    if (np.y < groundY) np.y = groundY + csy*0.6f;
                    // Отскок от земли — с трением
                    if (nv.y < 0) nv.y = -nv.y * 0.25f;
                    nv.x *= 0.9f; nv.z *= 0.9f;
                    // Оседание — если частица тяжелая и медленная, остается на земле
                    float T = flowParams.airTemperature;
                    const float T0s = 273.15f, S = 110.4f, mu0 = 1.716e-5f;
                    float mu = mu0 * powf(T/T0s, 1.5f) * (T0s+S)/(T+S);
                    float tau_p = rho_p * d_p*d_p / (18.0f*mu);
                    float vTerm = tau_p * 9.81f; // терминальная скорость оседания
                    if (glm::length(nv) < vTerm*2.0f) {
                        // Частица осела — респавн
                        float r1 = fabsf(sinf(i*12.9898f + time*11.0f) * 43758.5453f); r1 -= floorf(r1);
                        float r2 = fabsf(cosf(i*39.346f + time*16.0f) * 24634.6345f); r2 -= floorf(r2);
                        np.x = minX + (maxX-minX)*(0.05f + 0.9f*r1);
                        np.y = minY + (maxY-minY)*(0.05f + 0.9f*r2);
                        np.z = minZ + 0.05f;
                        nv = glm::vec3(vxInf, vyInf, vzInf) * 0.9f;
                    }
                }
            }

            // Границы домена — респавн
            if (np.x < minX || np.x > maxX || np.y < minY || np.y > maxY || np.z < minZ || np.z > maxZ) {
                float r1 = fabsf(sinf(i*12.9898f + time*12.0f) * 43758.5453f); r1 -= floorf(r1);
                float r2 = fabsf(cosf(i*39.346f + time*17.0f) * 24634.6345f); r2 -= floorf(r2);
                np.x = minX + (maxX-minX)*(0.05f + 0.9f*r1);
                np.y = minY + (maxY-minY)*(0.05f + 0.9f*r2);
                np.z = minZ + 0.05f;
                nv = glm::vec3(vxInf, vyInf, vzInf) * 0.9f;
            }

            posPtr[3*i] = np.x; posPtr[3*i+1] = np.y; posPtr[3*i+2] = np.z;

            // Цвет по скорости + по Re_p + по размеру
            glm::vec3 vFlow = computeVelocityFieldCPU(np, flowParams);
            float speed = glm::length(vFlow);
            if (!std::isfinite(speed)) speed = vInfMag;
            float t = glm::clamp(speed / maxSpeed, 0.0f, 1.0f);

            // Цветовая схема: по скорости как раньше, но с учетом размера (большие — темнее)
            glm::vec3 col;
            if (t < 0.25f) col = glm::vec3(0.0f, t*4.0f, 1.0f);
            else if (t < 0.5f) col = glm::vec3(0.0f, 1.0f, 1.0f - (t-0.25f)*4.0f);
            else if (t < 0.75f) col = glm::vec3((t-0.5f)*4.0f, 1.0f, 0.0f);
            else col = glm::vec3(1.0f, 1.0f - (t-0.75f)*4.0f, 0.0f);

            float sizeFactor = (d_p / d_p_base); // 0.5-1.5
            col *= (0.7f + 0.3f/sizeFactor); // большие темнее

            // Подсветка отрыва
            if (aeroShowSeparation && surfDist < csx*2.0f) {
                col = col * 0.5f + glm::vec3(1.0f, 0.3f, 0.1f)*0.5f;
            }

            colPtr[3*i] = col.x; colPtr[3*i+1] = col.y; colPtr[3*i+2] = col.z;
        }

        if (particleVAO != 0) {
            glBindBuffer(GL_ARRAY_BUFFER, particleVBO_pos);
            glBufferSubData(GL_ARRAY_BUFFER, 0, n*3*sizeof(float), posPtr);
            glBindBuffer(GL_ARRAY_BUFFER, particleVBO_col);
            glBufferSubData(GL_ARRAY_BUFFER, 0, n*3*sizeof(float), colPtr);
        }
    }
}
