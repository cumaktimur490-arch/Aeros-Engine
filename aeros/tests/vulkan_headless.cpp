// =====================================================
// Aeros Engine v1.33.0 — headless-тест Vulkan-бэкенда.
//
// В песочнице (и на CI) нет ни GPU, ни Vulkan SDK, поэтому проверить саму
// отрисовку нельзя. Зато можно проверить всё, что в Vulkan-пути чаще всего
// ломается и что при этом целиком лежит в чистом коде:
//
//   1) выбор размера swapchain (currentExtent == 0xFFFFFFFF, клампы, свёрнутое окно);
//   2) количество изображений в swapchain и режим показа (vsync/без vsync);
//   3) выбор формата изображения (sRGB предпочтительнее линейного);
//   4) правило включения Vulkan — по одной проверке на каждое условие;
//   5) раскладка вершины и размер push-констант: рассинхрон C++ и GLSL даёт
//      мусор на экране, а не ошибку, поэтому размеры проверяются явно;
//   6) вкомпилированный SPIR-V: магия, версия, границы идентификаторов, наличие
//      точки входа нужной стадии. Здесь же — что повреждённый модуль отвергается;
//   7) геометрия сцены (scene_geometry.h): габарит, оси, эллипсоид, контур среза,
//      стрелки сил — количество вершин, конечность значений, отсутствие вырожденных
//      треугольников на полюсах, устойчивость к NaN/inf на входе.
//
// Сборка и запуск: make vulkan
// =====================================================

#include "vulkan_backend.h"
#include "vulkan_scene_spv.h"
#include "scene_geometry.h"

#include <glm/glm.hpp>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static int g_checks = 0;
static int g_failed = 0;

static void check(bool ok, const std::string& what) {
    ++g_checks;
    if (!ok) ++g_failed;
    std::printf("  %s %s\n", ok ? "ok:  " : "ОШИБКА:", what.c_str());
}

static bool finiteAll(const std::vector<float>& v) {
    for (float x : v)
        if (!std::isfinite(x)) return false;
    return true;
}

// -----------------------------------------------------
// 1. Размер swapchain
// -----------------------------------------------------
static void testExtent() {
    std::printf("\n[1] Размер изображения swapchain\n");
    uint32_t w = 0, h = 0;

    // Поверхность сама задаёт размер — берём его как есть.
    check(vklogic::vkChooseExtent(800, 600, 1024, 768, 1, 1, 4096, 4096, &w, &h) && w == 1024 && h == 768,
          "currentExtent задан поверхностью — используется он");

    // 0xFFFFFFFF означает «размер задаёт приложение».
    check(vklogic::vkChooseExtent(800, 600, 0xFFFFFFFFu, 0xFFFFFFFFu, 1, 1, 4096, 4096, &w, &h) &&
              w == 800 && h == 600,
          "currentExtent == 0xFFFFFFFF — берётся размер кадрового буфера");

    // Кламп по минимуму и максимуму.
    check(vklogic::vkChooseExtent(10, 10, 0xFFFFFFFFu, 0xFFFFFFFFu, 64, 64, 4096, 4096, &w, &h) &&
              w == 64 && h == 64,
          "слишком маленький размер поднимается до minImageExtent");
    check(vklogic::vkChooseExtent(9000, 9000, 0xFFFFFFFFu, 0xFFFFFFFFu, 1, 1, 1920, 1080, &w, &h) &&
              w == 1920 && h == 1080,
          "слишком большой размер опускается до maxImageExtent");
    // maxImageExtent == 0 означает «без ограничения».
    check(vklogic::vkChooseExtent(3000, 2000, 0xFFFFFFFFu, 0xFFFFFFFFu, 1, 1, 0, 0, &w, &h) &&
              w == 3000 && h == 2000,
          "maxImageExtent == 0 трактуется как «без ограничения»");

    // Свёрнутое окно: рисовать нельзя, кадр надо пропустить, а не делить на ноль.
    check(!vklogic::vkChooseExtent(0, 0, 0xFFFFFFFFu, 0xFFFFFFFFu, 1, 1, 4096, 4096, &w, &h),
          "нулевой размер (окно свёрнуто) — кадр пропускается");
    check(!vklogic::vkChooseExtent(800, 600, 1024, 768, 1, 1, 4096, 4096, nullptr, &h),
          "нулевой указатель не разыменовывается");
}

// -----------------------------------------------------
// 2. Количество изображений и режим показа
// -----------------------------------------------------
static void testImageCountAndPresent() {
    std::printf("\n[2] Изображения swapchain и режим показа\n");
    check(vklogic::vkImageCount(2, 0, 3) == 3, "min=2, без максимума, хотим 3 → 3 изображения");
    check(vklogic::vkImageCount(3, 0, 3) == 4, "min=3 → берём min+1 (4), чтобы не голодать по кадрам");
    check(vklogic::vkImageCount(2, 2, 3) == 2, "maxImageCount=2 ограничивает сверху");
    check(vklogic::vkImageCount(2, 5, 3) == 3, "maxImageCount=5 не мешает желаемым 3");

    // Режимы: 0 = IMMEDIATE, 1 = MAILBOX, 2 = FIFO, 3 = FIFO_RELAXED.
    const std::vector<int> all = {0, 1, 2, 3};
    check(vklogic::vkChoosePresentMode(all, true) == 2,
          "vsync включён → FIFO (обязательный минимум по спецификации)");
    check(vklogic::vkChoosePresentMode(all, false) == 1,
          "vsync выключен → MAILBOX (нет разрывов и нет ожидания)");
    check(vklogic::vkChoosePresentMode({2}, false) == 2,
          "только FIFO доступен — используется FIFO независимо от vsync");
    check(vklogic::vkChoosePresentMode({0, 2}, false) == 0,
          "MB нет, но есть IMMEDIATE → IMMEDIATE");
    check(vklogic::vkChoosePresentMode({}, false) == 2,
          "пустой список режимов не приводит к неопределённому значению");
}

// -----------------------------------------------------
// 3. Формат изображения
// -----------------------------------------------------
static void testFormat() {
    std::printf("\n[3] Формат изображения\n");
    using vklogic::SurfaceFormat;
    const int BGRA_SRGB = vklogic::kFmtBgra8Srgb;
    const int RGBA_SRGB = vklogic::kFmtRgba8Srgb;
    const int BGRA_UNORM = vklogic::kFmtBgra8Unorm;
    const int SRGB_CS = vklogic::kColorSpaceSrgbNonlinear;

    check(vklogic::vkChooseFormat({{BGRA_UNORM, SRGB_CS}, {BGRA_SRGB, SRGB_CS}}).format == BGRA_SRGB,
          "sRGB предпочитается линейному формату (иначе картинка пересвечена)");
    check(vklogic::vkChooseFormat({{RGBA_SRGB, SRGB_CS}, {BGRA_SRGB, SRGB_CS}}).format == BGRA_SRGB,
          "BGRA предпочитается RGBA (родной порядок каналов большинства драйверов)");
    check(vklogic::vkChooseFormat({{BGRA_UNORM, SRGB_CS}}).format == BGRA_UNORM,
          "если sRGB нет — берётся линейный 8-битный с альфой");
    check(vklogic::vkChooseFormat({{0, 0}}).format == BGRA_SRGB,
          "VK_FORMAT_UNDEFINED («любой») заменяется нашим выбором");
    check(vklogic::vkChooseFormat({{37, 1}}).format == 37,
          "неизвестный набор форматов не ломает выбор (возвращается предложенный)");
    check(vklogic::vkChooseFormat({}).format == BGRA_SRGB,
          "пустой список форматов даёт рабочий формат по умолчанию");
}

// -----------------------------------------------------
// 4. Правило включения Vulkan
// -----------------------------------------------------
static void testPolicy() {
    std::printf("\n[4] Правило включения Vulkan\n");
    using vklogic::VkDecision;
    vklogic::VkPolicyInput in;
    in.builtWithVulkan = true;
    in.loaderPresent = true;
    in.deviceUsable = true;
    in.userAskedVulkan = true;
    check(vklogic::vkPolicy(in).decision == VkDecision::UseVulkan,
          "все условия выполнены → Vulkan");

    vklogic::VkPolicyInput noBuild = in;
    noBuild.builtWithVulkan = false;
    check(vklogic::vkPolicy(noBuild).decision == VkDecision::FallBackOpenGL,
          "сборка без заголовков Vulkan → OpenGL");

    vklogic::VkPolicyInput noLoader = in;
    noLoader.loaderPresent = false;
    check(vklogic::vkPolicy(noLoader).decision == VkDecision::FallBackOpenGL,
          "нет загрузчика Vulkan → OpenGL");

    vklogic::VkPolicyInput noDevice = in;
    noDevice.deviceUsable = false;
    check(vklogic::vkPolicy(noDevice).decision == VkDecision::FallBackOpenGL,
          "нет устройства с графикой → OpenGL");

    vklogic::VkPolicyInput notAsked = in;
    notAsked.userAskedVulkan = false;
    check(vklogic::vkPolicy(notAsked).decision == VkDecision::FallBackOpenGL,
          "пользователь не просил Vulkan → OpenGL (автовыбор не меняет поведение)");
    check(!vklogic::vkPolicy(notAsked).reason.empty(),
          "причина отказа заполнена — её видно в журнале и в панели «Рендерер»");
}

// -----------------------------------------------------
// 5. Раскладка вершины и push-константы
// -----------------------------------------------------
static void testLayout() {
    std::printf("\n[5] Раскладка вершины и push-константы\n");
    check(vklogic::kVertexFloatCount == 9, "вершина — 9 float (позиция/нормаль/цвет)");
    check(vklogic::kVertexStride == 36, "шаг вершины — 36 байт");
    check(vklogic::kPosOffset == 0 && vklogic::kNormalOffset == 12 && vklogic::kColorOffset == 24,
          "смещения атрибутов совпадают с layout(location) в scene.vert");
    check(vklogic::kPushConstantBytes == 112,
          "push-константы — 112 Б (mat4 + 3 vec4) и не превышают минимум 128 Б");
}

// -----------------------------------------------------
// 6. Проверка SPIR-V
// -----------------------------------------------------
static void testSpirv() {
    std::printf("\n[6] Вкомпилированные шейдеры (SPIR-V)\n");
    std::string err;

    check(vklogic::vkSpirvValid(kSceneVertSpv, kSceneVertSpvSize, false, &err),
          "вершинный шейдер проходит проверку" + (err.empty() ? std::string() : ": " + err));
    err.clear();
    check(vklogic::vkSpirvValid(kSceneFragSpv, kSceneFragSpvSize, true, &err),
          "фрагментный шейдер проходит проверку" + (err.empty() ? std::string() : ": " + err));
    check(kSceneVertSpv[0] == 0x07230203u && kSceneFragSpv[0] == 0x07230203u,
          "оба модуля начинаются с магии SPIR-V");
    check(kSceneVertSpvSize > 40 && kSceneFragSpvSize > 40,
          "модули не пустые (больше заголовка и пары инструкций)");

    // Стадии перепутать нельзя: точка входа ищется по execution model.
    err.clear();
    check(!vklogic::vkSpirvValid(kSceneVertSpv, kSceneVertSpvSize, true, &err),
          "вершинный модуль не принимается за фрагментный");

    // Повреждённые модули отвергаются до передачи драйверу.
    std::vector<uint32_t> bad(kSceneVertSpv, kSceneVertSpv + kSceneVertSpvSize);
    bad[0] = 0xDEADBEEFu;
    check(!vklogic::vkSpirvValid(bad.data(), bad.size(), false, &err), "неверная магия отвергается");

    std::vector<uint32_t> truncated(kSceneVertSpv, kSceneVertSpv + 3);
    check(!vklogic::vkSpirvValid(truncated.data(), truncated.size(), false, &err),
          "обрезанный модуль отвергается");

    std::vector<uint32_t> zeroBound(kSceneVertSpv, kSceneVertSpv + kSceneVertSpvSize);
    zeroBound[3] = 0;
    check(!vklogic::vkSpirvValid(zeroBound.data(), zeroBound.size(), false, &err),
          "нулевая граница идентификаторов отвергается");

    std::vector<uint32_t> badVersion(kSceneVertSpv, kSceneVertSpv + kSceneVertSpvSize);
    badVersion[1] = 2u << 16;   // major = 2
    check(!vklogic::vkSpirvValid(badVersion.data(), badVersion.size(), false, &err),
          "major-версия SPIR-V 2 отвергается");

    // Нулевая длина инструкции — иначе разбор зациклился бы.
    std::vector<uint32_t> zeroWordCount(kSceneVertSpv, kSceneVertSpv + kSceneVertSpvSize);
    zeroWordCount[5] = 0;
    check(!vklogic::vkSpirvValid(zeroWordCount.data(), zeroWordCount.size(), false, &err),
          "инструкция нулевой длины отвергается (нет зацикливания)");

    check(!vklogic::vkSpirvValid(nullptr, 0, false, &err), "пустой указатель отвергается");
}

// -----------------------------------------------------
// 7. Геометрия сцены
// -----------------------------------------------------
static void testSceneGeometry() {
    std::printf("\n[7] Геометрия сцены\n");
    using namespace scenegem;

    // Габарит: 12 рёбер = 24 вершины по 6 float.
    const std::vector<float> bbox = boundingBoxLines({-1, -2, -3}, {4, 5, 6}, {1, 0, 0});
    check(bbox.size() == 24 * 6, "габарит — 24 вершины (12 рёбер)");
    check(finiteAll(bbox), "координаты габарита конечны");
    bool inside = true;
    for (size_t i = 0; i + 5 < bbox.size(); i += 6) {
        if (bbox[i] < -1.0001f || bbox[i] > 4.0001f) inside = false;
        if (bbox[i + 1] < -2.0001f || bbox[i + 1] > 5.0001f) inside = false;
        if (bbox[i + 2] < -3.0001f || bbox[i + 2] > 6.0001f) inside = false;
    }
    check(inside, "все вершины габарита лежат в заданных границах");
    check(boundingBoxLines({0, 0, 0}, {0, 0, 0}, {1, 1, 1}).size() == 24 * 6,
          "вырожденный габарит заменяется единичным кубом, а не исчезает");

    // Оси: 3 линии по 2 вершины, у каждой свой цвет.
    const std::vector<float> axes = axisLines(2.0f, {1, 0, 0}, {0, 1, 0}, {0, 0, 1});
    // Каждая ось — 12 float: начало (позиция+цвет) и конец (позиция+цвет).
    check(axes.size() == 3 * 12, "оси — 3 линии по 2 вершины");
    check(axes[3] == 1.0f && axes[4] == 0.0f && axes[5] == 0.0f, "ось X красная");
    check(axes[15] == 0.0f && axes[16] == 1.0f && axes[17] == 0.0f, "ось Y зелёная");
    check(axes[27] == 0.0f && axes[28] == 0.0f && axes[29] == 1.0f, "ось Z синяя");
    check(std::fabs(axes[6] - 2.0f) < 1e-6f, "длина оси соответствует размеру сцены");
    const std::vector<float> unitAxes = axisLines(0.0f, {1, 1, 1}, {1, 1, 1}, {1, 1, 1});
    check(unitAxes.size() == 3 * 12 && std::fabs(unitAxes[6] - 1.0f) < 1e-6f,
          "нулевой размер оси заменяется единичным");

    // Эллипсоид: вершины кратны 3 (треугольники), нормали единичные.
    const glm::vec3 radii(2.0f, 1.0f, 0.5f);
    const std::vector<float> ell = ellipsoidTriangles(radii, 12, 16);
    check(!ell.empty() && ell.size() % (6 * 3) == 0, "эллипсоид состоит из целых треугольников");
    check(finiteAll(ell), "координаты эллипсоида конечны");
    bool unitNormals = true, onSurface = true, noDegenerate = true;
    for (size_t i = 0; i + 5 < ell.size(); i += 6) {
        const float nx = ell[i + 3], ny = ell[i + 4], nz = ell[i + 5];
        if (std::fabs(std::sqrt(nx * nx + ny * ny + nz * nz) - 1.0f) > 1e-3f) unitNormals = false;
        // Точка поверхности: (x/rx)^2 + (y/ry)^2 + (z/rz)^2 == 1.
        const float u = ell[i] / radii.x, v = ell[i + 1] / radii.y, w = ell[i + 2] / radii.z;
        if (std::fabs(u * u + v * v + w * w - 1.0f) > 1e-3f) onSurface = false;
    }
    for (size_t t = 0; t + 17 < ell.size(); t += 18) {
        // Площадь треугольника через векторное произведение: на полюсах
        // вырожденные треугольники не должны попадать в список.
        const glm::vec3 a(ell[t], ell[t + 1], ell[t + 2]);
        const glm::vec3 b(ell[t + 6], ell[t + 7], ell[t + 8]);
        const glm::vec3 c(ell[t + 12], ell[t + 13], ell[t + 14]);
        if (glm::length(glm::cross(b - a, c - a)) < 1e-7f) noDegenerate = false;
    }
    check(unitNormals, "нормали эллипсоида единичные");
    check(onSurface, "все вершины эллипсоида лежат на его поверхности");
    check(noDegenerate, "вырожденных треугольников на полюсах нет");
    check(ellipsoidTriangles({0, 0, 0}, 8, 8).size() == ellipsoidTriangles({1, 1, 1}, 8, 8).size(),
          "нулевые радиусы заменяются единичными (геометрия не пропадает)");
    check(ellipsoidTriangles(radii, 2, 2).size() > 0, "слишком мелкая сетка поднимается до минимума");

    // Контур среза: 4 отрезка = 8 вершин, все внутри габарита.
    for (int axis = 0; axis < 3; ++axis) {
        const std::vector<float> slice = sliceOutlineLines({-1, -1, -1}, {1, 1, 1}, axis, 0.5f, {0, 1, 1});
        check(slice.size() == 8 * 6, "контур среза по оси " + std::to_string(axis) + " — 8 вершин");
        bool planeFixed = true;
        for (size_t i = 0; i + 5 < slice.size(); i += 6)
            if (std::fabs(slice[i + (size_t)axis] - 0.0f) > 1e-5f) planeFixed = false;
        check(planeFixed, "контур среза лежит в плоскости оси " + std::to_string(axis));
    }
    check(sliceOutlineLines({-1, -1, -1}, {1, 1, 1}, 0, -5.0f, {1, 1, 1}).size() == 48,
          "отрицательная позиция среза зажимается в [0;1]");
    check(sliceOutlineLines({-1, -1, -1}, {1, 1, 1}, 0, NAN, {1, 1, 1}).size() == 48,
          "NaN в позиции среза не ломает геометрию");

    // Стрелки сил: нет величин — нет линий; есть — по 2 вершины на вектор.
    const std::vector<float> none = liftDragLines({0, 0, 0}, {1, 0, 0}, 0.0f, {0, 1, 0}, 0.0f,
                                                  {0, 0, 1}, 0.0f, 2.0f);
    check(none.empty(), "при нулевых силах стрелки не рисуются");
    const std::vector<float> arrows = liftDragLines({1, 2, 3}, {1, 0, 0}, 5.0f, {0, 1, 0}, 7.0f,
                                                    {0, 0, 1}, 0.5f, 2.0f);
    check(arrows.size() == 3 * 2 * 6, "три стрелки — по две вершины на каждую");
    check(finiteAll(arrows), "координаты стрелок конечны");
    check(liftDragLines({NAN, 0, 0}, {1, 0, 0}, 1.0f, {0, 1, 0}, 1.0f, {0, 0, 1}, 1.0f, 2.0f).empty(),
          "NaN в центре давления не порождает линий");
    check(liftDragLines({0, 0, 0}, {1, 0, 0}, 1.0f, {0, 1, 0}, 1.0f, {0, 0, 1}, 1.0f, 0.0f).empty(),
          "нулевой габарит сцены не приводит к делению на ноль");
}

int main() {
    std::printf("=== Aeros Engine v1.33.0 — headless-тест Vulkan-бэкенда ===\n");
    std::printf("(GPU не требуется: проверяется логика выбора, раскладка данных,\n"
                " вкомпилированные шейдеры и геометрия сцены)\n");

    testExtent();
    testImageCountAndPresent();
    testFormat();
    testPolicy();
    testLayout();
    testSpirv();
    testSceneGeometry();

    std::printf("\n=== %s (проверок: %d, ошибок: %d) ===\n",
                g_failed == 0 ? "VULKAN-БЭКЕНД В ПОРЯДКЕ" : "ЕСТЬ ОШИБКИ", g_checks, g_failed);
    return g_failed == 0 ? 0 : 1;
}
