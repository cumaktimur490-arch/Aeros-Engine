# Aeros Engine Lite Guide v1.20.1 — для слабых устройств

## Для кого Lite

### Potato (Ultra-Lite) — самые слабые
- **CPU:** Intel Atom N270/N450/N455, Celeron N2840/N3050, AMD E1/E2, 1-2 ядра, 1.6GHz, нет SSE4.2
- **GPU:** Intel GMA 3150, HD 3000, HD 2000, 4-6 EUs, OpenGL 2.1-3.0, shared 64MB
- **RAM:** 1-2GB, HDD 5400rpm
- **Экран:** 1024x600 нетбук
- **Настройки:** 500 частиц, 4x40 линий, voxel 16, LBM OFF 16 res, 20 FPS, 800x450, 1 поток, <256 MB RAM, ~3-5 MB binary

### Low (Lite) — слабые ноутбуки
- **CPU:** i3-3xxx Ivy Bridge 2C/4T SSE4.2, Pentium G2020, AMD A4/A6, 2.4GHz
- **GPU:** Intel HD 4000 16 EUs OpenGL 4.0, GT 620M/720M 96 cores 1GB, AMD HD 7000
- **RAM:** 4GB, HDD
- **Экран:** 1366x768
- **Настройки:** 1500 частиц, 8x80 линий, voxel 24, LBM OFF 32 res, 30 FPS, 1024x600, 2 потока, <512 MB, ~5-10 MB, SSE2, No CUDA, No Vulkan

### Medium — средние
- **CPU:** i5-4xxx 4C, AMD A8/A10
- **GPU:** HD 4600, GT 740M/840M, R5/R7
- **RAM:** 8GB
- **Настройки:** 5000 частиц, 16x150 линий, voxel 32, LBM OFF 48 res, 45 FPS, 1280x720

### Full — современные
- **CPU:** i5-8400+ 6C AVX2, Ryzen 5
- **GPU:** GTX 1060+, RTX, RX 580+
- **RAM:** 8GB+
- **Настройки:** 15000 частиц, 24x300 линий, voxel 48, LBM ON 128 res, 60 FPS, Vulkan 1.3 + CUDA

## Авто-детект

Launcher автоматически определяет железо:
- `launcher.bat` (Windows) или `launcher.sh` (Linux)
- Проверяет CPU threads, RAM, батарею, GPU
- Выбирает Potato/Lite/Medium/Full
- На батарее — автоматически Lite для экономии

Ручная проверка:
- `check-hardware.bat` / `check-hardware.sh`

## CLI

```
--lite                    # Lite mode
--ultra-lite, --potato    # Ultra-Lite Potato
--preset potato|low|medium|full
--auto-lite               # авто-детект слабого железа (вкл по умолчанию)
--no-auto-quality         # выкл динамическое качество по FPS
--battery-saver           # форсировать экономию батареи 30 FPS
--help
```

## Динамическое качество

Если FPS падает ниже 60% от цели:
- Снижает частицы 20%
- Снижает линии тока
- Снижает шаги

Включается `g_autoQualityScaling` (по умолчанию ON). Выключить: `--no-auto-quality` или в UI.

## Батарея

Авто-детект:
- Windows: `GetSystemPowerStatus` — если на батарее, включает saver
- Linux: `/sys/class/power_supply/BAT0/status` — Discharging = батарея

При батарее:
- Приоритет IDLE (вместо BELOW_NORMAL)
- 30 FPS лимит
- Можно форсировать: `--battery-saver`

## Конфиг файл

`aeros-lite.ini` — сохраняется/загружается:
```
preset=1 # 0=Potato 1=Low 2=Medium 3=Full
isLite=1
particles=1500
streamlines=8
...
autoQualityScaling=1
batterySaver=1
```

UI: Save/Load Lite Config кнопки.

## Бенчмарк

Встроенный бенчмарк оценивает FPS по железу и настройкам:
- `runAllBenchmarks()` — тестирует все пресеты
- `getBenchmarkRecommendation()` — рекомендует лучший проходящий пресет
- UI: можно добавить кнопку Benchmark

## Сборка

### Windows Lite
```cmd
build-lite.bat x64        # Lite SSE2 O1 2 threads
build-lite.bat x86        # Lite x86 для старых ноутов
build-lite.bat ultra      # Ultra-Lite Potato 1 thread
build-lite.bat deps       # зависимости
```

### Windows Ultra-Lite
```cmd
build-ultra-lite.bat x64  # Potato 500 particles 4x40 voxel16 20 FPS
```

### Linux
```bash
./build-lite.sh all       # Lite
./build-ultra-lite.sh all # Ultra-Lite Potato
./build-linux.sh all      # Full
```

### Makefile
```bash
make -f Makefile.lite all
make -f Makefile.linux all
```

### CMake
```bash
cmake -B build -DAEROS_LITE=ON
cmake --build build
cmake -B build-ultra -DAEROS_LITE=ON -DULTRA_LITE=ON
```

## Установщик

- `AerosEngine-Lite-x64.iss` — Lite установщик Win7+ x64compatible, minimal deps
- Portable Lite — `build-portable.ps1 -Lite`

## UI

Новый раздел "Lite — Weak Devices i3-3xxx / HD 4000 / Ultra-Lite (NEW v1.20.1)":
- Инфо: Lite Mode, HW Threads, Max Threads, Battery, Low RAM, Detected, Current Preset, VRAM/RAM est, FPS avg
- Пресеты: кнопки Potato/Low/Medium/Full + описания
- Оптимизации: bullet list
- Settings: Particles, Streamlines, Steps, Voxel, Power Saving, Battery Saver, Auto Quality Scaling, Target FPS, Max FPS
- Кнопки: Apply Lite, Apply Ultra-Lite, Print System Info, Save/Load Config, Auto-Detect Hardware
- Сравнение Lite vs Full vs Potato

## Troubleshooting для слабых устройств

### Черный экран / не запускается
- Проверьте OpenGL: `check-hardware.bat` — нужен OpenGL 3.3+
- Попробуйте Potato: `--preset potato`
- Обновите драйвер Intel HD: https://www.intel.com/content/www/us/en/download-center/home.html
- Для HD 3000 — нужен драйвер 9.17.10.4459+ с OpenGL 3.1+

### Низкий FPS <15
- Включите Potato: `--preset potato`
- Уменьшите частицы: в UI Slider 100-500
- Выключите LBM: LBM OFF
- Закройте браузер и другие программы — 4GB RAM мало
- Подключите питание — на батарее FPS ниже
- Включите Auto Quality Scaling — сам снизит качество

### Вылетает / Out of memory
- 2GB RAM — только Potato mode, закройте всё
- 4GB RAM — Lite mode, не Full
- Уменьшите voxel до 16
- Выключите volumetric, vortex, LIC, acoustic

### Греется ноутбук
- Lite включает BELOW_NORMAL приоритет и VSync ON — меньше нагрев
- Battery saver — IDLE приоритет
- Ограничьте FPS до 20-30
- Почистите от пыли, подставка с охлаждением

### На батарее быстро садится
- Включите Battery Saver: `--battery-saver`
- Используйте Lite или Potato
- Уменьшите яркость экрана
- Закройте другие программы

### Intel HD 4000 — артефакты
- Обновите драйвер
- Попробуйте OpenGL 3.3 (по умолчанию в Lite)
- Выключите MSAA (0 в Lite)
- Упрощенные шейдеры — уже в Lite

### GT 620M — не использует GPU
- В панели NVIDIA: выберите высокопроизводительный процессор для aeros-engine
- Обновите драйвер NVIDIA
- Проверьте что не на Intel HD — в диспетчере задач GPU

## Сравнение производительности

| Устройство | Пресет | FPS | RAM | VRAM | Бинарь |
|------------|--------|-----|-----|------|--------|
| Atom N450 1C 2GB GMA 3150 | Potato | 15-20 | 200 MB | 50 MB | 3 MB |
| Celeron N2840 2C 2GB HD Graphics | Potato | 18-22 | 250 MB | 80 MB | 3 MB |
| i3-3220 2C/4T 4GB HD 4000 | Low | 25-35 | 400 MB | 150 MB | 8 MB |
| i3-3217U 2C/4T 4GB HD 4000 ноут | Low | 20-30 | 400 MB | 150 MB | 8 MB |
| i5-4200U 2C/4T 8GB HD 4400 | Medium | 35-50 | 600 MB | 250 MB | 15 MB |
| i5-8400 6C 8GB GTX 1060 | Full | 55-60 | 1.2 GB | 500 MB | 25 MB |

## Будущее

- v1.21.0: OpenGL 2.1 fallback для GMA 3150, еще более low для Atom
- v1.22.0: ARM Lite для Raspberry Pi, Android
- v1.23.0: Web Lite WASM для браузера
- v1.24.0: Vulkan 1.0 для HD 4000, оптимизация под UHD 600

---
GoGonam AoS. 2026 — для всех, даже для самых слабых!
