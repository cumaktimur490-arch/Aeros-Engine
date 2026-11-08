#pragma once
// =====================================================
// v1.34.0 — единая таблица горячих клавиш.
//
// Зачем. Клавиши были описаны в трёх местах (стартовый экран, подсказки в
// панелях, этот текст в справке), и все три отставали от input.cpp: в них не
// было F6 (CSV), F10 (отчёт), F11 (самописец на весь экран), Ctrl+M (3D-вид на
// всё окно), Ctrl+1…3 (рабочие пространства), Ctrl+Enter (расчёт). Теперь
// таблица одна, и тест сверяет её с обработчиками в input.cpp/ cae_ui.h.
//
// Правило: клавиша попадает в таблицу только если она РЕАЛЬНО обрабатывается.
// =====================================================

#include <imgui.h>
#include <cstddef>

#include "lang.h"   // _TR/TR — описания клавиш на двух языках

namespace hotkeys {

struct Item {
    const char* keys;   // обозначение клавиш (одинаково для обоих языков)
    const char* ru;     // описание по-русски
    const char* en;     // описание по-английски
};

// Группы нужны, чтобы длинный список читался: камера, вид, расчёты, файлы.
enum Group { GroupCamera = 0, GroupView, GroupPhysics, GroupFile, GroupCount };
static const char* const g_groupRu[GroupCount] = {
    "Камера", "Вид и панели", "Расчёт и самописец", "Файлы и отчёты"
};
static const char* const g_groupEn[GroupCount] = {
    "Camera", "View and panels", "Solver and recorder", "Files and reports"
};

// ---- сама таблица (порядок = порядок в справке) ----
inline int count() {
    return 21;
}

inline Item item(int i) {
    static const Item tbl[] = {
        // Камера
        {"W / S",       "вперёд / назад",                          "forward / backward"},
        {"A / D",       "влево / вправо",                          "left / right"},
        {"Space / Q",   "вверх / вниз",                            "up / down"},
        {"E",           "вверх",                                   "up"},
        {"Shift",       "медленное движение камеры",                "slow camera movement"},
        {"Ctrl (камера)","ускорение камеры ×5",                     "camera speed ×5"},
        {"Ctrl+R",      "сбросить камеру",                          "reset camera"},
        // Вид
        {"F1",          "показать / скрыть модель",                 "show / hide model"},
        {"F2",          "карта давления",                           "pressure map"},
        {"F3",          "линии тока",                               "streamlines"},
        {"F4",          "частицы",                                  "particles"},
        {"F12",         "скрыть / показать панели",                 "hide / show panels"},
        {"F9",          "диагностический журнал",                   "diagnostic journal"},
        {"Ctrl+M",      "3D-вид на всё окно",                       "maximize 3D view"},
        {"Ctrl+1…3",    "рабочие пространства: Модель / Расчёт / Результаты",
                                                                   "workspaces: Model / Solve / Results"},
        // Расчёт и самописец
        {"Ctrl+Enter",  "запустить расчёт",                         "run the analysis"},
        {"F7 / F8",     "запись / просмотр самописца",              "record / play recorder"},
        {"F11",         "самописец на всё окно",                    "recorder fullscreen"},
        // Файлы и отчёты
        {"F5",          "скриншот",                                 "screenshot"},
        {"F6",          "экспорт CSV",                              "CSV export"},
        {"F10",         "сохранить отладочный отчёт",                "save debug report"},
    };
    const int n = (int)(sizeof(tbl) / sizeof(tbl[0]));
    if (i < 0 || i >= n) return Item{"", "", ""};
    return tbl[i];
}

// Группа, к которой относится элемент i (индексы фиксированы таблицей выше).
inline int group(int i) {
    if (i <= 6) return GroupCamera;
    if (i <= 14) return GroupView;
    if (i <= 17) return GroupPhysics;
    return GroupFile;
}

// Строка-сводка для стартового экрана: только то, что нужно новичку, чтобы
// начать. Полный список — в справке (Help → Горячие клавиши).
inline const char* summaryRu() {
    return "Esc — закрыть экран · Ctrl+O — открыть модель · F1…F4 — отображение · "
           "F5 — скриншот · F7/F8 — самописец · F9 — журнал · F12 — панели";
}
inline const char* summaryEn() {
    return "Esc — close this screen · Ctrl+O — open model · F1…F4 — display · "
           "F5 — screenshot · F7/F8 — recorder · F9 — journal · F12 — panels";
}

// Модальное окно справки. Открывается через ImGui::OpenPopup("AerosHotkeys").
inline void drawModal(bool* pOpen = nullptr) {
    if (ImGui::BeginPopupModal("AerosHotkeys", pOpen, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted(_TR("Keyboard shortcuts", "Горячие клавиши"));
        ImGui::Separator();
        ImGui::BeginChild("##hk", ImVec2(640, 360), true);
        int lastGroup = -1;
        for (int i = 0; i < count(); ++i) {
            Item it = item(i);
            if (!it.keys || !it.keys[0]) continue;
            int g = group(i);
            if (g != lastGroup) {
                if (lastGroup >= 0) ImGui::Spacing();
                ImGui::TextColored(ImVec4(0.55f, 0.78f, 1.0f, 1.0f), "%s",
                                   g == 0 ? g_groupRu[0] : g == 1 ? g_groupRu[1] : g == 2 ? g_groupRu[2] : g_groupRu[3]);
                lastGroup = g;
            }
            ImGui::BulletText("%-14s — %s", it.keys, _TR(it.en, it.ru));
        }
        ImGui::EndChild();
        ImGui::TextDisabled("%s", _TR("Camera keys are ignored while a text field has focus.",
                                      "Клавиши камеры не работают, пока фокус в поле ввода."));
        if (ImGui::Button(_TR("Close", "Закрыть"), ImVec2(-1, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape, false))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

} // namespace hotkeys
