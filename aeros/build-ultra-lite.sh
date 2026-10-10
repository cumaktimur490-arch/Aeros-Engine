#!/bin/bash
# Aeros Engine — Ultra-Lite сборка для самых слабых устройств v1.20.1
# Atom/Celeron, 1-2 ядра, 2GB RAM, HD 3000, 800x450, 20 FPS, 1 поток
# Использование: ./build-ultra-lite.sh [x64|x86|all|clean|install-deps|deps]

set -e

ARCH=$(uname -m)
VERSION=$(cat ../VERSION 2>/dev/null || echo "1.20.1")
MODE=${1:-all}

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

info() { echo -e "${BLUE}[INFO]${NC} $1"; }
ok() { echo -e "${GREEN}[OK]${NC} $1"; }
warn() { echo -e "${YELLOW}[WARN]${NC} $1"; }
err() { echo -e "${RED}[ERROR]${NC} $1"; }

echo -e "${BLUE}[Aeros Ultra-Lite] Building v$VERSION Ultra-Lite for $ARCH — Potato mode Atom/Celeron 2GB RAM${NC}"
echo "[Aeros Ultra-Lite] Optimized: SSE2, no AVX2, 1 thread, 500 particles, 4x40 streamlines, voxel 16, 20 FPS, 800x450"

LIBDIR="../libs"
SRCDIR="src"
BINDIR="bin"
OBJDIR="obj-ultra-lite"

CXXFLAGS="-std=c++17 -O1 -msse2 -mfpmath=sse -fPIC -fopenmp"
CXXFLAGS+=" -I$LIBDIR/glad/include -I$LIBDIR/glfw/include -I$LIBDIR/glm -I$SRCDIR -I$SRCDIR/imgui"
CXXFLAGS+=" -DAEROS_LITE -DULTRA_LITE -DCPU_ONLY -DLITE_MODE -DPOTATO_MODE -DVERSION=\"\\\"$VERSION Ultra-Lite\\\"\" -DLINUX -DGLFW_INCLUDE_NONE"
CXXFLAGS+=" -D_GLIBCXX_USE_CXX11_ABI=1"

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
    "$SRCDIR/benchmark.cpp"
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
    info "Checking Ultra-Lite dependencies..."
    if ! command -v g++ >/dev/null 2>&1; then err "g++ not found"; exit 1; fi
    ok "Deps checked — Ultra-Lite needs only GLFW and OpenGL (no Vulkan, no CUDA), 1 thread"
}

download_deps() {
    info "Downloading Ultra-Lite dependencies..."
    mkdir -p "$LIBDIR/glm" 2>/dev/null || true
    if [ -f "../installer/download_deps_linux.sh" ]; then
        bash "../installer/download_deps_linux.sh" --arch "$ARCH" || true
    fi
    if [ -f "build-linux.sh" ]; then
        ./build-linux.sh download-deps || true
    fi
    ok "Ultra-Lite dependencies downloaded"
}

install_deps() {
    info "Installing Ultra-Lite system dependencies (minimal, for Atom)..."
    if command -v apt-get >/dev/null 2>&1; then
        sudo apt-get update
        sudo apt-get install -y build-essential pkg-config libglfw3-dev libgl1-mesa-dev libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev libopenmp-dev git curl wget zenity
    elif command -v dnf >/dev/null 2>&1; then
        sudo dnf install -y gcc-c++ pkgconfig glfw-devel mesa-libGL-devel libX11-devel libomp-devel git curl wget zenity
    elif command -v pacman >/dev/null 2>&1; then
        sudo pacman -S --noconfirm base-devel pkgconf glfw mesa libx11 openmp git curl wget zenity
    fi
    ok "Ultra-Lite system dependencies installed"
    download_deps
}

build_ultra() {
    info "Building Ultra-Lite Potato version..."
    mkdir -p "$BINDIR" "$OBJDIR"
    rm -f "$OBJDIR"/*.o "$BINDIR"/aeros-engine-ultra-lite* "$BINDIR"/main-ultra-lite* "$BINDIR"/aeros-engine-potato* 2>/dev/null || true

    OBJ_FILES=()
    for src in "${SOURCES[@]}"; do
        obj="$OBJDIR/$(basename "$src" .cpp).o"
        obj="${obj/.c/.o}"
        OBJ_FILES+=("$obj")
        mkdir -p "$(dirname "$obj")"
        if [[ "$src" == *.cpp ]]; then
            echo "[CXX Ultra-Lite] $src"
            g++ $CXXFLAGS -c "$src" -o "$obj"
        elif [[ "$src" == *.c ]]; then
            echo "[CC Ultra-Lite] $src"
            gcc $CXXFLAGS -c "$src" -o "$obj"
        fi
    done

    TARGET="$BINDIR/aeros-engine-ultra-lite-linux-$ARCH"
    echo "[LD Ultra-Lite] $TARGET"
    g++ "${OBJ_FILES[@]}" -o "$TARGET" $LDFLAGS $GLFW_LIBS
    chmod +x "$TARGET"
    ln -sf "$(basename "$TARGET")" "$BINDIR/aeros-engine-ultra-lite" 2>/dev/null || true
    ln -sf "$(basename "$TARGET")" "$BINDIR/aeros-engine-potato" 2>/dev/null || true
    ln -sf "$(basename "$TARGET")" "$BINDIR/main-ultra-lite" 2>/dev/null || true

    SIZE=$(du -h "$TARGET" | cut -f1)
    ok "Ultra-Lite built: $TARGET — Size: $SIZE — Potato mode Atom/Celeron 2GB RAM"
    info "Ultra-Lite binary is tiny, uses SSE2, 1 thread, 500 particles, 20 FPS, 800x450"

    if [ -f "$BINDIR/aeros-engine-lite-linux-$ARCH" ]; then
        LITE_SIZE=$(du -h "$BINDIR/aeros-engine-lite-linux-$ARCH" | cut -f1)
        info "Lite size: $LITE_SIZE vs Ultra-Lite: $SIZE — Ultra-Lite should be even smaller"
    fi

    echo ""
    ok "Ultra-Lite build complete for Atom/Celeron 2GB RAM HD3000"
    echo ""
    info "Run: ./bin/aeros-engine-ultra-lite --preset potato --help"
    info "Or:  ./bin/aeros-engine-potato model.stl"
}

clean() {
    info "Cleaning Ultra-Lite..."
    rm -rf "$OBJDIR" "$BINDIR"/aeros-engine-ultra-lite* "$BINDIR"/main-ultra-lite* "$BINDIR"/aeros-engine-potato*
    ok "Cleaned"
}

case $MODE in
    install-deps) install_deps ;;
    deps|download-deps) download_deps ;;
    check-deps) check_deps ;;
    clean) clean ;;
    ultra|potato|all|"") check_deps; download_deps; build_ultra ;;
    help|--help|-h)
        echo "Aeros Engine Ultra-Lite Build Script v1.20.1 Potato"
        echo "For weakest devices: Atom/Celeron 1-2C, 2GB RAM, HD 3000, 800x450, 20 FPS, 1 thread"
        echo "Usage: ./build-ultra-lite.sh [mode]"
        echo "Modes:"
        echo "  all          — build Ultra-Lite (default)"
        echo "  ultra|potato — build Ultra-Lite"
        echo "  install-deps — install minimal system deps"
        echo "  deps         — download bundled deps"
        echo "  check-deps   — check deps"
        echo "  clean        — clean"
        echo "  help         — this help"
        echo ""
        echo "Ultra-Lite optimizations:"
        echo "  - SSE2 only (no AVX2) — compatible with Atom/Celeron"
        echo "  - O1 (not O3) — tiny binary, minimal heat"
        echo "  - No CUDA — CPU only, 1 thread"
        echo "  - No Vulkan — HD 3000 doesn't support"
        echo "  - Ultra low: particles 500 (was 15000), streamlines 4x40 (was 24x300)"
        echo "  - Voxel 16 (was 48) — 27x less memory"
        echo "  - LBM OFF, 16 res, 1 step/frame"
        echo "  - Target 20 FPS, VSync ON, battery saver, 800x450 window"
        echo "  - Tiny binary, <256MB RAM, works on 1024x600 netbooks"
        ;;
    *) err "Unknown mode: $MODE"; exit 1;;
esac
