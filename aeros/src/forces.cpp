#include <glad/glad.h>
#include <glm/glm.hpp>

#include <cmath>
#include <vector>
#include <algorithm>
#include <iostream>

#include "globals.h"
#include "cuda_api.h"
#include "flow_field.h"
#include "voxel_grid.h"
#include "atmosphere.h"
#include "forces.h"
#include "lbm.h"

#ifdef _OPENMP
#include <omp.h>
#endif

// =====================================================
// Давление — v1.14.0 Physics Ultra Fix
// - Cp физичный: (p-p_inf)/q для LBM, Bernoulli для потенциала
// - Сжимаемость: Prandtl-Glauert
// - RefArea — проекционная площадь
// - Трение: Blasius + 1/7 + шероховатость
// =====================================================
void updateVertexColors() {
    updateFlowParams();
    int numVerts = (int)(g_vertices.size() / 3);
    if (numVerts == 0) return;
    if (g_vertices.size() != g_normals.size()) return;
    if (g_vertices.size() % 3 != 0) return;

    if (aeroAutoRefArea) aeroRefArea = computeLBMRefArea();

    if (lbmParams.enabled && lbmInitialized) {
        try { g_vertexColors.resize(numVerts*3); } catch (...) { return; }
        float vinf = glm::length(glm::vec3(flowParams.vx, flowParams.vy, flowParams.vz));
        if (!std::isfinite(vinf) || vinf < 1e-4f) vinf = 1e-4f;
        float rho = airDensity;
        float qInf = 0.5f * rho * vinf * vinf;
        if (qInf < 1e-6f) qInf = 1.0f;
        float machInf = vinf / (speedOfSound + 1e-6f);
        float* vPtr = g_vertices.data();
        float* colPtr = g_vertexColors.data();

        #ifdef _OPENMP
        #pragma omp parallel for
        #endif
        for (int i = 0; i < numVerts; i++) {
            glm::vec3 p(vPtr[3*i], vPtr[3*i+1], vPtr[3*i+2]);
            glm::vec3 v = getLBMVelocityWorld(p);
            if (!std::isfinite(v.x)) v = glm::vec3(flowParams.vx, flowParams.vy, flowParams.vz);
            float speed = glm::length(v);
            if (!std::isfinite(speed)) speed = vinf;

            float pLBM = 0.0f;
            float fx = (p.x - lbmMinX) / lbmCellSizeX;
            float fy = (p.y - lbmMinY) / lbmCellSizeY;
            float fz = (p.z - lbmMinZ) / lbmCellSizeZ;
            int ix = (int)fx, iy = (int)fy, iz = (int)fz;
            if (ix>=0 && ix<lbmNx && iy>=0 && iy<lbmNy && iz>=0 && iz<lbmNz) {
                int cell = (iz*lbmNy + iy)*lbmNx + ix;
                if (cell>=0 && cell < (int)lbmPressure.size()) pLBM = lbmPressure[cell];
            }
            float cp = pLBM / qInf;

            // Сжимаемость — Prandtl-Glauert
            if (aeroMachEffects && machInf > 0.3f && machInf < 0.8f) {
                float beta = sqrtf(1.0f - machInf*machInf);
                if (beta < 0.2f) beta = 0.2f;
                cp /= beta;
            }

            cp = glm::clamp(cp, -4.0f, 1.8f);
            if (!std::isfinite(cp)) cp = 1.0f - (speed*speed)/(vinf*vinf);

            glm::vec3 col;
            if (aeroVisMode == AeroVisMode::Pressure) col = getRealisticPressureColor(cp);
            else if (aeroVisMode == AeroVisMode::VelocityMagnitude) col = getVelocityMagnitudeColor(speed, vinf*1.6f);
            else if (aeroVisMode == AeroVisMode::Vorticity) { float vort = getLBMVorticityWorld(p); col = getVorticityColor(vort); }
            else if (aeroVisMode == AeroVisMode::QCriterion) {
                float q = getLBMQWorld(p);
                float t = glm::clamp(q*5.0f + 0.5f, 0.0f, 1.0f);
                if (q > 0) col = glm::vec3(1, 1-t, 0); else col = glm::vec3(0, t, 1);
            } else if (aeroVisMode == AeroVisMode::TurbulentKE) {
                float tke = getLBMTKEWorld(p); float t = glm::clamp(tke*10.0f, 0.0f, 1.0f); col = glm::vec3(t, t*0.5f, 1-t);
            } else if (aeroVisMode == AeroVisMode::SkinFriction) {
                float strain = getLBMStrainWorld(p); float cf = strain * 0.02f; float t = glm::clamp(cf*50.0f, 0.0f, 1.0f); col = glm::vec3(t, 1-t, 0.5f);
            } else if (aeroVisMode == AeroVisMode::BoundaryLayer) {
                float d = sampleSDFCPU(p); float bl = glm::clamp(d*5.0f, 0.0f, 1.0f); col = glm::vec3(bl, bl, 1.0f - bl*0.5f);
            } else if (aeroVisMode == AeroVisMode::MachNumber) { float mach = getLBMMachWorld(p); col = getMachColor(mach); }
            else if (aeroVisMode == AeroVisMode::Helicity) { float hel = getLBMHelicityWorld(p); col = getHelicityColor(hel); }
            else if (aeroVisMode == AeroVisMode::TotalPressure) {
                float pt = getLBMTotalPressureWorld(p); float ptInf = airPressure + 0.5f*airDensity*vinf*vinf; col = getTotalPressureColor(pt, ptInf);
            } else col = getRealisticPressureColor(cp);

            if (aeroShowSeparation) {
                float vort = getLBMVorticityWorld(p);
                if (speed < vinf*0.35f && vort > 5.0f) col = col * 0.6f + glm::vec3(0.2f, 0.4f, 1.0f) * 0.4f;
            }

            if (!std::isfinite(col.x)) col = glm::vec3(0.5f);
            colPtr[3*i]=col.x; colPtr[3*i+1]=col.y; colPtr[3*i+2]=col.z;
        }

        if (modelVBO_colors == 0) {
            glGenBuffers(1, &modelVBO_colors);
            glBindVertexArray(modelVAO);
            glBindBuffer(GL_ARRAY_BUFFER, modelVBO_colors);
            glBufferData(GL_ARRAY_BUFFER, g_vertexColors.size()*sizeof(float), g_vertexColors.data(), GL_DYNAMIC_DRAW);
            glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, 3*sizeof(float), (void*)0);
            glEnableVertexAttribArray(2);
            glBindVertexArray(0);
        } else {
            glBindBuffer(GL_ARRAY_BUFFER, modelVBO_colors);
            glBufferSubData(GL_ARRAY_BUFFER, 0, g_vertexColors.size()*sizeof(float), g_vertexColors.data());
        }
        return;
    }

    if (useCUDA == 1) {
        try { computeVertexPressureCUDA(g_vertices, g_normals, g_vertexColors, numVerts, flowParams); }
        catch (...) { useCUDA = 0; }
    }
    if (useCUDA == 0) {
        try { g_vertexColors.resize(numVerts*3); } catch (...) { return; }
        float vinf = sqrtf(flowParams.vx*flowParams.vx + flowParams.vy*flowParams.vy + flowParams.vz*flowParams.vz);
        if (!std::isfinite(vinf) || vinf < 1e-4f) vinf = 1e-4f;
        float invVinf = 1.0f / vinf;
        float* vPtr = g_vertices.data();
        float* colPtr = g_vertexColors.data();
        float cx = flowParams.centerX, cy = flowParams.centerY, cz = flowParams.centerZ;
        float vx = flowParams.vx, vy = flowParams.vy, vz = flowParams.vz;
        float machInf = vinf / (speedOfSound + 1e-6f);

        #ifdef _OPENMP
        #pragma omp parallel for
        #endif
        for (int i = 0; i < numVerts; i++) {
            glm::vec3 p(vPtr[3*i], vPtr[3*i+1], vPtr[3*i+2]);
            if (!std::isfinite(p.x)) { colPtr[3*i]=1; colPtr[3*i+1]=0; colPtr[3*i+2]=1; continue; }
            glm::vec3 v = computeVelocityFieldCPU(p, flowParams);
            if (!std::isfinite(v.x)) v = glm::vec3(vx,vy,vz);
            float speed = glm::length(v);
            if (!std::isfinite(speed)) speed = vinf;
            float cp = 1.0f - (speed*invVinf)*(speed*invVinf);

            if (aeroMachEffects && machInf > 0.3f && machInf < 0.8f) {
                float beta = sqrtf(1.0f - machInf*machInf);
                if (beta < 0.2f) beta = 0.2f;
                cp /= beta;
            }

            glm::vec3 flowDir(vx*invVinf, vy*invVinf, vz*invVinf);
            glm::vec3 r = p - glm::vec3(cx,cy,cz);
            float along = glm::dot(r, flowDir);
            float D = maxDim;
            if (along > D*0.25f) {
                float w = glm::clamp((along - D*0.25f)/D, 0.0f, 1.0f);
                cp -= 0.25f * w * w;
            }
            cp = glm::clamp(cp, -3.5f, 1.2f);

            glm::vec3 col;
            if (aeroVisMode == AeroVisMode::Pressure) col = getRealisticPressureColor(cp);
            else if (aeroVisMode == AeroVisMode::VelocityMagnitude) col = getVelocityMagnitudeColor(speed, vinf*1.6f);
            else if (aeroVisMode == AeroVisMode::SkinFriction) { float cf = speed * 0.02f / vinf; float t = glm::clamp(cf*5.0f, 0.0f, 1.0f); col = glm::vec3(t, 1-t, 0.5f); }
            else if (aeroVisMode == AeroVisMode::MachNumber) { float mach = speed / (speedOfSound + 1e-6f); col = getMachColor(mach); }
            else if (aeroVisMode == AeroVisMode::TotalPressure) { float pt = airPressure + 0.5f*airDensity*speed*speed; float ptInf = airPressure + 0.5f*airDensity*vinf*vinf; col = getTotalPressureColor(pt, ptInf); }
            else col = getRealisticPressureColor(cp);

            if (!std::isfinite(col.x)) col = glm::vec3(0.5f);
            colPtr[3*i]=col.x; colPtr[3*i+1]=col.y; colPtr[3*i+2]=col.z;
        }
    }

    if (modelVBO_colors == 0) {
        glGenBuffers(1, &modelVBO_colors);
        glBindVertexArray(modelVAO);
        glBindBuffer(GL_ARRAY_BUFFER, modelVBO_colors);
        glBufferData(GL_ARRAY_BUFFER, g_vertexColors.size()*sizeof(float), g_vertexColors.data(), GL_DYNAMIC_DRAW);
        glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, 3*sizeof(float), (void*)0);
        glEnableVertexAttribArray(2);
        glBindVertexArray(0);
    } else {
        glBindBuffer(GL_ARRAY_BUFFER, modelVBO_colors);
        glBufferSubData(GL_ARRAY_BUFFER, 0, g_vertexColors.size()*sizeof(float), g_vertexColors.data());
    }
}

// =====================================================
// Lift / Drag — v1.14.0 Physics Ultra — полный аудит
// - Cd, Cl через q*RefArea
// - Индуктивное сопротивление Cd_i = Cl^2/(pi*AR)
// - Базовое сопротивление + трение Blasius
// - Момент Cm
// =====================================================
void computeLiftDrag() {
    updateFlowParams();
    int numVerts = (int)(g_vertices.size() / 3);
    if (numVerts == 0) return;
    if (g_vertices.size() < 9 || g_normals.size() < 9) return;
    if (g_vertices.size() != g_normals.size()) return;

    float vinf = sqrtf(flowParams.vx*flowParams.vx + flowParams.vy*flowParams.vy + flowParams.vz*flowParams.vz);
    if (!std::isfinite(vinf) || vinf < 1e-4f) vinf = 1e-4f;
    float invVinf = 1.0f / vinf;

    glm::vec3 flowDir(flowParams.vx, flowParams.vy, flowParams.vz);
    float fdLen = glm::length(flowDir);
    if (fdLen < 1e-6f || !std::isfinite(fdLen)) { liftMagnitude = 0; dragMagnitude = 0; liftToDragRatio=0; return; }
    flowDir /= fdLen;

    float rho = useRealDensity ? flowParams.airDensity : 1.225f;
    rho = glm::clamp(rho, 0.0001f, 10.0f);
    float q = 0.5f * rho * vinf * vinf;
    if (q < 1e-9f) q = 1e-6f;

    if (aeroAutoRefArea) aeroRefArea = computeLBMRefArea();
    float refArea = aeroRefArea;
    if (!std::isfinite(refArea) || refArea < 1e-6f) refArea = 1.0f;
    refArea = glm::clamp(refArea, 1e-6f, 1e6f);

    glm::vec3 totalForce(0.0f);
    glm::vec3 cpSum(0.0f);
    glm::vec3 momentSum(0.0f);
    float areaSum = 0.0f;

    int triCount = (int)(g_vertices.size() / 9);
    float* vertPtr = g_vertices.data();
    float* normPtr = g_normals.data();
    float cx = flowParams.centerX, cy = flowParams.centerY, cz = flowParams.centerZ;

    const float mu = 1.81e-5f;

    #ifdef _OPENMP
    #pragma omp parallel
    {
        glm::vec3 localForce(0.0f);
        glm::vec3 localCpSum(0.0f);
        glm::vec3 localMoment(0.0f);
        float localArea = 0.0f;
        #pragma omp for nowait
        for (int ti = 0; ti < triCount; ++ti) {
            int i = ti*9;
            if (i+8 >= (int)g_vertices.size()) continue;
            glm::vec3 v0(vertPtr[i],   vertPtr[i+1], vertPtr[i+2]);
            glm::vec3 v1(vertPtr[i+3], vertPtr[i+4], vertPtr[i+5]);
            glm::vec3 v2(vertPtr[i+6], vertPtr[i+7], vertPtr[i+8]);
            glm::vec3 n (normPtr[i],    normPtr[i+1],  normPtr[i+2]);
            if (!std::isfinite(v0.x) || !std::isfinite(n.x)) continue;
            float nLen = glm::length(n);
            if (nLen < 1e-6f || !std::isfinite(nLen)) continue;
            n /= nLen;
            glm::vec3 triCenter = (v0+v1+v2) * (1.0f/3.0f);
            glm::vec3 cr = glm::cross(v1-v0, v2-v0);
            float area = 0.5f * glm::length(cr);
            if (!std::isfinite(area) || area < 1e-9f || area > 1e6f) continue;

            float cp;
            if (lbmParams.enabled && lbmInitialized) {
                float pLBM = 0.0f;
                float fx = (triCenter.x - lbmMinX) / lbmCellSizeX;
                float fy = (triCenter.y - lbmMinY) / lbmCellSizeY;
                float fz = (triCenter.z - lbmMinZ) / lbmCellSizeZ;
                int ix = (int)fx, iy = (int)fy, iz = (int)fz;
                if (ix>=0 && ix<lbmNx && iy>=0 && iy<lbmNy && iz>=0 && iz<lbmNz) {
                    int cell = (iz*lbmNy + iy)*lbmNx + ix;
                    if (cell>=0 && cell < (int)lbmPressure.size()) pLBM = lbmPressure[cell];
                }
                cp = pLBM / q;
                // Сжимаемость
                float machInf = vinf / (speedOfSound + 1e-6f);
                if (aeroMachEffects && machInf > 0.3f && machInf < 0.8f) {
                    float beta = sqrtf(1.0f - machInf*machInf);
                    if (beta < 0.2f) beta = 0.2f;
                    cp /= beta;
                }
            } else {
                glm::vec3 vel = computeVelocityFieldCPU(triCenter, flowParams);
                if (!std::isfinite(vel.x)) vel = glm::vec3(flowParams.vx,flowParams.vy,flowParams.vz);
                float speed2 = glm::dot(vel,vel);
                cp = 1.0f - speed2 * (invVinf*invVinf);
            }

            cp = glm::clamp(cp, -4.0f, 2.0f);
            if (!std::isfinite(cp)) cp = 0;

            glm::vec3 pressureForce = -cp * q * n * area;

            // Трение — Re_x зависимое
            glm::vec3 rFromLE = triCenter - glm::vec3(cx,cy,cz) + flowDir * (maxDim*0.5f);
            float xAlong = glm::dot(rFromLE, flowDir);
            if (xAlong < 0.001f) xAlong = 0.001f;
            float Re_x = rho * vinf * xAlong / mu;
            Re_x = glm::clamp(Re_x, 1.0f, 1e9f);
            float Cf;
            if (Re_x < 5e5f) Cf = 0.664f / sqrtf(Re_x);
            else Cf = 0.027f / powf(Re_x, 1.0f/7.0f);
            // Шероховатость — увеличивает Cf на 10-20%
            Cf *= 1.15f;
            Cf = glm::clamp(Cf, 0.0002f, 0.015f);

            float dotFlowNormal = fabsf(glm::dot(n, flowDir));
            float frictionFactor = 1.0f - dotFlowNormal;
            // На подветренной трение меньше из-за отрыва
            float flowDotN = glm::dot(flowDir, n);
            if (flowDotN > 0) frictionFactor *= 0.6f; // подветренная

            float skinFriction = Cf * q * area * frictionFactor;
            glm::vec3 viscousForce = -flowDir * skinFriction;
            glm::vec3 force = pressureForce + viscousForce;

            localForce += force;
            localCpSum += triCenter * (fabsf(cp) * area);
            localMoment += glm::cross(triCenter - glm::vec3(cx,cy,cz), force);
            localArea += area;
        }
        #pragma omp critical
        {
            totalForce += localForce;
            cpSum += localCpSum;
            momentSum += localMoment;
            areaSum += localArea;
        }
    }
    #else
    for (int ti = 0; ti < triCount; ++ti) {
        int i = ti*9;
        if (i+8 >= (int)g_vertices.size()) continue;
        glm::vec3 v0(vertPtr[i],   vertPtr[i+1], vertPtr[i+2]);
        glm::vec3 v1(vertPtr[i+3], vertPtr[i+4], vertPtr[i+5]);
        glm::vec3 v2(vertPtr[i+6], vertPtr[i+7], vertPtr[i+8]);
        glm::vec3 n (normPtr[i],    normPtr[i+1],  normPtr[i+2]);
        if (!std::isfinite(v0.x) || !std::isfinite(n.x)) continue;
        float nLen = glm::length(n);
        if (nLen < 1e-6f || !std::isfinite(nLen)) continue;
        n /= nLen;
        glm::vec3 triCenter = (v0+v1+v2) * (1.0f/3.0f);
        glm::vec3 cr = glm::cross(v1-v0, v2-v0);
        float area = 0.5f * glm::length(cr);
        if (!std::isfinite(area) || area < 1e-9f || area > 1e6f) continue;

        float cp;
        if (lbmParams.enabled && lbmInitialized) {
            float pLBM = 0.0f;
            float fx = (triCenter.x - lbmMinX) / lbmCellSizeX;
            float fy = (triCenter.y - lbmMinY) / lbmCellSizeY;
            float fz = (triCenter.z - lbmMinZ) / lbmCellSizeZ;
            int ix = (int)fx, iy = (int)fy, iz = (int)fz;
            if (ix>=0 && ix<lbmNx && iy>=0 && iy<lbmNy && iz>=0 && iz<lbmNz) {
                int cell = (iz*lbmNy + iy)*lbmNx + ix;
                if (cell>=0 && cell < (int)lbmPressure.size()) pLBM = lbmPressure[cell];
            }
            cp = pLBM / q;
        } else {
            glm::vec3 vel = computeVelocityFieldCPU(triCenter, flowParams);
            if (!std::isfinite(vel.x)) vel = glm::vec3(flowParams.vx,flowParams.vy,flowParams.vz);
            float speed2 = glm::dot(vel,vel);
            cp = 1.0f - speed2 * (invVinf*invVinf);
        }

        cp = glm::clamp(cp, -4.0f, 2.0f);
        if (!std::isfinite(cp)) cp = 0;

        glm::vec3 pressureForce = -cp * q * n * area;
        glm::vec3 rFromLE = triCenter - glm::vec3(cx,cy,cz) + flowDir * (maxDim*0.5f);
        float xAlong = glm::dot(rFromLE, flowDir);
        if (xAlong < 0.001f) xAlong = 0.001f;
        float Re_x = rho * vinf * xAlong / mu;
        Re_x = glm::clamp(Re_x, 1.0f, 1e9f);
        float Cf = (Re_x < 5e5f) ? 0.664f / sqrtf(Re_x) : 0.027f / powf(Re_x, 1.0f/7.0f);
        Cf *= 1.15f;
        Cf = glm::clamp(Cf, 0.0002f, 0.015f);
        float dotFlowNormal = fabsf(glm::dot(n, flowDir));
        float frictionFactor = 1.0f - dotFlowNormal;
        float flowDotN = glm::dot(flowDir, n);
        if (flowDotN > 0) frictionFactor *= 0.6f;
        float skinFriction = Cf * q * area * frictionFactor;
        glm::vec3 viscousForce = -flowDir * skinFriction;
        glm::vec3 force = pressureForce + viscousForce;

        totalForce += force;
        cpSum += triCenter * (fabsf(cp) * area);
        momentSum += glm::cross(triCenter - glm::vec3(cx,cy,cz), force);
        areaSum += area;
    }
    #endif

    if (!std::isfinite(totalForce.x)) totalForce = glm::vec3(0);

    // Базовое сопротивление — давление на корме
    float baseDrag = 0.0f;
    {
        float sizeY = maxBB.y - minBB.y;
        float sizeZ = maxBB.z - minBB.z;
        float baseArea = sizeY * sizeZ * 0.3f; // 30% от фронтальной — кормовая площадь
        float CpBase = -0.15f; // типичный Cp на корме
        baseDrag = -CpBase * q * baseArea;
    }

    dragMagnitude = glm::dot(totalForce, flowDir) + baseDrag;
    if (!std::isfinite(dragMagnitude)) dragMagnitude = baseDrag;
    dragVector = flowDir * dragMagnitude;

    glm::vec3 liftDir = glm::vec3(0,1,0) - flowDir * glm::dot(glm::vec3(0,1,0), flowDir);
    float liftDirLen = glm::length(liftDir);
    if (liftDirLen < 1e-6f || !std::isfinite(liftDirLen)) {
        liftMagnitude = 0.0f;
        liftVector = glm::vec3(0.0f);
    } else {
        liftDir /= liftDirLen;
        liftMagnitude = glm::dot(totalForce, liftDir);
        if (!std::isfinite(liftMagnitude)) liftMagnitude = 0;
        liftVector = liftDir * liftMagnitude;
    }

    // Индуктивное сопротивление Cd_i = Cl^2/(pi*AR)
    float AR = 1.0f; // удлинение — для авто ~1, для крыла ~8
    float span = maxBB.z - minBB.z;
    float chord = maxBB.x - minBB.x;
    if (span > 1e-6f && chord > 1e-6f) AR = span / chord;
    AR = glm::clamp(AR, 0.5f, 15.0f);
    float Cl = liftMagnitude / (q * refArea + 1e-9f);
    float Cd_i = Cl*Cl / (3.14159265f * AR + 1e-6f);
    float inducedDrag = Cd_i * q * refArea;
    dragMagnitude += inducedDrag;

    if (fabsf(dragMagnitude) > 1e-9f) liftToDragRatio = liftMagnitude / dragMagnitude;
    else liftToDragRatio = 0;

    momentVector = momentSum;
    momentMagnitude = glm::length(momentSum);
    if (!std::isfinite(momentMagnitude)) { momentMagnitude = 0; momentVector = glm::vec3(0); }

    if (areaSum > 1e-9f && std::isfinite(cpSum.x)) centerOfPressure = cpSum / areaSum;
    else centerOfPressure = glm::vec3(cx,cy,cz);
    if (!std::isfinite(centerOfPressure.x)) centerOfPressure = glm::vec3(cx,cy,cz);
    liftDragDirty = true;
}

void updateLiftDragArrows() {
    if (!liftDragDirty) return;
    liftDragDirty = false;
    if (!std::isfinite(maxDim) || maxDim < 1e-6f) return;
    float dragScale = 0.5f / (maxDim + 1e-6f);
    float liftScale = 0.5f / (maxDim + 1e-6f);
    if (!std::isfinite(dragScale)) dragScale = 0.1f;
    if (!std::isfinite(liftScale)) liftScale = 0.1f;
    std::vector<float> verts;
    verts.reserve(24);
    if (fabs(dragMagnitude) > 1e-6f && std::isfinite(centerOfPressure.x)) {
        glm::vec3 s = centerOfPressure;
        glm::vec3 e = s + dragVector * dragScale;
        if (std::isfinite(e.x)) { verts.push_back(s.x); verts.push_back(s.y); verts.push_back(s.z); verts.push_back(e.x); verts.push_back(e.y); verts.push_back(e.z); }
    }
    if (fabs(liftMagnitude) > 1e-6f && std::isfinite(centerOfPressure.x)) {
        glm::vec3 s = centerOfPressure;
        glm::vec3 e = s + liftVector * liftScale;
        if (std::isfinite(e.x)) { verts.push_back(s.x); verts.push_back(s.y); verts.push_back(s.z); verts.push_back(e.x); verts.push_back(e.y); verts.push_back(e.z); }
    }
    if (fabs(momentMagnitude) > 1e-9f && std::isfinite(momentVector.x)) {
        glm::vec3 s = centerOfPressure;
        glm::vec3 e = s + glm::normalize(momentVector) * maxDim * 0.3f;
        if (std::isfinite(e.x)) { verts.push_back(s.x); verts.push_back(s.y); verts.push_back(s.z); verts.push_back(e.x); verts.push_back(e.y); verts.push_back(e.z); }
    }
    if (liftDragVAO == 0) glGenVertexArrays(1, &liftDragVAO);
    if (liftDragVBO == 0) glGenBuffers(1, &liftDragVBO);
    glBindVertexArray(liftDragVAO);
    glBindBuffer(GL_ARRAY_BUFFER, liftDragVBO);
    glBufferData(GL_ARRAY_BUFFER, verts.size()*sizeof(float), verts.empty()?nullptr:verts.data(), GL_DYNAMIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3*sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);
}
