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
#include "forces.h"
#include "ui.h"

// =====================================================
// Панель управления — v1.7.0 Realistic Aero
// =====================================================
void drawUI() {
ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_Once);
ImGui::SetNextWindowSize(ImVec2(480, 1000), ImGuiCond_Once);
ImGui::Begin("AeroS Control v1.7.0 Realistic Aero");
ImGui::Text("FPS: %.1f | Frame: %.2f ms | v1.7.0", deltaTime > 1e-6f ? 1.0f/deltaTime : 0.0f, perfFrameMs);
ImGui::Text("Vertices: %d | Triangles: %d", modelVertexCount, modelVertexCount/3);
ImGui::Text("Voxel: %dx%dx%d | LBM: %dx%dx%d", g_voxNx, g_voxNy, g_voxNz, lbmNx, lbmNy, lbmNz);
ImGui::Text("OpenMP: %d threads | %s | Realistic: %s",
    perfOpenMPThreads,
#ifdef _OPENMP
    "ON",
#else
    "OFF",
#endif
    lbmParams.enabled ? "LBM" : "Potential"
);
ImGui::Separator();

if (ImGui::CollapsingHeader("Performance (v1.7.0)", ImGuiTreeNodeFlags_DefaultOpen)) {
    ImGui::Text("Frame: %.2f ms | FPS: %.1f", perfFrameMs, perfFrameMs > 1e-3f ? 1000.0f/perfFrameMs : 0.0f);
    ImGui::Text("LBM: %.2f ms | Particles: %.2f ms", perfLBMms, perfParticlesMs);
    ImGui::Text("Forces: %.2f ms | Streamlines: %.2f ms", perfForcesMs, perfStreamlinesMs);
    ImGui::Text("Voxel: %.2f ms", perfVoxelMs);
    if (lbmInitialized) {
        int total = lbmNx*lbmNy*lbmNz;
        float mlups = (total * lbmParams.stepsPerFrame) / (lbmTimeMs > 0 ? lbmTimeMs : 1.0f) / 1000.0f;
        ImGui::Text("LBM: %d cells | %.2f MLUPS | %.1f ms/step | TKE %.4f", total, mlups, lbmTimeMs, lbmTKE);
        ImGui::Text("Re: %.0f | Converged: %s | Steps: %d", lbmReynolds, lbmConverged?"YES":"NO", lbmCurrentStep);
    }
    ImGui::Separator();
    ImGui::Text("Realistic Aero (as in photos):");
    ImGui::BulletText("Pressure rainbow (NASCAR) + U Magnitude (UAV)");
    ImGui::BulletText("Velocity-colored streamlines (car underbody)");
    ImGui::BulletText("Vorticity & Q-criterion + wake");
    ImGui::BulletText("Ground effect for cars (Cybertruck)");
    ImGui::BulletText("Flow separation highlighting");
    ImGui::BulletText("OpenMP + AVX2 + LTCG");
}
ImGui::Separator();

if (ImGui::CollapsingHeader("Realistic Aero (v1.7.0) — Photo Mode", ImGuiTreeNodeFlags_DefaultOpen)) {
    const char* visModes[] = { "Pressure Cp (NASCAR rainbow)", "Velocity |U| (UAV/Cybertruck)", "Vorticity |w|", "Q-Criterion (vortices)", "Turbulent TKE" };
    int visIdx = (int)aeroVisMode;
    if (ImGui::Combo("Visualization", &visIdx, visModes, IM_ARRAYSIZE(visModes))) {
        aeroVisMode = (AeroVisMode)visIdx;
        if (showPressure) updateVertexColors();
    }
    ImGui::Checkbox("Color Streamlines by Velocity (photo 2)", &aeroColorStreamlinesByVelocity);
    ImGui::Checkbox("Highlight Flow Separation", &aeroShowSeparation);
    ImGui::Checkbox("Ground Effect (for cars, photo 2/5)", &aeroGroundEffect);
    if (aeroGroundEffect) {
        ImGui::SliderFloat("Ground Height", &aeroGroundHeight, -2.0f, 2.0f, "%.2f");
        lbmParams.useGround = aeroGroundEffect;
        lbmParams.groundHeight = aeroGroundHeight;
    }
    ImGui::Separator();
    ImGui::Text("Reference Area for Cd/Cl:");
    ImGui::Checkbox("Auto Ref Area", &aeroAutoRefArea);
    if (!aeroAutoRefArea) ImGui::SliderFloat("Ref Area", &aeroRefArea, 0.1f, 10.0f, "%.2f m2");
    else ImGui::Text("Auto: %.3f m2 (from BB)", aeroRefArea);
    ImGui::Separator();
    ImGui::Text("Presets (like photos):");
    if (ImGui::Button("Car Preset (NASCAR/Cybertruck)")) {
        aeroVisMode = AeroVisMode::Pressure;
        aeroGroundEffect = true;
        aeroGroundHeight = 0.0f;
        aeroColorStreamlinesByVelocity = true;
        aeroShowSeparation = true;
        lbmParams.enabled = true;
        lbmParams.useGround = true;
        lbmParams.stepsPerFrame = 5;
        lbmParams.tau = 0.55f;
        lbmParams.useTurbulence = true;
        if (lbmInitialized) resetLBM(); else initLBM();
        updateVertexColors(); computeStreamlines();
    }
    ImGui::SameLine();
    if (ImGui::Button("UAV Preset (photo 1)")) {
        aeroVisMode = AeroVisMode::VelocityMagnitude;
        aeroGroundEffect = false;
        aeroColorStreamlinesByVelocity = true;
        lbmParams.enabled = true;
        lbmParams.stepsPerFrame = 3;
        lbmParams.tau = 0.6f;
        if (lbmInitialized) resetLBM(); else initLBM();
        updateVertexColors(); computeStreamlines();
    }
    ImGui::SameLine();
    if (ImGui::Button("Airfoil Preset (photo 3)")) {
        aeroVisMode = AeroVisMode::Pressure;
        aeroGroundEffect = false;
        aeroColorStreamlinesByVelocity = false;
        lbmParams.enabled = true;
        lbmParams.stepsPerFrame = 4;
        if (lbmInitialized) resetLBM(); else initLBM();
        updateVertexColors(); computeStreamlines();
    }
    if (ImGui::Button("Enable Realistic LBM (Recommended)")) {
        lbmParams.enabled = true;
        lbmParams.useZouHeBC = true;
        lbmParams.useConvectiveOutlet = true;
        lbmParams.useTurbulence = true;
        lbmParams.inletTurbulence = 0.02f;
        if (!lbmInitialized) initLBM(); else resetLBM();
    }
    ImGui::SameLine();
    if (ImGui::Button("Disable LBM (Legacy)")) {
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

if (ImGui::CollapsingHeader("Pressure & Forces (Realistic)", ImGuiTreeNodeFlags_DefaultOpen)) {
    ImGui::Checkbox("Show Pressure Colors", &showPressure);
    ImGui::Checkbox("Show Lift/Drag Vectors", &showLiftDrag);
    ImGui::Text("Drag:  %.3f N (scaled by rho) | Cd: %.3f", dragMagnitude, aeroRefArea>1e-6f ? dragMagnitude / (0.5f*airDensity*flowSpeed*flowSpeed*aeroRefArea) : 0);
    ImGui::Text("Lift:  %.3f N (scaled by rho) | Cl: %.3f", liftMagnitude, aeroRefArea>1e-6f ? liftMagnitude / (0.5f*airDensity*flowSpeed*flowSpeed*aeroRefArea) : 0);
    ImGui::Text("Ref Area: %.3f m2", aeroRefArea);
    float q = 0.5f * airDensity * flowSpeed * flowSpeed;
    if (q > 1e-6f) {
        ImGui::Text("Cd (approx q): %.3f | Cl (approx q): %.3f", dragMagnitude / q, liftMagnitude / q);
    }
    ImGui::Text("CoP: (%.2f, %.2f, %.2f)", centerOfPressure.x, centerOfPressure.y, centerOfPressure.z);
    if (lbmInitialized) {
        ImGui::Text("LBM Pressure: min %.2f max %.2f", 
            lbmPressure.empty()?0:*std::min_element(lbmPressure.begin(), lbmPressure.end()),
            lbmPressure.empty()?0:*std::max_element(lbmPressure.begin(), lbmPressure.end()));
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

if (ImGui::CollapsingHeader("LBM - Lattice Boltzmann (v1.7.0 Realistic)", ImGuiTreeNodeFlags_DefaultOpen)) {
    ImGui::Checkbox("Enable LBM (High-Accuracy Physics)", &lbmParams.enabled);
    if (lbmParams.enabled) {
        ImGui::TextColored(ImVec4(0.2f,1,0.8f,1), "LBM Active — Navier-Stokes, realistic as photos");
    } else {
        ImGui::TextColored(ImVec4(1,0.8f,0.2f,1), "Using potential flow + wake (legacy)");
    }
    ImGui::SliderInt("Steps per Frame", &lbmParams.stepsPerFrame, 1, 50);
    ImGui::SliderFloat("Tau (relaxation)", &lbmParams.tau, 0.51f, 1.5f, "%.3f");
    if (ImGui::IsItemDeactivatedAfterEdit()) { lbmParams.viscosity = (lbmParams.tau - 0.5f) * 0.333333f; }
    ImGui::SliderFloat("U0 (lattice speed)", &lbmParams.U0, 0.01f, 0.25f, "%.3f");
    ImGui::Checkbox("Smagorinsky LES Turbulence", &lbmParams.useTurbulence);
    if (lbmParams.useTurbulence) {
        ImGui::SliderFloat("Smagorinsky C", &lbmParams.smagorinskyC, 0.01f, 0.3f, "%.3f");
    }
    ImGui::Checkbox("MRT (High Re stability)", &lbmParams.useMRT);
    ImGui::Checkbox("Zou/He Inlet BC (accurate)", &lbmParams.useZouHeBC);
    ImGui::Checkbox("Convective Outlet", &lbmParams.useConvectiveOutlet);
    ImGui::SliderFloat("Inlet Turbulence", &lbmParams.inletTurbulence, 0.0f, 0.1f, "%.3f");
    ImGui::Checkbox("Ground (for cars)", &lbmParams.useGround);
    if (lbmParams.useGround) ImGui::SliderFloat("Ground Height##lbm", &lbmParams.groundHeight, -2.0f, 2.0f, "%.2f");
    ImGui::Separator();
    ImGui::Text("LBM Grid: %dx%dx%d = %d cells", lbmNx, lbmNy, lbmNz, lbmNx*lbmNy*lbmNz);
    ImGui::Text("Steps: %d | Converged: %s | TKE: %.4f", lbmCurrentStep, lbmConverged ? "YES" : "NO", lbmTKE);
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
    if (ImGui::Button("Compute Vorticity/Q/TKE")) { computeLBMVorticityAndQ(); }
    ImGui::Separator();
    ImGui::Text("Realistic features:");
    ImGui::BulletText("Zou/He inlet + convective outlet");
    ImGui::BulletText("Ground effect for cars");
    ImGui::BulletText("Inlet turbulence + TKE");
    ImGui::BulletText("MRT for high Re");
    ImGui::BulletText("Rainbow pressure & velocity (as photos)");
}

if (ImGui::CollapsingHeader("Compute")) {
    ImGui::RadioButton("CUDA", &useCUDA, 1);
    ImGui::SameLine();
    ImGui::RadioButton("CPU", &useCUDA, 0);
    if (ImGui::Button("Open Model")) {
        std::string p = openFileDialog();
        if (!p.empty()) loadModel(p);
    }
    ImGui::SameLine();
    if (ImGui::Button("FULL REBUILD (Clean)")) {
        // Подсказка пользователю — пересборка
        ImGui::OpenPopup("Rebuild Info");
    }
    if (ImGui::BeginPopup("Rebuild Info")) {
        ImGui::Text("For full rebuild run:");
        ImGui::Text("aeros\\build.bat x64 clean && build.bat x64");
        ImGui::Text("or tools\\rebuild-all.bat");
        ImGui::Text("This ensures aerodynamics tests changes are visible");
        if (ImGui::Button("Close")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

if (ImGui::CollapsingHeader("Test Mode (Physics + Code + LBM + Optimization)", ImGuiTreeNodeFlags_DefaultOpen)) {
    ImGui::Checkbox("Enable Test Mode", &testModeEnabled);
    ImGui::Checkbox("Continuous Validation", &testContinuous);
    ImGui::Text("Total: %d passed, %d failed", testsPassed, testsFailed);
    ImGui::Text("  Physics: %d / Code: %d / LBM: %d / Opt: %d passed", testsPassed - codeTestsPassed - lbmTestsPassed - 3, codeTestsPassed, lbmTestsPassed, 3);
    if (codeTestsFailed > 0) ImGui::Text("  Code failed: %d", codeTestsFailed);
    ImGui::Text("Last run: %.1f ms", lastTestTimeMs);
    if (lastGLError != 0) {
        ImGui::TextColored(ImVec4(1,0.3f,0.1f,1), "GL Error: %s", lastGLErrorStr.c_str());
    }
    if (testsFailed > 0) {
        ImGui::TextColored(ImVec4(1,0.2f,0.2f,1), "!!! ERRORS DETECTED: Physics=%d Code=%d LBM=%d !!!", testsFailed - codeTestsFailed - lbmTestsFailed, codeTestsFailed, lbmTestsFailed);
    } else if (testsPassed > 0) {
        ImGui::TextColored(ImVec4(0.2f,1,0.2f,1), "All tests passed — physics, code, LBM, optimization OK");
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
    if (ImGui::Button("Run Aero Tests")) {
        lastTestResults.clear(); testLog.clear(); testsPassed=0; testsFailed=0; codeTestsPassed=0; codeTestsFailed=0;
        runLBMTests(); runOptimizationTests();
    }
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
        int lbmPass = 0, lbmFail = 0;
        for (auto& r : lastTestResults) if (r.category=="LBM") { if (r.passed) lbmPass++; else lbmFail++; }
        if (ImGui::TreeNode(("LBM Tests (" + std::to_string(lbmPass) + "/" + std::to_string(lbmPass+lbmFail) + ")").c_str())) {
            for (auto& r : lastTestResults) if (r.category=="LBM") {
                ImVec4 col = r.passed ? ImVec4(0.2f,0.8f,1.0f,1) : ImVec4(1,0.5f,0.1f,1);
                ImGui::TextColored(col, "%s: %s", r.name.c_str(), r.passed ? "PASS" : "FAIL");
                if (!r.message.empty() && r.message != "OK" && r.message != "FAILED") {
                    ImGui::SameLine(); ImGui::Text(" - %s", r.message.c_str());
                }
            }
            ImGui::TreePop();
        }
        int optPass = 0, optFail = 0;
        for (auto& r : lastTestResults) if (r.category=="Optimization") { if (r.passed) optPass++; else optFail++; }
        if (ImGui::TreeNode(("Optimization Tests (" + std::to_string(optPass) + "/" + std::to_string(optPass+optFail) + ")").c_str())) {
            for (auto& r : lastTestResults) if (r.category=="Optimization") {
                ImVec4 col = r.passed ? ImVec4(0.8f,1.0f,0.2f,1) : ImVec4(1,0.2f,0.5f,1);
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
