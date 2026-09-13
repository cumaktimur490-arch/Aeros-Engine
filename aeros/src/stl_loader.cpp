#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <windows.h>
#include <commdlg.h>

#include <fstream>
#include <sstream>
#include <string>
#include <cstdint>
#include <cstring>
#include <vector>

#include <glm/glm.hpp>

#include "stl_loader.h"

// =====================================================
// STL loader
// =====================================================
bool loadSTL(const std::string& filename, std::vector<float>& vertices, std::vector<float>& normals) {
    std::ifstream file(filename, std::ios::binary);
    if (!file.is_open()) return false;

    // Формат определяем по размеру файла: бинарный STL имеет размер ровно
    // 84 + 50 * кол-во_треугольников байт. Всё остальное пробуем парсить как ASCII.
    // (Проверка по слову "solid" ненадёжна: бинарные файлы тоже могут начинаться с "solid".)
    file.seekg(0, std::ios::end);
    std::streamoff fileSize = file.tellg();
    file.seekg(80, std::ios::beg);
    uint32_t nt = 0;
    if (fileSize >= 84)
        file.read(reinterpret_cast<char*>(&nt), sizeof(nt));
    bool isBinary = (fileSize >= 84) && (fileSize == 84 + (std::streamoff)nt * 50);

    if (isBinary) {
        file.clear(); file.seekg(84, std::ios::beg);
        for (uint32_t i = 0; i < nt; i++) {
            float nx, ny, nz;
            file.read(reinterpret_cast<char*>(&nx), sizeof(float));
            file.read(reinterpret_cast<char*>(&ny), sizeof(float));
            file.read(reinterpret_cast<char*>(&nz), sizeof(float));
            for (int v = 0; v < 3; v++) {
                float x, y, z;
                file.read(reinterpret_cast<char*>(&x), sizeof(float));
                file.read(reinterpret_cast<char*>(&y), sizeof(float));
                file.read(reinterpret_cast<char*>(&z), sizeof(float));
                vertices.push_back(x); vertices.push_back(y); vertices.push_back(z);
                normals.push_back(nx);  normals.push_back(ny);  normals.push_back(nz);
            }
            file.ignore(2);
        }
        file.close();
        return !vertices.empty();
    }

    // ASCII STL
    file.clear(); file.seekg(0);
    std::string line;
    std::vector<glm::vec3> tv, tn;
    glm::vec3 cn(0.0f);
    while (std::getline(file, line)) {
        std::istringstream iss(line);
        std::string t; iss >> t;
        if (t == "facet") { std::string nk; iss >> nk; iss >> cn.x >> cn.y >> cn.z; }
        else if (t == "vertex") { glm::vec3 v; iss >> v.x >> v.y >> v.z; tv.push_back(v); tn.push_back(cn); }
    }
    for (size_t i = 0; i + 2 < tv.size(); i += 3) {
        glm::vec3 v0 = tv[i], v1 = tv[i+1], v2 = tv[i+2];
        glm::vec3 n = tn[i];
        if (glm::length(n) < 0.0001f) n = glm::normalize(glm::cross(v1-v0, v2-v0));
        for (int k = 0; k < 3; k++) {
            glm::vec3 v = tv[i+k];
            vertices.push_back(v.x); vertices.push_back(v.y); vertices.push_back(v.z);
            normals.push_back(n.x);  normals.push_back(n.y);  normals.push_back(n.z);
        }
    }
    file.close();
    return !vertices.empty();
}

// =====================================================
// Диалог выбора файла (Win32)
// =====================================================
std::string openFileDialog() {
    OPENFILENAMEA ofn;
    char fileName[MAX_PATH] = "";
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = nullptr;
    ofn.lpstrFilter = "STL Files\0*.stl\0All Files\0*.*\0";
    ofn.lpstrFile = fileName;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (GetOpenFileNameA(&ofn)) return std::string(fileName);
    return "";
}

