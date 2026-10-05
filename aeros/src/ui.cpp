#include <imgui.h>

#include <string>
#include <cstdio>
#include <cmath>
#include <algorithm>
#include <chrono>
#include <sstream>

#include "globals.h"
#include "stl_loader.h"
#include "model.h"
#include "particles.h"
#include "streamlines.h"
#include "voxel_grid.h"
#include "atmosphere.h"
#include "test_mode.h"
#include "lbm.h"
#include "forces.h"
#include "ui.h"

// =====================================================
// Панель управления — v1.9.0 Ultra Realistic+
// =====================================================
void drawUI() {
ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_Once);
ImGui::SetNextWindowSize(ImVec2(520, 1100), ImGuiCond_Once);
ImGui::Begin("Aeros Control v1.9.0 Ultra Realistic+");

ImGui::Text("FPS: %.1f | Frame: %.2f ms | v1.9.0", deltaTime > 1e-6f ? 1.0f/deltaTime : 0.0f, perfFrameMs);
ImGui::Text("Vertices: %d | Tris: %d | Voxel: %dx%dx%d", modelVertexCount, modelVertexCount/3, g_voxNx, g_voxNy, g_voxNz);
ImGui::Text("LBM: %dx%dx%d=%d | OpenMP: %d | %s", lbmNx, lbmNy, lbmNz, lbmNx*lbmNy*lbmNz, perfOpenMPThreads,
#ifdef _OPENMP
    "ON"
#else
    "OFF"
#endif
);
float machCurrent = flowSpeed/(speedOfSound+1e-6f);
ImGui::Text("Backend: %s | Re: %.0f | Mach: %.3f | Mem: %.1f MB",
    lbmParams.enabled ? "LBM Realistic" : "Potential+Wake",
    aeroReNumber, machCurrent,
    (g_vertices.size()*4 + g_distanceField.size()*4 + particlePositions.size()*4)/1024.0f/1024.0f);
if (!aeroLastScreenshotPath.empty()) ImGui::Text("Last Screenshot: %s", aeroLastScreenshotPath.c_str());
if (!aeroLastCSVPath.empty()) ImGui::Text("Last CSV: %s", aeroLastCSVPath.c_str());
ImGui::Separator();

if (ImGui::CollapsingHeader("Performance v1.9.0", ImGuiTreeNodeFlags_DefaultOpen)) {
    ImGui::Text("Frame: %.2f ms (%.1f FPS) | LBM: %.2f ms", perfFrameMs, perfFrameMs>1e-3f?1000.0f/perfFrameMs:0, perfLBMms);
    ImGui::Text("Particles: %.2f ms | Forces: %.2f ms | Streamlines: %.2f ms", perfParticlesMs, perfForcesMs, perfStreamlinesMs);
    ImGui::Text("Voxel: %.2f ms", perfVoxelMs);
    if (lbmInitialized) {
        int total = lbmNx*lbmNy*lbmNz;
        float mlups = (total * lbmParams.stepsPerFrame) / (lbmTimeMs > 0 ? lbmTimeMs : 1.0f) / 1000.0f;
        ImGui::Text("LBM: %d cells | %.2f MLUPS | %.1f ms/step | TKE %.4f", total, mlups, lbmTimeMs, lbmTKE);
        ImGui::Text("Re: %.0f | Conv: %.2e | Steps: %d | %s", lbmReynolds, lbmConvergence, lbmCurrentStep, lbmConverged?"YES":"NO");
        float maxP = lbmPressure.empty()?0:*std::max_element(lbmPressure.begin(), lbmPressure.end());
        float minP = lbmPressure.empty()?0:*std::min_element(lbmPressure.begin(), lbmPressure.end());
        ImGui::Text("Pressure: [%.2f, %.2f] | MaxVel LB %.3f World %.2f m/s", minP, maxP, lbmMaxVelocityLB, lbmMaxVelocityWorld);
    }
    ImGui::Checkbox("Show Perf Graph", &aeroShowPerfGraph);
    ImGui::Checkbox("Show Memory Usage", &aeroShowMemoryUsage);
    if (aeroShowPerfGraph) {
        static float frameHistory[100] = {0};
        static int histIdx = 0;
        frameHistory[histIdx] = perfFrameMs;
        histIdx = (histIdx+1)%100;
        char overlay[64];
        snprintf(overlay, sizeof(overlay), "Frame %.1f ms", perfFrameMs);
        ImGui::PlotLines("Frame Time", frameHistory, 100, histIdx, overlay, 0, 100, ImVec2(0,60));
    }
    ImGui::Separator();
    ImGui::Text("Features v1.9.0:");
    ImGui::BulletText("Mach + Helicity + Total Pressure visualization");
    ImGui::BulletText("Screenshot BMP + CSV export + settings save/load");
    ImGui::BulletText("Adaptive LBM + RK4 particles + surface streamlines");
    ImGui::BulletText("Improved ground + PBR + MSAA + perf graph");
}

if (ImGui::CollapsingHeader("Realistic Aero v1.9.0 — Ultra Photo Mode", ImGuiTreeNodeFlags_DefaultOpen)) {
    const char* visModes[] = {
        "Pressure Cp (NASCAR rainbow)",
        "Velocity |U| (UAV/Cybertruck)",
        "Vorticity |w|",
        "Q-Criterion (vortices)",
        "Turbulent TKE",
        "Skin Friction Cf",
        "Boundary Layer",
        "Mach Number (NEW v1.9.0)",
        "Helicity (NEW v1.9.0)",
        "Total Pressure (NEW v1.9.0)"
    };
    int visIdx = (int)aeroVisMode;
    if (ImGui::Combo("Visualization", &visIdx, visModes, IM_ARRAYSIZE(visModes))) {
        aeroVisMode = (AeroVisMode)visIdx;
        if (showPressure) updateVertexColors();
    }
    const char* cmapModes[] = { "Rainbow (NASCAR/UAV)", "Viridis", "Parula", "CoolWarm" };
    if (ImGui::Combo("Color Map", &aeroColorMap, cmapModes, IM_ARRAYSIZE(cmapModes))) {
        if (showPressure) updateVertexColors();
        computeStreamlines();
    }
    ImGui::Checkbox("Color Streamlines by Velocity (photo 2)", &aeroColorStreamlinesByVelocity);
    ImGui::Checkbox("Highlight Flow Separation", &aeroShowSeparation);
    ImGui::Checkbox("Show Wake (photo 5)", &aeroShowWake);
    if (aeroShowWake) ImGui::SliderFloat("Wake Opacity", &aeroWakeOpacity, 0.0f, 1.0f);
    ImGui::Checkbox("Show Vortices (Q)", &aeroShowVortices);
    ImGui::Checkbox("Show Boundary Layer", &aeroShowBoundaryLayer);
    ImGui::Checkbox("Realistic PBR Lighting", &aeroUseRealisticLighting);
    ImGui::Checkbox("Ground Effect (cars, photo 2/5)", &aeroGroundEffect);
    if (aeroGroundEffect) {
        ImGui::SliderFloat("Ground Height", &aeroGroundHeight, -2.0f, 2.0f, "%.2f");
        lbmParams.useGround = aeroGroundEffect;
        lbmParams.groundHeight = aeroGroundHeight;
    }
    ImGui::Checkbox("Show Ground Plane", &showGroundPlane);
    if (showGroundPlane) {
        ImGui::SliderFloat("Ground Alpha", &groundAlpha, 0.1f, 1.0f);
        ImGui::ColorEdit3("Ground Color", &groundColor[0]);
    }
    ImGui::Checkbox("Show Slice Plane (Cybertruck)", &showSlicePlane);
    if (showSlicePlane) {
        ImGui::Checkbox("Enable Aero Slice", &aeroShowSlice);
        const char* axisNames[] = {"X","Y","Z"};
        ImGui::Combo("Slice Axis", &aeroSliceAxis, axisNames, 3);
        ImGui::SliderFloat("Slice Pos", &aeroSlicePos, 0.0f, 1.0f);
    }
    ImGui::Checkbox("Mach Effects (compressibility)", &aeroMachEffects);
    if (aeroMachEffects) {
        ImGui::SliderFloat("Mach Threshold", &aeroMachThreshold, 0.1f, 1.0f);
    }
    ImGui::Separator();
    ImGui::Text("v1.9.0 New Features:");
    ImGui::Checkbox("Particle Trails", &aeroShowParticleTrails);
    if (aeroShowParticleTrails) {
        ImGui::SliderFloat("Trail Length", &aeroTrailLength, 0.1f, 2.0f);
        ImGui::SliderFloat("Trail Opacity", &aeroParticleTrailOpacity, 0.1f, 1.0f);
    }
    ImGui::Checkbox("Surface Streamlines", &aeroSurfaceStreamlines);
    ImGui::Checkbox("Use RK4 for Particles (accurate)", &aeroUseRK4Particles);
    ImGui::Checkbox("Adaptive LBM Steps", &aeroAdaptiveLBM);
    ImGui::Checkbox("Show Helicity", &aeroShowHelicity);
    ImGui::Checkbox("Show Total Pressure", &aeroShowTotalPressure);
    ImGui::Separator();
    ImGui::Text("Ref Area for Cd/Cl:");
    ImGui::Checkbox("Auto Ref Area", &aeroAutoRefArea);
    if (!aeroAutoRefArea) ImGui::SliderFloat("Ref Area", &aeroRefArea, 0.01f, 20.0f, "%.3f m2");
    else ImGui::Text("Auto: %.4f m2 (from BB) | Re=%.0f", aeroRefArea, aeroReNumber);
    ImGui::Separator();
    ImGui::Text("Presets (like photos):");
    if (ImGui::Button("Car Preset (NASCAR/Cybertruck)")) {
        aeroVisMode = AeroVisMode::Pressure;
        aeroGroundEffect = true;
        aeroGroundHeight = 0.0f;
        aeroColorStreamlinesByVelocity = true;
        aeroShowSeparation = true;
        aeroShowWake = true;
        showGroundPlane = true;
        aeroColorMap = 0;
        lbmParams.enabled = true;
        lbmParams.useGround = true;
        lbmParams.stepsPerFrame = 5;
        lbmParams.tau = 0.55f;
        lbmParams.useTurbulence = true;
        lbmParams.useZouHeBC = true;
        lbmParams.useConvectiveOutlet = true;
        lbmParams.inletTurbulence = 0.02f;
        aeroAdaptiveLBM = true;
        if (lbmInitialized) resetLBM(); else initLBM();
        updateVertexColors(); computeStreamlines();
    }
    ImGui::SameLine();
    if (ImGui::Button("UAV Preset (photo 1)")) {
        aeroVisMode = AeroVisMode::VelocityMagnitude;
        aeroGroundEffect = false;
        aeroColorStreamlinesByVelocity = true;
        aeroShowWake = true;
        aeroColorMap = 0;
        lbmParams.enabled = true;
        lbmParams.stepsPerFrame = 3;
        lbmParams.tau = 0.6f;
        lbmParams.useGround = false;
        if (lbmInitialized) resetLBM(); else initLBM();
        updateVertexColors(); computeStreamlines();
    }
    ImGui::SameLine();
    if (ImGui::Button("Airfoil Preset (photo 3)")) {
        aeroVisMode = AeroVisMode::Pressure;
        aeroGroundEffect = false;
        aeroColorStreamlinesByVelocity = false;
        aeroShowWake = true;
        aeroColorMap = 0;
        lbmParams.enabled = true;
        lbmParams.stepsPerFrame = 4;
        lbmParams.useGround = false;
        if (lbmInitialized) resetLBM(); else initLBM();
        updateVertexColors(); computeStreamlines();
    }
    if (ImGui::Button("Turbulent Wake (photo 5)")) {
        aeroVisMode = AeroVisMode::Vorticity;
        aeroColorMap = 0;
        aeroShowWake = true;
        aeroShowVortices = true;
        wakeStrength = 1.0f;
        wakeLength = 15.0f;
        lbmParams.enabled = true;
        lbmParams.useTurbulence = true;
        lbmParams.smagorinskyC = 0.15f;
        if (lbmInitialized) resetLBM(); else initLBM();
        updateVertexColors(); computeStreamlines();
    }
    ImGui::SameLine();
    if (ImGui::Button("Mach Preset (NEW)")) {
        aeroVisMode = AeroVisMode::MachNumber;
        aeroColorMap = 0;
        aeroMachEffects = true;
        aeroShowMach = true;
        flowSpeed = 100.0f;
        updateVertexColors(); computeStreamlines();
    }
    ImGui::SameLine();
    if (ImGui::Button("Helicity Preset (NEW)")) {
        aeroVisMode = AeroVisMode::Helicity;
        aeroColorMap = 0;
        aeroShowHelicity = true;
        lbmParams.enabled = true;
        if (lbmInitialized) resetLBM(); else initLBM();
        updateVertexColors(); computeStreamlines();
    }
    if (ImGui::Button("Viridis Style")) {
        aeroColorMap = 1;
        updateVertexColors(); computeStreamlines();
    }
    ImGui::SameLine();
    if (ImGui::Button("Enable Realistic LBM")) {
        lbmParams.enabled = true;
        lbmParams.useZouHeBC = true;
        lbmParams.useConvectiveOutlet = true;
        lbmParams.useTurbulence = true;
        lbmParams.inletTurbulence = 0.02f;
        lbmParams.useRegularized = false;
        aeroAdaptiveLBM = true;
        if (!lbmInitialized) initLBM(); else resetLBM();
    }
    ImGui::SameLine();
    if (ImGui::Button("Disable LBM")) {
        lbmParams.enabled = false;
        shutdownLBM();
    }
}

if (ImGui::CollapsingHeader("Flow", ImGuiTreeNodeFlags_DefaultOpen)) {
    const char* unitItems[] = { "m/s (м/с)", "km/h (км/ч)", "mph (миль/ч)", "kts (узлы)", "ft/s (фут/с)" };
    int unitIdx = (int)speedUnit;
    if (ImGui::Combo("Speed Unit", &unitIdx, unitItems, IM_ARRAYSIZE(unitItems))) {
        speedUnit = (SpeedUnit)unitIdx;
    }
    float displaySpeed = speedFromMS(flowSpeed, speedUnit);
    float maxDisplay = 10.0f;
    switch (speedUnit) {
        case SPEED_MS: maxDisplay = 30.0f; break;
        case SPEED_KMH: maxDisplay = 108.0f; break;
        case SPEED_MPH: maxDisplay = 67.0f; break;
        case SPEED_KNOTS: maxDisplay = 58.0f; break;
        case SPEED_FTS: maxDisplay = 98.0f; break;
        default: break;
    }
    if (ImGui::SliderFloat("##speed", &displaySpeed, 0.0f, maxDisplay, "%.2f")) {
        flowSpeed = speedToMS(displaySpeed, speedUnit);
    }
    ImGui::SameLine(); ImGui::SetNextItemWidth(80);
    if (ImGui::InputFloat("##spd", &displaySpeed, 0, 0, "%.2f")) {
        if (displaySpeed < 0) displaySpeed = 0;
        if (displaySpeed > maxDisplay) displaySpeed = maxDisplay;
        flowSpeed = speedToMS(displaySpeed, speedUnit);
    }
    ImGui::SameLine();
    ImGui::Text("%s", speedUnitShort(speedUnit));
    ImGui::Text("  = %.2f m/s | %.1f km/h | %.1f mph | %.1f kts | %.1f ft/s",
        flowSpeed,
        speedFromMS(flowSpeed, SPEED_KMH),
        speedFromMS(flowSpeed, SPEED_MPH),
        speedFromMS(flowSpeed, SPEED_KNOTS),
        speedFromMS(flowSpeed, SPEED_FTS));
    ImGui::SliderFloat("Azimuth", &flowAzimuth, 0.0f, 360.0f);
    ImGui::SameLine(); ImGui::SetNextItemWidth(80);
    if (ImGui::InputFloat("##az", &flowAzimuth, 0, 0, "%.1f")) {
        while (flowAzimuth < 0) flowAzimuth += 360.0f;
        while (flowAzimuth >= 360.0f) flowAzimuth -= 360.0f;
    }
    ImGui::SliderFloat("Elevation", &flowElevation, -90.0f, 90.0f);
    ImGui::SameLine(); ImGui::SetNextItemWidth(80);
    ImGui::InputFloat("##el", &flowElevation, 0, 0, "%.1f");
    ImGui::SliderFloat("Time Scale", &timeScale, 0.01f, 3.0f, "%.2f");
    ImGui::SliderFloat("Strouhal", &strouhal, 0.05f, 0.5f, "%.3f");
    ImGui::SliderFloat("Wake Strength", &wakeStrength, 0.0f, 2.0f);
    ImGui::SliderFloat("Wake Length", &wakeLength, 2.0f, 30.0f);
    ImGui::Checkbox("Auto Rotate (showcase)", &autoRotate);
    if (autoRotate) ImGui::SliderFloat("Rotate Speed", &aeroAutoRotateSpeed, 1.0f, 50.0f, "%.1f deg/s");
}

if (ImGui::CollapsingHeader("Atmosphere (ISA)", ImGuiTreeNodeFlags_DefaultOpen)) {
    ImGui::Checkbox("Use Real Air Density", &useRealDensity);
    ImGui::Text("Altitude affects drag/lift via rho");
    ImGui::SliderFloat("Altitude (m)", &altitude, 0.0f, 20000.0f, "%.0f m");
    ImGui::SameLine(); ImGui::SetNextItemWidth(80);
    if (ImGui::InputFloat("##alt", &altitude, 0, 0, "%.0f")) {
        if (altitude < 0) altitude = 0;
        if (altitude > 80000) altitude = 80000;
    }
    if (ImGui::Button("Sea Level")) altitude = 0.0f;
    ImGui::SameLine(); if (ImGui::Button("5 km")) altitude = 5000.0f;
    ImGui::SameLine(); if (ImGui::Button("10 km")) altitude = 10000.0f;
    ImGui::SameLine(); if (ImGui::Button("15 km")) altitude = 15000.0f;
    ImGui::Separator();
    ImGui::Text("Air Density: %.4f kg/m3 | Re: %.0f", airDensity, aeroReNumber);
    ImGui::Text("Pressure: %.0f Pa (%.2f atm)", airPressure, airPressure/101325.0f);
    ImGui::Text("Temperature: %.1f K (%.1f C)", airTemperature, airTemperature-273.15f);
    ImGui::Text("Speed of Sound: %.1f m/s", speedOfSound);
    float mach = (speedOfSound > 1e-3f) ? flowSpeed / speedOfSound : 0.0f;
    ImGui::Text("Mach: %.3f %s", mach, mach>0.8f?"(compressible!)":mach>0.3f?"(subsonic)":"(incompressible)");
    if (ImGui::TreeNode("Density Table")) {
        ImGui::Text(" Alt (m) | rho (kg/m3) | P (hPa) | T (C) | a (m/s)");
        ImGui::Separator();
        int alts[] = {0, 1000, 2000, 3000, 5000, 8000, 10000, 12000, 15000, 20000};
        for (int h : alts) {
            AtmosphereParams atm = calculateAtmosphere((float)h);
            ImGui::Text("%7d | %8.4f | %7.1f | %6.1f | %5.0f %s",
                h, atm.density, atm.pressure/100.0f, atm.temperature-273.15f, atm.speedOfSound,
                (fabsf((float)h - altitude) < 10.0f) ? "<--" : "");
        }
        ImGui::TreePop();
    }
    float q = 0.5f * airDensity * flowSpeed * flowSpeed;
    ImGui::Text("Dynamic Pressure q: %.1f Pa | q*RefArea: %.1f N", q, q*aeroRefArea);
}

if (ImGui::CollapsingHeader("Particles v1.9.0", ImGuiTreeNodeFlags_DefaultOpen)) {
    ImGui::Checkbox("Show Particles", &showParticles);
    ImGui::SliderInt("Count", &numParticles, 100, 200000);
    if (ImGui::IsItemDeactivatedAfterEdit()) initParticles();
    ImGui::SliderFloat("Size", &particleSize, 1.0f, 8.0f);
    ImGui::SliderFloat("Max Speed Color", &maxSpeedForColor, 0.5f, 20.0f);
    ImGui::Checkbox("Particle Trails (NEW)", &aeroShowParticleTrails);
    ImGui::Checkbox("RK4 Advection (NEW, accurate)", &aeroUseRK4Particles);
    if (ImGui::Button("Reset Particles")) initParticles();
    ImGui::SameLine(); if (ImGui::Button("5000")) { numParticles=5000; initParticles(); }
    ImGui::SameLine(); if (ImGui::Button("15000")) { numParticles=15000; initParticles(); }
    ImGui::SameLine(); if (ImGui::Button("50000")) { numParticles=50000; initParticles(); }
}

if (ImGui::CollapsingHeader("Streamlines v1.9.0", ImGuiTreeNodeFlags_DefaultOpen)) {
    ImGui::Checkbox("Show Streamlines", &showStreamlines);
    ImGui::SliderInt("Count##sl", &numStreamlines, 4, 200);
    ImGui::SliderInt("Steps", &streamlineSteps, 20, 1000);
    ImGui::SliderFloat("Step Size", &streamlineStepSize, 0.01f, 0.5f);
    ImGui::SliderFloat("Line Width", &streamlineWidth, 1.0f, 5.0f);
    ImGui::SliderFloat("Alpha", &streamlineAlpha, 0.1f, 1.0f);
    ImGui::Checkbox("Surface Seeding (NEW)", &aeroSurfaceStreamlines);
    if (ImGui::Button("Rebuild Streamlines")) computeStreamlines();
    ImGui::SameLine(); if (ImGui::Button("Low (12)")) { numStreamlines=12; computeStreamlines(); }
    ImGui::SameLine(); if (ImGui::Button("Med (24)")) { numStreamlines=24; computeStreamlines(); }
    ImGui::SameLine(); if (ImGui::Button("High (64)")) { numStreamlines=64; computeStreamlines(); }
}

if (ImGui::CollapsingHeader("Pressure & Forces v1.9.0 Realistic+", ImGuiTreeNodeFlags_DefaultOpen)) {
    ImGui::Checkbox("Show Pressure Colors", &showPressure);
    ImGui::Checkbox("Show Lift/Drag Vectors", &showLiftDrag);
    ImGui::Checkbox("Show Color Legend", &showColorLegend);
    float q = 0.5f * airDensity * flowSpeed * flowSpeed;
    float Cd = 0, Cl = 0;
    if (q > 1e-6f && aeroRefArea > 1e-6f) {
        Cd = dragMagnitude / (q * aeroRefArea);
        Cl = liftMagnitude / (q * aeroRefArea);
    }
    ImGui::Text("Drag:  %.3f N | Cd: %.4f", dragMagnitude, Cd);
    ImGui::Text("Lift:  %.3f N | Cl: %.4f | L/D: %.2f", liftMagnitude, Cl, liftToDragRatio);
    ImGui::Text("Moment: %.3f Nm | CoP: (%.2f, %.2f, %.2f)", momentMagnitude, centerOfPressure.x, centerOfPressure.y, centerOfPressure.z);
    ImGui::Text("Ref Area: %.4f m2 | q: %.1f Pa | Re: %.0f", aeroRefArea, q, aeroReNumber);
    if (lbmInitialized) {
        float minP = lbmPressure.empty()?0:*std::min_element(lbmPressure.begin(), lbmPressure.end());
        float maxP = lbmPressure.empty()?0:*std::max_element(lbmPressure.begin(), lbmPressure.end());
        ImGui::Text("LBM Pressure: [%.2f, %.2f] | TKE avg %.4f", minP, maxP, lbmTKE);
    }
    ImGui::Separator();
    if (ImGui::Button("Export Forces CSV (F6)")) aeroCSVExportRequested = true;
    if (ImGui::Button("Screenshot BMP (F5)")) aeroScreenshotRequested = true;
    if (!aeroLastCSVPath.empty()) ImGui::Text("CSV: %s", aeroLastCSVPath.c_str());
    if (!aeroLastScreenshotPath.empty()) ImGui::Text("BMP: %s", aeroLastScreenshotPath.c_str());
    if (showColorLegend && showPressure) {
        ImGui::Separator();
        ImGui::Text("Color Legend:");
        if (aeroVisMode == AeroVisMode::Pressure) ImGui::Text("Cp: -3.0 (blue, suction) -> 0 (yellow) -> +1.0 (red, stagnation)");
        else if (aeroVisMode == AeroVisMode::VelocityMagnitude) ImGui::Text("|U|: 0 (blue, low) -> %.1f m/s (red, high)", maxSpeedForColor);
        else if (aeroVisMode == AeroVisMode::Vorticity) ImGui::Text("|w|: 0 (blue) -> 10 (white) -> 20+ (red)");
        else if (aeroVisMode == AeroVisMode::QCriterion) ImGui::Text("Q: <0 (blue, strain) | >0 (red/yellow, vortex)");
        else if (aeroVisMode == AeroVisMode::TurbulentKE) ImGui::Text("TKE: 0 (dark) -> high (purple/yellow)");
        else if (aeroVisMode == AeroVisMode::MachNumber) ImGui::Text("Mach: 0 (blue) -> 0.8 (yellow) -> 1.5+ (red, supersonic)");
        else if (aeroVisMode == AeroVisMode::Helicity) ImGui::Text("Helicity: -1 (blue) -> 0 (white) -> +1 (red)");
        else if (aeroVisMode == AeroVisMode::TotalPressure) ImGui::Text("Pt: low (blue, loss) -> high (red, freestream)");
    }
}

if (ImGui::CollapsingHeader("Voxel Collision v1.9.0", ImGuiTreeNodeFlags_DefaultOpen)) {
    ImGui::Checkbox("Enable Voxel Collision", &useVoxelCollision);
    ImGui::SliderInt("Voxel Resolution", &voxelResolution, 16, 128);
    if (ImGui::Button("Rebuild Voxel Grid")) {
        buildVoxelGrid(g_vertices, voxelResolution);
        if (lbmParams.enabled) { shutdownLBM(); initLBM(); }
    }
    ImGui::Text("Voxel: %dx%dx%d = %d cells | %.1f ms", g_voxNx, g_voxNy, g_voxNz, g_voxNx*g_voxNy*g_voxNz, perfVoxelMs);
}

if (ImGui::CollapsingHeader("Display v1.9.0", ImGuiTreeNodeFlags_DefaultOpen)) {
    ImGui::Checkbox("Show Model", &showModel);
    ImGui::Checkbox("Show Obstacle", &showObstacle);
    if (showObstacle) {
        ImGui::SliderFloat("Obstacle Alpha", &obstacleAlpha, 0.02f, 1.0f, "%.2f");
        ImGui::ColorEdit3("Obstacle Color", &obstacleColor[0]);
    }
    ImGui::Checkbox("Show Bounding Box", &showBoundingBox);
    ImGui::Checkbox("Show Axes", &showAxes);
    ImGui::Checkbox("Lighting", &lightingEnabled);
    ImGui::Checkbox("VSync", &vsyncEnabled);
    ImGui::Checkbox("Limit FPS", &limitFPS);
    if (limitFPS) ImGui::SliderFloat("Max FPS", &maxFPS, 10.0f, 240.0f);
    ImGui::SliderFloat("Camera Speed", &cameraSpeedMultiplier, 0.1f, 5.0f);
    ImGui::SliderFloat("Mouse Sens", &mouseSensitivity, 0.05f, 1.0f);
    ImGui::ColorEdit3("Background", &bgColor[0]);
    ImGui::ColorEdit3("Model Color", &modelColor[0]);
    ImGui::Checkbox("Save Settings on Exit", &aeroSaveSettings);
    ImGui::Text("Controls: WASD+QE move, RMB/MMB drag rotate, Wheel zoom, F1-F6, Ctrl+R reset, F5 screenshot, F6 CSV");
}

if (ImGui::CollapsingHeader("LBM - Lattice Boltzmann v1.9.0 Ultra+", ImGuiTreeNodeFlags_DefaultOpen)) {
    ImGui::Checkbox("Enable LBM (High-Accuracy Physics)", &lbmParams.enabled);
    if (lbmParams.enabled) {
        ImGui::TextColored(ImVec4(0.2f,1,0.8f,1), "LBM Active — Navier-Stokes, realistic as photos");
    } else {
        ImGui::TextColored(ImVec4(1,0.8f,0.2f,1), "Using potential flow + wake (legacy)");
    }
    ImGui::SliderInt("Steps per Frame", &lbmParams.stepsPerFrame, 1, 50);
    ImGui::SliderFloat("Tau (relaxation)", &lbmParams.tau, 0.51f, 1.5f, "%.3f");
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        if (!std::isfinite(lbmParams.tau) || lbmParams.tau < 0.51f) lbmParams.tau = 0.6f;
        lbmParams.viscosity = (lbmParams.tau - 0.5f) * 0.333333f;
    }
    ImGui::SliderFloat("U0 (lattice speed)", &lbmParams.U0, 0.01f, 0.25f, "%.3f");
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        if (!std::isfinite(lbmParams.U0) || lbmParams.U0 < 0.01f) lbmParams.U0 = 0.1f;
    }
    ImGui::Checkbox("Smagorinsky LES Turbulence", &lbmParams.useTurbulence);
    if (lbmParams.useTurbulence) ImGui::SliderFloat("Smagorinsky C", &lbmParams.smagorinskyC, 0.01f, 0.3f, "%.3f");
    ImGui::Checkbox("Adaptive Stepping (NEW v1.9.0)", &aeroAdaptiveLBM);
    ImGui::Checkbox("MRT (High Re stability)", &lbmParams.useMRT);
    ImGui::Checkbox("Regularized LBM (stability)", &lbmParams.useRegularized);
    ImGui::Checkbox("Zou/He Inlet BC (accurate)", &lbmParams.useZouHeBC);
    ImGui::Checkbox("Convective Outlet", &lbmParams.useConvectiveOutlet);
    ImGui::SliderFloat("Inlet Turbulence", &lbmParams.inletTurbulence, 0.0f, 0.1f, "%.3f");
    ImGui::Checkbox("Ground (for cars)", &lbmParams.useGround);
    if (lbmParams.useGround) ImGui::SliderFloat("Ground Height##lbm", &lbmParams.groundHeight, -2.0f, 2.0f, "%.2f");
    ImGui::Separator();
    ImGui::Text("LBM Grid: %dx%dx%d = %d cells", lbmNx, lbmNy, lbmNz, lbmNx*lbmNy*lbmNz);
    ImGui::Text("Steps: %d | Converged: %s | TKE: %.4f", lbmCurrentStep, lbmConverged ? "YES" : "NO", lbmTKE);
    ImGui::Text("Avg Rho: %.4f | Kinetic: %.6f | MaxVel LB %.3f", lbmAvgRho, lbmAvgKineticEnergy, lbmMaxVelocityLB);
    ImGui::Text("Max Vel World: %.2f m/s | Reynolds: %.1f | Conv: %.2e", lbmMaxVelocityWorld, lbmReynolds, lbmConvergence);
    ImGui::Text("Cell Size: (%.4f, %.4f, %.4f) | Time: %.1f ms", lbmCellSizeX, lbmCellSizeY, lbmCellSizeZ, lbmTimeMs);
    if (ImGui::Button("Init / Reset LBM")) { initLBM(); }
    ImGui::SameLine(); if (ImGui::Button("Reset Only")) { resetLBM(); }
    ImGui::SameLine(); if (ImGui::Button("Shutdown")) { shutdownLBM(); }
    if (ImGui::Button("Step 10")) { stepLBMCPU(10); }
    ImGui::SameLine(); if (ImGui::Button("Step 100")) { stepLBMCPU(100); }
    ImGui::SameLine(); if (ImGui::Button("Step 500")) { stepLBMCPU(500); }
    ImGui::SameLine(); if (ImGui::Button("Compute Vort/Q/TKE")) { computeLBMVorticityAndQ(); }
    ImGui::Separator();
    ImGui::Text("Realistic features v1.9.0:");
    ImGui::BulletText("Zou/He inlet + convective outlet + ground");
    ImGui::BulletText("Mach + Helicity + Total Pressure");
    ImGui::BulletText("Adaptive stepping + stability checks");
    ImGui::BulletText("Rainbow/Viridis/Parula/CoolWarm");
}

if (ImGui::CollapsingHeader("Compute & Export v1.9.0")) {
    ImGui::RadioButton("CUDA", &useCUDA, 1);
    ImGui::SameLine();
    ImGui::RadioButton("CPU", &useCUDA, 0);
    if (ImGui::Button("Open Model")) {
        std::string p = openFileDialog();
        if (!p.empty()) loadModel(p);
    }
    ImGui::SameLine();
    if (ImGui::Button("Screenshot BMP (F5)")) aeroScreenshotRequested = true;
    ImGui::SameLine();
    if (ImGui::Button("Export CSV (F6)")) aeroCSVExportRequested = true;
    if (ImGui::Button("Save Settings")) {
        if (saveSettings("aeros_settings.ini")) ImGui::Text("Saved aeros_settings.ini");
    }
    ImGui::SameLine();
    if (ImGui::Button("Load Settings")) {
        if (loadSettings("aeros_settings.ini")) {
            updateVertexColors(); computeStreamlines();
            ImGui::Text("Loaded settings");
        }
    }
    ImGui::Separator();
    ImGui::Text("Shortcuts: F1 model, F2 pressure, F3 streamlines, F4 particles, F5 screenshot, F6 CSV, Ctrl+R reset camera");
}

if (ImGui::CollapsingHeader("Test Mode v1.9.0 Ultra (Physics+Code+LBM+Opt+Realistic+New)", ImGuiTreeNodeFlags_DefaultOpen)) {
    ImGui::Checkbox("Enable Test Mode", &testModeEnabled);
    ImGui::Checkbox("Continuous Validation", &testContinuous);
    int realisticPassed = 0, realisticFailed = 0;
    for (auto& r : lastTestResults) if (r.category=="Realistic") { if (r.passed) realisticPassed++; else realisticFailed++; }
    int optPassed = 0, optFailed = 0;
    for (auto& r : lastTestResults) if (r.category=="Optimization") { if (r.passed) optPassed++; else optFailed++; }
    ImGui::Text("Total: %d passed, %d failed | LBM: %d/%d | Realistic: %d/%d", testsPassed, testsFailed, lbmTestsPassed, lbmTestsPassed+lbmTestsFailed, realisticPassed, realisticPassed+realisticFailed);
    ImGui::Text("  Physics: %d | Code: %d/%d | Opt: %d/%d", testsPassed - codeTestsPassed - lbmTestsPassed - optPassed - realisticPassed, codeTestsPassed, codeTestsPassed+codeTestsFailed, optPassed, optPassed+optFailed);
    if (codeTestsFailed > 0) ImGui::Text("  Code failed: %d", codeTestsFailed);
    ImGui::Text("Last run: %.1f ms", lastTestTimeMs);
    if (lastGLError != 0) ImGui::TextColored(ImVec4(1,0.3f,0.1f,1), "GL Error: %s", lastGLErrorStr.c_str());
    if (testsFailed > 0) ImGui::TextColored(ImVec4(1,0.2f,0.2f,1), "!!! ERRORS: Phys=%d Code=%d LBM=%d Real=%d !!!", testsFailed - codeTestsFailed - lbmTestsFailed - realisticFailed - optFailed, codeTestsFailed, lbmTestsFailed, realisticFailed);
    else if (testsPassed > 0) ImGui::TextColored(ImVec4(0.2f,1,0.2f,1), "All tests passed — physics, code, LBM, opt, realistic OK");

    if (ImGui::Button("Run All Tests")) runAllTests();
    ImGui::SameLine(); if (ImGui::Button("Physics Only")) { lastTestResults.clear(); testLog.clear(); testsPassed=0; testsFailed=0; codeTestsPassed=0; codeTestsFailed=0; runPhysicsTests(); }
    ImGui::SameLine(); if (ImGui::Button("Code Only")) { lastTestResults.clear(); testLog.clear(); testsPassed=0; testsFailed=0; codeTestsPassed=0; codeTestsFailed=0; runCodeTests(); }
    if (ImGui::Button("LBM Only")) { lastTestResults.clear(); testLog.clear(); testsPassed=0; testsFailed=0; runLBMTests(); }
    ImGui::SameLine(); if (ImGui::Button("Realistic Only")) { lastTestResults.clear(); testLog.clear(); testsPassed=0; testsFailed=0; runRealisticTests(); }
    ImGui::SameLine(); if (ImGui::Button("Opt Only")) { lastTestResults.clear(); testLog.clear(); testsPassed=0; testsFailed=0; runOptimizationTests(); }
    if (ImGui::Button("Clear Log")) {
        testLog.clear(); lastTestResults.clear(); testsPassed=0; testsFailed=0; codeTestsPassed=0; codeTestsFailed=0; lbmTestsPassed=0; lbmTestsFailed=0; lastGLError=0; lastGLErrorStr.clear();
    }

    if (!lastTestResults.empty()) {
        ImGui::Separator();
        auto drawCat = [&](const char* catName, const char* label) {
            int pass=0, fail=0;
            for (auto& r : lastTestResults) if (r.category==catName) { if (r.passed) pass++; else fail++; }
            if (pass+fail>0) {
                std::string title = std::string(label) + " (" + std::to_string(pass) + "/" + std::to_string(pass+fail) + ")";
                if (ImGui::TreeNode(title.c_str())) {
                    for (auto& r : lastTestResults) if (r.category==catName) {
                        ImVec4 col = r.passed ? ImVec4(0.2f,1,0.2f,1) : ImVec4(1,0.2f,0.2f,1);
                        ImGui::TextColored(col, "%s: %s", r.name.c_str(), r.passed ? "PASS" : "FAIL");
                        if (!r.message.empty() && r.message != "OK" && r.message != "FAILED") {
                            ImGui::SameLine(); ImGui::Text(" - %s", r.message.c_str());
                        }
                    }
                    ImGui::TreePop();
                }
            }
        };
        drawCat("Physics", "Physics Tests");
        drawCat("Code", "Code Tests");
        drawCat("LBM", "LBM Tests");
        drawCat("Optimization", "Optimization");
        drawCat("Realistic", "Realistic Aero");
    }

    if (!testLog.empty()) {
        ImGui::Separator();
        ImGui::Text("Test Log:");
        ImGui::BeginChild("TestLog", ImVec2(0, 250), true);
        ImGui::TextUnformatted(testLog.c_str());
        ImGui::EndChild();
    }

    ImGui::Separator();
    ImGui::Text("Frame Validation (real-time):");
    bool hasNaN = false;
    if (!std::isfinite(flowSpeed) || !std::isfinite(altitude) || !std::isfinite(airDensity)) hasNaN = true;
    if (!std::isfinite(liftMagnitude) || !std::isfinite(dragMagnitude)) hasNaN = true;
    if (!std::isfinite(deltaTime) || !std::isfinite(maxDim)) hasNaN = true;
    if (!std::isfinite(aeroRefArea)) hasNaN = true;
    if (hasNaN) ImGui::TextColored(ImVec4(1,0,0,1), "NaN/Inf detected in globals!");
    else ImGui::TextColored(ImVec4(0,1,0,1), "No NaN in globals");

    bool bufOk = true;
    if (particleDrawCount < 0 || particleDrawCount > (int)(particlePositions.size()/3+1)) bufOk = false;
    if (!g_distanceField.empty() && (int)g_distanceField.size() != g_voxNx*g_voxNy*g_voxNz) bufOk = false;
    if (g_vertices.size() != g_normals.size()) bufOk = false;
    if (!bufOk) ImGui::TextColored(ImVec4(1,0.3f,0,1), "Buffer integrity issue!");
    else ImGui::TextColored(ImVec4(0,1,0,1), "Buffers OK");

    float vinf = sqrtf(flowParams.vx*flowParams.vx + flowParams.vy*flowParams.vy + flowParams.vz*flowParams.vz);
    ImGui::Text("Vinf: %.3f m/s (%.1f km/h) | dt=%.4f | Re=%.0f", vinf, vinf*3.6f, deltaTime, aeroReNumber);
    ImGui::Text("Flow: (%.2f, %.2f, %.2f) cs=(%.3f,%.3f,%.3f)", flowParams.vx, flowParams.vy, flowParams.vz, flowParams.cellSizeX, flowParams.cellSizeY, flowParams.cellSizeZ);
    ImGui::Text("Center: (%.2f, %.2f, %.2f) maxDim %.2f | RefArea %.3f", center.x, center.y, center.z, maxDim, aeroRefArea);
    ImGui::Text("Camera: pos(%.1f,%.1f,%.1f) front(%.2f,%.2f,%.2f) fov %.1f", cameraPos.x, cameraPos.y, cameraPos.z, cameraFront.x, cameraFront.y, cameraFront.z, fov);
    if (!g_distanceField.empty()) {
        float minD = 1e9f, maxD = -1e9f;
        int finiteCount = 0;
        for (float d : g_distanceField) if (std::isfinite(d)) { if (d < minD) minD = d; if (d > maxD) maxD = d; finiteCount++; }
        ImGui::Text("SDF: [%.2f, %.2f] %d/%d finite | Mem %.1f MB", minD, maxD, finiteCount, (int)g_distanceField.size(),
            (g_vertices.size()*4 + g_distanceField.size()*4 + particlePositions.size()*4)/1024.0f/1024.0f);
    }
    ImGui::Text("Particles: %d/%d | Streamlines: %d verts | VAOs: M%d P%d S%d B%d G%d",
        particleDrawCount, numParticles, streamlineVertexCount,
        modelVAO!=0, particleVAO!=0, streamlineVAO!=0, bboxVAO!=0, groundVAO!=0);
}
    ImGui::End();
}
