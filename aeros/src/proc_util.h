#pragma once
// =====================================================
// v1.36.0 — запуск внешних программ БЕЗ shell.
//
// Зачем отдельный модуль. В v1.35.0 открытие ссылки и диалоги на Linux были
// сделаны через std::system()/popen() со склейкой командной строки:
//
//     std::string cmd = "xdg-open \"" + url + "\" &";
//     popen(("zenity ... --filename='" + name + "'").c_str(), "r");
//
// Это две разные ошибки в одном приёме:
//   1) любой спецсимвол в url/name (`"`, `'`, `;`, `$(...)`, обратная кавычка)
//      трактуется оболочкой как часть синтаксиса — то есть строка собирается
//      в командную строку из данных, пришедших извне;
//   2) `&` и popen возвращают статус ОБОЛОЧКИ, а не программы. `system() == 0`
//      означает «sh запустился», поэтому функция рапортовала успех, даже когда
//      xdg-open отсутствует в системе.
//
// Здесь argv передаётся execvp() как массив: оболочки в цепочке нет вообще,
// поэтому разделители и кавычки внутри аргумента остаются обычными символами.
// Код завершения — код самой программы.
//
// Windows-ветка намеренно не реализована: там диалоги идут через WinAPI
// (GetOpenFileName/ShellExecute), а shell-строк в коде нет.
// =====================================================

#include <string>
#include <vector>

#ifndef _WIN32
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace procutil {

struct Result {
    bool launched = false;   // процесс удалось запустить (exec не упал)
    bool ok = false;         // и он завершился с кодом 0
    int  exitCode = -1;      // код выхода, -1 если завершился сигналом/не стартовал
    std::string output;      // stdout (для диалогов выбора файла)
    std::string error;       // причина, если запуск не удался (errno или пояснение)
};

namespace detail {

#ifndef _WIN32
inline std::vector<char*> toArgv(const std::vector<std::string>& argv) {
    std::vector<char*> out;
    out.reserve(argv.size() + 1);
    for (const std::string& a : argv) out.push_back(const_cast<char*>(a.c_str()));
    out.push_back(nullptr);
    return out;
}

// Общая часть run() и runCapture(): fork, exec, при необходимости — захват stdout.
inline Result spawn(const std::vector<std::string>& argv, bool capture) {
    Result r;
    if (argv.empty() || argv[0].empty()) {
        r.error = "пустой список аргументов";
        return r;
    }

    int fds[2] = {-1, -1};
    if (capture && pipe(fds) != 0) {
        r.error = std::string("pipe: ") + std::strerror(errno);
        return r;
    }

    const pid_t pid = fork();
    if (pid < 0) {
        r.error = std::string("fork: ") + std::strerror(errno);
        if (capture) { ::close(fds[0]); ::close(fds[1]); }
        return r;
    }

    if (pid == 0) {
        // ---- дочерний процесс: только dup2/execvp, без аллокаций после fork
        const int devnull = ::open("/dev/null", O_WRONLY);
        if (capture) {
            ::close(fds[0]);
            ::dup2(fds[1], STDOUT_FILENO);
            if (devnull >= 0) ::dup2(devnull, STDERR_FILENO);
            ::close(fds[1]);
        } else {
            if (devnull >= 0) {
                ::dup2(devnull, STDOUT_FILENO);
                ::dup2(devnull, STDERR_FILENO);
            }
        }
        if (devnull >= 0) ::close(devnull);

        std::vector<char*> cargv = toArgv(argv);
        execvp(cargv[0], cargv.data());
        _exit(127);   // exec не удался: код 127 — стандартный признак «команда не найдена»
    }

    if (capture) {
        ::close(fds[1]);
        char buf[4096];
        ssize_t n;
        while ((n = ::read(fds[0], buf, sizeof(buf))) > 0)
            r.output.append(buf, (size_t)n);
        ::close(fds[0]);
    }

    int status = 0;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) { /* повтор */ }

    r.launched = true;
    r.ok = WIFEXITED(status) && WEXITSTATUS(status) == 0;
    r.exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    return r;
}
#endif

} // namespace detail

// Запустить и дождаться завершения, собрав stdout.
inline Result runCapture(const std::vector<std::string>& argv) {
#ifdef _WIN32
    Result r;
    r.error = "procutil::runCapture не поддерживается на Windows";
    (void)argv;
    return r;
#else
    return detail::spawn(argv, true);
#endif
}

// Запустить и дождаться завершения; вывод глушится.
inline Result run(const std::vector<std::string>& argv) {
#ifdef _WIN32
    Result r;
    r.error = "procutil::run не поддерживается на Windows";
    (void)argv;
    return r;
#else
    return detail::spawn(argv, false);
#endif
}

// Первая непустая строка вывода (диалоги печатают ровно одну строку-путь).
inline std::string firstLine(const std::string& text) {
    size_t begin = 0;
    while (begin < text.size() && (text[begin] == '\n' || text[begin] == '\r')) ++begin;
    size_t end = begin;
    while (end < text.size() && text[end] != '\n' && text[end] != '\r') ++end;
    return text.substr(begin, end - begin);
}

} // namespace procutil
