#include "lang.h"
#include "imgui.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

#include <unordered_map>
#include <iostream>

Language currentLanguage = Language::Russian; // По умолчанию русский для RU аудитории

// Таблица переводов по ключам — v1.16.0
static std::unordered_map<std::string, std::pair<std::string, std::string>> translationTable = {
    // Общие
    {"app.title", {"Aeros Engine v1.16.0 Ultra Realistic+ [EN/RU]", "Aeros Engine v1.16.0 Ультра Реалистичный+ [EN/RU]"}},
    {"fps", {"FPS", "Кадров/с"}},
    {"frame", {"Frame", "Кадр"}},
    {"vertices", {"Vertices", "Вершин"}},
    {"tris", {"Tris", "Треугольников"}},
    {"voxel", {"Voxel", "Вокселей"}},
    {"backend", {"Backend", "Движок"}},
    {"reynolds", {"Re", "Re"}},
    {"mach", {"Mach", "Мах"}},

    // Performance
    {"perf.title", {"Performance v1.16.0", "Производительность v1.16.0"}},
    {"perf.frame", {"Frame", "Кадр"}},
    {"perf.lbm", {"LBM", "LBM"}},
    {"perf.particles", {"Particles", "Частицы"}},
    {"perf.forces", {"Forces", "Силы"}},
    {"perf.streamlines", {"Streamlines", "Линии тока"}},
    {"perf.voxel", {"Voxel", "Воксель"}},
    {"perf.show_graph", {"Show Perf Graph", "Показывать график"}},
    {"perf.show_memory", {"Show Memory Usage", "Показывать память"}},

    // Realistic Aero
    {"aero.title", {"Realistic Aero v1.16.0 — Ultra Photo Mode", "Реалистичная Аэро v1.16.0 — Ультра Фото Режим"}},
    {"aero.visualization", {"Visualization", "Визуализация"}},
    {"aero.color_map", {"Color Map", "Цветовая схема"}},
    {"aero.color_streamlines", {"Color Streamlines by Velocity", "Окраска линий тока по скорости"}},
    {"aero.highlight_separation", {"Highlight Flow Separation", "Подсветка отрыва потока"}},
    {"aero.show_wake", {"Show Wake", "Показывать след"}},
    {"aero.wake_opacity", {"Wake Opacity", "Прозрачность следа"}},
    {"aero.show_vortices", {"Show Vortices (Q)", "Показывать вихри (Q)"}},
    {"aero.show_bl", {"Show Boundary Layer", "Показывать погранслой"}},
    {"aero.pbr", {"Realistic PBR Lighting", "Реалистичное PBR освещение"}},
    {"aero.ground_effect", {"Ground Effect (cars)", "Эффект земли (авто)"}},
    {"aero.ground_height", {"Ground Height", "Высота земли"}},
    {"aero.show_ground", {"Show Ground Plane", "Показывать землю"}},
    {"aero.ground_alpha", {"Ground Alpha", "Прозрачность земли"}},
    {"aero.ground_color", {"Ground Color", "Цвет земли"}},
    {"aero.show_slice", {"Show Slice Plane", "Показывать срез"}},
    {"aero.enable_slice", {"Enable Aero Slice", "Включить аэро-срез"}},
    {"aero.slice_axis", {"Slice Axis", "Ось среза"}},
    {"aero.slice_pos", {"Slice Pos", "Позиция среза"}},
    {"aero.mach_effects", {"Mach Effects (compressibility)", "Эффекты Маха (сжимаемость)"}},
    {"aero.mach_threshold", {"Mach Threshold", "Порог Маха"}},

    // Flow
    {"flow.title", {"Flow", "Поток"}},
    {"flow.speed_unit", {"Speed Unit", "Единица скорости"}},
    {"flow.azimuth", {"Azimuth", "Азимут"}},
    {"flow.elevation", {"Elevation", "Угол атаки"}},
    {"flow.time_scale", {"Time Scale", "Масштаб времени"}},
    {"flow.strouhal", {"Strouhal", "Струхаль"}},
    {"flow.wake_strength", {"Wake Strength", "Сила следа"}},
    {"flow.wake_length", {"Wake Length", "Длина следа"}},
    {"flow.auto_rotate", {"Auto Rotate (showcase)", "Авто-вращение (демо)"}},
    {"flow.rotate_speed", {"Rotate Speed", "Скорость вращения"}},

    // Atmosphere
    {"atm.title", {"Atmosphere (ISA)", "Атмосфера (ISA)"}},
    {"atm.use_real", {"Use Real Air Density", "Использовать реальную плотность"}},
    {"atm.altitude", {"Altitude (m)", "Высота (м)"}},
    {"atm.sea_level", {"Sea Level", "Уровень моря"}},
    {"atm.density", {"Air Density", "Плотность воздуха"}},
    {"atm.pressure", {"Pressure", "Давление"}},
    {"atm.temperature", {"Temperature", "Температура"}},
    {"atm.speed_sound", {"Speed of Sound", "Скорость звука"}},

    // Particles
    {"particles.title", {"Particles v1.16.0", "Частицы v1.16.0"}},
    {"particles.show", {"Show Particles", "Показывать частицы"}},
    {"particles.count", {"Count", "Количество"}},
    {"particles.size", {"Size", "Размер"}},
    {"particles.max_speed", {"Max Speed Color", "Макс. скорость для цвета"}},
    {"particles.trails", {"Particle Trails", "Следы частиц"}},
    {"particles.rk4", {"RK4 Advection (accurate)", "RK4 адвекция (точная)"}},
    {"particles.reset", {"Reset Particles", "Сбросить частицы"}},

    // Streamlines
    {"streamlines.title", {"Streamlines v1.16.0", "Линии тока v1.16.0"}},
    {"streamlines.show", {"Show Streamlines", "Показывать линии тока"}},
    {"streamlines.count", {"Count", "Количество"}},
    {"streamlines.steps", {"Steps", "Шагов"}},
    {"streamlines.step_size", {"Step Size", "Шаг"}},
    {"streamlines.width", {"Line Width", "Толщина линии"}},
    {"streamlines.alpha", {"Alpha", "Прозрачность"}},
    {"streamlines.surface", {"Surface Seeding", "Старт с поверхности"}},
    {"streamlines.rebuild", {"Rebuild Streamlines", "Перестроить"}},

    // Forces
    {"forces.title", {"Pressure & Forces v1.16.0 Realistic+", "Давление и Силы v1.16.0 Реалистично+"}},
    {"forces.show_pressure", {"Show Pressure Colors", "Показывать давление"}},
    {"forces.show_lift_drag", {"Show Lift/Drag Vectors", "Показывать векторы Под/Сопр"}},
    {"forces.show_legend", {"Show Color Legend", "Показывать легенду"}},
    {"forces.drag", {"Drag", "Сопротивление"}},
    {"forces.lift", {"Lift", "Подъемная сила"}},
    {"forces.moment", {"Moment", "Момент"}},
    {"forces.ref_area", {"Ref Area", "Площадь"}},
    {"forces.export_csv", {"Export Forces CSV (F6)", "Экспорт сил в CSV (F6)"}},
    {"forces.screenshot", {"Screenshot BMP (F5)", "Скриншот BMP (F5)"}},

    // Voxel
    {"voxel.title", {"Voxel Collision v1.16.0", "Воксельная коллизия v1.16.0"}},
    {"voxel.enable", {"Enable Voxel Collision", "Включить воксельную коллизию"}},
    {"voxel.resolution", {"Voxel Resolution", "Разрешение вокселей"}},
    {"voxel.rebuild", {"Rebuild Voxel Grid", "Перестроить воксели"}},

    // Display
    {"display.title", {"Display v1.16.0", "Отображение v1.16.0"}},
    {"display.show_model", {"Show Model", "Показывать модель"}},
    {"display.show_obstacle", {"Show Obstacle", "Показывать препятствие"}},
    {"display.obstacle_alpha", {"Obstacle Alpha", "Прозрачность препятствия"}},
    {"display.obstacle_color", {"Obstacle Color", "Цвет препятствия"}},
    {"display.show_bbox", {"Show Bounding Box", "Показывать рамку"}},
    {"display.show_axes", {"Show Axes", "Показывать оси"}},
    {"display.lighting", {"Lighting", "Освещение"}},
    {"display.vsync", {"VSync", "Вертикальная синхронизация"}},
    {"display.limit_fps", {"Limit FPS", "Ограничить FPS"}},
    {"display.max_fps", {"Max FPS", "Макс. FPS"}},
    {"display.camera_speed", {"Camera Speed", "Скорость камеры"}},
    {"display.mouse_sens", {"Mouse Sens", "Чувств. мыши"}},
    {"display.background", {"Background", "Фон"}},
    {"display.model_color", {"Model Color", "Цвет модели"}},
    {"display.save_settings", {"Save Settings on Exit", "Сохранять настройки при выходе"}},
    {"display.language", {"Language / Язык", "Язык / Language"}},

    // LBM
    {"lbm.title", {"LBM - Lattice Boltzmann v1.16.0 Ultra+", "LBM - Решеточный Больцман v1.16.0 Ультра+"}},
    {"lbm.enable", {"Enable LBM (High-Accuracy Physics)", "Включить LBM (Точная физика)"}},
    {"lbm.steps", {"Steps per Frame", "Шагов за кадр"}},
    {"lbm.tau", {"Tau (relaxation)", "Тау (релаксация)"}},
    {"lbm.u0", {"U0 (lattice speed)", "U0 (скорость решетки)"}},
    {"lbm.turbulence", {"Smagorinsky LES Turbulence", "Турбулентность Smagorinsky LES"}},
    {"lbm.smag_c", {"Smagorinsky C", "Константа Smagorinsky"}},
    {"lbm.adaptive", {"Adaptive Stepping", "Адаптивные шаги"}},
    {"lbm.mrt", {"MRT (High Re stability)", "MRT (стабильность при высоком Re)"}},
    {"lbm.regularized", {"Regularized LBM (stability)", "Регуляризованный LBM (стабильность)"}},
    {"lbm.zouhe", {"Zou/He Inlet BC (accurate)", "Граничные условия Zou/He (точные)"}},
    {"lbm.convective", {"Convective Outlet", "Конвективный выход"}},
    {"lbm.inlet_turb", {"Inlet Turbulence", "Турбулентность на входе"}},
    {"lbm.ground", {"Ground (for cars)", "Земля (для авто)"}},
    {"lbm.ground_height", {"Ground Height", "Высота земли"}},

    // Compute
    {"compute.title", {"Compute & Export v1.16.0", "Вычисления и Экспорт v1.16.0"}},
    {"compute.open_model", {"Open Model", "Открыть модель"}},
    {"compute.screenshot", {"Screenshot BMP (F5)", "Скриншот BMP (F5)"}},
    {"compute.export_csv", {"Export CSV (F6)", "Экспорт CSV (F6)"}},
    {"compute.save_settings", {"Save Settings", "Сохранить настройки"}},
    {"compute.load_settings", {"Load Settings", "Загрузить настройки"}},

    // Test
    {"test.title", {"Test Mode v1.16.0 Ultra", "Режим тестов v1.16.0 Ультра"}},
    {"test.enable", {"Enable Test Mode", "Включить тесты"}},
    {"test.continuous", {"Continuous Validation", "Непрерывная проверка"}},
    {"test.run_all", {"Run All Tests", "Запустить все тесты"}},
    {"test.physics_only", {"Physics Only", "Только физика"}},
    {"test.code_only", {"Code Only", "Только код"}},
    {"test.lbm_only", {"LBM Only", "Только LBM"}},
    {"test.realistic_only", {"Realistic Only", "Только реалистичность"}},
    {"test.opt_only", {"Opt Only", "Только оптимизация"}},
    {"test.clear_log", {"Clear Log", "Очистить лог"}},
};

std::string getTranslation(const std::string& key) {
    auto it = translationTable.find(key);
    if (it != translationTable.end()) {
        return (currentLanguage == Language::Russian) ? it->second.second : it->second.first;
    }
    return key; // fallback to key itself
}

void initLocalization() {
    // Попытка загрузить шрифт с поддержкой кириллицы
    ImGuiIO& io = ImGui::GetIO();
    std::cout << "[Lang] Initializing localization, current lang: " << getCurrentLanguageName() << std::endl;

#ifdef _WIN32
    // На Windows пробуем загрузить системные шрифты с кириллицей
    const char* fontPaths[] = {
        "C:\\Windows\\Fonts\\segoeui.ttf",
        "C:\\Windows\\Fonts\\arial.ttf",
        "C:\\Windows\\Fonts\\tahoma.ttf",
        "C:\\Windows\\Fonts\\verdana.ttf",
        nullptr
    };

    for (int i = 0; fontPaths[i] != nullptr; ++i) {
        // Проверяем существование файла
        DWORD attrs = GetFileAttributesA(fontPaths[i]);
        if (attrs == INVALID_FILE_ATTRIBUTES) continue;

        std::cout << "[Lang] Trying to load font: " << fontPaths[i] << std::endl;
        ImFont* font = io.Fonts->AddFontFromFileTTF(fontPaths[i], 18.0f, nullptr, io.Fonts->GetGlyphRangesCyrillic());
        if (font) {
            std::cout << "[Lang] Loaded font with Cyrillic: " << fontPaths[i] << std::endl;
            // Также добавляем дефолтный для латиницы как fallback? ImGui мержит автоматически если MergeMode
            // Для простоты — оставляем один шрифт
            break;
        }
    }

    // Если не удалось загрузить ни один — используем дефолтный, но с кириллическим диапазоном (может не отрендерить, но попытка)
    if (io.Fonts->Fonts.Size == 0) {
        std::cout << "[Lang] No system font loaded, using default with Cyrillic range attempt" << std::endl;
        io.Fonts->AddFontDefault();
        // Попытка добавить кириллицу через дополнительный шрифт
        // ImGui по умолчанию не имеет кириллицы, но мы оставляем как есть — в худшем случае будут ??? но компиляция пройдет
    }
#else
    // На Linux — пробуем дефолтные пути
    const char* linuxFonts[] = {
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
        "/usr/share/fonts/truetype/freefont/FreeSans.ttf",
        nullptr
    };
    bool loaded = false;
    for (int i = 0; linuxFonts[i] != nullptr; ++i) {
        FILE* f = fopen(linuxFonts[i], "rb");
        if (!f) continue;
        fclose(f);
        std::cout << "[Lang] Trying Linux font: " << linuxFonts[i] << std::endl;
        ImFont* font = io.Fonts->AddFontFromFileTTF(linuxFonts[i], 18.0f, nullptr, io.Fonts->GetGlyphRangesCyrillic());
        if (font) {
            std::cout << "[Lang] Loaded Linux font with Cyrillic: " << linuxFonts[i] << std::endl;
            loaded = true;
            break;
        }
    }
    if (!loaded) {
        io.Fonts->AddFontDefault();
        std::cout << "[Lang] Using default font (may not have Cyrillic)" << std::endl;
    }
#endif

    // Важно: не билдим атлас здесь, ImGui_ImplOpenGL3 сделает это позже
    std::cout << "[Lang] Localization initialized, fonts: " << io.Fonts->Fonts.Size << std::endl;
}
