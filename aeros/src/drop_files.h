#pragma once
// =====================================================
// v1.35.0 — что делать с файлом, перетащенным в окно.
//
// Разбор расширения вынесен из ui.cpp в отдельную функцию, чтобы его можно было
// проверить headless-тестом: ошибка здесь означает «перетащил модель — ничего не
// произошло» или, хуже, попытку открыть STL как проект.
// =====================================================
#include <string>

namespace dropfiles {

enum class Kind { Model, Study, Unknown };

inline Kind classify(const std::string& path) {
    const size_t dot = path.find_last_of('.');
    if (dot == std::string::npos) return Kind::Unknown;
    std::string ext = path.substr(dot);
    for (char& ch : ext) {
        if (ch >= 'A' && ch <= 'Z') ch = (char)(ch - 'A' + 'a');
    }
    if (ext == ".stl") return Kind::Model;
    if (ext == ".aerosproj" || ext == ".study" || ext == ".aero") return Kind::Study;
    return Kind::Unknown;
}

inline const char* kindName(Kind k) {
    switch (k) {
        case Kind::Model: return "model";
        case Kind::Study: return "study";
        default:          return "unknown";
    }
}

} // namespace dropfiles
