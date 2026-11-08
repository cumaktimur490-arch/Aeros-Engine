#pragma once
// =====================================================
// v1.35.0 — открыть ссылку в системном браузере.
//
// Зачем: «Проверить обновления» не может тянуть HTTP из приложения без новой
// зависимости (WinHTTP/libcurl) и без сетевого кода, который нельзя проверить
// в песочнице. Честная альтернатива — открыть страницу релизов: пользователь
// видит актуальную версию из первых рук, а программа не делает вид, что
// «проверила» обновления.
// =====================================================
#include <cstdlib>
#include <string>

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif

inline bool aerosOpenUrl(const char* url) {
    if (!url || !url[0]) return false;
#ifdef _WIN32
    const HINSTANCE r = ShellExecuteA(nullptr, "open", url, nullptr, nullptr, SW_SHOWNORMAL);
    return reinterpret_cast<INT_PTR>(r) > 32;
#elif defined(__APPLE__)
    const std::string cmd = std::string("open \"") + url + "\" >/dev/null 2>&1 &";
    return std::system(cmd.c_str()) == 0;
#else
    const std::string cmd = std::string("xdg-open \"") + url + "\" >/dev/null 2>&1 &";
    return std::system(cmd.c_str()) == 0;
#endif
}

inline const char* aerosReleasesUrl() {
    return "https://github.com/cumaktimur490-arch/Aeros-Engine/releases/latest";
}
