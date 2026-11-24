#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commdlg.h>
#endif

#include <fstream>
#include <sstream>
#include <string>
#include <cstdint>
#include <cstring>
#include <vector>
#include <iostream>
#include <algorithm>
#include <cmath>

#include <glm/glm.hpp>

#include "stl_loader.h"
#include "globals.h"
#include "version.h"
#include "proc_util.h"
#include "stl_format.h"   // v1.36.0 — определение формата по содержимому
#include "mem_budget.h"   // v1.36.0 — бюджет памяти вместо магического лимита
#include "debug_log.h"    // v1.36.0 — причины отказа пишутся в журнал   // v1.36.0 — запуск zenity/kdialog без shell

#ifndef _WIN32
#include <unistd.h>
#endif

// =====================================================
// STL loader v1.8.0 — исправлено + улучшения
// =====================================================
bool loadSTL(const std::string& filename, std::vector<float>& vertices, std::vector<float>& normals) {
    std::ifstream file(filename, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "[STL] Cannot open: " << filename << std::endl;
        return false;
    }

    // v1.36.0 — читаем файл ОДНИМ блоком, а не по три float на треугольник.
    // Раньше на 1M треугольников приходилось 4M вызовов read(): это не только
    // медленно, но и давало разное поведение на разных ФС. Буфер целиком —
    // 50 МБ на 1M треугольников, что заведомо меньше, чем сами вершины в RAM.
    file.seekg(0, std::ios::end);
    const std::streamoff fileSizeOff = file.tellg();
    if (fileSizeOff < 10 || fileSizeOff > (std::streamoff)(2ull << 30)) {
        std::cerr << "[STL] Недопустимый размер файла: " << fileSizeOff << " байт" << std::endl;
        DBG_ERROR("STL: недопустимый размер файла (%lld байт): %s",
                  (long long)fileSizeOff, filename.c_str());
        return false;
    }
    const std::size_t fileSize = (std::size_t)fileSizeOff;
    std::vector<unsigned char> data;
    try {
        data.resize(fileSize);
    } catch (const std::bad_alloc&) {
        std::cerr << "[STL] Не хватило памяти на файл " << fileSize << " байт" << std::endl;
        return false;
    }
    file.seekg(0, std::ios::beg);
    file.read(reinterpret_cast<char*>(data.data()), (std::streamsize)fileSize);
    if (!file) {
        std::cerr << "[STL] Ошибка чтения файла (прочитано " << file.gcount()
                  << " из " << fileSize << " байт)" << std::endl;
        DBG_ERROR("STL: ошибка чтения %s (прочитано %lld из %zu байт)",
                  filename.c_str(), (long long)file.gcount(), fileSize);
        return false;
    }
    file.close();

    const stlformat::Detection det = stlformat::detect(data.data(), data.size());
    std::cout << "[STL] Формат: "
              << (det.format == stlformat::Format::Binary ? "binary" :
                  det.format == stlformat::Format::Ascii  ? "ascii" : "unknown")
              << " (" << det.reason << "), размер " << fileSize << " байт" << std::endl;

    if (det.format == stlformat::Format::Binary) {
        const std::uint32_t nt = det.declaredTriangles;
        // Бюджет по памяти, а не по числу треугольников: вершины+нормали — это
        // 72 байта на треугольник (9 float × 2 массива).
        const std::uint64_t needRam = (std::uint64_t)nt * 9 * sizeof(float) * 2;
        const std::uint64_t budget = membudget::budgetBytes();
        if (needRam > budget) {
            std::cerr << "[STL] Модель требует " << membudget::formatBytes(needRam)
                      << " памяти, доступно " << membudget::formatBytes(budget) << std::endl;
            DBG_ERROR("STL: на %u треугольников нужно %s памяти, бюджет %s — модель не загружена; "
                      "уменьшите число треугольников в CAD (decimate) и повторите",
                      nt, membudget::formatBytes(needRam).c_str(),
                      membudget::formatBytes(budget).c_str());
            return false;
        }

        std::cout << "[STL] Binary STL, triangles=" << nt << " fileSize=" << fileSize;
        if (det.trailingBytes) std::cout << " (хвост " << det.trailingBytes << " байт проигнорирован)";
        std::cout << std::endl;

        vertices.reserve((std::size_t)nt * 9);
        normals.reserve((std::size_t)nt * 9);

        std::size_t off = 84;
        std::uint32_t dropped = 0;   // треугольники с NaN/Inf
        for (std::uint32_t i = 0; i < nt; ++i, off += 50) {
            float n[3];
            std::memcpy(n, data.data() + off, 12);
            if (!std::isfinite(n[0]) || !std::isfinite(n[1]) || !std::isfinite(n[2])) {
                n[0] = 0; n[1] = 0; n[2] = 1;
            }
            const float nLen = std::sqrt(n[0]*n[0] + n[1]*n[1] + n[2]*n[2]);
            if (nLen > 1e-6f) { n[0] /= nLen; n[1] /= nLen; n[2] /= nLen; }
            else { n[0] = 0; n[1] = 0; n[2] = 1; }

            float v[9];
            std::memcpy(v, data.data() + off + 12, 36);

            // v1.36.0 — NaN/Inf и «большие координаты» больше НЕ смешаны.
            // Раньше |x| > 1e6 обнулялось наравне с NaN: модель в миллиметрах с
            // габаритом в тысячи единиц расплющивалась, треугольник терял форму,
            // меш переставал быть замкнутым. Теперь брак отбрасывается целиком
            // (и считается), а масштаб — задача выбора единиц в model.cpp.
            bool bad = false;
            for (int k = 0; k < 9; ++k) {
                if (!std::isfinite(v[k])) { bad = true; break; }
            }
            if (bad) { ++dropped; continue; }

            for (int k = 0; k < 3; ++k) {
                vertices.push_back(v[k*3 + 0]);
                vertices.push_back(v[k*3 + 1]);
                vertices.push_back(v[k*3 + 2]);
                normals.push_back(n[0]);
                normals.push_back(n[1]);
                normals.push_back(n[2]);
            }
        }
        if (dropped > 0) {
            std::cerr << "[STL] Отброшено " << dropped
                      << " треугольников с NaN/Inf координатами" << std::endl;
            DBG_WARN("STL: отброшено %u треугольников с NaN/Inf (файл %s)",
                     dropped, filename.c_str());
        }
        std::cout << "[STL] Loaded binary: " << vertices.size()/9 << " triangles" << std::endl;
        return !vertices.empty();
    }

    if (det.format == stlformat::Format::Unknown) {
        std::cerr << "[STL] Формат не распознан: " << det.reason << std::endl;
        DBG_ERROR("STL: не удалось определить формат %s (%s)", filename.c_str(), det.reason);
        return false;
    }

    // ---------------- ASCII ----------------
    std::cout << "[STL] ASCII парсинг (" << det.reason << ")" << std::endl;
    std::istringstream iss(std::string(reinterpret_cast<const char*>(data.data()), data.size()));
    std::string line;
    std::vector<glm::vec3> tv;
    tv.reserve(10000);
    glm::vec3 cn(0.0f, 0.0f, 1.0f);
    std::uint32_t droppedAscii = 0;
    while (std::getline(iss, line)) {
        line.erase(line.begin(), std::find_if(line.begin(), line.end(),
                   [](unsigned char ch){ return !std::isspace(ch); }));
        if (line.empty()) continue;
        std::istringstream ls(line);
        std::string t; ls >> t;
        std::transform(t.begin(), t.end(), t.begin(),
                       [](unsigned char c){ return (char)std::tolower(c); });
        if (t == "facet") {
            std::string nk; ls >> nk;
            std::transform(nk.begin(), nk.end(), nk.begin(),
                           [](unsigned char c){ return (char)std::tolower(c); });
            if (nk == "normal") {
                ls >> cn.x >> cn.y >> cn.z;
                if (!std::isfinite(cn.x) || !std::isfinite(cn.y) || !std::isfinite(cn.z))
                    cn = glm::vec3(0, 0, 1);
                const float len = glm::length(cn);
                if (len > 1e-6f) cn /= len; else cn = glm::vec3(0, 0, 1);
            }
        } else if (t == "vertex") {
            glm::vec3 v; ls >> v.x >> v.y >> v.z;
            // Как и в бинарном пути: NaN — брак, большие координаты — норма.
            if (!std::isfinite(v.x) || !std::isfinite(v.y) || !std::isfinite(v.z)) {
                ++droppedAscii;
                continue;
            }
            tv.push_back(v);
        }
    }
    if (droppedAscii % 3 != 0) droppedAscii -= droppedAscii % 3;
    if (droppedAscii > 0) {
        std::cerr << "[STL] Пропущено " << droppedAscii << " вершин с NaN/Inf" << std::endl;
        DBG_WARN("STL ASCII: пропущено %u вершин с NaN/Inf", droppedAscii);
    }
    // Вершины шли тройками: если брак «сдвинул» тройку, отбрасываем остаток.
    const std::size_t triCount = tv.size() / 3;
    vertices.reserve(triCount * 9);
    normals.reserve(triCount * 9);
    for (std::size_t i = 0; i + 2 < tv.size(); i += 3) {
        glm::vec3 v0 = tv[i], v1 = tv[i+1], v2 = tv[i+2];
        glm::vec3 n = glm::cross(v1 - v0, v2 - v0);
        const float len = glm::length(n);
        if (len > 1e-9f) n /= len; else n = glm::vec3(0, 1, 0);
        for (int k = 0; k < 3; ++k) {
            const glm::vec3& v = tv[i + k];
            vertices.push_back(v.x); vertices.push_back(v.y); vertices.push_back(v.z);
            normals.push_back(n.x);  normals.push_back(n.y);  normals.push_back(n.z);
        }
    }
    std::cout << "[STL] Loaded ASCII: " << vertices.size()/9 << " triangles" << std::endl;
    return !vertices.empty();
}

// =====================================================
// Запуск диалога на Linux без shell (v1.36.0).
//
// Возвращает выбранный путь или пустую строку (отмена / диалог не установлен).
// Никакой склейки командной строки: argv уходит в execvp как есть, поэтому путь
// с апострофом, кавычкой или `$(...)` — это просто имя файла, а не команда.
// =====================================================
namespace {
#ifndef _WIN32
enum class DialogStatus { NotFound, Cancelled, Ok };

DialogStatus runFileDialog(const std::vector<std::string>& argv, std::string& outPath) {
    outPath.clear();
    const procutil::Result r = procutil::runCapture(argv);
    if (!r.launched || r.exitCode == 127) return DialogStatus::NotFound;
    if (!r.ok) return DialogStatus::Cancelled;
    outPath = r.output;
    while (!outPath.empty() && (outPath.back() == '\n' || outPath.back() == '\r')) outPath.pop_back();
    return outPath.empty() ? DialogStatus::Cancelled : DialogStatus::Ok;
}
#endif
} // namespace

// =====================================================
// Диалог выбора файла v1.19.0 — Windows + Linux (zenity/kdialog/console)
// =====================================================
std::string openFileDialog(void* ownerWindow) {
#ifdef _WIN32
    OPENFILENAMEA ofn;
    char fileName[4096] = "";
    char initialDir[4096] = "";
    GetCurrentDirectoryA((DWORD)sizeof(initialDir), initialDir);
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    HWND owner = (HWND)(ownerWindow ? ownerWindow : g_mainWin32Hwnd);
    if (!owner) owner = GetActiveWindow();
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = "STL & Study Files (*.stl;*.aerosproj)\0*.stl;*.aerosproj;*.study;*.aero\0STL Files (*.stl)\0*.stl\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = fileName;
    ofn.nMaxFile = (DWORD)sizeof(fileName);
    ofn.lpstrInitialDir = initialDir;
    ofn.lpstrTitle = "Select STL Model - Aeros Engine v" AEROS_VERSION_STRING;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY | OFN_NOCHANGEDIR;
    ofn.lpstrDefExt = "stl";
    if (GetOpenFileNameA(&ofn)) return std::string(fileName);
    return "";
#else
    // Linux — zenity, затем kdialog, затем известные пути.
    // v1.36.0 — без popen: команды передаются execvp() массивом аргументов,
    // оболочки в цепочке нет. Раньше строка диалога склеивалась в shell-команду.
    {
        std::string s;
        DialogStatus st = runFileDialog({
            "zenity", "--file-selection",
            "--file-filter=STL & Study files | *.stl *.STL *.aerosproj *.study *.aero",
            "--file-filter=All files | *",
            "--title=Open STL Model - Aeros Engine v" AEROS_VERSION_STRING}, s);
        if (st == DialogStatus::Ok) return s;
        if (st == DialogStatus::Cancelled) return "";

        st = runFileDialog({"kdialog", "--getopenfilename", ".", "*.stl *.aerosproj | STL & Study Files"}, s);
        if (st == DialogStatus::Ok) return s;
        if (st == DialogStatus::Cancelled) return "";
    }
    // Fallback: check common locations
    std::vector<std::string> candidates = {
        "./models/model.stl", "./model.stl", "./bin/models/model.stl",
        "./aeros/models/model.stl", "./aeros/bin/models/sample_cube.stl",
        "/usr/share/aeros-engine/models/model.stl",
        "/usr/local/share/aeros-engine/models/model.stl",
        "./sample_cube.stl"
    };
    for (auto& c : candidates) {
        std::ifstream f(c);
        if (f.good()) {
            std::cout << "[FileDialog] Linux fallback using: " << c << std::endl;
            return c;
        }
    }
    // Если программа запущена не из интерактивного терминала (GUI/ярлык),
    // чтение std::cin заблокирует поток UI навсегда («программа зависает»).
    if (!isatty(STDIN_FILENO)) {
        DBG_WARN("диалог выбора файла недоступен (нет zenity/kdialog и интерактивного терминала)");
        return "";
    }
    std::cout << "Enter STL file path (or drag & drop): ";
    std::string input;
    if (!std::getline(std::cin, input)) return "";
    // Remove quotes if present
    if (input.size() >= 2 && input.front() == '\'' && input.back() == '\'') input = input.substr(1, input.size()-2);
    if (input.size() >= 2 && input.front() == '"' && input.back() == '"') input = input.substr(1, input.size()-2);
    return input;
#endif
}
