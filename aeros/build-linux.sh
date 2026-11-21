#!/bin/bash
# Aeros Engine — Linux build script v1.19.0
# Поддержка Vulkan + OpenGL, авто-загрузка зависимостей
# Использование: ./build-linux.sh [opengl|vulkan|all|clean|install-deps]

set -e

ARCH=$(uname -m)
VERSION=$(cat ../VERSION 2>/dev/null || echo "1.19.0")
MODE=${1:-all}

echo "[Aeros] Building Aeros Engine v$VERSION for Linux $ARCH — mode: $MODE"
echo "[Aeros] Date: $(date)"

# Цвета
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

info() { echo -e "${BLUE}[INFO]${NC} $1"; }
ok() { echo -e "${GREEN}[OK]${NC} $1"; }
warn() { echo -e "${YELLOW}[WARN]${NC} $1"; }
err() { echo -e "${RED}[ERROR]${NC} $1"; }

# Проверка зависимостей
check_deps() {
    info "Checking dependencies..."
    local missing=0

    if ! command -v g++ >/dev/null 2>&1; then err "g++ not found — install build-essential"; missing=1; fi
    if ! command -v pkg-config >/dev/null 2>&1; then warn "pkg-config not found"; fi

    if pkg-config --exists glfw3 2>/dev/null; then ok "glfw3: system"; else warn "glfw3: not found via pkg-config, will try bundled or need libglfw3-dev"; fi
    if pkg-config --exists vulkan 2>/dev/null; then ok "Vulkan: system"; else warn "Vulkan: not found — install libvulkan-dev for Vulkan support"; fi
    if command -v glslc >/dev/null 2>&1; then ok "glslc: found (shader compiler)"; else warn "glslc: not found — install shaderc or glslc"; fi
    if command -v nvcc >/dev/null 2>&1; then ok "CUDA: found"; else info "CUDA: not found — CPU fallback will be used"; fi
    if command -v zenity >/dev/null 2>&1 || command -v kdialog >/dev/null 2>&1; then ok "File dialog: zenity/kdialog found"; else warn "File dialog: zenity/kdialog not found — will use console fallback"; fi

    if [ $missing -eq 1 ]; then
        err "Missing critical dependencies. Run: ./build-linux.sh install-deps"
        exit 1
    fi
}

download_deps() {
    info "Downloading bundled dependencies..."
    mkdir -p ../libs/glm ../libs/glad/include/glad ../libs/glad/src ../libs/glad/include/KHR

    # GLM
    if [ ! -f ../libs/glm/glm/glm.hpp ]; then
        info "Downloading GLM..."
        if command -v git >/dev/null 2>&1; then
            rm -rf /tmp/glm_aeros
            git clone --depth 1 https://github.com/g-truc/glm.git /tmp/glm_aeros
            cp -r /tmp/glm_aeros/glm ../libs/glm/ 2>/dev/null || mkdir -p ../libs/glm && cp -r /tmp/glm_aeros/glm ../libs/glm/
            rm -rf /tmp/glm_aeros
            ok "GLM downloaded"
        else
            warn "git not found, trying curl..."
            curl -sL https://github.com/g-truc/glm/archive/refs/tags/0.9.9.8.tar.gz | tar -xz -C /tmp
            cp -r /tmp/glm-0.9.9.8/glm ../libs/glm/ 2>/dev/null || true
        fi
    else
        ok "GLM already present"
    fi

    # GLAD
    if [ ! -f ../libs/glad/include/glad/glad.h ]; then
        info "Downloading GLAD..."
        mkdir -p ../libs/glad/include/glad ../libs/glad/src ../libs/glad/include/KHR
        if command -v curl >/dev/null 2>&1; then
            curl -sL https://raw.githubusercontent.com/Dav1dde/glad/master/include/glad/glad.h -o ../libs/glad/include/glad/glad.h
            curl -sL https://raw.githubusercontent.com/Dav1dde/glad/master/src/glad.c -o ../libs/glad/src/glad.c
            curl -sL https://raw.githubusercontent.com/Dav1dde/glad/master/include/KHR/khrplatform.h -o ../libs/glad/include/KHR/khrplatform.h
        elif command -v wget >/dev/null 2>&1; then
            wget -q https://raw.githubusercontent.com/Dav1dde/glad/master/include/glad/glad.h -O ../libs/glad/include/glad/glad.h
            wget -q https://raw.githubusercontent.com/Dav1dde/glad/master/src/glad.c -O ../libs/glad/src/glad.c
            wget -q https://raw.githubusercontent.com/Dav1dde/glad/master/include/KHR/khrplatform.h -O ../libs/glad/include/KHR/khrplatform.h
        fi
        ok "GLAD downloaded"
    else
        ok "GLAD already present"
    fi

    # GLFW — prefer system, but download header if missing
    if [ ! -f ../libs/glfw/include/GLFW/glfw3.h ]; then
        warn "GLFW header not in libs — will use system glfw3 via pkg-config"
        mkdir -p ../libs/glfw/include/GLFW
        if command -v curl >/dev/null 2>&1; then
            curl -sL https://raw.githubusercontent.com/glfw/glfw/master/include/GLFW/glfw3.h -o ../libs/glfw/include/GLFW/glfw3.h 2>/dev/null || true
            curl -sL https://raw.githubusercontent.com/glfw/glfw/master/include/GLFW/glfw3native.h -o ../libs/glfw/include/GLFW/glfw3native.h 2>/dev/null || true
        fi
    fi

    # ImGui — check
    if [ ! -f src/imgui/imgui.h ]; then
        info "Downloading ImGui..."
        mkdir -p src/imgui
        if command -v git >/dev/null 2>&1; then
            rm -rf /tmp/imgui_aeros
            git clone --depth 1 https://github.com/ocornut/imgui.git /tmp/imgui_aeros
            cp /tmp/imgui_aeros/*.cpp /tmp/imgui_aeros/*.h src/imgui/ 2>/dev/null || true
            cp /tmp/imgui_aeros/backends/imgui_impl_glfw.* src/imgui/ 2>/dev/null || true
            cp /tmp/imgui_aeros/backends/imgui_impl_opengl3.* src/imgui/ 2>/dev/null || true
            rm -rf /tmp/imgui_aeros
            ok "ImGui downloaded"
        fi
    fi

    ok "Dependencies downloaded"
}

install_deps() {
    info "Installing system dependencies..."
    if command -v apt-get >/dev/null 2>&1; then
        info "Detected apt (Debian/Ubuntu)"
        sudo apt-get update
        sudo apt-get install -y build-essential cmake pkg-config \
            libglfw3-dev libgl1-mesa-dev libglu1-mesa-dev \
            libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev \
            libvulkan-dev vulkan-tools glslc shaderc \
            zenity kdialog libopenmp-dev git curl wget
    elif command -v dnf >/dev/null 2>&1; then
        info "Detected dnf (Fedora)"
        sudo dnf install -y gcc-c++ cmake pkgconfig glfw-devel mesa-libGL-devel \
            libX11-devel libXrandr-devel libXinerama-devel libXcursor-devel libXi-devel \
            vulkan-devel vulkan-tools glslc shaderc zenity kdialog libomp-devel git curl wget
    elif command -v pacman >/dev/null 2>&1; then
        info "Detected pacman (Arch)"
        sudo pacman -S --noconfirm base-devel cmake pkgconf glfw mesa libx11 libxrandr libxinerama libxcursor libxi vulkan-headers vulkan-tools shaderc zenity kdialog openmp git curl wget
    elif command -v zypper >/dev/null 2>&1; then
        info "Detected zypper (openSUSE)"
        sudo zypper install -y gcc-c++ cmake pkg-config glfw-devel Mesa-libGL-devel libX11-devel libXrandr-devel libXinerama-devel libXcursor-devel libXi-devel vulkan-devel vulkan-tools glslc zenity kdialog libomp-devel git curl wget
    else
        err "Unknown package manager. Please install manually:"
        echo "  build-essential, cmake, libglfw3-dev, libGL, X11, vulkan, zenity, openmp"
        exit 1
    fi
    ok "System dependencies installed"
    download_deps
}

build() {
    local target=$1
    info "Building $target..."
    mkdir -p bin obj

    # Use Makefile.linux
    if [ -f Makefile.linux ]; then
        make -f Makefile.linux $target
    else
        err "Makefile.linux not found"
        exit 1
    fi
}

clean() {
    info "Cleaning..."
    if [ -f Makefile.linux ]; then make -f Makefile.linux clean; fi
    rm -rf obj bin/aeros-engine-linux-* bin/aeros-engine-vulkan-* bin/aeros-engine-opengl-* bin/aeros-engine
    ok "Cleaned"
}

# Main
case $MODE in
    install-deps)
        install_deps
        ;;
    download-deps)
        download_deps
        ;;
    check-deps)
        check_deps
        ;;
    clean)
        clean
        ;;
    opengl)
        check_deps
        download_deps
        build opengl
        ;;
    vulkan)
        check_deps
        download_deps
        build vulkan
        ;;
    all|"")
        check_deps
        download_deps
        build all
        echo ""
        ok "Build complete!"
        echo ""
        info "Binaries:"
        ls -lh bin/aeros-engine* 2>/dev/null || true
        echo ""
        info "Run: ./bin/aeros-engine"
        if [ -f bin/aeros-engine ]; then
            info "Or: ./bin/aeros-engine-linux-$ARCH"
        fi
        ;;
    help|--help|-h)
        echo "Aeros Engine Linux Build Script v1.19.0"
        echo "Usage: ./build-linux.sh [mode]"
        echo "Modes:"
        echo "  all          — build with auto-detect (Vulkan if available, else OpenGL)"
        echo "  opengl       — build OpenGL only"
        echo "  vulkan       — build Vulkan only"
        echo "  install-deps — install system dependencies"
        echo "  download-deps — download bundled dependencies (GLM, GLAD)"
        echo "  check-deps   — check dependencies"
        echo "  clean        — clean build artifacts"
        echo "  help         — this help"
        ;;
    *)
        err "Unknown mode: $MODE"
        echo "Use: ./build-linux.sh help"
        exit 1
        ;;
esac
