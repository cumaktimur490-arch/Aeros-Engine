#!/bin/bash
# Aeros Engine — Linux Installer v1.19.0 Vulkan + OpenGL + авто-загрузка зависимостей
# Использование: ./install-linux.sh [--prefix /usr/local] [--user] [--deps] [--vulkan|--opengl|--all]

set -e

VERSION=$(cat ../VERSION 2>/dev/null || cat VERSION 2>/dev/null || echo "1.19.0")
ARCH=$(uname -m)
PREFIX="/usr/local"
USER_INSTALL=0
INSTALL_DEPS=0
RENDERER="all"
FORCE=0

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

while [[ $# -gt 0 ]]; do
    case $1 in
        --prefix) PREFIX="$2"; shift 2;;
        --user) USER_INSTALL=1; PREFIX="$HOME/.local"; shift;;
        --deps) INSTALL_DEPS=1; shift;;
        --vulkan) RENDERER="vulkan"; shift;;
        --opengl) RENDERER="opengl"; shift;;
        --all) RENDERER="all"; shift;;
        --force) FORCE=1; shift;;
        --version) echo "$VERSION"; exit 0;;
        --help|-h) 
            echo "Aeros Engine Linux Installer v$VERSION"
            echo "Usage: ./install-linux.sh [options]"
            echo "Options:"
            echo "  --prefix PATH  — install prefix (default: /usr/local, user: ~/.local)"
            echo "  --user         — user install to ~/.local"
            echo "  --deps         — install system dependencies (apt/dnf/pacman)"
            echo "  --vulkan       — Vulkan only"
            echo "  --opengl       — OpenGL only"
            echo "  --all          — both Vulkan and OpenGL (default, auto-select at runtime)"
            echo "  --force        — force reinstall"
            echo "  --help         — this help"
            echo ""
            echo "Examples:"
            echo "  ./install-linux.sh --deps --all        # full install with deps"
            echo "  ./install-linux.sh --user --deps       # user install"
            echo "  ./install-linux.sh --prefix /opt/aeros # custom prefix"
            exit 0
            ;;
        *) shift;;
    esac
done

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
AEROS_DIR="$ROOT_DIR/aeros"
BIN_DIR="$AEROS_DIR/bin"
LIB_DIR="$ROOT_DIR/libs"

echo -e "${BLUE}=== Aeros Engine Linux Installer v$VERSION $ARCH ===${NC}"
info "Prefix: $PREFIX"
info "Renderer: $RENDERER"
info "User install: $USER_INSTALL"
info "Install deps: $INSTALL_DEPS"

# Check if running as root for system install
if [ $USER_INSTALL -eq 0 ] && [ "$PREFIX" = "/usr/local" ] && [ "$EUID" -ne 0 ]; then
    warn "System install to $PREFIX requires sudo — will use sudo for final copy"
    NEED_SUDO=1
else
    NEED_SUDO=0
fi

# --- Step 1: Install system dependencies if requested ---
if [ $INSTALL_DEPS -eq 1 ]; then
    info "Installing system dependencies..."
    if command -v apt-get >/dev/null 2>&1; then
        info "Using apt (Debian/Ubuntu)"
        sudo apt-get update
        sudo apt-get install -y build-essential cmake pkg-config \
            libglfw3-dev libgl1-mesa-dev libglu1-mesa-dev \
            libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev \
            libvulkan-dev vulkan-tools glslc shaderc \
            zenity kdialog libopenmp-dev git curl wget
    elif command -v dnf >/dev/null 2>&1; then
        info "Using dnf (Fedora)"
        sudo dnf install -y gcc-c++ cmake pkgconfig glfw-devel mesa-libGL-devel \
            libX11-devel libXrandr-devel libXinerama-devel libXcursor-devel libXi-devel \
            vulkan-devel vulkan-tools glslc shaderc zenity kdialog libomp-devel git curl wget
    elif command -v pacman >/dev/null 2>&1; then
        info "Using pacman (Arch)"
        sudo pacman -S --noconfirm base-devel cmake pkgconf glfw mesa libx11 libxrandr libxinerama libxcursor libxi vulkan-headers vulkan-tools shaderc zenity kdialog openmp git curl wget
    elif command -v zypper >/dev/null 2>&1; then
        info "Using zypper (openSUSE)"
        sudo zypper install -y gcc-c++ cmake pkg-config glfw-devel Mesa-libGL-devel libX11-devel libXrandr-devel libXinerama-devel libXcursor-devel libXi-devel vulkan-devel vulkan-tools glslc zenity kdialog libomp-devel git curl wget
    else
        warn "Unknown package manager — please install manually:"
        echo "  build-essential, cmake, libglfw3-dev, libGL, X11, vulkan, zenity, openmp"
    fi
    ok "System dependencies installed"
fi

# --- Step 2: Download bundled dependencies ---
info "Downloading bundled dependencies (GLM, GLAD, ImGui, models)..."

# Use our download script if available
if [ -f "$SCRIPT_DIR/download_deps_linux.sh" ]; then
    bash "$SCRIPT_DIR/download_deps_linux.sh" --install-dir "$BIN_DIR" --arch "$ARCH" $([ $FORCE -eq 1 ] && echo "--force")
else
    warn "download_deps_linux.sh not found, doing minimal download..."
    # Minimal GLM download
    if [ ! -f "$LIB_DIR/glm/glm/glm.hpp" ]; then
        info "Downloading GLM..."
        rm -rf /tmp/glm_aeros
        git clone --depth 1 https://github.com/g-truc/glm.git /tmp/glm_aeros 2>&1 | tail -n 1
        mkdir -p "$LIB_DIR/glm"
        cp -r /tmp/glm_aeros/glm "$LIB_DIR/glm/" 2>/dev/null || true
        rm -rf /tmp/glm_aeros
    fi
fi

# --- Step 3: Check Vulkan ---
info "Checking Vulkan..."
VULKAN_FOUND=0
if pkg-config --exists vulkan 2>/dev/null || [ -f /usr/include/vulkan/vulkan.h ]; then
    ok "Vulkan found — Vulkan renderer will be available"
    VULKAN_FOUND=1
else
    warn "Vulkan not found — will build OpenGL only (fully working)"
    info "To enable Vulkan: sudo apt install libvulkan-dev vulkan-tools"
fi

if command -v glslc >/dev/null 2>&1; then ok "glslc found"; else warn "glslc not found — using embedded SPIR-V"; fi

# --- Step 4: Build ---
info "Building Aeros Engine v$VERSION for Linux..."

cd "$AEROS_DIR"

if [ ! -f build-linux.sh ]; then
    err "build-linux.sh not found in $AEROS_DIR"
    exit 1
fi

chmod +x build-linux.sh

# Build based on renderer choice
case $RENDERER in
    vulkan)
        info "Building Vulkan version..."
        ./build-linux.sh vulkan
        ;;
    opengl)
        info "Building OpenGL version..."
        ./build-linux.sh opengl
        ;;
    all|*)
        info "Building auto (Vulkan if available, else OpenGL)..."
        ./build-linux.sh all
        ;;
esac

# Find built binary
BINARY=""
for cand in bin/aeros-engine bin/aeros-engine-linux-* bin/aeros-engine-vulkan-* bin/aeros-engine-opengl-*; do
    if [ -f "$cand" ] && [ -x "$cand" ]; then
        BINARY="$cand"
        break
    fi
done

if [ -z "$BINARY" ]; then
    err "Build failed — no binary found in bin/"
    ls -lh bin/ 2>/dev/null || true
    exit 1
fi

ok "Built: $BINARY"
ls -lh "$BINARY"

# --- Step 5: Install ---
info "Installing to $PREFIX..."

INSTALL_BIN="$PREFIX/bin"
INSTALL_SHARE="$PREFIX/share/aeros-engine"
INSTALL_DESKTOP="$PREFIX/share/applications"
INSTALL_ICON="$PREFIX/share/icons/hicolor/256x256/apps"

mkdir -p "$INSTALL_BIN" "$INSTALL_SHARE/models" "$INSTALL_DESKTOP" "$INSTALL_ICON" 2>/dev/null || true

# Copy binary
if [ $NEED_SUDO -eq 1 ]; then
    sudo cp "$BINARY" "$INSTALL_BIN/aeros-engine"
    sudo chmod +x "$INSTALL_BIN/aeros-engine"
    # Also copy as aeros-engine-linux-ARCH for compatibility
    sudo cp "$BINARY" "$INSTALL_BIN/aeros-engine-linux-$ARCH" 2>/dev/null || true
    sudo chmod +x "$INSTALL_BIN/aeros-engine-linux-$ARCH" 2>/dev/null || true
else
    cp "$BINARY" "$INSTALL_BIN/aeros-engine"
    chmod +x "$INSTALL_BIN/aeros-engine"
    cp "$BINARY" "$INSTALL_BIN/aeros-engine-linux-$ARCH" 2>/dev/null || true
    chmod +x "$INSTALL_BIN/aeros-engine-linux-$ARCH" 2>/dev/null || true
fi

# Copy models
if [ -d "$BIN_DIR/models" ]; then
    if [ $NEED_SUDO -eq 1 ]; then
        sudo cp -r "$BIN_DIR/models"/* "$INSTALL_SHARE/models/" 2>/dev/null || true
    else
        cp -r "$BIN_DIR/models"/* "$INSTALL_SHARE/models/" 2>/dev/null || true
    fi
fi
if [ -d "$ROOT_DIR/models" ]; then
    if [ $NEED_SUDO -eq 1 ]; then
        sudo cp -r "$ROOT_DIR/models"/* "$INSTALL_SHARE/models/" 2>/dev/null || true
    else
        cp -r "$ROOT_DIR/models"/* "$INSTALL_SHARE/models/" 2>/dev/null || true
    fi
fi

# Copy config
if [ -f "$BIN_DIR/aeros_settings.ini" ]; then
    if [ $NEED_SUDO -eq 1 ]; then
        sudo cp "$BIN_DIR/aeros_settings.ini" "$INSTALL_SHARE/" 2>/dev/null || true
    else
        cp "$BIN_DIR/aeros_settings.ini" "$INSTALL_SHARE/" 2>/dev/null || true
    fi
fi

# Copy icon
if [ -f "$AEROS_DIR/icon.png" ]; then
    if [ $NEED_SUDO -eq 1 ]; then
        sudo cp "$AEROS_DIR/icon.png" "$INSTALL_ICON/aeros-engine.png" 2>/dev/null || true
        sudo cp "$AEROS_DIR/icon.png" "$INSTALL_SHARE/icon.png" 2>/dev/null || true
    else
        cp "$AEROS_DIR/icon.png" "$INSTALL_ICON/aeros-engine.png" 2>/dev/null || true
        cp "$AEROS_DIR/icon.png" "$INSTALL_SHARE/icon.png" 2>/dev/null || true
    fi
elif [ -f "$ROOT_DIR/aeros/icon.png" ]; then
    if [ $NEED_SUDO -eq 1 ]; then
        sudo cp "$ROOT_DIR/aeros/icon.png" "$INSTALL_ICON/aeros-engine.png" 2>/dev/null || true
    else
        cp "$ROOT_DIR/aeros/icon.png" "$INSTALL_ICON/aeros-engine.png" 2>/dev/null || true
    fi
fi

# Create desktop file
DESKTOP_FILE="$INSTALL_DESKTOP/aeros-engine.desktop"
DESKTOP_CONTENT="[Desktop Entry]
Name=Aeros Engine
Comment=Airflow Visualization with Vulkan and OpenGL — v$VERSION
Exec=$INSTALL_BIN/aeros-engine
Icon=aeros-engine
Terminal=false
Type=Application
Categories=Science;Engineering;Physics;
Version=$VERSION
Keywords=CFD;aerodynamics;wind tunnel;Vulkan;OpenGL;
StartupNotify=true
"

if [ $NEED_SUDO -eq 1 ]; then
    echo "$DESKTOP_CONTENT" | sudo tee "$DESKTOP_FILE" >/dev/null
    sudo chmod +x "$DESKTOP_FILE" 2>/dev/null || true
else
    echo "$DESKTOP_CONTENT" > "$DESKTOP_FILE"
    chmod +x "$DESKTOP_FILE" 2>/dev/null || true
fi

# Create install info
INFO_FILE="$INSTALL_SHARE/install-info.txt"
INFO_CONTENT="Aeros Engine v$VERSION Linux $ARCH
Installed: $(date)
Prefix: $PREFIX
Binary: $INSTALL_BIN/aeros-engine
Renderer: $RENDERER (Vulkan: $VULKAN_FOUND, OpenGL: always)
Features: Schlieren, Volumetric Smoke, Shock Waves, Vortex Tubes, Flight 6DOF, LIC, Aeroacoustics, FSR, FG
Linux: $ARCH
Vulkan: $(if [ $VULKAN_FOUND -eq 1 ]; then echo 'Available'; else echo 'Not available — OpenGL fallback'; fi)
OpenGL: Available
"

if [ $NEED_SUDO -eq 1 ]; then
    echo "$INFO_CONTENT" | sudo tee "$INFO_FILE" >/dev/null
else
    echo "$INFO_CONTENT" > "$INFO_FILE"
fi

ok "Installed to $PREFIX"
echo ""
info "=== Installation Summary v$VERSION ==="
info "Binary: $INSTALL_BIN/aeros-engine"
info "Models: $INSTALL_SHARE/models/"
info "Desktop: $DESKTOP_FILE"
info "Icon: $INSTALL_ICON/aeros-engine.png"
info "Renderer: $RENDERER — Vulkan: $VULKAN_FOUND, OpenGL: yes"
info "Version: $VERSION Arch: $ARCH"
echo ""
ok "You can now run:"
echo "  aeros-engine"
echo "  $INSTALL_BIN/aeros-engine"
echo "  $INSTALL_BIN/aeros-engine --vulkan   # force Vulkan"
echo "  $INSTALL_BIN/aeros-engine --opengl   # force OpenGL"
echo ""
info "For user install, ensure ~/.local/bin is in PATH:"
echo "  export PATH=\$HOME/.local/bin:\$PATH"
echo ""
info "Uninstall: $SCRIPT_DIR/uninstall-linux.sh --prefix $PREFIX"
echo ""

# Update desktop database
if command -v update-desktop-database >/dev/null 2>&1; then
    if [ $NEED_SUDO -eq 1 ]; then
        sudo update-desktop-database "$INSTALL_DESKTOP" 2>/dev/null || true
    else
        update-desktop-database "$INSTALL_DESKTOP" 2>/dev/null || true
    fi
fi

ok "Installation complete! v$VERSION"
