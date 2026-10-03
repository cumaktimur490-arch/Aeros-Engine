param(
    [string]$Version = "",
    [switch]$SkipBuild,
    [switch]$SkipPortable,
    [switch]$SkipInstaller
)

# Aeros Engine — Полная сборка релиза локально
# 1. Собирает x64 и x86 бинари
# 2. Собирает портативные версии
# 3. Собирает установщики (если Inno Setup установлен)
# Использование:
#   .\tools\build-all.ps1
#   .\tools\build-all.ps1 -Version 1.1.0

$ErrorActionPreference = "Stop"
$RootDir = Resolve-Path "$PSScriptRoot\.."
$AerosDir = Join-Path $RootDir "aeros"
$InstallerDir = Join-Path $RootDir "installer"
$ReleaseDir = Join-Path $RootDir "release"

if (-not $Version) {
    $VersionFile = Join-Path $RootDir "VERSION"
    if (Test-Path $VersionFile) { $Version = (Get-Content $VersionFile -Raw).Trim() }
    else { $Version = "1.0.0" }
}

Write-Host "==========================================" -ForegroundColor Cyan
Write-Host " Aeros Engine — Full Release Build v$Version" -ForegroundColor Cyan
Write-Host "==========================================" -ForegroundColor Cyan

if (-not (Test-Path $ReleaseDir)) { New-Item -ItemType Directory -Path $ReleaseDir | Out-Null }

# 1. Build binaries
if (-not $SkipBuild) {
    Write-Host "`n[1/3] Building binaries..." -ForegroundColor Yellow
    
    Push-Location $AerosDir
    try {
        Write-Host "[Aeros] Building x64..." -ForegroundColor Green
        & .\build.bat x64
        if ($LASTEXITCODE -ne 0) { throw "x64 build failed" }

        Write-Host "[Aeros] Building x86..." -ForegroundColor Green
        # x86 may fail if no 32-bit GLFW lib — warn but continue
        & .\build.bat x86
        if ($LASTEXITCODE -ne 0) {
            Write-Host "[WARN] x86 build failed — portable/installer for x86 will use x64 fallback or existing binary" -ForegroundColor Yellow
        }
    } finally {
        Pop-Location
    }
} else {
    Write-Host "[SKIP] Binary build" -ForegroundColor Gray
}

# 2. Portable
if (-not $SkipPortable) {
    Write-Host "`n[2/3] Building portable versions..." -ForegroundColor Yellow
    & "$InstallerDir\build-portable.ps1" -Version $Version -Arch both
} else {
    Write-Host "[SKIP] Portable build" -ForegroundColor Gray
}

# 3. Installers
if (-not $SkipInstaller) {
    Write-Host "`n[3/3] Building installers..." -ForegroundColor Yellow
    try {
        & "$InstallerDir\build-installers.ps1" -Version $Version -Arch both -Type all
    } catch {
        Write-Host "[WARN] Installer build failed or Inno Setup not installed: $_" -ForegroundColor Yellow
        Write-Host "Install Inno Setup 6 to build installers: https://jrsoftware.org/isinfo.php" -ForegroundColor Yellow
    }
} else {
    Write-Host "[SKIP] Installer build" -ForegroundColor Gray
}

Write-Host "`n==========================================" -ForegroundColor Cyan
Write-Host " Release build finished! Files in $ReleaseDir" -ForegroundColor Green
Write-Host "==========================================" -ForegroundColor Cyan
Get-ChildItem $ReleaseDir | Format-Table Name, Length, LastWriteTime -AutoSize

Write-Host "`nNext steps:" -ForegroundColor Cyan
Write-Host " - Test installers: release\Aeros-Engine-Setup-*.exe"
Write-Host " - Test portable: release\Aeros-Engine-Portable-*.zip"
Write-Host " - Create GitHub release: gh release create v$Version release/* --title v$Version --notes 'Release v$Version'"
