#include <imgui.h>

#include <string>
#include <cstdio>
#include <cmath>

#include "globals.h"
#include "stl_loader.h"
#include "model.h"
#include "particles.h"
#include "streamlines.h"
#include "voxel_grid.h"
#include "atmosphere.h"
#include "test_mode.h"
#include "lbm.h"
#include "ui.h"

// =====================================================
// Панель управления — v1.4.0: расширенный тест ошибок кода
// =====================================================
void drawUI() {
ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_Once);
ImGui::SetNextWindowSize(ImVec2(450, 950), ImGuiCond_Once);
ImGui::Begin("AeroS Control v1.6.0 Optimized");
ImGui::Text("FPS: %.1f | Frame: %.2f ms", deltaTime > 1e-6f ? 1.0f/deltaTime : 0.0f, perfFrameMs);
ImGui::Text("Vertices: %d | Triangles: %d", modelVertexCount, modelVertexCount/3);
ImGui::Text("Voxel Grid: %dx%dx%d", g_voxNx, g_voxNy, g_voxNz);
ImGui::Text("OpenMP: %d threads | %s", perfOpenMPThreads,
#ifdef _OPENMP
    "Enabled"
#else
    "Disabled"
#endif
);
ImGui::Separator();
if (ImGui::CollapsingHeader("Performance (v1.6.0)", ImGuiTreeNodeFlags_DefaultOpen)) {
    ImGui::Text("Frame: %.2f ms | FPS: %.1f", perfFrameMs, perfFrameMs > 1e-3f ? 1000.0f/perfFrameMs : 0.0f);
    ImGui::Text("LBM: %.2f ms | Particles: %.2f ms", perfLBMms, perfParticlesMs);
    ImGui::Text("Forces: %.2f ms | Streamlines: %.2f ms", perfForcesMs, perfStreamlinesMs);
    if (lbmInitialized) {
        int total = lbmNx*lbmNy*lbmNz;
        float mlups = (total * lbmParams.stepsPerFrame) / (lbmTimeMs > 0 ? lbmTimeMs : 1.0f) / 1000.0f;
        ImGui::Text("LBM: %d cells | %.2f MLUPS | %.1f ms/step", total, mlups, lbmTimeMs);
    }
    ImGui::Separator();
    ImGui::Text("Optimizations active:");
    ImGui::BulletText("OpenMP parallel for (LBM, voxels, particles, streamlines, forces)");
    ImGui::BulletText("AVX2 + O2/Ot + LTCG + fast math");
    ImGui::BulletText("Gather streaming (cache-friendly)");
    ImGui::BulletText("AABB culling for voxelization");
    ImGui::BulletText("Precomputed flow axis & inlet BC");
    ImGui::BulletText("SoA layout + raw pointers");
}
ImGui::Separator();

if (ImGui::CollapsingHeader("Flow", ImGuiTreeNodeFlags_DefaultOpen)) {
    const char* unitItems[] = { "m/s (м/с)", "km/h (км/ч)", "mph (миль/ч)", "kts (узлы)", "ft/s (фут/с)" };
    int unitIdx = (int)speedUnit;
    if (ImGui::Combo("Speed Unit", &unitIdx, unitItems, IM_ARRAYSIZE(unitItems))) {
        speedUnit = (SpeedUnit)unitIdx;
    }
    float displaySpeed = speedFromMS(flowSpeed, speedUnit);
    float maxDisplay = 10.0f;
    switch (speedUnit) {
        case SPEED_MS: maxDisplay = 10.0f; break;
        case SPEED_KMH: maxDisplay = 36.0f; break;
        case SPEED_MPH: maxDisplay = 22.0f; break;
        case SPEED_KNOTS: maxDisplay = 19.0f; break;
        case SPEED_FTS: maxDisplay = 33.0f; break;
        default: break;
    }
    if (ImGui::SliderFloat("##speed", &displaySpeed, 0.0f, maxDisplay, "%.2f")) {
        flowSpeed = speedToMS(displaySpeed, speedUnit);
    }
    ImGui::SameLine(); ImGui::SetNextItemWidth(70);
    if (ImGui::InputFloat("##spd", &displaySpeed, 0, 0, "%.2f")) {
        if (displaySpeed < 0) displaySpeed = 0;
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
    ImGui::InputFloat("##az", &flowAzimuth, 0, 0, "%.1f");
    ImGui::SliderFloat("Elevation", &flowElevation, -90.0f, 90.0f);
    ImGui::SameLine(); ImGui::SetNextItemWidth(80);
    ImGui::InputFloat("##el", &flowElevation, 0, 0, "%.1f");
    ImGui::SliderFloat("Time Scale", &timeScale, 0.01f, 3.0f, "%.2f");
    ImGui::SliderFloat("Strouhal", &strouhal, 0.05f, 0.5f, "%.3f");
    ImGui::SliderFloat("Wake Strength", &wakeStrength, 0.0f, 1.5f);
    ImGui::SliderFloat("Wake Length", &wakeLength, 2.0f, 30.0f);
}

if (ImGui::CollapsingHeader("Atmosphere (ISA)", ImGuiTreeNodeFlags_DefaultOpen)) {
    ImGui::Checkbox("Use Real Air Density", &useRealDensity);
    ImGui::Text("Altitude affects drag/lift via rho");
    ImGui::SliderFloat("Altitude (m)", &altitude, 0.0f, 20000.0f, "%.0f m");
    ImGui::SameLine(); ImGui::SetNextItemWidth(80);
    ImGui::InputFloat("##alt", &altitude, 0, 0, "%.0f");
    if (ImGui::Button("Sea Level")) altitude = 0.0f;
    ImGui::SameLine();
    if (ImGui::Button("5 km")) altitude = 5000.0f;
    ImGui::SameLine();
    if (ImGui::Button("10 km")) altitude = 10000.0f;
    ImGui::SameLine();
    if (ImGui::Button("15 km")) altitude = 15000.0f;
    ImGui::Separator();
    ImGui::Text("Air Density: %.4f kg/m3", airDensity);
    ImGui::Text("Pressure: %.0f Pa (%.2f atm)", airPressure, airPressure/101325.0f);
    ImGui::Text("Temperature: %.1f K (%.1f C)", airTemperature, airTemperature-273.15f);
    ImGui::Text("Speed of Sound: %.1f m/s", speedOfSound);
    float mach = (speedOfSound > 1e-3f) ? flowSpeed / speedOfSound : 0.0f;
    ImGui::Text("Mach: %.3f", mach);
    if (ImGui::TreeNode("Density Table")) {
        ImGui::Text(" Alt (m) | rho (kg/m3) | P (hPa) | T (C)");
        ImGui::Separator();
        int alts[] = {0, 1000, 2000, 3000, 5000, 8000, 10000, 12000, 15000, 20000};
        for (int h : alts) {
            AtmosphereParams atm = calculateAtmosphere((float)h);
            ImGui::Text("%7d | %8.4f | %7.1f | %6.1f %s",
                h, atm.density, atm.pressure/100.0f, atm.temperature-273.15f,
                (fabsf((float)h - altitude) < 10.0f) ? "<--" : "");
        }
        ImGui::TreePop();
    }
    float q = 0.5f * airDensity * flowSpeed * flowSpeed;
    ImGui::Text("Dynamic Pressure q: %.1f Pa", q);
}

if (ImGui::CollapsingHeader("Particles", ImGuiTreeNodeFlags_DefaultOpen)) {
    ImGui::Checkbox("Show Particles", &showParticles);
    ImGui::SliderInt("Count", &numParticles, 100, 200000);
    if (ImGui::IsItemDeactivatedAfterEdit()) initParticles();
    ImGui::SliderFloat("Size", &particleSize, 1.0f, 8.0f);
    ImGui::SliderFloat("Max Speed Color", &maxSpeedForColor, 0.5f, 20.0f);
    if (ImGui::Button("Reset Particles")) initParticles();
}

if (ImGui::CollapsingHeader("Streamlines", ImGuiTreeNodeFlags_DefaultOpen)) {
    ImGui::Checkbox("Show Streamlines", &showStreamlines);
    ImGui::SliderInt("Count##sl", &numStreamlines, 4, 200);
    ImGui::SliderInt("Steps", &streamlineSteps, 20, 1000);
    ImGui::SliderFloat("Step Size", &streamlineStepSize, 0.01f, 0.5f);
    ImGui::SliderFloat("Line Width", &streamlineWidth, 1.0f, 5.0f);
    ImGui::SliderFloat("Alpha", &streamlineAlpha, 0.1f, 1.0f);
    if (ImGui::Button("Rebuild Streamlines")) computeStreamlines();
}

if (ImGui::CollapsingHeader("Pressure & Forces", ImGuiTreeNodeFlags_DefaultOpen)) {
    ImGui::Checkbox("Show Pressure Colors", &showPressure);
    ImGui::Checkbox("Show Lift/Drag Vectors", &showLiftDrag);
    ImGui::Text("Drag:  %.3f N (scaled by rho)", dragMagnitude);
    ImGui::Text("Lift:  %.3f N (scaled by rho)", liftMagnitude);
    float q = 0.5f * airDensity * flowSpeed * flowSpeed;
    if (q > 1e-6f) {
        ImGui::Text("Cd (approx): %.3f", dragMagnitude / q);
        ImGui::Text("Cl (approx): %.3f", liftMagnitude / q);
    }
}

if (ImGui::CollapsingHeader("Voxel Collision", ImGuiTreeNodeFlags_DefaultOpen)) {
    ImGui::Checkbox("Enable Voxel Collision", &useVoxelCollision);
    ImGui::SliderInt("Voxel Resolution", &voxelResolution, 16, 128);
    if (ImGui::Button("Rebuild Voxel Grid")) {
        buildVoxelGrid(g_vertices, voxelResolution);
    }
}

if (ImGui::CollapsingHeader("Display", ImGuiTreeNodeFlags_DefaultOpen)) {
    ImGui::Checkbox("Show Model", &showModel);
    ImGui::Checkbox("Show Obstacle", &showObstacle);
    if (showObstacle) {
        ImGui::SliderFloat("Obstacle Alpha", &obstacleAlpha, 0.02f, 1.0f, "%.2f");
        ImGui::ColorEdit3("Obstacle Color", &obstacleColor[0]);
    }
    ImGui::Checkbox("Show Bounding Box", &showBoundingBox);
    ImGui::Checkbox("Show Axes", &showAxes);
    ImGui::Checkbox("Lighting", &lightingEnabled);
    ImGui::ColorEdit3("Background", &bgColor[0]);
}

if (ImGui::CollapsingHeader("LBM - Lattice Boltzmann (v1.6.0 Optimized)", ImGuiTreeNodeFlags_DefaultOpen)) {
    ImGui::Checkbox("Enable LBM (High-Accuracy Physics)", &lbmParams.enabled);
    if (lbmParams.enabled) {
        ImGui::TextColored(ImVec4(0.2f,1,0.8f,1), "LBM Active — superior to potential flow");
    } else {
        ImGui::TextColored(ImVec4(1,0.8f,0.2f,1), "Using potential flow + wake (legacy)");
    }
    ImGui::SliderInt("Steps per Frame", &lbmParams.stepsPerFrame, 1, 20);
    ImGui::SliderFloat("Tau (relaxation)", &lbmParams.tau, 0.51f, 1.5f, "%.3f");
    if (ImGui::IsItemDeactivatedAfterEdit()) { lbmParams.viscosity = (lbmParams.tau - 0.5f) * 0.333333f; }
    ImGui::SliderFloat("U0 (lattice speed)", &lbmParams.U0, 0.01f, 0.25f, "%.3f");
    ImGui::Checkbox("Smagorinsky LES Turbulence", &lbmParams.useTurbulence);
    if (lbmParams.useTurbulence) {
        ImGui::SliderFloat("Smagorinsky C", &lbmParams.smagorinskyC, 0.01f, 0.3f, "%.3f");
    }
    ImGui::Separator();
    ImGui::Text("LBM Grid: %dx%dx%d = %d cells", lbmNx, lbmNy, lbmNz, lbmNx*lbmNy*lbmNz);
    ImGui::Text("Steps: %d | Converged: %s", lbmCurrentStep, lbmConverged ? "YES" : "NO");
    ImGui::Text("Avg Rho: %.4f | Kinetic: %.6f", lbmAvgRho, lbmAvgKineticEnergy);
    ImGui::Text("Max Vel LB: %.4f | World: %.2f m/s", lbmMaxVelocityLB, lbmMaxVelocityWorld);
    ImGui::Text("Reynolds: %.1f | Conv: %.2e | Time: %.1f ms", lbmReynolds, lbmConvergence, lbmTimeMs);
    ImGui::Text("Cell Size: (%.3f, %.3f, %.3f)", lbmCellSizeX, lbmCellSizeY, lbmCellSizeZ);
    if (ImGui::Button("Init / Reset LBM")) { initLBM(); }
    ImGui::SameLine();
    if (ImGui::Button("Reset LBM Only")) { resetLBM(); }
    ImGui::SameLine();
    if (ImGui::Button("Shutdown LBM")) { shutdownLBM(); }
    if (ImGui::Button("Step 10")) { stepLBMCPU(10); }
    ImGui::SameLine();
    if (ImGui::Button("Step 100")) { stepLBMCPU(100); }
    ImGui::SameLine();
    if (ImGui::Button("Compute Vorticity/Q")) { computeLBMVorticityAndQ(); }
    ImGui::Separator();
    ImGui::Text("LBM provides:");
    ImGui::BulletText("True Navier-Stokes via Boltzmann");
    ImGui::BulletText("Automatic flow separation & vortices");
    ImGui::BulletText("No-slip bounce-back on surface");
    ImGui::BulletText("LES turbulence (Smagorinsky)");
    ImGui::BulletText("Vorticity & Q-criterion");
}

if (ImGui::CollapsingHeader("Compute")) {
    ImGui::RadioButton("CUDA", &useCUDA, 1);
    ImGui::SameLine();
    ImGui::RadioButton("CPU", &useCUDA, 0);
    if (ImGui::Button("Open Model")) {
        std::string p = openFileDialog();
        if (!p.empty()) loadModel(p);
    }
}

if (ImGui::CollapsingHeader("Test Mode (Physics + Code Errors)", ImGuiTreeNodeFlags_DefaultOpen)) {
    ImGui::Checkbox("Enable Test Mode", &testModeEnabled);
    ImGui::Checkbox("Continuous Validation", &testContinuous);
    ImGui::Text("Total: %d passed, %d failed", testsPassed, testsFailed);
    ImGui::Text("  Physics: %d / Code: %d passed", testsPassed - codeTestsPassed, codeTestsPassed);
    if (codeTestsFailed > 0) ImGui::Text("  Code failed: %d", codeTestsFailed);
    ImGui::Text("Last run: %.1f ms", lastTestTimeMs);
    if (lastGLError != 0) {
        ImGui::TextColored(ImVec4(1,0.3f,0.1f,1), "GL Error: %s", lastGLErrorStr.c_str());
    }
    if (testsFailed > 0) {
        ImGui::TextColored(ImVec4(1,0.2f,0.2f,1), "!!! ERRORS DETECTED: Physics=%d Code=%d !!!", testsFailed - codeTestsFailed, codeTestsFailed);
    } else if (testsPassed > 0) {
        ImGui::TextColored(ImVec4(0.2f,1,0.2f,1), "All tests passed — physics & code OK");
    }

    if (ImGui::Button("Run All Tests")) {
        runAllTests();
    }
    ImGui::SameLine();
    if (ImGui::Button("Run Physics Only")) {
        lastTestResults.clear(); testLog.clear(); testsPassed=0; testsFailed=0; codeTestsPassed=0; codeTestsFailed=0;
        runPhysicsTests();
    }
    ImGui::SameLine();
    if (ImGui::Button("Run Code Only")) {
        lastTestResults.clear(); testLog.clear(); testsPassed=0; testsFailed=0; codeTestsPassed=0; codeTestsFailed=0;
        runCodeTests();
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear Log")) {
        testLog.clear();
        lastTestResults.clear();
        testsPassed = 0; testsFailed = 0;
        codeTestsPassed = 0; codeTestsFailed = 0;
        lastGLError = 0; lastGLErrorStr.clear();
    }

    if (!lastTestResults.empty()) {
        ImGui::Separator();
        int physPass = 0, physFail = 0;
        for (auto& r : lastTestResults) if (r.category=="Physics") { if (r.passed) physPass++; else physFail++; }
        if (ImGui::TreeNode(("Physics Tests (" + std::to_string(physPass) + "/" + std::to_string(physPass+physFail) + ")").c_str())) {
            for (auto& r : lastTestResults) if (r.category=="Physics") {
                ImVec4 col = r.passed ? ImVec4(0.2f,1,0.2f,1) : ImVec4(1,0.2f,0.2f,1);
                ImGui::TextColored(col, "%s: %s", r.name.c_str(), r.passed ? "PASS" : "FAIL");
                if (!r.message.empty() && r.message != "OK" && r.message != "FAILED") {
                    ImGui::SameLine(); ImGui::Text(" - %s", r.message.c_str());
                }
            }
            ImGui::TreePop();
        }
        int codePass = 0, codeFail = 0;
        for (auto& r : lastTestResults) if (r.category=="Code") { if (r.passed) codePass++; else codeFail++; }
        if (ImGui::TreeNode(("Code Action Tests (" + std::to_string(codePass) + "/" + std::to_string(codePass+codeFail) + ")").c_str())) {
            for (auto& r : lastTestResults) if (r.category=="Code") {
                ImVec4 col = r.passed ? ImVec4(0.4f,0.8f,1.0f,1) : ImVec4(1,0.3f,0.1f,1);
                ImGui::TextColored(col, "%s: %s", r.name.c_str(), r.passed ? "PASS" : "FAIL");
                if (!r.message.empty() && r.message != "OK" && r.message != "FAILED") {
                    ImGui::SameLine(); ImGui::Text(" - %s", r.message.c_str());
                }
            }
            ImGui::TreePop();
        }
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
    if (hasNaN) ImGui::TextColored(ImVec4(1,0,0,1), "NaN/Inf detected in globals!");
    else ImGui::TextColored(ImVec4(0,1,0,1), "No NaN in globals");

    bool bufOk = true;
    if (particleDrawCount < 0 || particleDrawCount > (int)(particlePositions.size()/3+1)) bufOk = false;
    if (!g_distanceField.empty() && (int)g_distanceField.size() != g_voxNx*g_voxNy*g_voxNz) bufOk = false;
    if (!bufOk) ImGui::TextColored(ImVec4(1,0.3f,0,1), "Buffer integrity issue!");
    else ImGui::TextColored(ImVec4(0,1,0,1), "Buffers OK");

    float vinf = sqrtf(flowParams.vx*flowParams.vx + flowParams.vy*flowParams.vy + flowParams.vz*flowParams.vz);
    ImGui::Text("Vinf: %.3f m/s (%.1f km/h) | dt=%.4f", vinf, vinf*3.6f, deltaTime);
    ImGui::Text("FlowParams: (%.2f, %.2f, %.2f) cs=(%.3f,%.3f,%.3f)", flowParams.vx, flowParams.vy, flowParams.vz, flowParams.cellSizeX, flowParams.cellSizeY, flowParams.cellSizeZ);
    ImGui::Text("Center: (%.2f, %.2f, %.2f) maxDim %.2f", center.x, center.y, center.z, maxDim);
    ImGui::Text("Camera: pos(%.1f,%.1f,%.1f) front(%.2f,%.2f,%.2f)", cameraPos.x, cameraPos.y, cameraPos.z, cameraFront.x, cameraFront.y, cameraFront.z);
    if (!g_distanceField.empty()) {
        float minD = 1e9f, maxD = -1e9f;
        for (float d : g_distanceField) if (std::isfinite(d)) { if (d < minD) minD = d; if (d > maxD) maxD = d; }
        ImGui::Text("SDF range: [%.2f, %.2f] voxels | total mem %.1f MB", minD, maxD,
            (g_vertices.size()*4 + g_distanceField.size()*4 + particlePositions.size()*4)/1024.0f/1024.0f);
    }
    ImGui::Text("Particles: %d/%d | Streamlines: %d verts | VAOs: M%d P%d S%d B%d",
        particleDrawCount, numParticles, streamlineVertexCount,
        modelVAO!=0, particleVAO!=0, streamlineVAO!=0, bboxVAO!=0);
}
    ImGui::End();
}
