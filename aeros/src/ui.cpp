#include <imgui.h>

#include <iostream>
#include <string>
#include <cstdio>
#include <cmath>
#include <algorithm>
#include <chrono>
#include <sstream>
#include <thread>

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
#include "lang.h"
#include "fsr.h"
#include "framegen.h"
#include "interesting.h"
#include "lite_config.h"

// =====================================================
// Панель управления — v1.19.0 GoGonam AoS.
// =====================================================
void drawUI() {
ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_Once);
ImGui::SetNextWindowSize(ImVec2(540, 1150), ImGuiCond_Once);
ImGui::Begin(_TR("Aeros Control v1.19.0 GoGonam AoS.", "Управление Aeros v1.19.0 GoGonam AoS."));

ImGui::Text("%s: %.1f | %s: %.2f ms | v1.19.0 [%s]",
    _TR("FPS", "Кадров/с"), deltaTime > 1e-6f ? 1.0f/deltaTime : 0.0f,
    _TR("Frame", "Кадр"), perfFrameMs,
    getCurrentLanguageName());
ImGui::Text("%s: %d | %s: %d | %s: %dx%dx%d",
    _TR("Vertices", "Вершин"), modelVertexCount,
    _TR("Tris", "Треугольников"), modelVertexCount/3,
    _TR("Voxel", "Вокселей"), g_voxNx, g_voxNy, g_voxNz);
ImGui::Text("LBM: %dx%dx%d=%d | OpenMP: %d | %s", lbmNx, lbmNy, lbmNz, lbmNx*lbmNy*lbmNz, perfOpenMPThreads,
#ifdef _OPENMP
    "ON"
#else
    "OFF"
#endif
);
float machCurrent = flowSpeed/(speedOfSound+1e-6f);
ImGui::Text("%s: %s | Re: %.0f | Mach: %.3f | Mem: %.1f MB",
    _TR("Backend", "Движок"), lbmParams.enabled ? "LBM Realistic" : "Potential+Wake",
    aeroReNumber, machCurrent,
    (g_vertices.size()*4 + g_distanceField.size()*4 + particlePositions.size()*4)/1024.0f/1024.0f);
if (!aeroLastScreenshotPath.empty()) ImGui::Text("%s: %s", _TR("Last Screenshot", "Последний скриншот"), aeroLastScreenshotPath.c_str());
if (!aeroLastCSVPath.empty()) ImGui::Text("%s: %s", _TR("Last CSV", "Последний CSV"), aeroLastCSVPath.c_str());
ImGui::Separator();

// Language selector — always on top
if (ImGui::CollapsingHeader(_TR("Language / Язык", "Язык / Language"), ImGuiTreeNodeFlags_DefaultOpen)) {
    int langIdx = (int)currentLanguage;
    const char* langItems[] = {"English", "Русский"};
    if (ImGui::Combo(_TR("Language", "Язык"), &langIdx, langItems, 2)) {
        currentLanguage = (Language)langIdx;
        // Note: font with Cyrillic already loaded, but we may need to rebuild? ImGui handles it
        std::cout << "[Lang] Switched to: " << getCurrentLanguageName() << std::endl;
    }
    ImGui::Text("%s: %s", _TR("Current", "Текущий"), getCurrentLanguageName());
    ImGui::Separator();
    ImGui::TextWrapped("%s", _TR(
        "All UI elements support both English and Russian. Switch language anytime.",
        "Весь интерфейс поддерживает русский и английский. Переключайте язык в любой момент."));
}

if (ImGui::CollapsingHeader(_TR("Performance v1.19.0", "Производительность v1.19.0"), ImGuiTreeNodeFlags_DefaultOpen)) {
    ImGui::Text("%s: %.2f ms (%.1f FPS) | LBM: %.2f ms",
        _TR("Frame", "Кадр"), perfFrameMs, perfFrameMs>1e-3f?1000.0f/perfFrameMs:0, perfLBMms);
    ImGui::Text("%s: %.2f ms | %s: %.2f ms | %s: %.2f ms",
        _TR("Particles", "Частицы"), perfParticlesMs,
        _TR("Forces", "Силы"), perfForcesMs,
        _TR("Streamlines", "Линии тока"), perfStreamlinesMs);
    ImGui::Text("%s: %.2f ms", _TR("Voxel", "Воксель"), perfVoxelMs);
    if (lbmInitialized) {
        int total = lbmNx*lbmNy*lbmNz;
        float mlups = (total * lbmParams.stepsPerFrame) / (lbmTimeMs > 0 ? lbmTimeMs : 1.0f) / 1000.0f;
        ImGui::Text("LBM: %d %s | %.2f MLUPS | %.1f ms/step | TKE %.4f",
            total, _TR("cells", "ячеек"), mlups, lbmTimeMs, lbmTKE);
        ImGui::Text("Re: %.0f | Conv: %.2e | %s: %d | %s",
            lbmReynolds, lbmConvergence, _TR("Steps", "Шагов"), lbmCurrentStep, lbmConverged?_TR("YES","ДА"):_TR("NO","НЕТ"));
        float maxP = lbmPressure.empty()?0:*std::max_element(lbmPressure.begin(), lbmPressure.end());
        float minP = lbmPressure.empty()?0:*std::min_element(lbmPressure.begin(), lbmPressure.end());
        ImGui::Text("%s: [%.2f, %.2f] | MaxVel LB %.3f World %.2f m/s",
            _TR("Pressure", "Давление"), minP, maxP, lbmMaxVelocityLB, lbmMaxVelocityWorld);
    }
    ImGui::Checkbox(_TR("Show Perf Graph", "Показывать график"), &aeroShowPerfGraph);
    ImGui::Checkbox(_TR("Show Memory Usage", "Показывать память"), &aeroShowMemoryUsage);
    if (aeroShowPerfGraph) {
        static float frameHistory[100] = {0};
        static int histIdx = 0;
        frameHistory[histIdx] = perfFrameMs;
        histIdx = (histIdx+1)%100;
        char overlay[64];
        snprintf(overlay, sizeof(overlay), "%s %.1f ms", _TR("Frame", "Кадр"), perfFrameMs);
        ImGui::PlotLines(_TR("Frame Time", "Время кадра"), frameHistory, 100, histIdx, overlay, 0, 100, ImVec2(0,60));
    }
    ImGui::Separator();
    ImGui::Text("%s v1.19.0:", _TR("Features", "Возможности"));
    ImGui::BulletText("%s", _TR("Mach + Helicity + Total Pressure visualization", "Визуализация Маха + Спиральности + Полного давления"));
    ImGui::BulletText("%s", _TR("Screenshot BMP + CSV export + settings save/load", "Скриншот BMP + экспорт CSV + сохранение настроек"));
    ImGui::BulletText("%s", _TR("Adaptive LBM + RK4 particles + surface streamlines", "Адаптивный LBM + частицы RK4 + поверхностные линии тока"));
    ImGui::BulletText("%s", _TR("Multilingual EN/RU + Cyrillic font support", "Мультиязычность EN/RU + поддержка кириллицы"));
}

if (ImGui::CollapsingHeader(_TR("FSR & Optimizations v1.19.0 — Super Resolution", "FSR и Оптимизации v1.19.0 — Супер Разрешение"), ImGuiTreeNodeFlags_DefaultOpen)) {
    ImGui::Text("%s: %.2f ms | %s: %.2f ms | %s: %d tris culled",
        _TR("FSR", "ФСР"), perfFSRms,
        _TR("Culling", "Отсечение"), perfCullingMs,
        _TR("Culled", "Отсечено"), perfCulledTriangles);
    ImGui::Text("%s: %d particles culled | Scale: %.2f -> %dx%d",
        _TR("Particles", "Частицы"), perfCulledParticles,
        fsrCurrentScale,
        (int)(display_w*fsrCurrentScale), (int)(display_h*fsrCurrentScale));

    ImGui::Separator();
    ImGui::Text("%s:", _TR("AMD FSR 1.0 — Super Resolution", "AMD FSR 1.0 — Супер Разрешение"));
    ImGui::Checkbox(_TR("Enable FSR", "Включить FSR"), &fsrEnabled);
    if (fsrEnabled) {
        const char* fsrModesEn[] = {"Off (Native)", "Ultra Quality (77%)", "Quality (67%)", "Balanced (59%)", "Performance (50%)", "Ultra Perf (33%)"};
        const char* fsrModesRu[] = {"Выкл (Натив)", "Ультра Качество (77%)", "Качество (67%)", "Баланс (59%)", "Производительность (50%)", "Ультра Произв (33%)"};
        int modeIdx = (int)fsrMode;
        if (isRussian()) {
            if (ImGui::Combo(_TR("FSR Mode", "Режим FSR"), &modeIdx, fsrModesRu, IM_ARRAYSIZE(fsrModesRu))) {
                fsrMode = (FSRMode)modeIdx;
                fsrRenderScale = getFSRScale(modeIdx);
                if (!fsrDynamicRes) fsrCurrentScale = fsrRenderScale;
                resizeFSR(display_w, display_h);
            }
        } else {
            if (ImGui::Combo("FSR Mode", &modeIdx, fsrModesEn, IM_ARRAYSIZE(fsrModesEn))) {
                fsrMode = (FSRMode)modeIdx;
                fsrRenderScale = getFSRScale(modeIdx);
                if (!fsrDynamicRes) fsrCurrentScale = fsrRenderScale;
                resizeFSR(display_w, display_h);
            }
        }
        ImGui::SliderFloat(_TR("Sharpness (RCAS)", "Резкость (RCAS)"), &fsrSharpness, 0.0f, 1.0f, "%.2f");
        ImGui::Checkbox(_TR("Use RCAS Sharpening", "Использовать RCAS резкость"), &fsrUseRCAS);
        ImGui::Checkbox(_TR("Dynamic Resolution", "Динамическое разрешение"), &fsrDynamicRes);
        if (fsrDynamicRes) {
            ImGui::SliderFloat(_TR("Target FPS", "Целевой FPS"), &fsrTargetFPS, 15.0f, 144.0f, "%.0f");
            ImGui::Text("Current Scale: %.3f (%.0f%%)", fsrCurrentScale, fsrCurrentScale*100.0f);
        }
        ImGui::Checkbox(_TR("Show FSR Debug", "Показывать отладку FSR"), &fsrShowDebug);
        if (fsrShowDebug) {
            ImGui::Text("Render: %dx%d -> Display: %dx%d",
                (int)(display_w*fsrCurrentScale), (int)(display_h*fsrCurrentScale),
                display_w, display_h);
            ImGui::Text("Gain: %.2fx pixels, %.1f%% perf",
                1.0f/(fsrCurrentScale*fsrCurrentScale),
                (1.0f - fsrCurrentScale*fsrCurrentScale)*100.0f);
        }
    }

    ImGui::Separator();
    ImGui::Text("%s:", _TR("General Optimizations", "Общие оптимизации"));
    ImGui::Checkbox(_TR("Frustum Culling", "Отсечение пирамидой"), &optFrustumCulling);
    ImGui::Checkbox(_TR("Early-Z (opaque first)", "Early-Z (непрозрачные первыми)"), &optEarlyZ);
    ImGui::Checkbox(_TR("LOD System", "Система LOD"), &optLOD);
    ImGui::Checkbox(_TR("Dynamic Particles LOD", "Динамический LOD частиц"), &optDynamicParticles);
    ImGui::Checkbox(_TR("VRS (periphery low-res)", "VRS (периферия низкое разрешение)"), &optVRS);
    ImGui::Checkbox(_TR("Async Compute (LBM thread)", "Асинхронные вычисления (LBM поток)"), &optAsyncCompute);
    ImGui::Checkbox(_TR("Meshlet Culling", "Отсечение мешлетов"), &optMeshletCulling);
    if (optLOD) {
        ImGui::SliderFloat(_TR("LOD Distance", "Дистанция LOD"), &optLODDistance, 0.5f, 10.0f, "%.1f");
    }
    if (optDynamicParticles) {
        const char* lodNamesEn[] = {"Full", "Half", "Quarter"};
        const char* lodNamesRu[] = {"Полный", "Половина", "Четверть"};
        if (isRussian()) ImGui::Combo(_TR("Particle LOD", "LOD частиц"), &optParticleLOD, lodNamesRu, IM_ARRAYSIZE(lodNamesRu));
        else ImGui::Combo("Particle LOD", &optParticleLOD, lodNamesEn, IM_ARRAYSIZE(lodNamesEn));
    }

    ImGui::Separator();
    ImGui::Text("%s:", _TR("Frame Pacing & VSync", "Синхронизация кадров"));
    ImGui::Checkbox(_TR("VSync", "Верт. синхронизация"), &vsyncEnabled);
    ImGui::Checkbox(_TR("Limit FPS", "Ограничить FPS"), &limitFPS);
    if (limitFPS) ImGui::SliderFloat(_TR("Max FPS", "Макс FPS"), &maxFPS, 15.0f, 240.0f, "%.0f");
    ImGui::Checkbox(_TR("Frame Pacing", "Плавность кадров"), &optFramePacing);
    if (optFramePacing) ImGui::SliderFloat(_TR("Target FPS", "Целевой FPS"), &optTargetFPS, 15.0f, 144.0f, "%.0f");

    ImGui::Separator();
    ImGui::BulletText("%s", _TR("FSR: render low-res, upscale with EASU+RCAS", "FSR: рендер в низком разрешении, апскейл EASU+RCAS"));
    ImGui::BulletText("%s", _TR("Frustum culling: skip off-screen", "Отсечение: пропуск вне экрана"));
    ImGui::BulletText("%s", _TR("Early-Z: opaque first, less overdraw", "Early-Z: непрозрачные первыми, меньше перерисовок"));
    ImGui::BulletText("%s", _TR("Dynamic LOD: less particles when far/slow", "Динамический LOD: меньше частиц вдали/при лагах"));
}

if (ImGui::CollapsingHeader(_TR("Frame Generation v1.19.0 — Custom FG", "Генерация кадров v1.19.0 — Собственная ГП"), ImGuiTreeNodeFlags_DefaultOpen)) {
    ImGui::Text("%s: %.2f ms | Motion: %.2f ms | Interp: %.2f ms | Eff FPS: %.1f",
        _TR("FG", "ГП"), perfFGms, perfMotionMs, perfInterpMs, fgEffectiveFPS);
    ImGui::Text("%s: %d real + %d gen = %d total | %s: %s",
        _TR("Frames", "Кадры"), fgRealCount, fgGeneratedCount, fgRealCount+fgGeneratedCount,
        _TR("History", "История"), fgHasHistory ? _TR("YES","ДА") : _TR("NO","НЕТ"));

    ImGui::Separator();
    ImGui::Text("%s:", _TR("Custom Frame Generation — like DLSS FG / FSR FG", "Собственная генерация кадров — как DLSS FG / FSR FG"));
    ImGui::Checkbox(_TR("Enable Frame Generation", "Включить генерацию кадров"), &fgEnabled);
    if (fgEnabled) {
        const char* fgModesEn[] = {"Off", "2x — 1 gen / 2x FPS", "3x — 2 gen / 3x FPS", "4x — 3 gen / 4x FPS"};
        const char* fgModesRu[] = {"Выкл", "2x — 1 сген / 2x FPS", "3x — 2 сген / 3x FPS", "4x — 3 сген / 4x FPS"};
        int fgIdx = (int)fgMode;
        if (isRussian()) {
            if (ImGui::Combo(_TR("FG Mode", "Режим ГП"), &fgIdx, fgModesRu, IM_ARRAYSIZE(fgModesRu))) {
                fgMode = (FGMode)fgIdx;
            }
        } else {
            if (ImGui::Combo("FG Mode", &fgIdx, fgModesEn, IM_ARRAYSIZE(fgModesEn))) {
                fgMode = (FGMode)fgIdx;
            }
        }
        ImGui::SliderFloat(_TR("Blend Strength", "Сила бленда"), &fgBlendStrength, 0.0f, 1.0f, "%.2f");
        ImGui::Checkbox(_TR("Use Motion Vectors (camera)", "Использовать векторы движения (камера)"), &fgUseMotionVectors);
        ImGui::Checkbox(_TR("Use Optical Flow (fallback)", "Использовать оптический поток (запасной)"), &fgUseOpticalFlow);
        ImGui::Checkbox(_TR("Low Latency (Reflex-like)", "Низкая задержка (как Reflex)"), &fgLowLatency);
        ImGui::Checkbox(_TR("Async Generation", "Асинхронная генерация"), &fgAsync);
        ImGui::Checkbox(_TR("Show FG Debug (motion/occlusion)", "Показывать отладку ГП (движение/окклюзия)"), &fgShowDebug);
        if (fgShowDebug) {
            ImGui::Text("%s: red=occlusion, gray=motion length", _TR("Debug", "Отладка"));
        }
        ImGui::Separator();
        ImGui::Text("Multiplier: %dx | Effective: %.1f FPS (from %.1f real)",
            getFGMultiplier((int)fgMode), fgEffectiveFPS, deltaTime>1e-6f?1.0f/deltaTime:0);
        ImGui::Text("Real: %d | Generated: %d | Total: %d",
            fgRealCount, fgGeneratedCount, fgRealCount+fgGeneratedCount);
    }

    ImGui::Separator();
    ImGui::Text("%s:", _TR("How it works", "Как работает"));
    ImGui::BulletText("%s", _TR("1. Render real frame to FG FBO (with FSR if enabled)", "1. Рендер реального кадра в FBO ГП (с FSR если вкл)"));
    ImGui::BulletText("%s", _TR("2. Compute motion vectors from depth + camera matrices", "2. Вычисление векторов движения из глубины + матриц камеры"));
    ImGui::BulletText("%s", _TR("3. Generate interpolated frames at alpha=0.33/0.5/0.66", "3. Генерация интерполированных кадров при alpha=0.33/0.5/0.66"));
    ImGui::BulletText("%s", _TR("4. Motion-compensated warp + occlusion handling + clamp", "4. Компенсация движения + обработка окклюзий + ограничение"));
    ImGui::BulletText("%s", _TR("5. Present generated + real with frame pacing", "5. Показ сгенерированных + реальных с pacing"));
    ImGui::BulletText("%s", _TR("Like DLSS FG but custom — no tensor cores needed", "Как DLSS FG но своё — без тензорных ядер"));
}

if (ImGui::CollapsingHeader(_TR("Realistic Aero v1.19.0 — Ultra Photo Mode", "Реалистичная Аэро v1.19.0 — Ультра Фото Режим"), ImGuiTreeNodeFlags_DefaultOpen)) {
    const char* visModesEn[] = {
        "Pressure Cp (NASCAR rainbow)",
        "Velocity |U| (UAV/Cybertruck)",
        "Vorticity |w|",
        "Q-Criterion (vortices)",
        "Turbulent TKE",
        "Skin Friction Cf",
        "Boundary Layer",
        "Mach Number (NEW v1.9.0)",
        "Helicity (NEW v1.9.0)",
        "Total Pressure (NEW v1.9.0)",
        "Schlieren |∇ρ| (NEW v1.19.0) — wind tunnel",
        "Shadowgraph ∇²ρ (NEW v1.19.0)",
        "Volumetric Smoke (NEW v1.19.0)",
        "Shock Waves (NEW v1.19.0)",
        "Aeroacoustics Lighthill (NEW v1.19.0)",
        "Temperature (NEW v1.19.0)",
        "LIC Surface (NEW v1.19.0)"
    };
    const char* visModesRu[] = {
        "Давление Cp (NASCAR радуга)",
        "Скорость |U| (UAV/Cybertruck)",
        "Завихренность |w|",
        "Q-Критерий (вихри)",
        "Турбулентная энергия TKE",
        "Трение Cf",
        "Погранслой",
        "Число Маха (НОВОЕ v1.9.0)",
        "Спиральность (НОВОЕ v1.9.0)",
        "Полное давление (НОВОЕ v1.9.0)",
        "Шлирен |∇ρ| (НОВОЕ v1.19.0) — как в трубе",
        "Теневой ∇²ρ (НОВОЕ v1.19.0)",
        "Объемный дым (НОВОЕ v1.19.0)",
        "Скачки уплотнения (НОВОЕ v1.19.0)",
        "Аэроакустика Lighthill (НОВОЕ v1.19.0)",
        "Температура (НОВОЕ v1.19.0)",
        "LIC Surface (НОВОЕ v1.19.0)"
    };
    int visIdx = (int)aeroVisMode;
    if (isRussian()) {
        if (ImGui::Combo(_TR("Visualization", "Визуализация"), &visIdx, visModesRu, IM_ARRAYSIZE(visModesRu))) {
            aeroVisMode = (AeroVisMode)visIdx;
            if (showPressure) updateVertexColors();
        }
    } else {
        if (ImGui::Combo("Visualization", &visIdx, visModesEn, IM_ARRAYSIZE(visModesEn))) {
            aeroVisMode = (AeroVisMode)visIdx;
            if (showPressure) updateVertexColors();
        }
    }
    const char* cmapModesEn[] = { "Rainbow (NASCAR/UAV)", "Viridis", "Parula", "CoolWarm" };
    const char* cmapModesRu[] = { "Радуга (NASCAR/UAV)", "Viridis", "Parula", "CoolWarm" };
    if (isRussian()) {
        if (ImGui::Combo("Цветовая схема", &aeroColorMap, cmapModesRu, IM_ARRAYSIZE(cmapModesRu))) {
            if (showPressure) updateVertexColors();
            computeStreamlines();
        }
    } else {
        if (ImGui::Combo("Color Map", &aeroColorMap, cmapModesEn, IM_ARRAYSIZE(cmapModesEn))) {
            if (showPressure) updateVertexColors();
            computeStreamlines();
        }
    }
    ImGui::Checkbox(_TR("Color Streamlines by Velocity (photo 2)", "Окраска линий тока по скорости (фото 2)"), &aeroColorStreamlinesByVelocity);
    ImGui::Checkbox(_TR("Highlight Flow Separation", "Подсветка отрыва потока"), &aeroShowSeparation);
    ImGui::Checkbox(_TR("Show Wake (photo 5)", "Показывать след (фото 5)"), &aeroShowWake);
    if (aeroShowWake) ImGui::SliderFloat(_TR("Wake Opacity", "Прозрачность следа"), &aeroWakeOpacity, 0.0f, 1.0f);
    ImGui::Checkbox(_TR("Show Vortices (Q)", "Показывать вихри (Q)"), &aeroShowVortices);
    ImGui::Checkbox(_TR("Show Boundary Layer", "Показывать погранслой"), &aeroShowBoundaryLayer);
    ImGui::Checkbox(_TR("Realistic PBR Lighting", "Реалистичное PBR освещение"), &aeroUseRealisticLighting);
    ImGui::Checkbox(_TR("Ground Effect (cars, photo 2/5)", "Эффект земли (авто, фото 2/5)"), &aeroGroundEffect);
    if (aeroGroundEffect) {
        ImGui::SliderFloat(_TR("Ground Height", "Высота земли"), &aeroGroundHeight, -2.0f, 2.0f, "%.2f");
        lbmParams.useGround = aeroGroundEffect;
        lbmParams.groundHeight = aeroGroundHeight;
    }
    ImGui::Checkbox(_TR("Show Ground Plane", "Показывать землю"), &showGroundPlane);
    if (showGroundPlane) {
        ImGui::SliderFloat(_TR("Ground Alpha", "Прозрачность земли"), &groundAlpha, 0.1f, 1.0f);
        ImGui::ColorEdit3(_TR("Ground Color", "Цвет земли"), &groundColor[0]);
    }
    ImGui::Checkbox(_TR("Show Slice Plane (Cybertruck)", "Показывать срез (Cybertruck)"), &showSlicePlane);
    if (showSlicePlane) {
        ImGui::Checkbox(_TR("Enable Aero Slice", "Включить аэро-срез"), &aeroShowSlice);
        const char* axisNames[] = {"X","Y","Z"};
        ImGui::Combo(_TR("Slice Axis", "Ось среза"), &aeroSliceAxis, axisNames, 3);
        ImGui::SliderFloat(_TR("Slice Pos", "Позиция среза"), &aeroSlicePos, 0.0f, 1.0f);
    }
    ImGui::Checkbox(_TR("Mach Effects (compressibility)", "Эффекты Маха (сжимаемость)"), &aeroMachEffects);
    if (aeroMachEffects) {
        ImGui::SliderFloat(_TR("Mach Threshold", "Порог Маха"), &aeroMachThreshold, 0.1f, 1.0f);
    }
    ImGui::Separator();
    ImGui::Text("%s:", _TR("v1.9.0 New Features", "Новые фичи v1.9.0"));
    ImGui::Checkbox(_TR("Particle Trails", "Следы частиц"), &aeroShowParticleTrails);
    if (aeroShowParticleTrails) {
        ImGui::SliderFloat(_TR("Trail Length", "Длина следа"), &aeroTrailLength, 0.1f, 2.0f);
        ImGui::SliderFloat(_TR("Trail Opacity", "Прозрачность следа"), &aeroParticleTrailOpacity, 0.1f, 1.0f);
    }
    ImGui::Checkbox(_TR("Surface Streamlines", "Поверхностные линии тока"), &aeroSurfaceStreamlines);
    ImGui::Checkbox(_TR("Use RK4 for Particles (accurate)", "Использовать RK4 для частиц (точно)"), &aeroUseRK4Particles);
    ImGui::Checkbox(_TR("Adaptive LBM Steps", "Адаптивные шаги LBM"), &aeroAdaptiveLBM);
    ImGui::Checkbox(_TR("Show Helicity", "Показывать спиральность"), &aeroShowHelicity);
    ImGui::Checkbox(_TR("Show Total Pressure", "Показывать полное давление"), &aeroShowTotalPressure);
    ImGui::Separator();
    ImGui::Text("%s:", _TR("Ref Area for Cd/Cl", "Площадь для Cd/Cl"));
    ImGui::Checkbox(_TR("Auto Ref Area", "Авто площадь"), &aeroAutoRefArea);
    if (!aeroAutoRefArea) ImGui::SliderFloat(_TR("Ref Area", "Площадь"), &aeroRefArea, 0.01f, 20.0f, "%.3f m2");
    else ImGui::Text("Auto: %.4f m2 (from BB) | Re=%.0f", aeroRefArea, aeroReNumber);
    ImGui::Separator();
    ImGui::Text("%s:", _TR("Presets (like photos)", "Пресеты (как на фото)"));
    if (ImGui::Button(_TR("Car Preset (NASCAR/Cybertruck)", "Пресет Авто (NASCAR/Cybertruck)"))) {
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
    if (ImGui::Button(_TR("UAV Preset (photo 1)", "Пресет БПЛА (фото 1)"))) {
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
    if (ImGui::Button(_TR("Airfoil Preset (photo 3)", "Пресет Профиль (фото 3)"))) {
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
    if (ImGui::Button(_TR("Turbulent Wake (photo 5)", "Турбулентный след (фото 5)"))) {
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
    if (ImGui::Button(_TR("Mach Preset (NEW)", "Пресет Мах (НОВОЕ)"))) {
        aeroVisMode = AeroVisMode::MachNumber;
        aeroColorMap = 0;
        aeroMachEffects = true;
        aeroShowMach = true;
        flowSpeed = 100.0f;
        updateVertexColors(); computeStreamlines();
    }
    ImGui::SameLine();
    if (ImGui::Button(_TR("Helicity Preset (NEW)", "Пресет Спиральность (НОВОЕ)"))) {
        aeroVisMode = AeroVisMode::Helicity;
        aeroColorMap = 0;
        aeroShowHelicity = true;
        lbmParams.enabled = true;
        if (lbmInitialized) resetLBM(); else initLBM();
        updateVertexColors(); computeStreamlines();
    }
    if (ImGui::Button(_TR("Viridis Style", "Стиль Viridis"))) {
        aeroColorMap = 1;
        updateVertexColors(); computeStreamlines();
    }
    ImGui::SameLine();
    if (ImGui::Button(_TR("Enable Realistic LBM", "Включить реалистичный LBM"))) {
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
    if (ImGui::Button(_TR("Disable LBM", "Отключить LBM"))) {
        lbmParams.enabled = false;
        shutdownLBM();
    }
    ImGui::Separator();
    ImGui::Text("%s:", _TR("NEW v1.19.0 Interesting Presets", "НОВОЕ v1.19.0 Интересные пресеты"));
    if (ImGui::Button(_TR("Schlieren Wind Tunnel", "Шлирен Аэр труба"))) {
        aeroVisMode = AeroVisMode::Schlieren;
        aeroSchlierenEnabled = true;
        aeroSchlierenSensitivity = 1.2f;
        aeroSchlierenColor = true;
        aeroColorMap = 0;
        updateVertexColors();
    }
    ImGui::SameLine();
    if (ImGui::Button(_TR("Shock Waves (Supersonic)", "Скачки (Сверхзвук)"))) {
        aeroVisMode = AeroVisMode::ShockWaves;
        aeroShowShockWaves = true;
        aeroShowMachCone = true;
        flowSpeed = 400.0f;
        aeroMachEffects = true;
        updateVertexColors();
    }
    ImGui::SameLine();
    if (ImGui::Button(_TR("Volumetric Smoke", "Объемный дым"))) {
        aeroVisMode = AeroVisMode::VolumetricSmoke;
        aeroVolumetricEnabled = true;
        aeroSmokeDensity = 1.0f;
        aeroSmokeOpacity = 0.7f;
        aeroSmokeInjectors = 5;
        showParticles = true;
    }
    if (ImGui::Button(_TR("Vortex Tubes", "Вихревые трубки"))) {
        aeroShowVortexTubes = true;
        aeroVortexTubeCount = 20;
        aeroVortexTubeRadius = 0.03f;
        aeroVisMode = AeroVisMode::QCriterion;
        updateVertexColors();
    }
    ImGui::SameLine();
    if (ImGui::Button(_TR("Flight Mode", "Режим полета"))) {
        aeroFlightMode = !aeroFlightMode;
        if (aeroFlightMode) {
            aeroFlightMass = 1.0f;
            aeroFlightThrust = 5.0f;
            resetFlightDynamics();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button(_TR("Aeroacoustic Noise", "Аэроакустика"))) {
        aeroVisMode = AeroVisMode::AeroAcoustic;
        aeroShowAeroAcoustic = true;
        updateVertexColors();
    }
    if (ImGui::Button(_TR("Temperature (Heating)", "Температура (Нагрев)"))) {
        aeroVisMode = AeroVisMode::Temperature;
        aeroShowTemperature = true;
        updateVertexColors();
    }
    ImGui::SameLine();
    if (ImGui::Button(_TR("LIC Surface Flow", "LIC Поверхность"))) {
        aeroVisMode = AeroVisMode::LIC;
        aeroShowLIC = true;
        updateVertexColors();
    }
}

if (ImGui::CollapsingHeader(_TR("Interesting v1.19.0 — Schlieren, Smoke, Shocks, Vortex, Flight", "Интересное v1.19.0 — Шлирен, Дым, Скачки, Вихри, Полет"), ImGuiTreeNodeFlags_DefaultOpen)) {
    ImGui::Text("%s", _TR("Real wind tunnel visualizations & flight dynamics", "Реальные визуализации из трубы и динамика полета"));
    ImGui::Separator();
    ImGui::Text("%s:", _TR("Schlieren & Shadowgraph (real wind tunnel optics)", "Шлирен и теневой (реальная оптика трубы)"));
    ImGui::Checkbox(_TR("Enable Schlieren", "Включить шлирен"), &aeroSchlierenEnabled);
    if (aeroSchlierenEnabled) {
        ImGui::SliderFloat(_TR("Sensitivity", "Чувствительность"), &aeroSchlierenSensitivity, 0.1f, 5.0f, "%.2f");
        ImGui::SliderFloat(_TR("Cutoff", "Отсечка"), &aeroSchlierenCutoff, 0.0f, 1.0f);
        ImGui::Checkbox(_TR("Color Schlieren", "Цветной шлирен"), &aeroSchlierenColor);
        const char* schlModes[] = {"Schlieren |∇ρ|", "Shadowgraph ∇²ρ"};
        ImGui::Combo(_TR("Mode", "Режим"), &aeroSchlierenMode, schlModes, 2);
        ImGui::Text("  %s: |∇ρ| ~ |∇p|/(R T) — shows density gradients", _TR("Physics", "Физика"));
        ImGui::Text("  %s: knife edge blocks 50%% light, gradient shifts image", _TR("Optics", "Оптика"));
    }
    ImGui::Separator();
    ImGui::Text("%s:", _TR("Volumetric Smoke Tunnel", "Объемная дымовая труба"));
    ImGui::Checkbox(_TR("Volumetric Smoke", "Объемный дым"), &aeroVolumetricEnabled);
    if (aeroVolumetricEnabled) {
        ImGui::SliderFloat(_TR("Smoke Density", "Плотность дыма"), &aeroSmokeDensity, 0.1f, 3.0f);
        ImGui::SliderFloat(_TR("Smoke Opacity", "Прозрачность"), &aeroSmokeOpacity, 0.1f, 1.0f);
        ImGui::SliderFloat(_TR("Buoyancy", "Плавучесть"), &aeroSmokeBuoyancy, 0.0f, 0.5f);
        ImGui::SliderFloat(_TR("Dissipation", "Диссипация"), &aeroSmokeDissipation, 0.8f, 0.999f, "%.3f");
        ImGui::SliderFloat(_TR("Turbulence", "Турбулентность"), &aeroSmokeTurbulence, 0.0f, 1.0f);
        ImGui::SliderInt(_TR("Injectors", "Инжекторы"), &aeroSmokeInjectors, 1, 10);
        ImGui::Checkbox(_TR("Volumetric Light", "Объемный свет"), &aeroSmokeVolumetricLight);
        ImGui::Text("  Perf: %.2f ms | Particles: %zu", perfVolumetricMs, g_smokeParticles.size());
        if (ImGui::Button(_TR("Reset Smoke", "Сбросить дым"))) initSmokeTunnel();
    }
    ImGui::Separator();
    ImGui::Text("%s:", _TR("Shock Waves — Supersonic (θ-β-M)", "Скачки — Сверхзвук (θ-β-M)"));
    ImGui::Checkbox(_TR("Show Shock Waves", "Показывать скачки"), &aeroShowShockWaves);
    if (aeroShowShockWaves) {
        ImGui::SliderFloat(_TR("Shock Opacity", "Прозрачность скачка"), &aeroShockOpacity, 0.1f, 1.0f);
        ImGui::Checkbox(_TR("Mach Cone", "Конус Маха"), &aeroShowMachCone);
        ImGui::Checkbox(_TR("Expansion Fans", "Волны разрежения"), &aeroShowExpansionFans);
        float mach = flowSpeed / (speedOfSound+1e-6f);
        float mu = (mach>1.0f) ? asinf(1.0f/mach)*57.29578f : 90.0f;
        ImGui::Text("  Mach: %.2f | μ (Mach angle): %.1f° | β (shock): %.1f°", mach, mu, aeroShockAngle);
        ImGui::Text("  Physics: tanθ = 2cotβ(M²sin²β-1)/(M²(γ+cos2β)+2)");
        ImGui::Text("  Prandtl-Meyer: ν(M)=√((γ+1)/(γ-1)) atan(√((γ-1)/(γ+1)(M²-1))) - atan(√(M²-1))");
    }
    ImGui::Separator();
    ImGui::Text("%s:", _TR("Vortex Tubes — Q-criterion & λ2", "Вихревые трубки — Q и λ2"));
    ImGui::Checkbox(_TR("Show Vortex Tubes", "Показывать трубки"), &aeroShowVortexTubes);
    if (aeroShowVortexTubes) {
        ImGui::SliderFloat(_TR("Tube Radius", "Радиус трубки"), &aeroVortexTubeRadius, 0.005f, 0.1f, "%.3f");
        ImGui::SliderFloat(_TR("Opacity", "Прозрачность"), &aeroVortexTubeOpacity, 0.1f, 1.0f);
        ImGui::SliderInt(_TR("Count", "Количество"), &aeroVortexTubeCount, 1, 50);
        ImGui::SliderFloat(_TR("Helicity Scale", "Масштаб спиральности"), &aeroVortexHelicityScale, 0.1f, 3.0f);
        ImGui::Text("  Cores: %zu | Perf: %.2f ms | Q>5, |ω|>√10", g_vortexCores.size(), perfVortexMs);
        if (ImGui::Button(_TR("Extract Vortex Cores", "Найти вихри"))) extractVortexCores();
    }
    ImGui::Separator();
    ImGui::Text("%s:", _TR("Flight Dynamics 6DOF — model really flies!", "Динамика полета 6DOF — модель реально летит!"));
    ImGui::Checkbox(_TR("Flight Mode", "Режим полета"), &aeroFlightMode);
    if (aeroFlightMode) {
        ImGui::SliderFloat(_TR("Mass (kg)", "Масса (кг)"), &aeroFlightMass, 0.1f, 10.0f, "%.2f");
        ImGui::SliderFloat(_TR("Thrust (N)", "Тяга (Н)"), &aeroFlightThrust, 0.0f, 20.0f, "%.1f");
        ImGui::SliderFloat(_TR("Inertia", "Инерция"), &aeroFlightInertia, 0.1f, 5.0f);
        ImGui::Checkbox(_TR("Auto Trim", "Авто-триммирование"), &aeroFlightAutoTrim);
        ImGui::Text("  Pos: (%.2f, %.2f, %.2f) | Vel: %.2f m/s", aeroFlightPos.x, aeroFlightPos.y, aeroFlightPos.z, aeroFlightVelocity);
        ImGui::Text("  Angles: pitch %.1f° yaw %.1f° roll %.1f°", aeroFlightAngles.x*57.29578f, aeroFlightAngles.y*57.29578f, aeroFlightAngles.z*57.29578f);
        ImGui::Text("  Alt: %.2f m | %s", aeroFlightAltitude, isFlightFlying() ? "FLYING!" : g_flightState.onGround ? "ON GROUND" : "FALLING");
        if (ImGui::Button(_TR("Reset Flight", "Сброс полета"))) resetFlightDynamics();
        ImGui::SameLine();
        if (ImGui::Button(_TR("Takeoff!", "Взлет!"))) { aeroFlightThrust = 10.0f; g_flightState.velocity.y = 2.0f; }
    }
    ImGui::Separator();
    ImGui::Text("%s:", _TR("LIC — Line Integral Convolution", "LIC — Интегральная свертка"));
    ImGui::Checkbox(_TR("Show LIC", "Показывать LIC"), &aeroShowLIC);
    if (aeroShowLIC) {
        ImGui::SliderFloat(_TR("Strength", "Сила"), &aeroLICStrength, 0.1f, 2.0f);
        ImGui::SliderInt(_TR("Steps", "Шаги"), &aeroLICSteps, 5, 100);
        ImGui::SliderFloat(_TR("LIC Opacity", "Прозрачность"), &aeroLICOpacity, 0.1f, 1.0f);
        if (ImGui::Button(_TR("Compute LIC", "Вычислить LIC"))) computeLICOnSurface();
    }
    ImGui::Separator();
    ImGui::Text("%s:", _TR("Aeroacoustics — Lighthill analogy", "Аэроакустика — аналогия Лайтхилла"));
    ImGui::Checkbox(_TR("Show Aeroacoustic", "Показывать шум"), &aeroShowAeroAcoustic);
    if (aeroShowAeroAcoustic) {
        ImGui::SliderFloat(_TR("Acoustic Opacity", "Прозрачность"), &aeroAcousticOpacity, 0.1f, 1.0f);
        ImGui::SliderFloat(_TR("Freq Scale", "Масштаб частоты"), &aeroAcousticFreq, 100.0f, 10000.0f, "%.0f Hz");
        ImGui::Text("  Sources: %zu | T_ij = ρ v_i v_j + (p-c²ρ)δ_ij", g_acousticSources.size());
        if (ImGui::Button(_TR("Compute Noise Sources", "Найти источники шума"))) computeAeroacousticSources();
    }
    ImGui::Separator();
    ImGui::Text("%s:", _TR("Streaklines & Other", "Линии окрашенного дыма и др."));
    ImGui::Checkbox(_TR("Show Streaklines", "Показывать streaklines"), &aeroShowStreaklines);
    if (aeroShowStreaklines) {
        ImGui::SliderInt(_TR("History", "История"), &aeroStreakHistory, 10, 200);
        ImGui::SliderFloat(_TR("Streak Opacity", "Прозрачность"), &aeroStreakOpacity, 0.1f, 1.0f);
        if (ImGui::Button(_TR("Clear Streaks", "Очистить"))) clearStreaklines();
    }
    ImGui::Checkbox(_TR("Show Temperature", "Показывать температуру"), &aeroShowTemperature);
}

if (ImGui::CollapsingHeader(_TR("Lite — Weak Devices i3-3xxx / HD 4000 / Ultra-Lite (NEW v1.20.1)", "Lite — Слабые устройства i3-3xxx / HD 4000 / Ultra-Lite (НОВОЕ v1.20.1)"), ImGuiTreeNodeFlags_DefaultOpen)) {
    ImGui::Text("%s", getLiteInfoString());
    ImGui::Text("%s: %s", _TR("Lite Mode", "Lite режим"), isLiteMode() ? (isUltraLiteMode() ? "ULTRA-LITE Potato" : "ON — Optimized for weak devices") : "OFF — Full version");
    ImGui::Text("%s: %d", _TR("HW Threads", "Потоков CPU"), std::thread::hardware_concurrency());
    ImGui::Text("%s: %d", _TR("Max Threads", "Макс потоков"), getLiteMaxThreads() >0 ? getLiteMaxThreads() : (int)std::thread::hardware_concurrency());
    ImGui::Text("%s: %s", _TR("Battery", "Батарея"), isBatteryPower() ? "On battery — saver ON" : "On AC");
    ImGui::Text("%s: %s", _TR("Low RAM", "Мало RAM"), isLowMemorySystem() ? "Yes <4GB — Lite recommended" : "No");
    ImGui::Text("%s: %s", _TR("Detected", "Определено"), getPresetName(detectHardwarePreset()));
    ImGui::Text("%s: %s", _TR("Current Preset", "Текущий пресет"), getPresetName(g_currentPreset));
    ImGui::Text("VRAM est: %.1f MB, RAM est: %.1f MB, FPS avg: %.1f", getEstimatedVRAMUsageMB(), getEstimatedRAMUsageMB(), g_currentFPSAverage);
    ImGui::Separator();
    ImGui::Text("%s:", _TR("Presets", "Пресеты"));
    if (ImGui::Button("Potato (Ultra-Lite)")) { applyPreset(LiteQualityPreset::Potato); }
    ImGui::SameLine();
    if (ImGui::Button("Low (Lite)")) { applyPreset(LiteQualityPreset::Low); }
    ImGui::SameLine();
    if (ImGui::Button("Medium")) { applyPreset(LiteQualityPreset::Medium); }
    ImGui::SameLine();
    if (ImGui::Button("Full")) { applyPreset(LiteQualityPreset::Full); g_isLiteMode=false; }
    ImGui::Text("%s: %s", _TR("Potato desc", "Potato"), getPresetDescription(LiteQualityPreset::Potato));
    ImGui::Text("%s: %s", _TR("Low desc", "Low"), getPresetDescription(LiteQualityPreset::Low));
    ImGui::Text("%s: %s", _TR("Medium desc", "Medium"), getPresetDescription(LiteQualityPreset::Medium));
    ImGui::Text("%s: %s", _TR("Full desc", "Full"), getPresetDescription(LiteQualityPreset::Full));
    ImGui::Separator();
    ImGui::Text("%s:", _TR("Lite Optimizations for i3-3xxx / HD 4000 / GT 620M", "Оптимизации для i3-3xxx / HD 4000 / GT 620M"));
    ImGui::BulletText("SSE2 only (no AVX2) — Ivy Bridge compatible");
    ImGui::BulletText("O1 (not O3) — smaller binary, less heat");
    ImGui::BulletText("No CUDA — CPU only");
    ImGui::BulletText("No Vulkan by default — HD 4000 only supports Vulkan 1.0");
    ImGui::BulletText("Particles 1500 (was 15000), Streamlines 8x80 (was 24x300)");
    ImGui::BulletText("Voxel 24 (was 48) — 8x less memory, LBM OFF, 32 res");
    ImGui::BulletText("Target 30 FPS, VSync ON, power saving, small window 1024x600");
    ImGui::BulletText("Auto-detect weak hardware, battery saver, dynamic quality scaling");
    ImGui::BulletText("Ultra-Lite: Atom/Celeron 2GB — 500 particles 4x40 voxel16 20 FPS 800x450 1 thread");
    ImGui::Separator();
    ImGui::Text("%s:", _TR("Lite Settings", "Настройки Lite"));
    ImGui::SliderInt(_TR("Particles", "Частицы"), &numParticles, 100, 10000);
    ImGui::SliderInt(_TR("Streamlines", "Линии тока"), &numStreamlines, 2, 32);
    ImGui::SliderInt(_TR("Steps", "Шаги"), &streamlineSteps, 20, 300);
    ImGui::SliderInt(_TR("Voxel Res", "Воксели"), &voxelResolution, 8, 64);
    ImGui::Checkbox(_TR("Power Saving", "Энергосбережение"), &g_litePowerSaving);
    ImGui::Checkbox(_TR("Battery Saver", "Экономия батареи"), &g_batterySaver);
    ImGui::Checkbox(_TR("Auto Quality Scaling", "Авто качество по FPS"), &g_autoQualityScaling);
    ImGui::SliderFloat(_TR("Target FPS", "Целевой FPS"), &g_liteTargetFPS, 10.0f, 60.0f, "%.0f");
    ImGui::SliderFloat(_TR("Max FPS", "Макс FPS"), &maxFPS, 10.0f, 60.0f, "%.0f");
    if (ImGui::Button(_TR("Apply Lite Defaults", "Применить Lite настройки"))) {
        applyLiteDefaults();
    }
    ImGui::SameLine();
    if (ImGui::Button(_TR("Apply Ultra-Lite", "Применить Ultra-Lite"))) {
        applyUltraLiteDefaults();
    }
    ImGui::SameLine();
    if (ImGui::Button(_TR("Print System Info", "Инфо о системе"))) {
        printLiteSystemInfo();
    }
    if (ImGui::Button(_TR("Save Lite Config", "Сохранить Lite конфиг"))) {
        saveLiteConfig("aeros-lite.ini");
    }
    ImGui::SameLine();
    if (ImGui::Button(_TR("Load Lite Config", "Загрузить Lite конфиг"))) {
        loadLiteConfig("aeros-lite.ini");
    }
    ImGui::SameLine();
    if (ImGui::Button(_TR("Auto-Detect Hardware", "Авто-детект железа"))) {
        detectAndApplyLiteIfNeeded();
    }
    ImGui::Separator();
    if (!isLiteMode()) {
        ImGui::Text("%s", _TR("Full version — for modern PCs with Vulkan/CUDA", "Полная версия — для современных ПК с Vulkan/CUDA"));
        ImGui::Text("%s", _TR("For weak devices, download Lite version:", "Для слабых устройств скачайте Lite версию:"));
        ImGui::BulletText("Aeros-Engine-Setup-Lite-x64-v*.exe");
        ImGui::BulletText("aeros-engine-lite-linux-*");
        ImGui::BulletText("build-lite.bat / build-lite.sh");
        ImGui::BulletText("Potato for Atom/Celeron 2GB: --preset potato");
    }
    ImGui::Separator();
    ImGui::Text("%s:", _TR("Lite vs Full", "Lite vs Full"));
    ImGui::Text("  Potato: 500 particles, 4x40 streamlines, voxel 16, LBM OFF, 20 FPS, SSE2, ~3-5 MB, <256 MB RAM, 1 thread");
    ImGui::Text("  Lite: 1500 particles, 8x80 streamlines, voxel 24, LBM OFF, 30 FPS, SSE2, ~5-10 MB, <512 MB RAM");
    ImGui::Text("  Full: 15000 particles, 24x300 streamlines, voxel 48, LBM ON, 60 FPS, AVX2, ~20-30 MB, 1-2 GB RAM");
}

if (ImGui::CollapsingHeader(_TR("Renderer — Vulkan + OpenGL (NEW v1.19.0)", "Рендерер — Vulkan + OpenGL (НОВОЕ v1.19.0)"), ImGuiTreeNodeFlags_DefaultOpen)) {
    ImGui::Text("%s: %s", _TR("Current", "Текущий"), getRendererName());
    ImGui::Text("%s: %s", _TR("Vulkan Available", "Vulkan доступен"), isVulkanAvailable() ? "Yes — Vulkan 1.3" : "No — OpenGL fallback");
    if (isVulkanAvailable()) {
        ImGui::Text("Vulkan Version: %s", getVulkanVersionString().c_str());
        auto devs = getVulkanDevices();
        if (!devs.empty()) {
            ImGui::Text("%s:", _TR("Vulkan Devices", "Устройства Vulkan"));
            for (size_t i=0; i<devs.size() && i<3; ++i) ImGui::BulletText("%s", devs[i].c_str());
        }
    }
    ImGui::Separator();
    ImGui::Text("%s:", _TR("Select Renderer (requires restart)", "Выберите рендерер (требует перезапуск)"));
    if (ImGui::Button("Auto (Vulkan if available, else OpenGL)")) {
        // Save to settings
        FILE* f = fopen("aeros_settings.ini", "a");
        if (f) { fprintf(f, "renderer=auto\n"); fclose(f); }
        ImGui::OpenPopup("RestartPopup");
    }
    ImGui::SameLine();
    if (ImGui::Button("Vulkan 1.3")) {
        FILE* f = fopen("aeros_settings.ini", "a");
        if (f) { fprintf(f, "renderer=vulkan\n"); fclose(f); }
        ImGui::OpenPopup("RestartPopup");
    }
    ImGui::SameLine();
    if (ImGui::Button("OpenGL 4.6")) {
        FILE* f = fopen("aeros_settings.ini", "a");
        if (f) { fprintf(f, "renderer=opengl\n"); fclose(f); }
        ImGui::OpenPopup("RestartPopup");
    }
    if (ImGui::BeginPopup("RestartPopup")) {
        ImGui::Text("%s", _TR("Renderer will change after restart", "Рендерер изменится после перезапуска"));
        ImGui::Text("%s: %s", _TR("Run with", "Запуск с"), "--vulkan or --opengl");
        if (ImGui::Button("OK")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    ImGui::Separator();
    ImGui::Text("%s:", _TR("Vulkan Benefits", "Преимущества Vulkan"));
    ImGui::BulletText("%s", _TR("Lower CPU overhead, more draw calls", "Меньше нагрузки на CPU, больше draw calls"));
    ImGui::BulletText("%s", _TR("Better multi-threading", "Лучше многопоточность"));
    ImGui::BulletText("%s", _TR("Ray tracing ready (future)", "Готов к трассировке лучей (будущее)"));
    ImGui::BulletText("%s", _TR("Works on Windows and Linux", "Работает на Windows и Linux"));
    ImGui::Separator();
    ImGui::Text("%s: %s", _TR("Linux Port", "Порт на Linux"), _TR("Fully supported", "Полностью поддерживается"));
    ImGui::BulletText("build-linux.sh --deps — install deps");
    ImGui::BulletText("build-linux.sh all — build");
    ImGui::BulletText("installer/install-linux.sh --deps --all — full install");
    ImGui::BulletText("Vulkan: sudo apt install libvulkan-dev");
    ImGui::BulletText("File dialog: zenity/kdialog or console fallback");
}

if (ImGui::CollapsingHeader(_TR("Flow", "Поток"), ImGuiTreeNodeFlags_DefaultOpen)) {
    const char* unitItemsEn[] = { "m/s", "km/h", "mph", "kts", "ft/s" };
    const char* unitItemsRu[] = { "м/с", "км/ч", "миль/ч", "узлы", "фут/с" };
    int unitIdx = (int)speedUnit;
    if (isRussian()) {
        if (ImGui::Combo(_TR("Speed Unit", "Единица скорости"), &unitIdx, unitItemsRu, IM_ARRAYSIZE(unitItemsRu))) {
            speedUnit = (SpeedUnit)unitIdx;
        }
    } else {
        if (ImGui::Combo("Speed Unit", &unitIdx, unitItemsEn, IM_ARRAYSIZE(unitItemsEn))) {
            speedUnit = (SpeedUnit)unitIdx;
        }
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
    ImGui::SliderFloat(_TR("Azimuth", "Азимут"), &flowAzimuth, 0.0f, 360.0f);
    ImGui::SameLine(); ImGui::SetNextItemWidth(80);
    if (ImGui::InputFloat("##az", &flowAzimuth, 0, 0, "%.1f")) {
        while (flowAzimuth < 0) flowAzimuth += 360.0f;
        while (flowAzimuth >= 360.0f) flowAzimuth -= 360.0f;
    }
    ImGui::SliderFloat(_TR("Elevation", "Угол атаки"), &flowElevation, -90.0f, 90.0f);
    ImGui::SameLine(); ImGui::SetNextItemWidth(80);
    ImGui::InputFloat("##el", &flowElevation, 0, 0, "%.1f");
    ImGui::SliderFloat(_TR("Time Scale", "Масштаб времени"), &timeScale, 0.01f, 3.0f, "%.2f");
    ImGui::SliderFloat(_TR("Strouhal", "Струхаль"), &strouhal, 0.05f, 0.5f, "%.3f");
    ImGui::SliderFloat(_TR("Wake Strength", "Сила следа"), &wakeStrength, 0.0f, 2.0f);
    ImGui::SliderFloat(_TR("Wake Length", "Длина следа"), &wakeLength, 2.0f, 30.0f);
    ImGui::Checkbox(_TR("Auto Rotate (showcase)", "Авто-вращение (демо)"), &autoRotate);
    if (autoRotate) ImGui::SliderFloat(_TR("Rotate Speed", "Скорость вращения"), &aeroAutoRotateSpeed, 1.0f, 50.0f, "%.1f deg/s");
}

if (ImGui::CollapsingHeader(_TR("Atmosphere (ISA)", "Атмосфера (ISA)"), ImGuiTreeNodeFlags_DefaultOpen)) {
    ImGui::Checkbox(_TR("Use Real Air Density", "Использовать реальную плотность"), &useRealDensity);
    ImGui::Text("%s", _TR("Altitude affects drag/lift via rho", "Высота влияет на сопротивление/подъемную силу через rho"));
    ImGui::SliderFloat(_TR("Altitude (m)", "Высота (м)"), &altitude, 0.0f, 20000.0f, "%.0f m");
    ImGui::SameLine(); ImGui::SetNextItemWidth(80);
    if (ImGui::InputFloat("##alt", &altitude, 0, 0, "%.0f")) {
        if (altitude < 0) altitude = 0;
        if (altitude > 80000) altitude = 80000;
    }
    if (ImGui::Button(_TR("Sea Level", "Уровень моря"))) altitude = 0.0f;
    ImGui::SameLine(); if (ImGui::Button("5 km")) altitude = 5000.0f;
    ImGui::SameLine(); if (ImGui::Button("10 km")) altitude = 10000.0f;
    ImGui::SameLine(); if (ImGui::Button("15 km")) altitude = 15000.0f;
    ImGui::Separator();
    ImGui::Text("%s: %.4f kg/m3 | Re: %.0f", _TR("Air Density", "Плотность воздуха"), airDensity, aeroReNumber);
    ImGui::Text("%s: %.0f Pa (%.2f atm)", _TR("Pressure", "Давление"), airPressure, airPressure/101325.0f);
    ImGui::Text("%s: %.1f K (%.1f C)", _TR("Temperature", "Температура"), airTemperature, airTemperature-273.15f);
    ImGui::Text("%s: %.1f m/s", _TR("Speed of Sound", "Скорость звука"), speedOfSound);
    float mach = (speedOfSound > 1e-3f) ? flowSpeed / speedOfSound : 0.0f;
    ImGui::Text("Mach: %.3f %s", mach, mach>0.8f?_TR("(compressible!)","(сжимаемый!)"):mach>0.3f?_TR("(subsonic)","(дозвуковой)"):_TR("(incompressible)","(несжимаемый)"));
    if (ImGui::TreeNode(_TR("Density Table", "Таблица плотности"))) {
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
    ImGui::Text("%s q: %.1f Pa | q*RefArea: %.1f N", _TR("Dynamic Pressure", "Динамическое давление"), q, q*aeroRefArea);
}

if (ImGui::CollapsingHeader(_TR("Particles v1.19.0", "Частицы v1.19.0"), ImGuiTreeNodeFlags_DefaultOpen)) {
    ImGui::Checkbox(_TR("Show Particles", "Показывать частицы"), &showParticles);
    ImGui::SliderInt(_TR("Count", "Количество"), &numParticles, 100, 200000);
    if (ImGui::IsItemDeactivatedAfterEdit()) initParticles();
    ImGui::SliderFloat(_TR("Size", "Размер"), &particleSize, 1.0f, 8.0f);
    ImGui::SliderFloat(_TR("Max Speed Color", "Макс. скорость для цвета"), &maxSpeedForColor, 0.5f, 20.0f);
    ImGui::Checkbox(_TR("Particle Trails (NEW)", "Следы частиц (НОВОЕ)"), &aeroShowParticleTrails);
    ImGui::Checkbox(_TR("RK4 Advection (NEW, accurate)", "RK4 адвекция (НОВОЕ, точно)"), &aeroUseRK4Particles);
    if (ImGui::Button(_TR("Reset Particles", "Сбросить частицы"))) initParticles();
    ImGui::SameLine(); if (ImGui::Button("5000")) { numParticles=5000; initParticles(); }
    ImGui::SameLine(); if (ImGui::Button("15000")) { numParticles=15000; initParticles(); }
    ImGui::SameLine(); if (ImGui::Button("50000")) { numParticles=50000; initParticles(); }
}

if (ImGui::CollapsingHeader(_TR("Streamlines v1.19.0", "Линии тока v1.19.0"), ImGuiTreeNodeFlags_DefaultOpen)) {
    ImGui::Checkbox(_TR("Show Streamlines", "Показывать линии тока"), &showStreamlines);
    ImGui::SliderInt(_TR("Count##sl", "Количество##sl"), &numStreamlines, 4, 200);
    ImGui::SliderInt(_TR("Steps", "Шагов"), &streamlineSteps, 20, 1000);
    ImGui::SliderFloat(_TR("Step Size", "Шаг"), &streamlineStepSize, 0.01f, 0.5f);
    ImGui::SliderFloat(_TR("Line Width", "Толщина линии"), &streamlineWidth, 1.0f, 5.0f);
    ImGui::SliderFloat(_TR("Alpha", "Прозрачность"), &streamlineAlpha, 0.1f, 1.0f);
    ImGui::Checkbox(_TR("Surface Seeding (NEW)", "Старт с поверхности (НОВОЕ)"), &aeroSurfaceStreamlines);
    if (ImGui::Button(_TR("Rebuild Streamlines", "Перестроить линии тока"))) computeStreamlines();
    ImGui::SameLine(); if (ImGui::Button(_TR("Low (12)", "Мало (12)"))) { numStreamlines=12; computeStreamlines(); }
    ImGui::SameLine(); if (ImGui::Button(_TR("Med (24)", "Средне (24)"))) { numStreamlines=24; computeStreamlines(); }
    ImGui::SameLine(); if (ImGui::Button(_TR("High (64)", "Много (64)"))) { numStreamlines=64; computeStreamlines(); }
}

if (ImGui::CollapsingHeader(_TR("Pressure & Forces v1.19.0 Realistic+", "Давление и Силы v1.19.0 Реалистично+"), ImGuiTreeNodeFlags_DefaultOpen)) {
    ImGui::Checkbox(_TR("Show Pressure Colors", "Показывать давление"), &showPressure);
    ImGui::Checkbox(_TR("Show Lift/Drag Vectors", "Показывать векторы Под/Сопр"), &showLiftDrag);
    ImGui::Checkbox(_TR("Show Color Legend", "Показывать легенду"), &showColorLegend);
    float q = 0.5f * airDensity * flowSpeed * flowSpeed;
    float Cd = 0, Cl = 0;
    if (q > 1e-6f && aeroRefArea > 1e-6f) {
        Cd = dragMagnitude / (q * aeroRefArea);
        Cl = liftMagnitude / (q * aeroRefArea);
    }
    ImGui::Text("%s:  %.3f N | Cd: %.4f", _TR("Drag", "Сопротивление"), dragMagnitude, Cd);
    ImGui::Text("%s:  %.3f N | Cl: %.4f | L/D: %.2f", _TR("Lift", "Подъемная сила"), liftMagnitude, Cl, liftToDragRatio);
    ImGui::Text("%s: %.3f Nm | CoP: (%.2f, %.2f, %.2f)", _TR("Moment", "Момент"), momentMagnitude, centerOfPressure.x, centerOfPressure.y, centerOfPressure.z);
    ImGui::Text("%s: %.4f m2 | q: %.1f Pa | Re: %.0f", _TR("Ref Area", "Площадь"), aeroRefArea, q, aeroReNumber);
    if (lbmInitialized) {
        float minP = lbmPressure.empty()?0:*std::min_element(lbmPressure.begin(), lbmPressure.end());
        float maxP = lbmPressure.empty()?0:*std::max_element(lbmPressure.begin(), lbmPressure.end());
        ImGui::Text("LBM %s: [%.2f, %.2f] | TKE avg %.4f", _TR("Pressure", "Давление"), minP, maxP, lbmTKE);
    }
    ImGui::Separator();
    if (ImGui::Button(_TR("Export Forces CSV (F6)", "Экспорт сил в CSV (F6)"))) aeroCSVExportRequested = true;
    if (ImGui::Button(_TR("Screenshot BMP (F5)", "Скриншот BMP (F5)"))) aeroScreenshotRequested = true;
    if (!aeroLastCSVPath.empty()) ImGui::Text("CSV: %s", aeroLastCSVPath.c_str());
    if (!aeroLastScreenshotPath.empty()) ImGui::Text("BMP: %s", aeroLastScreenshotPath.c_str());
    if (showColorLegend && showPressure) {
        ImGui::Separator();
        ImGui::Text("%s:", _TR("Color Legend", "Легенда цветов"));
        if (aeroVisMode == AeroVisMode::Pressure) ImGui::Text("Cp: -3.0 (blue, %s) -> 0 (yellow) -> +1.0 (red, %s)", _TR("suction", "разрежение"), _TR("stagnation", "торможение"));
        else if (aeroVisMode == AeroVisMode::VelocityMagnitude) ImGui::Text("|U|: 0 (blue, %s) -> %.1f m/s (red, %s)", _TR("low", "мало"), maxSpeedForColor, _TR("high", "много"));
        else if (aeroVisMode == AeroVisMode::Vorticity) ImGui::Text("|w|: 0 (blue) -> 10 (white) -> 20+ (red)");
        else if (aeroVisMode == AeroVisMode::QCriterion) ImGui::Text("Q: <0 (blue, strain) | >0 (red/yellow, vortex)");
        else if (aeroVisMode == AeroVisMode::TurbulentKE) ImGui::Text("TKE: 0 (dark) -> high (purple/yellow)");
        else if (aeroVisMode == AeroVisMode::MachNumber) ImGui::Text("Mach: 0 (blue) -> 0.8 (yellow) -> 1.5+ (red, supersonic)");
        else if (aeroVisMode == AeroVisMode::Helicity) ImGui::Text("%s: -1 (blue) -> 0 (white) -> +1 (red)", _TR("Helicity", "Спиральность"));
        else if (aeroVisMode == AeroVisMode::TotalPressure) ImGui::Text("Pt: low (blue, loss) -> high (red, freestream)");
    }
}

if (ImGui::CollapsingHeader(_TR("Voxel Collision v1.19.0", "Воксельная коллизия v1.19.0"), ImGuiTreeNodeFlags_DefaultOpen)) {
    ImGui::Checkbox(_TR("Enable Voxel Collision", "Включить воксельную коллизию"), &useVoxelCollision);
    ImGui::SliderInt(_TR("Voxel Resolution", "Разрешение вокселей"), &voxelResolution, 16, 128);
    if (ImGui::Button(_TR("Rebuild Voxel Grid", "Перестроить воксели"))) {
        buildVoxelGrid(g_vertices, voxelResolution);
        if (lbmParams.enabled) { shutdownLBM(); initLBM(); }
    }
    ImGui::Text("%s: %dx%dx%d = %d %s | %.1f ms", _TR("Voxel", "Воксель"), g_voxNx, g_voxNy, g_voxNz, g_voxNx*g_voxNy*g_voxNz, _TR("cells", "ячеек"), perfVoxelMs);
}

if (ImGui::CollapsingHeader(_TR("Display v1.19.0", "Отображение v1.19.0"), ImGuiTreeNodeFlags_DefaultOpen)) {
    ImGui::Checkbox(_TR("Show Model", "Показывать модель"), &showModel);
    ImGui::Checkbox(_TR("Show Obstacle", "Показывать препятствие"), &showObstacle);
    if (showObstacle) {
        ImGui::SliderFloat(_TR("Obstacle Alpha", "Прозрачность препятствия"), &obstacleAlpha, 0.02f, 1.0f, "%.2f");
        ImGui::ColorEdit3(_TR("Obstacle Color", "Цвет препятствия"), &obstacleColor[0]);
    }
    ImGui::Checkbox(_TR("Show Bounding Box", "Показывать рамку"), &showBoundingBox);
    ImGui::Checkbox(_TR("Show Axes", "Показывать оси"), &showAxes);
    ImGui::Checkbox(_TR("Lighting", "Освещение"), &lightingEnabled);
    ImGui::Checkbox("VSync", &vsyncEnabled);
    ImGui::Checkbox(_TR("Limit FPS", "Ограничить FPS"), &limitFPS);
    if (limitFPS) ImGui::SliderFloat(_TR("Max FPS", "Макс. FPS"), &maxFPS, 10.0f, 240.0f);
    ImGui::SliderFloat(_TR("Camera Speed", "Скорость камеры"), &cameraSpeedMultiplier, 0.1f, 5.0f);
    ImGui::SliderFloat(_TR("Mouse Sens", "Чувств. мыши"), &mouseSensitivity, 0.05f, 1.0f);
    ImGui::ColorEdit3(_TR("Background", "Фон"), &bgColor[0]);
    ImGui::ColorEdit3(_TR("Model Color", "Цвет модели"), &modelColor[0]);
    ImGui::Checkbox(_TR("Save Settings on Exit", "Сохранять настройки при выходе"), &aeroSaveSettings);
    ImGui::Separator();
    // Language selector duplicate in Display for convenience
    int langIdx = (int)currentLanguage;
    const char* langItems[] = {"English", "Русский"};
    if (ImGui::Combo(_TR("Language / Язык", "Язык / Language"), &langIdx, langItems, 2)) {
        currentLanguage = (Language)langIdx;
    }
    ImGui::Text("%s: WASD+QE move, RMB/MMB drag rotate, Wheel zoom, F1-F6, Ctrl+R reset, F5 screenshot, F6 CSV",
        _TR("Controls", "Управление"));
}

if (ImGui::CollapsingHeader(_TR("LBM - Lattice Boltzmann v1.19.0 Ultra+", "LBM - Решеточный Больцман v1.19.0 Ультра+"), ImGuiTreeNodeFlags_DefaultOpen)) {
    ImGui::Checkbox(_TR("Enable LBM (High-Accuracy Physics)", "Включить LBM (Точная физика)"), &lbmParams.enabled);
    if (lbmParams.enabled) {
        ImGui::TextColored(ImVec4(0.2f,1,0.8f,1), "%s", _TR("LBM Active — Navier-Stokes, realistic as photos", "LBM Активен — Навье-Стокс, реалистично как на фото"));
    } else {
        ImGui::TextColored(ImVec4(1,0.8f,0.2f,1), "%s", _TR("Using potential flow + wake (legacy)", "Используется потенциальный поток + след (старый)"));
    }
    ImGui::SliderInt(_TR("Steps per Frame", "Шагов за кадр"), &lbmParams.stepsPerFrame, 1, 50);
    ImGui::SliderFloat(_TR("Tau (relaxation)", "Тау (релаксация)"), &lbmParams.tau, 0.51f, 1.5f, "%.3f");
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        if (!std::isfinite(lbmParams.tau) || lbmParams.tau < 0.51f) lbmParams.tau = 0.6f;
        lbmParams.viscosity = (lbmParams.tau - 0.5f) * 0.333333f;
    }
    ImGui::SliderFloat(_TR("U0 (lattice speed)", "U0 (скорость решетки)"), &lbmParams.U0, 0.01f, 0.25f, "%.3f");
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        if (!std::isfinite(lbmParams.U0) || lbmParams.U0 < 0.01f) lbmParams.U0 = 0.1f;
    }
    ImGui::Checkbox(_TR("Smagorinsky LES Turbulence", "Турбулентность Smagorinsky LES"), &lbmParams.useTurbulence);
    if (lbmParams.useTurbulence) ImGui::SliderFloat(_TR("Smagorinsky C", "Константа Smagorinsky"), &lbmParams.smagorinskyC, 0.01f, 0.3f, "%.3f");
    ImGui::Checkbox(_TR("Adaptive Stepping (NEW v1.9.0)", "Адаптивные шаги (НОВОЕ v1.9.0)"), &aeroAdaptiveLBM);
    ImGui::Checkbox(_TR("MRT (High Re stability)", "MRT (стабильность при высоком Re)"), &lbmParams.useMRT);
    ImGui::Checkbox(_TR("Regularized LBM (stability)", "Регуляризованный LBM (стабильность)"), &lbmParams.useRegularized);
    ImGui::Checkbox(_TR("Zou/He Inlet BC (accurate)", "Граничные условия Zou/He (точные)"), &lbmParams.useZouHeBC);
    ImGui::Checkbox(_TR("Convective Outlet", "Конвективный выход"), &lbmParams.useConvectiveOutlet);
    ImGui::SliderFloat(_TR("Inlet Turbulence", "Турбулентность на входе"), &lbmParams.inletTurbulence, 0.0f, 0.1f, "%.3f");
    ImGui::Checkbox(_TR("Ground (for cars)", "Земля (для авто)"), &lbmParams.useGround);
    if (lbmParams.useGround) ImGui::SliderFloat(_TR("Ground Height##lbm", "Высота земли##lbm"), &lbmParams.groundHeight, -2.0f, 2.0f, "%.2f");
    ImGui::Separator();
    ImGui::Text("LBM Grid: %dx%dx%d = %d %s", lbmNx, lbmNy, lbmNz, lbmNx*lbmNy*lbmNz, _TR("cells", "ячеек"));
    ImGui::Text("%s: %d | %s: %s | TKE: %.4f", _TR("Steps", "Шагов"), lbmCurrentStep, _TR("Converged", "Сходимость"), lbmConverged ? _TR("YES","ДА") : _TR("NO","НЕТ"), lbmTKE);
    ImGui::Text("Avg Rho: %.4f | Kinetic: %.6f | MaxVel LB %.3f", lbmAvgRho, lbmAvgKineticEnergy, lbmMaxVelocityLB);
    ImGui::Text("Max Vel World: %.2f m/s | Reynolds: %.1f | Conv: %.2e", lbmMaxVelocityWorld, lbmReynolds, lbmConvergence);
    ImGui::Text("%s: (%.4f, %.4f, %.4f) | Time: %.1f ms", _TR("Cell Size", "Размер ячейки"), lbmCellSizeX, lbmCellSizeY, lbmCellSizeZ, lbmTimeMs);
    if (ImGui::Button(_TR("Init / Reset LBM", "Инициализация / Сброс LBM"))) { initLBM(); }
    ImGui::SameLine(); if (ImGui::Button(_TR("Reset Only", "Только сброс"))) { resetLBM(); }
    ImGui::SameLine(); if (ImGui::Button(_TR("Shutdown", "Отключить"))) { shutdownLBM(); }
    if (ImGui::Button("Step 10")) { stepLBMCPU(10); }
    ImGui::SameLine(); if (ImGui::Button("Step 100")) { stepLBMCPU(100); }
    ImGui::SameLine(); if (ImGui::Button("Step 500")) { stepLBMCPU(500); }
    ImGui::SameLine(); if (ImGui::Button(_TR("Compute Vort/Q/TKE", "Вычислить вихрь/Q/TKE"))) { computeLBMVorticityAndQ(); }
    ImGui::Separator();
    ImGui::Text("%s v1.19.0:", _TR("Realistic features", "Реалистичные фичи"));
    ImGui::BulletText("%s", _TR("Zou/He inlet + convective outlet + ground", "Вход Zou/He + конвективный выход + земля"));
    ImGui::BulletText("%s", _TR("Mach + Helicity + Total Pressure", "Мах + Спиральность + Полное давление"));
    ImGui::BulletText("%s", _TR("Adaptive stepping + stability checks", "Адаптивные шаги + проверки стабильности"));
    ImGui::BulletText("%s", _TR("Rainbow/Viridis/Parula/CoolWarm + EN/RU", "Радуга/Viridis/Parula/CoolWarm + EN/RU"));
}

if (ImGui::CollapsingHeader(_TR("Compute & Export v1.19.0", "Вычисления и Экспорт v1.19.0"))) {
    ImGui::RadioButton("CUDA", &useCUDA, 1);
    ImGui::SameLine();
    ImGui::RadioButton("CPU", &useCUDA, 0);
    if (ImGui::Button(_TR("Open Model", "Открыть модель"))) {
        std::string p = openFileDialog();
        if (!p.empty()) loadModel(p);
    }
    ImGui::SameLine();
    if (ImGui::Button(_TR("Screenshot BMP (F5)", "Скриншот BMP (F5)"))) aeroScreenshotRequested = true;
    ImGui::SameLine();
    if (ImGui::Button(_TR("Export CSV (F6)", "Экспорт CSV (F6)"))) aeroCSVExportRequested = true;
    if (ImGui::Button(_TR("Save Settings", "Сохранить настройки"))) {
        if (saveSettings("aeros_settings.ini")) ImGui::Text("%s aeros_settings.ini", _TR("Saved", "Сохранено"));
    }
    ImGui::SameLine();
    if (ImGui::Button(_TR("Load Settings", "Загрузить настройки"))) {
        if (loadSettings("aeros_settings.ini")) {
            updateVertexColors(); computeStreamlines();
            ImGui::Text("%s", _TR("Loaded settings", "Настройки загружены"));
        }
    }
    ImGui::Separator();
    ImGui::Text("%s: F1 model, F2 pressure, F3 streamlines, F4 particles, F5 screenshot, F6 CSV, Ctrl+R reset camera",
        _TR("Shortcuts", "Горячие клавиши"));
}

if (ImGui::CollapsingHeader(_TR("Test Mode v1.19.0 Ultra (Physics+Code+LBM+Opt+Realistic+New+Lang)", "Режим тестов v1.19.0 Ультра (Физика+Код+LBM+Опт+Реалистичность+Новое+Язык)"), ImGuiTreeNodeFlags_DefaultOpen)) {
    ImGui::Checkbox(_TR("Enable Test Mode", "Включить тесты"), &testModeEnabled);
    ImGui::Checkbox(_TR("Continuous Validation", "Непрерывная проверка"), &testContinuous);
    int realisticPassed = 0, realisticFailed = 0;
    for (auto& r : lastTestResults) if (r.category=="Realistic") { if (r.passed) realisticPassed++; else realisticFailed++; }
    int optPassed = 0, optFailed = 0;
    for (auto& r : lastTestResults) if (r.category=="Optimization") { if (r.passed) optPassed++; else optFailed++; }
    ImGui::Text("%s: %d %s, %d %s | LBM: %d/%d | Realistic: %d/%d",
        _TR("Total", "Всего"), testsPassed, _TR("passed", "пройдено"), testsFailed, _TR("failed", "провалено"),
        lbmTestsPassed, lbmTestsPassed+lbmTestsFailed, realisticPassed, realisticPassed+realisticFailed);
    ImGui::Text("  Physics: %d | Code: %d/%d | Opt: %d/%d", testsPassed - codeTestsPassed - lbmTestsPassed - optPassed - realisticPassed, codeTestsPassed, codeTestsPassed+codeTestsFailed, optPassed, optPassed+optFailed);
    if (codeTestsFailed > 0) ImGui::Text("  Code failed: %d", codeTestsFailed);
    ImGui::Text("%s: %.1f ms", _TR("Last run", "Последний запуск"), lastTestTimeMs);
    if (lastGLError != 0) ImGui::TextColored(ImVec4(1,0.3f,0.1f,1), "GL Error: %s", lastGLErrorStr.c_str());
    if (testsFailed > 0) ImGui::TextColored(ImVec4(1,0.2f,0.2f,1), "!!! %s: Phys=%d Code=%d LBM=%d Real=%d !!!",
        _TR("ERRORS", "ОШИБКИ"), testsFailed - codeTestsFailed - lbmTestsFailed - realisticFailed - optFailed, codeTestsFailed, lbmTestsFailed, realisticFailed);
    else if (testsPassed > 0) ImGui::TextColored(ImVec4(0.2f,1,0.2f,1), "%s", _TR("All tests passed — physics, code, LBM, opt, realistic, lang OK", "Все тесты пройдены — физика, код, LBM, оптимизация, реалистичность, язык ОК"));

    if (ImGui::Button(_TR("Run All Tests", "Запустить все тесты"))) runAllTests();
    ImGui::SameLine(); if (ImGui::Button(_TR("Physics Only", "Только физика"))) { lastTestResults.clear(); testLog.clear(); testsPassed=0; testsFailed=0; codeTestsPassed=0; codeTestsFailed=0; runPhysicsTests(); }
    ImGui::SameLine(); if (ImGui::Button(_TR("Code Only", "Только код"))) { lastTestResults.clear(); testLog.clear(); testsPassed=0; testsFailed=0; codeTestsPassed=0; codeTestsFailed=0; runCodeTests(); }
    if (ImGui::Button(_TR("LBM Only", "Только LBM"))) { lastTestResults.clear(); testLog.clear(); testsPassed=0; testsFailed=0; runLBMTests(); }
    ImGui::SameLine(); if (ImGui::Button(_TR("Realistic Only", "Только реалистичность"))) { lastTestResults.clear(); testLog.clear(); testsPassed=0; testsFailed=0; runRealisticTests(); }
    ImGui::SameLine(); if (ImGui::Button(_TR("Opt Only", "Только оптимизация"))) { lastTestResults.clear(); testLog.clear(); testsPassed=0; testsFailed=0; runOptimizationTests(); }
    if (ImGui::Button(_TR("Clear Log", "Очистить лог"))) {
        testLog.clear(); lastTestResults.clear(); testsPassed=0; testsFailed=0; codeTestsPassed=0; codeTestsFailed=0; lbmTestsPassed=0; lbmTestsFailed=0; lastGLError=0; lastGLErrorStr.clear();
    }

    if (!lastTestResults.empty()) {
        ImGui::Separator();
        auto drawCat = [&](const char* catName, const char* labelEn, const char* labelRu) {
            int pass=0, fail=0;
            for (auto& r : lastTestResults) if (r.category==catName) { if (r.passed) pass++; else fail++; }
            if (pass+fail>0) {
                std::string title = std::string(_TR(labelEn, labelRu)) + " (" + std::to_string(pass) + "/" + std::to_string(pass+fail) + ")";
                if (ImGui::TreeNode(title.c_str())) {
                    for (auto& r : lastTestResults) if (r.category==catName) {
                        ImVec4 col = r.passed ? ImVec4(0.2f,1,0.2f,1) : ImVec4(1,0.2f,0.2f,1);
                        ImGui::TextColored(col, "%s: %s", r.name.c_str(), r.passed ? _TR("PASS","ПРОЙДЕН") : _TR("FAIL","ПРОВАЛ"));
                        if (!r.message.empty() && r.message != "OK" && r.message != "FAILED") {
                            ImGui::SameLine(); ImGui::Text(" - %s", r.message.c_str());
                        }
                    }
                    ImGui::TreePop();
                }
            }
        };
        drawCat("Physics", "Physics Tests", "Тесты Физики");
        drawCat("Code", "Code Tests", "Тесты Кода");
        drawCat("LBM", "LBM Tests", "Тесты LBM");
        drawCat("Optimization", "Optimization", "Оптимизация");
        drawCat("Realistic", "Realistic Aero", "Реалистичная Аэро");
    }

    if (!testLog.empty()) {
        ImGui::Separator();
        ImGui::Text("%s:", _TR("Test Log", "Лог тестов"));
        ImGui::BeginChild("TestLog", ImVec2(0, 250), true);
        ImGui::TextUnformatted(testLog.c_str());
        ImGui::EndChild();
    }

    ImGui::Separator();
    ImGui::Text("%s:", _TR("Frame Validation (real-time)", "Проверка кадра (реальное время)"));
    bool hasNaN = false;
    if (!std::isfinite(flowSpeed) || !std::isfinite(altitude) || !std::isfinite(airDensity)) hasNaN = true;
    if (!std::isfinite(liftMagnitude) || !std::isfinite(dragMagnitude)) hasNaN = true;
    if (!std::isfinite(deltaTime) || !std::isfinite(maxDim)) hasNaN = true;
    if (!std::isfinite(aeroRefArea)) hasNaN = true;
    if (hasNaN) ImGui::TextColored(ImVec4(1,0,0,1), "%s", _TR("NaN/Inf detected in globals!", "Обнаружен NaN/Inf в глобальных!"));
    else ImGui::TextColored(ImVec4(0,1,0,1), "%s", _TR("No NaN in globals", "Нет NaN в глобальных"));

    bool bufOk = true;
    if (particleDrawCount < 0 || particleDrawCount > (int)(particlePositions.size()/3+1)) bufOk = false;
    if (!g_distanceField.empty() && (int)g_distanceField.size() != g_voxNx*g_voxNy*g_voxNz) bufOk = false;
    if (g_vertices.size() != g_normals.size()) bufOk = false;
    if (!bufOk) ImGui::TextColored(ImVec4(1,0.3f,0,1), "%s", _TR("Buffer integrity issue!", "Проблема с буферами!"));
    else ImGui::TextColored(ImVec4(0,1,0,1), "%s", _TR("Buffers OK", "Буферы ОК"));

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
