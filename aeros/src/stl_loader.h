#ifndef STL_LOADER_H
#define STL_LOADER_H
// =====================================================
// Загрузка STL (бинарный + ASCII) и диалог выбора файла
// =====================================================

#include <string>
#include <vector>

bool loadSTL(const std::string& filename, std::vector<float>& vertices, std::vector<float>& normals);

// Диалог выбора STL. ownerWindow — HWND главного окна (void*, чтобы не тащить
// windows.h в заголовок); на Linux игнорируется.
// Владелец обязателен при запуске из установщика: без него диалог может остаться
// за окном программы установки, и пользователь видит «ничего не произошло».
std::string openFileDialog(void* ownerWindow = nullptr);

#endif // STL_LOADER_H
