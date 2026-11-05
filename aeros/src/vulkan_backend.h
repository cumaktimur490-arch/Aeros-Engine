#pragma once
// =====================================================
// v1.33.0 — логика Vulkan-бэкенда, не требующая ни заголовков Vulkan, ни GPU.
//
// Здесь собрано всё, что в рендерере можно проверить headless-тестом:
//   - выбор размера swapchain (clampDuration по возможностям поверхности);
//   - выбор формата изображения и режима показа;
//   - правило «включать ли Vulkan» (наличие загрузчика, сборка с Vulkan,
//     найденное устройство, явный выбор пользователя);
//   - проверка корректности SPIR-V перед передачей драйверу (магия, версия,
//     границы идентификаторов, наличие точки входа нужной стадии и capability
//     Shader) — иначе ошибка создания конвейера выглядела бы как «чёрный экран»;
//   - раскладка вершин и размер push-констант (совпадение C++ структуры и
//     шейдера: рассинхрон даёт мусор вместо геометрии).
// Вынесено в отдельный заголовок сознательно: сам рендерер без GPU проверить
// нельзя, а эти правила — можно, и именно в них чаще всего ошибаются.
// =====================================================

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace vklogic {

// -----------------------------------------------------
// Геометрия и push-константы
// -----------------------------------------------------
// Вершина: позиция, нормаль, цвет (по 3 float). Тот же порядок объявлен в
// shaders/scene.vert (layout location 0/1/2).
constexpr int kVertexFloatCount = 9;
constexpr uint32_t kVertexStride = kVertexFloatCount * sizeof(float);  // 36
constexpr uint32_t kPosOffset = 0;
constexpr uint32_t kNormalOffset = 3 * sizeof(float);
constexpr uint32_t kColorOffset = 6 * sizeof(float);

// Push-константы: mat4 mvp + 3 vec4 = 112 байт. Гарантированный минимум
// Vulkan — 128 байт, поэтому запас есть, а лишнего расхода нет.
constexpr uint32_t kPushConstantBytes = 64 + 16 + 16 + 16;

// -----------------------------------------------------
// Размер swapchain
// -----------------------------------------------------
// По правилам Vulkan: если currentExtent.width != 0xFFFFFFFF, размер задаёт
// поверхность (окно), иначе берём размер кадрового буфера и зажимаем в
// [minImageExtent; maxImageExtent]. Ноль недопустим (свёрнутое окно) — тогда
// кадр пропускается.
inline bool vkChooseExtent(uint32_t fbWidth, uint32_t fbHeight,
                           uint32_t curWidth, uint32_t curHeight,
                           uint32_t minW, uint32_t minH,
                           uint32_t maxW, uint32_t maxH,
                           uint32_t* outW, uint32_t* outH) {
    if (!outW || !outH) return false;
    uint32_t w = 0, h = 0;
    if (curWidth != 0xFFFFFFFFu && curHeight != 0xFFFFFFFFu) {
        w = curWidth;
        h = curHeight;
    } else {
        // Окно свёрнуто: размер кадрового буфера нулевой. Зажимать его в
        // minImageExtent нельзя — получилось бы изображение 1x1, и кадр
        // рисовался бы «в никуда» вместо честного пропуска.
        if (fbWidth == 0 || fbHeight == 0) return false;
        w = fbWidth;
        h = fbHeight;
        if (w < minW) w = minW;
        if (h < minH) h = minH;
        if (maxW != 0 && w > maxW) w = maxW;
        if (maxH != 0 && h > maxH) h = maxH;
    }
    if (w == 0 || h == 0) return false;  // свёрнутое окно — кадр не рисуем
    *outW = w;
    *outH = h;
    return true;
}

// Сколько изображений брать в swapchain: возможностей меньше, чем хочется,
// не бывает — берём max(minImageCount + 1, желаемое), но не выше maxImageCount
// (0 означает «без ограничения»).
inline uint32_t vkImageCount(uint32_t minImageCount, uint32_t maxImageCount, uint32_t want) {
    uint32_t n = minImageCount + 1;
    if (n < want) n = want;
    if (maxImageCount != 0 && n > maxImageCount) n = maxImageCount;
    if (n < minImageCount) n = minImageCount;
    return n;
}

// -----------------------------------------------------
// Формат и режим показа
// -----------------------------------------------------
struct SurfaceFormat {
    int format;       // значение VkFormat (для тестов достаточно целого)
    int colorSpace;
};

// Числовые значения VkFormat (проверяются static_assert в vulkan_renderer.cpp,
// чтобы константы здесь не разошлись с заголовками Vulkan):
//   37 = VK_FORMAT_R8G8B8A8_UNORM     43 = VK_FORMAT_R8G8B8A8_SRGB
//   44 = VK_FORMAT_B8G8R8A8_UNORM     50 = VK_FORMAT_B8G8R8A8_SRGB
//    0 = VK_FORMAT_UNDEFINED (драйвер предлагает «любой» формат)
// Цветовое пространство 0 = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR.
constexpr int kFmtRgba8Unorm = 37;
constexpr int kFmtRgba8Srgb = 43;
constexpr int kFmtBgra8Unorm = 44;
constexpr int kFmtBgra8Srgb = 50;
constexpr int kColorSpaceSrgbNonlinear = 0;

// Нужен 8-битный формат с альфой (его понимает и интерфейс ImGui) и
// sRGB-пространство: без него картинка выглядит пересвеченной.
inline SurfaceFormat vkChooseFormat(const std::vector<SurfaceFormat>& available) {
    const int preferred[] = {kFmtBgra8Srgb, kFmtRgba8Srgb, kFmtBgra8Unorm, kFmtRgba8Unorm};
    for (int want : preferred)
        for (const SurfaceFormat& f : available)
            if (f.format == want && f.colorSpace == kColorSpaceSrgbNonlinear) return f;
    if (!available.empty()) {
        if (available[0].format == 0) return SurfaceFormat{kFmtBgra8Srgb, kColorSpaceSrgbNonlinear};
        return available[0];
    }
    return SurfaceFormat{kFmtBgra8Srgb, kColorSpaceSrgbNonlinear};
}

// Режимы показа: 0 = IMMEDIATE, 1 = MAILBOX, 2 = FIFO, 3 = FIFO_RELAXED.
// При включённой вертикальной синхронизации FIFO — обязательный минимум по
// спецификации; при выключенной предпочитаем MAILBOX (нет разрывов, нет
// ожидания), затем IMMEDIATE, и только потом FIFO.
inline int vkChoosePresentMode(const std::vector<int>& available, bool vsync) {
    auto has = [&available](int m) {
        for (int a : available)
            if (a == m) return true;
        return false;
    };
    if (vsync) {
        if (has(2)) return 2;
        return 2;  // FIFO обязан поддерживаться всегда
    }
    if (has(1)) return 1;
    if (has(0)) return 0;
    if (has(3)) return 3;
    return 2;
}

// -----------------------------------------------------
// Правило включения бэкенда
// -----------------------------------------------------
enum class VkDecision { UseVulkan, FallBackOpenGL };

struct VkPolicyInput {
    bool builtWithVulkan = false;  // заголовки были при сборке
    bool loaderPresent = false;    // vulkan-1.dll / libvulkan.so.1 найден
    bool deviceUsable = false;     // есть устройство с графикой и показом
    bool userAskedVulkan = false;  // renderer=vulkan / --vulkan
};

struct VkPolicyResult {
    VkDecision decision = VkDecision::FallBackOpenGL;
    std::string reason;
};

// Vulkan включается только когда есть сборка с Vulkan, загрузчик, пригодное
// устройство И пользователь явно попросил. Автовыбор оставлен на OpenGL
// сознательно: у части драйверов Vulkan-путь медленнее, и молча менять
// поведение после обновления нельзя.
inline VkPolicyResult vkPolicy(const VkPolicyInput& in) {
    VkPolicyResult r;
    if (!in.builtWithVulkan) {
        r.reason = "сборка без Vulkan (нет заголовков при сборке) — OpenGL";
        return r;
    }
    if (!in.loaderPresent) {
        r.reason = "загрузчик Vulkan не найден (vulkan-1.dll / libvulkan.so.1) — OpenGL";
        return r;
    }
    if (!in.deviceUsable) {
        r.reason = "нет устройства Vulkan с графической очередью и показом в окно — OpenGL";
        return r;
    }
    if (!in.userAskedVulkan) {
        r.reason = "Vulkan доступен, но не выбран (renderer=vulkan или --vulkan) — OpenGL";
        return r;
    }
    r.decision = VkDecision::UseVulkan;
    r.reason = "Vulkan выбран и доступен";
    return r;
}

// -----------------------------------------------------
// Проверка SPIR-V
// -----------------------------------------------------
// Минимальный разбор потока инструкций: магия 0x07230203, версия не выше 1.6,
// границы идентификаторов, длина кратна 4 байтам, есть OpCapability Shader и
// OpEntryPoint с нужной стадией исполнения, поток заканчивается OpFunctionEnd
// или достигнут конец (допускается обрыв только по концу массива).
// Возвращает true, если модуль выглядит пригодным для vkCreateShaderModule.
inline bool vkSpirvValid(const uint32_t* words, size_t count, bool wantFragment,
                         std::string* err = nullptr) {
    auto fail = [&](const char* m) {
        if (err) *err = m;
        return false;
    };
    if (!words) return fail("нет данных SPIR-V");
    if (count < 5) return fail("SPIR-V короче заголовка (5 слов)");
    if (words[0] != 0x07230203u) return fail("неверная магия SPIR-V");
    const uint32_t version = words[1];
    const uint32_t major = (version >> 16) & 0xFFu;
    if (major != 1) return fail("major-версия SPIR-V не 1");
    const uint32_t bound = words[3];
    if (bound == 0) return fail("нулевая граница идентификаторов");

    bool hasShader = false, hasEntry = false, hasMemoryModel = false;
    size_t i = 5;
    while (i < count) {
        const uint32_t head = words[i];
        const uint16_t op = (uint16_t)(head & 0xFFFFu);
        const uint16_t wc = (uint16_t)(head >> 16);
        if (wc == 0) return fail("нулевая длина инструкции");
        if (i + wc > count) return fail("инструкция выходит за границу модуля");
        if (op == 17 /*OpCapability*/ && wc >= 2 && words[i + 1] == 1) hasShader = true;
        if (op == 14 /*OpMemoryModel*/) hasMemoryModel = true;
        if (op == 15 /*OpEntryPoint*/ && wc >= 3) {
            const uint32_t model = words[i + 1];
            // 0 = Vertex, 4 = Fragment
            if ((wantFragment && model == 4) || (!wantFragment && model == 0)) hasEntry = true;
        }
        i += wc;
    }
    if (!hasShader) return fail("нет OpCapability Shader");
    if (!hasMemoryModel) return fail("нет OpMemoryModel");
    if (!hasEntry) return fail(wantFragment ? "нет точки входа фрагментного шейдера"
                                            : "нет точки входа вершинного шейдера");
    return true;
}

}  // namespace vklogic
