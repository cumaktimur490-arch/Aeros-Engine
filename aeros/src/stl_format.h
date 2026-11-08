#pragma once
// =====================================================
// v1.36.0 — определение формата STL по содержимому.
//
// Почему отдельный модуль. Формат STL не имеет магии в заголовке: бинарный файл
// это 80 байт произвольного текста + uint32 с числом треугольников + 50 байт на
// треугольник. ASCII начинается с `solid`, но так же может начинаться и
// бинарный файл (так пишет, например, SolidWorks), поэтому одного слова мало.
//
// Что было не так до v1.36.0 (stl_loader.cpp):
//
//     bool isBinary = (fileSize >= 84) && (fileSize == 84 + nt * 50);
//     if (nt > 10000000) isBinary = false;
//
//   * требовалось ТОЧНОЕ совпадение размера. Любой реальный файл с хвостом
//     (мусор, выравнивание, дописанные CAD-данные) уходил в ASCII-парсер, тот
//     читал бинарный мусор как текст и возвращал пустую или битую модель;
//   * модели на 10+ млн треугольников принудительно объявлялись ASCII, то есть
//     гарантированно ломались: это ограничение памяти, а не формата.
//
// Здесь решение принимается по трём признакам: сколько треугольников обещает
// заголовок, хватает ли на них данных (хвост допускается) и похоже ли начало
// файла на текст. Функция чистая — её проверяет headless-тест на синтетических
// файлах, без обращения к диску.
// =====================================================

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace stlformat {

enum class Format { Binary, Ascii, Unknown };

struct Detection {
    Format format = Format::Unknown;
    std::uint32_t declaredTriangles = 0;   // из заголовка (для Binary)
    std::size_t payloadBytes = 0;          // сколько занимают треугольники
    std::size_t trailingBytes = 0;         // хвост после последнего треугольника
    const char* reason = "";               // короткое пояснение для журнала
};

inline std::uint32_t readLE32(const unsigned char* p) {
    return (std::uint32_t)p[0] | ((std::uint32_t)p[1] << 8) |
           ((std::uint32_t)p[2] << 16) | ((std::uint32_t)p[3] << 24);
}

// Начинается ли файл с "solid" (регистр не важен).
inline bool startsWithSolid(const unsigned char* d, std::size_t n) {
    if (n < 5) return false;
    const char* s = "solid";
    for (int i = 0; i < 5; ++i) {
        char c = (char)d[i];
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        if (c != s[i]) return false;
    }
    return true;
}

// Похоже ли начало файла на текст: нет NUL и управляющих байтов (кроме \t\r\n).
// Проверяем первые 2 КБ — этого достаточно, чтобы отличить ASCII-STL от
// бинарного даже с текстовым 80-байтовым заголовком.
inline bool looksLikeText(const unsigned char* d, std::size_t n) {
    const std::size_t limit = (n < 2048) ? n : 2048;
    for (std::size_t i = 0; i < limit; ++i) {
        const unsigned char c = d[i];
        if (c == 0) return false;
        if (c < 9 || (c > 13 && c < 32) || c == 127) return false;
    }
    return true;
}

// Есть ли в тексте ключевое слово ASCII-STL (facet/vertex).
inline bool hasAsciiKeywords(const unsigned char* d, std::size_t n) {
    const std::size_t limit = (n < 4096) ? n : 4096;
    for (std::size_t i = 0; i + 5 < limit; ++i) {
        if ((d[i] | 0x20) == 'f' && (d[i+1] | 0x20) == 'a' && (d[i+2] | 0x20) == 'c' &&
            (d[i+3] | 0x20) == 'e' && (d[i+4] | 0x20) == 't') return true;
        if ((d[i] | 0x20) == 'v' && (d[i+1] | 0x20) == 'e' && (d[i+2] | 0x20) == 'r' &&
            (d[i+3] | 0x20) == 't' && (d[i+4] | 0x20) == 'e' && (d[i+5] | 0x20) == 'x')
            return true;
    }
    return false;
}

inline Detection detect(const unsigned char* d, std::size_t size) {
    Detection out;
    if (!d || size == 0) {
        out.reason = "пустой файл";
        return out;
    }

    const bool solid = startsWithSolid(d, size);
    const bool text = looksLikeText(d, size);
    const bool keywords = text && hasAsciiKeywords(d, size);

    std::uint32_t nt = 0;
    if (size >= 84) nt = readLE32(d + 80);

    // Требуемый объём данных для nt треугольников (64-бит: 50 * 2^32 не влезает
    // в uint32, а «переполнение» здесь означало бы «не бинарный»).
    const std::uint64_t need = 84ull + (std::uint64_t)nt * 50ull;
    const bool fits = (nt > 0) && (need <= (std::uint64_t)size);

    out.declaredTriangles = nt;

    // 1) ASCII с настоящими ключевыми словами: даже если байты 80..84 случайно
    //    похожи на небольшое число, текст важнее.
    if (solid && keywords) {
        out.format = Format::Ascii;
        out.reason = "начинается с 'solid' и содержит facet/vertex";
        return out;
    }

    // 2) Бинарный: данных хватает на заявленное число треугольников.
    //    Хвост допускается и лишь подсчитывается — именно из-за требования
    //    точного размера реальные файлы раньше уходили в ASCII-парсер.
    if (fits) {
        out.format = Format::Binary;
        out.payloadBytes = (std::size_t)nt * 50;
        out.trailingBytes = size - (std::size_t)need;
        out.reason = out.trailingBytes ? "бинарный, с хвостом после треугольников"
                                       : "бинарный, размер совпадает точно";
        return out;
    }

    // 3) Текст без ключевых слов (обрезанный ASCII) — пусть решает парсер.
    if (text && !solid) {
        out.format = keywords ? Format::Ascii : Format::Unknown;
        out.reason = keywords ? "текст без 'solid'" : "текст без facet/vertex";
        return out;
    }

    // 4) Заголовок заявляет больше данных, чем есть в файле.
    if (nt > 0 && need > (std::uint64_t)size) {
        out.reason = "файл обрезан: заголовок обещает больше треугольников, чем есть";
        return out;
    }

    out.reason = "формат не распознан";
    return out;
}

} // namespace stlformat
