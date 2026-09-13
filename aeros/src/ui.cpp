#include <imgui.h>

#include <string>

#include "globals.h"
#include "stl_loader.h"
#include "model.h"
#include "particles.h"
#include "streamlines.h"
#include "voxel_grid.h"
#include "ui.h"

// =====================================================
// Панель управления
// =====================================================
void drawUI() {
ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_Once);
ImGui::SetNextWindowSize(ImVec2(380, 720), ImGuiCond_Once);
ImGui::Begin("AeroS Control");
ImGui::Text("FPS: %.1f", 1.0f/deltaTime);
ImGui::Text("Vertices: %d", modelVertexCount);
ImGui::Text("Triangles: %d", modelVertexCount/3);
ImGui::Text("Voxel Grid: %dx%dx%d", g_voxNx, g_voxNy, g_voxNz);
ImGui::Separator();

if (ImGui::CollapsingHeader("Flow", ImGuiTreeNodeFlags_DefaultOpen)) {
    ImGui::SliderFloat("Speed", &flowSpeed, 0.0f, 10.0f);
    ImGui::SameLine(); ImGui::SetNextItemWidth(80);
    ImGui::InputFloat("##spd", &flowSpeed, 0, 0, "%.2f");
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

if (ImGui::CollapsingHeader("Particles", ImGuiTreeNodeFlags_DefaultOpen)) {
    ImGui::Checkbox("Show Particles", &showParticles);
    ImGui::SliderInt("Count", &numParticles, 100, 200000);
    // Перевыделяем буферы только когда слайдер отпущен
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
    ImGui::Text("Drag:  %.3f", dragMagnitude);
    ImGui::Text("Lift:  %.3f", liftMagnitude);
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
