param(
    [string]$Version = "",
    [ValidateSet("x64","x86","arm64","both","all")][string]$Arch = "both",
    [ValidateSet("full","update","all")][string]$Type = "all",
    [string]$ISCCPath = ""
)

# Aeros Engine — Сборка установщиков через Inno Setup
# Требует Inno Setup 6.x установленного
# Использование:
#   .\build-installers.ps1
#   .\build-installers.ps1 -Arch x64 -Type full -Version 1.0.0
#
# ВАЖНО: перед упаковкой скрипт сверяет PE-заголовок бинаря той архитектуры,
# которую собирается положить в установщик. Раньше .iss выбирали бинарь через
# Check: FileExists('..\aeros\bin\main-<arch>.exe') — но Check выполняется во
# время установки на ПК пользователя, где такого пути нет, поэтому ставился
# общий bin\main.exe (архитектуры последней сборки) → «CreateProcess код 216».

$ErrorActionPreference = "Stop"
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RootDir = Resolve-Path "$ScriptDir\.."
$ReleaseDir = Join-Path $RootDir "release"
$BinDir = Join-Path $RootDir "aeros\bin"

if (-not $Version) {
    $VersionFile = Join-Path $RootDir "VERSION"
    if (Test-Path $VersionFile) {
        $Version = (Get-Content $VersionFile -Raw).Trim()
    } else {
        $Version = "1.0.0"
    }
}

Write-Host "[Aeros] Building installers v$Version Arch=$Arch Type=$Type" -ForegroundColor Cyan

# Ожидаемое значение Machine из PE-заголовка
$PEMachine = @{ "x64" = "8664"; "x86" = "014C"; "arm64" = "AA64" }

function Get-PEMachine([string]$Path) {
    $b = [System.IO.File]::ReadAllBytes($Path)
    if ($b.Length -lt 64 -or $b[0] -ne 0x4D -or $b[1] -ne 0x5A) { return "????" }
    $pe = [BitConverter]::ToInt32($b, 60)
    if ($pe + 6 -ge $b.Length) { return "????" }
    return [BitConverter]::ToUInt16($b, $pe + 4).ToString('X4')
}

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

# Проверка бинарей: для каждой заказанной архитектуры нужен exe ИМЕННО этой архитектуры
function Assert-ArchBinary([string]$ArchName) {
    $exe = Join-Path $BinDir "main-$ArchName.exe"
    if (-not (Test-Path $exe)) {
        throw "Не найден $exe — соберите $ArchName до упаковки (aeros\build.bat $ArchName или aeros\build-cpu.bat $ArchName). Общий bin\main.exe намеренно не используется: он может быть чужой архитектуры."
    }
    $mach = Get-PEMachine $exe
    if ($mach -ne $PEMachine[$ArchName]) {
        throw "$(Split-Path $exe -Leaf): PE machine = $mach, ожидалось $($PEMachine[$ArchName]) ($ArchName). Бинарь собран не для той архитектуры — упаковывать нельзя."
    }
    Write-Host "[OK] main-$ArchName.exe: PE machine $mach ($ArchName), $((Get-Item $exe).Length) байт" -ForegroundColor Green
}

function Build-Installer($issFile, [string]$ArchName, [string]$OutName) {
    $fullPath = Join-Path $ScriptDir $issFile
    if (-not (Test-Path $fullPath)) {
        Write-Host "[WARN] ISS not found: $fullPath" -ForegroundColor Yellow
        return
    }
    # 1. Бинарь строго своей архитектуры
    Assert-ArchBinary $ArchName

    Write-Host "[Aeros] Compiling $issFile ..." -ForegroundColor Green
    Write-Host "  ISS: $fullPath"
    Write-Host "  ISCC: $ISCCPath"
    Write-Host "  Version: $Version"
    Write-Host "  Bin dir contents:"
    Get-ChildItem $BinDir | Format-Table Name, Length | Out-String | Write-Host

    # 2. ISCC запускаем с рабочей папкой installer\ — тогда относительные пути
    #    (и в [Files], и в #if FileExists препроцессора) трактуются одинаково.
    try {
        $tempOut = [System.IO.Path]::GetTempFileName()
        $tempErr = [System.IO.Path]::GetTempFileName()
        Write-Host "  Running: $ISCCPath $fullPath /DAppVersion=$Version"
        $proc = Start-Process -FilePath $ISCCPath `
            -ArgumentList "`"$fullPath`"", "/DAppVersion=$Version" `
            -WorkingDirectory $ScriptDir `
            -Wait -PassThru -NoNewWindow `
            -RedirectStandardOutput $tempOut -RedirectStandardError $tempErr
        $exitCode = $proc.ExitCode
        $stdout = Get-Content $tempOut -Raw -ErrorAction SilentlyContinue
        $stderr = Get-Content $tempErr -Raw -ErrorAction SilentlyContinue
        Write-Host "  ISCC stdout:"
        Write-Host $stdout
        if ($stderr) { Write-Host "  ISCC stderr:"; Write-Host $stderr }
        Remove-Item $tempOut -Force -ErrorAction SilentlyContinue
        Remove-Item $tempErr -Force -ErrorAction SilentlyContinue
        if ($exitCode -ne 0) {
            Write-Host "[ERROR] Failed to compile $issFile (exit $exitCode)" -ForegroundColor Red
            throw "ISCC failed for $issFile with exit $exitCode"
        }
    } catch {
        Write-Host "[ERROR] Exception compiling $issFile : $_" -ForegroundColor Red
        Write-Host $_.Exception.Message
        Write-Host $_.ScriptStackTrace
        throw
    }

    # 3. Результат должен появиться в release\
    $out = Join-Path $ReleaseDir $OutName
    if (-not (Test-Path $out)) { throw "ISCC отработал без ошибки, но файл не создан: $out" }
    $size = (Get-Item $out).Length
    # Установщик без бинаря весит ~60–100 КБ (лицензия, README, run.bat, языки)
    if ($size -lt 150000) { throw "$OutName подозрительно маленький ($size байт) — похоже, бинарь не упакован" }
    Write-Host "[OK] $issFile -> $OutName ($size байт)" -ForegroundColor Green
}

try {
    if ($Type -eq "full" -or $Type -eq "all") {
        if ($Arch -eq "both" -or $Arch -eq "all" -or $Arch -eq "x64") { Build-Installer "AerosEngine-x64.iss" "x64" "Aeros-Engine-Setup-x64-v$Version.exe" }
        if ($Arch -eq "both" -or $Arch -eq "all" -or $Arch -eq "x86") { Build-Installer "AerosEngine-x86.iss" "x86" "Aeros-Engine-Setup-x86-v$Version.exe" }
        if ($Arch -eq "all" -or $Arch -eq "arm64") { Build-Installer "AerosEngine-arm64.iss" "arm64" "Aeros-Engine-Setup-arm64-v$Version.exe" }
    }
    if ($Type -eq "update" -or $Type -eq "all") {
        if ($Arch -eq "both" -or $Arch -eq "all" -or $Arch -eq "x64") { Build-Installer "AerosEngine-Updater-x64.iss" "x64" "Aeros-Engine-Update-x64-v$Version.exe" }
        if ($Arch -eq "both" -or $Arch -eq "all" -or $Arch -eq "x86") { Build-Installer "AerosEngine-Updater-x86.iss" "x86" "Aeros-Engine-Update-x86-v$Version.exe" }
        if ($Arch -eq "all" -or $Arch -eq "arm64") { Build-Installer "AerosEngine-Updater-arm64.iss" "arm64" "Aeros-Engine-Update-arm64-v$Version.exe" }
    }
} catch {
    Write-Host "[ERROR] $_" -ForegroundColor Red
    exit 1
}

Write-Host "[Aeros] Installers built. Output: $ReleaseDir" -ForegroundColor Cyan
Get-ChildItem $ReleaseDir -Filter "*.exe" | Format-Table Name, Length, LastWriteTime -AutoSize
