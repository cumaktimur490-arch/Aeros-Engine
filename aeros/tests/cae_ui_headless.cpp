// Headless-проверка оболочки CAD/CAE на НАСТОЯЩЕМ контексте Dear ImGui, но без
// бэкенда: OpenGL/GLFW не нужны, атлас шрифта строится в памяти, кадры
// заканчиваются Render() без отрисовки.
//
// Зачем: панели, меню и строка состояния — самый большой пласт кода, который
// headless-тесты раньше не трогали (нужен ImGui). Здесь проверяются:
//   * баланс стека окон ImGui: у Begin() ОБЯЗАН быть End() даже когда Begin()
//     вернул false (окно свёрнуто или полностью отсечено). Нарушение в отладочной
//     сборке даёт IM_ASSERT, в релизной — разъехавшиеся панели и падение;
//   * работа оболочек при вырожденных размерах окна и скрытых панелях;
//   * команды исследования (построить сетку, решить, сходимость, экспорт) на
//     пустом проекте и на решённом — без падений и с честными сообщениями;
//   * мусор в состоянии просмотра (NaN в координатах пробы, ось среза вне
//     диапазона, перепутанные границы шкалы) не ломает отрисовку.
//
// Сборка: make ui   (см. aeros/tests/Makefile)
#include "imgui.h"
#include "imgui_internal.h"      // GImGui, ImGuiWindow::Collapsed — для проверки ветки Begin()==false
#include "cae_ui.h"

#include <cstdio>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <unistd.h>          // readlink
#include <sys/resource.h>    // setrlimit
#include <sys/wait.h>        // WIFEXITED / WIFSIGNALED

// -----------------------------------------------------
// Заглушки движка: тест не линкует OpenGL/GLFW, поэтому функции, которые
// работают с видеопамятью и системными диалогами, заменены здесь.
// -----------------------------------------------------
Language currentLanguage = Language::Russian;   // lang.h объявляет, lang.cpp не линкуем
bool caeColorOverride = false;                  // в движке определена в forces.cpp
void updateVertexColors() {}
void uploadVertexColorsToGPU() {}
// те же заглушки, что в lbm_physics_headless.cpp: GLFW/CUDA в тесте не нужны
extern "C" double glfwGetTime(void) { static double t = 0.0; return (t += 1.0 / 60.0); }
extern "C" void setVoxelData(const int*, const float*, int, int, int, float, float, float,
                             float, float, float) {}
extern "C" int cudaConsumeError(char* buf, int sz) { if (buf && sz > 0) buf[0] = 0; return 0; }
// загрузка STL живёт в model.cpp и требует OpenGL: в стенде модели нет
bool loadModel(const std::string& path) { (void)path; return false; }
// Диалоги файлов в тесте возвращают заданные пути, чтобы сохранение и загрузка
// исследования проверялись по-настоящему, а не «отменены пользователем».
static std::string g_openPath;
static std::string g_savePath;
std::string openFileDialog(void* owner) { (void)owner; return g_openPath; }
std::string openFileDialogNative() { return g_openPath; }
std::string saveFileDialogNative(const std::string& defaultName) {
    (void)defaultName;
    return g_savePath;
}

static int g_checks = 0;
static int g_failed = 0;
static void check(bool cond, const char* what) {
    ++g_checks;
    std::printf("  %s  %s\n", cond ? "ok:  " : "FAIL:", what);
    if (!cond) ++g_failed;
}
static void section(const char* name) { std::printf("\n--- %s ---\n", name); }

// Глубина стека окон сразу после NewFrame — эталон: к концу отрисовки оболочки
// стек обязан вернуться к этому значению.
static int g_stackBase = 1;
static int windowStackDepth() {
    ImGuiContext& g = *GImGui;
    return (int)g.CurrentWindowStack.Size;
}

// -----------------------------------------------------
// Один кадр оболочки. Порядок вызовов повторяет drawUI() в ui.cpp — иначе тест
// проверял бы не тот код, который работает в программе.
// Возвращает true, если стек окон сбалансирован.
// -----------------------------------------------------
static bool drawShellFrame(float W, float H, bool drawLegacyTreePlaceholder) {
    UiLayoutState& st = uiLayoutState();
    caeHandleShortcuts(st);
    const UiLayoutRects L = uiLayoutCompute(st, W, H);
    const float fs = st.fontScale < 0.7f ? 0.7f : (st.fontScale > 2.0f ? 2.0f : st.fontScale);
    caeSyncLegacyLayout(st);

    // ---------- 0) главное меню ----------
    if (L.menuVisible && L.menu.valid()) {
        if (uiDockBeginMenuBar(L.menu, fs)) {
            caeDrawMainMenu(st);
            uiDockEndMenuBar();
        }
    }

    // ---------- 1) дерево проекта ----------
    if (L.tree.valid()) {
        char title[192];
        std::snprintf(title, sizeof(title), "%s — %s",
                      _TR("Project", "Проект"), uiWorkspaceName(st.workspace));
        const bool treeOpen = uiDockBeginPanel(title, L.tree, fs);
        if (treeOpen) {
            uiDockWorkspaceTabs(st);
            ImGui::Separator();
            ImGui::BeginChild("##treeBody", ImVec2(-1, -1), false);
            if (st.workspace == 1)       caeDrawStudyTree();
            else if (st.workspace == 2)  caeDrawResultsTree();
            else if (drawLegacyTreePlaceholder) ImGui::Text("навигатор расчёта (в ui.cpp)");
            ImGui::EndChild();
        }
        uiDockEndPanel();
    }

    // ---------- 2) свойства выбранного объекта ----------
    if (L.props.valid()) {
        char title[192];
        std::snprintf(title, sizeof(title), "%s — %s",
                      _TR("Properties", "Свойства"),
                      (st.workspace == 1) ? caeStudySelName(caeView().studySel)
                      : (st.workspace == 2) ? caeResultSelName(caeView().resultSel)
                      : "аэродинамика");
        const bool propsOpen = uiDockBeginPanel(title, L.props, fs);
        if (propsOpen && st.workspace == 1)      caeDrawStudyProps();
        else if (propsOpen && st.workspace == 2) caeDrawResultsProps();
        uiDockEndPanel();
    }

    // ---------- 3) нижние вкладки ----------
    if (L.bottom.valid()) {
        const bool enabled[UI_BOTTOM_TAB_COUNT] = { true, true, true, true, true };
        int tab = st.bottomTab;
        const int active = uiDockBeginBottom(L.bottom, fs, &tab, enabled);
        if (active >= 0) {
            const ImVec2 avail = ImGui::GetContentRegionAvail();
            if (active == 0) {
                float consoleW = avail.x * 0.5f;
                if (consoleW < 200.0f) consoleW = avail.x;
                ImGui::BeginChild("##uiConsole", ImVec2(consoleW, avail.y), false);
                ImGui::TextDisabled("консоль движка");
                ImGui::EndChild();
                ImGui::SameLine();
                ImGui::BeginChild("##caeMessages", ImVec2(-1, avail.y), false);
                caeDrawBottomMessages();
                ImGui::EndChild();
            } else if (active == 1) {
                caeDrawBottomConvergence();
            } else if (active == 2) {
                caeDrawBottomPlots();
            } else if (active == 3) {
                caeDrawBottomResults();
            } else {
                ImGui::TextDisabled("журнал физики");
            }
            uiDockEndBottom();
        }
    }

    // ---------- 4) строка состояния ----------
    if (L.status.valid()) {
        if (uiDockBeginStatusBar(L.status, fs)) {
            caeDrawStatusBarCae(st.workspace);
            uiDockEndStatusBar();
        }
    }

    // ---------- 5) разделители панелей ----------
    uiDockDrawSplitters(st, L);

    return windowStackDepth() == g_stackBase;
}

// Прогнать кадр и записать результат.
static bool frameOk(float W, float H, bool legacyPlaceholder = true) {
    ImGui::GetIO().DisplaySize = ImVec2(W, H);
    ImGui::NewFrame();
    const bool balanced = drawShellFrame(W, H, legacyPlaceholder);
    if (balanced) {
        ImGui::Render();               // Render сам проверяет стек и падает на IM_ASSERT
    } else {
        // стек разъехался: Render() уронил бы процесс, поэтому кадр не завершаем,
        // а честно возвращаем false (контекст пересоздаётся следующим сценарием)
        ImGuiContext& g = *GImGui;
        while ((int)g.CurrentWindowStack.Size > g_stackBase) {
            ImGui::End();
            if (g.CurrentWindowStack.Size <= 1) break;
        }
    }
    return balanced;
}

// -----------------------------------------------------
// Постановка «растяжение стержня» — та же, что в fem_headless.cpp, чтобы панели
// рисовались по настоящим решённым данным.
// -----------------------------------------------------
static void makeSolvedTensionBar() {
    StudyProject& pr = studyProject();
    pr = StudyProject();
    pr.name = "Стержень (стенд оболочки)";
    pr.analysis = StudyAnalysis::LinearStatic;
    pr.units = StudyUnitSystem::SI;
    pr.geometryLoaded = true;
    pr.geomMinX = 0; pr.geomMinY = 0; pr.geomMinZ = 0;
    pr.geomMaxX = 1.0; pr.geomMaxY = 0.1; pr.geomMaxZ = 0.1;
    StudyMaterial st;
    st.name = "Сталь"; st.youngE = 2.1e11; st.poisson = 0.3; st.density = 7850.0;
    st.conductivity = 45.0; st.yieldStrength = 3.55e8;
    pr.materials.clear();
    pr.materials.push_back(st);
    pr.materialIndex = 0;
    pr.mesh.resolution = 12;

    StudyLoad fix;
    fix.name = "Защемление X-";
    fix.kind = StudyBCKind::Fixed;
    fix.region = studyFaceRegion(pr, 0, false, "грань X-");
    pr.loads.push_back(fix);

    StudyLoad press;
    press.name = "Давление на торец X+";
    press.kind = StudyBCKind::Pressure;
    press.value = 1.0e6;
    press.region = studyFaceRegion(pr, 0, true, "грань X+");
    pr.loads.push_back(press);

    // Геометрия — как в движке: воксельная occupancy в глобальных массивах,
    // которые читает caeBuildMesh(). Тогда стенд проверяет настоящие команды
    // оболочки, а не прямой вызов решателя.
    const int R = 16;
    g_voxNx = R; g_voxNy = R; g_voxNz = R;
    g_voxMinX = 0.0f; g_voxMinY = 0.0f; g_voxMinZ = 0.0f;
    g_voxMaxX = 1.0f; g_voxMaxY = 0.1f; g_voxMaxZ = 0.1f;
    g_voxelData.assign((size_t)R * R * R, 1);

    g_vertices.clear();
    g_normals.clear();
    g_vertexColors.clear();
    modelVertexCount = 24;
    for (int i = 0; i < modelVertexCount; ++i) {
        g_vertices.push_back((float)i * 0.04f);
        g_vertices.push_back(0.05f);
        g_vertices.push_back(0.05f);
        g_normals.push_back(1.0f); g_normals.push_back(0.0f); g_normals.push_back(0.0f);
    }
    pr.geometryLoaded = true;
    pr.triangles = (size_t)modelVertexCount / 3;
    pr.geomMinX = g_voxMinX; pr.geomMinY = g_voxMinY; pr.geomMinZ = g_voxMinZ;
    pr.geomMaxX = g_voxMaxX; pr.geomMaxY = g_voxMaxY; pr.geomMaxZ = g_voxMaxZ;
    // пресеты граней созданы ДО заполнения габарита — пересоздаём их по факту,
    // как это делает интерфейс при выборе грани
    pr.loads[0].region = studyFaceRegion(pr, 0, false, "грань X-");
    pr.loads[1].region = studyFaceRegion(pr, 0, true, "грань X+");
}

// Заголовок панели свойств — ровно такой, как его собирает drawUI() в ui.cpp.
static std::string propsTitle(int workspace, int sel) {
    char t[192];
    std::snprintf(t, sizeof(t), "%s — %s", _TR("Properties", "Свойства"),
                  (workspace == 1) ? caeStudySelName(sel) : caeResultSelName(sel));
    return std::string(t);
}

// Программно «нажать» кнопку: находим окно панели по заголовку и ставим активацию
// элемента в очередь (ImGui::ActivateItemByID — тот же механизм, которым работает
// клавиатурная навигация). Срабатывает на следующем кадре, когда элемент рисуется
// снова, то есть выполняется ТОТ ЖЕ код обработчика, что и по щелчку мыши.
static bool pressButton(const char* panelTitle, const char* label) {
    ImGuiContext& g = *GImGui;
    for (int i = 0; i < g.Windows.Size; ++i) {
        ImGuiWindow* w = g.Windows[i];
        if (!w || !w->Name) continue;
        if (std::strcmp(w->Name, panelTitle) != 0) continue;
        ImGui::ActivateItemByID(w->GetID(label));
        return true;
    }
    return false;
}

// Нажать кнопку и дать активации сработать.
// Важно: ImGui::ActivateItemByID кладёт запрос в g.NavNextActivateId, а его
// разбирает NavUpdate() в КОНЦЕ кадра; сам обработчик кнопки увидит активацию
// только на следующем кадре. Поэтому после активации рисуем ДВА кадра.
static bool clickCommand(int workspace, int sel, const char* label, float W = 1600.0f, float H = 900.0f) {
    uiLayoutState().workspace = workspace;
    if (workspace == 1) caeView().studySel = sel; else caeView().resultSel = sel;
    if (!frameOk(W, H)) return false;
    const std::string t = propsTitle(workspace, sel);
    if (!pressButton(t.c_str(), label)) return false;
    if (!frameOk(W, H)) return false;   // кадр, в конце которого NavUpdate ставит активацию
    return frameOk(W, H);               // кадр, в котором обработчик кнопки выполняется
}

// Собран ли этот тест с санитайзерами. Под ASan/TSan дочерний процесс с
// RLIMIT_AS = 64 МБ вообще не стартует: сам санитайзер резервирует теневую
// память на десятки терабайт адресного пространства. Поэтому проверку нехватки
// памяти под санитайзерами честно пропускаем, а не выдаём её провал за ошибку.
#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__)
static const bool g_underSanitizer = true;
#elif defined(__has_feature)
#if __has_feature(address_sanitizer) || __has_feature(thread_sanitizer)
static const bool g_underSanitizer = true;
#else
static const bool g_underSanitizer = false;
#endif
#else
static const bool g_underSanitizer = false;
#endif

static bool fileExistsNonEmpty(const char* path) {
    std::ifstream f(path);
    if (!f) return false;
    std::string line;
    while (std::getline(f, line)) if (!line.empty()) return true;
    return false;
}


int main(int argc, char** argv) {
    // ---- Режим дочернего процесса: проверка нехватки памяти ----
    // Оболочка обязана отказать честно, а не упасть: другие модули движка
    // (lbm.cpp, voxel_grid.cpp, particles.cpp) ловят std::bad_alloc, и CAE-путь
    // теперь делает то же. Проверяем это в отдельном процессе с урезанным
    // адресным пространством, чтобы не ронять основной тест.
    if (argc > 1 && std::strcmp(argv[1], "--oom-child") == 0) {
        struct rlimit rl;
        rl.rlim_cur = rl.rlim_max = (rlim_t)64 * 1024 * 1024;
        if (setrlimit(RLIMIT_AS, &rl) != 0) return 4;   // ограничить не удалось — проверка непоказательна
        const int R = 8;
        g_voxNx = R; g_voxNy = R; g_voxNz = R;
        g_voxMinX = 0.0f; g_voxMinY = 0.0f; g_voxMinZ = 0.0f;
        g_voxMaxX = 1.0f; g_voxMaxY = 1.0f; g_voxMaxZ = 1.0f;
        g_voxelData.assign((size_t)R * R * R, 1);
        StudyProject& pr = studyProject();
        pr.geometryLoaded = true;
        pr.mesh.resolution = 220;      // 10.6 млн элементов: под пределом, но памяти не хватит
        pr.mesh.resolutionCap = 512;
        if (caeBuildMesh()) return 3;  // память неожиданно нашлась — проверка непоказательна
        bool said = false;
        for (const auto& m : caeMessages())
            if (m.level >= 1 && (m.text.find("памят") != std::string::npos ||
                                 m.text.find("не хватило") != std::string::npos))
                said = true;
        return said ? 0 : 5;
    }

    std::printf("=== Aeros Engine: headless-проверка оболочки CAD/CAE (v1.31.0) ===\n");
    char b[384];

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.DisplaySize = ImVec2(1920.0f, 1080.0f);
    io.DeltaTime = 1.0f / 60.0f;
    io.Fonts->AddFontDefault();
    unsigned char* pixels = nullptr;
    int fw = 0, fh = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &fw, &fh);   // строит атлас: NewFrame требует собранных шрифтов
    check(pixels != nullptr && fw > 0 && fh > 0, "атлас шрифта построен (бэкенд не нужен)");

    // =========================================================
    section("1. Баланс стека окон ImGui: размеры окна и рабочие пространства");
    // =========================================================
    {
        const float sizes[][2] = {
            { 1920.0f, 1080.0f }, { 1600.0f, 900.0f }, { 1280.0f, 720.0f },
            { 1024.0f, 768.0f },  { 800.0f, 600.0f },  { 640.0f, 480.0f },
            { 480.0f, 360.0f },   { 360.0f, 280.0f },  { 321.0f, 241.0f }
        };
        bool allOk = true;
        int frames = 0;
        for (const auto& s : sizes) {
            for (int ws = 0; ws < UI_WORKSPACE_COUNT; ++ws) {
                for (int tab = 0; tab < UI_BOTTOM_TAB_COUNT; ++tab) {
                    uiLayoutState().workspace = ws;
                    uiLayoutState().bottomTab = tab;
                    // два кадра подряд: на первом ImGui только создаёт окна,
                    // на втором видны ошибки, накопленные предыдущим кадром
                    frameOk(s[0], s[1]);
                    if (!frameOk(s[0], s[1])) {
                        allOk = false;
                        std::snprintf(b, sizeof(b), "разъехался стек окон: %.0fx%.0f, пространство %d, вкладка %d",
                                      s[0], s[1], ws, tab);
                        check(false, b);
                    }
                    frames += 2;
                }
            }
        }
        std::snprintf(b, sizeof(b), "%d кадров оболочки: стек окон сбалансирован во всех", frames);
        check(allOk && frames > 100, b);
    }

    // =========================================================
    section("2. Скрытые панели, максимизация вида, узкое окно");
    // =========================================================
    {
        UiLayoutState& st = uiLayoutState();
        struct Case { const char* name; bool panels; bool tree; bool props; bool bottom; bool maximized; bool menu; float w, h; };
        const Case cases[] = {
            { "все панели",            true,  true,  true,  true,  false, true,  1920, 1080 },
            { "F12: панели скрыты",    false, true,  true,  true,  false, true,  1920, 1080 },
            { "вид на всё окно",       true,  true,  true,  true,  true,  true,  1920, 1080 },
            { "без дерева",            true,  false, true,  true,  false, true,  1280, 720  },
            { "без свойств",           true,  true,  false, true,  false, true,  1280, 720  },
            { "без нижних вкладок",    true,  true,  true,  false, false, true,  1280, 720  },
            { "только вид и статус",   true,  false, false, false, false, true,  1280, 720  },
            { "без меню",              true,  true,  true,  true,  false, false, 1280, 720  },
            { "узкое окно 360x280",    true,  true,  true,  true,  false, true,  360,  280  },
            { "широкое и низкое",      true,  true,  true,  true,  false, true,  2560, 300  },
        };
        for (const Case& c : cases) {
            st.panelsVisible = c.panels;
            st.treePanel = c.tree;
            st.propsPanel = c.props;
            st.bottomPanel = c.bottom;
            st.maximized = c.maximized;
            st.menuBar = c.menu;
            st.workspace = 1;
            st.bottomTab = 1;
            frameOk(c.w, c.h);
            std::snprintf(b, sizeof(b), "%s: стек окон сбалансирован", c.name);
            check(frameOk(c.w, c.h), b);
        }
        st.panelsVisible = true; st.treePanel = true; st.propsPanel = true;
        st.bottomPanel = true; st.maximized = false; st.menuBar = true;
    }

    // =========================================================
    section("3. Begin() вернул false: End() всё равно обязан быть вызван");
    // =========================================================
    {
        // ImGui::Begin() возвращает !window->SkipItems: false получается, когда окно
        // свёрнуто, неактивно или полностью скрыто. Контракт ImGui при этом неизменен —
        // End() вызывать ОБЯЗАТЕЛЬНО. Ветка «false без End» в отладочной сборке роняет
        // IM_ASSERT, а в релизной оставляет стек окон несбалансированным: следующие
        // панели рисуются не там, где должны.
        //
        // Как честно получить Begin()==false: у панелей стоит ImGuiWindowFlags_NoCollapse,
        // поэтому ImGui сам сбрасывает Collapsed=false и «свернуть окно» из теста нельзя.
        // Штатные рычаги — style.Alpha <= 0 (окно помечается скрытым, Begin() возвращает
        // false для всех окон кадра) и io.ConfigDebugBeginReturnValueLoop, который ImGui
        // предоставляет именно для проверки корректности пар Begin/End.
        UiLayoutState& st = uiLayoutState();
        st.workspace = 1; st.bottomTab = 0;
        ImGuiStyle& style = ImGui::GetStyle();
        const float alphaSaved = style.Alpha;

        // 1) style.Alpha = 0: Begin() вернёт false для каждого окна кадра
        style.Alpha = 0.0f;
        bool balanced = frameOk(1280.0f, 720.0f);
        check(balanced, "style.Alpha=0 (все Begin() вернули false): стек окон сбалансирован");
        balanced = frameOk(1920.0f, 1080.0f);
        check(balanced, "второй кадр с Alpha=0: стек окон сбалансирован");
        style.Alpha = alphaSaved;

        // 2) штатный режим ImGui: false возвращается циклически на разных глубинах окон
        io.ConfigDebugBeginReturnValueLoop = true;
        bool loopOk = true;
        for (int f = 0; f < 24 && loopOk; ++f) {
            st.workspace = f % UI_WORKSPACE_COUNT;
            st.bottomTab = f % UI_BOTTOM_TAB_COUNT;
            loopOk = frameOk(1440.0f, 810.0f);
        }
        std::snprintf(b, sizeof(b),
                      "ConfigDebugBeginReturnValueLoop: 24 кадра с периодическим false — стек сбалансирован (%d)",
                      (int)loopOk);
        check(loopOk, b);
        io.ConfigDebugBeginReturnValueLoop = false;

        // 3) то же самое на первых кадрах окон (окна ещё не созданы)
        io.ConfigDebugBeginReturnValueOnce = true;
        bool onceOk = frameOk(1280.0f, 720.0f);
        check(onceOk, "ConfigDebugBeginReturnValueOnce: стек окон сбалансирован");
        io.ConfigDebugBeginReturnValueOnce = false;

        // после всех рычагов оболочка обязана рисоваться как обычно
        check(frameOk(1280.0f, 720.0f), "после возврата Alpha=1 оболочка снова рисуется нормально");
    }

    // =========================================================
    section("4. Команды на пустом проекте: честный отказ без падения");
    // =========================================================
    {
        caeClearMessages();
        StudyProject& pr = studyProject();
        pr = StudyProject();
        caeMesh() = FemMesh();

        const bool synced = caeSyncGeometry();
        check(!synced, "синхронизация геометрии без модели возвращает отказ");
        const bool built = caeBuildMesh();
        check(!built, "построение сетки без геометрии возвращает отказ");
        const bool solved = caeSolve(false);
        check(!solved, "решение без сетки возвращает отказ");
        std::snprintf(b, sizeof(b), "статус после отказа: %s", studyStatusName(pr.results.status));
        check(pr.results.status == StudySolveStatus::Error || !pr.results.valid, b);

        const size_t nMsg = caeMessages().size();
        std::snprintf(b, sizeof(b), "в журнал оболочки записано сообщений: %zu", nMsg);
        check(nMsg >= 2, b);
        bool hasText = true;
        for (const auto& m : caeMessages()) if (m.text.empty()) hasText = false;
        check(hasText, "у каждого сообщения непустой текст");

        // отрисовка с ошибочным состоянием не должна падать
        uiLayoutState().workspace = 1;
        check(frameOk(1280.0f, 720.0f), "панели рисуются при ошибочном состоянии исследования");
        uiLayoutState().workspace = 2;
        check(frameOk(1280.0f, 720.0f), "панели результатов рисуются без решённых данных");
        uiLayoutState().workspace = 1;
    }

    // =========================================================
    section("5. Решённый проект: панели, экспорт, сходимость по сетке");
    // =========================================================
    {
        caeClearMessages();
        makeSolvedTensionBar();
        StudyProject& pr = studyProject();

        // команды оболочки: построение сетки и решение — те же, что жмёт пользователь
        check(caeSyncGeometry(), "синхронизация геометрии с воксельным представлением выполнена");
        check(caeBuildMesh(), "сетка построена командой оболочки");
        std::snprintf(b, sizeof(b), "сетка оболочки: %d x %d x %d = %d элементов, %d узлов",
                      caeMesh().ex, caeMesh().ey, caeMesh().ez, caeMesh().elemCount, caeMesh().nodeCount);
        check(caeMesh().built && caeMesh().elemCount > 0, b);
        check(caeSolve(false), "расчёт выполнен командой оболочки");
        std::snprintf(b, sizeof(b), "стенд: расчёт сошёлся — статус %s, итераций %d, невязка %.2e",
                      studyStatusName(pr.results.status), pr.results.iterations, pr.results.finalResidual);
        check(pr.results.status == StudySolveStatus::Converged, b);
        check(pr.status == StudySolveStatus::Converged, "статус исследования обновлён после решения");

        // все пространства и вкладки на решённых данных
        bool allOk = true;
        for (int ws = 0; ws < UI_WORKSPACE_COUNT; ++ws) {
            for (int tab = 0; tab < UI_BOTTOM_TAB_COUNT; ++tab) {
                uiLayoutState().workspace = ws;
                uiLayoutState().bottomTab = tab;
                if (!frameOk(1600.0f, 900.0f)) allOk = false;
            }
        }
        check(allOk, "решённый проект: стек окон сбалансирован во всех пространствах и вкладках");

        // перебор ВСЕХ узлов дерева свойств: каждая панель рисуется хотя бы раз.
        // Без этого большая часть cae_ui.h просто не выполнялась бы.
        {
            std::vector<int> studySels, resultSels;
            for (int i = CAE_STUDY_ANALYSIS; i <= CAE_STUDY_RESULTS; ++i) studySels.push_back(i);
            for (int i = 0; i < 3; ++i) studySels.push_back(CAE_STUDY_MAT_BASE + i);
            for (int i = 0; i < 4; ++i) studySels.push_back(CAE_STUDY_LOAD_BASE + i);
            for (int i = CAE_RES_LEGEND; i <= CAE_RES_EXPORT; ++i) resultSels.push_back(i);
            for (int i = 0; i < 12; ++i) resultSels.push_back(CAE_RES_FIELD_BASE + i);
            bool selOk = true;
            CaeViewState& vs = caeView();
            uiLayoutState().bottomTab = 0;
            for (int sel : studySels) {
                vs.studySel = sel;
                uiLayoutState().workspace = 1;
                if (!frameOk(1600.0f, 900.0f)) selOk = false;
            }
            for (int sel : resultSels) {
                vs.resultSel = sel;
                uiLayoutState().workspace = 2;
                if (!frameOk(1600.0f, 900.0f)) selOk = false;
            }
            std::snprintf(b, sizeof(b),
                          "перебор узлов свойств: %zu узлов исследования и %zu узлов результатов нарисованы",
                          studySels.size(), resultSels.size());
            check(selOk, b);
            vs.studySel = CAE_STUDY_ANALYSIS;
            vs.resultSel = CAE_RES_LEGEND;
            uiLayoutState().workspace = 2;
        }

        // состояние просмотра: поле, проба, срез, график, шкала
        CaeViewState& v = caeView();
        v.field = ResultFieldId::VonMises;
        v.colorGeometry = true;
        v.probeEnabled = true;
        v.probeX = 0.5; v.probeY = 0.05; v.probeZ = 0.05;
        v.sliceEnabled = true;
        v.sliceAxis = 1; v.sliceCoord = 0.05; v.sliceSamples = 32;
        v.scale.autoRange = false; v.scale.manualLo = 0.0; v.scale.manualHi = 2.0e6;
        // Срез строим напрямую, а не кнопкой (проверка кнопки — в разделе 6):
        // здесь важно, что панель рисует тепловую карту среза без разбаланса стека.
        {
            const std::vector<double>* data = resultFieldData(pr.results, v.field);
            if (data)
                resultBuildSlice(v.slice, caeMesh(), *data, v.sliceAxis, v.sliceCoord,
                                 v.sliceSamples);
        }
        // проба и срез считаются в своих узлах дерева результатов
        v.resultSel = CAE_RES_PROBE;
        bool okFrames = frameOk(1600.0f, 900.0f);
        v.resultSel = CAE_RES_SLICE;
        for (int i = 0; i < 2 && okFrames; ++i) okFrames = frameOk(1600.0f, 900.0f);
        check(okFrames, "панели результатов с пробой, срезом и ручной шкалой рисуются");
        check(v.probeValid, "проба в центре стержня дала значение");
        std::snprintf(b, sizeof(b), "проба: значение %.6e при максимуме %.6e", v.probeValue,
                      pr.results.maxVonMises);
        check(v.probeValue > 0.0 && v.probeValue <= pr.results.maxVonMises * 1.001, b);
        std::snprintf(b, sizeof(b), "срез построен напрямую: %d x %d, значения %.4g .. %.4g",
                      v.slice.nu, v.slice.nv, v.slice.vmin, v.slice.vmax);
        check(v.slice.ok && v.slice.nu > 0 && v.slice.nv > 0, b);

        // раскраска геометрии по полю
        const bool colored = caeApplyFieldToGeometry();
        check(colored, "поле перенесено на геометрию (цвета вершин)");
        std::snprintf(b, sizeof(b), "цветов вершин %zu при %d вершинах модели",
                      g_vertexColors.size() / 3, modelVertexCount);
        check(g_vertexColors.size() == (size_t)modelVertexCount * 3, b);
        bool finiteColors = true;
        for (float c : g_vertexColors) if (!std::isfinite(c)) finiteColors = false;
        check(finiteColors, "в цветах вершин нет NaN/Inf");

        // сходимость по сетке
        caeRunMeshConvergence();
        std::snprintf(b, sizeof(b), "сходимость по сетке: точек %zu", v.convergence.size());
        check(!v.convergence.empty(), b);
        {
            bool ordered = true, finite = true, filled = true;
            int prevRes = 0;
            for (const auto& mp : v.convergence) {
                if (mp.resolution <= prevRes) ordered = false;
                prevRes = mp.resolution;
                if (!std::isfinite(mp.maxValue) || !std::isfinite(mp.residual)) finite = false;
                if (mp.elements <= 0 || mp.nodes <= 0) filled = false;
            }
            check(filled, "в точках сходимости записаны размеры сетки (элементы и узлы)");
            check(ordered, "точки сходимости идут по возрастанию разрешения сетки");
            check(finite, "значения и отклонения в точках сходимости конечны");
        }
        check(frameOk(1600.0f, 900.0f), "вкладка «Сходимость» с данными рисуется");

        // экспорт результатов и отчёта
        g_savePath = "/tmp/aeros-ui-test-results.csv";
        std::remove(g_savePath.c_str());
        const bool csv = caeExportCSV();
        check(csv, "экспорт CSV из оболочки выполнен");
        {
            std::ifstream f(g_savePath);
            std::string line;
            bool header = false;
            int rows = 0;
            while (std::getline(f, line)) {
                if (line.compare(0, 1, "#") == 0) continue;
                if (!header) { header = (line.find("node,") == 0); continue; }
                rows++;
            }
            std::snprintf(b, sizeof(b), "CSV оболочки: заголовок есть, строк данных %d при %d узлах",
                          rows, caeMesh().nodeCount);
            check(header && rows == caeMesh().nodeCount, b);
        }
        std::remove(g_savePath.c_str());

        g_savePath = "/tmp/aeros-ui-test-report.txt";
        std::remove(g_savePath.c_str());
        const bool rep = caeExportReport();
        check(rep, "отчёт по исследованию из оболочки выполнен");
        {
            std::ifstream f(g_savePath);
            std::stringstream ss; ss << f.rdbuf();
            const std::string txt = ss.str();
            std::snprintf(b, sizeof(b), "отчёт оболочки: %zu байт", txt.size());
            check(txt.size() > 500, b);
            check(txt.find("Ограничения методики") != std::string::npos,
                  "отчёт оболочки содержит раздел об ограничениях методики");
        }
        std::remove(g_savePath.c_str());

        // сохранение и загрузка исследования
        g_savePath = "/tmp/aeros-ui-test-study.aerosproj";
        std::remove(g_savePath.c_str());
        check(caeSaveStudy(), "исследование сохранено из оболочки");
        g_openPath = g_savePath;
        const double savedE = pr.materials.empty() ? 0.0 : pr.materials[0].youngE;
        pr.materials.clear();
        pr.loads.clear();
        check(caeLoadStudy(), "исследование загружено из оболочки");
        std::snprintf(b, sizeof(b), "после загрузки: материалов %zu, условий %zu, E = %.4g",
                      pr.materials.size(), pr.loads.size(),
                      pr.materials.empty() ? 0.0 : pr.materials[0].youngE);
        check(!pr.materials.empty() && pr.materials[0].youngE == savedE && !pr.loads.empty(), b);
        std::remove(g_savePath.c_str());
        g_savePath.clear();
        g_openPath.clear();

        // мусор в состоянии просмотра не должен ломать отрисовку
        v.probeX = NAN; v.probeY = 1e30; v.probeZ = -1e30;
        v.sliceAxis = 99; v.sliceCoord = NAN; v.sliceSamples = -5;
        v.scale.manualLo = 1e30; v.scale.manualHi = -1e30;
        v.field = (ResultFieldId)999;
        bool survived = true;
        for (int i = 0; i < 2 && survived; ++i) survived = frameOk(1280.0f, 720.0f);
        check(survived, "мусор в состоянии просмотра (NaN, ось 99, поле 999) не ломает оболочку");
        v.field = ResultFieldId::VonMises;
        v.probeX = 0.5; v.probeY = 0.05; v.probeZ = 0.05;
        v.sliceAxis = 1; v.sliceCoord = 0.05; v.sliceSamples = 32;
        v.scale.autoRange = true;
        check(frameOk(1280.0f, 720.0f), "оболочка восстанавливается после мусора в состоянии просмотра");
    }

    // =========================================================
    section("6. Команды оболочки: кнопки выполняют обработчики (ActivateItemByID)");
    // =========================================================
    {
        CaeViewState& v = caeView();
        StudyProject& pr = studyProject();

        // Раздел 5 закончился загрузкой исследования из файла, а caeLoadStudy()
        // честно сбрасывает результаты, статус и сетку (их надо пересчитать).
        // Поэтому снова строим сетку и решаем — теми же командами оболочки.
        check(caeBuildMesh(), "перед проверкой кнопок сетка восстановлена командой оболочки");
        check(caeSolve(false), "перед проверкой кнопок расчёт выполнен командой оболочки");
        std::snprintf(b, sizeof(b), "данные для кнопок готовы: максимум σ = %.6e Па",
                      pr.results.maxVonMises);
        check(pr.results.maxVonMises > 0.0, b);

        // --- проба в точке ---
        v.probeEnabled = true; v.probeValid = false; v.probeValue = 0.0;
        v.probeX = 0.5; v.probeY = 0.05; v.probeZ = 0.05;
        check(clickCommand(2, CAE_RES_PROBE, _TR("Sample", "Измерить")),
              "кнопка «Измерить» найдена в панели пробы и обработана");
        std::snprintf(b, sizeof(b), "проба по кнопке: значение %.6e (максимум поля %.6e)",
                      v.probeValue, pr.results.maxVonMises);
        check(v.probeValid && v.probeValue > 0.0, b);

        // --- срез ---
        v.sliceAxis = 1; v.sliceCoord = 0.05; v.sliceSamples = 32;
        v.slice = ResultSlice();
        check(clickCommand(2, CAE_RES_SLICE, _TR("Build slice", "Построить срез")),
              "кнопка «Построить срез» найдена и обработана");
        std::snprintf(b, sizeof(b), "срез по кнопке: %d x %d, значения %.4g .. %.4g",
                      v.slice.nu, v.slice.nv, v.slice.vmin, v.slice.vmax);
        check(v.slice.ok && v.slice.nu > 0 && v.slice.nv > 0, b);
        check(v.sliceEnabled, "после построения срез включён для показа");

        // --- график вдоль линии ---
        v.lineAx = 0.0; v.lineAy = 0.05; v.lineAz = 0.05;
        v.lineBx = 1.0; v.lineBy = 0.05; v.lineBz = 0.05;
        v.line = ResultLinePlot();
        check(clickCommand(2, CAE_RES_PLOT, _TR("Build plot", "Построить график")),
              "кнопка «Построить график» найдена и обработана");
        std::snprintf(b, sizeof(b), "график по кнопке: %zu точек, s от %.4g до %.4g м",
                      v.line.v.size(), v.line.s.empty() ? 0.0 : v.line.s.front(),
                      v.line.s.empty() ? 0.0 : v.line.s.back());
        check(v.line.ok && v.line.v.size() > 2, b);

        // --- шкала: ручной диапазон и возврат к данным ---
        // Кнопка «Применить ручной диапазон» рисуется только при выключенном автомате —
        // так и задумано в панели, поэтому сначала переключаем режим.
        v.scale.autoRange = false;
        v.scale.manualLo = 0.0; v.scale.manualHi = 1.0e6;
        check(clickCommand(2, CAE_RES_LEGEND, _TR("Apply manual range", "Применить ручной диапазон")),
              "панель легенды нарисована, команда «Применить ручной диапазон» отправлена");
        std::snprintf(b, sizeof(b), "шкала после кнопки: %.4g .. %.4g (авто=%d)",
                      v.scale.lo, v.scale.hi, (int)v.scale.autoRange);
        check(!v.scale.autoRange && v.scale.lo == 0.0 && v.scale.hi == 1.0e6, b);

        check(clickCommand(2, CAE_RES_LEGEND, _TR("Take from data", "Взять из данных")),
              "панель легенды нарисована, команда «Взять из данных» отправлена");
        std::snprintf(b, sizeof(b), "после «Взять из данных»: %.6g .. %.6g при данных %.6g .. %.6g",
                      v.scale.lo, v.scale.hi, v.scale.dataMin, v.scale.dataMax);
        check(std::fabs(v.scale.manualLo - v.scale.dataMin) < 1e-9 * (std::fabs(v.scale.dataMax) + 1.0) &&
              std::fabs(v.scale.manualHi - v.scale.dataMax) < 1e-9 * (std::fabs(v.scale.dataMax) + 1.0), b);
        v.scale.autoRange = true;

        // --- сходимость по сетке ---
        v.convergence.clear();
        check(clickCommand(2, CAE_RES_CONV,
                           _TR("Run at 3 resolutions (0.5x, 1x, 1.5x)",
                               "Прогнать на 3 разрешениях (0.5x, 1x, 1.5x)")),
              "кнопка прогона сходимости найдена и обработана");
        std::snprintf(b, sizeof(b), "сходимость по кнопке: точек %zu", v.convergence.size());
        check(v.convergence.size() >= 3, b);

        // --- экспорт результатов и отчёта ---
        g_savePath = "/tmp/aeros-ui-cmd-results.csv";
        std::remove(g_savePath.c_str());
        check(clickCommand(2, CAE_RES_EXPORT,
                           _TR("Export results to CSV…", "Экспорт результатов в CSV…")),
              "кнопка экспорта CSV найдена и обработана");
        check(fileExistsNonEmpty(g_savePath.c_str()), "по кнопке оболочки записан непустой CSV");
        std::remove(g_savePath.c_str());

        g_savePath = "/tmp/aeros-ui-cmd-report.txt";
        std::remove(g_savePath.c_str());
        check(clickCommand(2, CAE_RES_EXPORT,
                           _TR("Export report (TXT)…", "Экспорт отчёта (TXT)…")),
              "кнопка экспорта отчёта найдена и обработана");
        check(fileExistsNonEmpty(g_savePath.c_str()), "по кнопке оболочки записан непустой отчёт");
        std::remove(g_savePath.c_str());
        g_savePath.clear();

        // --- раскраска геометрии по полю и возврат к Cp ---
        g_vertexColors.clear();
        caeColorOverride = false;
        check(clickCommand(2, CAE_RES_LEGEND,
                           _TR("Apply field to geometry", "Применить поле к геометрии")),
              "кнопка «Применить поле к геометрии» найдена и обработана");
        std::snprintf(b, sizeof(b), "после кнопки: caeColorOverride=%d, цветов %zu на %d вершин",
                      (int)caeColorOverride, g_vertexColors.size() / 3, modelVertexCount);
        check(caeColorOverride && g_vertexColors.size() == (size_t)modelVertexCount * 3, b);
        check(clickCommand(2, CAE_RES_LEGEND,
                           _TR("Restore Cp coloring", "Вернуть раскраску по Cp")),
              "кнопка «Вернуть раскраску по Cp» найдена и обработана");
        check(!caeColorOverride && !v.colorGeometry, "раскраска по полю выключена, вернулась карта Cp");

        // --- недоступное поле: честный отказ, а не серая модель ---
        {
            caeClearMessages();
            const ResultFieldId keep = v.field;
            v.field = ResultFieldId::Temperature;   // в статическом расчёте температуры нет
            v.colorGeometry = true;
            g_vertexColors.assign((size_t)modelVertexCount * 3, 0.25f);
            caeColorOverride = false;
            frameOk(1600.0f, 900.0f);
            const bool applied = caeApplyFieldToGeometry();
            check(!applied, "раскраска по недоступному полю не выполнена (а не имитирована)");
            check(!caeColorOverride, "переопределение цвета не включено для недоступного поля");
            bool grayed = true;
            for (float c : g_vertexColors) if (std::fabs(c - 0.62f) < 1e-6f || std::fabs(c - 0.66f) < 1e-6f) grayed = false;
            check(grayed, "цвета вершин не перезаписаны цветом «вне тела»");
            bool explained = false;
            for (const auto& m : caeMessages())
                if (m.level >= 1 && m.text.find("не считается") != std::string::npos) explained = true;
            check(explained, "в журнале названа причина: поле в этих результатах не считается");
            v.field = keep;
            frameOk(1600.0f, 900.0f);
        }

        // --- команды исследования: построить сетку и рассчитать ---
        caeMesh() = FemMesh();
        pr.results.clear();
        check(clickCommand(1, CAE_STUDY_MESH, _TR("Build mesh", "Построить сетку")),
              "кнопка «Построить сетку» найдена и обработана");
        std::snprintf(b, sizeof(b), "сетка по кнопке: элементов %d, узлов %d",
                      caeMesh().elemCount, caeMesh().nodeCount);
        check(caeMesh().built && caeMesh().elemCount > 0, b);
        check(clickCommand(1, CAE_STUDY_MESH, _TR("Solve", "Рассчитать")),
              "кнопка «Рассчитать» найдена и обработана");
        std::snprintf(b, sizeof(b), "расчёт по кнопке: статус %s, итераций %d",
                      studyStatusName(pr.results.status), pr.results.iterations);
        check(pr.results.status == StudySolveStatus::Converged, b);
        check(clickCommand(1, CAE_STUDY_GEOMETRY, _TR("Sync geometry", "Синхронизировать геометрию")),
              "кнопка «Синхронизировать геометрию» найдена и обработана");

        // --- отказ нереализованного анализа не должен оставлять статус «решено» ---
        // Раньше ветка отказа не трогала результаты, и pr.status оставался от
        // предыдущего успешного расчёта: интерфейс показывал «решено» и старые
        // числа при фактически невыполненном расчёте.
        {
            const StudyAnalysis keep = pr.analysis;
            check(pr.results.status == StudySolveStatus::Converged,
                  "перед проверкой отказа проект действительно решён");
            pr.analysis = StudyAnalysis::AeroCFD;   // в CAE-оболочке не реализован
            caeClearMessages();
            const bool ran = caeSolve(false);
            std::snprintf(b, sizeof(b), "нереализованный анализ: отказ=%d, статус %s, results.valid=%d",
                          (int)!ran, studyStatusName(pr.status), (int)pr.results.valid);
            check(!ran, b);
            check(pr.status == StudySolveStatus::Error,
                  "после отказа статус исследования — ошибка, а не «решено» от прошлого расчёта");
            check(!pr.results.valid, "старые результаты не выдаются за текущие");
            bool said = false;
            for (const auto& m : caeMessages())
                if (m.level >= 1 && m.text.find("не реализован") != std::string::npos) said = true;
            check(said, "в журнале названа причина отказа");
            check(frameOk(1440.0f, 810.0f), "оболочка рисуется после отказа анализа");

            pr.analysis = keep;
            check(caeSolve(false), "после возврата анализа расчёт снова выполняется");
            check(pr.results.status == StudySolveStatus::Converged,
                  "статус снова «сошёлся» — отказ не оставил следов");
        }
    }

    // =========================================================
    section("7. Те же команды на пустом проекте: честный отказ, без падения");
    // =========================================================
    {
        CaeViewState& v = caeView();
        StudyProject& pr = studyProject();
        pr = StudyProject();
        caeMesh() = FemMesh();
        // очищаем и состояние движка: иначе «синхронизировать геометрию» честно
        // возьмёт данные из воксельных массивов, оставшихся от прошлого раздела
        g_voxelData.clear();
        g_voxNx = g_voxNy = g_voxNz = 0;
        g_vertices.clear();
        g_normals.clear();
        g_vertexColors.clear();
        modelVertexCount = 0;
        pr.geometryLoaded = false;
        v.slice = ResultSlice();
        v.line = ResultLinePlot();
        v.probeValid = false;
        v.convergence.clear();
        caeClearMessages();

        check(clickCommand(2, CAE_RES_SLICE, _TR("Build slice", "Построить срез")),
              "кнопка среза на пустом проекте обработана без падения");
        check(!v.slice.ok, "срез без данных не построен (а не выдуман)");
        check(clickCommand(2, CAE_RES_PLOT, _TR("Build plot", "Построить график")),
              "кнопка графика на пустом проекте обработана без падения");
        check(!v.line.ok, "график без данных не построен");
        check(clickCommand(2, CAE_RES_PROBE, _TR("Sample", "Измерить")),
              "кнопка пробы на пустом проекте обработана без падения");
        check(!v.probeValid, "проба без данных не дала значения");
        check(clickCommand(2, CAE_RES_EXPORT,
                           _TR("Export results to CSV…", "Экспорт результатов в CSV…")),
              "кнопка экспорта на пустом проекте обработана без падения");
        check(clickCommand(1, CAE_STUDY_MESH, _TR("Build mesh", "Построить сетку")),
              "кнопка построения сетки без геометрии обработана без падения");
        check(!caeMesh().built, "сетка без геометрии не построена");
        check(clickCommand(1, CAE_STUDY_MESH, _TR("Solve", "Рассчитать")),
              "кнопка расчёта без сетки обработана без падения");
        check(pr.results.status == StudySolveStatus::Error || !pr.results.valid,
              "расчёт без сетки дал статус ошибки, а не «решено»");

        bool honest = false;
        for (const auto& m : caeMessages())
            if (m.level >= 1 && !m.text.empty()) honest = true;
        check(honest, "в журнал оболочки записано понятное сообщение об отказе");
        check(frameOk(1280.0f, 720.0f), "оболочка рисуется после серии отказов");
    }

    // =========================================================
    section("8. Мусор в компоновке: оболочка не падает");
    // =========================================================
    {
        UiLayoutState& st = uiLayoutState();
        st.treeW = -500.0f; st.propsW = 1e9f; st.bottomH = -20.0f;
        st.fontScale = 1e6f; st.menuH = -5.0f; st.statusH = 1e6f;
        st.workspace = -7; st.bottomTab = 99;
        st.clampToScreen(1280.0f, 720.0f);
        std::snprintf(b, sizeof(b),
                      "после clampToScreen: дерево %.0f, свойства %.0f, низ %.0f, шрифт %.2f",
                      st.treeW, st.propsW, st.bottomH, st.fontScale);
        check(st.treeW >= UiLayoutState::minTreeW() && st.propsW >= UiLayoutState::minPropsW() &&
              st.bottomH >= UiLayoutState::minBottomH() && st.fontScale <= 2.0f, b);
        check(st.workspace == 0 && st.bottomTab == 0, "недопустимые индексы сброшены в допустимые");
        bool ok = true;
        for (int i = 0; i < 2 && ok; ++i) ok = frameOk(1280.0f, 720.0f);
        check(ok, "оболочка рисуется после мусора в размерах панелей");

        st.fontScale = 0.0f;
        ok = frameOk(1280.0f, 720.0f);
        check(ok, "нулевой масштаб шрифта не ломает отрисовку");
        st.fontScale = 1.0f;

        // экстремальные размеры окна
        ok = frameOk(321.0f, 241.0f);
        check(ok, "минимально возможное окно (321x241) рисуется");
        ok = frameOk(7680.0f, 4320.0f);
        check(ok, "окно 8K рисуется");
    }

    // =========================================================
    section("9. Состояние оболочки согласовано с настройками");
    // =========================================================
    {
        UiLayoutState& st = uiLayoutState();
        st.workspace = 2; st.bottomTab = 3; st.treeW = 300.0f;
        st.propsW = 420.0f; st.bottomH = 220.0f;
        caeSyncLegacyLayout(st);
        check(uiTreeW == 300.0f && uiPropsW == 420.0f && uiBottomH == 220.0f,
              "прежние глобальные размеры панелей — зеркала состояния компоновки");
        check(uiDocked == st.panelsVisible, "прежний флаг uiDocked отражает видимость панелей");
        st.treeW = 999.0f;
        caeSyncLegacyLayout(st);
        check(uiTreeW == 999.0f, "зеркала обновляются каждый кадр (источник истины — uiLayoutState)");
        st.treeW = 264.0f;

        // имена выбора в деревьях не пустые и не выходят за границы списков
        bool namesOk = true;
        for (int i = -3; i < 40; ++i) {
            const char* n1 = caeStudySelName(i);
            const char* n2 = caeResultSelName(i);
            if (!n1 || !n1[0] || !n2 || !n2[0]) namesOk = false;
        }
        check(namesOk, "имена узлов выбора заданы и для недопустимых индексов (защита от выхода за границы)");
    }

    // =========================================================
    section("10. Горячие клавиши и мастер-переключатель панелей");
    // =========================================================
    {
        UiLayoutState& st = uiLayoutState();
        // рабочее состояние: геометрия в вокселах, сетка и решённый проект
        makeSolvedTensionBar();
        check(caeBuildMesh(), "перед проверкой клавиш сетка построена");
        check(caeSolve(false), "перед проверкой клавиш расчёт выполнен");

        // --- Ctrl+1/2/3: переключение рабочих пространств ---
        const ImGuiKey keys[3] = { ImGuiKey_1, ImGuiKey_2, ImGuiKey_3 };
        bool wsOk = true;
        for (int k = 0; k < 3; ++k) {
            io.AddKeyEvent(ImGuiMod_Ctrl, true);   // GLFW-бэкенд передаёт модификатор именно так
            io.AddKeyEvent(keys[k], true);
            if (!frameOk(1600.0f, 900.0f)) wsOk = false;
            io.AddKeyEvent(keys[k], false);
            io.AddKeyEvent(ImGuiMod_Ctrl, false);   // GLFW-бэкенд передаёт модификатор именно так
            frameOk(1600.0f, 900.0f);
            std::snprintf(b, sizeof(b), "Ctrl+%d: пространство %d (ожидалось %d)", k + 1,
                          st.workspace, k);
            check(st.workspace == k, b);
        }
        check(wsOk, "кадры с переключением пространств сбалансированы");

        // без Ctrl те же клавиши не должны ничего переключать
        st.workspace = 1;
        io.AddKeyEvent(ImGuiKey_3, true);
        frameOk(1600.0f, 900.0f);
        io.AddKeyEvent(ImGuiKey_3, false);
        frameOk(1600.0f, 900.0f);
        check(st.workspace == 1, "клавиша 3 без Ctrl пространство не переключает");

        // --- Ctrl+M: вид на всё окно ---
        st.maximized = false;
        io.AddKeyEvent(ImGuiMod_Ctrl, true);   // GLFW-бэкенд передаёт модификатор именно так
        io.AddKeyEvent(ImGuiKey_M, true);
        frameOk(1600.0f, 900.0f);
        io.AddKeyEvent(ImGuiKey_M, false);
        io.AddKeyEvent(ImGuiMod_Ctrl, false);   // GLFW-бэкенд передаёт модификатор именно так
        frameOk(1600.0f, 900.0f);
        check(st.maximized, "Ctrl+M включает вид на всё окно");
        {
            UiLayoutRects L = uiLayoutCompute(st, 1600.0f, 900.0f);
            std::snprintf(b, sizeof(b), "в максимизации: дерево %d, свойства %d, низ %d, вид %.0fx%.0f",
                          (int)L.tree.valid(), (int)L.props.valid(), (int)L.bottom.valid(),
                          L.viewport.w, L.viewport.h);
            check(!L.tree.valid() && !L.props.valid() && !L.bottom.valid(), b);
            check(L.viewport.valid() && L.viewport.w > 1500.0f, "3D-вид занимает всё окно");
        }

        // --- мастер-переключатель панелей в максимизации: выбор пользователя не откатывается ---
        st.panelsVisible = false;
        frameOk(1600.0f, 900.0f);
        check(!st.panelsVisible,
              "максимизация не отменяет «скрыть все панели» (прежде нажатие откатывалось)");
        st.maximized = false;
        frameOk(1600.0f, 900.0f);
        {
            UiLayoutRects L = uiLayoutCompute(st, 1600.0f, 900.0f);
            check(!st.panelsVisible && !L.tree.valid() && !L.props.valid() && !L.bottom.valid(),
                  "после выхода из максимизации панели остаются скрытыми, как просил пользователь");
        }
        st.panelsVisible = true;
        frameOk(1600.0f, 900.0f);
        {
            UiLayoutRects L = uiLayoutCompute(st, 1600.0f, 900.0f);
            check(L.tree.valid() && L.props.valid() && L.bottom.valid(),
                  "панели возвращаются мастер-переключателем");
        }

        // --- Ctrl+Enter: расчёт с перестроением сетки ---
        StudyProject& pr = studyProject();
        const int iterBefore = pr.results.iterations;
        caeMesh() = FemMesh();
        pr.results.clear();
        io.AddKeyEvent(ImGuiMod_Ctrl, true);   // GLFW-бэкенд передаёт модификатор именно так
        io.AddKeyEvent(ImGuiKey_Enter, true);
        frameOk(1600.0f, 900.0f);
        io.AddKeyEvent(ImGuiKey_Enter, false);
        io.AddKeyEvent(ImGuiMod_Ctrl, false);   // GLFW-бэкенд передаёт модификатор именно так
        frameOk(1600.0f, 900.0f);
        std::snprintf(b, sizeof(b), "Ctrl+Enter: статус %s, итераций %d (до очистки %d), сетка %d",
                      studyStatusName(pr.results.status), pr.results.iterations, iterBefore,
                      (int)caeMesh().built);
        check(pr.results.status == StudySolveStatus::Converged && caeMesh().built, b);
        check(pr.results.iterations > 0, "расчёт по Ctrl+Enter действительно выполнен");
    }

    // =========================================================
    section("11. Нехватка памяти: честный отказ вместо падения процесса");
    // =========================================================
    {
        char self[4096];
        self[0] = 0;
        const ssize_t n = readlink("/proc/self/exe", self, sizeof(self) - 1);
        if (n <= 0) {
            check(false, "не удалось определить путь к тесту для проверки нехватки памяти");
        } else {
            self[n] = 0;
            const std::string cmd = std::string("\"") + self + "\" --oom-child >/dev/null 2>&1";
            const int rc = std::system(cmd.c_str());
            const bool exited = (rc != -1) && WIFEXITED(rc);
            const bool signaled = (rc != -1) && WIFSIGNALED(rc);
            const int code = exited ? WEXITSTATUS(rc) : -1;
            std::snprintf(b, sizeof(b),
                          "дочерний процесс с адресным пространством 64 МБ: завершился штатно=%d, "
                          "убит сигналом=%d, код=%d",
                          (int)exited, (int)signaled, code);
            check(!signaled, b);
            if (g_underSanitizer) {
                std::snprintf(b, sizeof(b),
                              "сборка с санитайзерами: дочерний процесс завершился с кодом %d — "
                              "проверка нехватки памяти непоказательна (ASan нужен больший лимит адресов)",
                              code);
                check(true, b);
            } else {
                // 0 — нехватка памяти обработана и объяснена; 3 — памяти хватило (непоказательно);
                // 4 — не удалось включить ограничение; 5 — отказ произошёл, но причина не названа
                std::snprintf(b, sizeof(b), "результат проверки нехватки памяти: код %d (0 — отказ объяснён, "
                                            "3 — памяти хватило, 5 — причина не названа)", code);
                check(code == 0 || code == 3, b);
                if (code == 0)
                    check(true, "при нехватке памяти оболочка сообщила причину и не упала");
            }
        }
    }

    ImGui::DestroyContext();

    std::printf("\n=== оболочка CAD/CAE: проверок %d, провалов %d — %s ===\n",
                g_checks, g_failed, g_failed == 0 ? "ВСЁ В ПОРЯДКЕ" : "ЕСТЬ ПРОВАЛЫ");
    return g_failed == 0 ? 0 : 1;
}
