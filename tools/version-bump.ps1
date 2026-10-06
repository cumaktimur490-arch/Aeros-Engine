param(
    [Parameter(Mandatory=$true)][string]$NewVersion
)

# Обновляет версию во всех файлах
# Использование: .\version-bump.ps1 1.1.0

$RootDir = Resolve-Path "$PSScriptRoot\.."
$VersionFile = Join-Path $RootDir "VERSION"
$VersionHeader = Join-Path $RootDir "aeros\src\version.h"

if ($NewVersion -notmatch "^\d+\.\d+\.\d+$") {
    Write-Host "[ERROR] Version must be X.Y.Z (e.g. 1.0.1)" -ForegroundColor Red
    exit 1
}

$parts = $NewVersion.Split(".")
$major = $parts[0]; $minor = $parts[1]; $patch = $parts[2]

Write-Host "[Aeros] Bumping version to $NewVersion" -ForegroundColor Cyan

# VERSION file
Set-Content -Path $VersionFile -Value $NewVersion -NoNewline
Write-Host "[OK] $VersionFile"

# version.h
$headerContent = @"
#pragma once
// =====================================================
// Aeros Engine — версия приложения
// Этот файл генерируется из корневого VERSION, но хранится в репозитории
// для сборки без дополнительных шагов.
// =====================================================

#define AEROS_VERSION_MAJOR $major
#define AEROS_VERSION_MINOR $minor
#define AEROS_VERSION_PATCH $patch

#define AEROS_VERSION_STRING "$NewVersion"
#define AEROS_VERSION_FULL   "Aeros Engine v$NewVersion"

#define AEROS_APP_NAME       "Aeros Engine"
#define AEROS_APP_PUBLISHER  "GoGonam AoS."
#define AEROS_APP_URL        "https://github.com/cumaktimur490-arch/Aeros-Engine"
#define AEROS_APP_ID         "{A7B8C9D0-E1F2-4A5B-8C9D-0E1F2A3B4C5D}"
#define AEROS_APP_ID_X86     "{A7B8C9D0-E1F2-4A5B-8C9D-0E1F2A3B4C5E}"
"@

Set-Content -Path $VersionHeader -Value $headerContent -Encoding UTF8
Write-Host "[OK] $VersionHeader"

# Update ISS files (they have default #define AppVersion)
Get-ChildItem "$RootDir\installer\*.iss" | ForEach-Object {
    $content = Get-Content $_.FullName -Raw
    $newContent = $content -replace '#define AppVersion ".*?"', "#define AppVersion `"$NewVersion`""
    if ($newContent -ne $content) {
        Set-Content -Path $_.FullName -Value $newContent -Encoding UTF8
        Write-Host "[OK] Updated $($_.Name)"
    }
}

Write-Host "[Aeros] Version bump to $NewVersion done!" -ForegroundColor Green
