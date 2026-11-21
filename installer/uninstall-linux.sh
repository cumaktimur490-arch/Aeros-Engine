#!/bin/bash
# Aeros Engine — Linux Uninstaller v1.19.0

set -e

PREFIX="/usr/local"
ARCH=$(uname -m)

while [[ $# -gt 0 ]]; do
    case $1 in
        --prefix) PREFIX="$2"; shift 2;;
        --user) PREFIX="$HOME/.local"; shift;;
        --help|-h)
            echo "Aeros Engine Uninstaller"
            echo "Usage: ./uninstall-linux.sh [--prefix PATH] [--user]"
            exit 0
            ;;
        *) shift;;
    esac
done

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

info() { echo -e "${BLUE}[INFO]${NC} $1"; }
ok() { echo -e "${GREEN}[OK]${NC} $1"; }
warn() { echo -e "${YELLOW}[WARN]${NC} $1"; }
err() { echo -e "${RED}[ERROR]${NC} $1"; }

echo -e "${BLUE}=== Aeros Engine Uninstaller ===${NC}"
info "Prefix: $PREFIX"

NEED_SUDO=0
if [ "$PREFIX" = "/usr/local" ] && [ "$EUID" -ne 0 ]; then NEED_SUDO=1; fi

FILES=(
    "$PREFIX/bin/aeros-engine"
    "$PREFIX/bin/aeros-engine-linux-$ARCH"
    "$PREFIX/share/applications/aeros-engine.desktop"
    "$PREFIX/share/icons/hicolor/256x256/apps/aeros-engine.png"
)

DIRS=(
    "$PREFIX/share/aeros-engine"
)

info "Removing files..."
for f in "${FILES[@]}"; do
    if [ -f "$f" ]; then
        if [ $NEED_SUDO -eq 1 ]; then sudo rm -f "$f" && ok "Removed $f" || warn "Failed to remove $f"
        else rm -f "$f" && ok "Removed $f" || warn "Failed to remove $f"
        fi
    else
        info "Not found (already removed): $f"
    fi
done

info "Removing directories..."
for d in "${DIRS[@]}"; do
    if [ -d "$d" ]; then
        if [ $NEED_SUDO -eq 1 ]; then sudo rm -rf "$d" && ok "Removed $d" || warn "Failed to remove $d"
        else rm -rf "$d" && ok "Removed $d" || warn "Failed to remove $d"
        fi
    fi
done

if command -v update-desktop-database >/dev/null 2>&1; then
    if [ $NEED_SUDO -eq 1 ]; then sudo update-desktop-database "$PREFIX/share/applications" 2>/dev/null || true
    else update-desktop-database "$PREFIX/share/applications" 2>/dev/null || true
    fi
fi

ok "Uninstall complete"
