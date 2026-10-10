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
// Давление — v1.19.0 Physics Logic Fix
// - Cp физичный: (p-p_inf)/q для LBM, Bernoulli для потенциала
// - Сжимаемость: Prandtl-Glauert + Karman-Tsien для Cp
// - RefArea — проекционная фронтальная площадь
// - Трение: Schlichting + Blasius + шероховатость
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

            // Сжимаемость — Karman-Tsien для Cp, точнее чем PG
            if (aeroMachEffects && machInf > 0.3f && machInf < 0.85f) {
                float beta = sqrtf(1.0f - machInf*machInf);
                if (beta < 0.2f) beta = 0.2f;
                // Karman-Tsien: Cp_comp = Cp_incomp / (beta + (M²/(1+beta))*Cp_incomp/2)
                float denom = beta + (machInf*machInf/(1.0f+beta)) * cp * 0.5f;
                if (fabsf(denom) > 1e-6f) cp = cp / denom;
                else cp /= beta;
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
            }
            // v1.19.0 new modes
            else if (aeroVisMode == AeroVisMode::Schlieren) {
                // |∇ρ| via LBM pressure gradient as proxy
                float eps = maxDim*0.02f;
                glm::vec3 pp = p;
                float pXp = 0, pXm = 0, pYp = 0, pYm = 0;
                // sample pressure nearby
                auto getP = [&](glm::vec3 qpos)->float {
                    float fx = (qpos.x - lbmMinX)/lbmCellSizeX, fy = (qpos.y - lbmMinY)/lbmCellSizeY, fz = (qpos.z - lbmMinZ)/lbmCellSizeZ;
                    int ix=(int)fx, iy=(int)fy, iz=(int)fz;
                    if (ix>=0&&ix<lbmNx&&iy>=0&&iy<lbmNy&&iz>=0&&iz<lbmNz) {
                        int cell=(iz*lbmNy+iy)*lbmNx+ix;
                        if (cell>=0&&cell<(int)lbmPressure.size()) return lbmPressure[cell];
                    }
                    return 0.0f;
                };
                pXp=getP(pp+glm::vec3(eps,0,0)); pXm=getP(pp-glm::vec3(eps,0,0));
                pYp=getP(pp+glm::vec3(0,eps,0)); pYm=getP(pp-glm::vec3(0,eps,0));
                float grad = sqrtf((pXp-pXm)*(pXp-pXm)+(pYp-pYm)*(pYp-pYm))/(2*eps*qInf+1e-6f);
                float t = glm::clamp(grad*2.0f*aeroSchlierenSensitivity, 0.0f, 1.0f);
                if (aeroSchlierenColor) {
                    if (t<0.25f) col=glm::vec3(0, t*2.0f, 0.5f+t*2.0f*0.5f);
                    else if (t<0.5f) col=glm::vec3(0, 0.5f+(t-0.25f)*2.0f, 1.0f-(t-0.25f)*0.6f);
                    else if (t<0.75f) col=glm::vec3((t-0.5f)*3.2f, 1.0f, 0.7f-(t-0.5f)*2.8f);
                    else col=glm::vec3(0.8f+(t-0.75f)*0.8f, 1.0f-(t-0.75f)*2.0f, 0.0f);
                } else col=glm::vec3(0.5f+(t-0.5f)*0.8f);
            }
            else if (aeroVisMode == AeroVisMode::Shadowgraph) {
                float eps = maxDim*0.02f;
                auto getP = [&](glm::vec3 qpos)->float {
                    float fx = (qpos.x - lbmMinX)/lbmCellSizeX, fy = (qpos.y - lbmMinY)/lbmCellSizeY, fz = (qpos.z - lbmMinZ)/lbmCellSizeZ;
                    int ix=(int)fx, iy=(int)fy, iz=(int)fz;
                    if (ix>=0&&ix<lbmNx&&iy>=0&&iy<lbmNy&&iz>=0&&iz<lbmNz) {
                        int cell=(iz*lbmNy+iy)*lbmNx+ix;
                        if (cell>=0&&cell<(int)lbmPressure.size()) return lbmPressure[cell];
                    }
                    return 0.0f;
                };
                float pc=getP(p), pxp=getP(p+glm::vec3(eps,0,0)), pxm=getP(p-glm::vec3(eps,0,0)), pyp=getP(p+glm::vec3(0,eps,0)), pym=getP(p-glm::vec3(0,eps,0));
                float lapl = (pxp+pxm+pyp+pym-4*pc)/(eps*eps*qInf+1e-6f);
                float t = glm::clamp(lapl*0.5f*aeroSchlierenSensitivity+0.5f, 0.0f, 1.0f);
                col = glm::vec3(t);
            }
            else if (aeroVisMode == AeroVisMode::ShockWaves) {
                float mach = getLBMMachWorld(p);
                if (mach > 1.05f) {
                    float pr = 1.0f + (mach-1.0f)*2.0f;
                    float t = glm::clamp((pr-1.0f)/4.0f, 0.0f, 1.0f);
                    col = glm::vec3(0.2f+t*0.8f, 0.4f+t*0.3f, 1.0f-t*0.5f);
                    if (t>0.5f) col = glm::vec3(1.0f, 0.7f-(t-0.5f), 0.5f-(t-0.5f));
                } else {
                    float t = mach/1.05f;
                    col = glm::vec3(t*0.3f, t*0.3f, 0.5f+t*0.2f);
                }
            }
            else if (aeroVisMode == AeroVisMode::AeroAcoustic) {
                float vort = getLBMVorticityWorld(p);
                float tke = getLBMTKEWorld(p);
                float acoustic = vort * tke * 0.1f;
                float t = glm::clamp(acoustic*0.2f, 0.0f, 1.0f);
                if (t<0.5f) col=glm::vec3(t, t, 0.5f+t*0.5f);
                else col=glm::vec3(0.5f+(t-0.5f), 0.5f-(t-0.5f)*0.6f, 1.0f-(t-0.5f));
            }
            else if (aeroVisMode == AeroVisMode::Temperature) {
                float mach = getLBMMachWorld(p);
                float T = airTemperature * (1.0f + 0.2f*mach*mach);
                float dT = T - airTemperature;
                float t = glm::clamp(dT/50.0f, 0.0f, 1.0f);
                if (t<0.25f) col=glm::vec3(0, t*0.8f, 0.8f+t*0.2f);
                else if (t<0.5f) col=glm::vec3((t-0.25f)*3.2f, 0.2f+(t-0.25f)*2.0f, 1.0f-(t-0.25f)*2.0f);
                else if (t<0.75f) col=glm::vec3(0.8f+(t-0.5f)*0.8f, 0.7f+(t-0.5f)*0.8f, 0.5f-(t-0.5f)*2.0f);
                else col=glm::vec3(1.0f, 0.9f-(t-0.75f)*1.6f, (t-0.75f)*1.2f);
            }
            else if (aeroVisMode == AeroVisMode::VolumetricSmoke) {
                col = getVelocityMagnitudeColor(speed, vinf*1.6f) * 0.7f + glm::vec3(0.8f,0.8f,0.9f)*0.3f;
            }
            else if (aeroVisMode == AeroVisMode::LIC) {
                col = modelColor;
            }
            else col = getRealisticPressureColor(cp);

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

            if (aeroMachEffects && machInf > 0.3f && machInf < 0.85f) {
                float beta = sqrtf(1.0f - machInf*machInf);
                if (beta < 0.2f) beta = 0.2f;
                float denom = beta + (machInf*machInf/(1.0f+beta))*cp*0.5f;
                if (fabsf(denom) > 1e-6f) cp = cp / denom;
                else cp /= beta;
            }

            glm::vec3 flowDir(vx*invVinf, vy*invVinf, vz*invVinf);
            glm::vec3 r = p - glm::vec3(cx,cy,cz);
            float along = glm::dot(r, flowDir);
            float D = maxDim;
            if (along > D*0.25f) {
                float w = glm::clamp((along - D*0.25f)/D, 0.0f, 1.0f);
                // Давление падает к корме из-за вязкости и отрыва — эмпирика
                cp -= 0.28f * w * w;
            }
            cp = glm::clamp(cp, -3.5f, 1.2f);

            glm::vec3 col;
            if (aeroVisMode == AeroVisMode::Pressure) col = getRealisticPressureColor(cp);
            else if (aeroVisMode == AeroVisMode::VelocityMagnitude) col = getVelocityMagnitudeColor(speed, vinf*1.6f);
            else if (aeroVisMode == AeroVisMode::SkinFriction) { float cf = speed * 0.02f / vinf; float t = glm::clamp(cf*5.0f, 0.0f, 1.0f); col = glm::vec3(t, 1-t, 0.5f); }
            else if (aeroVisMode == AeroVisMode::MachNumber) { float mach = speed / (speedOfSound + 1e-6f); col = getMachColor(mach); }
            else if (aeroVisMode == AeroVisMode::TotalPressure) { float pt = airPressure + 0.5f*airDensity*speed*speed; float ptInf = airPressure + 0.5f*airDensity*vinf*vinf; col = getTotalPressureColor(pt, ptInf); }
            else if (aeroVisMode == AeroVisMode::Schlieren || aeroVisMode == AeroVisMode::Shadowgraph) {
                float eps = maxDim*0.02f;
                glm::vec3 pp(p.x,p.y,p.z);
                glm::vec3 vx = computeVelocityFieldCPU(pp+glm::vec3(eps,0,0), flowParams) - computeVelocityFieldCPU(pp-glm::vec3(eps,0,0), flowParams);
                float grad = glm::length(vx)/(2*eps*vinf+1e-6f);
                float t = glm::clamp(grad*2.0f*aeroSchlierenSensitivity, 0.0f, 1.0f);
                col = glm::vec3(t);
                if (aeroSchlierenColor) col = glm::vec3(t*0.8f, t*0.5f, 0.5f+t*0.5f);
            }
            else if (aeroVisMode == AeroVisMode::ShockWaves) {
                float mach = speed/(speedOfSound+1e-6f);
                col = mach>1.0f ? glm::vec3(1,0.3f,0.2f) : glm::vec3(0.2f,0.3f,0.8f);
            }
            else if (aeroVisMode == AeroVisMode::AeroAcoustic) col = glm::vec3(0.5f,0.5f,0.8f);
            else if (aeroVisMode == AeroVisMode::Temperature) {
                float mach = speed/(speedOfSound+1e-6f);
                float T = airTemperature*(1+0.2f*mach*mach);
                float t = glm::clamp((T-airTemperature)/50.0f,0.0f,1.0f);
                col = glm::vec3(t, 0.5f*t, 1.0f-t);
            }
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
// Lift / Drag — v1.19.0 Physics Logic Fix — полный аудит
// Исправлено по аэродинамике:
// - RefArea: фронтальная проекция, не BB
// - Трение: Schlichting Cf = (0.455/log10(Re)^2.58) для турбулентного, Blasius для ламинарного
// - LE от передней кромки: min dot(p,flowDir)
// - Базовое сопротивление: только для треугольников с нормалью вдоль потока (корма)
// - Индуктивное: AR = span² / refArea, Oswald e=0.8
// - Момент относительно 25% хорды (аэродинамический центр), а не геометрического центра
// - Центр давления: взвешен по давлению, не |Cp|
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
    if (fdLen < 1e-6f || !std::isfinite(fdLen)) { liftMagnitude = 0; dragMagnitude = 0; liftToDragRatio=0; return; }
    flowDir /= fdLen;

    // Sutherland mu
    float T = flowParams.airTemperature;
    const float T0s = 273.15f, S = 110.4f, mu0 = 1.716e-5f;
    float mu = mu0 * powf(T/T0s, 1.5f) * (T0s+S)/(T+S);
    if (!std::isfinite(mu) || mu < 1e-6f) mu = 1.81e-5f;
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
    float pressureForceSum = 0.0f;
    float frontalAreaCalc = 0.0f;
    float baseAreaCalc = 0.0f;
    float baseForceCalc = 0.0f;

    int triCount = (int)(g_vertices.size() / 9);
    float* vertPtr = g_vertices.data();
    float* normPtr = g_normals.data();

    // Найдем переднюю кромку LE для корректного Re_x: min dot(p, flowDir)
    float minAlong = 1e9f, maxAlong = -1e9f;
    for (int ti=0; ti<triCount; ++ti) {
        int i = ti*9;
        if (i+8 >= (int)g_vertices.size()) continue;
        glm::vec3 v0(vertPtr[i], vertPtr[i+1], vertPtr[i+2]);
        float along = glm::dot(v0, flowDir);
        if (along < minAlong) minAlong = along;
        if (along > maxAlong) maxAlong = along;
    }
    if (!std::isfinite(minAlong)) { minAlong = 0; maxAlong = maxDim; }
    float chord = maxAlong - minAlong;
    if (chord < 1e-6f) chord = maxDim;
    glm::vec3 LEPoint = flowDir * minAlong; // точка на LE вдоль flowDir, для xAlong

    // Аэродинамический центр — 25% хорды от LE
    glm::vec3 aeroCenter = flowDir * (minAlong + chord*0.25f) + glm::vec3(flowParams.centerX, flowParams.centerY, flowParams.centerZ) * 0.0f + center;
    // Для простоты: aeroCenter = center - flowDir*0.25*chord + vertical offset? Используем center как приближение, но сместим на 25% назад от LE
    // LE уже в мировых координатах, так что AC = LE + 0.25*chord*flowDir + поперечная составляющая центра
    glm::vec3 centerProjAlong = flowDir * glm::dot(center, flowDir);
    glm::vec3 centerPerp = center - centerProjAlong;
    glm::vec3 ac = flowDir * (minAlong + chord*0.25f) + centerPerp;

    #ifdef _OPENMP
    #pragma omp parallel
    {
        glm::vec3 localForce(0.0f);
        glm::vec3 localCpSum(0.0f);
        glm::vec3 localMoment(0.0f);
        float localArea = 0.0f;
        float localPressSum = 0.0f;
        float localFrontal = 0.0f;
        float localBaseArea = 0.0f;
        float localBaseForce = 0.0f;
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
                float machInf = vinf / (speedOfSound + 1e-6f);
                if (aeroMachEffects && machInf > 0.3f && machInf < 0.85f) {
                    float beta = sqrtf(1.0f - machInf*machInf);
                    if (beta < 0.2f) beta = 0.2f;
                    float denom = beta + (machInf*machInf/(1.0f+beta))*cp*0.5f;
                    if (fabsf(denom) > 1e-6f) cp = cp / denom;
                    else cp /= beta;
                }
            } else {
                glm::vec3 vel = computeVelocityFieldCPU(triCenter, flowParams);
                if (!std::isfinite(vel.x)) vel = glm::vec3(flowParams.vx,flowParams.vy,flowParams.vz);
                float speed2 = glm::dot(vel,vel);
                cp = 1.0f - speed2 / (vinf*vinf);
            }

            cp = glm::clamp(cp, -4.0f, 2.0f);
            if (!std::isfinite(cp)) cp = 0;

            // Давление — сила
            glm::vec3 pressureForce = -cp * q * n * area;

            // Фронтальная площадь — проекция на плоскость перпендикулярную потоку
            float flowDotN = glm::dot(flowDir, n);
            float frontalContrib = area * fabsf(flowDotN);
            // Только ветреная сторона для фронтальной? На самом деле обе, но берем всю
            localFrontal += frontalContrib;

            // Базовая площадь — треугольники где нормаль почти вдоль потока (корма)
            if (flowDotN > 0.7f) { // нормаль по потоку — кормовая часть
                localBaseArea += area;
                localBaseForce += cp * q * area; // давление на корме
            }

            // Трение — Re_x от LE, Schlichting для турбулентного
            float xAlong = glm::dot(triCenter, flowDir) - minAlong;
            if (xAlong < 0.001f) xAlong = 0.001f;
            if (xAlong > chord*1.5f) xAlong = chord*1.5f;
            float Re_x = rho * vinf * xAlong / mu;
            Re_x = glm::clamp(Re_x, 1.0f, 1e9f);
            float Cf;
            if (Re_x < 5e5f) {
                // Laminar Blasius
                Cf = 0.664f / sqrtf(Re_x);
            } else {
                // Turbulent Schlichting: Cf = (0.455 / log10(Re_x)^2.58) - 1700/Re_x
                float logRe = log10f(Re_x);
                if (logRe < 1.0f) logRe = 1.0f;
                Cf = 0.455f / powf(logRe, 2.58f);
                // Коррекция для Re<1e9
                if (Re_x < 1e9f) {
                    Cf -= 1700.0f / Re_x;
                    if (Cf < 0.0005f) Cf = 0.0005f;
                }
            }
            // Шероховатость — 10-20% увеличение
            Cf *= 1.12f;
            Cf = glm::clamp(Cf, 0.0002f, 0.015f);

            float dotFlowNormal = fabsf(flowDotN);
            float frictionFactor = 1.0f - dotFlowNormal; // трение больше где нормаль перпендикулярна потоку
            if (flowDotN > 0) frictionFactor *= 0.55f; // подветренная — меньше из-за отрыва

            float skinFriction = Cf * q * area * frictionFactor;
            glm::vec3 viscousForce = -flowDir * skinFriction;
            glm::vec3 force = pressureForce + viscousForce;

            localForce += force;
            // Центр давления — взвешен по давлению (положительное давление больше вес)
            float pressWeight = fabsf(cp) * area;
            if (cp < 0) pressWeight *= 0.8f; // разрежение чуть меньше вес
            localCpSum += triCenter * pressWeight;
            localPressSum += pressWeight;
            localMoment += glm::cross(triCenter - ac, force);
            localArea += area;
        }
        #pragma omp critical
        {
            totalForce += localForce;
            cpSum += localCpSum;
            momentSum += localMoment;
            areaSum += localArea;
            pressureForceSum += localPressSum;
            frontalAreaCalc += localFrontal;
            baseAreaCalc += localBaseArea;
            baseForceCalc += localBaseForce;
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
            cp = 1.0f - speed2 / (vinf*vinf);
        }

        cp = glm::clamp(cp, -4.0f, 2.0f);
        if (!std::isfinite(cp)) cp = 0;

        glm::vec3 pressureForce = -cp * q * n * area;
        float flowDotN = glm::dot(flowDir, n);
        float frontalContrib = area * fabsf(flowDotN);
        frontalAreaCalc += frontalContrib;
        if (flowDotN > 0.7f) {
            baseAreaCalc += area;
            baseForceCalc += cp * q * area;
        }

        float xAlong = glm::dot(triCenter, flowDir) - minAlong;
        if (xAlong < 0.001f) xAlong = 0.001f;
        if (xAlong > chord*1.5f) xAlong = chord*1.5f;
        float Re_x = rho * vinf * xAlong / mu;
        Re_x = glm::clamp(Re_x, 1.0f, 1e9f);
        float Cf;
        if (Re_x < 5e5f) Cf = 0.664f / sqrtf(Re_x);
        else {
            float logRe = log10f(Re_x);
            if (logRe < 1.0f) logRe = 1.0f;
            Cf = 0.455f / powf(logRe, 2.58f);
            if (Re_x < 1e9f) { Cf -= 1700.0f / Re_x; if (Cf < 0.0005f) Cf = 0.0005f; }
        }
        Cf *= 1.12f;
        Cf = glm::clamp(Cf, 0.0002f, 0.015f);
        float dotFlowNormal = fabsf(flowDotN);
        float frictionFactor = 1.0f - dotFlowNormal;
        if (flowDotN > 0) frictionFactor *= 0.55f;
        float skinFriction = Cf * q * area * frictionFactor;
        glm::vec3 viscousForce = -flowDir * skinFriction;
        glm::vec3 force = pressureForce + viscousForce;

        totalForce += force;
        float pressWeight = fabsf(cp) * area;
        if (cp < 0) pressWeight *= 0.8f;
        cpSum += triCenter * pressWeight;
        pressureForceSum += pressWeight;
        momentSum += glm::cross(triCenter - ac, force);
        areaSum += area;
    }
    #endif

    if (!std::isfinite(totalForce.x)) totalForce = glm::vec3(0);

    // Если autoRefArea — используем фронтальную площадь, но не менее 0.5*refArea из BB
    if (aeroAutoRefArea && frontalAreaCalc > 1e-6f) {
        // frontalAreaCalc — это сумма area*|dot|, что уже фронтальная
        // Но для замкнутого тела она вдвое больше (перед+зад), делим на 2
        float frontal = frontalAreaCalc * 0.5f;
        if (frontal > 1e-6f) aeroRefArea = frontal;
    }

    // Базовое сопротивление — только если есть явная кормовая площадь и давление на ней не учтено полностью
    // В нашем интегрировании давление на корме уже учтено, так что baseDrag = 0 для замкнутого тела
    // Но для открытой кормы (как у авто) добавляем коррекцию: Cp_base ~ -0.12..-0.2
    float baseDrag = 0.0f;
    if (baseAreaCalc > 1e-6f) {
        // Если Cp на корме уже отрицательный, он уже дает drag, не добавляем
        // Если же модель открытая и Cp ~0, добавляем эмпирическое
        float avgCpBase = baseForceCalc / (q * baseAreaCalc + 1e-9f);
        if (avgCpBase > -0.05f) { // почти 0 — открытая корма
            float CpBase = -0.14f; // типичный
            // Коррекция по Re и удлинению
            if (aeroReNumber > 1e6f) CpBase = -0.12f;
            baseDrag = -CpBase * q * baseAreaCalc * 0.6f; // 60% от base area — эффективное
        }
    }

    dragMagnitude = glm::dot(totalForce, flowDir) + baseDrag;
    if (!std::isfinite(dragMagnitude)) dragMagnitude = baseDrag;
    dragVector = flowDir * dragMagnitude;

    // Подъемная — перпендикулярно потоку, в плоскости flow + up
    glm::vec3 up(0,1,0);
    glm::vec3 liftDir = up - flowDir * glm::dot(up, flowDir);
    float liftDirLen = glm::length(liftDir);
    if (liftDirLen < 1e-6f || !std::isfinite(liftDirLen)) {
        // Если поток вертикальный, используем Z как up
        up = glm::vec3(0,0,1);
        liftDir = up - flowDir * glm::dot(up, flowDir);
        liftDirLen = glm::length(liftDir);
    }
    if (liftDirLen < 1e-6f || !std::isfinite(liftDirLen)) {
        liftMagnitude = 0.0f;
        liftVector = glm::vec3(0.0f);
    } else {
        liftDir /= liftDirLen;
        liftMagnitude = glm::dot(totalForce, liftDir);
        if (!std::isfinite(liftMagnitude)) liftMagnitude = 0;
        liftVector = liftDir * liftMagnitude;
        // Ground effect для подъемной: при близости к земле downforce увеличивается
        if (aeroGroundEffect) {
            float groundY = g_voxMinY + aeroGroundHeight;
            float h = center.y - groundY;
            if (h > 0.01f && h < maxDim*1.0f) {
                float geFactor = 1.0f + 0.35f * expf(-h/(maxDim*0.3f));
                // Для авто downforce отрицательный lift, увеличиваем по модулю
                if (liftMagnitude < 0) liftMagnitude *= geFactor;
            }
        }
    }

    // Индуктивное сопротивление: Cd_i = Cl^2 / (pi * AR * e), e=Oswald ~0.8
    float AR = 1.0f;
    float span = maxBB.z - minBB.z;
    float chordEst = maxBB.x - minBB.x;
    if (span > 1e-6f && refArea > 1e-6f) {
        // AR = span² / refArea — корректная формула
        AR = (span*span) / refArea;
    } else if (span > 1e-6f && chordEst > 1e-6f) {
        AR = span / chordEst;
    }
    AR = glm::clamp(AR, 0.3f, 20.0f);
    float eOswald = 0.8f; // для авто ~0.7, для крыла ~0.85
    if (AR < 1.5f) eOswald = 0.65f;
    float Cl = liftMagnitude / (q * refArea + 1e-9f);
    Cl = glm::clamp(Cl, -3.0f, 3.0f);
    float Cd_i = Cl*Cl / (3.14159265f * AR * eOswald + 1e-6f);
    float inducedDrag = Cd_i * q * refArea;
    // Индуктивное только если есть подъемная
    if (fabsf(Cl) > 0.05f) dragMagnitude += inducedDrag;

    // Волновое сопротивление при M>0.7
    float machInf = vinf / (speedOfSound + 1e-6f);
    if (aeroMachEffects && machInf > 0.7f) {
        float Cd_wave = 0.0f;
        if (machInf < 0.9f) Cd_wave = 20.0f * powf(machInf - 0.7f, 4.0f); // резкий рост
        else Cd_wave = 0.1f + 0.2f*(machInf-0.9f);
        Cd_wave = glm::clamp(Cd_wave, 0.0f, 0.5f);
        dragMagnitude += Cd_wave * q * refArea;
    }

    if (fabsf(dragMagnitude) > 1e-9f) liftToDragRatio = liftMagnitude / dragMagnitude;
    else liftToDragRatio = 0;

    momentVector = momentSum;
    momentMagnitude = glm::length(momentSum);
    if (!std::isfinite(momentMagnitude)) { momentMagnitude = 0; momentVector = glm::vec3(0); }

    // Центр давления — взвешен по давлению
    if (pressureForceSum > 1e-9f && std::isfinite(cpSum.x)) centerOfPressure = cpSum / pressureForceSum;
    else if (areaSum > 1e-9f) centerOfPressure = cpSum / areaSum; // fallback
    else centerOfPressure = ac;
    if (!std::isfinite(centerOfPressure.x)) centerOfPressure = ac;
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
