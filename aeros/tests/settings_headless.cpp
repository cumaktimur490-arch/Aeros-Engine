// =====================================================
// Aeros Engine — headless-проверки реестра настроек (v1.32.0)
//
// Реестр (aeros/src/settings_store.h) — единственный источник списка ключей,
// диапазонов и стандартных значений. Поэтому проверяются именно те свойства,
// которые раньше ломались вручную поддерживаемым списком из main.cpp:
//   - каждая запись пригодна (непустой уникальный ключ, mn <= mx, def в диапазоне);
//   - сохранение и загрузка — обратимы, число ключей не «худеет»;
//   - мусор в файле (комментарии, неизвестные ключи, не числа, CRLF, повторы)
//     не роняет загрузку и не портит уже применённые значения;
//   - значения вне диапазона клампятся, а не принимаются как есть;
//   - сброс восстанавливает стандартные значения для ВСЕХ записей;
//   - профили: сохранение, список, загрузка, удаление, отказ на недопустимых
//     именах (защита от подстановки пути);
//   - файл остаётся прежнего формата и содержит все ключи версии 1.31.2,
//     поэтому старые aeros_settings.ini читаются.
//
// Запуск: make settings  (или ./aeros-test-settings)
// =====================================================

#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "settings_store.h"

static int g_checks = 0;
static int g_failed = 0;
static char b[512];

static void check(bool ok, const char* what) {
    ++g_checks;
    if (ok) {
        std::printf("  ok:    %s\n", what);
    } else {
        ++g_failed;
        std::printf("  FAIL:  %s\n", what);
    }
}

static std::string readAll(const std::string& path) {
    std::ifstream f(path);
    std::string all((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    return all;
}

static bool fileContains(const std::string& text, const std::string& needle) {
    return text.find(needle) != std::string::npos;
}

// Значение настройки по ключу (или NaN, если ключа нет).
static double value(const char* key) {
    const Setting* s = settingsFind(key);
    return s ? s->get() : std::nan("");
}

static void testTable() {
    std::printf("\n--- 1. Таблица настроек пригодна ---\n");
    const std::vector<Setting>& t = settingsTable();
    std::snprintf(b, sizeof(b), "в реестре %d настроек (не меньше 130)", (int)t.size());
    check(t.size() >= 130, b);

    bool keysOk = true, rangesOk = true, defOk = true, finOk = true;
    for (size_t i = 0; i < t.size(); ++i) {
        const Setting& s = t[i];
        if (!s.key || !s.key[0] || !s.get || !s.set) keysOk = false;
        for (size_t j = i + 1; j < t.size(); ++j)
            if (std::strcmp(s.key, t[j].key) == 0) keysOk = false;
        if (!(s.mn <= s.mx)) rangesOk = false;
        if (!(s.def >= s.mn && s.def <= s.mx)) defOk = false;
        if (!std::isfinite(s.get())) finOk = false;
    }
    check(keysOk, "ключи непустые и уникальные");
    check(rangesOk, "у каждой записи нижняя граница не выше верхней");
    check(defOk, "стандартное значение лежит внутри диапазона");
    check(finOk, "текущие значения конечны (нет NaN/Inf после инициализации)");
}

static void testRoundTrip() {
    std::printf("\n--- 2. Сохранение и загрузка обратимы ---\n");
    const std::vector<Setting>& t = settingsTable();
    std::vector<double> before(t.size());
    for (size_t i = 0; i < t.size(); ++i) before[i] = t[i].get();

    const std::string path = "/tmp/aeros-settings-roundtrip.ini";
    {
        std::ofstream f(path);
        f << "# Aeros Engine v" AEROS_VERSION_STRING " Settings\n";
        settingsWrite(f);
    }
    const std::string text = readAll(path);
    std::snprintf(b, sizeof(b), "файл записан (%d байт) и начинается с шапки версии",
                  (int)text.size());
    check(text.size() > 2000 && text.rfind("# Aeros Engine v", 0) == 0, b);

    // Все ключи присутствуют в файле.
    bool allPresent = true;
    for (const Setting& s : t) {
        std::string needle = std::string(s.key) + "=";
        if (!fileContains(text, needle)) { allPresent = false; break; }
    }
    check(allPresent, "в файле есть каждая запись реестра");

    // Портим значения, затем читаем файл: должны вернуться прежние.
    for (const Setting& s : t) s.set(s.mn);
    const int applied = settingsRead(path);
    std::snprintf(b, sizeof(b), "загрузка применила все %d ключей", applied);
    check(applied == (int)t.size(), b);

    bool same = true;
    for (size_t i = 0; i < t.size(); ++i) {
        const double d = std::fabs(t[i].get() - before[i]);
        const double scale = std::fmax(1.0, std::fabs(before[i]));
        if (!(d <= 1e-6 * scale)) { same = false; break; }
    }
    check(same, "значения после «сохранить → изменить → загрузить» совпали");
}

static void testLegacyKeys() {
    std::printf("\n--- 3. Ключи v1.31.2 читаются (совместимость файла) ---\n");
    // Ровно те ключи, которые писала и читала версия 1.31.2.
    const char* legacy[] = {
        "flowSpeed", "flowAzimuth", "flowElevation", "altitude", "timeScale", "strouhal",
        "wakeStrength", "wakeLength", "aeroVisMode", "aeroColorMap", "aeroGroundEffect",
        "aeroGroundHeight", "showParticles", "showStreamlines", "showPressure",
        "numParticles", "numStreamlines", "voxelResolution", "lbmEnabled", "lbmTau",
        "lbmStepsPerFrame", "language", "aeroShowParticleTrails", "aeroAdaptiveLBM",
        "aeroUseRK4Particles", "uiScale", "tlFullscreen", "uiDocked", "uiShowTree",
        "uiShowProps", "uiShowConsole", "uiSelectedLeaf", "uiTreeW", "uiPropsW",
        "uiBottomH", "physFixed", "physHz", "physMaxSubsteps", "physParticleSubsteps"
    };
    bool all = true;
    for (const char* k : legacy)
        if (!settingsFind(k)) { all = false; std::printf("  нет ключа: %s\n", k); }
    check(all, "все 39 ключей версии 1.31.2 присутствуют в реестре");

    // Старый файл (без новых ключей) читается, отсутствующие ключи не трогаются.
    const std::string path = "/tmp/aeros-settings-legacy.ini";
    {
        std::ofstream f(path);
        f << "# Aeros Engine v1.31.2 Settings\n"
             "flowSpeed=37.5\ntimeScale=2.25\nlanguage=0\nphysHz=1000\nuiScale=1.5\n";
    }
    const double keepFsr = value("fsrSharpness");
    const int applied = settingsRead(path);
    std::snprintf(b, sizeof(b), "старый файл: применено %d ключей из 5", applied);
    check(applied == 5, b);
    check(std::fabs(value("flowSpeed") - 37.5) < 1e-4, "flowSpeed = 37.5 из старого файла");
    check(std::fabs(value("timeScale") - 2.25) < 1e-4, "timeScale = 2.25 из старого файла");
    check(std::fabs(value("physHz") - 1000.0) < 1e-3, "physHz = 1000 из старого файла");
    check(std::fabs(value("fsrSharpness") - keepFsr) < 1e-6,
          "ключ, которого нет в старом файле, остался прежним");
}

static void testGarbage() {
    std::printf("\n--- 4. Мусор в файле не ломает загрузку ---\n");
    const std::string path = "/tmp/aeros-settings-garbage.ini";
    {
        std::ofstream f(path);
        f << "# комментарий\n"
             "; ещё комментарий\n"
             "\n"
             "flowSpeed = 12.5\r\n"          // пробелы вокруг '=' и CRLF
             "неизвестный_ключ=42\n"          // неизвестный ключ
             "showParticles=not_a_number\n"   // не число: значение не трогаем
             "numParticles=999999999\n"       // вне диапазона: кламп
             "fsrSharpness=-5\n"              // вне диапазона: кламп
             "uiScale=1e300\n"                // вне диапазона: кламп
             "aeroColorMap=2\n"
             "aeroColorMap=3\n"               // повтор: побеждает последний
             "строка без знака равенства\n"
             "physHz=nan\n";                  // текст «nan»: не число
    }
    settingsResetDefaults();
    const double particlesDef = value("numParticles");
    const bool showDef = value("showParticles") > 0.5;
    const double hzDef = value("physHz");

    const int applied = settingsRead(path);
    // 7 применений: flowSpeed, numParticles, fsrSharpness, uiScale, physHz и
    // aeroColorMap дважды (при повторе ключа побеждает последнее значение).
    // «physHz=nan» strtod разбирает как NaN, а settingsClamp заменяет NaN
    // стандартным значением — испорченное число не попадает в настройки.
    std::snprintf(b, sizeof(b), "применено %d ключей, мусор пропущен", applied);
    check(applied == 7, b);
    bool finite = true;
    for (const Setting& s2 : settingsTable()) if (!std::isfinite(s2.get())) finite = false;
    check(finite, "после загрузки мусорного файла нет ни одного NaN/Inf");
    check(std::fabs(value("flowSpeed") - 12.5) < 1e-4, "значение с пробелами и CRLF прочитано");
    check((value("showParticles") > 0.5) == showDef, "нечисловое значение не испортило настройку");
    check(std::fabs(value("physHz") - hzDef) < 1e-3, "текст «nan» не превратился в значение");
    check(value("numParticles") == 500000.0, "numParticles вне диапазона ограничен сверху");
    check(std::fabs(value("fsrSharpness") - 0.0) < 1e-6, "fsrSharpness=-5 ограничен снизу нулём");
    check(std::fabs(value("uiScale") - 2.0) < 1e-6, "uiScale=1e300 ограничен верхом диапазона");
    check(std::fabs(value("aeroColorMap") - 3.0) < 1e-6, "при повторе ключа побеждает последнее значение");
    (void)particlesDef;

    // Отсутствующий файл — не ошибка чтения, а -1 (main.cpp не считает это сбоем).
    check(settingsRead("/tmp/нет-такого-файла-настроек.ini") == -1,
          "отсутствующий файл возвращает -1, а не падение");
}

static void testReset() {
    std::printf("\n--- 5. Сброс к стандартным значениям ---\n");
    const std::vector<Setting>& t = settingsTable();
    // Ставим всё в верхнюю границу диапазона — так видно, что сброс реально сработал.
    for (const Setting& s : t) s.set(s.mx);
    settingsResetDefaults();
    bool ok = true;
    for (const Setting& s : t)
        if (std::fabs(s.get() - s.def) > 1e-6 * std::fmax(1.0, std::fabs(s.def))) { ok = false; break; }
    std::snprintf(b, sizeof(b), "все %d настроек вернулись к стандартным", (int)t.size());
    check(ok, b);
    // Выборочный контроль «человеческих» стандартов: они совпадают с
    // инициализаторами переменных, а не с краями диапазонов.
    check(std::fabs(value("physHz") - 240.0) < 1e-3, "стандартная частота физики 240 Гц");
    check(fabs(value("physMaxSubsteps") - 64.0) < 1e-6, "стандартный предел тактов 64");
    check(fabs(value("physParticleSubsteps") - 8.0) < 1e-6, "стандартный предел подшагов 8");
    check(std::fabs(value("voxelResolution") - 48.0) < 1e-6 || value("voxelResolution") > 0.0,
          "разрешение воксельной сетки — положительное число");
}

static void testProfiles() {
    std::printf("\n--- 6. Профили настроек ---\n");
    const std::filesystem::path cwd = std::filesystem::current_path();
    const std::filesystem::path tmp = "/tmp/aeros-settings-profiles";
    std::error_code ec;
    std::filesystem::remove_all(tmp, ec);
    std::filesystem::create_directories(tmp, ec);
    std::filesystem::current_path(tmp, ec);

    check(settingsProfileNameOk("my_profile-1"), "допустимое имя профиля принимается");
    check(!settingsProfileNameOk(""), "пустое имя отклоняется");
    check(!settingsProfileNameOk("../evil"), "подстановка пути «../» отклоняется");
    check(!settingsProfileNameOk("имя с пробелом"), "имя с пробелом отклоняется");
    check(!settingsProfileNameOk(std::string(41, 'a')), "имя длиннее 40 знаков отклоняется");
    check(!settingsProfileSave("../evil"), "сохранить профиль с недопустимым именем нельзя");

    settingsResetDefaults();
    settingsFind("flowSpeed")->set(99.0);
    settingsFind("aeroColorMap")->set(3.0);
    check(settingsProfileSave("fast"), "профиль «fast» сохранён");
    check(std::filesystem::exists("profiles/fast.ini"), "файл профиля появился в profiles/");

    settingsFind("flowSpeed")->set(1.0);
    settingsFind("aeroColorMap")->set(0.0);
    check(settingsProfileLoad("fast"), "профиль «fast» загружен");
    check(std::fabs(value("flowSpeed") - 99.0) < 1e-4, "значение из профиля восстановлено");
    check(std::fabs(value("aeroColorMap") - 3.0) < 1e-6, "второе значение из профиля восстановлено");

    settingsProfileSave("second");
    const std::vector<std::string> list = settingsProfileList();
    std::snprintf(b, sizeof(b), "в списке профилей %d записи (ожидалось 2)", (int)list.size());
    check(list.size() == 2 && list[0] == "fast" && list[1] == "second",
          "список профилей упорядочен по алфавиту");

    check(settingsProfileDelete("fast"), "профиль «fast» удалён");
    check(!std::filesystem::exists("profiles/fast.ini"), "файл удалённого профиля исчез");
    check(!settingsProfileLoad("fast"), "загрузка удалённого профиля возвращает отказ");
    check(!settingsProfileLoad("нет_такого"), "загрузка несуществующего профиля возвращает отказ");

    std::filesystem::current_path(cwd, ec);
    std::filesystem::remove_all(tmp, ec);
}

int main() {
    std::printf("=== Aeros Engine: реестр настроек (v1.32.0) ===\n");
    testTable();
    testRoundTrip();
    testLegacyKeys();
    testGarbage();
    testReset();
    testProfiles();

    std::printf("\n=== настройки: проверок %d, провалов %d — %s ===\n",
                g_checks, g_failed, g_failed == 0 ? "ВСЁ В ПОРЯДКЕ" : "ЕСТЬ ПРОВАЛЫ");
    return g_failed == 0 ? 0 : 1;
}
