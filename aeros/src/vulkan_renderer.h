#pragma once
// =====================================================
// Рендереры Aeros Engine.
//
// v1.33.0 — Vulkan-бэкенд реализован по-настоящему: swapchain, render pass,
// конвейеры (треугольники/точки/линии), буферы вершин, синхронизация кадров и
// интерфейс через imgui_impl_vulkan. Заголовок не тянет vulkan.h: все дескрипторы
// хранятся как void*, поэтому остальной код (и headless-тесты) собирается без
// Vulkan-заголовков.
// =====================================================

#include <glm/glm.hpp>
#include <vector>
#include <string>
#include <functional>
#include <cstdint>
#include <cstring>
#include <chrono>
#include <type_traits>

// -----------------------------------------------------
// Хранение дескрипторов Vulkan без подключения vulkan.h в заголовок.
//
// Не-диспетчеризуемые дескрипторы (VkBuffer, VkImage, VkSwapchainKHR, VkFence,
// VkSemaphore, VkPipeline, ...) в 64-битных сборках — это указатели, а в
// 32-битных — uint64_t (VK_USE_64_BIT_PTR_DEFINES == 0). Поэтому «универсальный»
// void* не подходит: MSVC на x86 отказывается преобразовывать uint64_t в void*
// (error C2440 — сборка x86 падала именно на этом), а приведение через uintptr_t
// на x86 обрезало бы значение. Этот тип хранит те же 8 байт и преобразуется в
// любой дескриптор через memcpy, поэтому одинаково верен на обеих разрядностях.
// -----------------------------------------------------
class AerosVkHandle {
public:
    AerosVkHandle() = default;
    AerosVkHandle(const AerosVkHandle&) = default;
    AerosVkHandle& operator=(const AerosVkHandle&) = default;

    // Дескриптор → хранилище.
    template <class H, typename std::enable_if<
                            std::is_trivially_copyable<H>::value &&
                                (std::is_pointer<H>::value ||
                                 (std::is_integral<H>::value && sizeof(H) == sizeof(uint64_t))),
                            int>::type = 0>
    AerosVkHandle(H h) {
        store(h);
    }

    // Хранилище → дескриптор (для явных приведений (VkBuffer)handle).
    template <class H, typename std::enable_if<
                            std::is_trivially_copyable<H>::value &&
                                (std::is_pointer<H>::value ||
                                 (std::is_integral<H>::value && sizeof(H) == sizeof(uint64_t))),
                            int>::type = 0>
    operator H() const {
        return load<H>();
    }

    // Присваивание и сравнение с nullptr — это «обнулить дескриптор».
    AerosVkHandle(std::nullptr_t) {}
    AerosVkHandle& operator=(std::nullptr_t) {
        bits = 0;
        return *this;
    }

    explicit operator bool() const { return bits != 0; }
    bool isNull() const { return bits == 0; }
    uint64_t raw() const { return bits; }

private:
    template <class H>
    void store(H h) {
        static_assert(sizeof(H) <= sizeof(bits), "дескриптор Vulkan шире 8 байт");
        bits = 0;
        std::memcpy(&bits, &h, sizeof(H));
    }
    template <class H>
    H load() const {
        static_assert(sizeof(H) <= sizeof(bits), "дескриптор Vulkan шире 8 байт");
        H h{};
        std::memcpy(&h, &bits, sizeof(H));
        return h;
    }

    uint64_t bits = 0;
};

// Forward declare GLFW
struct GLFWwindow;

enum class RendererAPI {
    OpenGL = 0,
    Vulkan = 1,
    Auto = 2 // выбирает лучший доступный (сейчас — OpenGL, Vulkan только явно)
};

struct RendererConfig {
    RendererAPI api = RendererAPI::Auto;
    bool enableValidation = false;
    bool vsync = true;
    int msaaSamples = 1;
    bool enableFSR = false;
    bool enableFG = false;
};

// Vulkan availability check
bool isVulkanAvailable();
// v1.26.0 — найден ли в системе загрузчик Vulkan (vulkan-1.dll / libvulkan.so).
// Отделено от isVulkanAvailable(): загрузчик есть почти везде, а рисовать через
// Vulkan умеет только сборка с заголовками Vulkan и рабочим устройством.
bool vulkanLoaderPresent();
bool initVulkanLoader();
std::string getVulkanVersionString();
std::vector<std::string> getVulkanExtensions();
std::vector<std::string> getVulkanDevices();
// v1.33.0 — почему Vulkan включён или нет (для панели «Отладка»).
std::string getVulkanDecisionReason();

// Renderer abstraction
class IRenderer {
public:
    virtual ~IRenderer() = default;
    virtual bool init(GLFWwindow* window, const RendererConfig& cfg) = 0;
    virtual void shutdown() = 0;
    virtual bool beginFrame() = 0;
    virtual void endFrame() = 0;
    virtual void resize(int w, int h) = 0;
    virtual void clearColor(glm::vec3 color) = 0;
    virtual void drawModel(const std::vector<float>& vertices, const std::vector<float>& normals, const std::vector<float>& colors, const glm::mat4& mvp) = 0;
    virtual void drawParticles(const std::vector<float>& positions, const std::vector<float>& colors, float pointSize, int count) = 0;
    virtual void drawLines(const std::vector<float>& vertices, glm::vec3 color, float width) = 0;
    virtual RendererAPI getAPI() const = 0;
    virtual const char* getAPIName() const = 0;
    virtual float getFrameTimeMs() const = 0;
    virtual bool isInitialized() const = 0;

    // ---------- v1.33.0: сцена для альтернативных бэкендов ----------
    // OpenGL-ветка эти вызовы не использует (рисует своими шейдерами напрямую),
    // поэтому реализации по умолчанию пустые.
    virtual bool isVulkanBackend() const { return false; }
    // Камера, модель и свет на текущий кадр. lightWorld — направление НА свет в
    // мировых координатах, lighting — включено ли освещение модели.
    virtual void beginScene(const glm::mat4& viewProj, const glm::mat4& model,
                            glm::vec3 cameraPos, glm::vec3 lightWorld, bool lighting) {
        (void)viewProj; (void)model; (void)cameraPos; (void)lightWorld; (void)lighting;
    }
    // Земля: два треугольника в плоскости Y=groundY с полуразмером halfSize.
    virtual void sceneGroundPlane(glm::vec3 center, float halfSize, float groundY,
                                  glm::vec3 color, float alpha) {
        (void)center; (void)halfSize; (void)groundY; (void)color; (void)alpha;
    }
    // Сетка на земле: divisions линий в каждую сторону.
    virtual void sceneGrid(glm::vec3 center, float halfSize, float groundY, int divisions,
                           glm::vec3 color, float alpha) {
        (void)center; (void)halfSize; (void)groundY; (void)divisions; (void)color; (void)alpha;
    }
    // Модель (позиции/нормали/цвета, 3 float на вершину) в пространстве модели.
    // colors может быть nullptr — тогда используется tint.
    virtual void sceneModel(const float* verts, const float* norms, const float* colors,
                            int count, glm::vec3 tint) {
        (void)verts; (void)norms; (void)colors; (void)count; (void)tint;
    }
    // Частицы: точки размером pointSize (в пикселях) в пространстве модели.
    virtual void sceneParticles(const float* positions, const float* colors, int count,
                                float pointSize) {
        (void)positions; (void)colors; (void)count; (void)pointSize;
    }
    // Произвольные треугольники (например, препятствие-эллипсоид).
    virtual void sceneTriangles(const float* verts, const float* norms, const float* colors,
                                int count, const glm::mat4& modelMatrix, glm::vec3 tint,
                                float alpha) {
        (void)verts; (void)norms; (void)colors; (void)count; (void)modelMatrix; (void)tint; (void)alpha;
    }
    // Линии: 6 float на вершину (позиция + цвет), рисуются с матрицей modelMatrix.
    virtual void sceneLines(const float* verts, int count, glm::vec3 tint, float alpha,
                            float width, const glm::mat4& modelMatrix) {
        (void)verts; (void)count; (void)tint; (void)alpha; (void)width; (void)modelMatrix;
    }
    // Записать накопленную геометрию в командный буфер (вызывается до интерфейса).
    virtual void flushScene() {}
    // Интерфейс на Vulkan: main.cpp не тянет vulkan.h — обо всём, что требует
    // типов Vulkan (ImGui_ImplVulkan_Init/RenderDrawData), заботится рендерер.
    virtual bool initUiBackend() { return false; }
    virtual void shutdownUiBackend() {}
    virtual void uiNewFrame() {}
    virtual void uiRender() {}
    // Вертикальная синхронизация: в OpenGL — glfwSwapInterval, в Vulkan —
    // режим показа swapchain, поэтому при смене пересоздаём цепочку.
    virtual void setVSync(bool) {}
};

// OpenGL renderer wrapper (existing code)
class OpenGLRenderer : public IRenderer {
public:
    bool init(GLFWwindow* window, const RendererConfig& cfg) override;
    void shutdown() override;
    bool beginFrame() override;
    void endFrame() override;
    void resize(int w, int h) override;
    void clearColor(glm::vec3 color) override;
    void drawModel(const std::vector<float>& vertices, const std::vector<float>& normals, const std::vector<float>& colors, const glm::mat4& mvp) override;
    void drawParticles(const std::vector<float>& positions, const std::vector<float>& colors, float pointSize, int count) override;
    void drawLines(const std::vector<float>& vertices, glm::vec3 color, float width) override;
    RendererAPI getAPI() const override { return RendererAPI::OpenGL; }
    const char* getAPIName() const override { return "OpenGL 4.6"; }
    float getFrameTimeMs() const override { return frameTimeMs; }
    bool isInitialized() const override { return initialized; }
private:
    bool initialized = false;
    float frameTimeMs = 0.0f;
    GLFWwindow* win = nullptr;
};

// Vulkan renderer — рабочая реализация (v1.33.0)
class VulkanRenderer : public IRenderer {
public:
    bool init(GLFWwindow* window, const RendererConfig& cfg) override;
    void shutdown() override;
    bool beginFrame() override;
    void endFrame() override;
    void resize(int w, int h) override;
    void clearColor(glm::vec3 color) override;
    void drawModel(const std::vector<float>& vertices, const std::vector<float>& normals, const std::vector<float>& colors, const glm::mat4& mvp) override;
    void drawParticles(const std::vector<float>& positions, const std::vector<float>& colors, float pointSize, int count) override;
    void drawLines(const std::vector<float>& vertices, glm::vec3 color, float width) override;
    RendererAPI getAPI() const override { return RendererAPI::Vulkan; }
    const char* getAPIName() const override { return "Vulkan"; }
    float getFrameTimeMs() const override { return frameTimeMs; }
    bool isInitialized() const override { return initialized; }

    // Сцена (см. IRenderer)
    bool isVulkanBackend() const override { return true; }
    void beginScene(const glm::mat4& viewProj, const glm::mat4& model,
                    glm::vec3 cameraPos, glm::vec3 lightWorld, bool lighting) override;
    void sceneGroundPlane(glm::vec3 center, float halfSize, float groundY,
                          glm::vec3 color, float alpha) override;
    void sceneGrid(glm::vec3 center, float halfSize, float groundY, int divisions,
                   glm::vec3 color, float alpha) override;
    void sceneModel(const float* verts, const float* norms, const float* colors,
                    int count, glm::vec3 tint) override;
    void sceneParticles(const float* positions, const float* colors, int count,
                        float pointSize) override;
    void sceneTriangles(const float* verts, const float* norms, const float* colors,
                        int count, const glm::mat4& modelMatrix, glm::vec3 tint,
                        float alpha) override;
    void sceneLines(const float* verts, int count, glm::vec3 tint, float alpha,
                    float width, const glm::mat4& modelMatrix) override;
    void flushScene() override;
    bool initUiBackend() override;
    void shutdownUiBackend() override;
    void uiNewFrame() override;
    void uiRender() override;
    void setVSync(bool enabled) override;

    // Шаги инициализации (вызываются из init по порядку)
    bool createInstance();
    bool pickPhysicalDevice();
    bool createLogicalDevice();
    bool createSwapchain();
    bool createRenderPass();
    bool createFramebuffers();
    bool createCommandPool();
    bool createCommandBuffers();
    bool createSyncObjects();
    bool createPipelines();
    bool createBuffers();
    void cleanupSwapchain();
    void destroyPipelines();
    bool recreateSwapchain();

    std::string getDeviceName() const { return deviceName; }
    std::string getDriverVersion() const { return driverVersion; }
    int getVRAM_MB() const { return vramMB; }
    // Текст последней ошибки — для журнала и панели «Отладка».
    std::string getLastError() const { return lastError; }

private:
    bool initialized = false;
    float frameTimeMs = 0.0f;
    GLFWwindow* win = nullptr;
    RendererConfig config;
    std::string lastError;

    // Вершина в общем формате кадра: позиция, нормаль, цвет (36 байт).
    // Тот же порядок объявлен в shaders/scene.vert.
    struct DrawCmd {
        int pipeline = 0;  // 0 — треугольники, 1 — точки, 2 — линии
        uint32_t first = 0;
        uint32_t count = 0;
        float mvp[16] = {0};
        float lightDirUse[4] = {0};  // xyz — свет в пространстве модели, w — освещение
        float tintAlpha[4] = {0};    // rgb — множитель цвета, a — прозрачность
        float camPosPoint[4] = {0};  // w — размер точки / толщина линии
    };
    struct FrameData {
        std::vector<DrawCmd> cmds;
        std::vector<float> cpuVerts;  // позиция/нормаль/цвет на вершину
        AerosVkHandle buffer;   // VkBuffer
        AerosVkHandle memory;   // VkDeviceMemory
        void* mapped = nullptr; // указатель на отображённую память
        size_t capacityVerts = 0;
    };
    static const int kFramesInFlight = 2;
    FrameData frames[kFramesInFlight];

    // Дескрипторы Vulkan. Диспетчеризуемые (VkInstance, VkDevice, VkQueue,
    // VkCommandBuffer) — всегда указатели, поэтому void*. Остальные — через
    // AerosVkHandle: на x86 они uint64_t, и void* для них не компилируется.
    void* instance = nullptr;
    void* physicalDevice = nullptr;
    void* device = nullptr;
    AerosVkHandle surface;        // VkSurfaceKHR — не-диспетчеризуемый
    void* graphicsQueue = nullptr;
    void* presentQueue = nullptr;
    int graphicsFamily = -1;
    int presentFamily = -1;
    AerosVkHandle swapchain;
    AerosVkHandle renderPass;
    AerosVkHandle pipelineLayout;
    AerosVkHandle graphicsPipeline;
    AerosVkHandle particlePipeline;
    AerosVkHandle linePipeline;
    AerosVkHandle commandPool;
    std::vector<void*> commandBuffers;            // VkCommandBuffer — указатель
    std::vector<AerosVkHandle> swapchainImages;
    std::vector<AerosVkHandle> swapchainImageViews;
    std::vector<AerosVkHandle> framebuffers;
    std::vector<AerosVkHandle> imageAvailableSemaphores;   // по кадру в полёте
    std::vector<AerosVkHandle> renderFinishedSemaphores;   // по изображению swapchain
    std::vector<AerosVkHandle> inFlightFences;             // по кадру в полёте
    AerosVkHandle depthImage;
    AerosVkHandle depthImageMemory;
    AerosVkHandle depthImageView;
    AerosVkHandle debugMessenger;
    uint32_t instanceApiVersion = 0;
    bool hasWideLines = false;
    bool hasLargePoints = false;
    bool uiInitialized = false;

    uint32_t minImageCount = 2;
    int swapchainFormat = 0;
    uint32_t extentW = 0, extentH = 0;
    int depthFormat = 0;

    int currentFrame = 0;
    int width = 0, height = 0;
    uint32_t imageIndex = 0;
    glm::vec3 clearCol = glm::vec3(0.05f, 0.06f, 0.09f);
    std::string deviceName = "Unknown";
    std::string driverVersion = "Unknown";
    int vramMB = 0;
    bool framebufferResized = false;

    // Вспомогательные функции записи геометрии
    bool ensureFrameCapacity(size_t verts);
    void appendVertex(float px, float py, float pz, float nx, float ny, float nz,
                      float r, float g, float b);
    void pushCmd(int pipeline, uint32_t first, uint32_t count, const glm::mat4& mvp,
                 glm::vec3 lightDir, bool lit, glm::vec3 tint, float alpha, float pointSize);

    // Состояние сцены на текущий кадр
    glm::mat4 sceneViewProj{1.0f};
    glm::mat4 sceneModelMatrix{1.0f};
    glm::vec3 sceneCamera{0.0f};
    glm::vec3 sceneLight{0.0f, 1.0f, 0.0f};
    bool sceneLighting = true;
    bool inFrame = false;
    std::chrono::steady_clock::time_point frameStart;
};

// Global renderer
extern IRenderer* g_renderer;
extern RendererAPI g_currentAPI;
extern RendererConfig g_rendererConfig;

// API
bool initRenderer(GLFWwindow* window, RendererAPI api = RendererAPI::Auto);
void shutdownRenderer();
bool isRendererVulkan();
bool isRendererOpenGL();
const char* getRendererName();
void setRendererAPI(RendererAPI api);
RendererAPI getRendererAPI();

// Vulkan shader compilation (runtime GLSL -> SPIR-V if glslc available, else embedded)
std::vector<uint32_t> compileGLSLtoSPIRV(const std::string& glsl, const std::string& stage);
bool hasGlslc();

// For Linux port — file dialog abstraction
std::string openFileDialogNative();
std::string saveFileDialogNative(const std::string& defaultName);
