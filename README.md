# AeroS Engine

Интерактивная визуализация обтекания 3D-модели (STL) потоком воздуха: линии тока, частицы,
распределение давления по поверхности, подъёмная сила и сопротивление. C++17 / OpenGL 3.3 /
CUDA / Dear ImGui / GLM.

> v1.7.0 Realistic Aero: фотореалистичная аэродинамика как на фото — Pressure rainbow (NASCAR), U Magnitude (UAV/Cybertruck), velocity-colored streamlines (car underbody), vorticity/Q/TKE, ground effect, flow separation, Cd/Cl с ref area, Zou/He BC, convective outlet, inlet turbulence, полная пересборка при обновлении (rebuild-all.bat), 27 тестов; v1.6.0 Optimized OpenMP+AVX2; v1.5.0 LBM D3Q19.

## 📥 Скачать (релиз)

Последний релиз: **[GitHub Releases](https://github.com/cumaktimur490-arch/Aeros-Engine/releases)**

| Файл | Описание | Система |
|------|----------|---------|
| `Aeros-Engine-Setup-x64-v*.exe` | Установщик (рекомендуется) | Windows 10+ 64-bit Intel/AMD |
| `Aeros-Engine-Setup-x86-v*.exe` | Установщик | Windows 10+ 32-bit |
| `Aeros-Engine-Setup-arm64-v*.exe` | Установщик | Windows 10+ ARM64 (Snapdragon, Surface) |
| `Aeros-Engine-Portable-x64-v*.zip` | Портативная (без установки) | 64-bit |
| `Aeros-Engine-Portable-x86-v*.zip` | Портативная | 32-bit |
| `Aeros-Engine-Portable-arm64-v*.zip` | Портативная | ARM64 |
| `Aeros-Engine-Update-x64-v*.exe` | Обновление | Для уже установленной x64 |
| `Aeros-Engine-Update-x86-v*.exe` | Обновление | Для уже установленной x86 |
| `Aeros-Engine-Update-arm64-v*.exe` | Обновление | Для уже установленной ARM64 |

**Быстрый старт:**
1. Скачайте `Aeros-Engine-Setup-x64` для современного ПК (x86 для 32-bit, arm64 для ARM)
2. Запустите установщик
3. Откройте через ярлык на рабочем столе
4. Выберите STL модель при запуске (примеры в папке `models`)

**Портативная версия:** распакуйте ZIP и запустите `AerosEngine.exe`

**Обновление:** если уже установлена старая версия — скачайте `Update` и запустите.

## Возможности

- Загрузка STL (бинарный и ASCII), встроенный файловый диалог;
- Вокселизация модели и поле расстояний (SDF) — основа столкновений и обтекания;
- Частицы (до 200 000) и линии тока с окраской по скорости/близости к поверхности;
- Раскраска модели по давлению (по Бернулли с разрежением за кормой), векторы lift/drag и центр давления;
- Два бэкенда вычислений: CUDA (GPU) и CPU-фолбэк — переключаются на лету, физика унифицирована;
- **Единицы скорости**: м/с, км/ч, миль/ч, узлы, фут/с — переключение в UI с автоконвертацией;
- **ISA атмосфера**: высота 0-20км, плотность, давление, температура, скорость звука, число Маха, динамическое давление q; плотность влияет на lift/drag;
- Управление параметрами потока: скорость, азимут, элевация, масштаб времени, число Струхаля, вихревой след, высота;
- Поддержка x64, x86, ARM64.

## Сборка

Требуется: Windows, Visual Studio 2022 (C++ рабочая нагрузка), CUDA Toolkit.

### Вариант 1 — build.bat (основной)

```bat
cd aeros
build.bat         — x64 (по умолчанию)
build.bat x64     — 64-bit
build.bat x86     — 32-bit
bin\main.exe
```

`build.bat` теперь автоопределяет Visual Studio через `vswhere` и поддерживает x64/x86.
Библиотеки берутся из `libs/` (в репозитории).

Для x86 сборки нужен 32-bit GLFW:
```powershell
.\tools\get-glfw-x86.ps1
```

### Вариант 2 — CMake

```bat
cd aeros
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release

cmake -B build32 -G "Visual Studio 17 2022" -A Win32
cmake --build build32 --config Release
```

### Вариант 3 — Полный релиз (установщики + портативные)

Требует Inno Setup 6: https://jrsoftware.org/isinfo.php

```bat
cd tools
build-all.bat
```

Результат в `release/`:
- Setup x64/x86
- Portable x64/x86 ZIP
- Update x64/x86

Подробнее: `installer/README.md`

### Автоматический релиз (GitHub Actions)

При пуше тега `v*`:

```bash
git tag v1.0.0
git push origin v1.0.0
```

Автоматически соберутся все версии и создастся GitHub Release.

Ручной запуск: Actions → Release Build → Run workflow

## Управление

| Клавиши | Действие |
|---|---|
| `W A S D` | полёт камеры |
| `Space` / `Shift` | вверх / вниз |
| `Ctrl` | ускорение ×5 |
| **Средняя кнопка мыши** | вращение камеры |
| **Колесо** | зум (FOV) |
| `Esc` | выход |

## Структура проекта

```
aeros/
├── build.bat              # сборка x64/x86 (nvcc + MSVC, автоопределение VS)
├── build-x64.bat          # wrapper для x64
├── build-x86.bat          # wrapper для x86
├── bin/                   # бинарь, DLL и примеры моделей
├── src/
│   ├── main.cpp           # точка входа: инициализация, главный цикл, рендер
│   ├── version.h          # версия приложения (из VERSION)
│   ├── globals.h/.cpp     # всё общее состояние приложения
│   ├── shaders.h          # GLSL-шейдеры (sources)
│   ├── gl_utils.h/.cpp    # компиляция шейдеров (+проверка ошибок), bbox/оси/эллипсоид
│   ├── input.h/.cpp       # клавиатура/мышь/камера
│   ├── ui.h/.cpp          # панель ImGui
│   ├── stl_loader.h/.cpp  # загрузка STL + диалог выбора файла
│   ├── model.h/.cpp       # загрузка модели и подготовка сцены
│   ├── voxel_grid.h/.cpp  # вокселизация + SDF (CPU сэмплинг)
│   ├── flow_field.h/.cpp  # CPU-поле скоростей, сборка FlowParams
│   ├── particles.h/.cpp   # частицы (CUDA/CPU)
│   ├── streamlines.h/.cpp # линии тока
│   ├── forces.h/.cpp      # давление, lift/drag
│   ├── cuda_api.h         # интерфейс CUDA-бэкенда
│   ├── kernel.cu          # CUDA-реализация физики
│   └── flow_params.h      # структура параметров потока (общая CPU/GPU)
├── CMakeLists.txt         # CMake сборка (x64/x86, CPack)
└── .vscode/tasks.json     # задача сборки для VS Code

libs/                      # GLFW, glad, GLM (в репозитории)
installer/                 # Скрипты установщиков
├── AerosEngine-x64.iss    # Полный установщик x64 (Inno Setup)
├── AerosEngine-x86.iss    # Полный установщик x86
├── AerosEngine-Updater-x64.iss # Апдейтер x64
├── AerosEngine-Updater-x86.iss # Апдейтер x86
├── build-installers.ps1/.bat   # Сборка установщиков
├── build-portable.ps1/.bat     # Сборка портативных
└── common.iss             # Общие определения

tools/                     # Инструменты релиза
├── build-all.ps1/.bat     # Полная сборка релиза
├── get-glfw-x86.ps1       # Скачать GLFW x86
└── version-bump.ps1       # Обновить версию

.github/workflows/
├── release.yml            # Авто-релиз при теге v*
└── build.yml              # Проверка сборки на PR

release/                   # Собранные релизы (генерируется)
```

`libs/` — GLFW, glad, GLM (в репозитории, сборка работает из свежего клона).

## Как это работает

1. **Загрузка**: STL читается, строится bounding box.
2. **Вокселизация**: модель заполняется воксельной сеткой (ray casting), BFS от поверхности
   строит знаковое поле расстояний (SDF), которое загружается в GPU.
3. **Поле скоростей**: набегающий поток + отталкивание нормалью SDF у поверхности +
   вихревая дорожка Кармана (частота из числа Струхаля). Есть реализация и на CUDA, и на CPU.
4. **Визуализация**: частицы адвектируются по полю, линии тока интегрируются от входного
   сечения, давление на поверхности оценивается по скорости (Бернулли), lift/drag —
   интегрированием давления по треугольникам.

## Системные требования

- **Для запуска:** Windows 10+, GPU с OpenGL 3.3+, 4GB RAM
- **Для CUDA ускорения:** NVIDIA GPU + CUDA Toolkit (опционально, есть CPU fallback)
- **Для сборки:** Visual Studio 2022, CUDA Toolkit 11+, CMake 3.18+ (опционально), Inno Setup 6 (для установщиков)

## Лицензия

GPL-3.0 — см. LICENSE
