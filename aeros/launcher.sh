#!/bin/bash
# Aeros Engine — Smart Launcher v1.20.1 — авто-определение железа и запуск нужной версии
# Для слабых устройств: Atom/Celeron -> Ultra-Lite, i3-3xxx/HD4000 -> Lite, i5+ -> Full

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BINDIR="$SCRIPT_DIR/bin"

echo "[Aeros Launcher] Detecting hardware..."

# CPU threads
HW_THREADS=$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)
echo "[Aeros Launcher] CPU threads: $HW_THREADS"

# RAM
TOTAL_RAM_MB=4096
if [ -f /proc/meminfo ]; then
    MEM_KB=$(grep MemTotal /proc/meminfo | awk '{print $2}')
    TOTAL_RAM_MB=$((MEM_KB / 1024))
elif command -v sysctl >/dev/null 2>&1; then
    MEM_BYTES=$(sysctl -n hw.memsize 2>/dev/null || echo 4294967296)
    TOTAL_RAM_MB=$((MEM_BYTES / 1024 / 1024))
fi
echo "[Aeros Launcher] RAM: $TOTAL_RAM_MB MB"

# Battery
ON_BATTERY=0
if [ -f /sys/class/power_supply/BAT0/status ]; then
    STATUS=$(cat /sys/class/power_supply/BAT0/status 2>/dev/null)
    if [ "$STATUS" = "Discharging" ]; then ON_BATTERY=1; fi
fi
if [ -f /sys/class/power_supply/BAT1/status ]; then
    STATUS=$(cat /sys/class/power_supply/BAT1/status 2>/dev/null)
    if [ "$STATUS" = "Discharging" ]; then ON_BATTERY=1; fi
fi
echo "[Aeros Launcher] Battery: $ON_BATTERY (0=AC, 1=Battery)"

# GPU check via glxinfo if available
GPU_INFO=""
if command -v glxinfo >/dev/null 2>&1; then
    GPU_INFO=$(glxinfo 2>/dev/null | grep "OpenGL renderer" | head -n1)
    echo "[Aeros Launcher] GPU: $GPU_INFO"
fi

# Выбор версии
LAUNCH_MODE="full"
EXE_NAME="aeros-engine-linux-$(uname -m)"

if [ "$HW_THREADS" -le 2 ]; then
    echo "[Aeros Launcher] Very weak CPU $HW_THREADS threads — Ultra-Lite Potato mode"
    LAUNCH_MODE="potato"
    EXE_NAME="aeros-engine-ultra-lite-linux-$(uname -m)"
elif [ "$HW_THREADS" -le 4 ]; then
    if [ "$TOTAL_RAM_MB" -lt 4000 ]; then
        echo "[Aeros Launcher] Weak CPU $HW_THREADS threads + low RAM $TOTAL_RAM_MB MB — Lite mode"
        LAUNCH_MODE="lite"
        EXE_NAME="aeros-engine-lite-linux-$(uname -m)"
    else
        echo "[Aeros Launcher] i3-like $HW_THREADS threads — Lite recommended"
        LAUNCH_MODE="lite"
        EXE_NAME="aeros-engine-lite-linux-$(uname -m)"
    fi
else
    if [ "$TOTAL_RAM_MB" -lt 4000 ]; then
        echo "[Aeros Launcher] Decent CPU but low RAM $TOTAL_RAM_MB MB — Lite"
        LAUNCH_MODE="lite"
        EXE_NAME="aeros-engine-lite-linux-$(uname -m)"
    else
        echo "[Aeros Launcher] Modern PC $HW_THREADS threads $TOTAL_RAM_MB MB — Full mode"
        LAUNCH_MODE="full"
        EXE_NAME="aeros-engine-linux-$(uname -m)"
    fi
fi

# Проверка батареи — если на батарее и Full, переключаем на Lite для экономии
if [ "$ON_BATTERY" -eq 1 ] && [ "$LAUNCH_MODE" = "full" ]; then
    echo "[Aeros Launcher] On battery + Full mode — switching to Lite for battery saving"
    LAUNCH_MODE="lite"
    EXE_NAME="aeros-engine-lite-linux-$(uname -m)"
fi

# Fallback проверка файлов
if [ ! -f "$BINDIR/$EXE_NAME" ]; then
    echo "[Aeros Launcher] $EXE_NAME not found, trying alternatives..."
    if [ -f "$BINDIR/aeros-engine-linux-$(uname -m)" ]; then
        EXE_NAME="aeros-engine-linux-$(uname -m)"
        LAUNCH_MODE="full"
    elif [ -f "$BINDIR/aeros-engine-lite-linux-$(uname -m)" ]; then
        EXE_NAME="aeros-engine-lite-linux-$(uname -m)"
        LAUNCH_MODE="lite"
    elif [ -f "$BINDIR/aeros-engine-ultra-lite-linux-$(uname -m)" ]; then
        EXE_NAME="aeros-engine-ultra-lite-linux-$(uname -m)"
        LAUNCH_MODE="potato"
    elif [ -f "$BINDIR/aeros-engine-lite" ]; then
        EXE_NAME="aeros-engine-lite"
        LAUNCH_MODE="lite"
    elif [ -f "$BINDIR/aeros-engine" ]; then
        EXE_NAME="aeros-engine"
        LAUNCH_MODE="full"
    else
        echo "[ERROR] No executable found in $BINDIR"
        echo "Please build first: ./build-linux.sh all or ./build-lite.sh all or ./build-ultra-lite.sh all"
        exit 1
    fi
fi

echo "[Aeros Launcher] Launching $LAUNCH_MODE mode: $EXE_NAME $@"
echo "[Aeros Launcher] Preset: $LAUNCH_MODE, Battery saver: $ON_BATTERY"

if [ "$LAUNCH_MODE" = "potato" ]; then
    exec "$BINDIR/$EXE_NAME" --preset potato "$@"
elif [ "$LAUNCH_MODE" = "lite" ]; then
    exec "$BINDIR/$EXE_NAME" --preset low "$@"
else
    exec "$BINDIR/$EXE_NAME" "$@"
fi
