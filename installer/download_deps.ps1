# Aeros Engine — Dependency Downloader v1.19.0
# Скачивает все необходимые файлы при установке: GLFW, GLM, GLAD, ImGui, Vulkan SDK check, models
# Использование: powershell -ExecutionPolicy Bypass -File download_deps.ps1 [-InstallDir "C:\AerosEngine"] [-Arch x64]

param(
    [string]$InstallDir = "",
    [string]$Arch = "x64",
    [switch]$Force,
    [switch]$Quiet
)

$ErrorActionPreference = "Continue"

function Write-Info($msg) { if (-not $Quiet) { Write-Host "[INFO] $msg" -ForegroundColor Cyan } }
function Write-OK($msg) { if (-not $Quiet) { Write-Host "[OK] $msg" -ForegroundColor Green } }
function Write-Warn($msg) { if (-not $Quiet) { Write-Host "[WARN] $msg" -ForegroundColor Yellow } }
function Write-Err($msg) { Write-Host "[ERROR] $msg" -ForegroundColor Red }

$VERSION = "1.19.0"
if (Test-Path "..\VERSION") { $VERSION = (Get-Content "..\VERSION" -Raw).Trim() }
elseif (Test-Path "VERSION") { $VERSION = (Get-Content "VERSION" -Raw).Trim() }

if ($InstallDir -eq "") {
    $InstallDir = Join-Path $PSScriptRoot "..\aeros\bin"
}
$LibDir = Join-Path $PSScriptRoot "..\libs"
$AerosDir = Join-Path $PSScriptRoot "..\aeros"

Write-Info "Aeros Engine Dependency Downloader v$VERSION"
Write-Info "InstallDir: $InstallDir"
Write-Info "LibDir: $LibDir"
Write-Info "Arch: $Arch"

# Создаем директории
@($InstallDir, "$LibDir\glfw", "$LibDir\glm", "$LibDir\glad\include\glad", "$LibDir\glad\src", "$LibDir\glad\include\KHR", "$LibDir\glfw\lib-vc2022", "$LibDir\glfw\include\GLFW", "$AerosDir\src\imgui", "$InstallDir\models") | ForEach-Object {
    if (!(Test-Path $_)) { New-Item -ItemType Directory -Force -Path $_ | Out-Null }
}

$downloaded = 0
$failed = 0

# --- GLM ---
if (!(Test-Path "$LibDir\glm\glm\glm.hpp") -or $Force) {
    Write-Info "Downloading GLM..."
    try {
        $tmp = Join-Path $env:TEMP "glm_aeros"
        if (Test-Path $tmp) { Remove-Item -Recurse -Force $tmp }
        if (Get-Command git -ErrorAction SilentlyContinue) {
            git clone --depth 1 https://github.com/g-truc/glm.git $tmp 2>&1 | Out-Null
            if (Test-Path "$tmp\glm") {
                Copy-Item -Recurse -Force "$tmp\glm" "$LibDir\glm\" -ErrorAction SilentlyContinue
                if (!(Test-Path "$LibDir\glm\glm\glm.hpp")) {
                    # glm repo structure may be glm/glm
                    if (Test-Path "$LibDir\glm\glm\glm\glm.hpp") {
                        Move-Item "$LibDir\glm\glm\glm\*" "$LibDir\glm\glm\" -Force
                    }
                }
                Write-OK "GLM downloaded via git"
                $downloaded++
            }
        } else {
            throw "git not found"
        }
    } catch {
        Write-Warn "GLM git failed, trying zip..."
        try {
            $zip = Join-Path $env:TEMP "glm.zip"
            Invoke-WebRequest -Uri "https://github.com/g-truc/glm/archive/refs/tags/0.9.9.8.zip" -OutFile $zip -UseBasicParsing
            Expand-Archive -Path $zip -DestinationPath $env:TEMP -Force
            Copy-Item -Recurse -Force "$env:TEMP\glm-0.9.9.8\glm" "$LibDir\glm\" -ErrorAction SilentlyContinue
            Write-OK "GLM downloaded via zip"
            $downloaded++
        } catch {
            Write-Err "GLM download failed: $_"
            $failed++
        }
    }
} else { Write-OK "GLM already present" }

# --- GLAD ---
if (!(Test-Path "$LibDir\glad\include\glad\glad.h") -or $Force) {
    Write-Info "Downloading GLAD..."
    try {
        Invoke-WebRequest -Uri "https://raw.githubusercontent.com/Dav1dde/glad/master/include/glad/glad.h" -OutFile "$LibDir\glad\include\glad\glad.h" -UseBasicParsing
        Invoke-WebRequest -Uri "https://raw.githubusercontent.com/Dav1dde/glad/master/src/glad.c" -OutFile "$LibDir\glad\src\glad.c" -UseBasicParsing
        Invoke-WebRequest -Uri "https://raw.githubusercontent.com/Dav1dde/glad/master/include/KHR/khrplatform.h" -OutFile "$LibDir\glad\include\KHR\khrplatform.h" -UseBasicParsing
        Write-OK "GLAD downloaded"
        $downloaded++
    } catch {
        Write-Err "GLAD download failed: $_"
        $failed++
    }
} else { Write-OK "GLAD already present" }

# --- GLFW headers ---
if (!(Test-Path "$LibDir\glfw\include\GLFW\glfw3.h") -or $Force) {
    Write-Info "Downloading GLFW headers..."
    try {
        Invoke-WebRequest -Uri "https://raw.githubusercontent.com/glfw/glfw/master/include/GLFW/glfw3.h" -OutFile "$LibDir\glfw\include\GLFW\glfw3.h" -UseBasicParsing
        Invoke-WebRequest -Uri "https://raw.githubusercontent.com/glfw/glfw/master/include/GLFW/glfw3native.h" -OutFile "$LibDir\glfw\include\GLFW\glfw3native.h" -UseBasicParsing
        Write-OK "GLFW headers downloaded"
        $downloaded++
    } catch {
        Write-Warn "GLFW header download failed: $_"
        $failed++
    }
} else { Write-OK "GLFW headers present" }

# --- GLFW lib ---
$glfwLibPath = "$LibDir\glfw\lib-vc2022\glfw3.lib"
if (!(Test-Path $glfwLibPath) -or $Force) {
    Write-Info "Downloading GLFW prebuilt lib for $Arch..."
    try {
        $url = "https://github.com/glfw/glfw/releases/download/3.3.8/glfw-3.3.8.bin.WIN64.zip"
        if ($Arch -eq "x86") { $url = "https://github.com/glfw/glfw/releases/download/3.3.8/glfw-3.3.8.bin.WIN32.zip" }
        $zip = Join-Path $env:TEMP "glfw.zip"
        Invoke-WebRequest -Uri $url -OutFile $zip -UseBasicParsing
        Expand-Archive -Path $zip -DestinationPath "$env:TEMP\glfw_tmp" -Force
        $lib = Get-ChildItem "$env:TEMP\glfw_tmp" -Recurse -Filter "glfw3.lib" | Where-Object { $_.FullName -like "*lib-vc2022*" } | Select-Object -First 1
        if ($lib) {
            New-Item -ItemType Directory -Force -Path "$LibDir\glfw\lib-vc2022" | Out-Null
            Copy-Item $lib.FullName "$LibDir\glfw\lib-vc2022\glfw3.lib" -Force
            $dll = Get-ChildItem "$env:TEMP\glfw_tmp" -Recurse -Filter "glfw3.dll" | Select-Object -First 1
            if ($dll) { Copy-Item $dll.FullName "$LibDir\glfw\lib-vc2022\" -Force; Copy-Item $dll.FullName "$InstallDir\" -Force -ErrorAction SilentlyContinue }
            Write-OK "GLFW lib downloaded"
            $downloaded++
        } else {
            throw "glfw3.lib not found in archive"
        }
        Remove-Item -Recurse -Force "$env:TEMP\glfw_tmp" -ErrorAction SilentlyContinue
        Remove-Item -Force $zip -ErrorAction SilentlyContinue
    } catch {
        Write-Err "GLFW lib download failed: $_"
        $failed++
    }
} else { Write-OK "GLFW lib present" }

# --- ImGui ---
if (!(Test-Path "$AerosDir\src\imgui\imgui.h") -or $Force) {
    Write-Info "Downloading ImGui..."
    try {
        $tmp = Join-Path $env:TEMP "imgui_aeros"
        if (Test-Path $tmp) { Remove-Item -Recurse -Force $tmp }
        git clone --depth 1 https://github.com/ocornut/imgui.git $tmp 2>&1 | Out-Null
        if (Test-Path "$tmp\imgui.h") {
            New-Item -ItemType Directory -Force -Path "$AerosDir\src\imgui" | Out-Null
            Copy-Item "$tmp\*.cpp" "$tmp\*.h" "$AerosDir\src\imgui\" -Force
            Copy-Item "$tmp\backends\imgui_impl_glfw.*" "$AerosDir\src\imgui\" -Force
            Copy-Item "$tmp\backends\imgui_impl_opengl3.*" "$AerosDir\src\imgui\" -Force
            Write-OK "ImGui downloaded"
            $downloaded++
        }
        Remove-Item -Recurse -Force $tmp -ErrorAction SilentlyContinue
    } catch {
        Write-Err "ImGui download failed: $_"
        $failed++
    }
} else { Write-OK "ImGui present" }

# --- Vulkan SDK check ---
Write-Info "Checking Vulkan SDK..."
$vulkanFound = $false
if ($env:VULKAN_SDK -and (Test-Path "$env:VULKAN_SDK\Lib\vulkan-1.lib")) {
    Write-OK "Vulkan SDK found via VULKAN_SDK env: $env:VULKAN_SDK"
    $vulkanFound = $true
} else {
    $vulkanPaths = @("$env:ProgramFiles\VulkanSDK", "${env:ProgramFiles(x86)}\VulkanSDK")
    foreach ($base in $vulkanPaths) {
        if (Test-Path $base) {
            $dirs = Get-ChildItem $base -Directory | Sort-Object Name -Descending
            foreach ($d in $dirs) {
                if (Test-Path "$($d.FullName)\Lib\vulkan-1.lib") {
                    Write-OK "Vulkan SDK found: $($d.FullName)"
                    $vulkanFound = $true
                    break
                }
            }
        }
        if ($vulkanFound) { break }
    }
}
if (-not $vulkanFound) {
    Write-Warn "Vulkan SDK not found — Vulkan renderer will fallback to OpenGL"
    Write-Info "Download Vulkan SDK from https://vulkan.lunarg.com/sdk/home"
    Write-Info "Or install via: winget install LunarG.VulkanSDK"
} else {
    Write-OK "Vulkan SDK OK"
}

# --- Models / STL samples ---
Write-Info "Checking sample models..."
$modelsDir = "$InstallDir\models"
if (!(Test-Path $modelsDir)) { New-Item -ItemType Directory -Force -Path $modelsDir | Out-Null }

# Create a simple sample STL if none present
$sampleStl = "$modelsDir\sample_cube.stl"
if (!(Test-Path $sampleStl)) {
    Write-Info "Creating sample cube STL..."
    @"
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
"@ | Out-File -FilePath $sampleStl -Encoding ASCII
    Write-OK "Sample STL created"
}

# --- Config / Settings ---
Write-Info "Checking config..."
$configPath = "$InstallDir\aeros_settings.ini"
if (!(Test-Path $configPath)) {
    Write-Info "Creating default config..."
    @"
# Aeros Engine v$VERSION Settings
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
"@ | Out-File -FilePath $configPath -Encoding UTF8
    Write-OK "Default config created"
}

# --- Summary ---
Write-Info "=== Dependency Download Summary v$VERSION ==="
Write-Info "Downloaded: $downloaded"
if ($failed -gt 0) { Write-Warn "Failed: $failed" } else { Write-OK "Failed: 0" }
Write-Info "InstallDir: $InstallDir"
Write-Info "LibDir: $LibDir"
Write-Info "Arch: $Arch"
Write-Info "Vulkan: $(if ($vulkanFound) { 'Found' } else { 'Not found — OpenGL fallback' })"

if ($failed -eq 0) {
    Write-OK "All dependencies ready — you can now build with build.bat"
    exit 0
} else {
    Write-Warn "Some dependencies failed — build may still work with system libs"
    exit 0
}
