#pragma once
// =====================================================
// v1.33.0 — динамическая загрузка Vulkan.
//
// Зачем так: для сборки с Vulkan обычно нужен Vulkan SDK и библиотека импорта
// (vulkan-1.lib). Тогда движок не собрался бы у того, кто просто клонировал
// репозиторий, а релиз зависел бы от установленного SDK на сборочном агенте.
// Здесь достаточно ЗАГОЛОВКОВ Vulkan (они лежат в libs/vulkan/include), а
// функции берутся у системного загрузчика vulkan-1.dll / libvulkan.so.1 через
// vkGetInstanceProcAddr / vkGetDeviceProcAddr. Если загрузчика или подходящего
// устройства нет — честный отказ и возврат на OpenGL.
//
// VK_NO_PROTOTYPES отключает прямые объявления: все вызовы идут через таблицу
// ниже, поэтому линковать нечего.
// =====================================================

#if defined(AEROS_HAS_VULKAN)

#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES 1
#endif
#include <vulkan/vulkan.h>

#include <cstring>
#include <string>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <dlfcn.h>
#endif

// Список функций, которые нужны рендереру и интерфейсу. X-макрос: из одного
// списка получаются и объявления указателей, и их загрузка — рассинхрона быть
// не может.
#define AEROS_VK_FUNCS(X)                                    \
    X(vkGetInstanceProcAddr)                                 \
    X(vkGetDeviceProcAddr)                                   \
    X(vkEnumerateInstanceVersion)                            \
    X(vkEnumerateInstanceExtensionProperties)                \
    X(vkEnumerateInstanceLayerProperties)                    \
    X(vkCreateInstance)                                      \
    X(vkDestroyInstance)                                     \
    X(vkEnumerateDeviceExtensionProperties)                  \
    X(vkEnumeratePhysicalDevices)                            \
    X(vkGetPhysicalDeviceProperties)                         \
    X(vkGetPhysicalDeviceFeatures)                           \
    X(vkGetPhysicalDeviceMemoryProperties)                   \
    X(vkGetPhysicalDeviceFormatProperties)                   \
    X(vkGetPhysicalDeviceQueueFamilyProperties)              \
    X(vkGetPhysicalDeviceSurfaceSupportKHR)                  \
    X(vkGetPhysicalDeviceSurfaceCapabilitiesKHR)             \
    X(vkGetPhysicalDeviceSurfaceFormatsKHR)                  \
    X(vkGetPhysicalDeviceSurfacePresentModesKHR)             \
    X(vkDestroySurfaceKHR)                                   \
    X(vkCreateDevice)                                        \
    X(vkDestroyDevice)                                       \
    X(vkGetDeviceQueue)                                      \
    X(vkDeviceWaitIdle)                                      \
    X(vkQueueWaitIdle)                                       \
    X(vkQueueSubmit)                                         \
    X(vkQueuePresentKHR)                                     \
    X(vkCreateSwapchainKHR)                                  \
    X(vkDestroySwapchainKHR)                                 \
    X(vkGetSwapchainImagesKHR)                               \
    X(vkAcquireNextImageKHR)                                 \
    X(vkCreateImage)                                         \
    X(vkDestroyImage)                                        \
    X(vkCreateImageView)                                     \
    X(vkDestroyImageView)                                    \
    X(vkGetImageMemoryRequirements)                          \
    X(vkAllocateMemory)                                      \
    X(vkFreeMemory)                                          \
    X(vkBindImageMemory)                                     \
    X(vkCreateBuffer)                                        \
    X(vkDestroyBuffer)                                       \
    X(vkGetBufferMemoryRequirements)                         \
    X(vkBindBufferMemory)                                    \
    X(vkMapMemory)                                           \
    X(vkUnmapMemory)                                         \
    X(vkCreateRenderPass)                                    \
    X(vkDestroyRenderPass)                                   \
    X(vkCreateFramebuffer)                                   \
    X(vkDestroyFramebuffer)                                  \
    X(vkCreateShaderModule)                                  \
    X(vkDestroyShaderModule)                                 \
    X(vkCreatePipelineLayout)                                \
    X(vkDestroyPipelineLayout)                               \
    X(vkCreateGraphicsPipelines)                             \
    X(vkDestroyPipeline)                                     \
    X(vkCreateDescriptorSetLayout)                           \
    X(vkDestroyDescriptorSetLayout)                          \
    X(vkCreateDescriptorPool)                                \
    X(vkDestroyDescriptorPool)                               \
    X(vkResetDescriptorPool)                                 \
    X(vkAllocateDescriptorSets)                              \
    X(vkUpdateDescriptorSets)                                \
    X(vkCreateSampler)                                       \
    X(vkDestroySampler)                                      \
    X(vkCreateCommandPool)                                   \
    X(vkDestroyCommandPool)                                  \
    X(vkAllocateCommandBuffers)                              \
    X(vkFreeCommandBuffers)                                  \
    X(vkResetCommandBuffer)                                  \
    X(vkBeginCommandBuffer)                                  \
    X(vkEndCommandBuffer)                                    \
    X(vkCmdBeginRenderPass)                                  \
    X(vkCmdEndRenderPass)                                    \
    X(vkCmdBindPipeline)                                     \
    X(vkCmdBindVertexBuffers)                                \
    X(vkCmdBindDescriptorSets)                               \
    X(vkCmdDraw)                                             \
    X(vkCmdSetLineWidth)                                     \
    X(vkCmdSetViewport)                                      \
    X(vkCmdSetScissor)                                       \
    X(vkCmdPushConstants)                                    \
    X(vkCmdPipelineBarrier)                                  \
    X(vkCreateSemaphore)                                     \
    X(vkDestroySemaphore)                                    \
    X(vkCreateFence)                                         \
    X(vkDestroyFence)                                        \
    X(vkWaitForFences)                                       \
    X(vkResetFences)

#define AEROS_VK_DECL(name) static PFN_##name p_##name = nullptr;
AEROS_VK_FUNCS(AEROS_VK_DECL)
#undef AEROS_VK_DECL

// Загрузчик (vulkan-1.dll / libvulkan.so.1). Держим открытым до выхода.
struct AerosVkLoader {
    void* lib = nullptr;
    bool loaded = false;
    std::string error;

    bool open() {
        if (loaded) return true;
#ifdef _WIN32
        // Строковый литерал уже является const char*: (char*)"vulkan-1.dll" —
        // единственное приведение, которое здесь нужно. Прежняя строка с
        // .data() не компилировалась MSVC (у массива нет метода data()).
        lib = (void*)LoadLibraryA("vulkan-1.dll");
#else
        lib = dlopen("libvulkan.so.1", RTLD_NOW | RTLD_LOCAL);
        if (!lib) lib = dlopen("libvulkan.so", RTLD_NOW | RTLD_LOCAL);
#endif
        if (!lib) {
            error = "загрузчик Vulkan (vulkan-1.dll / libvulkan.so.1) не найден";
            return false;
        }
        loaded = true;
        return true;
    }

    void* symbol(const char* name) {
        if (!lib) return nullptr;
#ifdef _WIN32
        return (void*)GetProcAddress((HMODULE)lib, name);
#else
        return dlsym(lib, name);
#endif
    }
};

inline AerosVkLoader& aerosVkLoader() {
    static AerosVkLoader l;
    return l;
}

// Загрузить базовые функции (без инстанса): нужен только загрузчик.
inline bool aerosVkLoadGlobal(std::string* err) {
    AerosVkLoader& l = aerosVkLoader();
    if (!l.open()) {
        if (err) *err = l.error;
        return false;
    }
    p_vkGetInstanceProcAddr =
        (PFN_vkGetInstanceProcAddr)l.symbol("vkGetInstanceProcAddr");
    if (!p_vkGetInstanceProcAddr) {
        if (err) *err = "в загрузчике Vulkan нет vkGetInstanceProcAddr";
        return false;
    }
    // Всё остальное — через загрузчик. vkGetInstanceProcAddr с null инстансом
    // отдаёт глобальные команды; остальные придут после создания инстанса.
    p_vkEnumerateInstanceVersion = (PFN_vkEnumerateInstanceVersion)
        p_vkGetInstanceProcAddr(VK_NULL_HANDLE, "vkEnumerateInstanceVersion");
    p_vkEnumerateInstanceExtensionProperties = (PFN_vkEnumerateInstanceExtensionProperties)
        p_vkGetInstanceProcAddr(VK_NULL_HANDLE, "vkEnumerateInstanceExtensionProperties");
    p_vkEnumerateInstanceLayerProperties = (PFN_vkEnumerateInstanceLayerProperties)
        p_vkGetInstanceProcAddr(VK_NULL_HANDLE, "vkEnumerateInstanceLayerProperties");
    p_vkCreateInstance = (PFN_vkCreateInstance)
        p_vkGetInstanceProcAddr(VK_NULL_HANDLE, "vkCreateInstance");
    return p_vkCreateInstance != nullptr;
}

// Загрузить функции уровня инстанса (после vkCreateInstance).
//
// requireSurface: функции работы с поверхностью (KHR_surface) существуют, только
// если расширения поверхности были включены при создании инстанса. Проба
// доступности (isVulkanAvailable) создаёт инстанс БЕЗ оконных расширений —
// там эти функции не нужны и их отсутствие не ошибка. Раньше они считались
// обязательными, и на части драйверов проба ложно объявляла Vulkan недоступным.
inline bool aerosVkLoadInstance(VkInstance instance, bool requireSurface = true) {
    bool ok = true;
#define AEROS_VK_LOAD_INST(name)                                            \
    p_##name = (PFN_##name)p_vkGetInstanceProcAddr(instance, #name);         \
    if (!p_##name) ok = false;
    AEROS_VK_LOAD_INST(vkDestroyInstance)
    AEROS_VK_LOAD_INST(vkEnumerateDeviceExtensionProperties)
    AEROS_VK_LOAD_INST(vkEnumeratePhysicalDevices)
    AEROS_VK_LOAD_INST(vkGetPhysicalDeviceProperties)
    AEROS_VK_LOAD_INST(vkGetPhysicalDeviceFeatures)
    AEROS_VK_LOAD_INST(vkGetPhysicalDeviceMemoryProperties)
    AEROS_VK_LOAD_INST(vkGetPhysicalDeviceFormatProperties)
    AEROS_VK_LOAD_INST(vkGetPhysicalDeviceQueueFamilyProperties)
    AEROS_VK_LOAD_INST(vkCreateDevice)
    AEROS_VK_LOAD_INST(vkGetDeviceProcAddr)
#undef AEROS_VK_LOAD_INST

#define AEROS_VK_LOAD_SURFACE(name)                                         \
    p_##name = (PFN_##name)p_vkGetInstanceProcAddr(instance, #name);         \
    if (!p_##name && requireSurface) ok = false;
    AEROS_VK_LOAD_SURFACE(vkGetPhysicalDeviceSurfaceSupportKHR)
    AEROS_VK_LOAD_SURFACE(vkGetPhysicalDeviceSurfaceCapabilitiesKHR)
    AEROS_VK_LOAD_SURFACE(vkGetPhysicalDeviceSurfaceFormatsKHR)
    AEROS_VK_LOAD_SURFACE(vkGetPhysicalDeviceSurfacePresentModesKHR)
    AEROS_VK_LOAD_SURFACE(vkDestroySurfaceKHR)
#undef AEROS_VK_LOAD_SURFACE
    return ok;
}

// Загрузить функции уровня устройства (после vkCreateDevice).
inline bool aerosVkLoadDevice(VkDevice device) {
    bool ok = true;
#define AEROS_VK_LOAD_DEV(name)                                     \
    p_##name = (PFN_##name)p_vkGetDeviceProcAddr(device, #name);    \
    if (!p_##name) ok = false;
    AEROS_VK_LOAD_DEV(vkDestroyDevice)
    AEROS_VK_LOAD_DEV(vkGetDeviceQueue)
    AEROS_VK_LOAD_DEV(vkDeviceWaitIdle)
    AEROS_VK_LOAD_DEV(vkQueueWaitIdle)
    AEROS_VK_LOAD_DEV(vkQueueSubmit)
    AEROS_VK_LOAD_DEV(vkQueuePresentKHR)
    AEROS_VK_LOAD_DEV(vkCreateSwapchainKHR)
    AEROS_VK_LOAD_DEV(vkDestroySwapchainKHR)
    AEROS_VK_LOAD_DEV(vkGetSwapchainImagesKHR)
    AEROS_VK_LOAD_DEV(vkAcquireNextImageKHR)
    AEROS_VK_LOAD_DEV(vkCreateImage)
    AEROS_VK_LOAD_DEV(vkDestroyImage)
    AEROS_VK_LOAD_DEV(vkCreateImageView)
    AEROS_VK_LOAD_DEV(vkDestroyImageView)
    AEROS_VK_LOAD_DEV(vkGetImageMemoryRequirements)
    AEROS_VK_LOAD_DEV(vkAllocateMemory)
    AEROS_VK_LOAD_DEV(vkFreeMemory)
    AEROS_VK_LOAD_DEV(vkBindImageMemory)
    AEROS_VK_LOAD_DEV(vkCreateBuffer)
    AEROS_VK_LOAD_DEV(vkDestroyBuffer)
    AEROS_VK_LOAD_DEV(vkGetBufferMemoryRequirements)
    AEROS_VK_LOAD_DEV(vkBindBufferMemory)
    AEROS_VK_LOAD_DEV(vkMapMemory)
    AEROS_VK_LOAD_DEV(vkUnmapMemory)
    AEROS_VK_LOAD_DEV(vkCreateRenderPass)
    AEROS_VK_LOAD_DEV(vkDestroyRenderPass)
    AEROS_VK_LOAD_DEV(vkCreateFramebuffer)
    AEROS_VK_LOAD_DEV(vkDestroyFramebuffer)
    AEROS_VK_LOAD_DEV(vkCreateShaderModule)
    AEROS_VK_LOAD_DEV(vkDestroyShaderModule)
    AEROS_VK_LOAD_DEV(vkCreatePipelineLayout)
    AEROS_VK_LOAD_DEV(vkDestroyPipelineLayout)
    AEROS_VK_LOAD_DEV(vkCreateGraphicsPipelines)
    AEROS_VK_LOAD_DEV(vkDestroyPipeline)
    AEROS_VK_LOAD_DEV(vkCreateDescriptorSetLayout)
    AEROS_VK_LOAD_DEV(vkDestroyDescriptorSetLayout)
    AEROS_VK_LOAD_DEV(vkCreateDescriptorPool)
    AEROS_VK_LOAD_DEV(vkDestroyDescriptorPool)
    AEROS_VK_LOAD_DEV(vkResetDescriptorPool)
    AEROS_VK_LOAD_DEV(vkAllocateDescriptorSets)
    AEROS_VK_LOAD_DEV(vkUpdateDescriptorSets)
    AEROS_VK_LOAD_DEV(vkCreateSampler)
    AEROS_VK_LOAD_DEV(vkDestroySampler)
    AEROS_VK_LOAD_DEV(vkCreateCommandPool)
    AEROS_VK_LOAD_DEV(vkDestroyCommandPool)
    AEROS_VK_LOAD_DEV(vkAllocateCommandBuffers)
    AEROS_VK_LOAD_DEV(vkFreeCommandBuffers)
    AEROS_VK_LOAD_DEV(vkResetCommandBuffer)
    AEROS_VK_LOAD_DEV(vkBeginCommandBuffer)
    AEROS_VK_LOAD_DEV(vkEndCommandBuffer)
    AEROS_VK_LOAD_DEV(vkCmdBeginRenderPass)
    AEROS_VK_LOAD_DEV(vkCmdEndRenderPass)
    AEROS_VK_LOAD_DEV(vkCmdBindPipeline)
    AEROS_VK_LOAD_DEV(vkCmdBindVertexBuffers)
    AEROS_VK_LOAD_DEV(vkCmdBindDescriptorSets)
    AEROS_VK_LOAD_DEV(vkCmdDraw)
    AEROS_VK_LOAD_DEV(vkCmdSetLineWidth)
    AEROS_VK_LOAD_DEV(vkCmdSetViewport)
    AEROS_VK_LOAD_DEV(vkCmdSetScissor)
    AEROS_VK_LOAD_DEV(vkCmdPushConstants)
    AEROS_VK_LOAD_DEV(vkCmdPipelineBarrier)
    AEROS_VK_LOAD_DEV(vkCreateSemaphore)
    AEROS_VK_LOAD_DEV(vkDestroySemaphore)
    AEROS_VK_LOAD_DEV(vkCreateFence)
    AEROS_VK_LOAD_DEV(vkDestroyFence)
    AEROS_VK_LOAD_DEV(vkWaitForFences)
    AEROS_VK_LOAD_DEV(vkResetFences)
#undef AEROS_VK_LOAD_DEV
    return ok;
}

#endif  // AEROS_HAS_VULKAN
