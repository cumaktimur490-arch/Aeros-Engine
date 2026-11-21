#include "vulkan_renderer.h"
#include "globals.h"
#include "stl_loader.h"
#include "proc_util.h"   // v1.36.0 — диалоги и glslc без shell

// Заголовки Vulkan лежат в репозитории (libs/vulkan/include), поэтому сборка с
// Vulkan не требует установленного Vulkan SDK. Флаг AEROS_HAS_VULKAN обычно
// приходит из сборочного скрипта; если его забыли — определяем сами по
// наличию заголовков, чтобы сборка не ломалась, а Vulkan-путь просто выключился.
#if !defined(AEROS_HAS_VULKAN) && defined(__has_include)
#  if __has_include(<vulkan/vulkan.h>)
#    define AEROS_HAS_VULKAN 1
#  endif
#endif

#include <glad/glad.h>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <vector>
#include <set>
#include <algorithm>
#include <fstream>
#include <chrono>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <cmath>
#include <sstream>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commdlg.h>
#else
#include <dlfcn.h>
#endif

#ifndef GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_NONE
#endif

// =====================================================
// Vulkan подключается всегда, но только заголовками из libs/vulkan/include.
// Символы библиотеки не нужны: всё берётся у системного загрузчика
// (см. vk_loader.h). Если заголовков при сборке не было — AEROS_HAS_VULKAN не
// определён, и Vulkan-путь честно выключен (ниже это видно по isVulkanAvailable).
// =====================================================
#if defined(AEROS_HAS_VULKAN)
#include "vk_loader.h"
#include "vulkan_backend.h"
#include "vulkan_scene_spv.h"
#include "imgui/imgui.h"
#include "imgui/imgui_impl_vulkan.h"

// Значения VkFormat проверяются здесь: если они разойдутся с заголовками
// Vulkan, компилятор остановит сборку, а не отдаст мусор на экран.
static_assert(vklogic::kFmtRgba8Unorm == (int)VK_FORMAT_R8G8B8A8_UNORM, "VkFormat R8G8B8A8_UNORM изменился");
static_assert(vklogic::kFmtRgba8Srgb  == (int)VK_FORMAT_R8G8B8A8_SRGB,  "VkFormat R8G8B8A8_SRGB изменился");
static_assert(vklogic::kFmtBgra8Unorm == (int)VK_FORMAT_B8G8R8A8_UNORM, "VkFormat B8G8R8A8_UNORM изменился");
static_assert(vklogic::kFmtBgra8Srgb  == (int)VK_FORMAT_B8G8R8A8_SRGB,  "VkFormat B8G8R8A8_SRGB изменился");
static_assert(vklogic::kColorSpaceSrgbNonlinear == (int)VK_COLOR_SPACE_SRGB_NONLINEAR_KHR, "VkColorSpaceKHR изменился");
static_assert(vklogic::kPushConstantBytes == sizeof(float) * (16 + 4 + 4 + 4), "размер push-констант не совпал с шейдером");
#endif

// GLFW подключается ПОСЛЕ заголовков Vulkan: только тогда glfw3.h объявляет
// glfwCreateWindowSurface и типы VkInstance. Со стороны OpenGL-ветки GL-функции
// идут через glad (GLFW_INCLUDE_NONE), поэтому порядок безопасен.
#include <GLFW/glfw3.h>

IRenderer* g_renderer = nullptr;
RendererAPI g_currentAPI = RendererAPI::OpenGL;
RendererConfig g_rendererConfig;

#if defined(AEROS_HAS_VULKAN)
// Устройство и инстанс — для загрузчика функций интерфейса (ImGui спрашивает
// адреса функций сам, ему нужен только способ их достать).
static VkInstance s_uiInstance = VK_NULL_HANDLE;
static VkDevice s_uiDevice = VK_NULL_HANDLE;

namespace vkrt {

// -----------------------------------------------------
// Вспомогательные функции
// -----------------------------------------------------
static uint32_t findMemoryType(VkPhysicalDevice pd, uint32_t typeBits, VkMemoryPropertyFlags want) {
    VkPhysicalDeviceMemoryProperties mp{};
    p_vkGetPhysicalDeviceMemoryProperties(pd, &mp);
    for (uint32_t i = 0; i < mp.memoryTypeCount; ++i) {
        if ((typeBits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & want) == want) return i;
    }
    return UINT32_MAX;
}

// Создать буфер и память под него. hostVisible — память, доступная процессору
// (для потоковой геометрии кадра); иначе видеопамять (для статичной модели).
static bool createBuffer(VkPhysicalDevice pd, VkDevice dev, VkDeviceSize size, VkBufferUsageFlags usage,
                         bool hostVisible, VkBuffer* outBuf, VkDeviceMemory* outMem) {
    VkBufferCreateInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bi.size = size;
    bi.usage = usage;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (p_vkCreateBuffer(dev, &bi, nullptr, outBuf) != VK_SUCCESS) return false;

    VkMemoryRequirements req{};
    p_vkGetBufferMemoryRequirements(dev, *outBuf, &req);
    VkMemoryAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize = req.size;
    uint32_t type = findMemoryType(pd, req.memoryTypeBits,
                                  hostVisible ? (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
                                              : VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (type == UINT32_MAX) {
        p_vkDestroyBuffer(dev, *outBuf, nullptr);
        *outBuf = VK_NULL_HANDLE;
        return false;
    }
    ai.memoryTypeIndex = type;
    if (p_vkAllocateMemory(dev, &ai, nullptr, outMem) != VK_SUCCESS) {
        p_vkDestroyBuffer(dev, *outBuf, nullptr);
        *outBuf = VK_NULL_HANDLE;
        return false;
    }
    if (p_vkBindBufferMemory(dev, *outBuf, *outMem, 0) != VK_SUCCESS) {
        p_vkFreeMemory(dev, *outMem, nullptr);
        p_vkDestroyBuffer(dev, *outBuf, nullptr);
        *outBuf = VK_NULL_HANDLE;
        *outMem = VK_NULL_HANDLE;
        return false;
    }
    return true;
}

static void destroyBuffer(VkDevice dev, VkBuffer* buf, VkDeviceMemory* mem) {
    if (*buf) { p_vkDestroyBuffer(dev, *buf, nullptr); *buf = VK_NULL_HANDLE; }
    if (*mem) { p_vkFreeMemory(dev, *mem, nullptr); *mem = VK_NULL_HANDLE; }
}

// Формат глубины: сначала пробуем 32-битный float, затем связку с трафаретом.
static int pickDepthFormat(VkPhysicalDevice pd) {
    const VkFormat candidates[] = {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT,
                                   VK_FORMAT_D24_UNORM_S8_UINT};
    for (VkFormat f : candidates) {
        VkFormatProperties props{};
        p_vkGetPhysicalDeviceFormatProperties(pd, f, &props);
        if (props.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) return (int)f;
    }
    return 0;
}

static VkShaderModule createShaderModule(VkDevice dev, const uint32_t* words, size_t count) {
    VkShaderModuleCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    ci.codeSize = count * sizeof(uint32_t);
    ci.pCode = words;
    VkShaderModule m = VK_NULL_HANDLE;
    if (p_vkCreateShaderModule(dev, &ci, nullptr, &m) != VK_SUCCESS) return VK_NULL_HANDLE;
    return m;
}

// Загрузчик функций для imgui_impl_vulkan: интерфейс сам решает, что ему нужно,
// а мы отдаём адреса через vkGetDeviceProcAddr / vkGetInstanceProcAddr.
static PFN_vkVoidFunction uiFuncLoader(const char* name, void*) {
    if (!name) return nullptr;
    PFN_vkVoidFunction f = nullptr;
    if (s_uiDevice && p_vkGetDeviceProcAddr) f = p_vkGetDeviceProcAddr(s_uiDevice, name);
    if (!f && s_uiInstance && p_vkGetInstanceProcAddr) f = p_vkGetInstanceProcAddr(s_uiInstance, name);
    return f;
}

}  // namespace vkrt
#endif  // AEROS_HAS_VULKAN

// =====================================================
// Проверка доступности Vulkan — без создания окна и без GPU-контекста
// =====================================================
// Практический смысл: ответ на вопрос «сможет ли эта сборка рисовать через
// Vulkan на этой машине». Загрузчик сам по себе ничего не значит (он есть почти
// везде), поэтому дополнительно создаётся инстанс и ищется устройство с
// графической очередью. Поверхность окна проверяется уже при инициализации
// рендерера — иначе пришлось бы создавать скрытое окно при каждом запуске.
struct VulkanProbe {
    bool loaderPresent = false;
    bool instanceOk = false;
    bool deviceUsable = false;
    uint32_t apiVersion = 0;
    int apiMajor = 0, apiMinor = 0;
    std::string deviceName;
    std::string driver;
    int vramMB = 0;
    std::string reason;
};

static VulkanProbe& vulkanProbe() {
    static VulkanProbe probe;
    static bool done = false;
    if (done) return probe;
    done = true;
#if defined(AEROS_HAS_VULKAN)
    std::string err;
    if (!aerosVkLoadGlobal(&err)) {
        probe.reason = "загрузчик Vulkan не найден: " + err;
        return probe;
    }
    probe.loaderPresent = true;

    uint32_t api = VK_API_VERSION_1_0;
    if (p_vkEnumerateInstanceVersion) {
        uint32_t v = 0;
        if (p_vkEnumerateInstanceVersion(&v) == VK_SUCCESS && v != 0) api = v;
    }
    probe.apiVersion = api;
    probe.apiMajor = (int)((api >> 22) & 0x7Fu);
    probe.apiMinor = (int)((api >> 12) & 0x3FFu);

    VkApplicationInfo app{};
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "Aeros Engine";
    app.applicationVersion = VK_MAKE_VERSION(1, 33, 0);
    app.pEngineName = "Aeros Engine";
    app.engineVersion = VK_MAKE_VERSION(1, 33, 0);
    app.apiVersion = api;

    VkInstanceCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ci.pApplicationInfo = &app;
    VkInstance inst = VK_NULL_HANDLE;
    if (p_vkCreateInstance(&ci, nullptr, &inst) != VK_SUCCESS) {
        probe.reason = "vkCreateInstance не удался";
        return probe;
    }
    probe.instanceOk = true;
    // Инстанс пробы создан без оконных расширений, поэтому функции поверхности
    // не обязательны (requireSurface = false).
    if (!aerosVkLoadInstance(inst, false)) {
        probe.reason = "не загрузились функции уровня инстанса";
        if (p_vkDestroyInstance) p_vkDestroyInstance(inst, nullptr);
        return probe;
    }

    uint32_t count = 0;
    if (p_vkEnumeratePhysicalDevices(inst, &count, nullptr) != VK_SUCCESS || count == 0) {
        probe.reason = "в системе нет устройств Vulkan";
        p_vkDestroyInstance(inst, nullptr);
        return probe;
    }
    std::vector<VkPhysicalDevice> devices(count);
    p_vkEnumeratePhysicalDevices(inst, &count, devices.data());

    for (VkPhysicalDevice pd : devices) {
        uint32_t qCount = 0;
        p_vkGetPhysicalDeviceQueueFamilyProperties(pd, &qCount, nullptr);
        if (qCount == 0) continue;
        std::vector<VkQueueFamilyProperties> qs(qCount);
        p_vkGetPhysicalDeviceQueueFamilyProperties(pd, &qCount, qs.data());
        bool graphics = false;
        for (const auto& q : qs)
            if (q.queueFlags & VK_QUEUE_GRAPHICS_BIT) graphics = true;
        if (!graphics) continue;

        // Устройство с графикой нашли — запоминаем сведения о нём.
        VkPhysicalDeviceProperties props{};
        p_vkGetPhysicalDeviceProperties(pd, &props);
        probe.deviceName = props.deviceName;
        char buf[128];
        std::snprintf(buf, sizeof(buf), "Vulkan %u.%u.%u, драйвер %u.%u.%u",
                      VK_VERSION_MAJOR(props.apiVersion), VK_VERSION_MINOR(props.apiVersion),
                      VK_VERSION_PATCH(props.apiVersion),
                      VK_VERSION_MAJOR(props.driverVersion), VK_VERSION_MINOR(props.driverVersion),
                      VK_VERSION_PATCH(props.driverVersion));
        probe.driver = buf;
        VkPhysicalDeviceMemoryProperties mp{};
        p_vkGetPhysicalDeviceMemoryProperties(pd, &mp);
        uint64_t local = 0;
        for (uint32_t i = 0; i < mp.memoryHeapCount; ++i)
            if (mp.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) local += mp.memoryHeaps[i].size;
        probe.vramMB = (int)(local / (1024ull * 1024ull));
        probe.deviceUsable = true;
        break;
    }
    if (!probe.deviceUsable) probe.reason = "нет устройства Vulkan с графической очередью";
    else probe.reason = "Vulkan доступен: " + probe.deviceName;
    p_vkDestroyInstance(inst, nullptr);
#else
    probe.reason = "движок собран без заголовков Vulkan";
#endif
    return probe;
}

bool vulkanLoaderPresent() {
#if defined(AEROS_HAS_VULKAN)
    return vulkanProbe().loaderPresent;
#else
    return false;
#endif
}

bool isVulkanAvailable() {
    // v1.33.0 — рендерер реализован, поэтому ответ честный: нужен работающий
    // загрузчик и устройство с графикой. Поверхность окна проверяется при init.
    return vulkanProbe().deviceUsable;
}

bool initVulkanLoader() {
    // Загрузчик открывается лениво при первом обращении; здесь только проверка.
    return vulkanLoaderPresent();
}

std::string getVulkanDecisionReason() {
    return vulkanProbe().reason;
}

std::string getVulkanVersionString() {
    const VulkanProbe& probe = vulkanProbe();
    if (!probe.loaderPresent) return "Vulkan: загрузчик не найден";
    if (!probe.deviceUsable) return "Vulkan: загрузчик есть, устройство не найдено (" + probe.reason + ")";
    return "Vulkan " + std::to_string(probe.apiMajor) + "." + std::to_string(probe.apiMinor) +
           " — " + probe.deviceName +
           (probe.vramMB > 0 ? (" (" + std::to_string(probe.vramMB) + " МБ)") : "");
}

std::vector<std::string> getVulkanExtensions() {
    std::vector<std::string> exts;
#if defined(AEROS_HAS_VULKAN)
    if (!vulkanProbe().loaderPresent) return exts;
    uint32_t count = 0;
    if (p_vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr) != VK_SUCCESS) return exts;
    std::vector<VkExtensionProperties> props(count);
    if (p_vkEnumerateInstanceExtensionProperties(nullptr, &count, props.data()) != VK_SUCCESS) return exts;
    for (const auto& p : props) exts.emplace_back(p.extensionName);
#endif
    return exts;
}

std::vector<std::string> getVulkanDevices() {
    std::vector<std::string> devices;
#if defined(AEROS_HAS_VULKAN)
    const VulkanProbe& probe = vulkanProbe();
    if (!probe.deviceUsable) return devices;
    devices.push_back(probe.deviceName);
#endif
    return devices;
}

bool hasGlslc() {
    // glslc нужен только разработчику шейдеров: в поставку вкомпилирован SPIR-V
    // (aeros/src/vulkan_scene_spv.h), поэтому на машине пользователя его нет.
    //
    // v1.36.0 — без shell: на Windows ищем через SearchPathA, на остальных
    // платформах спрашиваем сам glslc (`glslc --version`), а не `which`:
    // так проверяется именно возможность запустить нужную программу, а не
    // наличие одноимённого файла где-то в PATH.
#ifdef _WIN32
    char found[MAX_PATH] = {0};
    return SearchPathA(nullptr, "glslc.exe", nullptr,
                       (DWORD)sizeof(found), found, nullptr) > 0;
#else
    return procutil::run({"glslc", "--version"}).ok;
#endif
}

std::vector<uint32_t> compileGLSLtoSPIRV(const std::string& glsl, const std::string& stage) {
    // Если glslc есть — компилируем на месте (для правки шейдеров без пересборки).
    // Иначе возвращаем пусто: рендерер берёт вкомпилированный SPIR-V.
    std::vector<uint32_t> spirv;
    if (!hasGlslc()) return spirv;

    std::string tmpGLSL = "/tmp/aeros_shader_" + stage + ".glsl";
    std::string tmpSPV = "/tmp/aeros_shader_" + stage + ".spv";
#ifdef _WIN32
    tmpGLSL = "aeros_shader_" + stage + ".glsl";
    tmpSPV = "aeros_shader_" + stage + ".spv";
#endif
    std::ofstream f(tmpGLSL);
    if (!f) return spirv;
    f << glsl;
    f.close();

    // v1.36.0 — argv вместо командной строки. Все элементы здесь наши
    // (временные пути в /tmp и имя стадии из фиксированного набора), но и
    // случайно сломанное имя не должно превращаться в команду оболочки.
#ifdef _WIN32
    // Windows: exec-путь procutil не поддерживается (см. proc_util.h);
    // строка целиком построена из констант и путей, созданных этой функцией.
    std::string cmd = "glslc -fshader-stage=" + stage + " " + tmpGLSL + " -o " + tmpSPV + " 2>&1";
    if (std::system(cmd.c_str()) != 0) return spirv;
#else
    if (!procutil::run({"glslc", "-fshader-stage=" + stage, tmpGLSL, "-o", tmpSPV}).ok)
        return spirv;
#endif

    std::ifstream spvFile(tmpSPV, std::ios::binary | std::ios::ate);
    if (!spvFile) return spirv;
    size_t size = (size_t)spvFile.tellg();
    spvFile.seekg(0, std::ios::beg);
    spirv.resize(size / 4);
    spvFile.read((char*)spirv.data(), size);
    return spirv;
}

// =====================================================
// OpenGL renderer (обёртка над существующим кодом отрисовки в main.cpp)
// =====================================================
bool OpenGLRenderer::init(GLFWwindow* window, const RendererConfig& cfg) {
    win = window;
    initialized = true;
    std::cout << "[Renderer] OpenGL инициализирован — " << getAPIName() << std::endl;
    return true;
}
void OpenGLRenderer::shutdown() { initialized = false; }
bool OpenGLRenderer::beginFrame() { return true; }
void OpenGLRenderer::endFrame() { return; }
void OpenGLRenderer::resize(int w, int h) { glViewport(0, 0, w, h); }
void OpenGLRenderer::clearColor(glm::vec3 color) {
    glClearColor(color.r, color.g, color.b, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}
void OpenGLRenderer::drawModel(const std::vector<float>& vertices, const std::vector<float>& normals,
                               const std::vector<float>& colors, const glm::mat4& mvp) {
    // Отрисовка OpenGL-ветки выполняется в main.cpp собственными шейдерами.
    (void)vertices; (void)normals; (void)colors; (void)mvp;
}
void OpenGLRenderer::drawParticles(const std::vector<float>& positions, const std::vector<float>& colors,
                                   float pointSize, int count) {
    (void)positions; (void)colors; (void)pointSize; (void)count;
}
void OpenGLRenderer::drawLines(const std::vector<float>& vertices, glm::vec3 color, float width) {
    (void)vertices; (void)color; (void)width;
}

// =====================================================
// Vulkan renderer — рабочая реализация
// =====================================================
#if defined(AEROS_HAS_VULKAN)

namespace {

static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT /*types*/,
    const VkDebugUtilsMessengerCallbackDataEXT* data, void* /*user*/) {
    if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT && data && data->pMessage)
        std::cerr << "[Vulkan validation] " << data->pMessage << std::endl;
    return VK_FALSE;
}

static bool validationLayerAvailable() {
    if (!p_vkEnumerateInstanceLayerProperties) return false;
    uint32_t count = 0;
    if (p_vkEnumerateInstanceLayerProperties(&count, nullptr) != VK_SUCCESS || count == 0) return false;
    std::vector<VkLayerProperties> props(count);
    if (p_vkEnumerateInstanceLayerProperties(&count, props.data()) != VK_SUCCESS) return false;
    for (const auto& p : props)
        if (std::strcmp(p.layerName, "VK_LAYER_KHRONOS_validation") == 0) return true;
    return false;
}

// Есть ли у устройства расширение (например, VK_KHR_swapchain).
static bool deviceHasExtension(VkPhysicalDevice pd, const char* name) {
    uint32_t count = 0;
    if (p_vkEnumerateDeviceExtensionProperties(pd, nullptr, &count, nullptr) != VK_SUCCESS) return false;
    std::vector<VkExtensionProperties> props(count);
    if (p_vkEnumerateDeviceExtensionProperties(pd, nullptr, &count, props.data()) != VK_SUCCESS) return false;
    for (const auto& p : props)
        if (std::strcmp(p.extensionName, name) == 0) return true;
    return false;
}

}  // namespace

bool VulkanRenderer::init(GLFWwindow* window, const RendererConfig& cfg) {
    win = window;
    config = cfg;
    if (!win) { lastError = "нет окна"; return false; }

    // Окно должно быть создано без контекста OpenGL (GLFW_NO_API): иначе
    // поверхность Vulkan создать нельзя, и лучше сразу сказать это честно.
    if (glfwGetWindowAttrib(win, GLFW_CLIENT_API) != GLFW_NO_API) {
        lastError = "окно создано с OpenGL-контекстом, поверхность Vulkan невозможна";
        return false;
    }
    if (!isVulkanAvailable()) {
        lastError = "Vulkan недоступен: " + getVulkanDecisionReason();
        return false;
    }

    if (!createInstance()) return false;
    if (!pickPhysicalDevice()) return false;
    if (!createLogicalDevice()) return false;
    if (!createSwapchain()) return false;
    if (!createRenderPass()) return false;
    if (!createFramebuffers()) return false;
    if (!createCommandPool()) return false;
    if (!createCommandBuffers()) return false;
    if (!createSyncObjects()) return false;
    if (!createPipelines()) return false;
    if (!createBuffers()) return false;

    initialized = true;
    s_uiInstance = (VkInstance)instance;
    s_uiDevice = (VkDevice)device;
    std::cout << "[Vulkan] Инициализирован: " << deviceName << " | " << driverVersion
              << " | " << vramMB << " МБ | " << extentW << "x" << extentH
              << " | изображений в swapchain: " << swapchainImageViews.size() << std::endl;
    return true;
}

bool VulkanRenderer::createInstance() {
    uint32_t extCount = 0;
    const char** glfwExts = glfwGetRequiredInstanceExtensions(&extCount);
    if (!glfwExts || extCount == 0) {
        lastError = "GLFW не сообщил требуемые расширения Vulkan (нужен GLFW с поддержкой Vulkan)";
        return false;
    }
    std::vector<const char*> exts(glfwExts, glfwExts + extCount);
    const bool wantValidation = config.enableValidation && validationLayerAvailable();
    if (config.enableValidation && !wantValidation)
        std::cout << "[Vulkan] Слой VK_LAYER_KHRONOS_validation не найден — работаем без него" << std::endl;
    if (wantValidation) {
        exts.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }

    instanceApiVersion = VK_API_VERSION_1_0;
    if (p_vkEnumerateInstanceVersion) {
        uint32_t v = 0;
        if (p_vkEnumerateInstanceVersion(&v) == VK_SUCCESS && v != 0) {
            // Выше 1.3 движку ничего не нужно: просим ровно 1.3 (или ниже, если
            // загрузчик старее) — это совместимо со старыми драйверами.
            instanceApiVersion = v < VK_API_VERSION_1_3 ? v : VK_API_VERSION_1_3;
        }
    }

    VkApplicationInfo app{};
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "Aeros Engine";
    app.applicationVersion = VK_MAKE_VERSION(1, 33, 0);
    app.pEngineName = "Aeros Engine";
    app.engineVersion = VK_MAKE_VERSION(1, 33, 0);
    app.apiVersion = instanceApiVersion;

    VkInstanceCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ci.pApplicationInfo = &app;
    ci.enabledExtensionCount = (uint32_t)exts.size();
    ci.ppEnabledExtensionNames = exts.data();
    const char* layers[1] = {"VK_LAYER_KHRONOS_validation"};
    if (wantValidation) {
        ci.enabledLayerCount = 1;
        ci.ppEnabledLayerNames = layers;
    }

    if (p_vkCreateInstance(&ci, nullptr, (VkInstance*)&instance) != VK_SUCCESS || !instance) {
        lastError = "vkCreateInstance не удался";
        instance = nullptr;
        return false;
    }
    if (!aerosVkLoadInstance((VkInstance)instance)) {
        lastError = "не загрузились функции уровня инстанса";
        return false;
    }

    if (wantValidation) {
        auto createMsg = (PFN_vkCreateDebugUtilsMessengerEXT)
            p_vkGetInstanceProcAddr((VkInstance)instance, "vkCreateDebugUtilsMessengerEXT");
        if (createMsg) {
            VkDebugUtilsMessengerCreateInfoEXT mi{};
            mi.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
            mi.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                 VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
            mi.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                             VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                             VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
            mi.pfnUserCallback = debugCallback;
            if (createMsg((VkInstance)instance, &mi, nullptr, (VkDebugUtilsMessengerEXT*)&debugMessenger) != VK_SUCCESS)
                debugMessenger = nullptr;
        }
    }

    if (glfwCreateWindowSurface((VkInstance)instance, win, nullptr, (VkSurfaceKHR*)&surface) != VK_SUCCESS || !surface) {
        lastError = "glfwCreateWindowSurface не удался (окно без поддержки Vulkan?)";
        surface = nullptr;
        return false;
    }
    return true;
}

bool VulkanRenderer::pickPhysicalDevice() {
    uint32_t count = 0;
    if (p_vkEnumeratePhysicalDevices((VkInstance)instance, &count, nullptr) != VK_SUCCESS || count == 0) {
        lastError = "в системе нет устройств Vulkan";
        return false;
    }
    std::vector<VkPhysicalDevice> devices(count);
    p_vkEnumeratePhysicalDevices((VkInstance)instance, &count, devices.data());

    int bestScore = -1;
    for (VkPhysicalDevice pd : devices) {
        if (!deviceHasExtension(pd, VK_KHR_SWAPCHAIN_EXTENSION_NAME)) continue;

        uint32_t qCount = 0;
        p_vkGetPhysicalDeviceQueueFamilyProperties(pd, &qCount, nullptr);
        if (qCount == 0) continue;
        std::vector<VkQueueFamilyProperties> qs(qCount);
        p_vkGetPhysicalDeviceQueueFamilyProperties(pd, &qCount, qs.data());

        int gfx = -1, present = -1;
        for (uint32_t i = 0; i < qCount; ++i) {
            if ((qs[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && gfx < 0) gfx = (int)i;
            VkBool32 supported = VK_FALSE;
            p_vkGetPhysicalDeviceSurfaceSupportKHR(pd, i, (VkSurfaceKHR)surface, &supported);
            if (supported && present < 0) present = (int)i;
        }
        if (gfx < 0 || present < 0) continue;

        // Простой вес: дискретная карта предпочтительнее встроенной.
        VkPhysicalDeviceProperties props{};
        p_vkGetPhysicalDeviceProperties(pd, &props);
        int score = props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 1000 : 100;
        if (score <= bestScore) continue;

        bestScore = score;
        physicalDevice = pd;
        graphicsFamily = gfx;
        presentFamily = present;
        deviceName = props.deviceName;
        char buf[160];
        std::snprintf(buf, sizeof(buf), "Vulkan %u.%u.%u, драйвер %u.%u.%u",
                      VK_VERSION_MAJOR(props.apiVersion), VK_VERSION_MINOR(props.apiVersion),
                      VK_VERSION_PATCH(props.apiVersion),
                      VK_VERSION_MAJOR(props.driverVersion), VK_VERSION_MINOR(props.driverVersion),
                      VK_VERSION_PATCH(props.driverVersion));
        driverVersion = buf;
    }

    if (!physicalDevice) {
        lastError = "нет устройства Vulkan с графической очередью и поддержкой вывода в окно";
        return false;
    }

    VkPhysicalDeviceMemoryProperties mp{};
    p_vkGetPhysicalDeviceMemoryProperties((VkPhysicalDevice)physicalDevice, &mp);
    uint64_t local = 0;
    for (uint32_t i = 0; i < mp.memoryHeapCount; ++i)
        if (mp.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) local += mp.memoryHeaps[i].size;
    vramMB = (int)(local / (1024ull * 1024ull));
    return true;
}

bool VulkanRenderer::createLogicalDevice() {
    // Одна или две очереди: если графическая семья умеет и вывод в окно —
    // хватит одной (так быстрее и проще синхронизировать).
    std::set<int> families = {(int)graphicsFamily, (int)presentFamily};
    std::vector<VkDeviceQueueCreateInfo> qcis;
    float priority = 1.0f;
    for (int fam : families) {
        VkDeviceQueueCreateInfo qci{};
        qci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        qci.queueFamilyIndex = (uint32_t)fam;
        qci.queueCount = 1;
        qci.pQueuePriorities = &priority;
        qcis.push_back(qci);
    }

    // Широкие линии и крупные точки — не обязательные возможности: включаем,
    // если устройство их умеет, иначе рисуем линиями в 1 пиксель / точками 1 px.
    VkPhysicalDeviceFeatures supported{};
    p_vkGetPhysicalDeviceFeatures((VkPhysicalDevice)physicalDevice, &supported);
    hasWideLines = supported.wideLines == VK_TRUE;
    hasLargePoints = supported.largePoints == VK_TRUE;

    VkPhysicalDeviceFeatures enabled{};
    enabled.wideLines = supported.wideLines;
    enabled.largePoints = supported.largePoints;

    const char* devExts[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    VkDeviceCreateInfo dci{};
    dci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    dci.queueCreateInfoCount = (uint32_t)qcis.size();
    dci.pQueueCreateInfos = qcis.data();
    dci.enabledExtensionCount = 1;
    dci.ppEnabledExtensionNames = devExts;
    dci.pEnabledFeatures = &enabled;

    if (p_vkCreateDevice((VkPhysicalDevice)physicalDevice, &dci, nullptr, (VkDevice*)&device) != VK_SUCCESS || !device) {
        lastError = "vkCreateDevice не удался";
        device = nullptr;
        return false;
    }
    if (!aerosVkLoadDevice((VkDevice)device)) {
        lastError = "не загрузились функции уровня устройства";
        return false;
    }
    p_vkGetDeviceQueue((VkDevice)device, (uint32_t)graphicsFamily, 0, (VkQueue*)&graphicsQueue);
    p_vkGetDeviceQueue((VkDevice)device, (uint32_t)presentFamily, 0, (VkQueue*)&presentQueue);
    if (!graphicsQueue) {
        lastError = "не удалось получить графическую очередь";
        return false;
    }
    if (!presentQueue) presentQueue = graphicsQueue;
    return true;
}

bool VulkanRenderer::createSwapchain() {
    VkPhysicalDevice pd = (VkPhysicalDevice)physicalDevice;
    VkSurfaceCapabilitiesKHR caps{};
    if (p_vkGetPhysicalDeviceSurfaceCapabilitiesKHR(pd, (VkSurfaceKHR)surface, &caps) != VK_SUCCESS) {
        lastError = "не удалось получить возможности поверхности";
        return false;
    }

    int fbW = 0, fbH = 0;
    glfwGetFramebufferSize(win, &fbW, &fbH);
    if (fbW <= 0 || fbH <= 0) { fbW = 1280; fbH = 720; }

    uint32_t w = 0, h = 0;
    if (!vklogic::vkChooseExtent((uint32_t)fbW, (uint32_t)fbH,
                                 caps.currentExtent.width, caps.currentExtent.height,
                                 caps.minImageExtent.width, caps.minImageExtent.height,
                                 caps.maxImageExtent.width, caps.maxImageExtent.height, &w, &h)) {
        lastError = "некорректный размер поверхности (окно свёрнуто?)";
        return false;
    }

    uint32_t formatCount = 0;
    p_vkGetPhysicalDeviceSurfaceFormatsKHR(pd, (VkSurfaceKHR)surface, &formatCount, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    if (formatCount > 0)
        p_vkGetPhysicalDeviceSurfaceFormatsKHR(pd, (VkSurfaceKHR)surface, &formatCount, formats.data());
    std::vector<vklogic::SurfaceFormat> logicalFormats;
    logicalFormats.reserve(formats.size());
    for (const auto& f : formats) logicalFormats.push_back({(int)f.format, (int)f.colorSpace});
    const vklogic::SurfaceFormat chosen = vklogic::vkChooseFormat(logicalFormats);

    uint32_t modeCount = 0;
    p_vkGetPhysicalDeviceSurfacePresentModesKHR(pd, (VkSurfaceKHR)surface, &modeCount, nullptr);
    std::vector<VkPresentModeKHR> modes(modeCount);
    if (modeCount > 0)
        p_vkGetPhysicalDeviceSurfacePresentModesKHR(pd, (VkSurfaceKHR)surface, &modeCount, modes.data());
    std::vector<int> logicalModes;
    logicalModes.reserve(modes.size());
    for (auto m : modes) logicalModes.push_back((int)m);
    const int presentMode = vklogic::vkChoosePresentMode(logicalModes, config.vsync);

    const uint32_t images = vklogic::vkImageCount(caps.minImageCount, caps.maxImageCount, 3);
    minImageCount = caps.minImageCount;

    VkCompositeAlphaFlagBitsKHR alpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    if (!(caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR)) {
        if (caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR)
            alpha = VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
        else if (caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR)
            alpha = VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR;
        else
            alpha = VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR;
    }

    VkSwapchainCreateInfoKHR ci{};
    ci.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    ci.surface = (VkSurfaceKHR)surface;
    ci.minImageCount = images;
    ci.imageFormat = (VkFormat)chosen.format;
    ci.imageColorSpace = (VkColorSpaceKHR)chosen.colorSpace;
    ci.imageExtent.width = w;
    ci.imageExtent.height = h;
    ci.imageArrayLayers = 1;
    ci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    const uint32_t families[2] = {(uint32_t)graphicsFamily, (uint32_t)presentFamily};
    if (graphicsFamily != presentFamily) {
        ci.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        ci.queueFamilyIndexCount = 2;
        ci.pQueueFamilyIndices = families;
    } else {
        ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }
    ci.preTransform = caps.currentTransform;
    ci.compositeAlpha = alpha;
    ci.presentMode = (VkPresentModeKHR)presentMode;
    ci.clipped = VK_TRUE;
    ci.oldSwapchain = (VkSwapchainKHR)swapchain;

    VkSwapchainKHR newSwapchain = VK_NULL_HANDLE;
    if (p_vkCreateSwapchainKHR((VkDevice)device, &ci, nullptr, &newSwapchain) != VK_SUCCESS) {
        lastError = "vkCreateSwapchainKHR не удался";
        return false;
    }
    // Старая цепочка уничтожается только после успешного создания новой.
    if (swapchain) {
        for (void* fb : framebuffers) p_vkDestroyFramebuffer((VkDevice)device, (VkFramebuffer)fb, nullptr);
        framebuffers.clear();
        for (void* v : swapchainImageViews) p_vkDestroyImageView((VkDevice)device, (VkImageView)v, nullptr);
        swapchainImageViews.clear();
        p_vkDestroySwapchainKHR((VkDevice)device, (VkSwapchainKHR)swapchain, nullptr);
    }
    swapchain = newSwapchain;
    swapchainFormat = chosen.format;
    extentW = w;
    extentH = h;
    width = (int)w;
    height = (int)h;

    uint32_t imageCount = 0;
    p_vkGetSwapchainImagesKHR((VkDevice)device, (VkSwapchainKHR)swapchain, &imageCount, nullptr);
    swapchainImages.assign(imageCount, nullptr);
    if (imageCount == 0 ||
        p_vkGetSwapchainImagesKHR((VkDevice)device, (VkSwapchainKHR)swapchain, &imageCount, (VkImage*)swapchainImages.data()) != VK_SUCCESS) {
        lastError = "не удалось получить изображения swapchain";
        return false;
    }
    swapchainImages.resize(imageCount);
    swapchainImageViews.resize(imageCount, nullptr);

    for (uint32_t i = 0; i < imageCount; ++i) {
        VkImageViewCreateInfo vi{};
        vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vi.image = (VkImage)swapchainImages[i];
        vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vi.format = (VkFormat)swapchainFormat;
        vi.components = {VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
                         VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY};
        vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        if (p_vkCreateImageView((VkDevice)device, &vi, nullptr, (VkImageView*)&swapchainImageViews[i]) != VK_SUCCESS) {
            lastError = "не удалось создать VkImageView для изображения swapchain";
            return false;
        }
    }

    // Буфер глубины: без него модель рисуется «плоско», а порядок треугольников
    // определяет картинку. Формат выбираем по поддержке драйвером.
    depthFormat = vkrt::pickDepthFormat(pd);
    if (depthFormat == 0) {
        lastError = "у устройства нет формата глубины для вывода в окно";
        return false;
    }
    VkImageCreateInfo di{};
    di.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    di.imageType = VK_IMAGE_TYPE_2D;
    di.format = (VkFormat)depthFormat;
    di.extent.width = w;
    di.extent.height = h;
    di.extent.depth = 1;
    di.mipLevels = 1;
    di.arrayLayers = 1;
    di.samples = VK_SAMPLE_COUNT_1_BIT;
    di.tiling = VK_IMAGE_TILING_OPTIMAL;
    di.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    di.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    di.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (p_vkCreateImage((VkDevice)device, &di, nullptr, (VkImage*)&depthImage) != VK_SUCCESS) {
        lastError = "не удалось создать изображение глубины";
        return false;
    }
    VkMemoryRequirements req{};
    p_vkGetImageMemoryRequirements((VkDevice)device, (VkImage)depthImage, &req);
    VkMemoryAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize = req.size;
    const uint32_t type = vkrt::findMemoryType(pd, req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (type == UINT32_MAX) {
        lastError = "нет памяти под буфер глубины";
        return false;
    }
    ai.memoryTypeIndex = type;
    if (p_vkAllocateMemory((VkDevice)device, &ai, nullptr, (VkDeviceMemory*)&depthImageMemory) != VK_SUCCESS ||
        p_vkBindImageMemory((VkDevice)device, (VkImage)depthImage, (VkDeviceMemory)depthImageMemory, 0) != VK_SUCCESS) {
        lastError = "не удалось выделить память под буфер глубины";
        return false;
    }
    VkImageViewCreateInfo dvi{};
    dvi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    dvi.image = (VkImage)depthImage;
    dvi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    dvi.format = (VkFormat)depthFormat;
    dvi.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
    if (p_vkCreateImageView((VkDevice)device, &dvi, nullptr, (VkImageView*)&depthImageView) != VK_SUCCESS) {
        lastError = "не удалось создать VkImageView глубины";
        return false;
    }
    return true;
}

bool VulkanRenderer::createRenderPass() {
    // Прежний render pass мог быть создан для других форматов (смена режима
    // окна или HDR): его надо освободить, иначе утечка и «висящий» объект.
    if (renderPass) {
        p_vkDestroyRenderPass((VkDevice)device, (VkRenderPass)renderPass, nullptr);
        renderPass = nullptr;
    }
    VkAttachmentDescription attachments[2]{};
    attachments[0].format = (VkFormat)swapchainFormat;
    attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[0].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    attachments[1].format = (VkFormat)depthFormat;
    attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkAttachmentReference depthRef{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorRef;
    subpass.pDepthStencilAttachment = &depthRef;

    VkSubpassDependency dep{};
    dep.srcSubpass = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass = 0;
    dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dep.srcAccessMask = 0;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo rp{};
    rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rp.attachmentCount = 2;
    rp.pAttachments = attachments;
    rp.subpassCount = 1;
    rp.pSubpasses = &subpass;
    rp.dependencyCount = 1;
    rp.pDependencies = &dep;

    if (p_vkCreateRenderPass((VkDevice)device, &rp, nullptr, (VkRenderPass*)&renderPass) != VK_SUCCESS) {
        lastError = "vkCreateRenderPass не удался";
        return false;
    }
    return true;
}

bool VulkanRenderer::createFramebuffers() {
    framebuffers.assign(swapchainImageViews.size(), nullptr);
    for (size_t i = 0; i < swapchainImageViews.size(); ++i) {
        VkImageView views[2] = {(VkImageView)swapchainImageViews[i], (VkImageView)depthImageView};
        VkFramebufferCreateInfo fi{};
        fi.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fi.renderPass = (VkRenderPass)renderPass;
        fi.attachmentCount = 2;
        fi.pAttachments = views;
        fi.width = extentW;
        fi.height = extentH;
        fi.layers = 1;
        if (p_vkCreateFramebuffer((VkDevice)device, &fi, nullptr, (VkFramebuffer*)&framebuffers[i]) != VK_SUCCESS) {
            lastError = "vkCreateFramebuffer не удался";
            return false;
        }
    }
    return true;
}

bool VulkanRenderer::createCommandPool() {
    VkCommandPoolCreateInfo pi{};
    pi.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    // RESET_COMMAND_BUFFER: командный буфер кадра перезаписывается каждый кадр.
    pi.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pi.queueFamilyIndex = (uint32_t)graphicsFamily;
    if (p_vkCreateCommandPool((VkDevice)device, &pi, nullptr, (VkCommandPool*)&commandPool) != VK_SUCCESS) {
        lastError = "vkCreateCommandPool не удался";
        return false;
    }
    return true;
}

bool VulkanRenderer::createCommandBuffers() {
    commandBuffers.assign(kFramesInFlight, nullptr);
    VkCommandBufferAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool = (VkCommandPool)commandPool;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = (uint32_t)commandBuffers.size();
    if (p_vkAllocateCommandBuffers((VkDevice)device, &ai, (VkCommandBuffer*)commandBuffers.data()) != VK_SUCCESS) {
        lastError = "vkAllocateCommandBuffers не удался";
        return false;
    }
    return true;
}

bool VulkanRenderer::createSyncObjects() {
    imageAvailableSemaphores.assign(kFramesInFlight, nullptr);
    inFlightFences.assign(kFramesInFlight, nullptr);

    VkSemaphoreCreateInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    VkFenceCreateInfo fi{};
    fi.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    // SIGNALED: первый кадр не должен ждать несуществующую работу.
    fi.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (int i = 0; i < kFramesInFlight; ++i) {
        if (p_vkCreateSemaphore((VkDevice)device, &si, nullptr, (VkSemaphore*)&imageAvailableSemaphores[i]) != VK_SUCCESS ||
            p_vkCreateFence((VkDevice)device, &fi, nullptr, (VkFence*)&inFlightFences[i]) != VK_SUCCESS) {
            lastError = "не удалось создать средства синхронизации кадров";
            return false;
        }
    }
    // Семафор готовности кадра — свой у каждого изображения swapchain: иначе
    // при повторном использовании изображения возможен конфликт ожиданий.
    renderFinishedSemaphores.assign(swapchainImageViews.size(), nullptr);
    for (size_t i = 0; i < renderFinishedSemaphores.size(); ++i) {
        if (p_vkCreateSemaphore((VkDevice)device, &si, nullptr, (VkSemaphore*)&renderFinishedSemaphores[i]) != VK_SUCCESS) {
            lastError = "не удалось создать семафор готовности кадра";
            return false;
        }
    }
    return true;
}

void VulkanRenderer::destroyPipelines() {
    if (!device) return;
    VkDevice dev = (VkDevice)device;
    if (graphicsPipeline) p_vkDestroyPipeline(dev, (VkPipeline)graphicsPipeline, nullptr);
    if (particlePipeline) p_vkDestroyPipeline(dev, (VkPipeline)particlePipeline, nullptr);
    if (linePipeline) p_vkDestroyPipeline(dev, (VkPipeline)linePipeline, nullptr);
    graphicsPipeline = particlePipeline = linePipeline = nullptr;
    if (pipelineLayout) p_vkDestroyPipelineLayout(dev, (VkPipelineLayout)pipelineLayout, nullptr);
    pipelineLayout = nullptr;
}

bool VulkanRenderer::createPipelines() {
    // Модули шейдеров: SPIR-V вкомпилирован (vulkan_scene_spv.h), рядом с exe
    // ничего не нужно. Перед созданием модуля проверяем поток — иначе ошибка
    // драйвера выглядела бы как чёрный экран.
    std::string err;
    if (!vklogic::vkSpirvValid(kSceneVertSpv, kSceneVertSpvSize, false, &err)) {
        lastError = "вершинный SPIR-V не прошёл проверку: " + err;
        return false;
    }
    if (!vklogic::vkSpirvValid(kSceneFragSpv, kSceneFragSpvSize, true, &err)) {
        lastError = "фрагментный SPIR-V не прошёл проверку: " + err;
        return false;
    }
    VkShaderModule vert = vkrt::createShaderModule((VkDevice)device, kSceneVertSpv, kSceneVertSpvSize);
    VkShaderModule frag = vkrt::createShaderModule((VkDevice)device, kSceneFragSpv, kSceneFragSpvSize);
    if (!vert || !frag) {
        lastError = "не удалось создать модули шейдеров";
        if (vert) p_vkDestroyShaderModule((VkDevice)device, vert, nullptr);
        if (frag) p_vkDestroyShaderModule((VkDevice)device, frag, nullptr);
        return false;
    }

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vert;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = frag;
    stages[1].pName = "main";

    VkVertexInputBindingDescription binding{};
    binding.binding = 0;
    binding.stride = vklogic::kVertexStride;
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    VkVertexInputAttributeDescription attrs[3]{};
    attrs[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT, vklogic::kPosOffset};
    attrs[1] = {1, 0, VK_FORMAT_R32G32B32_SFLOAT, vklogic::kNormalOffset};
    attrs[2] = {2, 0, VK_FORMAT_R32G32B32_SFLOAT, vklogic::kColorOffset};

    VkPipelineVertexInputStateCreateInfo vin{};
    vin.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vin.vertexBindingDescriptionCount = 1;
    vin.pVertexBindingDescriptions = &binding;
    vin.vertexAttributeDescriptionCount = 3;
    vin.pVertexAttributeDescriptions = attrs;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo raster{};
    raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = VK_CULL_MODE_NONE;  // STL-модели приходят с разной ориентацией
    raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisample{};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depth{};
    depth.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depth.depthTestEnable = VK_TRUE;
    depth.depthWriteEnable = VK_TRUE;
    depth.depthCompareOp = VK_COMPARE_OP_LESS;

    VkPipelineColorBlendAttachmentState blendAttachment{};
    // Смешивание включено всегда: для непрозрачной геометрии (альфа = 1)
    // результат совпадает с обычной записью, а прозрачные земля/препятствие и
    // линии тока рисуются правильно без второго набора конвейеров.
    blendAttachment.blendEnable = VK_TRUE;
    blendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    blendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    blendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
    blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                     VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo blend{};
    blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blend.attachmentCount = 1;
    blend.pAttachments = &blendAttachment;

    VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR,
                                      VK_DYNAMIC_STATE_LINE_WIDTH};
    VkPipelineDynamicStateCreateInfo dynamic{};
    dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic.dynamicStateCount = 3;
    dynamic.pDynamicStates = dynamicStates;

    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushRange.offset = 0;
    pushRange.size = vklogic::kPushConstantBytes;

    VkPipelineLayoutCreateInfo pl{};
    pl.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pl.setLayoutCount = 0;  // наборов дескрипторов у геометрии нет — только push-константы
    pl.pSetLayouts = nullptr;
    pl.pushConstantRangeCount = 1;
    pl.pPushConstantRanges = &pushRange;
    if (p_vkCreatePipelineLayout((VkDevice)device, &pl, nullptr, (VkPipelineLayout*)&pipelineLayout) != VK_SUCCESS) {
        lastError = "vkCreatePipelineLayout не удался";
        p_vkDestroyShaderModule((VkDevice)device, vert, nullptr);
        p_vkDestroyShaderModule((VkDevice)device, frag, nullptr);
        return false;
    }

    auto makePipeline = [&](VkPrimitiveTopology topo, VkPipeline* out) -> bool {
        VkPipelineInputAssemblyStateCreateInfo ia{};
        ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        ia.topology = topo;
        ia.primitiveRestartEnable = VK_FALSE;

        VkGraphicsPipelineCreateInfo gp{};
        gp.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        gp.stageCount = 2;
        gp.pStages = stages;
        gp.pVertexInputState = &vin;
        gp.pInputAssemblyState = &ia;
        gp.pViewportState = &viewportState;
        gp.pRasterizationState = &raster;
        gp.pMultisampleState = &multisample;
        gp.pDepthStencilState = &depth;
        gp.pColorBlendState = &blend;
        gp.pDynamicState = &dynamic;
        gp.layout = (VkPipelineLayout)pipelineLayout;
        gp.renderPass = (VkRenderPass)renderPass;
        gp.subpass = 0;
        return p_vkCreateGraphicsPipelines((VkDevice)device, VK_NULL_HANDLE, 1, &gp, nullptr, out) == VK_SUCCESS;
    };

    bool ok = makePipeline(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, (VkPipeline*)&graphicsPipeline);
    ok = ok && makePipeline(VK_PRIMITIVE_TOPOLOGY_POINT_LIST, (VkPipeline*)&particlePipeline);
    ok = ok && makePipeline(VK_PRIMITIVE_TOPOLOGY_LINE_LIST, (VkPipeline*)&linePipeline);

    p_vkDestroyShaderModule((VkDevice)device, vert, nullptr);
    p_vkDestroyShaderModule((VkDevice)device, frag, nullptr);
    if (!ok) {
        lastError = "vkCreateGraphicsPipelines не удался";
        return false;
    }
    std::cout << "[Vulkan] Конвейеры готовы (треугольники/точки/линии), push-константы "
              << vklogic::kPushConstantBytes << " Б"
              << (hasWideLines ? ", широкие линии включены" : ", широкие линии недоступны")
              << (hasLargePoints ? ", крупные точки включены" : ", точки будут 1 px")
              << std::endl;
    return true;
}

bool VulkanRenderer::createBuffers() {
    // Буфер геометрии на каждый кадр в полёте: потоковая запись из процессора.
    for (int i = 0; i < kFramesInFlight; ++i) {
        currentFrame = i;
        if (!ensureFrameCapacity(65536)) {
            lastError = "не удалось создать буфер вершин";
            return false;
        }
    }
    currentFrame = 0;
    return true;
}

bool VulkanRenderer::ensureFrameCapacity(size_t verts) {
    FrameData& f = frames[currentFrame];
    if (f.buffer && verts <= f.capacityVerts) return true;
    size_t want = std::max<size_t>(verts, 65536);
    if (f.capacityVerts > 0) want = std::max(want, f.capacityVerts + f.capacityVerts / 2);

    VkBuffer buf = VK_NULL_HANDLE;
    VkDeviceMemory mem = VK_NULL_HANDLE;
    if (!vkrt::createBuffer((VkPhysicalDevice)physicalDevice, (VkDevice)device,
                            (VkDeviceSize)want * vklogic::kVertexStride,
                            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, true, &buf, &mem)) {
        lastError = "нет памяти под буфер вершин кадра";
        return false;
    }
    // Буфер этого кадра уже не используется: в beginFrame дождались его fence.
    if (f.mapped) p_vkUnmapMemory((VkDevice)device, (VkDeviceMemory)f.memory);
    VkBuffer oldBuf = (VkBuffer)f.buffer;
    VkDeviceMemory oldMem = (VkDeviceMemory)f.memory;
    vkrt::destroyBuffer((VkDevice)device, &oldBuf, &oldMem);
    f.buffer = buf;
    f.memory = mem;
    f.capacityVerts = want;
    f.mapped = nullptr;
    if (p_vkMapMemory((VkDevice)device, mem, 0, VK_WHOLE_SIZE, 0, &f.mapped) != VK_SUCCESS) {
        lastError = "не удалось отобразить буфер вершин в память процесса";
        return false;
    }
    return true;
}

void VulkanRenderer::appendVertex(float px, float py, float pz, float nx, float ny, float nz,
                                  float r, float g, float b) {
    FrameData& f = frames[currentFrame];
    f.cpuVerts.push_back(px); f.cpuVerts.push_back(py); f.cpuVerts.push_back(pz);
    f.cpuVerts.push_back(nx); f.cpuVerts.push_back(ny); f.cpuVerts.push_back(nz);
    f.cpuVerts.push_back(r);  f.cpuVerts.push_back(g);  f.cpuVerts.push_back(b);
}

void VulkanRenderer::pushCmd(int pipeline, uint32_t first, uint32_t count, const glm::mat4& mvp,
                             glm::vec3 lightDir, bool lit, glm::vec3 tint, float alpha,
                             float pointSize) {
    DrawCmd c;
    c.pipeline = pipeline;
    c.first = first;
    c.count = count;
    std::memcpy(c.mvp, glm::value_ptr(mvp), sizeof(c.mvp));
    c.lightDirUse[0] = lightDir.x; c.lightDirUse[1] = lightDir.y; c.lightDirUse[2] = lightDir.z;
    c.lightDirUse[3] = lit ? 1.0f : 0.0f;
    c.tintAlpha[0] = tint.r; c.tintAlpha[1] = tint.g; c.tintAlpha[2] = tint.b;
    c.tintAlpha[3] = alpha;
    c.camPosPoint[0] = sceneCamera.x; c.camPosPoint[1] = sceneCamera.y; c.camPosPoint[2] = sceneCamera.z;
    c.camPosPoint[3] = pointSize;
    frames[currentFrame].cmds.push_back(c);
}

// -----------------------------------------------------
// Кадр
// -----------------------------------------------------
bool VulkanRenderer::beginFrame() {
    frameStart = std::chrono::steady_clock::now();
    if (!initialized || !device) return false;
    VkDevice dev = (VkDevice)device;

    if (inFlightFences.empty() || currentFrame < 0 || currentFrame >= (int)inFlightFences.size())
        return false;

    // Ждём, пока кадр с этим номером будет отрисован: буфер вершин и командный
    // буфер переиспользуются, писать в них раньше нельзя.
    VkFence fence = (VkFence)inFlightFences[currentFrame];
    if (p_vkWaitForFences(dev, 1, &fence, VK_TRUE, UINT64_MAX) != VK_SUCCESS) return false;

    if (framebufferResized) {
        framebufferResized = false;
        int w = 0, h = 0;
        glfwGetFramebufferSize(win, &w, &h);
        if (w <= 0 || h <= 0) return false;  // окно свёрнуто — кадр пропускаем
        if (!recreateSwapchain()) return false;
    }

    uint32_t idx = 0;
    VkSemaphore imgSem = (VkSemaphore)imageAvailableSemaphores[currentFrame];
    VkResult r = p_vkAcquireNextImageKHR(dev, (VkSwapchainKHR)swapchain, UINT64_MAX, imgSem,
                                         VK_NULL_HANDLE, &idx);
    if (r == VK_ERROR_OUT_OF_DATE_KHR) {
        if (!recreateSwapchain()) return false;
        imgSem = (VkSemaphore)imageAvailableSemaphores[currentFrame];
        r = p_vkAcquireNextImageKHR(dev, (VkSwapchainKHR)swapchain, UINT64_MAX, imgSem,
                                    VK_NULL_HANDLE, &idx);
    }
    if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR) {
        lastError = "vkAcquireNextImageKHR не удался";
        return false;
    }
    if (idx >= framebuffers.size()) return false;
    imageIndex = idx;
    p_vkResetFences(dev, 1, &fence);

    VkCommandBuffer cb = (VkCommandBuffer)commandBuffers[currentFrame];
    p_vkResetCommandBuffer(cb, 0);
    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (p_vkBeginCommandBuffer(cb, &bi) != VK_SUCCESS) return false;

    frames[currentFrame].cmds.clear();
    frames[currentFrame].cpuVerts.clear();

    VkClearValue clears[2]{};
    clears[0].color.float32[0] = clearCol.r;
    clears[0].color.float32[1] = clearCol.g;
    clears[0].color.float32[2] = clearCol.b;
    clears[0].color.float32[3] = 1.0f;
    clears[1].depthStencil.depth = 1.0f;
    clears[1].depthStencil.stencil = 0;

    VkRenderPassBeginInfo rp{};
    rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rp.renderPass = (VkRenderPass)renderPass;
    rp.framebuffer = (VkFramebuffer)framebuffers[imageIndex];
    rp.renderArea.offset = {0, 0};
    rp.renderArea.extent = {extentW, extentH};
    rp.clearValueCount = 2;
    rp.pClearValues = clears;
    p_vkCmdBeginRenderPass(cb, &rp, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport vp{0.0f, 0.0f, (float)extentW, (float)extentH, 0.0f, 1.0f};
    VkRect2D sc{{0, 0}, {extentW, extentH}};
    p_vkCmdSetViewport(cb, 0, 1, &vp);
    p_vkCmdSetScissor(cb, 0, 1, &sc);

    inFrame = true;
    return true;
}

void VulkanRenderer::endFrame() {
    if (!inFrame) return;
    VkCommandBuffer cb = (VkCommandBuffer)commandBuffers[currentFrame];

    p_vkCmdEndRenderPass(cb);
    if (p_vkEndCommandBuffer(cb) != VK_SUCCESS) {
        inFrame = false;
        lastError = "vkEndCommandBuffer не удался";
        return;
    }

    VkSemaphore waitSem = (VkSemaphore)imageAvailableSemaphores[currentFrame];
    VkSemaphore signalSem = (VkSemaphore)renderFinishedSemaphores[imageIndex];
    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.waitSemaphoreCount = 1;
    si.pWaitSemaphores = &waitSem;
    si.pWaitDstStageMask = &waitStage;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cb;
    si.signalSemaphoreCount = 1;
    si.pSignalSemaphores = &signalSem;

    VkFence fence = (VkFence)inFlightFences[currentFrame];
    if (p_vkQueueSubmit((VkQueue)graphicsQueue, 1, &si, fence) != VK_SUCCESS) {
        inFrame = false;
        lastError = "vkQueueSubmit не удался";
        return;
    }

    VkSwapchainKHR sc = (VkSwapchainKHR)swapchain;
    VkPresentInfoKHR pi{};
    pi.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores = &signalSem;
    pi.swapchainCount = 1;
    pi.pSwapchains = &sc;
    pi.pImageIndices = &imageIndex;
    const VkResult r = p_vkQueuePresentKHR((VkQueue)presentQueue, &pi);
    if (r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_SUBOPTIMAL_KHR) framebufferResized = true;

    const auto now = std::chrono::steady_clock::now();
    frameTimeMs = std::chrono::duration<float, std::milli>(now - frameStart).count();

    currentFrame = (currentFrame + 1) % kFramesInFlight;
    inFrame = false;
}

void VulkanRenderer::resize(int w, int h) {
    // Пересоздание swapchain — в начале следующего кадра: в этот момент
    // изображения ещё используются предыдущим кадром.
    if (w != width || h != height) framebufferResized = true;
    width = w;
    height = h;
}

void VulkanRenderer::clearColor(glm::vec3 color) { clearCol = color; }

void VulkanRenderer::cleanupSwapchain() {
    if (!device) return;
    VkDevice dev = (VkDevice)device;
    for (void* fb : framebuffers)
        if (fb) p_vkDestroyFramebuffer(dev, (VkFramebuffer)fb, nullptr);
    framebuffers.clear();
    for (void* v : swapchainImageViews)
        if (v) p_vkDestroyImageView(dev, (VkImageView)v, nullptr);
    swapchainImageViews.clear();
    swapchainImages.clear();
    if (depthImageView) p_vkDestroyImageView(dev, (VkImageView)depthImageView, nullptr);
    if (depthImage) p_vkDestroyImage(dev, (VkImage)depthImage, nullptr);
    if (depthImageMemory) p_vkFreeMemory(dev, (VkDeviceMemory)depthImageMemory, nullptr);
    depthImageView = nullptr;
    depthImage = nullptr;
    depthImageMemory = nullptr;
    if (swapchain) p_vkDestroySwapchainKHR(dev, (VkSwapchainKHR)swapchain, nullptr);
    swapchain = nullptr;
}

bool VulkanRenderer::recreateSwapchain() {
    int w = 0, h = 0;
    glfwGetFramebufferSize(win, &w, &h);
    if (w <= 0 || h <= 0) return false;  // свёрнутое окно: ждём разворота

    // Ждём полного простоя: старые изображения и конвейеры ещё могут
    // использоваться в очереди.
    p_vkDeviceWaitIdle((VkDevice)device);

    const int oldFormat = swapchainFormat;
    const int oldDepth = depthFormat;
    cleanupSwapchain();
    if (!createSwapchain()) return false;

    // Render pass и конвейеры зависят только от ФОРМАТОВ, а не от размера окна.
    // Поэтому при обычном изменении размера их трогать нельзя: конвейер
    // интерфейса ImGui привязан к конкретному render pass, и его пересоздание
    // сделало бы интерфейс несовместимым (ошибка проверки на каждом кадре).
    if (swapchainFormat != oldFormat || depthFormat != oldDepth) {
        std::cout << "[Vulkan] Формат вывода изменился — пересоздаю render pass, конвейеры и интерфейс"
                  << std::endl;
        destroyPipelines();
        if (!createRenderPass()) return false;
        if (uiInitialized) {
            ImGui_ImplVulkan_Shutdown();
            uiInitialized = false;
        }
        if (!createPipelines()) return false;
        if (!initUiBackend()) return false;
    }

    if (!createFramebuffers()) return false;

    // Семафоров готовности кадра должно быть столько же, сколько изображений.
    VkDevice dev = (VkDevice)device;
    for (void* sem : renderFinishedSemaphores)
        if (sem) p_vkDestroySemaphore(dev, (VkSemaphore)sem, nullptr);
    renderFinishedSemaphores.clear();
    VkSemaphoreCreateInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    for (size_t i = 0; i < swapchainImageViews.size(); ++i) {
        VkSemaphore sem = VK_NULL_HANDLE;
        if (p_vkCreateSemaphore(dev, &si, nullptr, &sem) != VK_SUCCESS) {
            lastError = "не удалось пересоздать семафоры готовности кадра";
            return false;
        }
        renderFinishedSemaphores.push_back(sem);
    }

    if (uiInitialized) ImGui_ImplVulkan_SetMinImageCount(minImageCount);
    std::cout << "[Vulkan] Swapchain пересоздан: " << extentW << "x" << extentH
              << " (изображений " << swapchainImageViews.size() << ")" << std::endl;
    return true;
}

// -----------------------------------------------------
// Геометрия сцены
// -----------------------------------------------------
void VulkanRenderer::beginScene(const glm::mat4& viewProj, const glm::mat4& model,
                                glm::vec3 cameraPos, glm::vec3 lightWorld, bool lighting) {
    sceneViewProj = viewProj;
    sceneModelMatrix = model;
    sceneCamera = cameraPos;
    sceneLight = lightWorld;
    sceneLighting = lighting;
}

// Направление на свет в пространстве модели: нормали заданы в пространстве
// модели, поэтому и свет надо привести туда же (матрица нормалей).
static glm::vec3 lightInModelSpace(const glm::mat4& model, const glm::vec3& lightWorld) {
    const glm::mat3 nm = glm::transpose(glm::inverse(glm::mat3(model)));
    glm::vec3 l = nm * lightWorld;
    const float len = glm::length(l);
    if (!std::isfinite(len) || len < 1e-6f) return glm::vec3(0.0f, 1.0f, 0.0f);
    return l / len;
}

void VulkanRenderer::sceneGroundPlane(glm::vec3 center, float halfSize, float groundY,
                                      glm::vec3 color, float alpha) {
    if (!inFrame || halfSize <= 0.0f) return;
    const uint32_t first = (uint32_t)(frames[currentFrame].cpuVerts.size() / vklogic::kVertexFloatCount);
    const float x0 = center.x - halfSize, x1 = center.x + halfSize;
    const float z0 = center.z - halfSize, z1 = center.z + halfSize;
    const float y = groundY;
    // Два треугольника, обход против часовой стрелки при взгляде сверху.
    const float quad[6][3] = {{x0, y, z0}, {x0, y, z1}, {x1, y, z1},
                              {x0, y, z0}, {x1, y, z1}, {x1, y, z0}};
    for (const auto& p : quad) appendVertex(p[0], p[1], p[2], 0.0f, 1.0f, 0.0f,
                                            color.r, color.g, color.b);
    if (alpha <= 0.0f) return;
    const glm::vec3 light = glm::normalize(sceneLight + glm::vec3(1e-6f));
    pushCmd(0, first, 6, sceneViewProj, light, true, glm::vec3(1.0f), alpha, 0.0f);
}

void VulkanRenderer::sceneGrid(glm::vec3 center, float halfSize, float groundY, int divisions,
                               glm::vec3 color, float alpha) {
    if (!inFrame || halfSize <= 0.0f || divisions < 1) return;
    const uint32_t first = (uint32_t)(frames[currentFrame].cpuVerts.size() / vklogic::kVertexFloatCount);
    const float step = (2.0f * halfSize) / (float)divisions;
    for (int i = 0; i <= divisions; ++i) {
        const float t = -halfSize + step * (float)i;
        // линия вдоль Z
        appendVertex(center.x + t, groundY, center.z - halfSize, 0.0f, 1.0f, 0.0f, color.r, color.g, color.b);
        appendVertex(center.x + t, groundY, center.z + halfSize, 0.0f, 1.0f, 0.0f, color.r, color.g, color.b);
        // линия вдоль X
        appendVertex(center.x - halfSize, groundY, center.z + t, 0.0f, 1.0f, 0.0f, color.r, color.g, color.b);
        appendVertex(center.x + halfSize, groundY, center.z + t, 0.0f, 1.0f, 0.0f, color.r, color.g, color.b);
    }
    const uint32_t count = (uint32_t)(divisions + 1) * 4u;
    pushCmd(2, first, count, sceneViewProj, glm::vec3(0.0f, 1.0f, 0.0f), false,
            glm::vec3(1.0f), alpha, 1.0f);
}

void VulkanRenderer::sceneModel(const float* verts, const float* norms, const float* colors,
                                int count, glm::vec3 tint) {
    if (!inFrame || !verts || !norms || count <= 0) return;
    // Защита от повреждённых данных: NaN в вершинах превращается в «невидимую»
    // геометрию, и без проверки это выглядело бы как пропавшая модель.
    if (!std::isfinite(verts[0])) return;
    const size_t base = frames[currentFrame].cpuVerts.size();
    frames[currentFrame].cpuVerts.resize(base + (size_t)count * vklogic::kVertexFloatCount);
    float* dst = frames[currentFrame].cpuVerts.data() + base;
    for (int i = 0; i < count; ++i) {
        float* v = dst + (size_t)i * vklogic::kVertexFloatCount;
        v[0] = verts[i * 3 + 0]; v[1] = verts[i * 3 + 1]; v[2] = verts[i * 3 + 2];
        v[3] = norms[i * 3 + 0]; v[4] = norms[i * 3 + 1]; v[5] = norms[i * 3 + 2];
        if (colors) {
            v[6] = colors[i * 3 + 0]; v[7] = colors[i * 3 + 1]; v[8] = colors[i * 3 + 2];
        } else {
            v[6] = 1.0f; v[7] = 1.0f; v[8] = 1.0f;
        }
    }
    const glm::mat4 mvp = sceneViewProj * sceneModelMatrix;
    pushCmd(0, (uint32_t)(base / vklogic::kVertexFloatCount), (uint32_t)count, mvp,
            lightInModelSpace(sceneModelMatrix, sceneLight), sceneLighting, tint, 1.0f, 0.0f);
}

void VulkanRenderer::sceneParticles(const float* positions, const float* colors, int count,
                                    float pointSize) {
    if (!inFrame || !positions || count <= 0) return;
    const size_t base = frames[currentFrame].cpuVerts.size();
    frames[currentFrame].cpuVerts.resize(base + (size_t)count * vklogic::kVertexFloatCount);
    float* dst = frames[currentFrame].cpuVerts.data() + base;
    for (int i = 0; i < count; ++i) {
        float* v = dst + (size_t)i * vklogic::kVertexFloatCount;
        v[0] = positions[i * 3 + 0]; v[1] = positions[i * 3 + 1]; v[2] = positions[i * 3 + 2];
        v[3] = 0.0f; v[4] = 1.0f; v[5] = 0.0f;
        if (colors) {
            v[6] = colors[i * 3 + 0]; v[7] = colors[i * 3 + 1]; v[8] = colors[i * 3 + 2];
        } else {
            v[6] = 1.0f; v[7] = 1.0f; v[8] = 1.0f;
        }
    }
    pushCmd(1, (uint32_t)(base / vklogic::kVertexFloatCount), (uint32_t)count,
            sceneViewProj * sceneModelMatrix, lightInModelSpace(sceneModelMatrix, sceneLight), false,
            glm::vec3(1.0f), 1.0f, pointSize);
}

void VulkanRenderer::sceneTriangles(const float* verts, const float* norms, const float* colors,
                                    int count, const glm::mat4& modelMatrix, glm::vec3 tint,
                                    float alpha) {
    if (!inFrame || !verts || !norms || count <= 0) return;
    const size_t base = frames[currentFrame].cpuVerts.size();
    frames[currentFrame].cpuVerts.resize(base + (size_t)count * vklogic::kVertexFloatCount);
    float* dst = frames[currentFrame].cpuVerts.data() + base;
    for (int i = 0; i < count; ++i) {
        float* v = dst + (size_t)i * vklogic::kVertexFloatCount;
        v[0] = verts[i * 3 + 0]; v[1] = verts[i * 3 + 1]; v[2] = verts[i * 3 + 2];
        v[3] = norms[i * 3 + 0]; v[4] = norms[i * 3 + 1]; v[5] = norms[i * 3 + 2];
        if (colors) {
            v[6] = colors[i * 3 + 0]; v[7] = colors[i * 3 + 1]; v[8] = colors[i * 3 + 2];
        } else {
            v[6] = 1.0f; v[7] = 1.0f; v[8] = 1.0f;
        }
    }
    pushCmd(0, (uint32_t)(base / vklogic::kVertexFloatCount), (uint32_t)count,
            sceneViewProj * modelMatrix, lightInModelSpace(modelMatrix, sceneLight),
            sceneLighting, tint, alpha, 0.0f);
}

void VulkanRenderer::sceneLines(const float* verts, int count, glm::vec3 tint, float alpha,
                                float width, const glm::mat4& modelMatrix) {
    if (!inFrame || !verts || count <= 0) return;
    const size_t base = frames[currentFrame].cpuVerts.size();
    frames[currentFrame].cpuVerts.resize(base + (size_t)count * vklogic::kVertexFloatCount);
    float* dst = frames[currentFrame].cpuVerts.data() + base;
    for (int i = 0; i < count; ++i) {
        float* v = dst + (size_t)i * vklogic::kVertexFloatCount;
        v[0] = verts[i * 6 + 0]; v[1] = verts[i * 6 + 1]; v[2] = verts[i * 6 + 2];
        v[3] = 0.0f; v[4] = 1.0f; v[5] = 0.0f;
        v[6] = verts[i * 6 + 3]; v[7] = verts[i * 6 + 4]; v[8] = verts[i * 6 + 5];
    }
    // Без wideLines толщина линий всегда 1 пиксель — не притворяемся, что шире.
    const float safeWidth = hasWideLines ? std::max(width, 1.0f) : 1.0f;
    pushCmd(2, (uint32_t)(base / vklogic::kVertexFloatCount), (uint32_t)count,
            sceneViewProj * modelMatrix, glm::vec3(0.0f, 1.0f, 0.0f), false, tint, alpha, safeWidth);
}

void VulkanRenderer::flushScene() {
    if (!initialized || !inFrame) return;
    FrameData& f = frames[currentFrame];
    const size_t verts = f.cpuVerts.size() / vklogic::kVertexFloatCount;
    if (verts > 0) {
        if (!ensureFrameCapacity(verts)) return;
        std::memcpy(f.mapped, f.cpuVerts.data(), verts * vklogic::kVertexFloatCount * sizeof(float));
    }
    if (f.cmds.empty() || !f.buffer) return;

    VkCommandBuffer cb = (VkCommandBuffer)commandBuffers[currentFrame];
    VkBuffer vbuf = (VkBuffer)f.buffer;
    VkDeviceSize offset = 0;
    p_vkCmdBindVertexBuffers(cb, 0, 1, &vbuf, &offset);

    int bound = -1;
    for (const DrawCmd& c : f.cmds) {
        if (c.count == 0) continue;
        if (c.pipeline != bound) {
            const VkPipeline pipe = c.pipeline == 0 ? (VkPipeline)graphicsPipeline
                                  : c.pipeline == 1 ? (VkPipeline)particlePipeline
                                                    : (VkPipeline)linePipeline;
            p_vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
            bound = c.pipeline;
        }
        if (c.pipeline == 2) {
            // LINE_WIDTH объявлено динамическим состоянием, поэтому задавать его
            // обязательно перед каждой отрисовкой линий. Без wideLines значение
            // может быть только 1.0 — так и передаём, а не «как получится».
            float width = c.camPosPoint[3] > 0.0f ? c.camPosPoint[3] : 1.0f;
            if (!hasWideLines) width = 1.0f;
            p_vkCmdSetLineWidth(cb, width);
        }
        // Раскладка DrawCmd совпадает с push-константами шейдера (проверено
        // static_assert): можно передать указатель на первое поле.
        p_vkCmdPushConstants(cb, (VkPipelineLayout)pipelineLayout,
                             VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                             vklogic::kPushConstantBytes, &c.mvp);
        p_vkCmdDraw(cb, c.count, 1, c.first, 0);
    }
}

// -----------------------------------------------------
// Устаревший интерфейс IRenderer: переадресуем в сцену
// -----------------------------------------------------
void VulkanRenderer::drawModel(const std::vector<float>& vertices, const std::vector<float>& normals,
                               const std::vector<float>& colors, const glm::mat4& mvp) {
    if (!inFrame || vertices.empty() || normals.size() < vertices.size()) return;
    const int count = (int)(vertices.size() / 3);
    const size_t base = frames[currentFrame].cpuVerts.size();
    frames[currentFrame].cpuVerts.resize(base + (size_t)count * vklogic::kVertexFloatCount);
    float* dst = frames[currentFrame].cpuVerts.data() + base;
    const bool haveColors = colors.size() >= vertices.size();
    for (int i = 0; i < count; ++i) {
        float* v = dst + (size_t)i * vklogic::kVertexFloatCount;
        v[0] = vertices[i * 3 + 0]; v[1] = vertices[i * 3 + 1]; v[2] = vertices[i * 3 + 2];
        v[3] = normals[i * 3 + 0];  v[4] = normals[i * 3 + 1];  v[5] = normals[i * 3 + 2];
        v[6] = haveColors ? colors[i * 3 + 0] : 1.0f;
        v[7] = haveColors ? colors[i * 3 + 1] : 1.0f;
        v[8] = haveColors ? colors[i * 3 + 2] : 1.0f;
    }
    pushCmd(0, (uint32_t)(base / vklogic::kVertexFloatCount), (uint32_t)count, mvp,
            lightInModelSpace(sceneModelMatrix, sceneLight), sceneLighting, glm::vec3(1.0f), 1.0f, 0.0f);
}

void VulkanRenderer::drawParticles(const std::vector<float>& positions,
                                   const std::vector<float>& colors, float pointSize, int count) {
    if (count > 0 && positions.size() >= (size_t)count * 3)
        sceneParticles(positions.data(), colors.size() >= (size_t)count * 3 ? colors.data() : nullptr,
                       count, pointSize);
}

void VulkanRenderer::drawLines(const std::vector<float>& vertices, glm::vec3 color, float width) {
    if (vertices.size() < 6) return;
    // Входной формат: только позиции (3 float на вершину) + один цвет на вызов.
    const int count = (int)(vertices.size() / 3);
    std::vector<float> lineVerts((size_t)count * 6);
    for (int i = 0; i < count; ++i) {
        lineVerts[i * 6 + 0] = vertices[i * 3 + 0];
        lineVerts[i * 6 + 1] = vertices[i * 3 + 1];
        lineVerts[i * 6 + 2] = vertices[i * 3 + 2];
        lineVerts[i * 6 + 3] = color.r;
        lineVerts[i * 6 + 4] = color.g;
        lineVerts[i * 6 + 5] = color.b;
    }
    sceneLines(lineVerts.data(), count, glm::vec3(1.0f), 1.0f, width, sceneModelMatrix);
}

// -----------------------------------------------------
// Интерфейс (ImGui) на Vulkan
// -----------------------------------------------------
bool VulkanRenderer::initUiBackend() {
    if (!initialized || uiInitialized) return uiInitialized;
    if (!p_vkGetInstanceProcAddr) {
        lastError = "нет загрузчика функций Vulkan для интерфейса";
        return false;
    }
    if (!ImGui_ImplVulkan_LoadFunctions(instanceApiVersion, vkrt::uiFuncLoader, nullptr)) {
        lastError = "ImGui_ImplVulkan_LoadFunctions не удался";
        return false;
    }

    ImGui_ImplVulkan_InitInfo info{};
    info.ApiVersion = instanceApiVersion;
    info.Instance = (VkInstance)instance;
    info.PhysicalDevice = (VkPhysicalDevice)physicalDevice;
    info.Device = (VkDevice)device;
    info.QueueFamily = (uint32_t)graphicsFamily;
    info.Queue = (VkQueue)graphicsQueue;
    // Пул дескрипторов интерфейса создаёт сам бэкенд (DescriptorPoolSize > 0):
    // так нельзя ошибиться с размерами пула.
    info.DescriptorPool = VK_NULL_HANDLE;
    info.DescriptorPoolSize = IMGUI_IMPL_VULKAN_MINIMUM_SAMPLED_IMAGE_POOL_SIZE;
    info.MinImageCount = minImageCount;
    info.ImageCount = (uint32_t)swapchainImageViews.size();
    info.PipelineCache = VK_NULL_HANDLE;
    info.PipelineInfoMain.RenderPass = (VkRenderPass)renderPass;
    info.PipelineInfoMain.Subpass = 0;
    info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    info.UseDynamicRendering = false;  // рисуем через классический render pass
    info.Allocator = nullptr;
    info.CheckVkResultFn = nullptr;
    info.MinAllocationSize = 1024 * 1024;

    if (!ImGui_ImplVulkan_Init(&info)) {
        lastError = "ImGui_ImplVulkan_Init не удался";
        return false;
    }
    uiInitialized = true;
    std::cout << "[Vulkan] Интерфейс ImGui подключён к Vulkan-бэкенду" << std::endl;
    return true;
}

void VulkanRenderer::shutdownUiBackend() {
    if (!uiInitialized) return;
    ImGui_ImplVulkan_Shutdown();
    uiInitialized = false;
}

void VulkanRenderer::setVSync(bool enabled) {
    // Режим показа задаётся при создании swapchain, поэтому просим пересоздать
    // её на следующем кадре — иначе настройка осталась бы только на бумаге.
    if (config.vsync == enabled) return;
    config.vsync = enabled;
    framebufferResized = true;
}

void VulkanRenderer::uiNewFrame() {
    if (uiInitialized) ImGui_ImplVulkan_NewFrame();
}

void VulkanRenderer::uiRender() {
    if (!uiInitialized || !inFrame) return;
    ImDrawData* drawData = ImGui::GetDrawData();
    if (!drawData) return;
    // Командный буфер уже внутри render pass — интерфейс рисуется поверх сцены.
    ImGui_ImplVulkan_RenderDrawData(drawData, (VkCommandBuffer)commandBuffers[currentFrame]);
}

// -----------------------------------------------------
// Завершение работы
// -----------------------------------------------------
void VulkanRenderer::shutdown() {
    if (uiInitialized) {
        // Контекст ImGui мог быть уже уничтожен в main.cpp — проверяем.
        if (ImGui::GetCurrentContext()) ImGui_ImplVulkan_Shutdown();
        uiInitialized = false;
    }
    if (!device) {
        initialized = false;
        return;
    }
    p_vkDeviceWaitIdle((VkDevice)device);
    VkDevice dev = (VkDevice)device;

    for (int i = 0; i < kFramesInFlight; ++i) {
        FrameData& f = frames[i];
        if (f.mapped) p_vkUnmapMemory(dev, (VkDeviceMemory)f.memory);
        VkBuffer b = (VkBuffer)f.buffer;
        VkDeviceMemory m = (VkDeviceMemory)f.memory;
        vkrt::destroyBuffer(dev, &b, &m);
        f.mapped = nullptr;
        f.buffer = nullptr;
        f.memory = nullptr;
        f.capacityVerts = 0;
        f.cmds.clear();
        f.cpuVerts.clear();
    }
    cleanupSwapchain();
    destroyPipelines();
    if (renderPass) p_vkDestroyRenderPass(dev, (VkRenderPass)renderPass, nullptr);
    renderPass = nullptr;
    for (void* s : imageAvailableSemaphores)
        if (s) p_vkDestroySemaphore(dev, (VkSemaphore)s, nullptr);
    for (void* s : renderFinishedSemaphores)
        if (s) p_vkDestroySemaphore(dev, (VkSemaphore)s, nullptr);
    for (void* f : inFlightFences)
        if (f) p_vkDestroyFence(dev, (VkFence)f, nullptr);
    imageAvailableSemaphores.clear();
    renderFinishedSemaphores.clear();
    inFlightFences.clear();
    if (commandPool) p_vkDestroyCommandPool(dev, (VkCommandPool)commandPool, nullptr);
    commandPool = nullptr;
    commandBuffers.clear();
    p_vkDestroyDevice(dev, nullptr);
    device = nullptr;
    s_uiDevice = VK_NULL_HANDLE;
    if (surface) p_vkDestroySurfaceKHR((VkInstance)instance, (VkSurfaceKHR)surface, nullptr);
    surface = nullptr;
    if (debugMessenger && instance) {
        auto destroyMsg = (PFN_vkDestroyDebugUtilsMessengerEXT)
            p_vkGetInstanceProcAddr((VkInstance)instance, "vkDestroyDebugUtilsMessengerEXT");
        if (destroyMsg) destroyMsg((VkInstance)instance, (VkDebugUtilsMessengerEXT)debugMessenger, nullptr);
        debugMessenger = nullptr;
    }
    if (instance) p_vkDestroyInstance((VkInstance)instance, nullptr);
    instance = nullptr;
    s_uiInstance = VK_NULL_HANDLE;
    initialized = false;
    std::cout << "[Vulkan] Рендерер остановлен, ресурсы освобождены" << std::endl;
}

#else  // AEROS_HAS_VULKAN

// Сборка без заголовков Vulkan: класс должен существовать (на него ссылается
// initRenderer), но инициализация честно возвращает false.
bool VulkanRenderer::init(GLFWwindow*, const RendererConfig&) {
    lastError = "движок собран без заголовков Vulkan";
    return false;
}
void VulkanRenderer::shutdown() { initialized = false; }
bool VulkanRenderer::beginFrame() { return false; }
void VulkanRenderer::endFrame() {}
void VulkanRenderer::resize(int w, int h) { width = w; height = h; }
void VulkanRenderer::clearColor(glm::vec3 color) { clearCol = color; }
void VulkanRenderer::drawModel(const std::vector<float>&, const std::vector<float>&,
                               const std::vector<float>&, const glm::mat4&) {}
void VulkanRenderer::drawParticles(const std::vector<float>&, const std::vector<float>&, float, int) {}
void VulkanRenderer::drawLines(const std::vector<float>&, glm::vec3, float) {}
void VulkanRenderer::beginScene(const glm::mat4&, const glm::mat4&, glm::vec3, glm::vec3, bool) {}
void VulkanRenderer::sceneGroundPlane(glm::vec3, float, float, glm::vec3, float) {}
void VulkanRenderer::sceneGrid(glm::vec3, float, float, int, glm::vec3, float) {}
void VulkanRenderer::sceneModel(const float*, const float*, const float*, int, glm::vec3) {}
void VulkanRenderer::sceneParticles(const float*, const float*, int, float) {}
void VulkanRenderer::sceneTriangles(const float*, const float*, const float*, int,
                                    const glm::mat4&, glm::vec3, float) {}
void VulkanRenderer::sceneLines(const float*, int, glm::vec3, float, float, const glm::mat4&) {}
void VulkanRenderer::flushScene() {}
bool VulkanRenderer::initUiBackend() { return false; }
void VulkanRenderer::shutdownUiBackend() {}
void VulkanRenderer::uiNewFrame() {}
void VulkanRenderer::uiRender() {}
void VulkanRenderer::setVSync(bool) {}
bool VulkanRenderer::createInstance() { return false; }
bool VulkanRenderer::pickPhysicalDevice() { return false; }
bool VulkanRenderer::createLogicalDevice() { return false; }
bool VulkanRenderer::createSwapchain() { return false; }
bool VulkanRenderer::createRenderPass() { return false; }
bool VulkanRenderer::createFramebuffers() { return false; }
bool VulkanRenderer::createCommandPool() { return false; }
bool VulkanRenderer::createCommandBuffers() { return false; }
bool VulkanRenderer::createSyncObjects() { return false; }
bool VulkanRenderer::createPipelines() { return false; }
bool VulkanRenderer::createBuffers() { return false; }
void VulkanRenderer::cleanupSwapchain() {}
void VulkanRenderer::destroyPipelines() {}
bool VulkanRenderer::recreateSwapchain() { return false; }
bool VulkanRenderer::ensureFrameCapacity(size_t) { return false; }
void VulkanRenderer::appendVertex(float, float, float, float, float, float, float, float, float) {}
void VulkanRenderer::pushCmd(int, uint32_t, uint32_t, const glm::mat4&, glm::vec3, bool,
                             glm::vec3, float, float) {}

#endif  // AEROS_HAS_VULKAN

// =====================================================
// Управление глобальным рендерером
// =====================================================
bool initRenderer(GLFWwindow* window, RendererAPI api) {
    if (g_renderer) shutdownRenderer();

    RendererConfig cfg;
    cfg.api = api;
    cfg.enableValidation = false;
    cfg.vsync = vsyncEnabled;
    g_rendererConfig = cfg;

    if (api == RendererAPI::Auto) {
        // Автовыбор остаётся за OpenGL: Vulkan включается только явным запросом
        // (renderer=vulkan в настройках или ключ --vulkan), иначе обновление
        // молча меняло бы бэкенд и поведение на уже настроенных машинах.
        api = RendererAPI::OpenGL;
    }

    if (api == RendererAPI::Vulkan) {
        VulkanRenderer* vk = new VulkanRenderer();
        if (vk->init(window, cfg)) {
            g_renderer = vk;
            g_currentAPI = RendererAPI::Vulkan;
            std::cout << "[Renderer] Используется Vulkan: " << vk->getDeviceName() << std::endl;
            return true;
        }
        std::cerr << "[Renderer] Vulkan не запустился: " << vk->getLastError()
                  << " — переходим на OpenGL" << std::endl;
        delete vk;
        // Окно уже создано без контекста OpenGL — вызвать OpenGL в нём нельзя.
        // Об этом честно сообщаем: main.cpp в этом случае создаёт окно заново.
        g_currentAPI = RendererAPI::OpenGL;
        return false;
    }

    OpenGLRenderer* gl = new OpenGLRenderer();
    if (gl->init(window, cfg)) {
        g_renderer = gl;
        g_currentAPI = RendererAPI::OpenGL;
        std::cout << "[Renderer] Используется OpenGL" << std::endl;
        return true;
    }
    delete gl;
    std::cerr << "[Renderer] OpenGL не инициализировался" << std::endl;
    return false;
}

void shutdownRenderer() {
    if (g_renderer) {
        g_renderer->shutdown();
        delete g_renderer;
        g_renderer = nullptr;
    }
}

bool isRendererVulkan() { return g_currentAPI == RendererAPI::Vulkan; }
bool isRendererOpenGL() { return g_currentAPI == RendererAPI::OpenGL; }
const char* getRendererName() { return g_renderer ? g_renderer->getAPIName() : "None"; }
void setRendererAPI(RendererAPI api) { g_rendererConfig.api = api; }
RendererAPI getRendererAPI() { return g_currentAPI; }

// =====================================================
// Диалоги выбора файлов — между платформами
// =====================================================
namespace {
#ifndef _WIN32
// v1.36.0 — общий безопасный запуск zenity/kdialog (без shell).
// Возвращает строку из stdout; пусто = отмена или диалог не установлен.
std::string runDialogCapture(const std::vector<std::string>& argv) {
    const procutil::Result r = procutil::runCapture(argv);
    if (!r.ok) return "";
    return procutil::firstLine(r.output);
}
#endif
} // namespace

std::string openFileDialogNative() {
#ifdef _WIN32
    return openFileDialog();
#else
    // v1.36.0 — без shell: argv передаётся execvp напрямую (см. proc_util.h).
    {
        std::string s = runDialogCapture({
            "zenity", "--file-selection",
            "--file-filter=STL files | *.stl *.STL",
            "--title=Открыть модель STL"});
        if (!s.empty()) return s;
        s = runDialogCapture({"kdialog", "--getopenfilename", ".", "*.stl | STL Models"});
        if (!s.empty()) return s;
    }
    std::cout << "[FileDialog] Нативный диалог не найден, проверяем ./models/ и ./bin/" << std::endl;
    const std::vector<std::string> candidates = {
        "./models/model.stl", "./model.stl", "./bin/model.stl",
        "./aeros/models/model.stl", "/usr/share/aeros-engine/model.stl"};
    for (const auto& c : candidates) {
        std::ifstream f(c);
        if (f.good()) {
            std::cout << "[FileDialog] Используется " << c << std::endl;
            return c;
        }
    }
    std::cout << "Введите путь к файлу STL: ";
    std::string input;
    std::getline(std::cin, input);
    return input;
#endif
}

std::string saveFileDialogNative(const std::string& defaultName) {
#ifdef _WIN32
    OPENFILENAMEA ofn;
    char szFile[260] = {0};
    // v1.36.0 — раньше длинное имя молча обрезалось strncpy(): диалог
    // открывался с усечённым именем, и файл сохранялся не под тем именем,
    // которое показала программа. Теперь это явный отказ с сообщением.
    if (defaultName.size() >= sizeof(szFile)) {
        std::cerr << "[FileDialog] имя файла длиннее " << (sizeof(szFile) - 1)
                  << " символов — диалог сохранения не открыт" << std::endl;
        return "";
    }
    std::memcpy(szFile, defaultName.c_str(), defaultName.size() + 1);
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile);
    ofn.lpstrFilter = "All Files\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrFileTitle = NULL;
    ofn.nMaxFileTitle = 0;
    ofn.lpstrInitialDir = NULL;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT;
    if (GetSaveFileNameA(&ofn)) return std::string(szFile);
    return "";
#else
    // v1.36.0 — было popen("... --filename='" + defaultName + "'"), то есть
    // апостроф в имени файла закрывал кавычки и превращал остаток пути в
    // команды оболочки. Теперь " --filename=<имя>" — ОДИН аргумент execvp.
    const std::string s = runDialogCapture({
        "zenity", "--file-selection", "--save", "--confirm-overwrite",
        "--filename=" + defaultName});
    if (!s.empty()) return s;
    return defaultName;
#endif
}
