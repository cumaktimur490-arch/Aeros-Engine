#ifndef VOXEL_GRID_H
#define VOXEL_GRID_H
// =====================================================
// Вокселизация модели и поле расстояний (SDF)
//
// v1.36.0 — планирование сетки вынесено в чистую функцию voxelPlan():
// по габариту и разрешению она говорит, сколько будет ячеек, сколько это
// памяти и влезает ли в бюджет. Раньше при превышении потолка программа
// МОЛЧА уменьшала сетку (Clamped to NxNxN) и продолжала расчёт — пользователь
// не знал, что результат получен на другой сетке, а числа становились
// невоспроизводимыми.
// =====================================================

#include <glm/glm.hpp>
#include <string>
#include <vector>

void buildVoxelGrid(const std::vector<float>& verts, int res);
float sampleSDFCPU(const glm::vec3& p);
glm::vec3 sdfNormalCPU(const glm::vec3& p);

// -----------------------------------------------------
// План воксельной сетки. Чистая функция — проверяется headless-тестом.
//
// bytesPerCell — оценка памяти на ячейку: маска (4) + SDF (4) + поля LBM
// (rho, u, uW, vorticity, Q, pressure, TKE, strain — 12×4) + маски
// solid/ground (8) + функции распределения f/fNext (2×19×4 = 152).
// Итого 216 → округляем до 224 с запасом на выравнивание.
// -----------------------------------------------------
struct VoxelPlan {
    int nx = 0, ny = 0, nz = 0;
    long long cells = 0;
    unsigned long long bytes = 0;   // оценка пиковой памяти
    bool fits = true;               // влезает в бюджет
    int requestedRes = 0;
    int suggestedRes = 0;           // до какого разрешения уменьшить, чтобы влезло
};

VoxelPlan voxelPlan(const glm::vec3& size, int res, unsigned long long budgetBytes);

// Результат последней вокселизации — для UI и журнала.
extern std::string g_voxelBuildError;     // пусто = успех
extern std::string g_voxelThinWarning;    // пусто = тонких элементов не обнаружено
extern int g_voxelSuggestedRes;           // 0 = уменьшать некуда/не нужно
extern long long g_voxelSolidCells;       // твёрдых ячеек в последней сетке
extern int g_voxelUsedRes;                // фактическое разрешение
extern bool g_voxelDialogPending;         // интерфейс должен спросить пользователя

#endif // VOXEL_GRID_H
