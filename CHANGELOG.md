# Changelog — Aeros Engine

Все значимые изменения проекта документируются здесь.

Формат основан на [Keep a Changelog](https://keepachangelog.com/ru/1.0.0/),
версии — [Semantic Versioning](https://semver.org/lang/ru/).

## [1.4.0] - 2026-10-05

### Добавлено
- **Расширенный режим теста на ошибки в коде (Code Action Tests)** — 10 новых тестов в дополнение к 9 физике:
  - **OpenGL State**: проверка `glGetError`, `display_w/h`, `maxDim`, валидность VAO/VBO через `glIsVertexArray`, наличие буферов при загруженной модели
  - **Buffer Integrity**: размеры `particlePositions` %3, соответствие `pos/col`, `drawCount <= alloc`, `g_vertices` %9, соответствие `normals`/`colors`, `modelVertexCount`, `voxelData` vs `distanceField`, `streamlineVertexCount` чётность
  - **Model Integrity**: NaN в вершинах/нормалях, вырожденные треугольники (area<1e-12), нулевые/ненормализованные нормали, `minBB <= maxBB`, `center` внутри BB и совпадает с `(min+max)/2`, `maxDim` соответствует `length(max-min)`
  - **FlowParams Sanity**: `cellSize >1e-6`, `min<max` для домена и воксельной сетки, `gridNx*Ny*Nz == gridCellCount`, `radius>0`, `Vinf` vs `flowSpeed`, `time>=0`, `timeScale (0,10]`, `strouhal (0,1]`, `wakeStrength [0,5]`, `wakeLength (0,100]`, `airDensity/P/T/a` в диапазонах
  - **Time & Camera**: `deltaTime (0,0.5]`, `lastFrame>=0`, `cameraPos` finite, `cameraFront` нормализован, `cameraUp` нормализован и ортогонален, `yaw/pitch/fov` в диапазонах, `dist(camera,center)>1e-4`
  - **Memory Safety**: `voxel total <20M`, `g_voxelData` только 0/1, `distanceField` диапазон <10000, память <1GB, `numParticles` [0,1M], OOB тесты `sampleSDFCPU(1e6)`, `computeVelocityField` с zero params не крашится
  - **Division by Zero Risks**: проверка критичных делителей `maxDim`, `Vinf`, `cellSize`, `D`, `airDensity`, `speedOfSound`, `maxSpeedForColor`, `deltaTime`, `maxSpeed`, симуляция `D=0`, `radius=0`, `q~0`
  - **Input & State**: `useCUDA 0/1`, `voxelResolution [8,256]` (рекомендовано [16,128]), `particleSize (0,50]`, `alpha [0,1]`, `azimuth [-360,720]`, `elevation [-90,90]`, противоречивые состояния `showParticles && drawCount==0`, `showModel && VAO==0` и т.д.
  - **Shaders & Resources**: непустые `*ShaderSource`, `VAO` сгенерированы, `glGetError` после операций, подсчёт активных VAO
  - **Error Handling**: `sampleSDFCPU(NaN)`, `computeVelocityField(NaN)`, `atmosphere(NaN)`, `speedToMS(Inf)`, `huge pos 1e10`, `zero vinf`, `long log message` — все в try/catch, не должны крашиться
  - Новые утилиты: `checkGLErrors()`, `getGLErrorString()`, `logTestError()`, `logTestWarn()`, `validateFrameCode()`, `runPhysicsTests()`, `runCodeTests()`
  - UI: разделение на Physics и Code группы с отдельными TreeNode, кнопки Run Physics Only / Run Code Only, отображение `GL Error`, расширенная Frame Validation (buffers, camera, Vinf, dt, SDF mem, VAOs)
  - Глобальные счётчики `codeTestsPassed/Failed`, `lastGLError/Str`

### Исправлено (защита от ошибок в действии кода)
- `voxel_grid.cpp`: `sampleSDFCPU` и `sdfNormalCPU` теперь защищены от деления на ноль (`cellSize<1e-8` → 1000), NaN входа, OOB индексов, NaN в `distanceField`, безопасный `safeGet`
- `flow_field.cpp`: `updateFlowParams()` полностью переписан с защитой от NaN/Inf, clamp всех параметров, гарантия `min<max`, `cellSize>1e-6`; `computeVelocityFieldCPU()` защита NaN входа, `csx` и `k` проверка, `safeWakeLen`, `st` проверка, `D` проверка
- `particles.cpp`: `initParticles()` проверка `numParticles` [100,500k], `min/max` finite, `try/catch bad_alloc` fallback, NaN проверка `r1/r2` и позиций; `updateParticles()` проверка `dt`, `drawCount` vs alloc, `pos/col` size match, `cellSize` защита, NaN частиц респавн, OOB проверка `idx`, `surfDist` finite, `push` finite, `perpFactor` finite, `np` finite, GL error после `BufferSubData`
- `forces.cpp`: `updateVertexColors()` проверка `vertices==normals`, `try/catch resize`, `vinf` finite, `p` finite, `v` finite, `speedRatio` finite, `cp` finite, `fl` finite, `D` finite, `t` finite; `computeLiftDrag()` проверка размеров, `vinf` finite, `flowDir` len, `rho` clamp [0.0001,10], `q` finite, `D` finite, треугольники NaN/len/area проверки, `vel` finite, `cp` finite, `along` finite, `pressureForce` finite, `skinFriction` finite, `totalForce` finite, `dragMagnitude` finite, `liftDir` len, `centerOfPressure` finite

## [1.3.0] - 2026-10-05

### Добавлено
- **Режим теста для проверки ошибок вычислений** — новый модуль `test_mode.h/cpp`:
  - 9 автоматических тестов: Speed Conversion, ISA Atmosphere, SDF Sampling, Velocity Field, Pressure Calculation, Particle System, Force Calculation, Voxel Grid, NaN Checks
  - Проверка конвертации скорости round-trip для всех единиц (м/с, км/ч, mph, узлы, ft/s) с допуском 0.01
  - Проверка ISA модели по референсным значениям на 0,1,2,5,10,15,20км с допуском 5% по плотности/давлению, 2K по температуре, монотонность убывания плотности
  - Проверка SDF: NaN/Inf, дальняя точка положительна, нормаль нормализована, 100 точек без NaN
  - Проверка поля скоростей: 200 точек домена на NaN, скорость не >5*Vinf (предупреждение) и не >10*Vinf (fail), проверка непротекания у поверхности, внутри объекта скорость <0.5*Vinf
  - Проверка давления: Cp в [-3.5,1.5], цвета в [0,1], NaN
  - Проверка частиц: NaN позиций, выход за границы (margin 3*maxDim), валидность цветов
  - Проверка сил: lift/drag не NaN, не астрономические (>maxDim²*1000)
  - Проверка воксельной сетки: размеры, соответствие размеров полей, NaN, диапазон, границы
  - Проверка глобальных переменных на NaN/Inf
  - Логирование в консоль и в UI, время выполнения, счётчики passed/failed
  - UI секция **Test Mode (Physics Validation)**: Enable Test Mode, Continuous Validation, Run All Tests, Clear Log, отображение результатов PASS/FAIL с цветами, лог в скроллируемом child, быстрая диагностика кадра (NaN, Vinf, FlowParams, SDF range)
  - Валидация каждый кадр при `testContinuous`: проверка flowSpeed/altitude/airDensity/lift/drag на NaN, первые 100 частиц на NaN
- Интеграция в главный цикл `main.cpp`: вызов `validateFrame()` при включённом continuous mode

### Улучшено
- `build.bat`, `build-cpu.bat`, `CMakeLists.txt`: добавлен `test_mode.cpp`

## [1.2.1] - 2026-10-05

### Исправлено
- **Критичный баг в `particles.cpp`**: `surfDist` хранился в вокселях, а сравнивался с `1.5*cellSizeX` в мировых единицах; теперь `rawDist*cellSizeX` и порог `1.5f` в вокселях — унифицировано с `cuda_stub.cpp`
- `atmosphere.cpp`: убрана неиспользуемая `T32`, `RHO0`; исправлен градиент 20-32км с `-0.001` на `+0.001` (температура должна расти), давление теперь считается правильно
- `flow_field.cpp` и `kernel.cu`: замена `powf(x,2)` на `x*x` для избежания NaN при отрицательном основании
- `kernel.cu`: удалён мёртвый код `voxelQuery`, `voxelNormal` и неиспользуемые глобальные `d_voxNx`, `d_voxMinX`, `d_cellX`
- `gl_utils.cpp`: `createObstacleSphere` теперь проверяет `if (VAO==0)` перед `glGen*`, исправлена утечка VAO/VBO
- `model.cpp`: добавлен `#include <cfloat>` для `FLT_MAX`
- `ui.cpp`: добавлен `#include <cmath>`, исправлен `InputFloat` формат с `%.2f %s` на `%.2f` + отдельный `Text(unit)`, убраны неиспользуемые буферы
- `main.cpp`: добавлен `#include \"atmosphere.h\"`, учёт изменения `altitude` в dirty-проверке линий тока
- `installer/*.iss`: обновлён комментарий `/DAppVersion` с 1.0.0 на 1.2.1, `common.iss` теперь `ifndef OutputBaseName`
- `tools/build-all.ps1`: добавлен `arm64` билд и `-Arch all` для портативок/установщиков
- `README.md` и `installer/README.md`: обновлены с учётом ARM64, единиц скорости и атмосферы

### Улучшено
- `forces.cpp`: теперь использует `flowParams.airDensity` вместо глобальной переменной, добавлена защита `rho>0.0001`
- Общий аудит всех файлов на предмет утечек, неиспользуемых переменных и смешения единиц

## [1.2.0] - 2026-10-04

### Добавлено
- **Единицы скорости**: переключение между м/с, км/ч, миль/ч (mph), узлами (kts), фут/с (ft/s) — слайдер и ввод в выбранных единицах, автоматическая конвертация и отображение во всех единицах
- **Плотность воздуха на разных высотах (ISA модель)**: 
  - Новый модуль `atmosphere.h/cpp` с расчётом по Международной стандартной атмосфере
  - Высота 0–20000 м с пресетами Sea Level / 5km / 10km / 15km
  - Расчёт температуры, давления, плотности, скорости звука, числа Маха
  - Таблица плотности по высотам (0,1,2,3,5,8,10,12,15,20 км)
  - Динамическое давление q = 0.5*rho*v²
- **Учёт плотности в силах**: Lift/Drag теперь масштабируются по `rho`, добавлен `Cd/Cl` (коэффициенты), чекбокс `Use Real Air Density`
- Новый UI раздел **Atmosphere (ISA)** с отображением rho, P, T, a, Mach, q
- `FlowParams` расширен полями `altitude`, `airDensity`, `airPressure`, `airTemperature`, `speedOfSound`

### Улучшено
- `forces.cpp`: силы теперь `F = -Cp * q * n * area` с вязким трением `q`-зависимым
- `flow_field.cpp`: `updateFlowParams()` теперь вызывает `updateAtmosphereParams()`
- `CMakeLists.txt`, `build.bat`, `build-cpu.bat`: добавлен `atmosphere.cpp`

## [1.1.1] - 2026-10-04

### Исправлено
- Исправлена ошибка версионирования установщиков: `AppVersion` в ISS файлах теперь `#ifndef` чтобы `/DAppVersion` из CI работал, файлы теперь корректно именуются `v1.1.1`
- Обновлён fallback версии в `common.iss` и всех ISS файлах с `1.0.0` на `1.1.1`

## [1.1.0] - 2026-10-04

### Исправлено
- **Физика значительно улучшена**: исправлена ошибка в `cuda_stub.cpp` — CPU fallback теперь полностью совпадает с CPU версией (давление, частицы, цвета)
- Исправлена нормализация Cp в `computeVertexPressureCUDA`: было `(cp+3)/4`, стало `(cp+1)/2` с картой цветов как в `forces.cpp`
- Исправлен `updateParticlesCUDA`: добавлена корректная обработка SDF в мировых единицах, `colorForPoint`, коллизии и рестарт частиц
- Исправлена логика `useCUDA` — в CPU-only сборке теперь по умолчанию CPU бэкенд
- Исправлен синтаксис ISS файлов (DestName/Flags порядок, `skipifsourcedoesntexist`)

### Улучшено
- **Поле скоростей `computeVelocityFieldCPU` и `baseFlowSDF/wakeField` в `kernel.cu`**: 
  - Более точное условие непротекания (только если `vn < 0`)
  - Пограничный слой с вязкостью и тангенциальное ускорение по профилю Гаусса (эффект Бернулли)
  - Вихревой след: дефицит скорости, растущая ширина следа, попеременные вихри Кармана, физичная турбулентность по компонентам
- **Lift/Drag**: добавлен учёт разрежения за кормой, вязкое сопротивление (skin friction), более стабильное ограничение Cp
- Добавлена поддержка **ARM64**: `arm64` архитектура в `build.bat`, `build-cpu.bat`, сборка GLFW из исходников, установщики и портативные версии для ARM64
- Workflow релиза теперь собирает **9 артефактов**: Setup x64/x86/arm64, Portable x64/x86/arm64, Update x64/x86/arm64 + SHA256SUMS

### Добавлено
- `tools/build-glfw-arm64.ps1` — сборка GLFW для ARM64/x64/x86 из исходников
- `installer/AerosEngine-arm64.iss`, `AerosEngine-Updater-arm64.iss`
- Поддержка `arm64` в `common.iss` и сборочных скриптах

## [1.0.0] - 2026-10-03

### Добавлено
- Система релизов: установщики x64/x86, портативные версии, апдейтеры
- Inno Setup скрипты для всех типов установщиков (`installer/`)
- PowerShell/BAT скрипты для сборки (`installer/build-*.ps1`, `tools/build-all.ps1`)
- GitHub Actions workflow для автоматического релиза (`release.yml`)
- Версионирование через `VERSION` файл и `aeros/src/version.h`
- Поддержка сборки x86 (32-bit) и x64 (64-bit) через `build.bat x86|x64`
- Автоопределение Visual Studio через `vswhere`
- Поддержка нескольких CUDA архитектур (sm_75, sm_86, sm_89)
- Скрипт для скачивания GLFW x86 (`tools/get-glfw-x86.ps1`)
- Скрипт для обновления версии (`tools/version-bump.ps1`)

### Улучшено
- `build.bat` теперь универсальный, поддерживает аргументы и автоопределение VS
- `CMakeLists.txt` поддерживает x86/x64, версию из файла, CPack для портативных ZIP
- `.gitignore` обновлён для релизной системы

### Инфраструктура
- 4 установщика: Setup x64, Setup x86, Update x64, Update x86
- 2 портативные версии: Portable x64, Portable x86
- Автоматическое создание SHA256 checksums
- GitHub Release с описанием на русском и английском

## [0.9.0] - Предыдущие версии
- Базовый функционал: загрузка STL, вокселизация, SDF, частицы, линии тока
- CUDA и CPU бэкенды
- ImGui UI
- Визуализация давления, lift/drag
