#include <glad/glad.h>
#include <glm/glm.hpp>

#include <cmath>
#include <vector>

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
// Давление — v1.6.0 оптимизировано с OpenMP
// =====================================================
void updateVertexColors() {
    updateFlowParams();
    int numVerts = (int)(g_vertices.size() / 3);
    if (numVerts == 0) return;
    if (g_vertices.size() != g_normals.size()) return;

    // LBM: если включен — используем LBM давление и скорость для Cp
    if (lbmParams.enabled && lbmInitialized) {
        try { g_vertexColors.resize(numVerts*3); } catch (...) { return; }
        float vinf = glm::length(glm::vec3(flowParams.vx, flowParams.vy, flowParams.vz));
        if (!std::isfinite(vinf) || vinf < 1e-4f) vinf = 1e-4f;
        float invVinf2 = 1.0f / (vinf*vinf);
        float maxCp = 1.0f, minCp = -3.0f;
        float rangeCp = maxCp - minCp;
        float invRange = 1.0f / rangeCp;
        float* vPtr = g_vertices.data();
        float* colPtr = g_vertexColors.data();

        #ifdef _OPENMP
        #pragma omp parallel for
        #endif
        for (int i = 0; i < numVerts; i++) {
            glm::vec3 p(vPtr[3*i], vPtr[3*i+1], vPtr[3*i+2]);
            glm::vec3 v = getLBMVelocityWorld(p);
            if (!std::isfinite(v.x)) v = glm::vec3(flowParams.vx, flowParams.vy, flowParams.vz);
            float speed2 = v.x*v.x + v.y*v.y + v.z*v.z;
            float cp = 1.0f - speed2*invVinf2;
            float lbmRho = getLBMDensityWorld(p);
            cp += (lbmRho - 1.0f) * 0.5f;
            if (cp > maxCp) cp = maxCp;
            if (cp < minCp) cp = minCp;
            float t = (cp - minCp) * invRange;
            glm::vec3 col;
            if (t<0.25f) { float k=t*4.0f; col=glm::vec3(0,k,1); }
            else if (t<0.5f) { float k=(t-0.25f)*4.0f; col=glm::vec3(0,1,1-k); }
            else if (t<0.75f) { float k=(t-0.5f)*4.0f; col=glm::vec3(k,1,0); }
            else { float k=(t-0.75f)*4.0f; col=glm::vec3(1,1-k,0); }
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
        catch (...) {}
    } else {
        try { g_vertexColors.resize(numVerts*3); } catch (...) { return; }
        float vinf = sqrtf(flowParams.vx*flowParams.vx + flowParams.vy*flowParams.vy + flowParams.vz*flowParams.vz);
        if (!std::isfinite(vinf) || vinf < 1e-4f) vinf = 1e-4f;
        float invVinf = 1.0f / vinf;
        float* vPtr = g_vertices.data();
        float* colPtr = g_vertexColors.data();
        float cx = flowParams.centerX, cy = flowParams.centerY, cz = flowParams.centerZ;
        float vx = flowParams.vx, vy = flowParams.vy, vz = flowParams.vz;
        float radY = flowParams.radiusY, radZ = flowParams.radiusZ;

        #ifdef _OPENMP
        #pragma omp parallel for
        #endif
        for (int i = 0; i < numVerts; i++) {
            glm::vec3 p(vPtr[3*i], vPtr[3*i+1], vPtr[3*i+2]);
            glm::vec3 v = computeVelocityFieldCPU(p, flowParams);
            float speed = glm::length(v);
            float cp = 1.0f - (speed*invVinf)*(speed*invVinf);
            float rx = p.x - cx, ry = p.y - cy, rz = p.z - cz;
            float along = rx*(vx*invVinf) + ry*(vy*invVinf) + rz*(vz*invVinf);
            float D = 2.0f * fmaxf(radY, radZ);
            if (D < 1e-6f) D = 0.5f;
            if (along > D * 0.3f) {
                float w = (along - D*0.3f) / D;
                if (w > 1.0f) w = 1.0f;
                cp -= 0.8f * w * w;
            }
            if (cp > 1.0f) cp = 1.0f;
            if (cp < -3.0f) cp = -3.0f;
            float t = (cp+1.0f)*0.5f;
            t = glm::clamp(t, 0.0f, 1.0f);
            glm::vec3 col;
            if (t<0.25f) { float k=t*4.0f; col=glm::vec3(0,k,1); }
            else if (t<0.5f) { float k=(t-0.25f)*4.0f; col=glm::vec3(0,1,1-k); }
            else if (t<0.75f) { float k=(t-0.5f)*4.0f; col=glm::vec3(k,1,0); }
            else { float k=(t-0.75f)*4.0f; col=glm::vec3(1,1-k,0); }
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
// Lift / Drag — v1.6.0 оптимизировано
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
    if (fdLen < 1e-6f) { liftMagnitude = 0; dragMagnitude = 0; return; }
    flowDir /= fdLen;

    float rho = useRealDensity ? flowParams.airDensity : 1.225f;
    if (rho < 0.0001f) rho = 0.0001f;
    if (rho > 10.0f) rho = 10.0f;
    float q = 0.5f * rho * vinf * vinf;
    if (q < 1e-9f) q = 1e-6f;

    glm::vec3 totalForce(0.0f);
    glm::vec3 cpSum(0.0f);
    float areaSum = 0.0f;

    float D = 2.0f * fmaxf(flowParams.radiusY, flowParams.radiusZ);
    if (D < 1e-4f) D = 0.5f;

    int triCount = (int)(g_vertices.size() / 9);
    float* vertPtr = g_vertices.data();
    float* normPtr = g_normals.data();
    float cx = flowParams.centerX, cy = flowParams.centerY, cz = flowParams.centerZ;
    float fvx = flowParams.vx, fvy = flowParams.vy, fvz = flowParams.vz;

    // Для OpenMP — ручная редукция для glm::vec3
    #ifdef _OPENMP
    #pragma omp parallel
    {
        glm::vec3 localForce(0.0f);
        glm::vec3 localCpSum(0.0f);
        float localArea = 0.0f;
        #pragma omp for nowait
        for (int ti = 0; ti < triCount; ++ti) {
            int i = ti*9;
            glm::vec3 v0(vertPtr[i],   vertPtr[i+1], vertPtr[i+2]);
            glm::vec3 v1(vertPtr[i+3], vertPtr[i+4], vertPtr[i+5]);
            glm::vec3 v2(vertPtr[i+6], vertPtr[i+7], vertPtr[i+8]);
            glm::vec3 n (normPtr[i],    normPtr[i+1],  normPtr[i+2]);
            float nLen = glm::length(n);
            if (nLen < 1e-6f) continue;
            n /= nLen;
            glm::vec3 triCenter = (v0+v1+v2) * (1.0f/3.0f);
            glm::vec3 cr = glm::cross(v1-v0, v2-v0);
            float area = 0.5f * glm::length(cr);
            if (area < 1e-9f || area > 1e6f) continue;

            glm::vec3 vel = computeVelocityFieldCPU(triCenter, flowParams);
            float speed2 = vel.x*vel.x + vel.y*vel.y + vel.z*vel.z;
            float cp = 1.0f - speed2 * (invVinf*invVinf);
            float rx = triCenter.x - cx, ry = triCenter.y - cy, rz = triCenter.z - cz;
            float along = rx*(fvx*invVinf) + ry*(fvy*invVinf) + rz*(fvz*invVinf);
            if (along > D * 0.3f) {
                float w = (along - D*0.3f) / D;
                if (w > 1.0f) w = 1.0f;
                cp -= 0.8f * w * w;
            }
            if (cp > 1.5f) cp = 1.5f;
            if (cp < -3.0f) cp = -3.0f;

            glm::vec3 pressureForce = -cp * q * n * area;
            float dotFlowNormal = fabsf(glm::dot(n, flowDir));
            float skinFriction = 0.02f * q * area * (1.0f - dotFlowNormal);
            glm::vec3 viscousForce = -flowDir * skinFriction;
            glm::vec3 force = pressureForce + viscousForce;

            localForce += force;
            localCpSum += triCenter * (fabsf(cp) * area);
            localArea += area;
        }
        #pragma omp critical
        {
            totalForce += localForce;
            cpSum += localCpSum;
            areaSum += localArea;
        }
    }
    #else
    for (int ti = 0; ti < triCount; ++ti) {
        int i = ti*9;
        glm::vec3 v0(vertPtr[i],   vertPtr[i+1], vertPtr[i+2]);
        glm::vec3 v1(vertPtr[i+3], vertPtr[i+4], vertPtr[i+5]);
        glm::vec3 v2(vertPtr[i+6], vertPtr[i+7], vertPtr[i+8]);
        glm::vec3 n (normPtr[i],    normPtr[i+1],  normPtr[i+2]);
        float nLen = glm::length(n);
        if (nLen < 1e-6f) continue;
        n /= nLen;
        glm::vec3 triCenter = (v0+v1+v2) * (1.0f/3.0f);
        glm::vec3 cr = glm::cross(v1-v0, v2-v0);
        float area = 0.5f * glm::length(cr);
        if (area < 1e-9f || area > 1e6f) continue;

        glm::vec3 vel = computeVelocityFieldCPU(triCenter, flowParams);
        float speed2 = vel.x*vel.x + vel.y*vel.y + vel.z*vel.z;
        float cp = 1.0f - speed2 * (invVinf*invVinf);
        float rx = triCenter.x - cx, ry = triCenter.y - cy, rz = triCenter.z - cz;
        float along = rx*(fvx*invVinf) + ry*(fvy*invVinf) + rz*(fvz*invVinf);
        if (along > D * 0.3f) {
            float w = (along - D*0.3f) / D;
            if (w > 1.0f) w = 1.0f;
            cp -= 0.8f * w * w;
        }
        if (cp > 1.5f) cp = 1.5f;
        if (cp < -3.0f) cp = -3.0f;

        glm::vec3 pressureForce = -cp * q * n * area;
        float dotFlowNormal = fabsf(glm::dot(n, flowDir));
        float skinFriction = 0.02f * q * area * (1.0f - dotFlowNormal);
        glm::vec3 viscousForce = -flowDir * skinFriction;
        glm::vec3 force = pressureForce + viscousForce;

        totalForce += force;
        cpSum += triCenter * (fabsf(cp) * area);
        areaSum += area;
    }
    #endif

    dragMagnitude = glm::dot(totalForce, flowDir);
    dragVector = flowDir * dragMagnitude;

    glm::vec3 liftDir = glm::vec3(0,1,0) - flowDir * glm::dot(glm::vec3(0,1,0), flowDir);
    float liftDirLen = glm::length(liftDir);
    if (liftDirLen < 1e-6f) {
        liftMagnitude = 0.0f;
        liftVector = glm::vec3(0.0f);
    } else {
        liftDir /= liftDirLen;
        liftMagnitude = glm::dot(totalForce, liftDir);
        liftVector = liftDir * liftMagnitude;
    }

    if (areaSum > 1e-9f) {
        centerOfPressure = cpSum / areaSum;
    }
    liftDragDirty = true;
}

void updateLiftDragArrows() {
    if (!liftDragDirty) return;
    liftDragDirty = false;

    float dragScale = 0.5f / (maxDim + 1e-6f);
    float liftScale = 0.5f / (maxDim + 1e-6f);

    std::vector<float> verts;
    verts.reserve(12);
    if (fabs(dragMagnitude) > 1e-6f) {
        glm::vec3 s = centerOfPressure;
        glm::vec3 e = s + dragVector * dragScale;
        verts.push_back(s.x); verts.push_back(s.y); verts.push_back(s.z);
        verts.push_back(e.x); verts.push_back(e.y); verts.push_back(e.z);
    }
    if (fabs(liftMagnitude) > 1e-6f) {
        glm::vec3 s = centerOfPressure;
        glm::vec3 e = s + liftVector * liftScale;
        verts.push_back(s.x); verts.push_back(s.y); verts.push_back(s.z);
        verts.push_back(e.x); verts.push_back(e.y); verts.push_back(e.z);
    }

    if (liftDragVAO == 0) glGenVertexArrays(1, &liftDragVAO);
    if (liftDragVBO == 0) glGenBuffers(1, &liftDragVBO);
    glBindVertexArray(liftDragVAO);
    glBindBuffer(GL_ARRAY_BUFFER, liftDragVBO);
    glBufferData(GL_ARRAY_BUFFER, verts.size()*sizeof(float), verts.data(), GL_DYNAMIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3*sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);
}
