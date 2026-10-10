@echo off
REM Aeros Engine — Android APK сборка v1.20.1 для слабых телефонов
REM Использование: build-android.bat [lite|potato|full|clean|deps]

setlocal EnableDelayedExpansion
set MODE=%~1
if "%MODE%"=="" set MODE=lite
set VERSION=1.20.1
if exist "..\..\VERSION" set /p VERSION=<"..\..\VERSION"
if exist "..\VERSION" set /p VERSION=<"..\VERSION"

echo [Aeros Android] Building v%VERSION% Android %MODE% — for weak phones

if /I "%MODE%"=="clean" goto :clean
if /I "%MODE%"=="deps" goto :install_deps
if /I "%MODE%"=="check" goto :check_sdk
if /I "%MODE%"=="help" goto :help

REM Проверка SDK
:check_sdk
if "%ANDROID_HOME%"=="" (
  if exist "%USERPROFILE%\AppData\Local\Android\Sdk" set ANDROID_HOME=%USERPROFILE%\AppData\Local\Android\Sdk
)
if "%ANDROID_HOME%"=="" (
  echo [ERROR] ANDROID_HOME not set — install Android Studio or SDK
  echo   set ANDROID_HOME=C:\Users\%USERNAME%\AppData\Local\Android\Sdk
  exit /b 1
)
echo [OK] Android SDK: %ANDROID_HOME%
if "%ANDROID_NDK_HOME%"=="" (
  for /D %%D in ("%ANDROID_HOME%\ndk\*") do set ANDROID_NDK_HOME=%%D
)
echo [OK] Android NDK: %ANDROID_NDK_HOME%
if /I "%MODE%"=="check" goto :eof

REM Сборка
:build_apk
set BUILD_TYPE=%MODE%
echo [INFO] Building APK — type: %BUILD_TYPE%

if exist "gradlew.bat" (
  set GRADLE=gradlew.bat
) else (
  where gradle >nul 2>&1
  if !ERRORLEVEL! neq 0 (
    echo [ERROR] Gradle not found — install gradle or use Android Studio
    exit /b 1
  )
  set GRADLE=gradle
)

if /I "%BUILD_TYPE%"=="potato" (
  echo [INFO] Building Potato APK — weakest phones 1GB RAM Adreno 306
  call %GRADLE% assemblePotatoRelease
) else (
  echo [INFO] Building Lite APK — weak phones 2-4GB RAM
  call %GRADLE% assembleRelease
)

REM Поиск APK
for /R "app\build\outputs\apk" %%F in (*.apk) do (
  echo [OK] APK built: %%F
  copy /Y "%%F" "..\..\release\aeros-engine-android-%BUILD_TYPE%-v%VERSION%.apk" >nul
  echo [OK] Copied to release\aeros-engine-android-%BUILD_TYPE%-v%VERSION%.apk
  goto :done
)
echo [WARN] APK not found in app\build\outputs\apk
dir /S /B *.apk 2>nul | head -n 10

:done
goto :eof

:clean
echo [INFO] Cleaning Android build...
if exist "gradlew.bat" call gradlew.bat clean
rmdir /S /Q app\build 2>nul
rmdir /S /Q .gradle 2>nul
rmdir /S /Q build 2>nul
echo [OK] Cleaned
goto :eof

:install_deps
echo [INFO] Installing Android dependencies...
if "%ANDROID_HOME%"=="" (
  echo [ERROR] ANDROID_HOME not set
  exit /b 1
)
set SDKMANAGER=%ANDROID_HOME%\cmdline-tools\latest\bin\sdkmanager.bat
if not exist "%SDKMANAGER%" set SDKMANAGER=%ANDROID_HOME%\tools\bin\sdkmanager.bat
if exist "%SDKMANAGER%" (
  call %SDKMANAGER% --install "platform-tools" "build-tools;34.0.0" "platforms;android-34" "platforms;android-21" "ndk;25.1.8937393"
  echo [OK] Android dependencies installed
) else (
  echo [WARN] sdkmanager not found — install Android Studio
)
goto :eof

:help
echo Aeros Engine Android Build Script v1.20.1
echo For weak phones: Adreno 306, Mali-400, 1-2GB RAM, Android 5.0+
echo Usage: build-android.bat [mode]
echo Modes:
echo   lite         — build Lite APK (default)
echo   potato       — build Potato APK — 1GB RAM Adreno 306
echo   full         — build Full APK
echo   all          — build both Lite and Potato
echo   clean        — clean
echo   deps         — install Android SDK dependencies
echo   check        — check SDK/NDK
echo   help         — this help
goto :eof
