#!/bin/bash
# Aeros Engine — Android APK сборка v1.20.1 — самая оптимизированная для слабых телефонов
# Для старых телефонов: Adreno 306, Mali-400, 1-2GB RAM, Android 5.0+
# Использование: ./build-android.sh [lite|potato|full|clean|deps]

set -e

MODE=${1:-lite}
VERSION=$(cat ../../VERSION 2>/dev/null || cat ../VERSION 2>/dev/null || echo "1.20.1")

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

info() { echo -e "${BLUE}[INFO]${NC} $1"; }
ok() { echo -e "${GREEN}[OK]${NC} $1"; }
warn() { echo -e "${YELLOW}[WARN]${NC} $1"; }
err() { echo -e "${RED}[ERROR]${NC} $1"; }

echo -e "${BLUE}[Aeros Android] Building v$VERSION Android $MODE — for weak phones${NC}"

# Проверка Android SDK/NDK
check_sdk() {
    if [ -z "$ANDROID_HOME" ] && [ -z "$ANDROID_SDK_ROOT" ]; then
        warn "ANDROID_HOME not set — trying common locations"
        for dir in "$HOME/Android/Sdk" "$HOME/Android/Sdk" "/opt/android-sdk" "/usr/local/android-sdk" "$HOME/Library/Android/sdk"; do
            if [ -d "$dir" ]; then
                export ANDROID_HOME="$dir"
                info "Found Android SDK at $dir"
                break
            fi
        done
    fi

    if [ -z "$ANDROID_HOME" ]; then
        err "Android SDK not found — install Android Studio or SDK"
        echo "  export ANDROID_HOME=/path/to/Android/Sdk"
        echo "  Or install: https://developer.android.com/studio"
        return 1
    fi

    if [ ! -d "$ANDROID_HOME/ndk" ] && [ -z "$ANDROID_NDK_HOME" ]; then
        warn "NDK not found in SDK — trying to find"
        NDK_DIR=$(ls -d "$ANDROID_HOME/ndk/"* 2>/dev/null | sort -V | tail -n1)
        if [ -n "$NDK_DIR" ]; then
            export ANDROID_NDK_HOME="$NDK_DIR"
            info "Found NDK at $NDK_DIR"
        else
            err "Android NDK not found — install via SDK Manager: sdkmanager --install 'ndk;25.1.8937393'"
            return 1
        fi
    fi

    ok "Android SDK: $ANDROID_HOME"
    ok "Android NDK: ${ANDROID_NDK_HOME:-$ANDROID_HOME/ndk}"
}

build_apk() {
    local BUILD_TYPE=$1
    info "Building APK — type: $BUILD_TYPE"

    # Проверяем gradle
    if [ -f "./gradlew" ]; then
        GRADLE="./gradlew"
    elif command -v gradle >/dev/null 2>&1; then
        GRADLE="gradle"
    else
        err "Gradle not found — install gradle or use Android Studio"
        echo "  Or download gradle wrapper: gradle wrapper"
        return 1
    fi

    # Сборка
    if [ "$BUILD_TYPE" = "potato" ]; then
        info "Building Potato APK — for weakest phones 1GB RAM Adreno 306"
        $GRADLE assemblePotatoRelease --info || $GRADLE :app:assemblePotatoRelease
    elif [ "$BUILD_TYPE" = "lite" ]; then
        info "Building Lite APK — for weak phones 2-4GB RAM Adreno 405+"
        $GRADLE assembleRelease --info || $GRADLE :app:assembleRelease
    elif [ "$BUILD_TYPE" = "full" ]; then
        info "Building Full APK — for modern phones 4GB+ RAM"
        $GRADLE assembleRelease -Pfull=true || $GRADLE :app:assembleRelease
    else
        $GRADLE assembleRelease
    fi

    # Поиск APK
    APK=$(find app/build/outputs/apk -name "*.apk" 2>/dev/null | head -n1)
    if [ -n "$APK" ]; then
        SIZE=$(du -h "$APK" | cut -f1)
        ok "APK built: $APK — Size: $SIZE"
        info "For weak phones, APK should be <20 MB (Potato <10 MB)"

        # Копируем в release
        mkdir -p ../../release
        cp "$APK" "../../release/aeros-engine-android-$BUILD_TYPE-v$VERSION.apk"
        ok "Copied to ../../release/aeros-engine-android-$BUILD_TYPE-v$VERSION.apk"

        # Информация о APK
        if command -v aapt >/dev/null 2>&1; then
            aapt dump badging "$APK" | head -n20
        fi
    else
        warn "APK not found in app/build/outputs/apk"
        find . -name "*.apk" 2>/dev/null | head -n10
    fi
}

clean() {
    info "Cleaning Android build..."
    if [ -f "./gradlew" ]; then
        ./gradlew clean || true
    fi
    rm -rf app/build .gradle build
    ok "Cleaned"
}

install_deps() {
    info "Installing Android dependencies..."
    if [ -z "$ANDROID_HOME" ]; then
        err "ANDROID_HOME not set"
        return 1
    fi

    SDKMANAGER="$ANDROID_HOME/cmdline-tools/latest/bin/sdkmanager"
    if [ ! -f "$SDKMANAGER" ]; then
        SDKMANAGER="$ANDROID_HOME/tools/bin/sdkmanager"
    fi

    if [ -f "$SDKMANAGER" ]; then
        info "Installing NDK, platform-tools, build-tools, platforms..."
        yes | $SDKMANAGER --licenses || true
        $SDKMANAGER --install "platform-tools" "build-tools;34.0.0" "platforms;android-34" "platforms;android-21" "ndk;25.1.8937393" || true
        ok "Android dependencies installed"
    else
        warn "sdkmanager not found — install Android Studio or cmdline-tools"
        echo "  https://developer.android.com/studio#command-tools"
    fi
}

# Main
case $MODE in
    lite)
        check_sdk
        build_apk lite
        ;;
    potato)
        check_sdk
        build_apk potato
        ;;
    full)
        check_sdk
        build_apk full
        ;;
    all)
        check_sdk
        build_apk potato
        build_apk lite
        ;;
    clean)
        clean
        ;;
    deps|install-deps)
        install_deps
        ;;
    check)
        check_sdk
        ;;
    help|--help|-h)
        echo "Aeros Engine Android Build Script v1.20.1"
        echo "For weak phones: Adreno 306, Mali-400, 1-2GB RAM, Android 5.0+"
        echo "Usage: ./build-android.sh [mode]"
        echo "Modes:"
        echo "  lite         — build Lite APK (default) — 2-4GB RAM, 500-1500 particles, 20-30 FPS, <20 MB"
        echo "  potato       — build Potato APK — 1GB RAM, Adreno 306, 300 particles, 15 FPS, <10 MB"
        echo "  full         — build Full APK — 4GB+ RAM, modern phones"
        echo "  all          — build both Lite and Potato"
        echo "  clean        — clean"
        echo "  deps         — install Android SDK dependencies (NDK, build-tools)"
        echo "  check        — check SDK/NDK"
        echo "  help         — this help"
        echo ""
        echo "Requirements:"
        echo "  - Android SDK (ANDROID_HOME)"
        echo "  - Android NDK (25.1.8937393+)"
        echo "  - Gradle 8.4+ or Android Studio"
        echo "  - Java 17+"
        echo ""
        echo "Optimizations for weak phones:"
        echo "  - minSdk 21 Android 5.0 Lollipop — supports old phones from 2014"
        echo "  - OpenGL ES 3.0 (fallback 2.0 for Potato) — Adreno 306 Mali-400"
        echo "  - O1 + Os — tiny binary, less heat, small APK"
        echo "  - R8 minify + shrinkResources — APK <20 MB Lite, <10 MB Potato"
        echo "  - 1-2 threads, 300-1500 particles, voxel 12-24, LBM OFF, 15-25 FPS"
        echo "  - Battery saver, auto quality scaling, touch optimized"
        echo "  - No Vulkan, no CUDA, CPU only, SSE2/NEON"
        ;;
    *)
        err "Unknown mode: $MODE"
        exit 1
        ;;
esac
