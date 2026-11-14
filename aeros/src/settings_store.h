#pragma once
// =====================================================
// v1.32.0 — единый реестр настроек Aeros Engine.
//
// Зачем: органы управления в интерфейсе привязаны к переменным из globals.h,
// а сохранялось из них меньше трети (28 ключей из более чем сотни). Пользователь
// подгонял цвета, прозрачности, эффекты, FSR/генерацию кадров и оптимизацию под
// свою машину, а после перезапуска всё возвращалось к стандарту.
//
// Теперь каждая настройка описана ОДНОЙ записью в таблице settings_table.inc
// (ключ в aeros_settings.ini, способ чтения и записи значения, допустимый
// диапазон, стандартное значение). Из таблицы автоматически получаются:
//   - сохранение и загрузка (с клампом по диапазону, устойчиво к мусору в файле);
//   - сброс всех настроек к стандартным значениям;
//   - профили (именованные файлы profiles/<имя>.ini) — сохранить/загрузить;
//   - счётчик ключей для интерфейса и тестов.
//
// Модуль чистый: без OpenGL, GLFW и ImGui, поэтому проверяется headless-тестом
// aeros/tests/settings_headless.cpp.
//
// Формат файла прежний (`ключ=значение`, строки `#` — комментарии), поэтому
// aeros_settings.ini от v1.31.x читается без потерь, а новые ключи добавляются
// к нему при следующем сохранении.
// =====================================================

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <system_error>
#include <vector>

#include "version.h"       // AEROS_VERSION_STRING — шапка файла профиля
#include "atmosphere.h"   // altitude, speedUnit, useRealDensity — тоже настройки
#include "globals.h"
#include "lbm.h"            // lbmParams — параметры решателя LBM

// Описание одной настройки. kind: 0 = логическая, 1 = целая, 2 = вещественная
// (нужен для точной записи в файл: 0/1 вместо 0.000000).
struct Setting {
    const char* key;
    int kind;
    std::function<double()> get;
    std::function<void(double)> set;
    double mn;
    double mx;
    double def;
};

#define SET_B(key, var, mn, mx, def)                                        \
    Setting {                                                               \
        key, 0, []() -> double { return (var) ? 1.0 : 0.0; },               \
            [](double v) { (var) = (v > 0.5); }, mn, mx, def                \
    },
#define SET_I(key, var, mn, mx, def)                                        \
    Setting {                                                               \
        key, 1, []() -> double { return (double)(var); },                   \
            [](double v) { (var) = (int)v; }, mn, mx, def                   \
    },
#define SET_F(key, var, mn, mx, def)                                        \
    Setting {                                                               \
        key, 2, []() -> double { return (double)(var); },                   \
            [](double v) { (var) = (float)v; }, mn, mx, def                 \
    },
// Универсальная запись: когда значение не простая переменная (поле структуры,
// элемент вектора, перечисление — через явные приведения в макросах).
#define SET_X(key, getexpr, setexpr, mn, mx, def)                           \
    Setting {                                                               \
        key, 2, []() -> double { return (double)(getexpr); },               \
            [](double v) { setexpr; }, mn, mx, def                          \
    },

// Сама таблица: 140 записей, генерируется из globals.cpp и atmosphere.cpp,
// чтобы стандартные значения совпадали с инициализаторами переменных.
inline const std::vector<Setting>& settingsTable() {
    static const std::vector<Setting> t = {
#include "settings_table.inc"
    };
    return t;
}

inline int settingsCount() { return (int)settingsTable().size(); }

inline const Setting* settingsFind(const std::string& key) {
    for (const Setting& s : settingsTable())
        if (key == s.key) return &s;
    return nullptr;
}

// Значение, приведённое к допустимому диапазону. Нечисловое (NaN) заменяется
// стандартным: иначе один испорченный символ в файле ломал бы отображение.
inline double settingsClamp(const Setting& s, double v) {
    if (!(v == v)) return s.def;
    if (v < s.mn) return s.mn;
    if (v > s.mx) return s.mx;
    return v;
}

inline void settingsWriteValue(std::ostream& os, const Setting& s) {
    char buf[48];
    const double v = s.get();
    if (s.kind == 0) {
        std::snprintf(buf, sizeof(buf), "%d", (v > 0.5) ? 1 : 0);
    } else if (s.kind == 1) {
        std::snprintf(buf, sizeof(buf), "%ld", (long)std::lround(v));
    } else {
        // %.9g — float печатается без потери точности и без «мусорных» хвостов.
        std::snprintf(buf, sizeof(buf), "%.9g", v);
    }
    os << s.key << "=" << buf << "\n";
}

// Записать все настройки в поток (в том же формате, что aeros_settings.ini).
inline void settingsWrite(std::ostream& os) {
    for (const Setting& s : settingsTable()) settingsWriteValue(os, s);
}

inline std::string settingsTrim(const std::string& in) {
    size_t a = 0, b = in.size();
    while (a < b && (in[a] == ' ' || in[a] == '\t' || in[a] == '\r')) ++a;
    while (b > a && (in[b - 1] == ' ' || in[b - 1] == '\t' || in[b - 1] == '\r')) --b;
    return in.substr(a, b - a);
}

// Разобрать одну строку файла. Возвращает: 1 — применено, 0 — ключ неизвестен
// или строка-комментарий, -1 — значение не число (настройка не тронута).
inline int settingsApplyLine(const std::string& rawLine) {
    const std::string line = settingsTrim(rawLine);
    if (line.empty() || line[0] == '#' || line[0] == ';') return 0;
    const size_t eq = line.find('=');
    if (eq == std::string::npos) return 0;
    const std::string key = settingsTrim(line.substr(0, eq));
    const std::string val = settingsTrim(line.substr(eq + 1));
    const Setting* s = settingsFind(key);
    if (!s) return 0;
    double parsed = 0.0;
    if (val == "true" || val == "on" || val == "yes") {
        parsed = 1.0;
    } else if (val == "false" || val == "off" || val == "no") {
        parsed = 0.0;
    } else {
        const char* begin = val.c_str();
        char* end = nullptr;
        parsed = std::strtod(begin, &end);
        if (end == begin) return -1;  // не число — оставляем прежнее значение
    }
    s->set(settingsClamp(*s, parsed));
    return 1;
}

// Прочитать файл настроек. Возвращает число применённых ключей или -1, если
// файл не открылся (нет файла — это не ошибка для вызывающего, см. main.cpp).
inline int settingsRead(const std::string& path) {
    std::ifstream f(path);
    if (!f) return -1;
    int applied = 0;
    std::string line;
    while (std::getline(f, line))
        if (settingsApplyLine(line) == 1) ++applied;
    return applied;
}

inline void settingsResetDefaults() {
    for (const Setting& s : settingsTable()) s.set(settingsClamp(s, s.def));
}

// -----------------------------------------------------
// Профили настроек: именованные файлы в каталоге profiles/.
// Имя ограничено [A-Za-z0-9_-] и длиной 40: подставлять произвольную строку в
// путь нельзя (иначе «../» уводил бы файл куда угодно).
// -----------------------------------------------------
inline std::string settingsProfileDir() { return "profiles"; }

inline bool settingsProfileNameOk(const std::string& name) {
    if (name.empty() || name.size() > 40) return false;
    for (char c : name) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                        (c >= '0' && c <= '9') || c == '_' || c == '-';
        if (!ok) return false;
    }
    return true;
}

inline std::string settingsProfilePath(const std::string& name) {
    return settingsProfileDir() + "/" + name + ".ini";
}

inline bool settingsProfileSave(const std::string& name) {
    if (!settingsProfileNameOk(name)) return false;
    std::error_code ec;
    std::filesystem::create_directories(settingsProfileDir(), ec);
    std::ofstream f(settingsProfilePath(name));
    if (!f) return false;
    f << "# Aeros Engine v" AEROS_VERSION_STRING " Settings profile: " << name << "\n";
    settingsWrite(f);
    return f.good();
}

inline bool settingsProfileLoad(const std::string& name) {
    if (!settingsProfileNameOk(name)) return false;
    return settingsRead(settingsProfilePath(name)) >= 0;
}

inline bool settingsProfileDelete(const std::string& name) {
    if (!settingsProfileNameOk(name)) return false;
    std::error_code ec;
    return std::filesystem::remove(settingsProfilePath(name), ec);
}

// Имена существующих профилей (без расширения), по алфавиту.
inline std::vector<std::string> settingsProfileList() {
    std::vector<std::string> out;
    std::error_code ec;
    const std::filesystem::path dir(settingsProfileDir());
    if (!std::filesystem::is_directory(dir, ec)) return out;
    for (const auto& e : std::filesystem::directory_iterator(dir, ec)) {
        if (!e.is_regular_file(ec)) continue;
        if (e.path().extension() != ".ini") continue;
        const std::string stem = e.path().stem().string();
        if (settingsProfileNameOk(stem)) out.push_back(stem);
    }
    std::sort(out.begin(), out.end());
    return out;
}
