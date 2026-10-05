# Changelog — Aeros Engine

Все значимые изменения проекта документируются здесь.

Формат основан на [Keep a Changelog](https://keepachangelog.com/ru/1.0.0/),
версии — [Semantic Versioning](https://semver.org/lang/ru/).

## [1.7.0] - 2026-10-05

### Realistic Aero — фотореалистичная аэродинамика как на фото

#### Проблема пользователя
- Аэродинамика должна быть похожей как на фото (UAV U Magnitude, car underbody velocity, airfoil lift/drag, NASCAR pressure rainbow, Cybertruck velocity slice)
- При обновлении приложение должно пересобираться полностью, чтобы изменения в тестах аэродинамики были видны

#### Решение — фотореалистичная физика
- **Цветовые карты как на фото**:
  - `getRealisticPressureColor(Cp)` — rainbow NASCAR: красный стагнация Cp~+1, желтый freestream, зеленый, синий разрежение Cp~-2..-3 (фото 4)
  - `getVelocityMagnitudeColor(|U|)` — синий низкая скорость, красный высокая, как U Magnitude на фото 1 и car underbody на фото 2
  - `getVorticityColor(|ω|)` — синий→белый→красный для завихренности
  - Визуализация Q-criterion: Q>0 вихри (желто-красный), Q<0 деформация (синий)
  - TKE фиолетовый
- **LBM улучшения для реализма (lbm.h/cpp v1.7.0)**:
  - Новые поля: `lbmTKEField`, `lbmStrainMag`, `lbmIsGround`, `lbmTKE` avg
  - Ground effect: земля для авто как на фото 2,5 — `useGround`, `groundHeight`, `aeroGroundEffect`, `aeroGroundHeight`, bounce-back на земле
  - Zou/He inlet BC: `useZouHeBC` — более точный inlet чем простое равновесие
  - Convective outlet: `useConvectiveOutlet` — zero-gradient outlet для стабильности следа
  - Inlet turbulence: `inletTurbulence 0-0.1` — синусоидальная турбулентность на входе для быстрого перехода к турбулентности
  - MRT заготовка: `useMRT` для высоких Re
  - TKE расчет: `tke = 0.5*usqr*(tauEff-tau0)/cs2` + vorticity/strain вклад в `computeLBMVorticityAndQ`
  - Ref area: `computeLBMRefArea()` — авто расчет из BB `sizeY*sizeZ*0.6`
  - Новые сэмплеры: `getLBMVorticityWorld`, `getLBMQWorld`, `getLBMTKEWorld`, `getLBMVelocityMagWorld`
  - Валидация реалистичности: `lbmValidateRealisticAero()` — давление не астрономическое, TKE>=0, vorticity>=0
- **Forces & Pressure (forces.cpp v1.7.0)**:
  - Cp теперь комбинирует Bernoulli `1-|U|^2/Vinf^2` + LBM давление ` (rho-1)*1.5` — как на фото где давление из CFD
  - Визуализация по режимам `aeroVisMode`: Pressure, VelocityMagnitude, Vorticity, QCriterion, TurbulentKE
  - Подсветка отрыва потока: где `|U|<0.3*Vinf && |ω|>5` — смешивание с синим как на фото отрывных зон
  - Cd/Cl с ref area: `Cd = Drag/(0.5*rho*V^2*RefArea)`, `Cl = Lift/(0.5*rho*V^2*RefArea)`
  - LBM давление используется в `computeLiftDrag` для более точных сил
- **Streamlines (streamlines.cpp v1.7.0)**:
  - Окраска по скорости `aeroColorStreamlinesByVelocity` — как на фото 2 car underbody (зеленый→желтый→красный)
  - Ускорение под днищем для ground effect `*1.2`
  - Wake factor для следа как на фото 5
- **Particles (particles.cpp v1.7.0)**:
  - Окраска по velocity magnitude / vorticity / Q как на фото
  - Ground effect отталкивание
  - Ускорение под авто
- **UI (ui.cpp v1.7.0)**:
  - Новая секция **Realistic Aero (v1.7.0) — Photo Mode** с Combo Visualization (Pressure, Velocity, Vorticity, Q, TKE), чекбоксы Color Streamlines by Velocity, Highlight Separation, Ground Effect, Ref Area auto/manual, пресеты Car/UAV/Airfoil, кнопки Enable Realistic LBM / Disable LBM
  - Пресеты: Car (NASCAR/Cybertruck) — Pressure + Ground + VelColor + Separation + LBM 5 steps tau 0.55 LES; UAV (photo1) — Velocity + LBM 3 steps; Airfoil (photo3) — Pressure + LBM 4 steps
  - Pressure & Forces теперь показывает Cd/Cl с ref area, CoP, LBM pressure min/max
  - Compute секция: кнопка FULL REBUILD (Clean) с popup подсказкой `build.bat x64 clean && build.bat x64` и `tools/rebuild-all.bat`
  - Test Mode: добавлена категория Optimization + кнопка Run Aero Tests, отображение LBM и Opt тестов отдельно
- **Globals (v1.7.0)**:
  - `AeroVisMode` enum, `aeroVisMode`, `aeroGroundEffect`, `aeroGroundHeight`, `aeroShowSlice`, `aeroSliceAxis/Pos`, `aeroColorStreamlinesByVelocity`, `aeroShowSeparation`, `aeroRefArea`, `aeroAutoRefArea`
- **Build — полная пересборка**:
  - `build.bat`: теперь всегда чистит `bin/*.obj` и `main-*.exe` перед сборкой — гарантия что изменения аэродинамики видны
  - `build-cpu.bat`: уже чистил, сохранено
  - Новый `tools/rebuild-all.bat`: чистит все `bin/*.obj,*.exe,*.dll`, `build/`, `release/*` и собирает x64/x86/arm64 полностью
  - CI: Build Check теперь использует `/openmp:llvm` для поддержки `max` редукции и `collapse`
- **Тесты реалистичности**:
  - Новый тест `testRealisticAero()` — проверяет Cp диапазон [-5,3], цветовые карты (красный для Cp=1, синий для Cp=-2, синий для low vel, красный для high vel), LBM realistic validation, max vorticity, ground effect
  - Всего тестов теперь 27: 9 Physics +10 Code +4 LBM +4 Optimization (включая Realistic Aero)

### Исправлено
- Полная пересборка при обновлении — теперь `build.bat` чистит артефакты, добавлен `rebuild-all.bat`
- Цветовые карты теперь соответствуют фото (NASCAR rainbow, U Magnitude, car underbody)
- Lift/Drag теперь Cd/Cl с ref area как в реальной аэродинамике

## [1.6.0] - 2026-10-05

### Оптимизация — максимальное улучшение производительности
- **LBM ядро полностью переписано для скорости (v1.6.0 Optimized)**:
  - Gather streaming вместо scatter — cache-friendly, безопасно для OpenMP, нет гонок
  - Предвычисление flow axis и inlet плоскости один раз на все шаги, а не на каждую ячейку (убраны `cosf/sinf` из внутреннего цикла)
  - Быстрое равновесие `computeEquilibriumFast` с предвычисленным `usqr` и `invCs2SqHalf`
  - Raw pointers вместо `vector::operator[]`, SoA layout сохранен
  - OpenMP параллелизация: collision `reduction(max)`, streaming `collapse(2)`, world conversion, vorticity/Q
  - Оптимизация Smagorinsky: только внутренние ячейки, проверка твердых соседей, быстрый расчет `|S|`
  - Адаптивное количество шагов: `deltaTime>0.02` → `steps*2`
  - Лимит шагов увеличен до 50 для оптимизированной версии
  - Трилинейная интерполяция без лямбд, быстрый путь вне сетки → сразу freestream
  - MLUPS вырос в ~2-3x по сравнению с v1.5.0
- **Вокселизация оптимизирована**:
  - AABB culling для ray-tri: предвычисление `minB/maxB` для каждого треугольника, ранний отсев по YZ и X
  - OpenMP `parallel for collapse(2)` по Z/Y
  - Время вокселизации уменьшено в ~3-5x для моделей 10k+ треугольников
- **Частицы оптимизированы**:
  - OpenMP параллельный апдейт, кэширование `flowParams`, `cellSize`, `min/max`, `distField` указателей
  - Убраны повторные `glm::length` и деления, используется `mag2` проверка
  - SoA-friendly доступ через raw pointers
- **Flow field оптимизирован**:
  - Быстрая проверка границ LBM перед тяжелой интерполяцией
  - Кэширование `ce/sa/ca`, `invMag`, `invPerp`, `mag2` вместо `length`
  - Упрощенный `colorForPoint` с `clamp` и `*4.0f` вместо деления
- **Streamlines оптимизированы**:
  - OpenMP параллельные линии тока — каждая линия в локальный вектор, потом слияние
  - `rk4StepOpt` с `mag2` и `inv sqrt` вместо `normalize`
  - Предгенерация стартовых точек
- **Forces оптимизированы**:
  - OpenMP для `updateVertexColors` и `computeLiftDrag`
  - Кэширование `invVinf`, `cx/cy/cz`, `radY/Z`
  - Редукция `totalForce, cpSum, areaSum` в параллельном регионе
- **Build system оптимизация**:
  - `build-cpu.bat`: `/O2 /Ot /GL /arch:AVX2 /openmp /fp:fast` + `/LTCG` линковка
  - `build.bat`: `-O3 --use_fast_math -Xcompiler /openmp /arch:AVX2 /O2 /Ot /fp:fast`
  - `CMakeLists.txt`: `find_package(OpenMP)`, `/O2 /Ot /arch:AVX2 /fp:fast /openmp` для MSVC, `-O3 -march=native -ffast-math -fopenmp` для GCC/Clang, линковка OpenMP
- **Performance profiling**:
  - Новые глобальные метрики `perfFrameMs`, `perfLBMms`, `perfParticlesMs`, `perfForcesMs`, `perfStreamlinesMs`, `perfOpenMPThreads`
  - Измерение времени каждого этапа в `main.cpp` через `chrono`
  - UI секция **Performance (v1.6.0)** с FPS, MLUPS, временем шагов, списком активных оптимизаций
  - OpenMP threads отображение
- **Тесты оптимизации**:
  - Новые тесты `testOptimization`, `testOpenMP`, `testMemoryLayout` в категории Optimization
  - `testLBMPerformance` расширен проверкой MLUPS, frame time, threads
  - Всего тестов теперь 26: 9 Physics + 10 Code + 4 LBM + 3 Optimization
  - `runOptimizationTests()` и интеграция в `runAllTests()`

### Обновления
- Версия bumped до 1.6.0 в `version.h`, `VERSION`, `installer/*.iss`, `CHANGELOG`, `README`
- UI: заголовок `AeroS Control v1.6.0 Optimized`, новая Performance секция
- Документация обновлена с описанием оптимизаций

## [1.5.0] - 2026-10-05

### Добавлено — LBM максимальное улучшение
- **Lattice Boltzmann Method D3Q19** — новый модуль `lbm.h/cpp`:
  - Полноценный Navier-Stokes солвер через Boltzmann BGK: 19 направлений, веса `1/3, 1/18, 1/36`, `cs2=1/3`
  - Bounce-back no-slip на поверхности модели из воксельной сетки
  - Inlet/outlet граничные условия с учетом направления потока (azimuth/elevation)
  - LES Smagorinsky турбулентность: `nu_t = (Cs*dx)^2*|S|`, `tau_eff = tau + tau_t`, `Cs=0.12`
  - Трилинейная интерполяция скорости из LBM сетки в мировые координаты
  - Конвертация решеточных единиц в мировые: `scale = flowSpeed / U0`, `U0=0.1` ~ Mach 0.17, стабильность
  - Поля: `rho`, `Ux,Uy,Uz` (LB и World), `pressure = cs2*(rho-1)`, `vorticity |ω|`, `Q-criterion = 0.5*(||Ω||²-||S||²)`
  - Счетчики: шаги, сходимость, средняя плотность, кинетическая энергия, макс скорость LB/World, Reynolds `Re=U*L/nu`, время шага ms, MLUPS
  - Интеграция: `computeVelocityFieldCPU` теперь приоритетно использует LBM если `enabled`, частицы и давление используют LBM
  - UI секция **LBM - Lattice Boltzmann (v1.5.0)**: Enable, Steps per Frame 1-20, Tau 0.51-1.5, U0 0.01-0.25, Smagorinsky toggle + C, отображение grid, steps, converged, rho, kinetic, max vel, Re, convergence, cell size, кнопки Init/Reset/Shutdown/Step 10/100/Compute Vorticity
  - Тесты LBM: Physics (init, BC, solid, NaN, pressure), Conservation (avg rho ~1, solid fraction, kinetic), Vorticity & Q (max, range, NaN), Performance (MLUPS, time, Re, tau)
  - Всего тестов теперь 23: 9 Physics + 10 Code + 4 LBM
  - Улучшено: `flow_field.cpp` использует LBM, `forces.cpp` использует LBM для Cp + давление, `model.cpp` инициализирует LBM после вокселизации, `main.cpp` вызывает `updateLBM` каждый кадр до частиц
  - Build: `lbm.cpp` добавлен в `build.bat`, `build-cpu.bat`, `CMakeLists.txt`

### Улучшено до максимума
- Физика теперь истинный Navier-Stokes вместо потенциального течения + эвристического следа
- Автоматическое отрывное течение, вихри Кармана, турбулентность без ручных параметров wake
- Давление из плотности LBM, более точные lift/drag
- Подготовка к CUDA LBM: структура для GPU ускорения

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
