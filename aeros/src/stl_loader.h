#ifndef STL_LOADER_H
#define STL_LOADER_H
// =====================================================
// Загрузка STL (бинарный + ASCII) и диалог выбора файла
// =====================================================

#include <string>
#include <vector>

bool loadSTL(const std::string& filename, std::vector<float>& vertices, std::vector<float>& normals);
std::string openFileDialog();

#endif // STL_LOADER_H
