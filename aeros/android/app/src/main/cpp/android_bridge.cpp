#include "android_bridge.h"
#include <android/log.h>
#include <sys/system_properties.h>
#include <string>

#define LOGI(...) ((void)__android_log_print(ANDROID_LOG_INFO, "AerosEngine", __VA_ARGS__))

void androidBridgeInit(android_app* app) {
    LOGI("Android Bridge Init");
}

const char* androidGetDeviceInfo() {
    static char info[512];
    char brand[PROP_VALUE_MAX], model[PROP_VALUE_MAX], version[PROP_VALUE_MAX];
    __system_property_get("ro.product.brand", brand);
    __system_property_get("ro.product.model", model);
    __system_property_get("ro.build.version.release", version);
    snprintf(info, sizeof(info), "%s %s Android %s", brand, model, version);
    return info;
}

bool androidIsLowRamDevice() {
    // Проверяем low RAM — для Android Go и старых телефонов
    char lowRam[PROP_VALUE_MAX];
    __system_property_get("ro.config.low_ram", lowRam);
    return (lowRam[0] == 't' || lowRam[0] == '1');
}

int androidGetApiLevel() {
    char api[PROP_VALUE_MAX];
    __system_property_get("ro.build.version.sdk", api);
    return atoi(api);
}
