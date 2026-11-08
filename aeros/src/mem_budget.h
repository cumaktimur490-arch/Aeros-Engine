#pragma once
// =====================================================
// v1.36.0 — сколько памяти реально есть у машины.
//
// Зачем. Раньше потолки сетки были магическими константами (10M ячеек в
// voxel_grid.cpp, 8M в lbm.cpp), и при превышении программа МОЛЧА уменьшала
// сетку. Пользователь не знал, что расчёт прошёл на другой сетке, а результаты
// становились невоспроизводимыми. Бюджет должен считаться от доступной памяти,
// а не от числа, выбранного однажды.
//
// Значения — «доступно сейчас», а не «установлено в машине»: у 16 ГБ ноутбука
// с браузером свободным может быть 3 ГБ, и планировать по 16 нельзя.
// =====================================================

#include <cstddef>
#include <cstdint>
#include <string>

#ifdef _WIN32
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#  ifdef min
#    undef min
#  endif
#  ifdef max
#    undef max
#  endif
#else
#  include <unistd.h>
#endif

namespace membudget {

// Физическая память машины (0, если узнать не удалось).
inline std::uint64_t physicalBytes() {
#ifdef _WIN32
    MEMORYSTATUSEX st;
    st.dwLength = sizeof(st);
    if (GlobalMemoryStatusEx(&st)) return (std::uint64_t)st.ullTotalPhys;
    return 0;
#else
    const long pages = sysconf(_SC_PHYS_PAGES);
    const long page = sysconf(_SC_PAGE_SIZE);
    if (pages <= 0 || page <= 0) return 0;
    return (std::uint64_t)pages * (std::uint64_t)page;
#endif
}

// Доступная (свободная) память сейчас.
inline std::uint64_t availableBytes() {
#ifdef _WIN32
    MEMORYSTATUSEX st;
    st.dwLength = sizeof(st);
    if (GlobalMemoryStatusEx(&st)) return (std::uint64_t)st.ullAvailPhys;
    return 0;
#else
    const long pages = sysconf(_SC_AVPHYS_PAGES);
    const long page = sysconf(_SC_PAGE_SIZE);
    if (pages <= 0 || page <= 0) return 0;
    return (std::uint64_t)pages * (std::uint64_t)page;
#endif
}

// Бюджет для одного расчёта: половина доступной памяти, но не больше 60 %
// физической. Половина — потому что кроме сетки в процессе живут модель,
// интерфейс и копии полей; забирать всё свободное — верный способ получить
// OOM-killer вместо сообщения об ошибке.
inline std::uint64_t budgetBytes() {
    const std::uint64_t avail = availableBytes();
    const std::uint64_t phys = physicalBytes();
    std::uint64_t budget = avail / 2;
    if (phys > 0 && budget > phys / 10 * 6) budget = phys / 10 * 6;
    if (budget < 256ull * 1024 * 1024) budget = 256ull * 1024 * 1024;   // минимум для расчёта
    return budget;
}

inline std::string formatBytes(std::uint64_t bytes) {
    char buf[64];
    if (bytes >= (1ull << 30)) {
        std::snprintf(buf, sizeof(buf), "%.2f ГБ", (double)bytes / (double)(1ull << 30));
    } else if (bytes >= (1ull << 20)) {
        std::snprintf(buf, sizeof(buf), "%.0f МБ", (double)bytes / (double)(1ull << 20));
    } else {
        std::snprintf(buf, sizeof(buf), "%.0f КБ", (double)bytes / 1024.0);
    }
    return buf;
}

} // namespace membudget
