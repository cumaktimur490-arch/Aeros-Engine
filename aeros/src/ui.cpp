#include <imgui.h>

#include <string>
#include <cstdio>

#include "globals.h"
#include "stl_loader.h"
#include "model.h"
#include "particles.h"
#include "streamlines.h"
#include "voxel_grid.h"
#include "atmosphere.h"
#include "ui.h"

// =====================================================
// Панель управления — v1.2.0: единицы скорости + атмосфера
// =====================================================
void drawUI() {
ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_Once);
ImGui::SetNextWindowSize(ImVec2(400, 800), ImGuiCond_Once);
ImGui::Begin("AeroS Control");
ImGui::Text("FPS: %.1f", 1.0f/deltaTime);
ImGui::Text("Vertices: %d", modelVertexCount);
ImGui::Text("Triangles: %d", modelVertexCount/3);
ImGui::Text("Voxel Grid: %dx%dx%d", g_voxNx, g_voxNy, g_voxNz);
ImGui::Separator();

if (ImGui::CollapsingHeader("Flow", ImGuiTreeNodeFlags_DefaultOpen)) {
    // Выбор единиц измерения скорости
    const char* unitItems[] = { "m/s (м/с)", "km/h (км/ч)", "mph (миль/ч)", "kts (узлы)", "ft/s (фут/с)" };
    int unitIdx = (int)speedUnit;
    if (ImGui::Combo("Speed Unit", &unitIdx, unitItems, IM_ARRAYSIZE(unitItems))) {
        // При смене единицы — конвертируем текущее значение чтобы сохранить физическую скорость
        speedUnit = (SpeedUnit)unitIdx;
    }

    // Показываем скорость в выбранных единицах, но храним внутри в м/с
    float displaySpeed = speedFromMS(flowSpeed, speedUnit);
    // Диапазоны в зависимости от единицы
    float maxDisplay = 10.0f;
    switch (speedUnit) {
        case SPEED_MS: maxDisplay = 10.0f; break;
        case SPEED_KMH: maxDisplay = 36.0f; break;
        case SPEED_MPH: maxDisplay = 22.0f; break;
        case SPEED_KNOTS: maxDisplay = 19.0f; break;
        case SPEED_FTS: maxDisplay = 33.0f; break;
        default: break;
    }

    char speedLabel[64];
    snprintf(speedLabel, sizeof(speedLabel), "Speed (%.2f %s)", displaySpeed, speedUnitShort(speedUnit));
    if (ImGui::SliderFloat("##speed", &displaySpeed, 0.0f, maxDisplay, "%.2f")) {
        flowSpeed = speedToMS(displaySpeed, speedUnit);
    }
    ImGui::SameLine(); ImGui::SetNextItemWidth(100);
    char inputFmt[32];
    snprintf(inputFmt, sizeof(inputFmt), "%%.2f %s", speedUnitShort(speedUnit));
    if (ImGui::InputFloat("##spd", &displaySpeed, 0, 0, inputFmt)) {
        if (displaySpeed < 0) displaySpeed = 0;
        flowSpeed = speedToMS(displaySpeed, speedUnit);
    }

    // Показываем конвертацию во все единицы для наглядности
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

    // Слайдер высоты в метрах
    ImGui::SliderFloat("Altitude (m)", &altitude, 0.0f, 20000.0f, "%.0f m");
    ImGui::SameLine(); ImGui::SetNextItemWidth(80);
    ImGui::InputFloat("##alt", &altitude, 0, 0, "%.0f");

    // Быстрые пресеты
    if (ImGui::Button("Sea Level")) altitude = 0.0f;
    ImGui::SameLine();
    if (ImGui::Button("5 km")) altitude = 5000.0f;
    ImGui::SameLine();
    if (ImGui::Button("10 km")) altitude = 10000.0f;
    ImGui::SameLine();
    if (ImGui::Button("15 km")) altitude = 15000.0f;

    // Показываем параметры атмосферы
    ImGui::Separator();
    ImGui::Text("Air Density: %.4f kg/m3", airDensity);
    ImGui::Text("Pressure: %.0f Pa (%.2f atm)", airPressure, airPressure/101325.0f);
    ImGui::Text("Temperature: %.1f K (%.1f C)", airTemperature, airTemperature-273.15f);
    ImGui::Text("Speed of Sound: %.1f m/s", speedOfSound);
    float mach = (speedOfSound > 1e-3f) ? flowSpeed / speedOfSound : 0.0f;
    ImGui::Text("Mach: %.3f", mach);

    // Таблица плотности по высотам
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

if (ImGui::CollapsingHeader("Compute")) {
    ImGui::RadioButton("CUDA", &useCUDA, 1);
    ImGui::SameLine();
    ImGui::RadioButton("CPU", &useCUDA, 0);
    if (ImGui::Button("Open Model")) {
        std::string p = openFileDialog();
        if (!p.empty()) loadModel(p);
    }
}
    ImGui::End();
}
