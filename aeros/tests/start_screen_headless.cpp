// Headless-проверки главного меню (стартового экрана) и списка недавних моделей — v1.34.0.
//
// Что проверяется:
//  1) список недавних моделей: отсутствующий файл = пустой список, дубликаты не
//     копятся, число записей ограничено, управляющие символы и сверхдлинные
//     строки отбрасываются (иначе «недавний путь» ломал бы раскладку меню);
//  2) демо-тела: геометрия замкнутая, нормали единичные, габариты совпадают с
//     заданными полуосями (крыло действительно тонкое по Y, дирижабль вытянут
//     по потоку X) — иначе демо-режим показывал бы не то тело, что обещает;
//  3) действия экрана: отмена диалога НЕ закрывает экран и показывает причину,
//     успешная загрузка закрывает, «Выход» выставляет флаг выхода, «Настройки»
//     переводят дерево в нужный узел;
//  4) отрисовка: стек окон ImGui сбалансирован при пустом и заполненном списке,
//     при вырожденном размере окна и при открытом/закрытом экране.
//
// Сборка: make startup
#include "imgui.h"
#include "imgui_internal.h"

#include "cae_ui.h"
#include "start_screen.h"
#include "recent_files.h"
#include "drop_files.h"
#include "demo_geometry.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <unistd.h>

// ---- заглушки движка (как в cae_ui_headless.cpp) ----
Language currentLanguage = Language::Russian;
bool caeColorOverride = false;
void updateVertexColors() {}
void uploadVertexColorsToGPU() {}
extern "C" double glfwGetTime(void) { static double t = 0.0; return (t += 1.0 / 60.0); }
extern "C" void setVoxelData(const int*, const float*, int, int, int, float, float, float,
                             float, float, float) {}
extern "C" int cudaConsumeError(char* buf, int sz) { if (buf && sz > 0) buf[0] = 0; return 0; }

// Управляемые из теста загрузчики.
static bool g_loadModelOk = false;
static std::string g_lastLoaded;
static bool g_loadDemoOk = true;
static int  g_lastDemoKind = -1;
bool loadModel(const std::string& path) {
    g_lastLoaded = path;
    return g_loadModelOk;
}
bool loadDemoBody(int kind) {
    g_lastDemoKind = kind;
    return g_loadDemoOk;
}


static std::string g_dialogPath;
std::string openFileDialog(void*) { return g_dialogPath; }
std::string openFileDialogNative() { return g_dialogPath; }
std::string saveFileDialogNative(const std::string&) { return g_dialogPath; }

// ---- проверки ----
static int g_checks = 0, g_failed = 0;
static void check(bool cond, const char* what) {
    ++g_checks;
    std::printf("  %s  %s\n", cond ? "ok:  " : "FAIL:", what);
    if (!cond) ++g_failed;
}
static void section(const char* n) { std::printf("\n--- %s ---\n", n); }

static void frame() {
    ImGuiIO& io = ImGui::GetIO();
    io.DeltaTime = 1.0f / 60.0f;
    ImGui::NewFrame();
}

int main() {
    // Тесты не должны трогать настоящий список недавних моделей пользователя:
    // работаем в отдельном каталоге.
    char tmpl[] = "/tmp/aeros-start-XXXXXX";
    const char* dir = mkdtemp(tmpl);
    if (!dir) { std::printf("не удалось создать временный каталог\n"); return 1; }
    if (chdir(dir) != 0) return 1;

    // ---------- ImGui без бэкенда ----------
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(1280.0f, 720.0f);
    io.DeltaTime = 1.0f / 60.0f;
    unsigned char* pixels = nullptr;
    int tw = 0, th = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &tw, &th);
    io.Fonts->SetTexID((ImTextureID)(intptr_t)1);

    std::printf("=== главное меню и недавние модели (v1.35.0) ===\n");

    section("список недавних моделей");
    {
        // Путь во временном каталоге: тест не должен оставлять файлов в репозитории.
        const std::string f = "/tmp/aeros_recent_test.txt";
        // отсутствующий файл — пустой список, а не ошибка
        check(recentfiles::load(f).empty(), "отсутствующий файл читается как пустой список");
        check(recentfiles::add("/models/a.stl", f, 6), "добавление пути");
        recentfiles::add("/models/b.stl", f, 6);
        recentfiles::add("/models/a.stl", f, 6);          // повторно — наверх, без дубля
        std::vector<std::string> r = recentfiles::load(f, 6);
        check(r.size() == 2, "дубликат не создаёт вторую запись");
        check(!r.empty() && r[0] == "/models/a.stl", "повторное открытие поднимает запись наверх");
        for (int i = 0; i < 10; ++i) recentfiles::add("/models/m" + std::to_string(i) + ".stl", f, 6);
        r = recentfiles::load(f, 6);
        check(r.size() == 6, "список ограничен шестью записями");
        check(r[0] == "/models/m9.stl", "самая свежая запись — первая");
        check(!recentfiles::add("/bad\npath.stl", f, 6), "путь с управляющим символом отклоняется");
        check(recentfiles::load(f, 6).size() == 6, "отклонённая запись не попала в файл");
        check(recentfiles::shortName("/models/wing.stl") == "wing.stl", "короткое имя — только файл");
        check(recentfiles::shortName("C:\\models\\wing.stl") == "wing.stl", "короткое имя понимает обратный слэш");
        recentfiles::clear(f);
        check(recentfiles::load(f).empty(), "очистка списка");
    }

    section("недавние исследования и удаление записей (v1.35.0)");
    {
        const std::string f = "/tmp/aeros_recent_studies_test.txt";
        recentfiles::clear(f);
        check(recentfiles::add("/work/a.aerosproj", f, 4), "исследование добавлено в отдельный список");
        recentfiles::add("/work/b.aerosproj", f, 4);
        check(recentfiles::load(f, 4).size() == 2, "список исследований ведётся отдельно от моделей");
        check(recentfiles::remove("/work/a.aerosproj", f), "запись удаляется");
        const std::vector<std::string> r = recentfiles::load(f, 4);
        check(r.size() == 1 && r[0] == "/work/b.aerosproj", "удалена именно указанная запись");
        check(!recentfiles::remove("/work/a.aerosproj", f), "повторное удаление возвращает false");
        check(std::string(recentfiles::studiesPath()) != std::string(recentfiles::defaultPath()),
              "файлы списков моделей и исследований разные");
        recentfiles::clear(f);
    }

    section("drag&drop: разбор файла");
    {
        using dropfiles::Kind;
        check(dropfiles::classify("wing.STL") == Kind::Model, "STL в верхнем регистре — модель");
        check(dropfiles::classify("/tmp/a b/wing.stl") == Kind::Model, "путь с пробелом — модель");
        check(dropfiles::classify("study.aerosproj") == Kind::Study, "проект — исследование");
        check(dropfiles::classify("old.study") == Kind::Study, "старое расширение .study понимается");
        check(dropfiles::classify("x.AerosProj") == Kind::Study, "регистр расширения не важен");
        check(dropfiles::classify("notes.txt") == Kind::Unknown, "текстовый файл не открывается как модель");
        check(dropfiles::classify("noext") == Kind::Unknown, "файл без расширения не угадывается");
        check(dropfiles::classify("") == Kind::Unknown, "пустой путь безопасен");
    }

    section("v1.35.1 — что показывать после запуска");
    {
        // Без модели за главным меню пусто: Esc не должен открывать пустую
        // оболочку, иначе «боковые панели до выбора модели» снова появятся.
        g_vertices.clear();
        check(!startscreen::canClose(), "без модели главное меню по Esc не закрывается");
        g_vertices.assign(9, 0.0f);
        check(startscreen::canClose(), "с загруженной моделью экран закрывается по Esc");
        g_vertices.clear();
        check(!startscreen::canClose(), "после сброса модели правило снова действует");
    }

    section("демо-тела: геометрия");
    for (int k = 0; k < demogeom::count(); ++k) {
        std::vector<float> pos, nrm;
        const bool ok = demogeom::build((demogeom::Body)k, pos, nrm);
        char nm[256];
        std::snprintf(nm, sizeof(nm), "%s: вершины/нормали построены (%zu вершин)",
                      demogeom::nameEn((demogeom::Body)k), pos.size() / 3);
        check(ok && !pos.empty() && pos.size() == nrm.size() && pos.size() % 9 == 0, nm);

        const glm::vec3 r = demogeom::radii((demogeom::Body)k);
        float mn[3] = {1e30f, 1e30f, 1e30f}, mx[3] = {-1e30f, -1e30f, -1e30f};
        float worstNormal = 0.0f;
        for (size_t i = 0; i + 2 < pos.size(); i += 3) {
            for (int a = 0; a < 3; ++a) {
                if (pos[i + a] < mn[a]) mn[a] = pos[i + a];
                if (pos[i + a] > mx[a]) mx[a] = pos[i + a];
            }
            const float len = std::sqrt(nrm[i] * nrm[i] + nrm[i + 1] * nrm[i + 1] + nrm[i + 2] * nrm[i + 2]);
            worstNormal = std::max(worstNormal, std::fabs(len - 1.0f));
        }
        const float ex = 0.5f * (mx[0] - mn[0]);
        const float ey = 0.5f * (mx[1] - mn[1]);
        const float ez = 0.5f * (mx[2] - mn[2]);
        std::snprintf(nm, sizeof(nm), "%s: габариты %.2f x %.2f x %.2f (ожидалось %.2f x %.2f x %.2f)",
                      demogeom::nameEn((demogeom::Body)k), ex, ey, ez, r.x, r.y, r.z);
        check(std::fabs(ex - r.x) < 0.01f * r.x && std::fabs(ey - r.y) < 0.01f * r.y &&
              std::fabs(ez - r.z) < 0.01f * r.z, nm);
        std::snprintf(nm, sizeof(nm), "%s: нормали единичные (макс. отклонение %.4f)",
                      demogeom::nameEn((demogeom::Body)k), worstNormal);
        check(worstNormal < 1e-3f, nm);
    }
    {
        const glm::vec3 w = demogeom::radii(demogeom::Body::Wing);
        const glm::vec3 a = demogeom::radii(demogeom::Body::Airship);
        check(w.y < w.x * 0.2f && w.z > w.x * 2.0f, "крыло тонкое по Y и длинное по размаху Z");
        check(a.x > a.y * 4.0f, "дирижабль вытянут по потоку X");
    }

    section("действия экрана");
    {
        UiLayoutState& st = uiLayoutState();
        aeroExitRequested = false;
        startscreen::state().lastError.clear();

        // Отмена выбора файла — экран остаётся, причина видна.
        g_dialogPath = "";
        check(!startscreen::perform(startscreen::Action::OpenModel, st),
              "отмена диалога не закрывает экран");
        check(!startscreen::state().lastError.empty(),
              "отмена диалога объясняет причину прямо на экране");

        // Ошибка загрузки — тоже остаётся открытым и с сообщением.
        g_dialogPath = "/nonexistent/model.stl";
        g_loadModelOk = false;
        check(!startscreen::perform(startscreen::Action::OpenModel, st),
              "ошибка загрузки не закрывает экран");
        check(startscreen::state().lastError.find("model.stl") != std::string::npos,
              "в сообщении об ошибке есть путь файла");

        // Успешная загрузка — закрывает и запоминает путь.
        recentfiles::clear(recentfiles::defaultPath());
        g_loadModelOk = true;
        g_dialogPath = "wing.stl";
        check(startscreen::perform(startscreen::Action::OpenModel, st), "успешная загрузка закрывает экран");
        check(g_lastLoaded == "wing.stl", "загружен именно выбранный файл");
        {
            const std::vector<std::string> rec = recentfiles::load(recentfiles::defaultPath(), 6);
            check(!rec.empty() && rec[0] == "wing.stl", "открытая модель попала в недавние");
        }

        // Демо-тела.
        g_lastDemoKind = -1;
        check(startscreen::perform(startscreen::Action::DemoWing, st), "демо-крыло загружено");
        check(g_lastDemoKind == (int)demogeom::Body::Wing, "передан правильный вид демо-тела");
        g_loadDemoOk = false;
        check(!startscreen::perform(startscreen::Action::DemoAirship, st),
              "неудача с демо-телом не закрывает экран");
        check(!startscreen::state().lastError.empty(), "и объясняет причину");
        g_loadDemoOk = true;

        // Настройки.
        st.workspace = 2;
        uiSelectedLeaf = 0;
        check(startscreen::perform(startscreen::Action::Settings, st), "пункт «Настройки» закрывает экран");
        check(st.workspace == 0 && uiSelectedLeaf == 1, "и открывает раздел «Общие и язык»");

        // Выход.
        aeroExitRequested = false;
        check(startscreen::perform(startscreen::Action::Exit, st), "пункт «Выход» закрывает экран");
        check(aeroExitRequested, "и выставляет запрос на выход из программы");
        aeroExitRequested = false;

        // Продолжение без модели.
        check(startscreen::perform(startscreen::Action::Continue, st), "«Продолжить без модели» закрывает экран");
    }

    section("отрисовка экрана и баланс стека окон");
    {
        UiLayoutState& st = uiLayoutState();
        const int base = (int)GImGui->CurrentWindowStack.Size;

        auto oneFrame = [&](bool open) {
            aeroStartScreen = open;
            frame();
            startscreen::draw(st);
            ImGui::Render();
            return (int)GImGui->CurrentWindowStack.Size == base;
        };

        check(oneFrame(true), "стек окон сбалансирован при открытом экране");
        check(oneFrame(false), "стек окон сбалансирован при закрытом экране");

        // Пустой список недавних.
        recentfiles::clear(recentfiles::defaultPath());
        check(oneFrame(true), "экран рисуется с пустым списком недавних");

        // Заполненный список.
        for (int i = 0; i < 8; ++i) recentfiles::add("/models/part" + std::to_string(i) + ".stl");
        check(oneFrame(true), "экран рисуется с заполненным списком недавних");

        // Вырожденный размер окна — экран не должен падать и не должен
        // разбалансировать стек.
        io.DisplaySize = ImVec2(10.0f, 10.0f);
        check(oneFrame(true), "экран не падает при вырожденном размере окна (10x10)");
        io.DisplaySize = ImVec2(320.0f, 240.0f);
        check(oneFrame(true), "экран рисуется на минимальном размере (320x240)");
        io.DisplaySize = ImVec2(1280.0f, 720.0f);

        // Крупный масштаб шрифта (проверка, что кнопки не уходят за край).
        st.fontScale = 1.5f;
        check(oneFrame(true), "экран рисуется при масштабе шрифта 1.5x");
        st.fontScale = 1.0f;
        aeroStartScreen = false;
    }

    ImGui::DestroyContext();
    std::printf("\n=== %s (проверок: %d, ошибок: %d) ===\n",
                g_failed == 0 ? "ГЛАВНОЕ МЕНЮ В ПОРЯДКЕ" : "ЕСТЬ ПРОВАЛЫ", g_checks, g_failed);
    return g_failed == 0 ? 0 : 1;
}
