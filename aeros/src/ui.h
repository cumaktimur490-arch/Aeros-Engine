#ifndef UI_H
#define UI_H
// =====================================================
// Панель управления (Dear ImGui) v1.9.0
// =====================================================
#include <string>

void drawUI();
bool saveSettings(const std::string& path);
bool loadSettings(const std::string& path);

#endif // UI_H
