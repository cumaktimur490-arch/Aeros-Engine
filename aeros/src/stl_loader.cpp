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

// =====================================================
// STL loader v1.8.0 — исправлено + улучшения
// =====================================================
bool loadSTL(const std::string& filename, std::vector<float>& vertices, std::vector<float>& normals) {
    std::ifstream file(filename, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "[STL] Cannot open: " << filename << std::endl;
        return false;
    }

    file.seekg(0, std::ios::end);
    std::streamoff fileSize = file.tellg();
    if (fileSize < 10) {
        std::cerr << "[STL] File too small: " << fileSize << std::endl;
        return false;
    }
    file.seekg(80, std::ios::beg);
    uint32_t nt = 0;
    if (fileSize >= 84)
        file.read(reinterpret_cast<char*>(&nt), sizeof(nt));
    bool isBinary = (fileSize >= 84) && (fileSize == 84 + (std::streamoff)nt * 50);
    // Дополнительная эвристика: если nt очень большое и не совпадает — считаем ASCII
    if (nt > 10000000) isBinary = false;

    if (isBinary) {
        std::cout << "[STL] Binary STL, triangles=" << nt << " fileSize=" << fileSize << std::endl;
        file.clear(); file.seekg(84, std::ios::beg);
        vertices.reserve(nt*9);
        normals.reserve(nt*9);
        for (uint32_t i = 0; i < nt; i++) {
            float nx, ny, nz;
            file.read(reinterpret_cast<char*>(&nx), sizeof(float));
            file.read(reinterpret_cast<char*>(&ny), sizeof(float));
            file.read(reinterpret_cast<char*>(&nz), sizeof(float));
            if (!std::isfinite(nx) || !std::isfinite(ny) || !std::isfinite(nz)) {
                nx = 0; ny = 0; nz = 1;
            }
            float nLen = sqrtf(nx*nx+ny*ny+nz*nz);
            if (nLen > 1e-6f) { nx/=nLen; ny/=nLen; nz/=nLen; }
            else { nx=0; ny=0; nz=1; }

            for (int v = 0; v < 3; v++) {
                float x, y, z;
                file.read(reinterpret_cast<char*>(&x), sizeof(float));
                file.read(reinterpret_cast<char*>(&y), sizeof(float));
                file.read(reinterpret_cast<char*>(&z), sizeof(float));
                if (!std::isfinite(x) || fabsf(x) > 1e6f) x = 0;
                if (!std::isfinite(y) || fabsf(y) > 1e6f) y = 0;
                if (!std::isfinite(z) || fabsf(z) > 1e6f) z = 0;
                vertices.push_back(x); vertices.push_back(y); vertices.push_back(z);
                normals.push_back(nx);  normals.push_back(ny);  normals.push_back(nz);
            }
            file.ignore(2);
            if (file.eof()) break;
        }
        file.close();
        std::cout << "[STL] Loaded binary: " << vertices.size()/9 << " triangles" << std::endl;
        return !vertices.empty();
    }

    // ASCII STL
    std::cout << "[STL] Trying ASCII parse..." << std::endl;
    file.clear(); file.seekg(0);
    std::string line;
    std::vector<glm::vec3> tv, tn;
    tv.reserve(10000);
    tn.reserve(10000);
    glm::vec3 cn(0.0f, 0.0f, 1.0f);
    int lineNum = 0;
    while (std::getline(file, line)) {
        lineNum++;
        // Trim
        line.erase(line.begin(), std::find_if(line.begin(), line.end(), [](unsigned char ch){ return !std::isspace(ch); }));
        if (line.empty()) continue;
        std::istringstream iss(line);
        std::string t; iss >> t;
        std::transform(t.begin(), t.end(), t.begin(), ::tolower);
        if (t == "facet") {
            std::string nk; iss >> nk;
            std::transform(nk.begin(), nk.end(), nk.begin(), ::tolower);
            if (nk == "normal") {
                iss >> cn.x >> cn.y >> cn.z;
                if (!std::isfinite(cn.x)) cn = glm::vec3(0,0,1);
                float len = glm::length(cn);
                if (len > 1e-6f) cn /= len; else cn = glm::vec3(0,0,1);
            }
        } else if (t == "vertex") {
            glm::vec3 v; iss >> v.x >> v.y >> v.z;
            if (!std::isfinite(v.x) || fabsf(v.x) > 1e6f) continue;
            tv.push_back(v); tn.push_back(cn);
        }
    }
    std::cout << "[STL] ASCII parsed verts=" << tv.size() << std::endl;
    for (size_t i = 0; i + 2 < tv.size(); i += 3) {
        glm::vec3 v0 = tv[i], v1 = tv[i+1], v2 = tv[i+2];
        glm::vec3 n = tn[i];
        if (glm::length(n) < 0.0001f || !std::isfinite(n.x)) {
            glm::vec3 e1 = v1-v0, e2 = v2-v0;
            n = glm::cross(e1, e2);
            float len = glm::length(n);
            if (len > 1e-9f) n /= len; else n = glm::vec3(0,1,0);
        }
        for (int k = 0; k < 3; k++) {
            glm::vec3 v = tv[i+k];
            vertices.push_back(v.x); vertices.push_back(v.y); vertices.push_back(v.z);
            normals.push_back(n.x);  normals.push_back(n.y);  normals.push_back(n.z);
        }
    }
    file.close();
    std::cout << "[STL] Loaded ASCII: " << vertices.size()/9 << " triangles" << std::endl;
    return !vertices.empty();
}

// =====================================================
// Диалог выбора файла (Win32) v1.8.0 — улучшено
// =====================================================
std::string openFileDialog() {
#ifdef _WIN32
    OPENFILENAMEA ofn;
    char fileName[MAX_PATH] = "";
    char initialDir[MAX_PATH] = "";
    GetCurrentDirectoryA(MAX_PATH, initialDir);
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = nullptr;
    ofn.lpstrFilter = "STL Files (*.stl)\0*.stl\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = fileName;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrInitialDir = initialDir;
    ofn.lpstrTitle = "Select STL Model - Aeros Engine v1.8.0";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
    ofn.lpstrDefExt = "stl";
    if (GetOpenFileNameA(&ofn)) return std::string(fileName);
    return "";
#else
    // Linux fallback — try to find an STL in current dir or return empty
    return "";
#endif
}
