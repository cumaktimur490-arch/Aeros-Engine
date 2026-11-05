# Собирает GLFW из исходников для ARM64 (и других архитектур) на Windows
# Использование: .\build-glfw-arm64.ps1 -Arch arm64
# Требует: git, cmake, MSVC

param(
    [ValidateSet("x64","x86","arm64")][string]$Arch = "arm64",
    [string]$GLFWVersion = "3.4"
)

$ErrorActionPreference = "Stop"
$RootDir = Resolve-Path "$PSScriptRoot\.."
$LibsDir = Join-Path $RootDir "libs\glfw"
$TempDir = Join-Path $env:TEMP "glfw-build-$Arch"
$SourceDir = Join-Path $TempDir "glfw-src"
$BuildDir = Join-Path $TempDir "build"

Write-Host "[Aeros] Building GLFW $GLFWVersion for $Arch from source..." -ForegroundColor Cyan

# Определяем папку для либы
$libFolder = switch ($Arch) {
    "x64" { "lib-vc2022" }
    "x86" { "lib-vc2022-x86" }
    "arm64" { "lib-vc2022-arm64" }
}

$destDir = Join-Path $LibsDir $libFolder

# Проверяем если уже есть
if (Test-Path (Join-Path $destDir "glfw3.lib")) {
    Write-Host "[INFO] GLFW lib already exists at $destDir, skipping build" -ForegroundColor Yellow
    Get-ChildItem $destDir
    exit 0
}

# Клонируем GLFW
if (Test-Path $TempDir) { Remove-Item -Recurse -Force $TempDir -ErrorAction SilentlyContinue }
New-Item -ItemType Directory -Path $TempDir -Force | Out-Null

try {
    Write-Host "[Aeros] Cloning GLFW $GLFWVersion..." -ForegroundColor Gray
    # Попробуем скачать zip исходников с glfw.org (более надёжно чем git в CI)
    $zipUrl = "https://github.com/glfw/glfw/releases/download/$GLFWVersion/glfw-$GLFWVersion.zip"
    $zipPath = Join-Path $TempDir "glfw.zip"
    Write-Host "Downloading $zipUrl"
    try {
        Invoke-WebRequest -Uri $zipUrl -OutFile $zipPath -UseBasicParsing -TimeoutSec 60
        Expand-Archive -Path $zipPath -DestinationPath $TempDir -Force
        $extracted = Get-ChildItem $TempDir -Directory | Where-Object { $_.Name -like "glfw-*" } | Select-Object -First 1
        if ($extracted) {
            Move-Item $extracted.FullName $SourceDir -Force
        }
    } catch {
        Write-Host "[WARN] Zip download failed, trying git clone: $_" -ForegroundColor Yellow
        # Fallback to git
        git clone --depth 1 --branch $GLFWVersion https://github.com/glfw/glfw.git $SourceDir
        if (-not (Test-Path $SourceDir)) {
            git clone --depth 1 https://github.com/glfw/glfw.git $SourceDir
        }
    }
} catch {
    Write-Host "[ERROR] Failed to get GLFW source: $_" -ForegroundColor Red
    exit 1
}

if (-not (Test-Path $SourceDir)) {
    Write-Host "[ERROR] Source dir not found: $SourceDir" -ForegroundColor Red
    Get-ChildItem $TempDir -Recurse | Format-Table Name
    exit 1
}

Write-Host "[Aeros] Source at $SourceDir" -ForegroundColor Gray
Get-ChildItem $SourceDir | Format-Table Name

# CMake configure
if (-not (Test-Path $BuildDir)) { New-Item -ItemType Directory -Path $BuildDir | Out-Null }

$cmakeArch = switch ($Arch) {
    "x64" { "x64" }
    "x86" { "Win32" }
    "arm64" { "ARM64" }
}

Write-Host "[Aeros] Configuring CMake for $Arch ($cmakeArch)..." -ForegroundColor Gray
Push-Location $BuildDir
try {
    # Используем Visual Studio 17 2022 генератор
    $cmakeArgs = @(
        "-S", $SourceDir,
        "-B", ".",
        "-A", $cmakeArch,
        "-DGLFW_BUILD_DOCS=OFF",
        "-DGLFW_BUILD_TESTS=OFF",
        "-DGLFW_BUILD_EXAMPLES=OFF",
        "-DBUILD_SHARED_LIBS=OFF"
    )
    Write-Host "cmake $($cmakeArgs -join ' ')"
    & cmake @cmakeArgs
    if ($LASTEXITCODE -ne 0) { throw "CMake configure failed" }

    Write-Host "[Aeros] Building GLFW..." -ForegroundColor Gray
    & cmake --build . --config Release
    if ($LASTEXITCODE -ne 0) { throw "CMake build failed" }

    # Ищем glfw3.lib
    $builtLib = Get-ChildItem -Recurse -Filter "glfw3.lib" | Select-Object -First 1
    if (-not $builtLib) {
        Write-Host "[ERROR] glfw3.lib not found after build" -ForegroundColor Red
        Get-ChildItem -Recurse | Format-Table Name, Directory
        throw "Lib not found"
    }

    Write-Host "[OK] Found built lib: $($builtLib.FullName)" -ForegroundColor Green

    if (-not (Test-Path $destDir)) { New-Item -ItemType Directory -Path $destDir -Force | Out-Null }
    Copy-Item $builtLib.FullName -Destination (Join-Path $destDir "glfw3.lib") -Force
    Write-Host "[OK] Copied to $destDir" -ForegroundColor Green

    # Попробуем найти dll если собирали shared (но мы собираем static, так что dll не будет)
    $builtDll = Get-ChildItem -Recurse -Filter "glfw3.dll" | Select-Object -First 1
    if ($builtDll) {
        Copy-Item $builtDll.FullName -Destination (Join-Path $destDir "glfw3.dll") -Force
        Write-Host "[OK] Copied DLL" -ForegroundColor Green
    }

    Get-ChildItem $destDir | Format-Table Name, Length

} catch {
    Write-Host "[ERROR] $_" -ForegroundColor Red
    Pop-Location
    exit 1
} finally {
    Pop-Location
}

# Очистка
try { Remove-Item -Recurse -Force $TempDir -ErrorAction SilentlyContinue } catch {}

Write-Host "[Aeros] GLFW $Arch build complete: $destDir" -ForegroundColor Cyan
