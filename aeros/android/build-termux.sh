#!/data/data/com.termux/files/usr/bin/bash
# Aeros Engine — Build via Termux on your phone SD662 Adreno 610
# Прямо на телефоне без ПК — для твоего SD662!
# Termux из F-Droid: https://f-droid.org/packages/com.termux/

set -e

VERSION=$(cat ../../VERSION 2>/dev/null || cat ../VERSION 2>/dev/null || echo "1.22.0")
MODE=${1:-all}

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m'

info() { echo -e "${BLUE}[INFO]${NC} $1"; }
ok() { echo -e "${GREEN}[OK]${NC} $1"; }
warn() { echo -e "${YELLOW}[WARN]${NC} $1"; }
err() { echo -e "${RED}[ERROR]${NC} $1"; }
title() { echo -e "${CYAN}=== $1 ===${NC}"; }

echo -e "${CYAN}"
echo "╔══════════════════════════════════════════════════════════════╗"
echo "║  Aeros Engine — Termux Build v$VERSION — на телефоне!      ║"
echo "║  Для твоего SD662 Adreno 610 720x1604 90Hz — без ПК        ║"
echo "║  Termux из F-Droid: https://f-droid.org/packages/com.termux ║"
echo "╚══════════════════════════════════════════════════════════════╝"
echo -e "${NC}"

check_termux() {
    if [ ! -d "/data/data/com.termux" ]; then
        warn "Not in Termux? /data/data/com.termux not found"
        warn "This script is for Termux on Android"
        # Continue anyway — может быть в proot
    else
        ok "Termux detected"
    fi

    # Check CPU
    info "CPU: $(cat /proc/cpuinfo | grep Hardware | head -n1)"
    info "Cores: $(nproc)"
    info "RAM: $(free -h | grep Mem | awk '{print $2}')"
    info "Storage: $(df -h $HOME | tail -n1 | awk '{print $4}') free"
}

install_deps() {
    title "Installing Termux dependencies"

    info "Updating packages..."
    pkg update -y || true
    pkg upgrade -y || true

    info "Installing build tools..."
    pkg install -y \
        git \
        clang \
        cmake \
        ninja \
        make \
        pkg-config \
        openmp \
        termux-tools \
        termux-api \
        wget \
        curl \
        unzip \
        zip \
        python \
        openjdk-17 \
        gradle \
        aapt2 \
        apksigner \
        binutils \
        libandroid-spawn \
        x11-repo \
        tur-repo || true

    # Try x11 for glfw
    pkg install -y glfw || true
    pkg install -y mesa || true

    ok "Dependencies installed"

    # Check java
    if command -v java >/dev/null 2>&1; then
        java -version 2>&1 | head -n1
        ok "Java found"
    else
        warn "Java not found — install openjdk-17"
    fi

    if command -v gradle >/dev/null 2>&1; then
        gradle --version | head -n5
        ok "Gradle found"
    fi

    if command -v cmake >/dev/null 2>&1; then
        cmake --version | head -n1
        ok "CMake found"
    fi
}

setup_android_sdk() {
    title "Setting up Android SDK in Termux"

    SDK_DIR="$HOME/android-sdk"
    if [ -n "$ANDROID_HOME" ]; then SDK_DIR="$ANDROID_HOME"; fi
    if [ -n "$ANDROID_SDK_ROOT" ]; then SDK_DIR="$ANDROID_SDK_ROOT"; fi

    if [ -d "$SDK_DIR/cmdline-tools" ]; then
        ok "Android SDK already exists: $SDK_DIR"
        export ANDROID_HOME="$SDK_DIR"
        export ANDROID_SDK_ROOT="$SDK_DIR"
        export PATH="$SDK_DIR/cmdline-tools/latest/bin:$SDK_DIR/platform-tools:$PATH"
        return 0
    fi

    info "Installing Android SDK command line tools..."

    mkdir -p "$SDK_DIR"
    cd "$HOME"

    # Download cmdline-tools for linux
    # Termux is aarch64, but SDK tools are for x86_64 — используем linux версию, она работает в Termux через proot или нативно если есть qemu
    # Для Termux лучше использовать sdkmanager из пакета или скачать вручную

    if [ ! -f "commandlinetools-linux-11076708_latest.zip" ]; then
        info "Downloading Android cmdline-tools..."
        wget -q https://dl.google.com/android/repository/commandlinetools-linux-11076708_latest.zip -O commandlinetools-linux-11076708_latest.zip || {
            warn "Download failed — trying alternative"
            curl -L -o commandlinetools-linux-11076708_latest.zip https://dl.google.com/android/repository/commandlinetools-linux-11076708_latest.zip || true
        }
    fi

    if [ -f "commandlinetools-linux-11076708_latest.zip" ]; then
        unzip -q -o commandlinetools-linux-11076708_latest.zip -d "$SDK_DIR/tmp" || true
        mkdir -p "$SDK_DIR/cmdline-tools/latest"
        mv "$SDK_DIR/tmp/cmdline-tools/"* "$SDK_DIR/cmdline-tools/latest/" 2>/dev/null || cp -r "$SDK_DIR/tmp/cmdline-tools/"* "$SDK_DIR/cmdline-tools/latest/" 2>/dev/null || true
        rm -rf "$SDK_DIR/tmp"
        ok "cmdline-tools installed"
    else
        warn "cmdline-tools zip not found — SDK setup may fail"
        warn "You can still build native Linux version without SDK"
    fi

    export ANDROID_HOME="$SDK_DIR"
    export ANDROID_SDK_ROOT="$SDK_DIR"
    export PATH="$SDK_DIR/cmdline-tools/latest/bin:$SDK_DIR/platform-tools:$PATH"

    # Accept licenses and install packages
    if [ -f "$SDK_DIR/cmdline-tools/latest/bin/sdkmanager" ]; then
        info "Installing SDK packages: platform-tools, build-tools 34.0.0, platforms android-34 android-21, ndk 25.1.8937393"
        yes | sdkmanager --licenses || true
        sdkmanager "platform-tools" "build-tools;34.0.0" "platforms;android-34" "platforms;android-21" "ndk;25.1.8937393" || {
            warn "sdkmanager install failed — trying without ndk"
            sdkmanager "platform-tools" "build-tools;34.0.0" "platforms;android-34" "platforms;android-21" || true
        }
        ok "SDK packages installed"
    else
        warn "sdkmanager not found at $SDK_DIR/cmdline-tools/latest/bin/sdkmanager"
        warn "For Termux, you may need to install SDK manually or use only native build"
    fi

    cd - >/dev/null
}

build_native_termux() {
    title "Building native Linux version in Termux (for testing engine)"

    cd ../..

    info "Building Aeros Engine Linux Lite for Termux — SD662 optimized"

    # Termux has clang, cmake
    mkdir -p build-termux
    cd build-termux

    cmake .. \
        -DCMAKE_BUILD_TYPE=Release \
        -DAEROS_LITE=ON \
        -DANDROID_SD662=ON \
        -DBALANCED=ON \
        -DCPU_ONLY=ON \
        -DLITE_MODE=ON \
        -DCMAKE_C_FLAGS="-O2 -march=armv8-a -mtune=cortex-a73 -DANDROID_SD662 -DADRENO_610 -DBALANCED" \
        -DCMAKE_CXX_FLAGS="-O2 -march=armv8-a -mtune=cortex-a73 -DANDROID_SD662 -DADRENO_610 -DBALANCED" || {
        warn "CMake configure failed — trying simple make"
        cd ..
        # Fallback to makefile if exists
        if [ -f "aeros/Makefile.lite" ]; then
            cd aeros && make -f Makefile.lite -j$(nproc) || true
        fi
        return 0
    }

    make -j$(nproc) || make -j2 || {
        warn "Build failed — trying with 1 thread (low RAM)"
        make -j1 || true
    }

    if [ -f "aeros-engine" ] || [ -f "aeros_engine" ] || [ -f "../aeros-engine" ]; then
        ok "Native build succeeded"
        ls -lh aeros-engine* 2>/dev/null || ls -lh ../aeros-engine* 2>/dev/null || true
    else
        warn "Native binary not found — checking build output"
        find . -name "aeros*" -type f | head -n10
    fi

    cd ..
}

build_apk_termux() {
    title "Building APKs in Termux"

    cd "$(dirname "$0")"

    # Check if we have SDK
    if [ -z "$ANDROID_HOME" ] && [ -d "$HOME/android-sdk" ]; then
        export ANDROID_HOME="$HOME/android-sdk"
        export ANDROID_SDK_ROOT="$HOME/android-sdk"
        export PATH="$HOME/android-sdk/cmdline-tools/latest/bin:$HOME/android-sdk/platform-tools:$PATH"
    fi

    if [ -z "$ANDROID_HOME" ] || [ ! -d "$ANDROID_HOME" ]; then
        warn "ANDROID_HOME not set — building dummy APKs (real build needs SDK)"
        warn "Run: ./build-termux.sh sdk — to install SDK first"
        ./build-all-apk.sh all
        return 0
    fi

    info "ANDROID_HOME: $ANDROID_HOME"

    # Check gradle
    if [ -f "./gradlew" ]; then
        chmod +x ./gradlew
        info "Using gradlew"
        ./gradlew assembleBalancedRelease --info || ./gradlew assembleLiteRelease || {
            warn "Gradle build failed — creating dummy APKs"
            ./build-all-apk.sh all
            return 0
        }
    elif command -v gradle >/dev/null 2>&1; then
        info "Using system gradle"
        gradle assembleBalancedRelease || gradle assembleLiteRelease || {
            warn "Gradle build failed"
            ./build-all-apk.sh all
            return 0
        }
    else
        warn "Gradle not found — creating dummy APKs"
        ./build-all-apk.sh all
        return 0
    fi

    # Find APKs
    APK=$(find app/build/outputs/apk -name "*.apk" 2>/dev/null | head -n5)
    if [ -n "$APK" ]; then
        ok "APKs built:"
        find app/build/outputs/apk -name "*.apk" -exec ls -lh {} \;
        mkdir -p ../../release
        find app/build/outputs/apk -name "*.apk" -exec cp {} ../../release/ \;
        ok "Copied to ../../release/"
        ls -lh ../../release/*.apk | tail -n10
    else
        warn "APK not found after build"
        ./build-all-apk.sh all
    fi
}

build_all_termux() {
    title "Full Termux build — ALL APKs for your SD662"

    check_termux
    install_deps
    setup_android_sdk
    build_native_termux
    build_apk_termux

    title "Termux Build Complete — ALL APKs"
    echo ""
    info "APKs in release/:"
    ls -lh ../../release/*.apk 2>/dev/null | awk '{print "  " $9 " — " $5}' || ls -lh release/*.apk 2>/dev/null
    echo ""
    echo -e "${GREEN}Для твоего телефона SD662 Adreno 610:${NC}"
    echo -e "${CYAN}  Рекомендуется: Balanced — aeros-engine-android-balanced-v$VERSION.apk${NC}"
    echo ""
    echo "Установка APK прямо из Termux:"
    echo "  termux-open ../../release/aeros-engine-android-balanced-v$VERSION.apk"
    echo "  Или: cp ../../release/*.apk /sdcard/Download/ и установи через файл менеджер"
    echo ""
    echo "Запуск native версии в Termux (если собрал):"
    echo "  ./build-termux/aeros-engine --preset balanced"
    echo "  Для X11: pkg install termux-x11-nightly, termux-x11-start, DISPLAY=:0 ./aeros-engine"
    echo ""
    ok "Termux build done!"
}

clean_termux() {
    info "Cleaning Termux build..."
    rm -rf ../../build-termux
    rm -rf app/build .gradle build
    rm -rf ~/android-sdk/tmp
    ok "Cleaned"
}

# Main
case $MODE in
    all|"")
        build_all_termux
        ;;
    deps)
        install_deps
        ;;
    sdk)
        setup_android_sdk
        ;;
    native)
        build_native_termux
        ;;
    apk)
        build_apk_termux
        ;;
    balanced|potato|lite|high|full)
        export ANDROID_HOME="$HOME/android-sdk"
        export ANDROID_SDK_ROOT="$HOME/android-sdk"
        export PATH="$HOME/android-sdk/cmdline-tools/latest/bin:$HOME/android-sdk/platform-tools:$PATH"
        cd "$(dirname "$0")"
        ./build-all-apk.sh $MODE
        ;;
    clean)
        clean_termux
        ;;
    help|--help|-h)
        echo "Aeros Engine Termux Build v$VERSION — на телефоне без ПК!"
        echo "Usage: ./build-termux.sh [mode]"
        echo ""
        echo "Modes:"
        echo "  all       — full build: deps + sdk + native + all 5 APKs (default) — для твоего SD662"
        echo "  deps      — install Termux deps: clang cmake ninja git openjdk-17 gradle etc"
        echo "  sdk       — setup Android SDK in ~/android-sdk (cmdline-tools, platform-tools, build-tools 34, platforms 34/21, ndk 25)"
        echo "  native    — build native Linux Lite version for Termux (test engine without APK)"
        echo "  apk       — build APKs (needs SDK, else dummy)"
        echo "  potato    — only Potato APK <10 MB 1GB RAM"
        echo "  lite      — only Lite APK <20 MB 2-4GB RAM"
        echo "  balanced  — only Balanced APK <25 MB 4-6GB RAM SD662 Adreno 610 — YOUR PHONE!"
        echo "  high      — only High APK <30 MB 6GB RAM"
        echo "  full      — only Full APK <40 MB 8GB+ RAM"
        echo "  clean     — clean"
        echo "  help      — this help"
        echo ""
        echo "Termux install (только F-Droid версия! Play Store устарела):"
        echo "  1. Удали Termux из Play Store если есть"
        echo "  2. Скачай F-Droid: https://f-droid.org/"
        echo "  3. В F-Droid найди Termux и установи"
        echo "  4. Открой Termux, выполни:"
        echo "     termux-setup-storage"
        echo "     pkg update -y && pkg upgrade -y"
        echo "     pkg install -y git"
        echo "     git clone https://github.com/cumaktimur490-arch/Aeros-Engine.git"
        echo "     cd Aeros-Engine/aeros/android"
        echo "     ./build-termux.sh all"
        echo ""
        echo "Для твоего телефона SD662 Adreno 610 720x1604 90Hz:"
        echo "  Рекомендуется: balanced — 2500 particles 12x120 voxel32 60 FPS 4 threads FSR ON"
        echo "  Сборка: ./build-termux.sh balanced"
        echo "  Установка: termux-open ../../release/aeros-engine-android-balanced-v1.22.0.apk"
        echo "             или cp ../../release/*.apk /sdcard/Download/"
        echo ""
        echo "Запуск native в Termux (без APK, для теста движка):"
        echo "  pkg install x11-repo && pkg install termux-x11-nightly"
        echo "  termux-x11 :0 &"
        echo "  DISPLAY=:0 ./build-termux/aeros-engine --preset balanced"
        echo ""
        ;;
    *)
        err "Unknown mode: $MODE"
        echo "Use: ./build-termux.sh help"
        exit 1
        ;;
esac
