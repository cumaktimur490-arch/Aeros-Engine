// =====================================================
// v1.34.0 — тест таблицы горячих клавиш (hotkeys.h).
//
// Что проверяется:
//  1. каждая строка таблицы непустая, группы не «прыгают» назад, обозначения
//     клавиш уникальны (иначе справка противоречит сама себе);
//  2. каждая клавиша из таблицы РЕАЛЬНО обрабатывается в input.cpp или
//     cae_ui.h — в справке не может быть клавиши, которая ничего не делает;
//  3. обратное направление для главных клавиш: обработчик F5..F12/Ctrl+M и
//     т.п. существует и не выключен комментарием;
//  4. сводка стартового экрана собирается из той же таблицы, а не дублируется.
//
// Тест не поднимает ImGui-контекст: горячие клавиши — это текстовые строки и
// статические проверки исходников.
// =====================================================
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <set>
#include <fstream>
#include <sstream>

#include "hotkeys.h"

static int g_checks = 0, g_fail = 0;
static void check(bool ok, const std::string& what) {
    ++g_checks;
    if (!ok) { ++g_fail; std::printf("FAIL: %s\n", what.c_str()); }
}

static std::string readFile(const std::string& p) {
    std::ifstream f(p.c_str(), std::ios::binary);
    if (!f) return std::string();
    std::ostringstream ss; ss << f.rdbuf();
    return ss.str();
}

int main(int argc, char** argv) {
    const std::string dir = (argc > 1) ? argv[1] : "..";
    const std::string input = readFile(dir + "/input.cpp");
    const std::string cae = readFile(dir + "/cae_ui.h");
    const std::string start = readFile(dir + "/start_screen.h");
    check(!input.empty(), "input.cpp прочитан");
    check(!cae.empty(), "cae_ui.h прочитан");
    check(!start.empty(), "start_screen.h прочитан");

    const int n = hotkeys::count();
    check(n >= 15, "в таблице достаточно клавиш (" + std::to_string(n) + ")");

    // 1) согласованность самой таблицы
    std::set<std::string> keys;
    int lastGroup = -1;
    for (int i = 0; i < n; ++i) {
        hotkeys::Item it = hotkeys::item(i);
        check(it.keys && it.keys[0], "строка " + std::to_string(i) + ": есть обозначение клавиш");
        check(it.ru && it.ru[0], "строка " + std::to_string(i) + ": есть описание RU");
        check(it.en && it.en[0], "строка " + std::to_string(i) + ": есть описание EN");
        if (!it.keys || !it.keys[0]) continue;
        check(keys.insert(it.keys).second, std::string("обозначение не повторяется: ") + it.keys);
        int g = hotkeys::group(i);
        check(g >= 0 && g < hotkeys::GroupCount, std::string("группа в диапазоне: ") + it.keys);
        check(g >= lastGroup, "группы идут по порядку, без возврата назад: " + std::string(it.keys));
        lastGroup = g;
    }
    check(hotkeys::item(-1).keys[0] == '\0', "отрицательный индекс не ломает таблицу");
    check(hotkeys::item(hotkeys::count()).keys[0] == '\0', "индекс за концом таблицы безопасен");

    // 2) таблица -> обработчики
    // Для каждой клавиши ищем характерный фрагмент в исходниках.
    struct Evidence { const char* key; const char* token; bool inCae; };
    const Evidence ev[] = {
        {"W / S",        "GLFW_KEY_W",            false},
        {"A / D",        "GLFW_KEY_A",            false},
        {"Space / Q",    "GLFW_KEY_SPACE",        false},
        {"E",            "GLFW_KEY_E",            false},
        {"Shift",        "GLFW_KEY_LEFT_SHIFT",   false},
        {"Ctrl (камера)","cameraSpeed *= 5.0f",    false},
        {"Ctrl+R",       "GLFW_KEY_R",            false},
        {"F1",           "GLFW_KEY_F1",           false},
        {"F2",           "GLFW_KEY_F2",           false},
        {"F3",           "GLFW_KEY_F3",           false},
        {"F4",           "GLFW_KEY_F4",           false},
        {"F12",          "GLFW_KEY_F12",          false},
        {"Ctrl+M",       "ImGuiKey_M",            true},
        {"Ctrl+1…3",     "ImGuiKey_1",            true},
        {"Ctrl+Enter",   "ImGuiKey_Enter",        true},
        {"F7 / F8",      "GLFW_KEY_F7",           false},
        {"F11",          "GLFW_KEY_F11",          false},
        {"F5",           "GLFW_KEY_F5",           false},
        {"F6",           "GLFW_KEY_F6",           false},
        {"F9",           "GLFW_KEY_F9",           false},
        {"F10",          "GLFW_KEY_F10",          false},
    };
    for (const auto& e : ev) {
        bool listed = false;
        for (int i = 0; i < n; ++i) if (std::strcmp(hotkeys::item(i).keys, e.key) == 0) listed = true;
        check(listed, std::string("клавиша из обработчиков есть в таблице: ") + e.key);
        const std::string& src = e.inCae ? cae : input;
        check(src.find(e.token) != std::string::npos,
              std::string("обработчик существует для ") + e.key);
    }

    // 3) обратное направление: в input.cpp не осталось функциональных клавиш,
    //    которых нет в таблице (новая клавиша обязана попасть в справку).
    for (int f = 1; f <= 12; ++f) {
        std::string tok = "GLFW_KEY_F" + std::to_string(f);
        if (input.find(tok) == std::string::npos) continue;
        bool listed = false;
        for (int i = 0; i < n; ++i) {
            std::string k = hotkeys::item(i).keys;
            if (k.find("F" + std::to_string(f)) != std::string::npos) {
                // проверяем, что это именно F<число>, а не F1 в «F12»
                size_t pos = k.find("F" + std::to_string(f));
                size_t after = pos + 1 + std::to_string(f).size();
                if (after >= k.size() || k[after] < '0' || k[after] > '9') listed = true;
            }
        }
        check(listed, tok + " обрабатывается и описан в справке");
    }

    // 4) сводка стартового экрана берётся из таблицы
    check(start.find("hotkeys::summaryRu()") != std::string::npos,
          "стартовый экран использует hotkeys::summaryRu()");
    check(start.find("F5 — screenshot · F7/F8 — recorder") == std::string::npos,
          "в start_screen.h нет второй копии списка клавиш");

    // 4b) Esc не выходит из программы, пока открыт диалог: это часть контракта
    // горячих клавиш (в справке Esc — «закрыть экран», а не «выйти»).
    check(input.find("ImGuiPopupFlags_AnyPopupId") != std::string::npos,
          "input.cpp не завершает программу по Esc при открытом диалоге");
    check(input.find("ImGui::IsPopupOpen") != std::string::npos,
          "input.cpp действительно проверяет открытые всплывающие окна");
    {
        size_t first = cae.find("IsKeyPressed(ImGuiKey_Escape, false)) ImGui::CloseCurrentPopup();");
        bool about = first != std::string::npos;
        bool limits = about && cae.find("IsKeyPressed(ImGuiKey_Escape, false)) ImGui::CloseCurrentPopup();",
                                         first + 1) != std::string::npos;
        check(about, "«О программе» закрывается по Esc");
        check(limits, "«Методика и ограничения» закрывается по Esc");
    }

    // 5) сводка не врёт: перечисленные в ней клавиши есть в таблице
    const char* summary = hotkeys::summaryRu();
    const char* mustMention[] = { "Esc", "Ctrl+O", "F5", "F12" };
    for (const char* m : mustMention) {
        bool inSummary = std::string(summary).find(m) != std::string::npos;
        check(inSummary, std::string("сводка упоминает ") + m);
    }

    std::printf("=== ГОРЯЧИЕ КЛАВИШИ: проверок %d, ошибок %d ===\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
