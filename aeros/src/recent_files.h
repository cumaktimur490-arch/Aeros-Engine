#pragma once
// =====================================================
// v1.34.0 — список недавно открытых моделей для главного меню.
//
// Формат: обычный текст, один путь на строку, самая свежая запись — первая.
// Файл лежит рядом с настройками (aeros_recent.txt), поэтому не требует ни
// реестра, ни сторонних библиотек.
//
// Почему отдельный модуль: список читается и пишется не только интерфейсом, а
// ошибки в нём (пустые строки, дубликаты, отсутствующий файл) не должны ронять
// программу или показывать «недавние», которых нет. Здесь всё это —
// детерминированные функции, их можно проверить headless-тестом.
// =====================================================

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

namespace recentfiles {

inline const char* defaultPath() { return "aeros_recent.txt"; }

// Прочитать список. Отсутствующий файл — это не ошибка, а пустой список.
// Пути длиннее 1024 символов и строки с управляющими символами отбрасываются:
// такой «путь» всё равно не откроется, а в интерфейсе сломал бы раскладку.
inline std::vector<std::string> load(const std::string& path = defaultPath(),
                                     size_t maxCount = 6) {
    std::vector<std::string> out;
    if (maxCount == 0) return out;
    std::ifstream f(path);
    if (!f) return out;
    std::string line;
    while (std::getline(f, line) && out.size() < maxCount) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t'))
            line.pop_back();
        size_t first = line.find_first_not_of(" \t");
        if (first == std::string::npos) continue;
        line = line.substr(first);
        if (line.empty() || line.size() > 1024) continue;
        bool bad = false;
        for (unsigned char c : line)
            if (c < 0x20) { bad = true; break; }
        if (bad) continue;
        if (std::find(out.begin(), out.end(), line) != out.end()) continue;  // дубликат
        out.push_back(line);
    }
    return out;
}

// Добавить путь в начало списка. Повторное открытие той же модели не создаёт
// второй записи — она поднимается наверх.
inline bool add(const std::string& file, const std::string& path = defaultPath(),
                size_t maxCount = 6) {
    if (file.empty() || file.size() > 1024 || maxCount == 0) return false;
    for (unsigned char c : file)
        if (c < 0x20) return false;

    std::vector<std::string> items = load(path, maxCount > 32 ? maxCount : 32);
    items.erase(std::remove(items.begin(), items.end(), file), items.end());
    items.insert(items.begin(), file);
    if (items.size() > maxCount) items.resize(maxCount);

    std::ofstream f(path, std::ios::trunc);
    if (!f) return false;
    for (const auto& s : items) f << s << '\n';
    return f.good();
}

inline void clear(const std::string& path = defaultPath()) {
    std::ofstream f(path, std::ios::trunc);
    // Пустой файл — валидный «пустой список»; ошибку открытия игнорируем
    // осознанно: очистка списка не должна ронять интерфейс.
}

// Короткое имя для кнопки: последний компонент пути, без каталогов.
inline std::string shortName(const std::string& path) {
    const size_t sep = path.find_last_of("/\\");
    return (sep == std::string::npos) ? path : path.substr(sep + 1);
}

}  // namespace recentfiles
