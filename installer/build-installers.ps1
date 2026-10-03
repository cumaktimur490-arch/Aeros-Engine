param(
    [string]$Version = "",
    [ValidateSet("x64","x86","both")][string]$Arch = "both",
    [ValidateSet("full","update","all")][string]$Type = "all",
    [string]$ISCCPath = ""
)

# Aeros Engine — Сборка установщиков через Inno Setup
# Требует Inno Setup 6.x установленного
# Использование:
#   .\build-installers.ps1
#   .\build-installers.ps1 -Arch x64 -Type full -Version 1.0.0

$ErrorActionPreference = "Stop"
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RootDir = Resolve-Path "$ScriptDir\.."
$ReleaseDir = Join-Path $RootDir "release"

if (-not $Version) {
    $VersionFile = Join-Path $RootDir "VERSION"
    if (Test-Path $VersionFile) {
        $Version = (Get-Content $VersionFile -Raw).Trim()
    } else {
        $Version = "1.0.0"
    }
}

Write-Host "[Aeros] Building installers v$Version Arch=$Arch Type=$Type" -ForegroundColor Cyan

# Поиск ISCC.exe
if (-not $ISCCPath) {
    $possiblePaths = @(
        "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe",
        "${env:ProgramFiles}\Inno Setup 6\ISCC.exe",
        "${env:ProgramFiles(x86)}\Inno Setup 5\ISCC.exe",
        "C:\Program Files (x86)\Inno Setup 6\ISCC.exe",
        "ISCC.exe"
    )
    foreach ($p in $possiblePaths) {
        if (Get-Command $p -ErrorAction SilentlyContinue) { $ISCCPath = $p; break }
        if (Test-Path $p) { $ISCCPath = $p; break }
    }
}

if (-not $ISCCPath -or -not (Test-Path $ISCCPath -ErrorAction SilentlyContinue) -and -not (Get-Command $ISCCPath -ErrorAction SilentlyContinue)) {
    Write-Host "[ERROR] Inno Setup compiler (ISCC.exe) not found!" -ForegroundColor Red
    Write-Host "Install Inno Setup 6 from https://jrsoftware.org/isinfo.php" -ForegroundColor Yellow
    Write-Host "Or specify -ISCCPath path/to/ISCC.exe" -ForegroundColor Yellow
    exit 1
}

Write-Host "[Aeros] Using ISCC: $ISCCPath" -ForegroundColor Gray

if (-not (Test-Path $ReleaseDir)) { New-Item -ItemType Directory -Path $ReleaseDir | Out-Null }

# Проверка бинарей
$binDir = Join-Path $RootDir "aeros\bin"
if (-not (Test-Path (Join-Path $binDir "main.exe")) -and -not (Test-Path (Join-Path $binDir "main-x64.exe"))) {
    Write-Host "[WARN] No binaries found in $binDir. Build first: aeros\build.bat x64" -ForegroundColor Yellow
}

function Build-Installer($issFile) {
    $fullPath = Join-Path $ScriptDir $issFile
    if (-not (Test-Path $fullPath)) {
        Write-Host "[WARN] ISS not found: $fullPath" -ForegroundColor Yellow
        return
    }
    Write-Host "[Aeros] Compiling $issFile ..." -ForegroundColor Green
    $args = "`"$fullPath`" /DAppVersion=$Version"
    $proc = Start-Process -FilePath $ISCCPath -ArgumentList $args -Wait -PassThru -NoNewWindow
    if ($proc.ExitCode -ne 0) {
        Write-Host "[ERROR] Failed to compile $issFile (exit $($proc.ExitCode))" -ForegroundColor Red
        throw "ISCC failed"
    }
    Write-Host "[OK] $issFile compiled" -ForegroundColor Green
}

try {
    if ($Type -eq "full" -or $Type -eq "all") {
        if ($Arch -eq "both" -or $Arch -eq "x64") { Build-Installer "AerosEngine-x64.iss" }
        if ($Arch -eq "both" -or $Arch -eq "x86") { Build-Installer "AerosEngine-x86.iss" }
    }
    if ($Type -eq "update" -or $Type -eq "all") {
        if ($Arch -eq "both" -or $Arch -eq "x64") { Build-Installer "AerosEngine-Updater-x64.iss" }
        if ($Arch -eq "both" -or $Arch -eq "x86") { Build-Installer "AerosEngine-Updater-x86.iss" }
    }
} catch {
    Write-Host "[ERROR] $_" -ForegroundColor Red
    exit 1
}

Write-Host "[Aeros] Installers built. Output: $ReleaseDir" -ForegroundColor Cyan
Get-ChildItem $ReleaseDir -Filter "*.exe" | Format-Table Name, Length, LastWriteTime -AutoSize
