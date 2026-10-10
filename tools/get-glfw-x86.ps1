# Скачивает GLFW 32-bit бинарники для сборки x86 версии
# Использование: .\get-glfw-x86.ps1

$ErrorActionPreference = "Stop"
$RootDir = Resolve-Path "$PSScriptRoot\.."
$LibsDir = Join-Path $RootDir "libs\glfw"
$TempZip = Join-Path $env:TEMP "glfw.zip"

$GLFW_URL = "https://github.com/glfw/glfw/releases/download/3.3.8/glfw-3.3.8.bin.WIN32.zip"

Write-Host "[Aeros] Downloading GLFW Win32 binaries..." -ForegroundColor Cyan
Write-Host "URL: $GLFW_URL"

try {
    Invoke-WebRequest -Uri $GLFW_URL -OutFile $TempZip -UseBasicParsing
} catch {
    Write-Host "[ERROR] Download failed: $_" -ForegroundColor Red
    Write-Host "Download manually from https://www.glfw.org/download.html and extract lib-vc2022 to libs/glfw/lib-vc2022-x86" -ForegroundColor Yellow
    exit 1
}

$extractDir = Join-Path $env:TEMP "glfw_extract"
if (Test-Path $extractDir) { Remove-Item -Recurse -Force $extractDir }
New-Item -ItemType Directory -Path $extractDir | Out-Null

Expand-Archive -Path $TempZip -DestinationPath $extractDir -Force

$srcLib = Get-ChildItem $extractDir -Recurse -Filter "glfw3.lib" | Where-Object { $_.FullName -like "*lib-vc2022*" } | Select-Object -First 1
$srcDll = Get-ChildItem $extractDir -Recurse -Filter "glfw3.dll" | Where-Object { $_.FullName -like "*lib-vc2022*" } | Select-Object -First 1

if (-not $srcLib) {
    Write-Host "[ERROR] glfw3.lib not found in archive" -ForegroundColor Red
    exit 1
}

$destDir = Join-Path $LibsDir "lib-vc2022-x86"
if (-not (Test-Path $destDir)) { New-Item -ItemType Directory -Path $destDir | Out-Null }

Copy-Item $srcLib.FullName -Destination (Join-Path $destDir "glfw3.lib") -Force
Write-Host "[OK] Copied $($srcLib.FullName) -> $destDir" -ForegroundColor Green

if ($srcDll) {
    Copy-Item $srcDll.FullName -Destination (Join-Path $destDir "glfw3.dll") -Force
    $binDll = Join-Path $RootDir "aeros\bin\glfw3-x86.dll"
    Copy-Item $srcDll.FullName -Destination $binDll -Force
    Write-Host "[OK] Copied DLL $($srcDll.FullName) (+ $binDll)" -ForegroundColor Green
}

# Копируем include если нет
$srcInclude = Get-ChildItem $extractDir -Recurse -Directory -Filter "include" | Select-Object -First 1
if ($srcInclude) {
    Write-Host "[INFO] Include already exists in libs/glfw/include, skipping" -ForegroundColor Gray
}

Remove-Item $TempZip -Force -ErrorAction SilentlyContinue
Remove-Item -Recurse -Force $extractDir -ErrorAction SilentlyContinue

Write-Host "[Aeros] GLFW x86 setup complete: $destDir" -ForegroundColor Cyan
Get-ChildItem $destDir
