#pragma once
// =====================================================
// v1.34.0 — какие символы растеризуются в атлас шрифта интерфейса.
//
// Зачем отдельная таблица. `GetGlyphRangesCyrillic()` покрывает латиницу,
// латинские дополнения и кириллицу, но НЕ покрывает типографскую пунктуацию:
// длинное тире «—» (U+2014), многоточие «…» (U+2026), кавычки-ёлочки ««»»
// (U+00AB/BB есть в Latin-1, а вот U+201C…U+201E нет) и стрелки. В интерфейсе
// эти символы используются повсеместно ("Вход — равновесные популяции",
// "Сохранить исследование…"), и они рисовались пустым ромбиком: текст выглядел
// повреждённым, а причина не была видна ни в журнале, ни в коде вызова шрифта.
//
// Здесь один источник истины: таблица диапазонов, которую получает и
// initLocalization(), и headless-тест (он сканирует строки исходников и
// проверяет, что каждый не-ASCII символ покрыт).
// =====================================================

#include <imgui.h>

// Диапазоны глифов. Значения — включительные, 0 — конец списка.
inline const ImWchar* aerosUiGlyphRanges() {
    static const ImWchar ranges[] = {
        0x0020, 0x00FF,   // Basic Latin + Latin-1 Supplement («» и т.п.)
        0x0100, 0x017F,   // Latin Extended-A (ā, ē — единицы/обозначения)
        0x0400, 0x052F,   // Cyrillic + Cyrillic Supplement
        0x0370, 0x03FF,   // Greek: σ ν ρ τ μ θ λ ω — обозначения в физике/МКЭ
        0x2000, 0x206F,   // General Punctuation: – — … ‘ ’ “ ” † ‰
        0x2070, 0x209F,   // Superscripts/subscripts: ⁴, ₂ (индексы в формулах)
        0x20A0, 0x20BF,   // Currency symbols (₽, €)
        0x2100, 0x214F,   // Letterlike symbols: № (номер), ℓ
        0x2190, 0x21FF,   // Arrows (→ ← ↑ ↓)
        0x2200, 0x22FF,   // Mathematical operators (≈ ≠ ≤ ≥ ∞)
        0x00B0, 0x00B0,   // ° (градус) — уже в Latin-1, оставлено как напоминание
        0x2DE0, 0x2DFF,   // Cyrillic Extended-A
        0xA640, 0xA69F,   // Cyrillic Extended-B
        0,
    };
    return ranges;
}

// Проверка покрытия одного символа — используется тестом и годится для
// диагностики: если в интерфейс попадёт символ вне таблицы, это будет видно.
inline bool aerosUiGlyphCovered(unsigned int codepoint) {
    const ImWchar* r = aerosUiGlyphRanges();
    for (int i = 0; r[i] != 0 && r[i + 1] != 0; i += 2)
        if (codepoint >= (unsigned int)r[i] && codepoint <= (unsigned int)r[i + 1]) return true;
    return false;
}
