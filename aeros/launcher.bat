@echo off
REM Aeros Engine — Smart Launcher v1.20.1 — авто-определение железа и запуск нужной версии
REM Для слабых устройств: Atom/Celeron -> Ultra-Lite, i3-3xxx/HD4000 -> Lite, i5+ -> Full

setlocal EnableDelayedExpansion
chdir /d "%~dp0"
set BINDIR=%CD%\bin

echo [Aeros Launcher] Detecting hardware...

REM Проверка CPU threads через wmic
set HW_THREADS=4
for /f "tokens=2 delims==" %%a in ('wmic cpu get NumberOfLogicalProcessors /value 2^>nul ^| find "="') do set HW_THREADS=%%a
if "%HW_THREADS%"=="" set HW_THREADS=4

REM Проверка RAM через wmic
set TOTAL_RAM_MB=4096
for /f "tokens=2 delims==" %%a in ('wmic ComputerSystem get TotalPhysicalMemory /value 2^>nul ^| find "="') do (
  set BYTES=%%a
  set /a TOTAL_RAM_MB=!BYTES:~0,-6!/1000 2>nul
)
REM Fallback
if "%TOTAL_RAM_MB%"=="4096" (
  for /f "skip=1 tokens=4" %%a in ('wmic ComputerSystem get TotalPhysicalMemory 2^>nul') do (
    if not "%%a"=="" set BYTES=%%a
  )
)

REM Проверка батареи
set ON_BATTERY=0
for /f "tokens=2 delims==" %%a in ('wmic Path Win32_Battery get BatteryStatus /value 2^>nul ^| find "="') do (
  if "%%a"=="1" set ON_BATTERY=1
)

echo [Aeros Launcher] CPU threads: %HW_THREADS%, RAM: %TOTAL_RAM_MB% MB, Battery: %ON_BATTERY%

REM Логика выбора версии
set LAUNCH_MODE=full
set EXE_NAME=main-x64.exe

if %HW_THREADS% LEQ 2 (
  echo [Aeros Launcher] Very weak CPU %HW_THREADS% threads — Ultra-Lite Potato mode
  set LAUNCH_MODE=potato
  set EXE_NAME=main-ultra-lite-x64.exe
) else if %HW_THREADS% LEQ 4 (
  REM Проверяем RAM
  if %TOTAL_RAM_MB% LSS 4000 (
    echo [Aeros Launcher] Weak CPU %HW_THREADS% threads + low RAM %TOTAL_RAM_MB% MB — Lite mode
    set LAUNCH_MODE=lite
    set EXE_NAME=main-lite-x64.exe
  ) else (
    echo [Aeros Launcher] i3-like %HW_THREADS% threads — Lite recommended but trying Full first
    set LAUNCH_MODE=lite
    set EXE_NAME=main-lite-x64.exe
  )
) else (
  if %TOTAL_RAM_MB% LSS 4000 (
    echo [Aeros Launcher] Decent CPU but low RAM %TOTAL_RAM_MB% MB — Medium/Lite
    set LAUNCH_MODE=lite
    set EXE_NAME=main-lite-x64.exe
  ) else (
    echo [Aeros Launcher] Modern PC %HW_THREADS% threads %TOTAL_RAM_MB% MB — Full mode
    set LAUNCH_MODE=full
    set EXE_NAME=main-x64.exe
  )
)

REM Проверка наличия файлов — fallback
if not exist "%BINDIR%\%EXE_NAME%" (
  echo [Aeros Launcher] %EXE_NAME% not found, trying alternatives...
  if exist "%BINDIR%\main-x64.exe" (
    set EXE_NAME=main-x64.exe
    set LAUNCH_MODE=full
  ) else if exist "%BINDIR%\main-lite-x64.exe" (
    set EXE_NAME=main-lite-x64.exe
    set LAUNCH_MODE=lite
  ) else if exist "%BINDIR%\main-ultra-lite-x64.exe" (
    set EXE_NAME=main-ultra-lite-x64.exe
    set LAUNCH_MODE=potato
  ) else if exist "%BINDIR%\main.exe" (
    set EXE_NAME=main.exe
    set LAUNCH_MODE=full
  ) else (
    echo [ERROR] No executable found in %BINDIR%
    echo Please build first: build.bat x64 or build-lite.bat x64 or build-ultra-lite.bat x64
    pause
    exit /b 1
  )
)

echo [Aeros Launcher] Launching %LAUNCH_MODE% mode: %EXE_NAME% %*
echo [Aeros Launcher] Preset: %LAUNCH_MODE%, Battery saver: %ON_BATTERY%

REM Запуск с правильными аргументами
if "%LAUNCH_MODE%"=="potato" (
  "%BINDIR%\%EXE_NAME%" --preset potato %*
) else if "%LAUNCH_MODE%"=="lite" (
  "%BINDIR%\%EXE_NAME%" --preset low %*
) else (
  "%BINDIR%\%EXE_NAME%" %*
)

exit /b %ERRORLEVEL%
