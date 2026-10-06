#pragma once
// v1.19.0 Vulkan Support — рабочий Vulkan рендерер для Aeros Engine
// Поддерживает Windows и Linux, fallback на OpenGL если Vulkan недоступен

#include <glm/glm.hpp>
#include <vector>
#include <string>
#include <functional>

// Forward declare GLFW
struct GLFWwindow;

enum class RendererAPI {
    OpenGL = 0,
    Vulkan = 1,
    Auto = 2 // выбирает лучший доступный
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
bool initVulkanLoader();
std::string getVulkanVersionString();
std::vector<std::string> getVulkanExtensions();
std::vector<std::string> getVulkanDevices();

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

// Vulkan renderer — полная реализация
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
    const char* getAPIName() const override { return "Vulkan 1.3"; }
    float getFrameTimeMs() const override { return frameTimeMs; }
    bool isInitialized() const override { return initialized; }

    // Vulkan-specific
    bool createInstance();
    bool pickPhysicalDevice();
    bool createLogicalDevice();
    bool createSwapchain();
    bool createRenderPass();
    bool createFramebuffers();
    bool createCommandPool();
    bool createCommandBuffers();
    bool createSyncObjects();
    bool createDescriptorPool();
    bool createPipelines();
    bool createBuffers();
    void cleanupSwapchain();
    bool recreateSwapchain();

    // Stats
    std::string getDeviceName() const { return deviceName; }
    std::string getDriverVersion() const { return driverVersion; }
    int getVRAM_MB() const { return vramMB; }

private:
    bool initialized = false;
    float frameTimeMs = 0.0f;
    GLFWwindow* win = nullptr;
    RendererConfig config;

    // Vulkan handles (pimpl-like, void* to avoid including vulkan.h in header)
    void* instance = nullptr;
    void* physicalDevice = nullptr;
    void* device = nullptr;
    void* surface = nullptr;
    void* swapchain = nullptr;
    void* renderPass = nullptr;
    void* pipelineLayout = nullptr;
    void* graphicsPipeline = nullptr;
    void* particlePipeline = nullptr;
    void* linePipeline = nullptr;
    void* commandPool = nullptr;
    std::vector<void*> commandBuffers;
    std::vector<void*> swapchainImages;
    std::vector<void*> swapchainImageViews;
    std::vector<void*> framebuffers;
    std::vector<void*> imageAvailableSemaphores;
    std::vector<void*> renderFinishedSemaphores;
    std::vector<void*> inFlightFences;
    std::vector<void*> uniformBuffers;
    std::vector<void*> uniformBuffersMemory;
    void* descriptorPool = nullptr;
    void* descriptorSetLayout = nullptr;
    std::vector<void*> descriptorSets;

    // Buffers
    void* vertexBuffer = nullptr;
    void* vertexBufferMemory = nullptr;
    void* indexBuffer = nullptr;
    void* indexBufferMemory = nullptr;
    size_t vertexBufferSize = 0;

    int currentFrame = 0;
    int width = 0, height = 0;
    uint32_t imageIndex = 0;
    glm::vec3 clearCol = glm::vec3(0.05f, 0.06f, 0.09f);
    std::string deviceName = "Unknown";
    std::string driverVersion = "Unknown";
    int vramMB = 0;
    bool framebufferResized = false;

    // Internal helpers
    bool vulkanAvailable = false;
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
