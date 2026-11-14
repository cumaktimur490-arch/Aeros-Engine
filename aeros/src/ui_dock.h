#ifndef AEROS_UI_DOCK_H
#define AEROS_UI_DOCK_H
// =====================================================
// Aeros Engine — закрепление панелей средствами приложения ( Dear ImGui )
//
// ВАЖНО: в репозитории лежит Dear ImGui 1.92.9b БЕЗ ветки docking
// (IMGUI_HAS_DOCK нигде не определён, ImGuiDockNode* отсутствует). Проверено
// перед выбором подхода. Поэтому «пристыкованные» панели реализованы здесь:
// позиция и размер каждого окна задаются КАЖДЫЙ кадр из прямоугольников, которые
// посчитал ui_layout.h, плюс флаги NoMove|NoResize|NoCollapse|NoSavedSettings.
// Свободно плавающих окон в оболочке не остаётся — всплывать могут только меню
// (ImGui::BeginMenu) и модальные диалоги.
//
// Разделители (splitters) — тонкие невидимые окна поверх границ панелей:
// перетаскивание меняет размеры в UiLayoutState, которые сохраняются в настройках.
// =====================================================

#include <imgui.h>
#include <cstdio>
#include <string>

#include "ui_layout.h"
#include "lang.h"

// Флаги пристыкованной панели: нельзя двигать, менять размер, сворачивать в
// «таблетку», и состояние не пишется в imgui.ini (иначе конфликтует с нашими).
inline ImGuiWindowFlags uiDockPanelFlags() {
    return ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
           ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus |
           ImGuiWindowFlags_NoNavFocus;
}

// -----------------------------------------------------
// Панель: открыть по прямоуготнику из компоновки. Возвращает false, если панель
// свёрнута (тогда между Begin/End ничего рисовать не нужно, но End обязателен).
// -----------------------------------------------------
inline bool uiDockBeginPanel(const char* title, const UiRect& r, float fontScale) {
    if (!r.valid()) {
        // окна нет — Begin/End не вызываем вовсе
        return false;
    }
    ImGui::SetNextWindowPos(ImVec2(r.x, r.y));
    ImGui::SetNextWindowSize(ImVec2(r.w, r.h));
    const bool open = ImGui::Begin(title, nullptr, uiDockPanelFlags());
    if (open && fontScale > 0.5f) ImGui::SetWindowFontScale(fontScale);
    return open;
}

inline void uiDockEndPanel() { ImGui::End(); }

// Открыть панель и сразу войти в её строку меню (нужно для главного меню).
// ВАЖНО: в отличие от uiDockBeginPanel(), здесь false означает «окно НЕ открыто,
// End вызывать не нужно». ImGui::Begin() обязан сопровождаться End() даже когда
// вернул false (окно свёрнуто или полностью отсечено), поэтому закрываем окно
// прямо здесь: иначе вызывающий код, написанный как `if (Begin…) { …; End…; }`,
// оставляет несбалансированный стек окон — в отладочной сборке это IM_ASSERT,
// в релизной разъехавшиеся панели и падение.
inline bool uiDockBeginMenuBar(const UiRect& r, float fontScale) {
    if (!r.valid()) return false;
    ImGui::SetNextWindowPos(ImVec2(r.x, r.y));
    ImGui::SetNextWindowSize(ImVec2(r.w, r.h));
    const bool open = ImGui::Begin("##AerosMainMenuBar", nullptr,
                                   uiDockPanelFlags() | ImGuiWindowFlags_MenuBar |
                                   ImGuiWindowFlags_NoScrollbar);
    if (!open) { ImGui::End(); return false; }
    if (fontScale > 0.5f) ImGui::SetWindowFontScale(fontScale);
    return true;
}
inline void uiDockEndMenuBar() { ImGui::End(); }

// -----------------------------------------------------
// Строка состояния — постоянная полоса у самого низа окна. Не закрывается,
// не двигается, всегда видна: по требованию к оболочке САПР.
// -----------------------------------------------------
// Как и uiDockBeginMenuBar(): false означает, что окно уже закрыто здесь и End
// вызывать не нужно (см. пояснение выше).
inline bool uiDockBeginStatusBar(const UiRect& r, float fontScale) {
    if (!r.valid()) return false;
    ImGui::SetNextWindowPos(ImVec2(r.x, r.y));
    ImGui::SetNextWindowSize(ImVec2(r.w, r.h));
    ImGui::SetNextWindowBgAlpha(1.0f);
    const bool open = ImGui::Begin("##AerosStatusBar", nullptr,
                                   uiDockPanelFlags() | ImGuiWindowFlags_NoTitleBar |
                                   ImGuiWindowFlags_NoScrollbar);
    if (!open) { ImGui::End(); return false; }
    if (fontScale > 0.5f) ImGui::SetWindowFontScale(fontScale);
    return true;
}
inline void uiDockEndStatusBar() { ImGui::End(); }

// -----------------------------------------------------
// Разделитель панелей. vertical=true — тянет ширину (дерево/свойства),
// invert=true — движение мыши вправо уменьшает значение (панель у правого края).
// Изменяет *size напрямую; вызывающий код обязан сохранить его в настройках.
// -----------------------------------------------------
inline void uiDockDrawSplitter(const char* id, const UiRect& track, bool vertical,
                               bool invert, float* size, float minSize, float maxSize) {
    if (!track.valid() || !size) return;
    ImGui::PushID(id);
    ImGui::SetNextWindowPos(ImVec2(track.x, track.y));
    ImGui::SetNextWindowSize(ImVec2(track.w, track.h));
    ImGui::SetNextWindowBgAlpha(0.0f);
    const ImGuiWindowFlags f = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                               ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar |
                               ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                               ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground |
                               ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoFocusOnAppearing;
    if (ImGui::Begin("##splitter", nullptr, f)) {
        const ImVec2 sz(track.w, track.h);
        if (ImGui::InvisibleButton("##handle", sz)) {}
        const bool hovered = ImGui::IsItemHovered();
        const bool active  = ImGui::IsItemActive();
        if (hovered || active)
            ImGui::SetMouseCursor(vertical ? ImGuiMouseCursor_ResizeEW : ImGuiMouseCursor_ResizeNS);
        if (active && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            const float d = vertical ? ImGui::GetIO().MouseDelta.x : -ImGui::GetIO().MouseDelta.y;
            *size = uiLayoutDragSize(*size, d, invert, minSize, maxSize);
        }
        // подсветка ручки, чтобы было видно, за что можно тянуть
        if (hovered || active) {
            const ImU32 col = ImGui::GetColorU32(ImVec4(0.35f, 0.62f, 0.95f, active ? 0.85f : 0.45f));
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const ImVec2 p = ImGui::GetWindowPos();
            if (vertical) {
                const float cx = p.x + track.w * 0.5f;
                dl->AddLine(ImVec2(cx, p.y + 2.0f), ImVec2(cx, p.y + track.h - 2.0f), col, 2.0f);
            } else {
                const float cy = p.y + track.h * 0.5f;
                dl->AddLine(ImVec2(p.x + 2.0f, cy), ImVec2(p.x + track.w - 2.0f, cy), col, 2.0f);
            }
        }
    }
    ImGui::End();
    ImGui::PopID();
}

// Все три разделителя сразу (вызывается ПОСЛЕ панелей, чтобы ручки были сверху).
inline void uiDockDrawSplitters(UiLayoutState& st, const UiLayoutRects& L) {
    if (st.maximized) return;
    if (L.treeVisible)
        uiDockDrawSplitter("tree", L.spTree, true, false, &st.treeW,
                           UiLayoutState::minTreeW(), UiLayoutState::maxTreeW());
    if (L.propsVisible)
        uiDockDrawSplitter("props", L.spProps, true, true, &st.propsW,
                           UiLayoutState::minPropsW(), UiLayoutState::maxPropsW());
    if (L.bottomVisible)
        uiDockDrawSplitter("bottom", L.spBottom, false, false, &st.bottomH,
                           UiLayoutState::minBottomH(), UiLayoutState::maxBottomH());
}

// -----------------------------------------------------
// Переключатель рабочих пространств (вверху дерева проекта). Имена и подсказки —
// в ui_layout.h: они нужны и headless-тесту, который ImGui не линкует.
// -----------------------------------------------------
// Кнопки-вкладки пространств. Возвращает true, если пространство сменилось.
inline bool uiDockWorkspaceTabs(UiLayoutState& st) {
    bool changed = false;
    for (int i = 0; i < UI_WORKSPACE_COUNT; ++i) {
        if (i > 0) ImGui::SameLine(0.0f, 4.0f);
        const float w = (ImGui::GetContentRegionAvail().x - 8.0f) / 3.0f;
        // Решение «подсвечивать ли кнопку» принимается ДО отрисовки и запоминается:
        // сама кнопка меняет st.workspace, и прежний вариант (push до кнопки, pop
        // после по тому же условию) при клике делал PopStyleColor(3) БЕЗ парного
        // push. В отладочной сборке ImGui падал на ассерте «PopStyleColor() too
        // many times», а в релизной (NDEBUG) стек стилей уезжал в отрицательный
        // размер и молча портил цвета последующих виджетов — в частности, текст
        // в панелях переставал отображаться.
        const bool styled = (st.workspace == i);
        if (styled) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.24f, 0.45f, 0.78f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.30f, 0.53f, 0.86f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.18f, 0.38f, 0.68f, 1.0f));
        }
        char id[64];
        std::snprintf(id, sizeof(id), "%s##ws%d", uiWorkspaceName(i), i);
        if (ImGui::Button(id, ImVec2(w > 40.0f ? w : 40.0f, 0.0f))) {
            if (st.workspace != i) { st.workspace = i; changed = true; }
        }
        if (styled) ImGui::PopStyleColor(3);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", uiWorkspaceHint(i));
    }
    return changed;
}

// -----------------------------------------------------
// Нижние вкладки: сообщения, сходимость, графики, результаты, журнал.
// Имена вкладок — uiBottomTabName() в ui_layout.h: они нужны и headless-тесту,
// который ImGui не линкует.
// -----------------------------------------------------

// Открыть нижнюю панель и войти в активную вкладку. Возвращает индекс активной
// вкладки (или -1, если панель/вкладка недоступны — тогда End не вызывать).
inline int uiDockBeginBottom(const UiRect& r, float fontScale, int* tabInOut,
                             const bool* tabEnabled) {
    if (!r.valid()) return -1;
    ImGui::SetNextWindowPos(ImVec2(r.x, r.y));
    ImGui::SetNextWindowSize(ImVec2(r.w, r.h));
    if (!ImGui::Begin(_TR("Console & convergence monitor", "Консоль и мониторинг сходимости"),
                      nullptr, uiDockPanelFlags())) {
        ImGui::End();
        return -1;
    }
    if (fontScale > 0.5f) ImGui::SetWindowFontScale(fontScale);
    if (!ImGui::BeginTabBar("##bottomTabs", ImGuiTabBarFlags_None)) {
        ImGui::End();
        return -1;
    }
    int active = -1;
    int cur = (tabInOut && *tabInOut >= 0 && *tabInOut < UI_BOTTOM_TAB_COUNT) ? *tabInOut : 0;
    for (int t = 0; t < UI_BOTTOM_TAB_COUNT; ++t) {
        if (tabEnabled && !tabEnabled[t]) continue;
        char id[96];
        std::snprintf(id, sizeof(id), "%s##bt%d", uiBottomTabName(t), t);
        const bool sel = (t == cur);
        bool open = true;
        if (ImGui::BeginTabItem(id, &open, sel ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None)) {
            active = t;
            if (tabInOut) *tabInOut = t;
            return active;   // EndTabItem/EndTabBar/End делает uiDockEndBottom
        }
    }
    ImGui::EndTabBar();
    ImGui::End();
    return -1;
}
inline void uiDockEndBottom() {
    ImGui::EndTabItem();
    ImGui::EndTabBar();
    ImGui::End();
}

// -----------------------------------------------------
// Вспомогательные рисовалки для панелей оболочки.
// -----------------------------------------------------
// Цветовой маркер состояния узла дерева (не настроено / готово / предупреждение /
// ошибка / решено) — маленький кружок перед подписью.
inline void uiDockStateDot(int stateKind) {
    ImVec4 col(0.55f, 0.55f, 0.55f, 1.0f);
    switch (stateKind) {
        case 1: col = ImVec4(0.35f, 0.78f, 0.35f, 1.0f); break;  // готово
        case 2: col = ImVec4(0.95f, 0.75f, 0.15f, 1.0f); break;  // предупреждение
        case 3: col = ImVec4(0.92f, 0.28f, 0.24f, 1.0f); break;  // ошибка
        case 4: col = ImVec4(0.30f, 0.62f, 0.95f, 1.0f); break;  // решено
        default: break;                                          // не настроено
    }
    const float r = ImGui::GetFontSize() * 0.28f;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddCircleFilled(
        ImVec2(p.x + r, p.y + ImGui::GetFontSize() * 0.5f), r, ImGui::GetColorU32(col));
    ImGui::Dummy(ImVec2(r * 2.0f + 4.0f, 0.0f));
    ImGui::SameLine(0.0f, 2.0f);
}

// Градиентная полоса легенды с подписями минимума и максимума в единицах
// интерфейса. Цвета берёт вызывающий код (тот же рамп, что у карты давления, —
// чтобы раскраска результатов и CFD выглядела одинаково).
inline void uiDockLegendBar(float width, float height,
                            const char* caption, const char* loText, const char* hiText,
                            ImU32 (*rampFn)(float)) {
    if (!caption) caption = "";
    ImGui::TextUnformatted(caption);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const int steps = 64;
    const float seg = (width > 1.0f) ? width / (float)steps : 1.0f;
    for (int i = 0; i < steps; ++i) {
        const float t0 = (float)i / (float)steps, t1 = (float)(i + 1) / (float)steps;
        dl->AddRectFilled(ImVec2(p.x + seg * i, p.y), ImVec2(p.x + seg * (i + 1) + 0.5f, p.y + height),
                          rampFn ? rampFn(0.5f * (t0 + t1)) : IM_COL32(128, 128, 128, 255));
    }
    dl->AddRect(p, ImVec2(p.x + width, p.y + height), IM_COL32(255, 255, 255, 90));
    ImGui::Dummy(ImVec2(width, height));
    ImGui::TextUnformatted(loText ? loText : "");
    ImGui::SameLine(width * 0.55f);
    ImGui::TextUnformatted(hiText ? hiText : "");
}

#endif // AEROS_UI_DOCK_H
