#ifndef AEROS_UI_LAYOUT_H
#define AEROS_UI_LAYOUT_H
// =====================================================
// Aeros Engine — геометрия закреплённой компоновки интерфейса (CAE-оболочка)
//
// Почему это отдельный модуль без ImGui:
//   * правила компоновки (что где, какие минимумы, как ведут себя сплиттеры)
//     можно проверить headless-тестом, а не «на глаз»;
//   * сам Dear ImGui 1.92.9b в репозитории БЕЗ ветки docking (IMGUI_HAS_DOCK = 0),
//     поэтому закрепление панелей реализуется кодом приложения. Этот файл —
//     чистая математика прямоугольников; рисование — в ui_dock.h.
//
// Компоновка как в профессиональных инженерных пакетах:
//   ┌────────────────────────── главное меню ──────────────────────────┐
//   │ дерево │            3D-вид             │      свойства           │
//   │проекта │                               │ выбранного объекта      │
//   ├────────┴───────────────────────────────┴─────────────────────────┤
//   │ нижние вкладки: сообщения | сходимость | графики | результаты    │
//   ├─────────────────────────── строка состояния ─────────────────────┤
//
// Никаких свободно плавающих окон: всплывать могут только меню и диалоги.
// Размеры панелей меняются перетаскиванием разделителей, панели сворачиваются,
// 3D-вид можно временно развернуть на всё окно. Компоновка сохраняется между
// запусками (см. uiLayoutWrite / uiLayoutApplyValue).
// =====================================================

#include "lang.h"     // _TR — подписи пространств и вкладок локализованы

#include <string>
#include <cmath>
#include <cstdio>
#include <algorithm>
#include <ostream>

// Рабочие пространства. Каждое — реально реализованный набор панелей, а не
// заглушка: «Модель» = геометрия и структура проекта, «Исследование» =
// материалы/нагрузки/закрепления/сетка, «Результаты» = поля/графики/срезы/отчёт.
enum class UiWorkspace { Model = 0, Study = 1, Results = 2 };
static const int UI_WORKSPACE_COUNT = 3;

// Нижние вкладки.
enum class UiBottomTab { Messages = 0, Convergence = 1, Plots = 2, Results = 3, Journal = 4 };
static const int UI_BOTTOM_TAB_COUNT = 5;

// -----------------------------------------------------
// Имена рабочих пространств и нижних вкладок. Модель — геометрия и структура
// проекта; Исследование — материалы, нагрузки, закрепления, сетка; Результаты —
// поля, срезы, графики, отчёт.
// -----------------------------------------------------
inline const char* uiWorkspaceName(int w) {
    switch (w) {
        case 1:  return _TR("Study", "Исследование");
        case 2:  return _TR("Results", "Результаты");
        default: return _TR("Model", "Модель");
    }
}
inline const char* uiWorkspaceHint(int w) {
    switch (w) {
        case 1:  return _TR("Materials, loads, constraints, mesh",
                            "Материалы, нагрузки, закрепления, сетка");
        case 2:  return _TR("Fields, legend, slices, plots, report",
                            "Поля, легенда, срезы, графики, отчёт");
        default: return _TR("Geometry and project structure", "Геометрия и структура проекта");
    }
}
inline const char* uiBottomTabName(int t) {
    switch (t) {
        case 1:  return _TR("Convergence", "Сходимость");
        case 2:  return _TR("Plots", "Графики");
        case 3:  return _TR("Results", "Результаты");
        case 4:  return _TR("Journal", "Журнал");
        default: return _TR("Messages", "Сообщения");
    }
}

struct UiRect {
    float x = 0, y = 0, w = 0, h = 0;
    bool valid() const { return w > 1.0f && h > 1.0f; }
    float right()  const { return x + w; }
    float bottom() const { return y + h; }
};

// Состояние компоновки — то, что сохраняется между запусками.
struct UiLayoutState {
    int  workspace     = 0;      // UiWorkspace
    int  bottomTab     = 0;      // UiBottomTab
    bool menuBar       = true;   // главное меню сверху
    bool treePanel     = true;   // дерево проекта слева
    bool propsPanel    = true;   // панель свойств справа
    bool bottomPanel   = true;   // нижние вкладки
    bool maximized     = false;  // 3D-вид временно на всё окно (Esc/F12 — обратно)
    bool panelsVisible = true;   // мастер-переключатель (F12): скрывает доки, меню и статус остаются

    float menuH    = 24.0f;
    float statusH  = 22.0f;      // строка состояния — постоянная, не отключается
    float splitter = 6.0f;       // толщина «ручки» разделителя
    float treeW    = 264.0f;
    float propsW   = 470.0f;
    float bottomH  = 200.0f;
    float fontScale = 1.0f;

    // Ограничения — чтобы панель нельзя было утянуть в ноль или на весь экран.
    static float minTreeW()   { return 150.0f; }
    static float maxTreeW()   { return 600.0f; }
    static float minPropsW()  { return 240.0f; }
    static float maxPropsW()  { return 1200.0f; }
    static float minBottomH() { return 90.0f;  }
    static float maxBottomH() { return 700.0f; }

    void clampToScreen(float W, float H) {
        if (!(std::isfinite(W)) || W < 320.0f) W = 800.0f;
        if (!(std::isfinite(H)) || H < 240.0f) H = 600.0f;
        if (workspace < 0 || workspace >= UI_WORKSPACE_COUNT) workspace = 0;
        if (bottomTab < 0 || bottomTab >= UI_BOTTOM_TAB_COUNT) bottomTab = 0;
        treeW   = std::min(std::max(treeW,   minTreeW()),   maxTreeW());
        propsW  = std::min(std::max(propsW,  minPropsW()),  maxPropsW());
        bottomH = std::min(std::max(bottomH, minBottomH()), maxBottomH());
        menuH   = std::min(std::max(menuH,   18.0f), 48.0f);
        statusH = std::min(std::max(statusH, 18.0f), 48.0f);
        splitter= std::min(std::max(splitter,4.0f), 16.0f);
        fontScale = std::min(std::max(fontScale, 0.7f), 2.0f);
        // нижняя панель не может занимать больше половины рабочей высоты
        const float avail = H - menuH - statusH;
        if (avail > 40.0f && bottomH > avail * 0.55f) bottomH = avail * 0.55f;
        // Боковые панели вместе не должны съедать больше 60% ширины: на маленьком
        // экране 3D-вид обязан оставаться рабочим, а не превращаться в щель.
        // Но минимумы панелей — жёсткие: если окно настолько узкое, что 60% не
        // хватает даже на минимальные панели, приоритет у панелей (иначе в них
        // нельзя ни прочитать значение, ни попасть мышью).
        if (treeW + propsW > W * 0.60f) {
            const float k = (W * 0.60f) / (treeW + propsW);
            treeW  = std::max(treeW  * k, minTreeW());
            propsW = std::max(propsW * k, minPropsW());
        }
    }
};

// Единственный экземпляр состояния компоновки (header-only модуль, как
// studyProject()). Все панели читают и пишут его; прежние глобальные ключи
// (uiShowTree/uiTreeW/...) обновляются из него каждый кадр, поэтому старый код и
// сохранённые настройки продолжают работать.
inline UiLayoutState& uiLayoutState() { static UiLayoutState s; return s; }

struct UiLayoutRects {
    UiRect menu;        // главное меню
    UiRect tree;        // дерево проекта
    UiRect props;       // свойства
    UiRect bottom;      // нижние вкладки
    UiRect viewport;    // 3D-вид
    UiRect status;      // строка состояния (всегда есть)
    // зоны перетаскивания разделителей
    UiRect spTree;      // вертикальная, между деревом и видом
    UiRect spProps;     // вертикальная, между видом и свойствами
    UiRect spBottom;    // горизонтальная, между видом и нижними вкладками
    bool treeVisible = false, propsVisible = false, bottomVisible = false;
    bool maximized = false, menuVisible = false;
};

// Главный расчёт компоновки. Детерминированная функция состояния и размера окна:
// легко проверяется тестом и не зависит от ImGui.
inline UiLayoutRects uiLayoutCompute(const UiLayoutState& s, float W, float H) {
    UiLayoutState st = s;
    st.clampToScreen(W, H);
    if (!(W > 0)) W = 800.0f;
    if (!(H > 0)) H = 600.0f;

    UiLayoutRects L;
    const bool showDocks = st.panelsVisible && !st.maximized;
    L.maximized = st.maximized;

    // --- строка состояния: постоянная, во всю ширину, у самого низа окна ---
    L.status = UiRect{ 0.0f, H - st.statusH, W, st.statusH };

    // --- главное меню: только сверху, во всю ширину ---
    L.menuVisible = st.menuBar;
    float y0 = 0.0f;
    if (st.menuBar) {
        L.menu = UiRect{ 0.0f, 0.0f, W, st.menuH };
        y0 = st.menuH;
    } else {
        L.menu = UiRect{ 0, 0, 0, 0 };
    }

    const float y1 = L.status.y;          // низ рабочей области
    float availH = y1 - y0;
    if (availH < 1.0f) availH = 1.0f;

    // --- нижние вкладки: во всю ширину рабочей области ---
    L.bottomVisible = st.bottomPanel && showDocks;
    float bottomH = 0.0f;
    if (L.bottomVisible) {
        bottomH = std::min(st.bottomH, availH * 0.55f);
        if (bottomH < 24.0f) bottomH = 24.0f;
        L.bottom = UiRect{ 0.0f, y1 - bottomH, W, bottomH };
    } else {
        L.bottom = UiRect{ 0, 0, 0, 0 };   // вкладок нет — вид занимает их место
    }

    const float panelsTop = y0;
    const float panelsBot = y1 - (L.bottomVisible ? bottomH : 0.0f);
    float panelsH = panelsBot - panelsTop;
    if (panelsH < 1.0f) panelsH = 1.0f;

    // --- боковые панели ---
    float treeW  = (st.treePanel  && showDocks) ? st.treeW  : 0.0f;
    float propsW = (st.propsPanel && showDocks) ? st.propsW : 0.0f;
    // 1) на нормальном окне панели вместе не шире 60% — иначе вид превращается в щель
    if (treeW + propsW > W * 0.60f) {
        const float k = (W * 0.60f) / (treeW + propsW);
        treeW  = std::max(treeW  * k, UiLayoutState::minTreeW());
        propsW = std::max(propsW * k, UiLayoutState::minPropsW());
    }
    // 2) экстремально узкое окно: здесь уже минимумы панелей не выдерживаются,
    //    панели ужимаются ради вида; если места не остаётся вовсе — скрываются
    {
        const float vpMin = std::min(120.0f, W * 0.25f);
        const float sum = treeW + propsW;
        if (sum > 0.0f && sum + vpMin > W) {
            const float room = std::max(W - vpMin, 0.0f);
            if (room < 80.0f) { treeW = 0.0f; propsW = 0.0f; }
            else { const float k = room / sum; treeW *= k; propsW *= k; }
        }
    }
    L.treeVisible  = treeW  > 20.0f;
    L.propsVisible = propsW > 20.0f;
    if (!L.treeVisible)  treeW  = 0.0f;
    if (!L.propsVisible) propsW = 0.0f;
    if (treeW > 0.0f) L.tree = UiRect{ 0.0f, panelsTop, treeW, panelsH };
    if (propsW > 0.0f) L.props = UiRect{ W - propsW, panelsTop, propsW, panelsH };

    // --- 3D-вид: всё, что осталось в центре ---
    const float vx = (treeW > 0.0f) ? treeW : 0.0f;
    const float vw = std::max(W - treeW - propsW, 1.0f);
    if (st.maximized) {
        L.viewport = UiRect{ 0.0f, y0, W, y1 - y0 };
    } else {
        L.viewport = UiRect{ vx, panelsTop, vw, panelsH };
    }

    // --- разделители: тонкие полосы на границах панелей, только там, где есть
    //     соседняя видимая панель (иначе тянуть нечего) ---
    if (L.treeVisible && !st.maximized)
        L.spTree = UiRect{ L.tree.right() - st.splitter * 0.5f, panelsTop, st.splitter, panelsH };
    if (L.propsVisible && !st.maximized)
        L.spProps = UiRect{ L.props.x - st.splitter * 0.5f, panelsTop, st.splitter, panelsH };
    if (L.bottomVisible && !st.maximized)
        L.spBottom = UiRect{ 0.0f, L.bottom.y - st.splitter * 0.5f, W, st.splitter };

    return L;
}

// -----------------------------------------------------
// Перетаскивание разделителя: новое значение размера.
// vertical=true  — разделитель двигает ширину (дерево/свойства);
// invert=true    — рост delta уменьшает значение (панель справа).
// -----------------------------------------------------
inline float uiLayoutDragSize(float current, float delta, bool invert, float minV, float maxV) {
    float v = current + (invert ? -delta : delta);
    if (!(std::isfinite(v))) v = current;
    v = std::min(std::max(v, minV), maxV);
    return v;
}

// -----------------------------------------------------
// Сохранение/загрузка компоновки. Ключи — в тот же aeros_settings.ini, что и
// остальные настройки, поэтому старая раскладка пользователя не теряется:
// прежние ключи (uiShowTree/uiTreeW/...) принимаются как устаревшие имена.
// -----------------------------------------------------
inline void uiLayoutWrite(std::ostream& f, const UiLayoutState& s) {
    char b[128];
    auto b1 = [](bool v) { return v ? 1 : 0; };
    std::snprintf(b, sizeof(b), "layoutWorkspace=%d\n", s.workspace);        f << b;
    std::snprintf(b, sizeof(b), "layoutBottomTab=%d\n", s.bottomTab);        f << b;
    std::snprintf(b, sizeof(b), "layoutMenuBar=%d\n", b1(s.menuBar));        f << b;
    std::snprintf(b, sizeof(b), "layoutTree=%d\n", b1(s.treePanel));         f << b;
    std::snprintf(b, sizeof(b), "layoutProps=%d\n", b1(s.propsPanel));       f << b;
    std::snprintf(b, sizeof(b), "layoutBottom=%d\n", b1(s.bottomPanel));     f << b;
    std::snprintf(b, sizeof(b), "layoutPanels=%d\n", b1(s.panelsVisible));   f << b;
    std::snprintf(b, sizeof(b), "layoutTreeW=%.1f\n", s.treeW);              f << b;
    std::snprintf(b, sizeof(b), "layoutPropsW=%.1f\n", s.propsW);            f << b;
    std::snprintf(b, sizeof(b), "layoutBottomH=%.1f\n", s.bottomH);          f << b;
    std::snprintf(b, sizeof(b), "layoutFontScale=%.2f\n", s.fontScale);      f << b;
    // намеренно НЕ сохраняем maximized: полноэкранный вид — временный режим
}

// Возвращает true, если ключ распознан (и применён) этим модулем.
inline bool uiLayoutApplyValue(UiLayoutState& s, const std::string& key, const std::string& val) {
    auto toF = [&]() {
        float v = 0.0f;
        try { v = std::stof(val); } catch (...) { return 0.0f; }
        return std::isfinite(v) ? v : 0.0f;
    };
    auto toI = [&]() {
        int v = 0;
        try { v = std::stoi(val); } catch (...) { return 0; }
        return v;
    };
    if      (key == "layoutWorkspace") s.workspace   = toI();
    else if (key == "layoutBottomTab") s.bottomTab   = toI();
    else if (key == "layoutMenuBar")   s.menuBar     = toI() != 0;
    else if (key == "layoutTree")      s.treePanel   = toI() != 0;
    else if (key == "layoutProps")     s.propsPanel  = toI() != 0;
    else if (key == "layoutBottom")    s.bottomPanel = toI() != 0;
    else if (key == "layoutPanels")    s.panelsVisible = toI() != 0;
    else if (key == "layoutTreeW")     s.treeW   = toF();
    else if (key == "layoutPropsW")    s.propsW  = toF();
    else if (key == "layoutBottomH")   s.bottomH = toF();
    else if (key == "layoutFontScale") s.fontScale = toF();
    // --- устаревшие ключи v1.28.0 (сохраняем преемственность настроек) ---
    else if (key == "uiDocked")        s.panelsVisible = toI() != 0;
    else if (key == "uiShowTree")      s.treePanel     = toI() != 0;
    else if (key == "uiShowProps")     s.propsPanel    = toI() != 0;
    else if (key == "uiShowConsole")   s.bottomPanel   = toI() != 0;
    else if (key == "uiTreeW")         s.treeW   = (float)toI();
    else if (key == "uiPropsW")        s.propsW  = (float)toI();
    else if (key == "uiBottomH")       s.bottomH = (float)toI();
    else if (key == "uiScale")         s.fontScale = toF();
    else return false;
    s.clampToScreen(1920.0f, 1080.0f);   // защита от мусора в файле
    return true;
}

#endif // AEROS_UI_LAYOUT_H
