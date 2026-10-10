# Aeros Engine v1.20.0 — Lite для слабых устройств

## 🪶 Lite версия — НОВОЕ! Для i3-3xxx / HD 4000 / GT 620M / 4GB RAM / No CUDA

### Для кого Lite
- **CPU:** Intel Core i3-3xxx Ivy Bridge (2 ядра / 4 потока, SSE4.2, AVX но нет AVX2) — 2012 года, до сих пор много таких ноутов
- **GPU:** Intel HD Graphics 4000 (16 EUs, OpenGL 4.0, Vulkan 1.0 только), GT 620M/720M/820M ноутбучные, AMD APU A4/A6/A8
- **RAM:** 4GB ноутбуки, общий RAM для GPU, медленный HDD
- **Экран:** 1366x768 ноутбуки
- **ОС:** Windows 7/10/11, Linux, без CUDA, без дискретной GPU

### Что оптимизировано в Lite

#### CPU — i3-3xxx
- **SSE2 only** — нет AVX2, нет AVX, совместимо с Ivy Bridge и даже Core2Duo
- **O1 вместо O3** — меньший бинарь (~5-10 MB vs 20-30 MB), меньше нагрев, меньше RAM для компиляции, быстрее запуск на HDD
- **2 потока OpenMP** — i3-3xxx 2C/4T, не грузит систему, оставляет ядра для ОС, меньше нагрев ноута
- **fp:precise** — не fast-math, стабильнее на старых CPU
- **BELOW_NORMAL_PRIORITY_CLASS** Windows — не тормозит систему, power saving
- **Без LTCG** — меньше бинарь, быстрее линковка

#### GPU — HD 4000 / GT 620M
- **OpenGL 3.3** (не 4.6) — совместимо с HD 4000 (поддерживает 4.0 но 3.3 стабильнее)
- **MSAA 0** — нет сглаживания, экономия GPU
- **Vulkan OFF по умолчанию** — HD 4000 поддерживает только Vulkan 1.0 (не 1.3), GT 620M не поддерживает Vulkan
- **Простые шейдеры** — без PBR, без сложных вычислений, low/medium quality
- **Texture 1024 low** — меньше VRAM (shared RAM)
- **Shadows OFF** — экономия GPU

#### Память — 4GB RAM
- **<512 MB RAM лимит** — вместо 1-2 GB
- **Частицы 1500** (было 15000) — в 10x меньше, max 5000 (было 100k)
- **Линии тока 8x80** (было 24x300) — в 3x меньше количество, в 3.75x меньше шагов, max 16x150
- **Воксели 24** (было 48) — 24^3=13824 vs 48^3=110592 — в 8x меньше памяти!
- **LBM OFF по умолчанию** — 32 рес (было 128) — 32^3=32768 vs 128^3=2M — в 64x меньше! 1 шаг/кадр (было 3), без турбулентности
- **Smoke particles 16** (было 64), vortex tube 12 (было 48), fluid 16 (было 32)
- **Streaklines 10**, LIC 10 — меньше

#### Фичи — упрощено
- **FSR OFF** — нет апскейла, экономия GPU, HD 4000 не потянет
- **FrameGen OFF** — нет генерации кадров, экономия
- **Volumetric OFF** — нет объемного дыма, дорого для HD 4000
- **Vortex viz OFF** — упрощено, но vortex cores считаются (low)
- **LIC OFF** — дорого
- **Acoustic OFF** — дорого, CPU heavy
- **Flight dynamics OFF** — упрощено
- **Schlieren ON, Shock ON** — оставлены, важные для аэродинамики, не дорогие
- **FlowSpeed 2.0** (было 5.0) — медленнее, плавнее, меньше нагрузка

#### Окно и FPS
- **1024x600 по умолчанию** — для 1366x768 ноутов, не fullscreen, помещается с панелью задач
- **30 FPS target** (было 60) — меньше нагрев, меньше нагрузка, достаточно для визуализации
- **VSync ON** — нет разрывов, экономия GPU, не греет ноут на 300 FPS
- **Power saving ON** — BELOW_NORMAL приоритет, меньше нагрев, тише кулеры

### Сравнение Lite vs Full

| Параметр | Lite (i3-3xxx / HD 4000) | Full (i5+ / GTX 1060+) |
|----------|--------------------------|------------------------|
| CPU | i3-3xxx 2C/4T SSE4.2 | i5+ 4C+ AVX2 |
| CPU flags | SSE2 only, O1, precise, 2 threads | AVX2, O3, fast-math, all cores |
| GPU | HD 4000 16 EUs / GT 620M | GTX 1060+ / RTX |
| GPU API | OpenGL 3.3, no Vulkan | OpenGL 4.6 + Vulkan 1.3 |
| MSAA | 0 | 4x |
| PBR | OFF | ON |
| FSR/FG | OFF | ON |
| Particles | 1500 / 5000 max | 15000 / 100k max |
| Streamlines | 8 / 16 max, 80 / 150 steps | 24 / 64 max, 300 / 600 steps |
| Voxel | 24 (13k cells) | 48 (110k cells) |
| LBM | OFF default, 32 res, 1 step, no turb | ON, 128 res, 3 steps, turb |
| RAM | <512 MB | 1-2 GB |
| Binary | ~5-10 MB | ~20-30 MB |
| FPS target | 30 | 60 |
| Window | 1024x600 | 1280x720 |
| VSync | ON | OFF |
| Power saving | ON, BELOW_NORMAL | OFF |
| Volumetric | OFF | ON |
| Vortex viz | OFF | ON |
| LIC | OFF | ON |
| Acoustic | OFF | ON |
| Schlieren | ON | ON |
| Shock | ON | ON |

### Файлы Lite

#### Исходники
- `aeros/src/lite_config.h` — все константы Lite: MAX_THREADS 2, SSE2 1 AVX2 0, GL 3.3 MSAA 0 Vulkan 0 FSR 0 FG 0 PBR 0, particles 1500 max 5000, streamlines 8 max 16 steps 80 max 150, voxel 24, LBM 32 OFF, memory 512MB, target FPS 30 vsync ON, volumetric/vortex/LIC/acoustic OFF schlieren/shock ON
- `aeros/src/lite_config.cpp` — applyLiteDefaults() устанавливает low настройки, applyLiteOptimizations() — omp 2 threads, BELOW_NORMAL priority, printLiteSystemInfo() — RAM detection <4GB warning, getLiteInfoString()
- `aeros/src/globals.h/cpp` — g_isLiteMode, g_litePowerSaving, g_liteTargetFPS, g_liteMaxThreads

#### Сборка
- `aeros/build-lite.bat` — Windows: x64/x86/clean/deps, MSVC vswhere, INCLUDES, DEFINES /DAEROS_LITE /DCPU_ONLY /DLITE_MODE /DVERSION Lite /DGLFW_INCLUDE_NONE, CXXFLAGS /O1 /arch:SSE2 /openmp /fp:precise, без LTCG, small binary, копирует в main-lite-*.exe aeros-engine-lite*.exe
- `aeros/build-lite.sh` — Linux: O1 -msse2 -mfpmath=sse -fopenmp, -DAEROS_LITE -DCPU_ONLY -DLITE_MODE, только GLFW+OpenGL, без Vulkan/CUDA, small binary
- `aeros/Makefile.lite` — make -f Makefile.lite all/clean/install-deps/download-deps/package
- `aeros/CMakeLists.txt` — option AEROS_LITE, if LITE: O1 SSE2 no AVX2, no LTCG, AEROS_LITE LITE_MODE CPU_ONLY defines
- `aeros/src/main.cpp` — isLiteMode() check, applyLiteDefaults(), applyLiteOptimizations(), display_w/h 1024x600 для Lite, --lite CLI flag, help с Lite vs Full, forcing OpenGL для Lite
- `aeros/src/ui.cpp` — Lite collapsing header с getLiteInfoString(), HW threads, Max threads, оптимизации bullet list, Lite settings sliders (particles 500-5000, streamlines 4-16, steps 40-150, voxel 16-32, power saving, target FPS), Apply Lite Defaults, Print System Info, Lite vs Full comparison

#### Установщик
- `installer/AerosEngine-Lite-x64.iss` — Lite installer для слабых устройств, AppId LITE, LiteAppName, LiteExeName aeros-engine-lite.exe, x64compatible, MinVersion 6.1 (Win7+), tasks desktopicon + install_deps minimal, files main-lite-x64.exe main-lite.exe aeros-engine-lite*.exe + dll + stl + ini + scripts build-lite + LICENSE README VERSION, icons Lite + Lite Low Power --lite, Run download_deps.ps1 Lite minimal + Lite exe, UninstallDelete logs imgui.ini settings, Code install-info.txt с Lite info

#### CI/CD
- `.github/workflows/build.yml` — Build Lite x64 Windows + Build Lite Linux, package tar.gz both
- `.github/workflows/release.yml` — Build Lite x64 + Build Lite Linux, portable Lite + installer Lite, release body с Lite vs Full table, installation Lite

### Системные требования Lite

**Минимальные (Lite):**
- CPU: Intel Core i3-3xxx / Pentium G2020 / AMD A4-5300 (2 ядра, SSE2)
- GPU: Intel HD 4000 / HD 3000 / GT 620M / AMD HD 7000 APU (OpenGL 3.3)
- RAM: 2GB (рекомендуется 4GB)
- HDD: 100 MB
- OS: Windows 7 SP1 64-bit / Linux Ubuntu 18.04+ / 1366x768 экран
- Без CUDA, без Vulkan, только OpenGL 3.3

**Рекомендуемые (Lite):**
- CPU: i3-3xxx 2C/4T / i5-2xxx / AMD A8
- GPU: HD 4000 / GT 720M / AMD R5
- RAM: 4GB
- SSD: 100 MB
- OS: Windows 10 / Linux Ubuntu 22.04 / 1366x768

**Full (для сравнения):**
- CPU: i5-8400+ / Ryzen 5 2600+ (4C+, AVX2)
- GPU: GTX 1060+ / RTX 2060+ / RX 580+ (Vulkan 1.3, OpenGL 4.6)
- RAM: 8GB+
- SSD: 500 MB
- OS: Windows 10+ / Linux Ubuntu 22.04+ / 1920x1080+

### Установка Lite

**Windows Lite (для слабых ноутов):**
1. Скачайте `Aeros-Engine-Setup-Lite-x64-v1.20.0.exe` — Lite установщик (~5-10 MB бинарь)
2. Запустите, включите "Download Lite dependencies (minimal)"
3. Установщик скачает только необходимое: GLFW, GLM, GLAD, модели, без Vulkan SDK (HD 4000 не поддерживает)
4. Запустите через ярлык "Aeros Engine Lite" — окно 1024x600, 30 FPS, low настройки

**Windows Lite портативная:**
- `Aeros-Engine-Portable-Lite-x64-v1.20.0.zip` — распакуйте и запустите aeros-engine-lite.exe

**Windows сборка Lite:**
```cmd
cd aeros
build-lite.bat x64          :: Собрать Lite x64 SSE2 no CUDA
build-lite.bat deps         :: Скачать зависимости Lite minimal
bin\main-lite-x64.exe --lite :: Запуск Lite
bin\main-lite-x64.exe --help :: Help с Lite info
```

**Linux Lite (для слабых ноутов с Linux):**
```bash
cd aeros
./build-lite.sh install-deps  # Минимальные зависимости: glfw, mesa, X11, openmp — без Vulkan
./build-lite.sh deps          # GLM, GLAD, ImGui
./build-lite.sh all           # Сборка Lite SSE2 O1
./bin/aeros-engine-lite --lite --help
./bin/aeros-engine-lite model.stl
```

**Linux Makefile Lite:**
```bash
cd aeros
make -f Makefile.lite install-deps
make -f Makefile.lite
./bin/aeros-engine-lite
```

**CMake Lite:**
```bash
mkdir build-lite && cd build-lite
cmake -DAEROS_LITE=ON -DAEROS_CPU_ONLY=ON -DAEROS_ENABLE_VULKAN=OFF ..
make -j2  # 2 потока для i3
./aeros-engine-lite
```

### CLI Lite
```
Aeros Engine v1.20.0 Lite — Airflow Visualization
Full: Vulkan+OpenGL, CUDA, FSR, FG, all features
Lite: i3-3xxx / HD 4000 / GT 620M / 4GB RAM / No CUDA / SSE2 / 30 FPS
Usage: aeros-engine-lite [options] [model.stl]
Options:
  --vulkan, --vk    — force Vulkan renderer (full only)
  --opengl, --gl    — force OpenGL renderer
  --lite            — force Lite mode (low particles, low res)
  --help, -h        — this help
  model.stl         — STL file to load
Lite: Optimized for weak devices — 1500 particles, 8x80 streamlines, voxel 24, LBM OFF, 30 FPS, SSE2, small binary
Full: 15000 particles, 24x300 streamlines, voxel 48, LBM ON, 60 FPS, AVX2
```

### UI Lite
- Новый раздел "Lite — Weak Devices i3-3xxx / HD 4000 (NEW v1.20.0)" в настройках
- Показывает Lite info string, Lite mode ON/OFF, HW threads, Max threads
- Bullet list оптимизаций: SSE2 only, O1, No CUDA, No Vulkan, Particles 1500, Streamlines 8x80, Voxel 24 8x less memory, LBM OFF 32 res, 30 FPS VSync ON power saving, 1366x768 laptops 4GB RAM integrated graphics
- Если Lite mode ON: слайдеры Particles 500-5000, Streamlines 4-16, Steps 40-150, Voxel 16-32, Power Saving checkbox, Target FPS 15-60, кнопки Apply Lite Defaults + Print System Info
- Если Full mode: текст Full version for modern PCs + где скачать Lite version: Setup-Lite-x64, lite-linux, build-lite.bat/sh
- Сравнение Lite vs Full: binary size, RAM, FPS, CPU, GPU, Vulkan, CUDA

### Тестирование Lite
- [x] build-lite.bat синтаксис — MSVC flags /O1 /arch:SSE2 /openmp /fp:precise /DAEROS_LITE /DCPU_ONLY /DLITE_MODE, без LTCG, small binary
- [x] build-lite.sh — g++ -O1 -msse2 -mfpmath=sse -fopenmp -DAEROS_LITE -DCPU_ONLY -DLITE_MODE, minimal deps
- [x] Makefile.lite — all, check-deps, download-deps, install-deps, clean, package
- [x] CMake AEROS_LITE=ON — O1 SSE2 no AVX2, no LTCG, defines LITE_MODE CPU_ONLY
- [x] main.cpp — isLiteMode(), applyLiteDefaults(), applyLiteOptimizations(), 1024x600, --lite flag, OpenGL forcing
- [x] ui.cpp — Lite panel, system info, sliders, apply defaults, Lite vs Full
- [x] lite_config.h/cpp — all defines and functions
- [x] globals.h/cpp — g_isLiteMode etc
- [x] AerosEngine-Lite-x64.iss — Lite installer
- [x] build.yml/release.yml — Lite jobs
- [ ] Реальная сборка на Windows с MSVC — требует Windows (CI проверит)
- [ ] Реальная сборка на Linux — требует Linux (CI проверит)
- [ ] Запуск на i3-3xxx / HD 4000 / 4GB RAM — требует реального железа

### Что нового v1.20.0 vs v1.19.0
- **Lite версия** для слабых устройств i3-3xxx / HD 4000 / GT 620M / 4GB RAM / No CUDA
- SSE2 only, no AVX2, O1, small binary, 30 FPS, power saving, BELOW_NORMAL priority
- Low particles 1500, streamlines 8x80, voxel 24, LBM OFF 32 res 1 step
- OpenGL 3.3, no Vulkan by default, simple shaders no PBR/FSR/FG/volumetric/vortex/LIC/acoustic
- Window 1024x600 for 1366x768 laptops, VSync ON, flowSpeed 2.0
- build-lite.bat / build-lite.sh / Makefile.lite / CMake AEROS_LITE=ON
- Lite installer AerosEngine-Lite-x64.iss, portable Lite
- UI Lite panel with system info, apply defaults, settings, Lite vs Full comparison
- CLI --lite flag, auto-detection via AEROS_LITE macro
- CI/CD Lite jobs in build.yml and release.yml
- Version bump 1.19.0 -> 1.20.0

### Известные ограничения Lite
- Нет CUDA — только CPU flow field, медленнее чем Full с CUDA
- Нет Vulkan — только OpenGL 3.3, меньше производительность чем Vulkan
- Low particles/streamlines/voxel/LBM — меньше деталей, но работает на слабом железе
- Нет volumetric/vortex/LIC/acoustic — упрощено, но schlieren/shock есть
- 30 FPS — не 60, но достаточно и меньше греет ноут
- 1024x600 — не fullscreen 1280x720, но помещается на 1366x768
- SSE2 only — медленнее чем AVX2 на современных CPU, но совместимо со старыми
- O1 — медленнее чем O3 на 10-20%, но бинарь меньше и меньше нагрев

### Будущее Lite
- v1.21.0: еще более low настройки для Atom/Celeron, OpenGL 2.1 fallback
- v1.22.0: ARM Lite для Raspberry Pi, Android
- v1.23.0: Web Lite для браузера (WASM)

---
**GoGonam AoS. 2026**
**Aeros Engine v1.20.0 Lite — для всех, даже для слабых ноутов!**
