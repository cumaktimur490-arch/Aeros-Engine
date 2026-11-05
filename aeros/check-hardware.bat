@echo off
REM Aeros Engine — Проверка совместимости железа v1.20.1
REM Проверяет CPU, RAM, GPU и рекомендует версию

echo ========================================
echo Aeros Engine — Hardware Check v1.20.1
echo ========================================
echo.

REM CPU
echo [CPU]
wmic cpu get Name,NumberOfCores,NumberOfLogicalProcessors,Architecture /value 2>nul | find "="
echo.

REM RAM
echo [RAM]
wmic ComputerSystem get TotalPhysicalMemory /value 2>nul | find "="
wmic OS get FreePhysicalMemory /value 2>nul | find "="
echo.

REM GPU
echo [GPU]
wmic path win32_VideoController get Name,DriverVersion,AdapterRAM /value 2>nul | find "="
echo.

REM OS
echo [OS]
wmic os get Caption,Version,OSArchitecture /value 2>nul | find "="
echo.

REM Battery
echo [Battery]
wmic Path Win32_Battery get BatteryStatus,EstimatedChargeRemaining /value 2>nul | find "="
echo.

REM Рекомендация
echo [Recommendation]
set HW_THREADS=4
for /f "tokens=2 delims==" %%a in ('wmic cpu get NumberOfLogicalProcessors /value 2^>nul ^| find "="') do set HW_THREADS=%%a
if "%HW_THREADS%"=="" set HW_THREADS=4

echo CPU Threads: %HW_THREADS%
if %HW_THREADS% LEQ 2 (
  echo -> ULTRA-LITE Potato recommended (Atom/Celeron 2GB RAM)
  echo    Build: build-ultra-lite.bat x64
  echo    Run: bin\main-ultra-lite-x64.exe --preset potato
) else if %HW_THREADS% LEQ 4 (
  echo -> LITE recommended (i3-3xxx, HD 4000, GT 620M, 4GB RAM)
  echo    Build: build-lite.bat x64
  echo    Run: bin\main-lite-x64.exe --preset low
) else (
  echo -> FULL recommended (i5+, GTX 1060+, 8GB+)
  echo    Build: build.bat x64
  echo    Run: bin\main-x64.exe
)

echo.
echo For auto-detection, use: launcher.bat
echo ========================================
pause
