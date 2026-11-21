// =====================================================
// v1.36.0 — headless-проверки open_url.h / proc_util.h.
//
// Что проверяется:
//   1) отказ на заведомо негодных аргументах (nullptr, пустая строка);
//   2) спецсимволы оболочки в URL НЕ исполняются: opener — обычная программа,
//      которая получает argv и записывает его в файл; после вызова проверяем,
//      что (а) посторонняя команда не выполнилась, (б) URL пришёл ОДНИМ
//      аргументом целиком;
//   3) отсутствующий opener даёт false — раньше system() со `&` возвращал 0
//      всегда, и функция «успешно» открывала ссылку при отсутствии xdg-open;
//   4) код возврата opener'а различается: 0 → true, 3 → false.
// =====================================================

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <sys/stat.h>
#include <unistd.h>

#include "open_url.h"
#include "proc_util.h"

static int g_checks = 0, g_failed = 0;
static void check(bool cond, const std::string& what) {
    ++g_checks;
    std::printf("  %s  %s\n", cond ? "ok:  " : "FAIL:", what.c_str());
    if (!cond) ++g_failed;
}

static std::string readFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return "";
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static bool exists(const std::string& path) {
    struct stat st;
    return ::stat(path.c_str(), &st) == 0;
}

int main() {
    std::printf("=== открытие ссылки без shell (v1.36.0) ===\n");

    char tmpl[] = "/tmp/aeros-openurl-XXXXXX";
    const char* dir = mkdtemp(tmpl);
    if (!dir) { std::printf("не удалось создать временный каталог\n"); return 1; }
    const std::string base(dir);
    const std::string argvFile = base + "/argv.txt";
    const std::string exit3File = base + "/fail3.sh";
    const std::string okFile = base + "/ok.sh";
    const std::string script = base + "/record.sh";
    const std::string pwned = base + "/pwned";

    // Скрипт-«браузер»: записывает каждый аргумент на своей строке.
    {
        std::ofstream f(script);
        f << "#!/bin/sh\n"
          << ": > '" << argvFile << "'\n"
          << "for a in \"$@\"; do printf '%s\\n' \"$a\" >> '" << argvFile << "'; done\n"
          << "exit 0\n";
    }
    ::chmod(script.c_str(), 0755);
    {
        std::ofstream f(okFile);
        f << "#!/bin/sh\nexit 0\n";
    }
    ::chmod(okFile.c_str(), 0755);
    {
        std::ofstream f(exit3File);
        f << "#!/bin/sh\nexit 3\n";
    }
    ::chmod(exit3File.c_str(), 0755);

    // ---------- 1. негодные аргументы ----------
    std::printf("\n--- отказ на негодных аргументах ---\n");
    check(!aerosOpenUrl(nullptr), "nullptr → false");
    check(!aerosOpenUrl(""), "пустая строка → false");
    check(!aerosOpenUrlWith(nullptr, "https://example.com"), "nullptr opener → false");
    check(!aerosOpenUrlWith(script.c_str(), nullptr), "nullptr url при годном opener → false");
    check(!aerosOpenUrlWith("", "https://example.com"), "пустой opener → false");

    std::printf("\n--- проверка самой ссылки (aerosUrlIsSafe) ---\n");
    check(aerosUrlIsSafe("https://github.com/cumaktimur490-arch/Aeros-Engine/releases/latest"),
          "обычная https-ссылка допустима");
    check(aerosUrlIsSafe(aerosReleasesUrl()), "ссылка релизов проходит проверку");
    check(!aerosUrlIsSafe("https://example.com/\nrm -rf /"), "перевод строки в URL отклоняется");
    check(!aerosUrlIsSafe("https://example.com/\t"), "табуляция отклоняется");
    check(!aerosUrlIsSafe("https://example.com/\x7f"), "DEL отклоняется");
    check(!aerosUrlIsSafe(""), "пустая ссылка отклоняется");
    const std::string longUrl = "https://example.com/" + std::string(5000, 'a');
    check(!aerosUrlIsSafe(longUrl.c_str()), "слишком длинная ссылка отклоняется");

    // ---------- 2. спецсимволы не исполняются ----------
    std::printf("\n--- спецсимволы оболочки не исполняются ---\n");
    ::remove(pwned.c_str());

    const std::string nasty =
        "https://example.com/a\"; touch " + pwned + "; echo \"";
    const bool opened = aerosOpenUrlWith(script.c_str(), nasty.c_str());
    check(opened, "opener запустился и завершился с кодом 0");
    check(!exists(pwned), "команда из URL не выполнилась (файл не создан)");

    const std::string recorded = readFile(argvFile);
    std::vector<std::string> args;
    {
        std::istringstream in(recorded);
        std::string line;
        while (std::getline(in, line)) args.push_back(line);
    }
    check(args.size() == 1, "opener получил ровно один аргумент (URL целиком)");
    check(!args.empty() && args[0] == nasty,
          "аргумент совпадает с URL побайтово, включая кавычки и ;");

    // Одиночная кавычка: раньше она ломала --filename='...' в zenity.
    ::remove(argvFile.c_str());
    const std::string quoteUrl = "https://example.com/it's;$(id)`id`";
    check(aerosOpenUrlWith(script.c_str(), quoteUrl.c_str()), "URL с апострофом и $() запускается");
    check(!exists(pwned), "подстановка $() не выполнена");
    check(readFile(argvFile) == quoteUrl + "\n",
          "апостроф, $() и обратные кавычки дошли как текст");

    // Пробелы и переводы кавычек внутри пути.
    ::remove(argvFile.c_str());
    const std::string spaced = "https://example.com/a b c";
    check(aerosOpenUrlWith(script.c_str(), spaced.c_str()), "URL с пробелами запускается");
    check(readFile(argvFile) == spaced + "\n", "пробелы не разбили аргумент на части");

    // ---------- 3. отсутствующий opener ----------
    std::printf("\n--- отсутствующий opener — это НЕ успех ---\n");
    check(!aerosOpenUrlWith("/nonexistent/aeros-no-such-browser", "https://example.com"),
          "несуществующая программа → false (раньше system() с & давал 0)");

    // ---------- 4. код возврата различается ----------
    std::printf("\n--- код возврата opener'а важен ---\n");
    check(aerosOpenUrlWith(okFile.c_str(), "https://example.com"), "код 0 → true");
    check(!aerosOpenUrlWith(exit3File.c_str(), "https://example.com"), "код 3 → false");

    // ---------- 5. procutil ----------
    std::printf("\n--- procutil: захват вывода и argv без shell ---\n");
    {
        const procutil::Result r = procutil::runCapture({"printf", "%s-%s", "a b", "c;d"});
        check(r.launched && r.ok, "runCapture запускает программу");
        check(r.output == "a b-c;d", "вывод совпадает, спецсимволы не интерпретированы");
    }
    {
        const procutil::Result r = procutil::runCapture({"/nonexistent/aeros-nope"});
        check(r.launched && !r.ok && r.exitCode == 127,
              "отсутствующий бинарь: запуск был, код 127, ok=false");
    }
    {
        const procutil::Result r = procutil::runCapture({});
        check(!r.launched && !r.ok, "пустой argv → launched=false");
    }
    {
        const procutil::Result r = procutil::runCapture({"printf", "line1\nline2\n"});
        check(procutil::firstLine(r.output) == "line1", "firstLine берёт первую строку");
    }

    std::printf("\n=== ИТОГ: проверок %d, ошибок %d ===\n", g_checks, g_failed);
    return g_failed == 0 ? 0 : 1;
}
