#include "android_file.h"
#include <android/log.h>
#include <fstream>
#include <sys/stat.h>

#define LOGI(...) ((void)__android_log_print(ANDROID_LOG_INFO, "AerosEngine", __VA_ARGS__))
#define LOGW(...) ((void)__android_log_print(ANDROID_LOG_WARN, "AerosEngine", __VA_ARGS__))

static AAssetManager* g_assetManager = nullptr;
static std::string g_externalDir;
static std::string g_cacheDir;

void androidFileInit(AAssetManager* assetManager) {
    g_assetManager = assetManager;
    LOGI("Android File Init — assetManager %p", assetManager);

    // Для старых телефонов — используем cache dir для временных файлов
    // g_externalDir и g_cacheDir будут установлены из Java если нужно
    g_externalDir = "/data/data/com.aos.aerosengine/files";
    g_cacheDir = "/data/data/com.aos.aerosengine/cache";
}

bool androidFileExists(const char* path) {
    // Проверяем сначала в assets
    if (g_assetManager) {
        AAsset* asset = AAssetManager_open(g_assetManager, path, AASSET_MODE_UNKNOWN);
        if (asset) {
            AAsset_close(asset);
            return true;
        }
    }
    // Потом в файловой системе
    struct stat st;
    return (stat(path, &st) == 0);
}

std::vector<char> androidFileLoad(const char* path) {
    std::vector<char> data;

    // Пробуем из assets
    if (g_assetManager) {
        AAsset* asset = AAssetManager_open(g_assetManager, path, AASSET_MODE_BUFFER);
        if (asset) {
            size_t size = AAsset_getLength(asset);
            data.resize(size);
            AAsset_read(asset, data.data(), size);
            AAsset_close(asset);
            LOGI("Loaded from assets: %s %zu bytes", path, size);
            return data;
        }
    }

    // Пробуем из файловой системы — для слабых телефонов streaming
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (f.is_open()) {
        size_t size = f.tellg();
        f.seekg(0, std::ios::beg);
        data.resize(size);
        f.read(data.data(), size);
        LOGI("Loaded from file: %s %zu bytes", path, size);
        return data;
    }

    LOGW("Failed to load file: %s", path);
    return data;
}

std::string androidGetExternalFilesDir() {
    return g_externalDir;
}

std::string androidGetCacheDir() {
    return g_cacheDir;
}

std::vector<std::string> androidListAssetModels() {
    std::vector<std::string> models;
    if (!g_assetManager) return models;

    AAssetDir* dir = AAssetManager_openDir(g_assetManager, "models");
    if (!dir) {
        // Пробуем корень
        dir = AAssetManager_openDir(g_assetManager, "");
    }
    if (dir) {
        const char* file;
        while ((file = AAssetDir_getNextFileName(dir)) != nullptr) {
            std::string name(file);
            if (name.size() > 4 && name.substr(name.size()-4) == ".stl") {
                models.push_back(name);
            }
        }
        AAssetDir_close(dir);
    }
    return models;
}

bool androidCopyAssetToCache(const char* assetPath, const char* cachePath) {
    auto data = androidFileLoad(assetPath);
    if (data.empty()) return false;

    std::ofstream f(cachePath, std::ios::binary);
    if (!f.is_open()) return false;
    f.write(data.data(), data.size());
    LOGI("Copied asset %s to cache %s %zu bytes", assetPath, cachePath, data.size());
    return true;
}
