param(
    [string]$Version = "",
    [ValidateSet("x64","x86","arm64","both","all")][string]$Arch = "both",
    [string]$OutputDir = "..\release",
    [switch]$Lite
)

# Aeros Engine — Сборка портативных версий
# Использование:
#   .\build-portable.ps1
#   .\build-portable.ps1 -Version 1.0.0 -Arch x64

$ErrorActionPreference = "Stop"
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RootDir = Resolve-Path "$ScriptDir\.."
$BinDir = Join-Path $RootDir "aeros\bin"

if (-not $Version) {
    $VersionFile = Join-Path $RootDir "VERSION"
    if (Test-Path $VersionFile) {
        $Version = (Get-Content $VersionFile -Raw).Trim()
    } else {
        $Version = "1.0.0"
    }
}

Write-Host "[Aeros] Building portable v$Version Arch=$Arch" -ForegroundColor Cyan

if (-not (Test-Path $OutputDir)) { New-Item -ItemType Directory -Path $OutputDir | Out-Null }
$OutputDir = Resolve-Path $OutputDir

function Build-Portable($archSuffix) {
    if ($Lite) {
        $exeName = "main-lite-$archSuffix.exe"
        $exePath = Join-Path $BinDir $exeName
        $fallbackExe = Join-Path $BinDir "main-lite.exe"
        $fallback2 = Join-Path $BinDir "aeros-engine-lite-$archSuffix.exe"
        if (-not (Test-Path $exePath)) {
            if (Test-Path $fallbackExe) { $exePath = $fallbackExe }
            elseif (Test-Path $fallback2) { $exePath = $fallback2 }
            else {
                Write-Host "[WARN] Lite $exeName not found, trying main-$archSuffix.exe" -ForegroundColor Yellow
                $exeName = "main-$archSuffix.exe"
                $exePath = Join-Path $BinDir $exeName
                $fallbackExe = Join-Path $BinDir "main.exe"
                if (-not (Test-Path $exePath) -and (Test-Path $fallbackExe)) { $exePath = $fallbackExe }
                elseif (-not (Test-Path $exePath)) { Write-Host "[ERROR] No exe for Lite $archSuffix" -ForegroundColor Red; return }
            }
        }
        $portablePrefix = "Aeros-Engine-Portable-Lite-$archSuffix"
        $exeDestName = "AerosEngine-Lite.exe"
    } else {
        $exeName = "main-$archSuffix.exe"
        $exePath = Join-Path $BinDir $exeName
        $fallbackExe = Join-Path $BinDir "main.exe"
        if (-not (Test-Path $exePath)) {
            if (Test-Path $fallbackExe) {
                Write-Host "[WARN] $exeName not found, using main.exe as fallback for $archSuffix" -ForegroundColor Yellow
                $exePath = $fallbackExe
            } else {
                Write-Host "[ERROR] No executable found for $archSuffix : $exePath" -ForegroundColor Red
                return
            }
        }
        $portablePrefix = "Aeros-Engine-Portable-$archSuffix"
        $exeDestName = "AerosEngine.exe"
    }

    $portableDir = Join-Path $env:TEMP "$portablePrefix"
    if (Test-Path $portableDir) { Remove-Item -Recurse -Force $portableDir }
    New-Item -ItemType Directory -Path $portableDir | Out-Null

    # Копируем бинарь
    Copy-Item $exePath -Destination (Join-Path $portableDir $exeDestName) -Force
    # Также копируем как main.exe для совместимости
    Copy-Item $exePath -Destination (Join-Path $portableDir "main.exe") -Force
    if ($Lite) {
        Copy-Item $exePath -Destination (Join-Path $portableDir "aeros-engine-lite.exe") -Force -ErrorAction SilentlyContinue
    }

    # DLLs: glfw своей архитектуры (x64 лежит как glfw3.dll, остальные — с суффиксом)
    $glfwName = if ($archSuffix -eq "x64") { "glfw3.dll" } else { "glfw3-$archSuffix.dll" }
    $glfwSrc = Join-Path $BinDir $glfwName
    if (Test-Path $glfwSrc) {
        Copy-Item $glfwSrc -Destination (Join-Path $portableDir "glfw3.dll") -Force
    } else {
        Write-Host "[INFO] No glfw dll for $archSuffix ($glfwName) - static link or missing" -ForegroundColor Gray
    }

    # Модели
    $modelsDir = Join-Path $portableDir "models"
    New-Item -ItemType Directory -Path $modelsDir | Out-Null
    Get-ChildItem $BinDir -Filter "*.stl" | ForEach-Object {
        Copy-Item $_.FullName -Destination $modelsDir -Force
    }
    $extraStl = Join-Path $RootDir "aeros\2.stl"
    if (Test-Path $extraStl) { Copy-Item $extraStl -Destination $modelsDir -Force }

    # run.bat
    $runBat = Join-Path $BinDir "run.bat"
    if (Test-Path $runBat) { Copy-Item $runBat -Destination $portableDir -Force }
    else {
        @"
@echo off
cd /d "%~dp0"
AerosEngine.exe
pause
"@ | Set-Content -Path (Join-Path $portableDir "run.bat") -Encoding ASCII
    }

    # README и LICENSE и иконка
    Copy-Item (Join-Path $RootDir "LICENSE") -Destination (Join-Path $portableDir "LICENSE.txt") -Force -ErrorAction SilentlyContinue
    Copy-Item (Join-Path $RootDir "README.md") -Destination (Join-Path $portableDir "README.txt") -Force -ErrorAction SilentlyContinue
    Copy-Item (Join-Path $RootDir "VERSION") -Destination (Join-Path $portableDir "VERSION.txt") -Force -ErrorAction SilentlyContinue
    Copy-Item (Join-Path $RootDir "icon.png") -Destination (Join-Path $portableDir "icon.png") -Force -ErrorAction SilentlyContinue
    Copy-Item (Join-Path $RootDir "aeros/src/icon.png") -Destination (Join-Path $portableDir "Aeros-Engine-Icon.png") -Force -ErrorAction SilentlyContinue
    Copy-Item (Join-Path $BinDir "icon.ico") -Destination (Join-Path $portableDir "icon.ico") -Force -ErrorAction SilentlyContinue

    # Portable info
    if ($Lite) {
        @"
Aeros Engine Portable Lite $archSuffix v$Version — для слабых устройств i3-3xxx / HD 4000 / GT 620M / 4GB RAM / No CUDA
========================================

Запуск: AerosEngine-Lite.exe или aeros-engine-lite.exe или main.exe или run.bat

Lite версия — оптимизирована для слабых устройств:
- i3-3xxx Ivy Bridge 2C/4T SSE4.2, нет AVX2
- Intel HD 4000 16 EUs, GT 620M/720M, AMD APU, 4GB RAM ноутбуки
- SSE2 only, O1 small binary ~5-10 MB, <512 MB RAM, OpenGL 3.3, MSAA 0, 30 FPS, VSync ON, power saving
- Частицы 1500 (было 15000), линии тока 8x80 (было 24x300), воксели 24 (было 48) — 8x меньше памяти
- LBM OFF по умолчанию 32 рес 1 шаг/кадр, без турбулентности
- FSR OFF, FrameGen OFF, volumetric OFF, vortex OFF, LIC OFF, acoustic OFF, schlieren/shock ON
- Окно 1024x600 для 1366x768 ноутов

Архитектура: $archSuffix Lite
Версия: $Version
Дата сборки: $(Get-Date -Format "yyyy-MM-dd HH:mm:ss")

Сайт: https://github.com/cumaktimur490-arch/Aeros-Engine
"@ | Set-Content -Path (Join-Path $portableDir "README-Portable.txt") -Encoding UTF8
    } else {
        @"
Aeros Engine Portable $archSuffix v$Version
========================================

Запуск: AerosEngine.exe или main.exe или run.bat

Портативная версия — не требует установки.
Все файлы находятся в этой папке.
Для удаления — просто удалите папку.

Архитектура: $archSuffix
Версия: $Version
Дата сборки: $(Get-Date -Format "yyyy-MM-dd HH:mm:ss")

Сайт: https://github.com/cumaktimur490-arch/Aeros-Engine
"@ | Set-Content -Path (Join-Path $portableDir "README-Portable.txt") -Encoding UTF8
    }

    # Архивируем
    if ($Lite) { $zipName = "Aeros-Engine-Portable-Lite-$archSuffix-v$Version.zip" }
    else { $zipName = "Aeros-Engine-Portable-$archSuffix-v$Version.zip" }
    $zipPath = Join-Path $OutputDir $zipName
    if (Test-Path $zipPath) { Remove-Item $zipPath -Force }
    
    Write-Host "[Aeros] Creating $zipName ..." -ForegroundColor Green
    Compress-Archive -Path "$portableDir\*" -DestinationPath $zipPath -CompressionLevel Optimal -Force

    # Также создаём папку-версию без архива (опционально)
    $folderName = "Aeros-Engine-Portable-$archSuffix-v$Version"
    $folderDest = Join-Path $OutputDir $folderName
    if (Test-Path $folderDest) { Remove-Item -Recurse -Force $folderDest }
    Copy-Item $portableDir -Destination $folderDest -Recurse -Force

    Write-Host "[OK] Portable $archSuffix -> $zipPath" -ForegroundColor Green
    Write-Host "     Folder  -> $folderDest" -ForegroundColor Gray

    # Очистка temp
    Remove-Item -Recurse -Force $portableDir
}

if ($Arch -eq "both" -or $Arch -eq "all" -or $Arch -eq "x64") { Build-Portable "x64" }
if ($Arch -eq "both" -or $Arch -eq "all" -or $Arch -eq "x86") { Build-Portable "x86" }
if ($Arch -eq "all" -or $Arch -eq "arm64") { Build-Portable "arm64" }

Write-Host "[Aeros] Portable builds done. Output: $OutputDir" -ForegroundColor Cyan
Get-ChildItem $OutputDir -Filter "*Portable*"
