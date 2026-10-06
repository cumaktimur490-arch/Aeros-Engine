#!/bin/bash
# Aeros Engine — Lite сборка для слабых устройств v1.20.0
# i3-3xxx (Ivy Bridge 2C/4T SSE4.2, нет AVX2), Intel HD 4000, GT 620M, 4GB RAM, без CUDA
# Использование: ./build-lite.sh [x64|x86|all|clean|install-deps|deps]

set -e

ARCH=$(uname -m)
VERSION=$(cat ../VERSION 2>/dev/null || echo "1.20.0")
MODE=${1:-all}

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

info() { echo -e "${BLUE}[INFO]${NC} $1"; }
ok() { echo -e "${GREEN}[OK]${NC} $1"; }
warn() { echo -e "${YELLOW}[WARN]${NC} $1"; }
err() { echo -e "${RED}[ERROR]${NC} $1"; }

echo -e "${BLUE}[Aeros Lite] Building v$VERSION Lite for $ARCH — mode: $MODE${NC}"
echo "[Aeros Lite] For i3-3xxx / HD 4000 / GT 620M / 4GB RAM / No CUDA"
echo "[Aeros Lite] Optimized: SSE2, no AVX2, low RAM, 30 FPS, small binary"

LIBDIR="../libs"
SRCDIR="src"
BINDIR="bin"
OBJDIR="obj-lite"

# Lite flags — для слабых CPU
# i3-3xxx Ivy Bridge: SSE4.2, AVX (нет AVX2) — для совместимости используем SSE2
# O1 вместо O3 для меньшего бинаря и меньшего нагрева
# No fast-math — может давать артефакты, используем precise
CXXFLAGS="-std=c++17 -O1 -msse2 -mfpmath=sse -fPIC -fopenmp"
CXXFLAGS+=" -I$LIBDIR/glad/include -I$LIBDIR/glfw/include -I$LIBDIR/glm -I$SRCDIR -I$SRCDIR/imgui"
CXXFLAGS+=" -DAEROS_LITE -DCPU_ONLY -DLITE_MODE -DVERSION=\"\\\"$VERSION Lite\\\"\" -DLINUX -DGLFW_INCLUDE_NONE"
CXXFLAGS+=" -D_GLIBCXX_USE_CXX11_ABI=1"

# Check pkg-config
if command -v pkg-config >/dev/null 2>&1; then
    if pkg-config --exists glfw3 2>/dev/null; then
        CXXFLAGS+=" $(pkg-config --cflags glfw3 2>/dev/null)"
        GLFW_LIBS=$(pkg-config --libs glfw3 2>/dev/null)
    else
        GLFW_LIBS="-lglfw"
    fi
else
    GLFW_LIBS="-lglfw"
fi

LDFLAGS="-fopenmp -ldl -lpthread -lGL -lX11 -lXrandr -lXinerama -lXcursor -lXi"

# No Vulkan in Lite by default (HD 4000 doesn't support Vulkan 1.3 well)
VULKAN_LIBS=""

# Sources — без CUDA
SOURCES=(
    "$SRCDIR/main.cpp"
    "$SRCDIR/globals.cpp"
    "$SRCDIR/input.cpp"
    "$SRCDIR/gl_utils.cpp"
    "$SRCDIR/stl_loader.cpp"
    "$SRCDIR/voxel_grid.cpp"
    "$SRCDIR/flow_field.cpp"
    "$SRCDIR/particles.cpp"
    "$SRCDIR/streamlines.cpp"
    "$SRCDIR/forces.cpp"
    "$SRCDIR/model.cpp"
    "$SRCDIR/ui.cpp"
    "$SRCDIR/atmosphere.cpp"
    "$SRCDIR/test_mode.cpp"
    "$SRCDIR/lbm.cpp"
    "$SRCDIR/lang.cpp"
    "$SRCDIR/fsr.cpp"
    "$SRCDIR/framegen.cpp"
    "$SRCDIR/interesting.cpp"
    "$SRCDIR/vulkan_renderer.cpp"
    "$SRCDIR/lite_config.cpp"
    "$SRCDIR/cuda_stub.cpp"
    "$SRCDIR/glad.c"
    "$SRCDIR/imgui/imgui.cpp"
    "$SRCDIR/imgui/imgui_draw.cpp"
    "$SRCDIR/imgui/imgui_tables.cpp"
    "$SRCDIR/imgui/imgui_widgets.cpp"
    "$SRCDIR/imgui/imgui_impl_glfw.cpp"
    "$SRCDIR/imgui/imgui_impl_opengl3.cpp"
)

check_deps() {
    info "Checking Lite dependencies..."
    if ! command -v g++ >/dev/null 2>&1; then err "g++ not found"; exit 1; fi
    if [ ! -f "$LIBDIR/glm/glm/glm.hpp" ]; then warn "GLM missing — run ./build-lite.sh deps"; fi
    if [ ! -f "$LIBDIR/glad/include/glad/glad.h" ]; then warn "GLAD missing — run ./build-lite.sh deps"; fi
    ok "Deps checked — Lite needs only GLFW and OpenGL (no Vulkan, no CUDA)"
}

download_deps() {
    info "Downloading Lite dependencies..."
    mkdir -p "$LIBDIR/glm" "$LIB_DIR/glad/include/glad" "$LIB_DIR/glad/src" "$LIB_DIR/glad/include/KHR" "$SRCDIR/imgui" "$BINDIR/models" 2>/dev/null || true
    # Use existing download script
    if [ -f "../installer/download_deps_linux.sh" ]; then
        bash "../installer/download_deps_linux.sh" --arch "$ARCH" || true
    fi
    if [ -f "build-linux.sh" ]; then
        ./build-linux.sh download-deps || true
    fi
    ok "Lite dependencies downloaded"
}

install_deps() {
    info "Installing Lite system dependencies (minimal)..."
    if command -v apt-get >/dev/null 2>&1; then
        sudo apt-get update
        sudo apt-get install -y build-essential pkg-config \
            libglfw3-dev libgl1-mesa-dev \
            libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev \
            libopenmp-dev git curl wget zenity
    elif command -v dnf >/dev/null 2>&1; then
        sudo dnf install -y gcc-c++ pkgconfig glfw-devel mesa-libGL-devel \
            libX11-devel libXrandr-devel libXinerama-devel libXcursor-devel libXi-devel \
            libomp-devel git curl wget zenity
    elif command -v pacman >/dev/null 2>&1; then
        sudo pacman -S --noconfirm base-devel pkgconf glfw mesa libx11 libxrandr libxinerama libxcursor libxi openmp git curl wget zenity
    fi
    ok "Lite system dependencies installed"
    download_deps
}

build_lite() {
    info "Building Lite version..."
    mkdir -p "$BINDIR" "$OBJDIR"

    # Clean previous lite objs
    rm -f "$OBJDIR"/*.o "$BINDIR"/aeros-engine-lite* "$BINDIR"/main-lite* 2>/dev/null || true

    # Compile
    OBJ_FILES=()
    for src in "${SOURCES[@]}"; do
        obj="$OBJDIR/$(basename "$src" .cpp).o"
        obj="${obj/.c/.o}"
        OBJ_FILES+=("$obj")
        mkdir -p "$(dirname "$obj")"
        if [[ "$src" == *.cpp ]]; then
            echo "[CXX Lite] $src"
            g++ $CXXFLAGS -c "$src" -o "$obj"
        elif [[ "$src" == *.c ]]; then
            echo "[CC Lite] $src"
            gcc $CXXFLAGS -c "$src" -o "$obj"
        fi
    done

    # Link
    TARGET="$BINDIR/aeros-engine-lite-linux-$ARCH"
    echo "[LD Lite] $TARGET"
    g++ "${OBJ_FILES[@]}" -o "$TARGET" $LDFLAGS $GLFW_LIBS $VULKAN_LIBS
    chmod +x "$TARGET"
    ln -sf "$(basename "$TARGET")" "$BINDIR/aeros-engine-lite" 2>/dev/null || true
    ln -sf "$(basename "$TARGET")" "$BINDIR/main-lite" 2>/dev/null || true

    # Size info
    SIZE=$(du -h "$TARGET" | cut -f1)
    ok "Lite built: $TARGET — Size: $SIZE"
    info "Lite binary is smaller, uses SSE2, no AVX2, no CUDA, 30 FPS target"

    # Compare with full if exists
    if [ -f "$BINDIR/aeros-engine-linux-$ARCH" ]; then
        FULL_SIZE=$(du -h "$BINDIR/aeros-engine-linux-$ARCH" | cut -f1)
        info "Full version size: $FULL_SIZE vs Lite: $SIZE — Lite should be smaller"
    fi

    echo ""
    ok "Lite build complete for i3-3xxx / HD 4000 / 4GB RAM"
    echo ""
    info "Run: ./bin/aeros-engine-lite --help"
    info "Or:  ./bin/aeros-engine-lite model.stl"
}

clean() {
    info "Cleaning Lite..."
    rm -rf "$OBJDIR" "$BINDIR"/aeros-engine-lite* "$BINDIR"/main-lite*
    ok "Cleaned"
}

# Main
case $MODE in
    install-deps) install_deps ;;
    deps|download-deps) download_deps ;;
    check-deps) check_deps ;;
    clean) clean ;;
    lite|all|"") check_deps; download_deps; build_lite ;;
    help|--help|-h)
        echo "Aeros Engine Lite Build Script v1.20.0"
        echo "For weak devices: i3-3xxx, Intel HD 4000, GT 620M, 4GB RAM, no CUDA"
        echo "Usage: ./build-lite.sh [mode]"
        echo "Modes:"
        echo "  all          — build Lite (default)"
        echo "  lite         — build Lite"
        echo "  install-deps — install minimal system deps (no Vulkan)"
        echo "  deps         — download bundled deps"
        echo "  check-deps   — check deps"
        echo "  clean        — clean"
        echo "  help         — this help"
        echo ""
        echo "Lite optimizations:"
        echo "  - SSE2 only (no AVX2) — compatible with i3-3xxx Ivy Bridge"
        echo "  - O1 (not O3) — smaller binary, less heat"
        echo "  - No CUDA — CPU only"
        echo "  - No Vulkan by default — HD 4000 doesn't support 1.3 well"
        echo "  - Low particles 1500 (was 15000), streamlines 8x80 (was 24x300)"
        echo "  - Voxel 24 (was 48) — 8x less memory"
        echo "  - LBM OFF by default, 32 res, 1 step/frame"
        echo "  - Target 30 FPS, VSync ON, power saving"
        echo "  - Smaller binary, low RAM <512MB, works on 1366x768 laptops"
        ;;
    *) err "Unknown mode: $MODE"; exit 1;;
esac
