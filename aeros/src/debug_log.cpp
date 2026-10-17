// =====================================================
// Aeros Engine — реализация журнала отладки (см. debug_log.h)
// Работает на Windows (MSVC x64/x86/arm64) и Linux (g++), без внешних зависимостей.
// =====================================================

#ifdef _MSC_VER
// fopen/getenv в этом модуле используются намеренно (кросс-платформенно):
// без этой строки MSVC засыпает сборку предупреждениями C4996.
#define _CRT_SECURE_NO_WARNINGS 1
#endif

#include "debug_log.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <cmath>
#include <chrono>
#include <deque>
#include <mutex>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <sys/stat.h>
#include <sys/utsname.h>
#include <unistd.h>
#endif

// cpuid есть только на x86/x64 — на ARM64 (Windows и Linux) его не подключаем,
// иначе сборка arm64 не компилируется.
#if defined(_MSC_VER) && (defined(_M_IX86) || defined(_M_X64))
  #include <intrin.h>
  #define AEROS_HAVE_CPUID 1
#elif (defined(__GNUC__) || defined(__clang__)) && (defined(__i386__) || defined(__x86_64__))
  #include <cpuid.h>
  #define AEROS_HAVE_CPUID 1
#else
  #define AEROS_HAVE_CPUID 0
#endif

namespace {

struct Fact {
    std::string category;
    std::string key;
    std::string value;
};

std::mutex                 g_mutex;
FILE*                      g_logFile   = nullptr;
FILE*                      g_csvFile   = nullptr;
bool                       g_enabled   = false;   // режим отладки (F9)
bool                       g_actions   = true;    // журнал действий
bool                       g_physics   = false;   // журнал физики (CSV)
bool                       g_overlay   = false;   // показ журнала поверх сцены
bool                       g_headerCsv = false;
long long                  g_csvBytes  = 0;
const long long            kCsvMaxBytes = 16LL * 1024 * 1024;   // защита от переполнения диска
bool                       g_csvOverflow = false;
bool                       g_initDone  = false;
long long                  g_total     = 0;
long long                  g_errors    = 0;
std::string                g_dir;                 // папка с логами
std::string                g_exeDir;              // папка exe
std::string                g_logPath;
std::string                g_csvPath;
std::deque<std::string>    g_recent;              // кольцо последних строк
const std::size_t          kRecentMax = 5000;
std::vector<Fact>          g_facts;
DebugFactCallback          g_factCb = nullptr;
std::chrono::steady_clock::time_point g_t0;

const size_t kBufSize = 4096;

const char* levelTag(int level) {
    switch (level) {
        case DBG_TRACE:  return "TRACE";
        case DBG_INFO:   return "INFO ";
        case DBG_PHYS:   return "PHYS ";
        case DBG_ACTION: return "ACTION";
        case DBG_WARN:   return "WARN ";
        case DBG_ERROR:  return "ERROR";
        default:         return "?    ";
    }
}

std::string exeDirectory() {
#ifdef _WIN32
    char buf[MAX_PATH * 2];
    DWORD n = GetModuleFileNameA(NULL, buf, sizeof(buf) - 1);
    if (n == 0 || n >= sizeof(buf) - 1) return std::string();
    std::string p(buf, n);
#else
    char buf[8192];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n <= 0) return std::string();
    buf[n] = '\0';
    std::string p(buf);
#endif
    size_t pos = p.find_last_of("\\/");
    if (pos == std::string::npos) return std::string();
    return p.substr(0, pos);
}

std::string fallbackDirectory() {
#ifdef _WIN32
    const char* la = getenv("LOCALAPPDATA");
    if (la && *la) return std::string(la) + "\\AerosEngine";
    const char* tp = getenv("TEMP");
    if (tp && *tp) return std::string(tp) + "\\AerosEngine";
    return std::string();
#else
    const char* home = getenv("HOME");
    if (home && *home) return std::string(home) + "/.aeros";
    return std::string("/tmp/aeros");
#endif
}

bool makeOneDir(const std::string& d) {
    if (d.empty()) return false;
#ifdef _WIN32
    DWORD attrs = GetFileAttributesA(d.c_str());
    if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY)) return true;
    return CreateDirectoryA(d.c_str(), NULL) != 0;
#else
    struct stat st;
    if (stat(d.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) return true;
    return mkdir(d.c_str(), 0755) == 0;
#endif
}

// Создаёт папку и, при необходимости, её родителя (запасной путь ~/.aeros/logs)
bool ensureDir(const std::string& d) {
    if (d.empty()) return false;
    if (makeOneDir(d)) return true;
    size_t pos = d.find_last_of("\\/");
    if (pos == std::string::npos || pos == 0) return false;
    if (!makeOneDir(d.substr(0, pos))) return false;
    return makeOneDir(d);
}

std::string joinPath(const std::string& dir, const std::string& name) {
    if (dir.empty()) return name;
    char last = dir[dir.size() - 1];
    if (last == '\\' || last == '/') return dir + name;
#ifdef _WIN32
    return dir + "\\" + name;
#else
    return dir + "/" + name;
#endif
}

void timeStamp(char* out, size_t outSize) {
    using namespace std::chrono;
    system_clock::time_point nowSys = system_clock::now();
    std::time_t tt = system_clock::to_time_t(nowSys);
    std::tm tmLocal;
#ifdef _WIN32
    localtime_s(&tmLocal, &tt);
#else
    localtime_r(&tt, &tmLocal);
#endif
    int ms = (int)(duration_cast<milliseconds>(nowSys.time_since_epoch()).count() % 1000);
    std::snprintf(out, outSize, "%02d:%02d:%02d.%03d", tmLocal.tm_hour, tmLocal.tm_min, tmLocal.tm_sec, ms);
}

double secondsSinceStart() {
    using namespace std::chrono;
    return duration_cast<microseconds>(steady_clock::now() - g_t0).count() / 1000000.0;
}

std::string dateTimeStamp() {
    std::time_t tt = std::time(nullptr);
    std::tm tmLocal;
#ifdef _WIN32
    localtime_s(&tmLocal, &tt);
#else
    localtime_r(&tt, &tmLocal);
#endif
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%04d%02d%02d-%02d%02d%02d",
                  tmLocal.tm_year + 1900, tmLocal.tm_mon + 1, tmLocal.tm_mday,
                  tmLocal.tm_hour, tmLocal.tm_min, tmLocal.tm_sec);
    return std::string(buf);
}

void writeLineLocked(int level, const std::string& text) {
    char ts[32];
    timeStamp(ts, sizeof(ts));
    char head[64];
    std::snprintf(head, sizeof(head), "[%s][%s][%8.3fs] ", ts, levelTag(level), secondsSinceStart());
    std::string line = std::string(head) + text;

    g_recent.push_back(line);
    while (g_recent.size() > kRecentMax) g_recent.pop_front();
    g_total++;
    if (level == DBG_ERROR) g_errors++;

    if (g_logFile) {
        std::fputs(line.c_str(), g_logFile);
        std::fputc('\n', g_logFile);
        std::fflush(g_logFile);   // журнал не должен обрываться при падении
    }
}

bool levelPasses(int level) {
    // Ошибки и предупреждения пишем всегда — иначе пользователь не сможет
    // прислать причину сбоя, не догадавшись заранее нажать F9.
    if (level >= DBG_WARN) return true;
    if (level == DBG_INFO) return true;
    return g_enabled;
}

int vformat(char* buf, size_t n, const char* fmt, va_list ap) {
    int r = std::vsnprintf(buf, n, fmt, ap);
    if (r < 0) { std::snprintf(buf, n, "<ошибка форматирования>"); return 0; }
    if ((size_t)r >= n) { buf[n - 1] = '\0'; buf[n - 4] = '.'; buf[n - 3] = '.'; buf[n - 2] = '.'; }
    return r;
}

// ---------- факты о системе ----------
void cpuFacts() {
#ifdef AEROS_HAVE_CPUID
    int regs[4] = {0, 0, 0, 0};
#if defined(_MSC_VER)
    __cpuid(regs, 1);
#else
    __get_cpuid(1, (unsigned*)&regs[0], (unsigned*)&regs[1], (unsigned*)&regs[2], (unsigned*)&regs[3]);
#endif
    bool sse2 = (regs[3] & (1 << 26)) != 0;
    bool avx  = (regs[2] & (1 << 28)) != 0;
    bool fma  = (regs[2] & (1 << 12)) != 0;
    bool avx2 = false;
    int regs7[4] = {0, 0, 0, 0};
#if defined(_MSC_VER)
    __cpuidex(regs7, 7, 0);
#else
    __get_cpuid_count(7, 0, (unsigned*)&regs7[0], (unsigned*)&regs7[1], (unsigned*)&regs7[2], (unsigned*)&regs7[3]);
#endif
    avx2 = (regs7[1] & (1 << 5)) != 0;

    char brand[64] = {0};
    int b[4] = {0, 0, 0, 0};
    size_t pos = 0;
    for (unsigned leaf = 0x80000002u; leaf <= 0x80000004u; ++leaf) {
#if defined(_MSC_VER)
        __cpuid(b, (int)leaf);
#else
        __get_cpuid(leaf, (unsigned*)&b[0], (unsigned*)&b[1], (unsigned*)&b[2], (unsigned*)&b[3]);
#endif
        for (int k = 0; k < 4 && pos + 4 < sizeof(brand); ++k) {
            std::memcpy(brand + pos, &b[k], 4);
            pos += 4;
        }
    }
    brand[sizeof(brand) - 1] = '\0';
    std::string model(brand);
    while (!model.empty() && (model[model.size() - 1] == ' ' || model[model.size() - 1] == '\0')) model.erase(model.size() - 1);

    debugLogAddFact("Система", "CPU", model.empty() ? std::string("x86/x64 (имя не получено)") : model);
    char feats[160];
    std::snprintf(feats, sizeof(feats), "SSE2=%s AVX=%s AVX2=%s FMA=%s",
                  sse2 ? "да" : "нет", avx ? "да" : "нет", avx2 ? "да" : "нет", fma ? "да" : "нет");
    debugLogAddFact("Система", "Наборы инструкций CPU", feats);
#else
    debugLogAddFact("Система", "Наборы инструкций CPU", "не x86 (ARM64): cpuid недоступен");
#endif

#ifdef __AVX2__
    debugLogAddFact("Сборка", "Флаги компилятора", "бинарь собран с AVX2");
#elif defined(__SSE2__)
    debugLogAddFact("Сборка", "Флаги компилятора", "бинарь собран с SSE2 (базовый набор)");
#elif defined(_MSC_VER)
    debugLogAddFact("Сборка", "Флаги компилятора", "MSVC: базовый набор x64 = SSE2, x86 = /arch:SSE2");
#endif
}

void osFacts() {
#ifdef _WIN32
    SYSTEM_INFO si;
    GetNativeSystemInfo(&si);
    const char* arch = "неизвестно";
    switch (si.wProcessorArchitecture) {
        case PROCESSOR_ARCHITECTURE_AMD64: arch = "x64 (AMD64)"; break;
        case PROCESSOR_ARCHITECTURE_INTEL: arch = "x86 (32-bit)"; break;
#ifdef PROCESSOR_ARCHITECTURE_ARM64
        case PROCESSOR_ARCHITECTURE_ARM64: arch = "ARM64"; break;
#endif
        case PROCESSOR_ARCHITECTURE_ARM:   arch = "ARM32"; break;
        default: break;
    }
    debugLogAddFact("Система", "Архитектура Windows", arch);
    debugLogAddFact("Система", "Логических процессоров", std::to_string((int)si.dwNumberOfProcessors));

    HMODULE ntdll = GetModuleHandleA("ntdll.dll");
    if (ntdll) {
        typedef LONG (WINAPI *RtlGetVersionFn)(OSVERSIONINFOW*);
        RtlGetVersionFn fn = (RtlGetVersionFn)GetProcAddress(ntdll, "RtlGetVersion");
        OSVERSIONINFOW vi;
        std::memset(&vi, 0, sizeof(vi));
        vi.dwOSVersionInfoSize = sizeof(vi);
        if (fn && fn(&vi) == 0) {
            char buf[96];
            std::snprintf(buf, sizeof(buf), "Windows %lu.%lu build %lu",
                          (unsigned long)vi.dwMajorVersion, (unsigned long)vi.dwMinorVersion,
                          (unsigned long)vi.dwBuildNumber);
            debugLogAddFact("Система", "ОС", buf);
        }
    }

    MEMORYSTATUSEX ms;
    ms.dwLength = sizeof(ms);
    if (GlobalMemoryStatusEx(&ms)) {
        char buf[96];
        std::snprintf(buf, sizeof(buf), "всего %llu МБ, доступно %llu МБ",
                      (unsigned long long)(ms.ullTotalPhys / (1024 * 1024)),
                      (unsigned long long)(ms.ullAvailPhys / (1024 * 1024)));
        debugLogAddFact("Система", "Память", buf);
    }
#else
    struct utsname un;
    if (uname(&un) == 0) {
        debugLogAddFact("Система", "ОС", std::string(un.sysname) + " " + un.release);
        debugLogAddFact("Система", "Архитектура", un.machine);
    }
#endif
}

// PE-заголовок собственного exe: доказывает, какой именно бинарь запущен
void selfBinaryFacts() {
#ifdef _WIN32
    char path[MAX_PATH * 2] = {0};
    DWORD n = GetModuleFileNameA(NULL, path, sizeof(path) - 1);
    if (n == 0) return;
    debugLogAddFact("Сборка", "Файл программы", path);
    FILE* f = std::fopen(path, "rb");
    if (!f) return;
    unsigned char hdr[1024];
    size_t got = std::fread(hdr, 1, sizeof(hdr), f);
    std::fclose(f);
    if (got < 64 || hdr[0] != 'M' || hdr[1] != 'Z') return;
    int pe = (int)hdr[60] | ((int)hdr[61] << 8) | ((int)hdr[62] << 16) | ((int)hdr[63] << 24);
    if (pe <= 0 || pe + 6 >= (int)got) return;
    unsigned machine = (unsigned)hdr[pe + 4] | ((unsigned)hdr[pe + 5] << 8);
    char buf[128];
    std::snprintf(buf, sizeof(buf), "PE machine = %04X (%s)", machine,
                  machine == 0x8664 ? "x64" : (machine == 0x014C ? "x86" : (machine == 0xAA64 ? "ARM64" : "другая")));
    debugLogAddFact("Сборка", "Архитектура exe", buf);
#endif
}

} // namespace

// ======================= публичное API =======================

void debugLogInit(const std::string& dirPath) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_t0 = std::chrono::steady_clock::now();

    if (g_logFile) { std::fclose(g_logFile); g_logFile = nullptr; }
    if (g_csvFile) { std::fclose(g_csvFile); g_csvFile = nullptr; }
    g_headerCsv = false;

    g_exeDir = dirPath.empty() ? exeDirectory() : dirPath;

    // Пробуем папку программы, затем запасную (Program Files часто только для чтения)
    std::string candidates[3];
    candidates[0] = joinPath(g_exeDir, "logs");
    candidates[1] = joinPath(fallbackDirectory(), "logs");
    candidates[2] = fallbackDirectory();

    for (int i = 0; i < 3; ++i) {
        if (candidates[i].empty()) continue;
        if (!ensureDir(candidates[i])) continue;
        std::string logPath = joinPath(candidates[i], "aeros-debug.log");
        FILE* f = std::fopen(logPath.c_str(), "ab");
        if (!f) continue;
        g_dir     = candidates[i];
        g_logFile = f;
        g_logPath = logPath;
        g_csvPath = joinPath(candidates[i], "physics.csv");
        break;
    }

    g_initDone = true;
    g_csvOverflow = false;
    g_csvBytes = 0;
    g_recent.clear();
    g_total = 0;
    g_errors = 0;

    if (!g_logFile) {
        // Журнал не открылся — хотя бы в консоль
        std::fprintf(stderr, "[debug] не удалось открыть файл журнала\n");
        return;
    }
    std::fputs("\n========== НОВЫЙ ЗАПУСК ==========\n", g_logFile);
    std::fflush(g_logFile);
}

void debugLogClose() {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_csvFile) { std::fclose(g_csvFile); g_csvFile = nullptr; }
    if (g_logFile) { std::fflush(g_logFile); std::fclose(g_logFile); g_logFile = nullptr; }
}

bool debugLogReady() { return g_logFile != nullptr; }

bool debugLogEnabled()        { return g_enabled; }
void debugLogSetEnabled(bool on) {
    g_enabled = on;
    debugLogWrite(DBG_INFO, "режим отладки %s (журнал: %s)", on ? "ВКЛЮЧЁН" : "выключен",
                  g_logPath.empty() ? "не открыт" : g_logPath.c_str());
}
bool debugLogActionsEnabled() { return g_actions; }
void debugLogSetActions(bool on) { g_actions = on; }
bool debugLogPhysicsEnabled() { return g_physics; }
void debugLogSetPhysics(bool on) {
    if (on == g_physics) return;
    g_physics = on;
    if (!on) {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (g_csvFile) { std::fclose(g_csvFile); g_csvFile = nullptr; }
    }
    debugLogWrite(DBG_INFO, "журнал физики (physics.csv) %s", on ? "ВКЛЮЧЁН" : "выключен");
}
bool debugLogOverlayEnabled() { return g_overlay; }
void debugLogSetOverlay(bool on) { g_overlay = on; }

void debugLogWrite(int level, const char* fmt, ...) {
    if (!fmt) return;
    char buf[kBufSize];
    va_list ap;
    va_start(ap, fmt);
    vformat(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_initDone) {
        // До инициализации пишем в консоль, чтобы ничего не потерять
        std::fprintf(stderr, "[%s] %s\n", levelTag(level), buf);
        return;
    }
    if (!levelPasses(level)) return;
    writeLineLocked(level, buf);
}

void debugLogAction(const char* what, const char* fmt, ...) {
    if (!g_actions) return;
    char buf[kBufSize];
    va_list ap;
    va_start(ap, fmt);
    vformat(buf, sizeof(buf), fmt ? fmt : "", ap);
    va_end(ap);

    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_initDone) return;
    if (!g_enabled) return;   // журнал действий пишем только в режиме отладки
    char line[kBufSize + 128];
    std::snprintf(line, sizeof(line), "%s: %s", what ? what : "?", buf);
    writeLineLocked(DBG_ACTION, line);
}

// Открывает physics.csv на добавление; заголовок пишем только в пустой файл,
// чтобы данные предыдущих сессий не терялись.
bool openCsvLocked() {
    if (g_csvFile) return true;
    if (g_csvPath.empty()) return false;
    g_csvFile = std::fopen(g_csvPath.c_str(), "ab");
    if (!g_csvFile) return false;
    std::fseek(g_csvFile, 0, SEEK_END);
    long sz = std::ftell(g_csvFile);
    g_csvBytes = (sz > 0) ? (long long)sz : 0;
    g_headerCsv = (sz > 0);   // файл уже существует — заголовок в нём есть
    return true;
}

void debugLogPhysicsHeader(const char* commaSeparatedColumns) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_physics || !commaSeparatedColumns) return;
    if (!openCsvLocked() || g_headerCsv) return;
    int w = std::fprintf(g_csvFile, "%s\n", commaSeparatedColumns);
    std::fflush(g_csvFile);
    if (w > 0) g_csvBytes += w;
    g_headerCsv = true;
}

void debugLogPhysicsRow(const char* fmt, ...) {
    if (!g_physics || !fmt) return;
    char buf[kBufSize];
    va_list ap;
    va_start(ap, fmt);
    vformat(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_csvOverflow) return;
    if (!openCsvLocked()) return;
    if (g_csvBytes > kCsvMaxBytes) {
        g_csvOverflow = true;
        writeLineLocked(DBG_WARN, "physics.csv достиг 16 МБ — запись журнала физики остановлена");
        return;
    }
    int w = std::fprintf(g_csvFile, "%s\n", buf);
    std::fflush(g_csvFile);
    if (w > 0) g_csvBytes += w;
}

void debugLogAddFact(const std::string& category, const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> lock(g_mutex);
    for (size_t i = 0; i < g_facts.size(); ++i) {
        if (g_facts[i].category == category && g_facts[i].key == key) {
            g_facts[i].value = value;
            return;
        }
    }
    Fact f; f.category = category; f.key = key; f.value = value;
    g_facts.push_back(f);
}

void debugLogSetFactCallback(DebugFactCallback cb) { g_factCb = cb; }

int debugLogRecentCount() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return (int)g_recent.size();
}

std::string debugLogRecent(int indexFromNewest) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (indexFromNewest < 0 || g_recent.empty()) return std::string();
    size_t n = g_recent.size();
    if ((size_t)indexFromNewest >= n) indexFromNewest = (int)n - 1;
    return g_recent[n - 1 - (size_t)indexFromNewest];
}

long long debugLogTotalCount() { std::lock_guard<std::mutex> lock(g_mutex); return g_total; }
int       debugLogErrorCount() { std::lock_guard<std::mutex> lock(g_mutex); return (int)g_errors; }
void      debugLogClearErrors() { std::lock_guard<std::mutex> lock(g_mutex); g_errors = 0; }

std::string debugLogDir()             { return g_dir; }
std::string debugLogFilePath()        { return g_logPath; }
std::string debugLogPhysicsFilePath() { return g_csvPath; }

int debugCountNonFinite(const float* data, std::size_t count, std::size_t stride) {
    if (!data || count == 0) return 0;
    if (stride == 0) stride = 1;
    int bad = 0;
    for (std::size_t i = 0; i < count; i += stride) {
        float v = data[i];
        if (!std::isfinite(v)) bad++;
    }
    return bad;
}

std::string debugLogBuildReport() {
    if (g_factCb) g_factCb();          // собрать свежие параметры
    cpuFacts();
    osFacts();
    selfBinaryFacts();
    debugLogAddFact("Журнал", "Файл журнала", g_logPath.empty() ? std::string("не открыт") : g_logPath);
    debugLogAddFact("Журнал", "Файл физики", g_csvPath.empty() ? std::string("не создавался") : g_csvPath);
    debugLogAddFact("Журнал", "Всего записей", std::to_string((long long)g_total));
    debugLogAddFact("Журнал", "Ошибок в журнале", std::to_string((long long)g_errors));
    debugLogAddFact("Журнал", "Режим отладки", g_enabled ? "включён" : "выключен");
    debugLogAddFact("Журнал", "Журнал действий", g_actions ? "включён" : "выключен");
    debugLogAddFact("Журнал", "Журнал физики", g_physics ? "включён" : "выключен");

    std::string out;
    out.reserve(64 * 1024);
    out += "=== Aeros Engine - ОТЛАДОЧНЫЙ ОТЧЁТ ===\n";
    out += "Сформирован: " + dateTimeStamp() + "\n";
    out += "Как пользоваться: пришлите этот файл разработчику вместе с описанием проблемы.\n\n";

    std::lock_guard<std::mutex> lock(g_mutex);
    std::string lastCat;
    for (size_t i = 0; i < g_facts.size(); ++i) {
        if (g_facts[i].category != lastCat) {
            lastCat = g_facts[i].category;
            out += "--- " + lastCat + " ---\n";
        }
        out += "  " + g_facts[i].key + ": " + g_facts[i].value + "\n";
    }

    out += "\n--- Последние записи журнала (до 400) ---\n";
    size_t start = g_recent.size() > 400 ? g_recent.size() - 400 : 0;
    for (size_t i = start; i < g_recent.size(); ++i) {
        out += g_recent[i];
        out += "\n";
    }
    out += "=== КОНЕЦ ОТЧЁТА ===\n";
    return out;
}

std::string debugLogSaveReport() {
    std::string text = debugLogBuildReport();
    std::string name = "Aeros-Debug-Report-" + dateTimeStamp() + ".txt";

    std::string places[3];
    places[0] = g_exeDir;
    places[1] = g_dir;
    places[2] = fallbackDirectory();
    for (int i = 0; i < 3; ++i) {
        if (places[i].empty()) continue;
        ensureDir(places[i]);
        std::string path = joinPath(places[i], name);
        FILE* f = std::fopen(path.c_str(), "wb");
        if (!f) continue;
        std::fwrite(text.data(), 1, text.size(), f);
        std::fclose(f);
        debugLogWrite(DBG_INFO, "отладочный отчёт сохранён: %s", path.c_str());
        return path;
    }
    debugLogWrite(DBG_ERROR, "не удалось сохранить отладочный отчёт ни в одну папку");
    return std::string();
}
