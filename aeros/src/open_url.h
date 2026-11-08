#pragma once
// =====================================================
// v1.35.0 — открыть ссылку в системном браузере.
// v1.36.0 — без shell и с честным результатом.
//
// Зачем: «Проверить обновления» не может тянуть HTTP из приложения без новой
// зависимости (WinHTTP/libcurl) и без сетевого кода, который нельзя проверить
// в песочнице. Честная альтернатива — открыть страницу релизов: пользователь
// видит актуальную версию из первых рук, а программа не делает вид, что
// «проверила» обновления.
//
// Что было не так до v1.36.0 (open_url.h):
//
//     const std::string cmd = "xdg-open \"" + url + "\" >/dev/null 2>&1 &";
//     return std::system(cmd.c_str()) == 0;
//
//   * url попадал в командную строку оболочки: кавычка/`;`/`$(...)` в ссылке
//     исполнялись как shell-код (латентно, пока все вызовы — константы, но API
//     принимает const char* и обязан быть безопасным);
//   * из-за `&` код возврата — статус sh, а не xdg-open, поэтому «успех»
//     возвращался и тогда, когда xdg-open в системе нет.
//
// Теперь argv передаётся execvp() напрямую (proc_util.h) и проверяется код
// выхода самой программы. Плюс отдельная проверка URL: управляющие символы
// (перевод строки, NUL-подобные) отклоняются до запуска — как защита в глубину.
// =====================================================

#include <cstring>
#include <string>

#include "proc_util.h"

#ifdef _WIN32
// windows.h объявляет min()/max() макросами, а этот заголовок подключается из
// общих TU (cae_ui.h) — после него `std::max(a, b)` перестаёт компилироваться
// (MSVC: C2589 «illegal token on right side of '::'»). NOMINMAX просит Windows
// их не объявлять; undef нужен на случай, если windows.h уже был включён
// раньше без этого макроса.
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#  include <shellapi.h>
#  ifdef min
#    undef min
#  endif
#  ifdef max
#    undef max
#  endif
#endif

// -----------------------------------------------------
// Проверка ссылки ДО запуска: пустая, слишком длинная и со управляющими
// символами отклоняется. Функция чистая — её проверяет headless-тест.
// -----------------------------------------------------
inline bool aerosUrlIsSafe(const char* url) {
    if (!url || !url[0]) return false;
    const size_t len = std::strlen(url);
    if (len > 4096) return false;
    for (const char* p = url; *p; ++p) {
        const unsigned char c = (unsigned char)*p;
        if (c < 0x20 || c == 0x7f) return false;   // \n, \r, \t, ESC и прочее
    }
    return true;
}

// -----------------------------------------------------
// Открыть url конкретной программой (opener получает url ровно одним
// аргументом). Отдельная функция нужна тесту: он подставляет свой исполняемый
// файл и проверяет, что спецсимволы не превратились в команды.
//
// Возвращает true только если программа запустилась И завершилась с кодом 0.
// -----------------------------------------------------
inline bool aerosOpenUrlWith(const char* opener, const char* url) {
    if (!opener || !opener[0] || !aerosUrlIsSafe(url)) return false;
    const procutil::Result r = procutil::run({std::string(opener), std::string(url)});
    return r.ok;
}

inline bool aerosOpenUrl(const char* url) {
    if (!aerosUrlIsSafe(url)) return false;
#ifdef _WIN32
    // ShellExecute без shell: url уходит прямо в ShellExecuteA как строка.
    const HINSTANCE r = ShellExecuteA(nullptr, "open", url, nullptr, nullptr, SW_SHOWNORMAL);
    return reinterpret_cast<INT_PTR>(r) > 32;
#elif defined(__APPLE__)
    return aerosOpenUrlWith("/usr/bin/open", url);
#else
    // xdg-open — стандарт freedesktop; если его нет, пробуем sensible-browser.
    // Раньше оба варианта «успешно» завершались через system() даже без бинаря.
    if (aerosOpenUrlWith("xdg-open", url)) return true;
    return aerosOpenUrlWith("sensible-browser", url);
#endif
}

inline const char* aerosReleasesUrl() {
    return "https://github.com/cumaktimur490-arch/Aeros-Engine/releases/latest";
}
