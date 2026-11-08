#pragma once
// =====================================================
// v1.34.0 — ГЛАВНОЕ МЕНЮ приложения (стартовый экран).
//
// Что это. Полноэкранный экран, который встречает пользователя при запуске:
// крупные кнопки настоящих действий (открыть модель, демо-тела, загрузить
// исследование, настройки), список недавних моделей и краткая справка по
// горячим клавишам. Это не «заставка»: каждый элемент вызывает тот же код,
// что и меню/дерево, никаких заглушек.
//
// Почему отдельный модуль, а не часть cae_ui.h. Экран не зависит от оболочки
// CAD/CAE: ему нужны только действия (открыть/загрузить/выйти) и состояние
// приложения. Здесь же лежит таблица действий — `startScreenPerform()` —
// чтобы поведение кнопок можно было проверить headless-тестом без отрисовки.
//
// ВЗАИМОДЕЙСТВИЕ С ДВИЖКОМ:
//   * ESC закрывает экран (input.cpp), а не программу;
//   * пока экран открыт, хоткеи движка не срабатывают (aeroStartScreen);
//   * состояние «показывать при запуске» сохраняется в настройках
//     (aeroStartScreenOnStartup);
//   * экран открывается снова из меню «Файл → Главное меню».
// =====================================================

#include <cstdio>
#include <string>
#include <vector>

#include "imgui.h"

#include "globals.h"
#include "lang.h"
#include "model.h"
#include "stl_loader.h"
#include "ui_layout.h"
#include "debug_log.h"
#include "recent_files.h"
#include "demo_geometry.h"
#include "hotkeys.h"   // v1.34.0 — единая таблица клавиш (сводка и полная справка)
#include "version.h"

// cae_ui.h подключается вызывающим кодом (ui.cpp) ДО этого заголовка: экран
// пользуется caeLog/caeSyncGeometry/caeSaveStudy/caeLoadStudy. Отдельный
// include здесь создал бы цикл (cae_ui.h → start_screen.h → cae_ui.h).

namespace startscreen {

// -----------------------------------------------------
// Действия стартового экрана. Значения — то, что нажимает пользователь.
// -----------------------------------------------------
enum class Action {
    None = 0,
    OpenModel,      // диалог выбора STL
    DemoSphere,
    DemoWing,
    DemoAirship,
    LoadStudy,      // загрузить исследование (.study)
    Settings,       // перейти в раздел настроек
    Continue,       // закрыть экран, ничего не загружая
    Exit
};

inline const char* actionName(Action a) {
    switch (a) {
        case Action::OpenModel:   return "open-model";
        case Action::DemoSphere:  return "demo-sphere";
        case Action::DemoWing:    return "demo-wing";
        case Action::DemoAirship: return "demo-airship";
        case Action::LoadStudy:   return "load-study";
        case Action::Settings:    return "settings";
        case Action::Continue:    return "continue";
        case Action::Exit:        return "exit";
        case Action::None:        break;
    }
    return "none";
}

// Состояние экрана, переживающее кадры: последняя ошибка и путь последней
// открытой модели. Ошибка показывается прямо в экране, а не только в журнале:
// иначе «нажал и ничего не произошло» — самый частый вид непонятного отказа.
struct State {
    std::string lastError;
    std::string lastOpened;
    int  lastAction = 0;      // для теста и журнала
};
inline State& state() { static State s; return s; }

// Запомнить путь открытой модели в списке недавних (вызывается после успешной
// загрузки). Отдельная функция, чтобы её можно было проверить тестом.
inline void rememberModel(const std::string& path) {
    if (path.empty()) return;
    recentfiles::add(path, recentfiles::defaultPath(), 6);
    state().lastOpened = path;
}

// -----------------------------------------------------
// Выполнить действие. Возвращает true, если экран нужно закрыть.
//
// ВАЖНО: здесь нет ни одного «пустого» обработчика. Если действие не может
// быть выполнено (нет файла, отмена в диалоге, ошибка чтения), экран остаётся
// открытым и показывает причину — пользователь видит, что произошло.
// -----------------------------------------------------
inline bool perform(Action a, UiLayoutState& st) {
    state().lastAction = (int)a;
    state().lastError.clear();

    switch (a) {
        case Action::None:
            return false;

        case Action::Continue:
            debugLogAction("главное меню", "продолжение без загрузки модели");
            return true;

        case Action::Exit:
            debugLogAction("главное меню", "выход из программы");
            aeroExitRequested = true;
            return true;

        case Action::Settings:
            // Раздел «Общие и язык» в дереве модели (идентификатор 1).
            st.workspace = 0;
            uiSelectedLeaf = 1;
            debugLogAction("главное меню", "открыты настройки");
            return true;

        case Action::OpenModel: {
            std::string p = openFileDialog();
            if (p.empty()) {
                state().lastError = _TR("No file selected", "Файл не выбран");
                return false;
            }
            if (!loadModel(p)) {
                state().lastError = std::string(_TR("Failed to load: ", "Не удалось открыть: ")) + p;
                caeLog(2, "геометрия", "не удалось загрузить модель: %s", p.c_str());
                return false;
            }
            rememberModel(p);
            caeLog(0, "геометрия", "модель загружена: %s (вершин %d)", p.c_str(), modelVertexCount);
            caeSyncGeometry();
            st.workspace = 0;
            return true;
        }

        case Action::DemoSphere:
        case Action::DemoWing:
        case Action::DemoAirship: {
            const int kind = (a == Action::DemoSphere) ? (int)demogeom::Body::Sphere
                           : (a == Action::DemoWing)   ? (int)demogeom::Body::Wing
                                                       : (int)demogeom::Body::Airship;
            if (!loadDemoBody(kind)) {
                state().lastError = _TR("Failed to build the demo body", "Не удалось построить демо-тело");
                caeLog(2, "геометрия", "демо-тело не построено (вид %d)", kind);
                return false;
            }
            const char* name = (currentLanguage == Language::Russian)
                                   ? demogeom::nameRu((demogeom::Body)kind)
                                   : demogeom::nameEn((demogeom::Body)kind);
            caeLog(0, "геометрия", "загружено демо-тело «%s» (%d вершин)", name, modelVertexCount);
            caeSyncGeometry();
            state().lastOpened = std::string("demo:") + name;
            st.workspace = 0;
            return true;
        }

        case Action::LoadStudy: {
            std::string p = openFileDialog();
            if (p.empty()) {
                state().lastError = _TR("No file selected", "Файл не выбран");
                return false;
            }
            // caeLoadStudy() сам спрашивает путь; передать уже выбранный нельзя,
            // поэтому честно сообщаем об этом поведении в сообщениях.
            caeLoadStudy();
            st.workspace = 1;
            return true;
        }
    }
    return false;
}

// Открыть список недавних моделей: путь передаётся в loadModel напрямую.
inline bool openRecent(const std::string& path, UiLayoutState& st) {
    state().lastError.clear();
    if (path.empty()) return false;
    if (!loadModel(path)) {
        state().lastError = std::string(_TR("Failed to load: ", "Не удалось открыть: ")) + path;
        caeLog(2, "геометрия", "не удалось открыть недавнюю модель: %s", path.c_str());
        // Битый/удалённый путь убираем из списка, иначе он останется навсегда.
        recentfiles::add(path, recentfiles::defaultPath(), 6);
        return false;
    }
    rememberModel(path);
    caeLog(0, "геометрия", "модель загружена: %s (вершин %d)", path.c_str(), modelVertexCount);
    caeSyncGeometry();
    st.workspace = 0;
    return true;
}

// -----------------------------------------------------
// Отрисовка. Экран занимает всё окно и рисуется ПОВЕРХ оболочки, поэтому
// нижележащие панели не перехватывают мышь.
// -----------------------------------------------------
inline void draw(UiLayoutState& st) {
    if (!aeroStartScreen) return;

    ImGuiIO& io = ImGui::GetIO();
    const ImVec2 screen = io.DisplaySize;
    const float W = (screen.x > 320.0f) ? screen.x : 800.0f;
    const float H = (screen.y > 240.0f) ? screen.y : 600.0f;
    const float scale = (st.fontScale > 0.5f) ? st.fontScale : 1.0f;

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(W, H));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.055f, 0.065f, 0.080f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.075f, 0.085f, 0.105f, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                   ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                                   ImGuiWindowFlags_NoSavedSettings |
                                   ImGuiWindowFlags_NoBringToFrontOnFocus;

    if (ImGui::Begin("##AerosMainMenu", nullptr, flags)) {
        ImGui::SetWindowFontScale(scale);

        // ----- Заголовок -----
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.45f, 0.85f, 1.0f, 1.0f));
        ImGui::SetWindowFontScale(scale * 1.9f);
        ImGui::TextUnformatted("AEROS ENGINE");
        ImGui::SetWindowFontScale(scale);
        ImGui::PopStyleColor();

        ImGui::TextDisabled("%s %s", _TR("version", "версия"), AEROS_VERSION_STRING);
        ImGui::TextWrapped("%s", _TR("CAD/CAE environment: geometry, CFD (LBM), FEM structural and thermal analyses.",
                                     "Среда CAD/CAE: геометрия, CFD (LBM), прочностной и тепловой анализ МКЭ."));
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        const float colW = (W > 900.0f * scale) ? W * 0.52f : W * 0.58f;
        const float rowH = 34.0f * scale;
        // Высота нижней строки (флажок + кнопки) резервируется ЗАРАНЕЕ.
        // Раньше колонки занимали всю оставшуюся высоту, и нижняя строка
        // оказывалась за границей окна — её просто не было видно.
        const float bottomH = ImGui::GetFrameHeightWithSpacing() * 2.2f + 8.0f * scale;
        const float columnsH = (H - bottomH - ImGui::GetCursorPosY() > 80.0f * scale)
                                   ? (H - bottomH - ImGui::GetCursorPosY())
                                   : 80.0f * scale;

        // ----- Левая колонка: действия -----
        ImGui::BeginChild("##startActions", ImVec2(colW, columnsH), false);
        ImGui::TextDisabled("%s", _TR("START", "НАЧАТЬ РАБОТУ"));
        ImGui::Spacing();

        auto bigButton = [&](const char* label, const char* hint) -> bool {
            const bool pressed = ImGui::Button(label, ImVec2(colW - 24.0f * scale, rowH));
            if (hint && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", hint);
            return pressed;
        };

        if (bigButton(_TR("Open model (STL)…", "Открыть модель (STL)…"),
                      _TR("Reads an STL file and rebuilds voxels, SDF, solver and particles",
                          "Читает STL и заново строит воксели, SDF, решатель и частицы"))) {
            if (perform(Action::OpenModel, st)) aeroStartScreen = false;
        }
        ImGui::Spacing();
        ImGui::TextDisabled("%s", _TR("Demo bodies (no file needed)", "Демо-тела (файл не нужен)"));
        if (bigButton(_TR("Sphere", "Сфера"),
                      _TR("Ellipsoid 1:1:1 — textbook flow around a sphere",
                          "Эллипсоид 1:1:1 — классическое обтекание сферы"))) {
            if (perform(Action::DemoSphere, st)) aeroStartScreen = false;
        }
        if (bigButton(_TR("Wing", "Крыло"),
                      _TR("Thin ellipsoid 2.4 x 0.18 x 6.0 m — lift and tip vortices",
                          "Тонкий эллипсоид 2.4 x 0.18 x 6.0 м — подъёмная сила и концевые вихри"))) {
            if (perform(Action::DemoWing, st)) aeroStartScreen = false;
        }
        if (bigButton(_TR("Airship", "Дирижабль"),
                      _TR("Elongated body 8 x 1.4 x 1.4 m — attached flow and wake",
                          "Вытянутое тело 8 x 1.4 x 1.4 м — безотрывное обтекание и след"))) {
            if (perform(Action::DemoAirship, st)) aeroStartScreen = false;
        }
        ImGui::Spacing();
        if (bigButton(_TR("Load study…", "Загрузить исследование…"),
                      _TR("Restores a saved CAE study with geometry and results",
                          "Восстанавливает сохранённое исследование с геометрией и результатами"))) {
            if (perform(Action::LoadStudy, st)) aeroStartScreen = false;
        }
        if (bigButton(_TR("Settings", "Настройки"), nullptr)) {
            if (perform(Action::Settings, st)) aeroStartScreen = false;
        }

        // Ошибка последнего действия — прямо здесь, не только в журнале.
        if (!state().lastError.empty()) {
            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.45f, 0.38f, 1.0f));
            ImGui::TextWrapped("%s", state().lastError.c_str());
            ImGui::PopStyleColor();
        }
        ImGui::EndChild();

        // Правая колонка ставится рядом только если для неё есть место:
        // при узком окне (и при крупном шрифте) она уезжает вниз, а не
        // сжимается в ноль пикселей.
        const float rightAvail = W - colW - 40.0f * scale;
        if (rightAvail > 260.0f * scale) ImGui::SameLine();
        else ImGui::Spacing();

        // ----- Правая колонка: недавние + справка -----
        ImGui::BeginChild("##startRight", ImVec2(rightAvail > 260.0f * scale ? 0.0f : -1.0f, columnsH), false);
        // Явный правый отступ для переносимого текста: у дочернего окна
        // WorkRect может совпасть с границей, и строка упирается в рамку.
        const float rightTextRight = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - 8.0f;
        ImGui::TextDisabled("%s", _TR("RECENT MODELS", "НЕДАВНИЕ МОДЕЛИ"));
        ImGui::Spacing();
        const std::vector<std::string> recent = recentfiles::load(recentfiles::defaultPath(), 6);
        if (recent.empty()) {
            ImGui::TextDisabled("%s", _TR("The list is empty: open a model or pick a demo body.",
                                          "Список пуст: откройте модель или выберите демо-тело."));
        } else {
            for (size_t i = 0; i < recent.size(); ++i) {
                const std::string label = recentfiles::shortName(recent[i]);
                ImGui::PushID((int)i);
                if (ImGui::Button(label.c_str(), ImVec2(-1, 0))) {
                    if (openRecent(recent[i], st)) aeroStartScreen = false;
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", recent[i].c_str());
                ImGui::PopID();
            }
            if (ImGui::SmallButton(_TR("Clear list", "Очистить список"))) {
                recentfiles::clear(recentfiles::defaultPath());
                debugLogAction("главное меню", "список недавних моделей очищен");
            }
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        ImGui::TextDisabled("%s", _TR("KEYBOARD", "ГОРЯЧИЕ КЛАВИШИ"));
        // Сводка берётся из hotkeys.h: она же используется в справке, поэтому
        // не может отстать от реальных обработчиков.
        ImGui::PushTextWrapPos(rightTextRight);
        ImGui::TextWrapped("%s", _TR(hotkeys::summaryEn(), hotkeys::summaryRu()));
        ImGui::PopTextWrapPos();
        ImGui::TextDisabled("%s", _TR("Full list: Help → Keyboard shortcuts.",
                                      "Полный список: Справка → Горячие клавиши."));
        ImGui::Spacing();
        // ВАЖНО: TextDisabled() не переносит строки (в отличие от TextWrapped),
        // поэтому длинная приписка обрезалась по правому краю панели. Здесь
        // цвет приглушается вручную и текст переносится.
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::PushTextWrapPos(rightTextRight);
        ImGui::TextWrapped("%s", _TR("All menu items perform real actions; unimplemented analyses are not listed.",
                                     "Все пункты меню выполняют настоящие действия; нереализованные анализы не показаны."));
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
        ImGui::EndChild();

        // ----- Нижняя строка -----
        ImGui::Separator();

        // Клавиатурная навигация: подсказка + фактическое действие. Раньше
        // подсказка «Enter — открыть» была бы ложью: Enter ничего не делал.
        // Здесь Enter сразу открывает диалог выбора модели (первое действие
        // экрана), Ctrl+O дублируется из ui.cpp.
        ImGui::TextDisabled("%s", _TR("Enter — open model · Esc — close · Ctrl+O — open model",
                                      "Enter — открыть модель · Esc — закрыть · Ctrl+O — открыть модель"));
        if (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false)) {
            if (perform(Action::OpenModel, st)) aeroStartScreen = false;
        }
        bool showOnStart = aeroStartScreenOnStartup;
        if (ImGui::Checkbox(_TR("Show this screen on startup", "Показывать этот экран при запуске"),
                            &showOnStart)) {
            aeroStartScreenOnStartup = showOnStart;
        }
        ImGui::SameLine();
        if (ImGui::Button(_TR("Continue without a model", "Продолжить без модели"), ImVec2(0, 0))) {
            if (perform(Action::Continue, st)) aeroStartScreen = false;
        }
        ImGui::SameLine();
        if (ImGui::Button(_TR("Exit", "Выход"))) {
            if (perform(Action::Exit, st)) aeroStartScreen = false;
        }
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);
}

}  // namespace startscreen
