#include <glad/glad.h>
#include <glm/glm.hpp>

#include <cmath>
#include <vector>

#include "globals.h"
#include "cuda_api.h"
#include "flow_field.h"
#include "voxel_grid.h"
#include "forces.h"

// =====================================================
// Давление
// =====================================================
void updateVertexColors() {
    updateFlowParams();
    int numVerts = (int)(g_vertices.size() / 3);
    if (numVerts == 0) return;

    if (useCUDA == 1) {
        computeVertexPressureCUDA(g_vertices, g_normals, g_vertexColors, numVerts, flowParams);
    } else {
        g_vertexColors.resize(numVerts*3);
        float vinf = sqrtf(flowParams.vx*flowParams.vx + flowParams.vy*flowParams.vy + flowParams.vz*flowParams.vz);
        if (vinf < 1e-4f) vinf = 1e-4f;
        for (int i = 0; i < numVerts; i++) {
            glm::vec3 p(g_vertices[3*i], g_vertices[3*i+1], g_vertices[3*i+2]);
            glm::vec3 v = computeVelocityFieldCPU(p, flowParams);
            float speed = glm::length(v);
            float speedRatio = speed / vinf;
            float cp = 1.0f - speedRatio*speedRatio;

            float rx = p.x - flowParams.centerX;
            float ry = p.y - flowParams.centerY;
            float rz = p.z - flowParams.centerZ;
            float fl = 1.0f / vinf;
            float along = rx*(flowParams.vx*fl) + ry*(flowParams.vy*fl) + rz*(flowParams.vz*fl);
            float D = 2.0f * fmaxf(flowParams.radiusY, flowParams.radiusZ);
            if (along > D * 0.3f) {
                float w = (along - D*0.3f) / D;
                if (w > 1.0f) w = 1.0f;
                cp -= 0.8f * w * w;
            }
            if (cp > 1.0f)  { cp = 1.0f; }
            if (cp < -3.0f) { cp = -3.0f; }

            float t = (cp+1.0f)/2.0f;
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
// Lift / Drag
// =====================================================
void computeLiftDrag() {
    updateFlowParams();
    int numVerts = (int)(g_vertices.size() / 3);
    if (numVerts == 0) return;

    float vinf = sqrtf(flowParams.vx*flowParams.vx + flowParams.vy*flowParams.vy + flowParams.vz*flowParams.vz);
    if (vinf < 1e-4f) vinf = 1e-4f;

    glm::vec3 flowDir(flowParams.vx, flowParams.vy, flowParams.vz);
    flowDir /= vinf;

    glm::vec3 totalForce(0.0f);
    glm::vec3 cpSum(0.0f);
    float areaSum = 0.0f;

    for (size_t i = 0; i + 8 < g_vertices.size(); i += 9) {
        glm::vec3 v0(g_vertices[i],   g_vertices[i+1], g_vertices[i+2]);
        glm::vec3 v1(g_vertices[i+3], g_vertices[i+4], g_vertices[i+5]);
        glm::vec3 v2(g_vertices[i+6], g_vertices[i+7], g_vertices[i+8]);
        glm::vec3 n (g_normals[i],    g_normals[i+1],  g_normals[i+2]);

        glm::vec3 triCenter = (v0+v1+v2) / 3.0f;
        glm::vec3 cr = glm::cross(v1-v0, v2-v0);
        float area = 0.5f * glm::length(cr);
        if (area < 1e-9f) continue;

        glm::vec3 vel = computeVelocityFieldCPU(triCenter, flowParams);
        float speed = glm::length(vel);
        float cp = 1.0f - (speed*speed)/(vinf*vinf);

        glm::vec3 force = -cp * n * area;
        totalForce += force;
        cpSum += triCenter * (cp * area);
        areaSum += area;
    }

    dragMagnitude = glm::dot(totalForce, flowDir);
    dragVector = flowDir * dragMagnitude;

    glm::vec3 liftDir = glm::vec3(0,1,0) - flowDir * glm::dot(glm::vec3(0,1,0), flowDir);
    if (glm::length(liftDir) > 1e-6f) {
        liftDir = glm::normalize(liftDir);
        liftMagnitude = glm::dot(totalForce, liftDir);
        liftVector = liftDir * liftMagnitude;
    } else {
        liftMagnitude = 0.0f;
        liftVector = glm::vec3(0.0f);
    }

    if (areaSum > 1e-9f) centerOfPressure = cpSum / areaSum;
    liftDragDirty = true;
}

void updateLiftDragArrows() {
    if (!liftDragDirty) return;
    liftDragDirty = false;

    float dragScale = 0.5f / (maxDim + 1e-6f);
    float liftScale = 0.5f / (maxDim + 1e-6f);

    std::vector<float> verts;
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
