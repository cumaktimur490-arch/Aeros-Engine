#!/bin/bash
# Aeros Engine — Проверка совместимости железа v1.20.1

echo "========================================"
echo "Aeros Engine — Hardware Check v1.20.1"
echo "========================================"
echo ""

echo "[CPU]"
lscpu 2>/dev/null | grep -E "Model name|CPU\(s\)|Thread|Core|MHz" || cat /proc/cpuinfo | grep "model name" | head -n1
echo "Threads: $(nproc)"
echo ""

echo "[RAM]"
free -h 2>/dev/null || cat /proc/meminfo | grep MemTotal
echo ""

echo "[GPU]"
lspci 2>/dev/null | grep -i vga || echo "lspci not found"
if command -v glxinfo >/dev/null 2>&1; then
    glxinfo 2>/dev/null | grep -E "OpenGL vendor|OpenGL renderer|OpenGL version" | head -n5
else
    echo "glxinfo not found — install mesa-utils"
fi
echo ""

echo "[OS]"
uname -a
cat /etc/os-release 2>/dev/null | head -n5
echo ""

echo "[Battery]"
if [ -f /sys/class/power_supply/BAT0/status ]; then
    echo "BAT0 status: $(cat /sys/class/power_supply/BAT0/status 2>/dev/null)"
    echo "BAT0 capacity: $(cat /sys/class/power_supply/BAT0/capacity 2>/dev/null)%"
else
    echo "No battery (desktop)"
fi
echo ""

echo "[Disk]"
df -h . | tail -n1
echo ""

echo "[Recommendation]"
HW_THREADS=$(nproc 2>/dev/null || echo 4)
TOTAL_RAM_MB=$(free -m 2>/dev/null | awk '/Mem:/ {print $2}' || echo 4096)

echo "CPU Threads: $HW_THREADS, RAM: $TOTAL_RAM_MB MB"

if [ "$HW_THREADS" -le 2 ]; then
    echo "-> ULTRA-LITE Potato recommended (Atom/Celeron 2GB RAM)"
    echo "   Build: ./build-ultra-lite.sh all"
    echo "   Run: ./bin/aeros-engine-ultra-lite --preset potato"
elif [ "$HW_THREADS" -le 4 ] || [ "$TOTAL_RAM_MB" -lt 4000 ]; then
    echo "-> LITE recommended (i3-3xxx, HD 4000, GT 620M, 4GB RAM)"
    echo "   Build: ./build-lite.sh all"
    echo "   Run: ./bin/aeros-engine-lite --preset low"
else
    echo "-> FULL recommended (i5+, GTX 1060+, 8GB+)"
    echo "   Build: ./build-linux.sh all"
    echo "   Run: ./bin/aeros-engine"
fi

echo ""
echo "For auto-detection, use: ./launcher.sh"
echo "========================================"
