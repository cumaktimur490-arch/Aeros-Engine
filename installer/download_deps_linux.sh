#!/bin/bash
# Aeros Engine — Linux Dependency Downloader v1.19.0
# Скачивает все необходимые файлы при установке: GLFW, GLM, GLAD, ImGui, Vulkan check, models
# Использование: ./download_deps_linux.sh [--install-dir /usr/share/aeros-engine] [--arch x64] [--force]

set -e

INSTALL_DIR=""
ARCH=$(uname -m)
FORCE=0
QUIET=0

while [[ $# -gt 0 ]]; do
    case $1 in
        --install-dir) INSTALL_DIR="$2"; shift 2;;
        --arch) ARCH="$2"; shift 2;;
        --force) FORCE=1; shift;;
        --quiet) QUIET=1; shift;;
        *) shift;;
    esac
done

VERSION=$(cat ../VERSION 2>/dev/null || cat VERSION 2>/dev/null || echo "1.19.0")
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
LIB_DIR="$ROOT_DIR/libs"
AEROS_DIR="$ROOT_DIR/aeros"
if [ -z "$INSTALL_DIR" ]; then INSTALL_DIR="$AEROS_DIR/bin"; fi

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

info() { if [ $QUIET -eq 0 ]; then echo -e "${BLUE}[INFO]${NC} $1"; fi; }
ok() { if [ $QUIET -eq 0 ]; then echo -e "${GREEN}[OK]${NC} $1"; fi; }
warn() { if [ $QUIET -eq 0 ]; then echo -e "${YELLOW}[WARN]${NC} $1"; fi; }
err() { echo -e "${RED}[ERROR]${NC} $1"; }

echo -e "${BLUE}[Aeros] Dependency Downloader v$VERSION Linux $ARCH${NC}"
info "InstallDir: $INSTALL_DIR"
info "LibDir: $LIB_DIR"

mkdir -p "$INSTALL_DIR" "$LIB_DIR/glm" "$LIB_DIR/glad/include/glad" "$LIB_DIR/glad/src" "$LIB_DIR/glad/include/KHR" "$LIB_DIR/glfw/include/GLFW" "$AEROS_DIR/src/imgui" "$INSTALL_DIR/models"

downloaded=0
failed=0

# --- GLM ---
if [ ! -f "$LIB_DIR/glm/glm/glm.hpp" ] || [ $FORCE -eq 1 ]; then
    info "Downloading GLM..."
    if command -v git >/dev/null 2>&1; then
        rm -rf /tmp/glm_aeros
        git clone --depth 1 https://github.com/g-truc/glm.git /tmp/glm_aeros 2>&1 | tail -n 1
        if [ -d /tmp/glm_aeros/glm ]; then
            cp -r /tmp/glm_aeros/glm "$LIB_DIR/glm/" 2>/dev/null || mkdir -p "$LIB_DIR/glm" && cp -r /tmp/glm_aeros/glm "$LIB_DIR/glm/"
            ok "GLM downloaded via git"
            downloaded=$((downloaded+1))
        fi
        rm -rf /tmp/glm_aeros
    else
        info "git not found, trying curl..."
        if command -v curl >/dev/null 2>&1; then
            curl -sL https://github.com/g-truc/glm/archive/refs/tags/0.9.9.8.tar.gz | tar -xz -C /tmp
            cp -r /tmp/glm-0.9.9.8/glm "$LIB_DIR/glm/" 2>/dev/null || true
            ok "GLM downloaded via curl"
            downloaded=$((downloaded+1))
        elif command -v wget >/dev/null 2>&1; then
            wget -q https://github.com/g-truc/glm/archive/refs/tags/0.9.9.8.tar.gz -O /tmp/glm.tar.gz
            tar -xz -C /tmp -f /tmp/glm.tar.gz
            cp -r /tmp/glm-0.9.9.8/glm "$LIB_DIR/glm/" 2>/dev/null || true
            ok "GLM downloaded via wget"
            downloaded=$((downloaded+1))
        else
            err "Cannot download GLM — no git/curl/wget"
            failed=$((failed+1))
        fi
    fi
else
    ok "GLM already present"
fi

# --- GLAD ---
if [ ! -f "$LIB_DIR/glad/include/glad/glad.h" ] || [ $FORCE -eq 1 ]; then
    info "Downloading GLAD..."
    mkdir -p "$LIB_DIR/glad/include/glad" "$LIB_DIR/glad/src" "$LIB_DIR/glad/include/KHR"
    if command -v curl >/dev/null 2>&1; then
        curl -sL https://raw.githubusercontent.com/Dav1dde/glad/master/include/glad/glad.h -o "$LIB_DIR/glad/include/glad/glad.h"
        curl -sL https://raw.githubusercontent.com/Dav1dde/glad/master/src/glad.c -o "$LIB_DIR/glad/src/glad.c"
        curl -sL https://raw.githubusercontent.com/Dav1dde/glad/master/include/KHR/khrplatform.h -o "$LIB_DIR/glad/include/KHR/khrplatform.h"
        ok "GLAD downloaded via curl"
        downloaded=$((downloaded+1))
    elif command -v wget >/dev/null 2>&1; then
        wget -q https://raw.githubusercontent.com/Dav1dde/glad/master/include/glad/glad.h -O "$LIB_DIR/glad/include/glad/glad.h"
        wget -q https://raw.githubusercontent.com/Dav1dde/glad/master/src/glad.c -O "$LIB_DIR/glad/src/glad.c"
        wget -q https://raw.githubusercontent.com/Dav1dde/glad/master/include/KHR/khrplatform.h -O "$LIB_DIR/glad/include/KHR/khrplatform.h"
        ok "GLAD downloaded via wget"
        downloaded=$((downloaded+1))
    else
        err "Cannot download GLAD"
        failed=$((failed+1))
    fi
else
    ok "GLAD already present"
fi

# --- GLFW headers (system preferred) ---
if [ ! -f "$LIB_DIR/glfw/include/GLFW/glfw3.h" ]; then
    if pkg-config --exists glfw3 2>/dev/null; then
        ok "GLFW found via pkg-config — system"
    else
        info "Downloading GLFW headers..."
        mkdir -p "$LIB_DIR/glfw/include/GLFW"
        if command -v curl >/dev/null 2>&1; then
            curl -sL https://raw.githubusercontent.com/glfw/glfw/master/include/GLFW/glfw3.h -o "$LIB_DIR/glfw/include/GLFW/glfw3.h" 2>/dev/null || true
            curl -sL https://raw.githubusercontent.com/glfw/glfw/master/include/GLFW/glfw3native.h -o "$LIB_DIR/glfw/include/GLFW/glfw3native.h" 2>/dev/null || true
            ok "GLFW headers downloaded"
            downloaded=$((downloaded+1))
        fi
    fi
else
    ok "GLFW headers present"
fi

# --- ImGui ---
if [ ! -f "$AEROS_DIR/src/imgui/imgui.h" ] || [ $FORCE -eq 1 ]; then
    info "Downloading ImGui..."
    if command -v git >/dev/null 2>&1; then
        rm -rf /tmp/imgui_aeros
        git clone --depth 1 https://github.com/ocornut/imgui.git /tmp/imgui_aeros
        if [ -f /tmp/imgui_aeros/imgui.h ]; then
            mkdir -p "$AEROS_DIR/src/imgui"
            cp /tmp/imgui_aeros/*.cpp /tmp/imgui_aeros/*.h "$AEROS_DIR/src/imgui/" 2>/dev/null || true
            cp /tmp/imgui_aeros/backends/imgui_impl_glfw.* "$AEROS_DIR/src/imgui/" 2>/dev/null || true
            cp /tmp/imgui_aeros/backends/imgui_impl_opengl3.* "$AEROS_DIR/src/imgui/" 2>/dev/null || true
            ok "ImGui downloaded"
            downloaded=$((downloaded+1))
        fi
        rm -rf /tmp/imgui_aeros
    else
        warn "git not found — cannot download ImGui"
        failed=$((failed+1))
    fi
else
    ok "ImGui present"
fi

# --- Vulkan SDK check ---
info "Checking Vulkan SDK..."
if pkg-config --exists vulkan 2>/dev/null; then
    VULKAN_VERSION=$(pkg-config --modversion vulkan 2>/dev/null || echo "found")
    ok "Vulkan found: $VULKAN_VERSION"
    VULKAN_FOUND=1
else
    if [ -f /usr/include/vulkan/vulkan.h ] || [ -f /usr/local/include/vulkan/vulkan.h ]; then
        ok "Vulkan headers found"
        VULKAN_FOUND=1
    else
        warn "Vulkan not found — will build OpenGL only"
        info "Install: sudo apt install libvulkan-dev vulkan-tools glslc"
        info "     or: sudo dnf install vulkan-devel vulkan-tools"
        info "     or: sudo pacman -S vulkan-headers vulkan-tools shaderc"
        VULKAN_FOUND=0
    fi
fi

if command -v glslc >/dev/null 2>&1; then ok "glslc found"; else warn "glslc not found — shader compilation will use embedded SPIR-V"; fi
if command -v zenity >/dev/null 2>&1 || command -v kdialog >/dev/null 2>&1; then ok "File dialog: zenity/kdialog found"; else warn "File dialog: zenity/kdialog not found — will use console fallback"; fi

# --- Sample models ---
info "Checking sample models..."
mkdir -p "$INSTALL_DIR/models"
SAMPLE="$INSTALL_DIR/models/sample_cube.stl"
if [ ! -f "$SAMPLE" ]; then
    info "Creating sample cube STL..."
    cat > "$SAMPLE" << 'EOF'
solid cube
  facet normal 0 0 -1
    outer loop
      vertex 0 0 0
      vertex 1 0 0
      vertex 1 1 0
    endloop
  endfacet
  facet normal 0 0 -1
    outer loop
      vertex 0 0 0
      vertex 1 1 0
      vertex 0 1 0
    endloop
  endfacet
  facet normal 0 0 1
    outer loop
      vertex 0 0 1
      vertex 1 1 1
      vertex 1 0 1
    endloop
  endfacet
  facet normal 0 0 1
    outer loop
      vertex 0 0 1
      vertex 0 1 1
      vertex 1 1 1
    endloop
  endfacet
endsolid cube
EOF
    ok "Sample STL created"
fi

# --- Config ---
CONFIG="$INSTALL_DIR/aeros_settings.ini"
if [ ! -f "$CONFIG" ]; then
    info "Creating default config..."
    cat > "$CONFIG" << EOF
# Aeros Engine v$VERSION Settings — Linux
renderer=auto
api=auto
vsync=1
msaa=1
fsr=0
fg=0
version=$VERSION
vulkan=auto
opengl=1
# Interesting features
schlieren=0
volumetric=0
vortex=1
flight=0
# Linux specific
file_dialog=zenity
EOF
    ok "Default config created"
fi

# --- Desktop file ---
DESKTOP="$INSTALL_DIR/../aeros-engine.desktop"
if [ ! -f "$DESKTOP" ]; then
    cat > "$DESKTOP" << EOF
[Desktop Entry]
Name=Aeros Engine
Comment=Airflow Visualization with Vulkan and OpenGL
Exec=$INSTALL_DIR/aeros-engine
Icon=$INSTALL_DIR/../aeros/icon.png
Terminal=false
Type=Application
Categories=Science;Engineering;
Version=$VERSION
EOF
    ok "Desktop file created"
fi

# --- Summary ---
echo ""
info "=== Dependency Download Summary v$VERSION Linux $ARCH ==="
info "Downloaded: $downloaded"
if [ $failed -gt 0 ]; then warn "Failed: $failed"; else ok "Failed: 0"; fi
info "InstallDir: $INSTALL_DIR"
info "LibDir: $LIB_DIR"
info "Arch: $ARCH"
info "Vulkan: $(if [ $VULKAN_FOUND -eq 1 ]; then echo 'Found'; else echo 'Not found — OpenGL fallback'; fi)"

if [ $failed -eq 0 ]; then
    ok "All dependencies ready — you can now build with ./build-linux.sh"
    exit 0
else
    warn "Some dependencies failed — build may still work with system libs"
    exit 0
fi
