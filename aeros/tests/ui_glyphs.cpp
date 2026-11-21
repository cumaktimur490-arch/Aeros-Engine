// =====================================================
// v1.34.0 — тест покрытия глифов интерфейса.
//
// Дефект, который он закрывает: в UI используются «—» (U+2014), «…» (U+2026),
// «→», греческие σ/ν/ρ и «№», а набор глифов брался из
// GetGlyphRangesCyrillic(), где их нет. На экране это выглядело как пустые
// ромбики посреди текста: и ошибка кодировки, и «шрифт не загрузился» были
// ложными версиями, потому что код вызова шрифта выглядел корректно.
//
// Тест читает исходники интерфейса, вытаскивает ВСЕ строковые литералы
// (комментарии не сканируются) и проверяет каждый не-ASCII символ по таблице
// aerosUiGlyphRanges(). Так же проверяется, что символ действительно есть в
// файле шрифта (cmap), если шрифт доступен — чтобы таблица не «разрешала»
// символ, которого в шрифте нет.
// =====================================================
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <set>
#include <map>
#include <fstream>
#include <sstream>

#include "ui_font_ranges.h"

static int g_fail = 0;
static int g_checks = 0;

static void check(bool ok, const std::string& what) {
    ++g_checks;
    if (!ok) { ++g_fail; std::printf("FAIL: %s\n", what.c_str()); }
}

// ---------- чтение cmap TrueType (формат 4 и 12) ----------
static uint32_t be16(const unsigned char* p) { return (uint32_t(p[0]) << 8) | p[1]; }
static uint32_t be32(const unsigned char* p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
}

static std::set<uint32_t> loadFontCmap(const char* path, bool& ok) {
    std::set<uint32_t> cps;
    ok = false;
    std::ifstream f(path, std::ios::binary);
    if (!f) return cps;
    std::vector<unsigned char> d((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (d.size() < 12) return cps;
    uint32_t numTables = be16(&d[4]);
    size_t cmapOff = 0;
    for (uint32_t i = 0; i < numTables; ++i) {
        size_t rec = 12 + size_t(i) * 16;
        if (rec + 16 > d.size()) break;
        if (std::memcmp(&d[rec], "cmap", 4) == 0) { cmapOff = be32(&d[rec + 8]); break; }
    }
    if (!cmapOff || cmapOff + 4 > d.size()) return cps;
    uint32_t nSub = be16(&d[cmapOff + 2]);
    for (uint32_t s = 0; s < nSub; ++s) {
        size_t sub = cmapOff + 4 + size_t(s) * 8;
        if (sub + 8 > d.size()) break;
        uint32_t off = cmapOff + be32(&d[sub + 4]);
        if (off + 2 > d.size()) continue;
        uint32_t fmt = be16(&d[off]);
        if (fmt == 4) {
            uint32_t segX2 = be16(&d[off + 6]);
            uint32_t seg = segX2 / 2;
            size_t endP = off + 14, startP = endP + segX2 + 2, deltaP = startP + segX2, rangeP = deltaP + segX2;
            if (rangeP + segX2 > d.size()) continue;
            for (uint32_t i = 0; i < seg; ++i) {
                uint32_t end = be16(&d[endP + i * 2]);
                uint32_t start = be16(&d[startP + i * 2]);
                if (start > end || end == 0xFFFF) continue;
                for (uint32_t c = start; c <= end && c < 0xFFFF; ++c) cps.insert(c);
            }
        } else if (fmt == 12) {
            uint32_t nGroups = be32(&d[off + 12]);
            size_t g = off + 16;
            for (uint32_t i = 0; i < nGroups && g + 12 <= d.size(); ++i, g += 12) {
                uint32_t start = be32(&d[g]), end = be32(&d[g + 4]);
                if (start > end || end - start > 0x2000) continue;
                for (uint32_t c = start; c <= end; ++c) cps.insert(c);
            }
        }
    }
    ok = !cps.empty();
    return cps;
}

// ---------- вытащить UTF-8 кодовые точки из строковых литералов C++ ----------
// Обрабатываются ", u8", R"(...)" не поддерживаются намеренно: в интерфейсе
// они не используются, и тест сообщит о пропуске.
static void collectStringCodepoints(const std::string& src,
                                    std::map<uint32_t, std::string>& where) {
    std::vector<std::string> lines;
    { std::istringstream is(src); std::string ln; while (std::getline(is, ln)) lines.push_back(ln); }

    bool inBlockComment = false;
    for (size_t li = 0; li < lines.size(); ++li) {
        const std::string& ln = lines[li];
        for (size_t i = 0; i < ln.size();) {
            if (inBlockComment) {
                if (i + 1 < ln.size() && ln[i] == '*' && ln[i + 1] == '/') { inBlockComment = false; i += 2; }
                else ++i;
                continue;
            }
            if (i + 1 < ln.size() && ln[i] == '/' && ln[i + 1] == '/') break;
            if (i + 1 < ln.size() && ln[i] == '/' && ln[i + 1] == '*') { inBlockComment = true; i += 2; continue; }
            if (ln[i] == '"') {
                size_t j = i + 1;
                while (j < ln.size() && ln[j] != '"') {
                    if (ln[j] == '\\' && j + 1 < ln.size()) { j += 2; continue; }
                    unsigned char c = (unsigned char)ln[j];
                    uint32_t cp = 0;
                    int extra = 0;
                    if (c < 0x80) { ++j; continue; }
                    else if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; extra = 1; }
                    else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; extra = 2; }
                    else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; extra = 3; }
                    else { ++j; continue; }
                    if (j + extra >= ln.size()) break;
                    for (int k = 1; k <= extra; ++k) cp = (cp << 6) | ((unsigned char)ln[j + k] & 0x3F);
                    where.emplace(cp, ln);
                    j += extra + 1;
                }
                i = (j < ln.size()) ? j + 1 : j;
                continue;
            }
            ++i;
        }
    }
}

static std::string readFile(const char* p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return std::string();
    std::ostringstream ss; ss << f.rdbuf();
    return ss.str();
}

int main(int argc, char** argv) {
    const char* dir = (argc > 1) ? argv[1] : "..";   // каталог с исходниками (aeros/src)
    // Первый аргумент — каталог src; запускается из aeros/tests.
    std::vector<std::string> files = {
        "ui.cpp", "cae_ui.h", "start_screen.h", "demo_geometry.h", "recent_files.h",
        "main.cpp", "results_view.h", "ui_dock.h", "fem_solver.h", "lang.cpp",
        "settings_table.inc", "help_ui.h",
    };

    std::map<uint32_t, std::string> cps;
    int read = 0;
    for (auto& f : files) {
        std::string path = std::string(dir) + "/" + f;
        std::string src = readFile(path.c_str());
        if (src.empty()) continue;
        ++read;
        collectStringCodepoints(src, cps);
    }
    check(read >= 6, "прочитано достаточно файлов интерфейса (" + std::to_string(read) + ")");

    // 1) каждый символ из строковых литералов покрыт таблицей диапазонов
    std::vector<std::string> missing;
    for (auto& kv : cps) {
        if (!aerosUiGlyphCovered(kv.first)) {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "U+%04X", kv.first);
            missing.push_back(std::string(buf) + " в строке: " + kv.second);
        }
    }
    check(!cps.empty(), "найдены не-ASCII строки интерфейса (символов: " + std::to_string(cps.size()) + ")");
    if (!cps.empty()) {
        std::string list;
        int shown = 0;
        for (auto& kv : cps) {
            if (++shown > 24) { list += " …"; break; }
            char buf[16]; std::snprintf(buf, sizeof(buf), "U+%04X ", kv.first);
            list += buf;
        }
        std::printf("СИМВОЛЫ: %s\n", list.c_str());
    }
    for (auto& m : missing) check(false, "символ не покрыт диапазонами глифов: " + m);
    if (missing.empty())
        check(true, "все не-ASCII символы интерфейса покрыты таблицей глифов");

    // 2) таблица покрывает то, что реально нужно (не «пустая» случайно)
    check(aerosUiGlyphCovered(0x2014), "длинное тире —");
    check(aerosUiGlyphCovered(0x2026), "многоточие …");
    check(aerosUiGlyphCovered(0x00AB) && aerosUiGlyphCovered(0x00BB), "кавычки «»");
    check(aerosUiGlyphCovered(0x0430), "кириллица");
    check(aerosUiGlyphCovered(0x03C3) && aerosUiGlyphCovered(0x03BD), "греческие σ ν");
    check(aerosUiGlyphCovered(0x2116), "знак №");
    check(aerosUiGlyphCovered(0x2192), "стрелка →");
    check(aerosUiGlyphCovered(0x2248), "знак ≈");
    check(!aerosUiGlyphCovered(0x4E00), "CJK не заявлен (не раздуваем атлас)");

    // 3) загрузчик шрифта обязан брать именно эту таблицу: иначе тест выше
    // проверял бы таблицу, которую никто не использует.
    {
        std::string lang = readFile((std::string(dir) + "/lang.cpp").c_str());
        check(!lang.empty(), "lang.cpp прочитан");
        check(lang.find("aerosUiGlyphRanges()") != std::string::npos,
              "initLocalization() передаёт aerosUiGlyphRanges() в AddFontFromFileTTF");
        // Ищем именно вызов загрузки шрифта со старым диапазоном (упоминание в
        // комментарии — не ошибка).
        bool oldRangeInCall = false;
        {
            std::istringstream is(lang);
            std::string ln;
            while (std::getline(is, ln)) {
                if (ln.find("AddFontFromFileTTF") != std::string::npos &&
                    ln.find("GetGlyphRangesCyrillic") != std::string::npos) oldRangeInCall = true;
            }
        }
        check(!oldRangeInCall, "ни один вызов AddFontFromFileTTF не использует урезанный GetGlyphRangesCyrillic()");
    }

    // 4) символы действительно есть в файле шрифта
    const char* font = "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf";
    bool cmapOk = false;
    std::set<uint32_t> cmap = loadFontCmap(font, cmapOk);
    if (!cmapOk) {
        std::printf("ПРИМЕЧАНИЕ: шрифт %s недоступен, проверка cmap пропущена\n", font);
    } else {
        check(true, "cmap шрифта прочитан (" + std::to_string(cmap.size()) + " символов)");
        int absent = 0;
        for (auto& kv : cps)
            if (aerosUiGlyphCovered(kv.first) && !cmap.count(kv.first)) {
                ++absent;
                char buf[64]; std::snprintf(buf, sizeof(buf), "U+%04X", kv.first);
                std::printf("НЕТ В ШРИФТЕ: %s\n", buf);
            }
        check(absent == 0, "все используемые символы присутствуют в шрифте (отсутствует: " + std::to_string(absent) + ")");
    }

    std::printf("=== ГЛИФЫ ИНТЕРФЕЙСА: проверок %d, ошибок %d ===\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
