#pragma once
// =====================================================
// Aeros Engine — система локализации v1.15.0
// Поддерживает русский и английский, переключение в рантайме
// =====================================================

#include <string>

enum class Language {
    English = 0,
    Russian = 1
};

extern Language currentLanguage;

// Базовая функция перевода — возвращает строку в зависимости от языка
inline const char* TR(const char* en, const char* ru) {
    return (currentLanguage == Language::Russian) ? ru : en;
}

// Короткий алиас для удобства
#define _TR(en, ru) TR(en, ru)

// Получить имя языка
inline const char* getLanguageName(Language lang) {
    return (lang == Language::Russian) ? "Русский" : "English";
}

// Получить имя текущего языка
inline const char* getCurrentLanguageName() {
    return getLanguageName(currentLanguage);
}

// Список языков для комбо-бокса
inline const char* getLanguageList() {
    // Для ImGui combo с нулевым разделителем
    return "English\0Русский\0";
}

// Функция для получения перевода по ключу (расширяемая)
std::string getTranslation(const std::string& key);

// Инициализация локализации (загрузка шрифта с кириллицей)
void initLocalization();

// Проверка, является ли текущий язык русским
inline bool isRussian() { return currentLanguage == Language::Russian; }
inline bool isEnglish() { return currentLanguage == Language::English; }
