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

// =====================================================
// Давление — v1.4.0 защита
// =====================================================
void updateVertexColors() {
    updateFlowParams();
    int numVerts = (int)(g_vertices.size() / 3);
    if (numVerts == 0) return;
    if (g_vertices.size() != g_normals.size()) return;

    if (useCUDA == 1) {
        try { computeVertexPressureCUDA(g_vertices, g_normals, g_vertexColors, numVerts, flowParams); }
        catch (...) {}
    } else {
        try { g_vertexColors.resize(numVerts*3); } catch (...) { return; }
        float vinf = sqrtf(flowParams.vx*flowParams.vx + flowParams.vy*flowParams.vy + flowParams.vz*flowParams.vz);
        if (!std::isfinite(vinf) || vinf < 1e-4f) vinf = 1e-4f;
        for (int i = 0; i < numVerts; i++) {
            glm::vec3 p(g_vertices[3*i], g_vertices[3*i+1], g_vertices[3*i+2]);
            if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)) continue;
            glm::vec3 v = computeVelocityFieldCPU(p, flowParams);
            if (!std::isfinite(v.x) || !std::isfinite(v.y) || !std::isfinite(v.z)) v = glm::vec3(flowParams.vx, flowParams.vy, flowParams.vz);
            float speed = glm::length(v);
            if (!std::isfinite(speed)) speed = vinf;
            float speedRatio = speed / vinf;
            if (!std::isfinite(speedRatio)) speedRatio = 1.0f;
            float cp = 1.0f - speedRatio*speedRatio;
            if (!std::isfinite(cp)) cp = 0.0f;
            float rx = p.x - flowParams.centerX;
            float ry = p.y - flowParams.centerY;
            float rz = p.z - flowParams.centerZ;
            float fl = 1.0f / vinf;
            if (!std::isfinite(fl)) fl = 0.0f;
            float along = rx*(flowParams.vx*fl) + ry*(flowParams.vy*fl) + rz*(flowParams.vz*fl);
            float D = 2.0f * fmaxf(flowParams.radiusY, flowParams.radiusZ);
            if (!std::isfinite(D) || D < 1e-6f) D = 0.5f;
            if (along > D * 0.3f) {
                float w = (along - D*0.3f) / D;
                if (!std::isfinite(w)) w = 0;
                if (w > 1.0f) w = 1.0f;
                if (w < 0) w = 0;
                cp -= 0.8f * w * w;
            }
            if (!std::isfinite(cp)) cp = 0;
            if (cp > 1.0f) cp = 1.0f;
            if (cp < -3.0f) cp = -3.0f;
            float t = (cp+1.0f)/2.0f;
            if (!std::isfinite(t)) t = 0.5f;
            t = glm::clamp(t, 0.0f, 1.0f);
            glm::vec3 col;
            if (t<0.25f) { float k=t/0.25f; col=glm::vec3(0,k,1); }
            else if (t<0.5f) { float k=(t-0.25f)/0.25f; col=glm::vec3(0,1,1-k); }
            else if (t<0.75f) { float k=(t-0.5f)/0.25f; col=glm::vec3(k,1,0); }
            else { float k=(t-0.75f)/0.25f; col=glm::vec3(1,1-k,0); }
            g_vertexColors[3*i]=col.x; g_vertexColors[3*i+1]=col.y; g_vertexColors[3*i+2]=col.z;
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
// Lift / Drag — v1.4.0 защита + плотность
// =====================================================
void computeLiftDrag() {
    updateFlowParams();
    int numVerts = (int)(g_vertices.size() / 3);
    if (numVerts == 0) return;
    if (g_vertices.size() < 9 || g_normals.size() < 9) return;
    if (g_vertices.size() != g_normals.size()) return;

    float vinf = sqrtf(flowParams.vx*flowParams.vx + flowParams.vy*flowParams.vy + flowParams.vz*flowParams.vz);
    if (!std::isfinite(vinf) || vinf < 1e-4f) vinf = 1e-4f;

    glm::vec3 flowDir(flowParams.vx, flowParams.vy, flowParams.vz);
    float fdLen = glm::length(flowDir);
    if (!std::isfinite(fdLen) || fdLen < 1e-6f) { liftMagnitude = 0; dragMagnitude = 0; return; }
    flowDir /= fdLen;

    float rho = useRealDensity ? flowParams.airDensity : 1.225f;
    if (!std::isfinite(rho) || rho < 0.0001f) rho = 0.0001f;
    if (rho > 10.0f) rho = 10.0f;
    float q = 0.5f * rho * vinf * vinf;
    if (!std::isfinite(q) || q < 1e-9f) q = 1e-6f;

    glm::vec3 totalForce(0.0f);
    glm::vec3 cpSum(0.0f);
    float areaSum = 0.0f;

    float D = 2.0f * fmaxf(flowParams.radiusY, flowParams.radiusZ);
    if (!std::isfinite(D) || D < 1e-4f) D = 0.5f;

    for (size_t i = 0; i + 8 < g_vertices.size(); i += 9) {
        glm::vec3 v0(g_vertices[i],   g_vertices[i+1], g_vertices[i+2]);
        glm::vec3 v1(g_vertices[i+3], g_vertices[i+4], g_vertices[i+5]);
        glm::vec3 v2(g_vertices[i+6], g_vertices[i+7], g_vertices[i+8]);
        glm::vec3 n (g_normals[i],    g_normals[i+1],  g_normals[i+2]);
        if (!std::isfinite(v0.x) || !std::isfinite(v1.x) || !std::isfinite(v2.x) || !std::isfinite(n.x)) continue;
        float nLen = glm::length(n);
        if (!std::isfinite(nLen) || nLen < 1e-6f) continue;
        n /= nLen;
        glm::vec3 triCenter = (v0+v1+v2) / 3.0f;
        if (!std::isfinite(triCenter.x)) continue;
        glm::vec3 cr = glm::cross(v1-v0, v2-v0);
        float area = 0.5f * glm::length(cr);
        if (!std::isfinite(area) || area < 1e-9f) continue;
        if (area > 1e6f) continue;
        glm::vec3 vel = computeVelocityFieldCPU(triCenter, flowParams);
        if (!std::isfinite(vel.x)) vel = glm::vec3(flowParams.vx, flowParams.vy, flowParams.vz);
        float speed = glm::length(vel);
        if (!std::isfinite(speed)) speed = vinf;
        float cp = 1.0f - (speed*speed)/(vinf*vinf);
        if (!std::isfinite(cp)) cp = 0.0f;
        float rx = triCenter.x - flowParams.centerX;
        float ry = triCenter.y - flowParams.centerY;
        float rz = triCenter.z - flowParams.centerZ;
        float fl = 1.0f / vinf;
        if (!std::isfinite(fl)) fl = 0;
        float along = rx*(flowParams.vx*fl) + ry*(flowParams.vy*fl) + rz*(flowParams.vz*fl);
        if (!std::isfinite(along)) along = 0;
        if (along > D * 0.3f) {
            float w = (along - D*0.3f) / D;
            if (!std::isfinite(w)) w = 0;
            if (w > 1.0f) w = 1.0f;
            if (w < 0) w = 0;
            cp -= 0.8f * w * w;
        }
        if (cp > 1.5f) cp = 1.5f;
        if (cp < -3.0f) cp = -3.0f;
        glm::vec3 pressureForce = -cp * q * n * area;
        if (!std::isfinite(pressureForce.x)) pressureForce = glm::vec3(0);
        float viscousCoeff = 0.02f;
        float dotFlowNormal = fabsf(glm::dot(n, flowDir));
        if (!std::isfinite(dotFlowNormal)) dotFlowNormal = 0;
        float skinFriction = viscousCoeff * q * area * (1.0f - dotFlowNormal);
        if (!std::isfinite(skinFriction)) skinFriction = 0;
        glm::vec3 viscousForce = -flowDir * skinFriction;
        glm::vec3 force = pressureForce + viscousForce;
        if (!std::isfinite(force.x)) continue;
        totalForce += force;
        if (!std::isfinite(cp)) cp = 0;
        cpSum += triCenter * (fabsf(cp) * area);
        areaSum += area;
    }

    if (!std::isfinite(totalForce.x)) totalForce = glm::vec3(0);
    dragMagnitude = glm::dot(totalForce, flowDir);
    if (!std::isfinite(dragMagnitude)) dragMagnitude = 0;
    dragVector = flowDir * dragMagnitude;
    if (!std::isfinite(dragVector.x)) dragVector = glm::vec3(0);

    glm::vec3 liftDir = glm::vec3(0,1,0) - flowDir * glm::dot(glm::vec3(0,1,0), flowDir);
    float liftDirLen = glm::length(liftDir);
    if (!std::isfinite(liftDirLen) || liftDirLen < 1e-6f) {
        liftMagnitude = 0.0f;
        liftVector = glm::vec3(0.0f);
    } else {
        liftDir = liftDir / liftDirLen;
        liftMagnitude = glm::dot(totalForce, liftDir);
        if (!std::isfinite(liftMagnitude)) liftMagnitude = 0;
        liftVector = liftDir * liftMagnitude;
        if (!std::isfinite(liftVector.x)) liftVector = glm::vec3(0);
    }

    if (areaSum > 1e-9f && std::isfinite(areaSum)) {
        glm::vec3 cop = cpSum / areaSum;
        if (std::isfinite(cop.x)) centerOfPressure = cop;
    }
    liftDragDirty = true;
}

void updateLiftDragArrows() {
    if (!liftDragDirty) return;
    liftDragDirty = false;

    float dragScale = 0.5f / (maxDim + 1e-6f);
    float liftScale = 0.5f / (maxDim + 1e-6f);
    if (!std::isfinite(dragScale)) dragScale = 0.1f;
    if (!std::isfinite(liftScale)) liftScale = 0.1f;

    std::vector<float> verts;
    if (fabs(dragMagnitude) > 1e-6f && std::isfinite(dragMagnitude)) {
        glm::vec3 s = centerOfPressure;
        glm::vec3 e = s + dragVector * dragScale;
        if (std::isfinite(s.x) && std::isfinite(e.x)) {
            verts.push_back(s.x); verts.push_back(s.y); verts.push_back(s.z);
            verts.push_back(e.x); verts.push_back(e.y); verts.push_back(e.z);
        }
    }
    if (fabs(liftMagnitude) > 1e-6f && std::isfinite(liftMagnitude)) {
        glm::vec3 s = centerOfPressure;
        glm::vec3 e = s + liftVector * liftScale;
        if (std::isfinite(s.x) && std::isfinite(e.x)) {
            verts.push_back(s.x); verts.push_back(s.y); verts.push_back(s.z);
            verts.push_back(e.x); verts.push_back(e.y); verts.push_back(e.z);
        }
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
