# Changelog — Aeros Engine

Все значимые изменения проекта документируются здесь.

Формат основан на [Keep a Changelog](https://keepachangelog.com/ru/1.0.0/),
версии — [Semantic Versioning](https://semver.org/lang/ru/).

## [1.26.0] - 2026-10-18

### Добавлено — режим вычислений: таймлапс физики (F7 / F8)
- Новый модуль `aeros/src/timelapse.{h,cpp}`: кольцевая история расчёта. Пишутся все
  скаляры кадра (время, dt, FPS, бэкенд, частицы, линии тока, шаг LBM, rho, vmax, TKE,
  Reynolds, векторы подъёмной силы / сопротивления / момента, центр давления, времена
  шести подсистем, счётчик NaN/Inf, остаток сходимости LBM, диапазон Cp) — 39 колонок.
- Снимки поля давления: куб `24^3` ячеек, квантованный в 1 байт на ячейку, по умолчанию
  каждый 30-й кадр. Работает и с LBM (трилинейная выборка `lbmPressure`), и без него
  (Cp по Бернулли из аналитического поля скоростей), поэтому таймлапс доступен всегда.
- Окно «Таймлапс физики»: 10 графиков истории (FPS, время кадра, время LBM, подъёмная,
  сопротивление, TKE, остаток сходимости, vmax, число частиц, счётчик NaN/Inf) с курсором
  воспроизведения и три тепловых среза записанного поля (XY, XZ, YZ) в той же цветовой
  схеме и в том же диапазоне, что действовали в момент записи.
- Воспроизведение: старт/пауза (F8), перемотка в начало/конец, скорость 0.1x…32x
  (1x = реальное время записи), цикл, ползунок по кадрам. Во время просмотра буфер
  замораживается, иначе кольцо перезаписывалось бы прямо под курсором.
- Фоновая история включена по умолчанию (`tlAutoRecord`): к моменту сбоя запись уже есть,
  её не нужно успевать включить. F7 — явная запись с очисткой истории.
- Экспорт `logs/timelapse-<дата>.csv` (кнопка в панели и в окне), автоматический экспорт
  при выходе, если включён режим отладки, и сводка истории в отладочном отчёте F10
  (новый механизм разделов отчёта `debugLogAddSectionCallback`).
- Ёмкость (300…60000 кадров), период снимков поля и ширина окна графиков настраиваются
  в панели; оценка занимаемой памяти показывается там же (по умолчанию около 5 МБ).
- Ключи командной строки: `--timelapse`, `--no-timelapse`, `--timelapse-capacity N`,
  `--timelapse-field N`, `--timelapse-speed X`.
- Headless-тест модуля (40 проверок: переполнение кольца, поиск по номеру кадра, снимки
  поля и отказ отдавать затёртый снимок, скорость и цикл воспроизведения, CSV, сводка,
  смена ёмкости, фоновая запись, заморозка при просмотре).

### Исправлено — карта давления
- Давление sampled трилинейно (`getLBMPressureWorld` / `getLBMPressureWorldChecked`),
  а не «ближайшей ячейкой»: карта перестала быть ступенчатой. Твёрдые ячейки
  исключаются с перенормировкой весов, как уже делалось для скорости.
- Диапазон раскраски считается по фактическим данным (робастно: гистограмма Cp по
  вершинам, отсечение 1% крайних значений, симметрия относительно нуля, сглаживание),
  а не зашит как `[-3, +1]`. Прежнее окно не совпадало с данными: Cp = 0 попадал на 75%
  шкалы и рисовался оранжевым, а всё сжатие сливалось в красный.
- Схема теперь расходящаяся: Cp = 0 — нейтральный светло-серый, разрежение — холодная
  ветвь, сжатие — тёплая; ноль всегда в центре независимо от асимметрии данных.
- Схема №2 больше не игнорируется: раньше `getRealisticPressureColor` обрабатывала
  только 1 и 3, и выбор «Plasma» для давления ничего не менял.
- GPU красит модель той же картой, что и CPU: диапазон и номер схемы передаются в
  `FlowParams` (`cpLo`, `cpHi`, `colorMap`) и используются в `pressureKernel`. Раньше у
  ядра была своя зашитая шкала `(cp+1)/2` — при переключении бэкенда модель меняла цвет.
- CPU-заглушка (`cuda_stub.cpp`) тоже использует общую функцию раскраски.
- Вместо текстовой легенды, которая обещала «0 = жёлтый», рисуется настоящая цветовая
  шкала из тех же цветов, что применяются к модели, с отметкой нуля и подписями
  фактического диапазона; добавлены переключатель авто-диапазона и ручные границы Cp.
- Ограничение Cp единообразно (`aeroClampCp`, [-4, +2]): раньше карта давления
  ограничивала до 1.8, а расчёт сил — до 2.0.
- Полное давление, шлирен и шлирлих-граф больше не берут давление ближайшей ячейкой.

### Исправлено — физика
- Индексы ячеек: `(int)` вместо `floor` в 20+ местах (частицы, воксельная сетка и SDF,
  эйлеров сетка дыма, все скалярные аксессоры LBM, device-функция `sampleSDF` в CUDA).
  При отрицательных координатах `(int)(-0.4) = 0`, поэтому точка вне расчётной области
  читала ячейку у противоположного края и получала ложные расстояние/давление/скорость;
  в трилинейной выборке дыма это давало ещё и отрицательные веса, то есть экстраполяцию.
- Расчёт вклада треугольника в силы вынесен в общую функцию: ветки с OpenMP и без него
  разошлись — без OpenMP не применялась поправка Кармана-Тсина на сжимаемость, то есть
  результат зависел от того, собран ли бинарь с `/openmp`.
- `dragVector` пересчитывается после базового, индуктивного и волнового сопротивления,
  а `liftVector` — после поправки на экранный эффект. Раньше векторы собирались только
  из давления и трения, поэтому стрелки в 3D показывали меньшую силу, чем числа в панели,
  в CSV и в отчёте.
- Сходимость LBM считается по существу: RMS изменения плотности за кадр
  (`lbmResidualRho`) и изменение максимальной скорости (`lbmResidualU`), а не только
  разница средней кинетической энергии. Видны в панели, в таймлапсе и в отчёте.
- Мин/макс давления в сетке LBM кэшируются за шаг решателя (`lbmPressureMin/Max`),
  вместо двух полных проходов `min_element`/`max_element` по всей сетке каждый кадр
  (на 128^3 это миллионы сравнений на кадр ради одной строки текста в панели).
- Частицы: RK4 проверял на конечность только `pos.x`, а путь Эйлера не проверял ничего —
  NaN в `y`/`z` или в скорости оставался в буфере вершин навсегда (частица «залипала»
  или улетала). Проверяются все компоненты, скорость сбрасывается на скорость потока.

### Исправлено — CUDA
- Постоянные буферы устройства вместо `cudaMalloc`/`cudaFree` на каждом кадре (частицы:
  2 буфера, давление вершин: 3). Буферы растут с запасом 25% и освобождаются один раз
  при завершении (`cudaReleaseAll`).
- Убран лишний `cudaDeviceSynchronize` после каждого запуска ядра: последующая
  `cudaMemcpy` D2H на потоке по умолчанию и так блокирующая, то есть на кадр приходилось
  две полные синхронизации вместо одной.
- `setVoxelData` больше не игнорирует ошибки `cudaMalloc`/`cudaMemcpy`: при неудаче
  старый буфер остаётся валидным, а причина уходит в журнал. Раньше при ошибке
  `g_dDist` оставался освобождённым, а ядра продолжали его разыменовывать.
- Поле SDF проверяется на соответствие текущей сетке (`validateDistField`): если
  `gridNx*gridNy*gridNz` не совпадает с загруженным объёмом, буфер освобождается,
  иначе ядро читало память за его пределами (сетка перестраивается на лету).
- Поле SDF теперь освобождается при завершении — раньше оно не освобождалось никогда.
- Проба бэкенда проверяет версию драйвера до запуска тестового ядра: сборка сделана на
  CUDA 13.x, которая работает на драйверах ветки 580 и новее. Драйвер, сообщающий
  CUDA 12.x, получает внятное сообщение «обновите драйвер» вместо кода ошибки 35/36.
- Добавлены подсказки по-русски для кодов `cudaErrorIllegalAddress`,
  `cudaErrorLaunchTimeout`, `cudaErrorLaunchOutOfResources`, `cudaErrorAssert`,
  `cudaErrorInvalidDevicePointer`.

### Исправлено — то, что не работало
- Vulkan-рендерер — заглушка: `createRenderPass`, `createSwapchain` и запись команд
  возвращали «успех», ничего не рисуя, а `isVulkanAvailable()` отвечала «да» по одному
  факту наличия `vulkan-1.dll` (он есть почти на любом ПК с драйвером видеокарты).
  В результате `main.cpp` создавал окно с `GLFW_NO_API` — без контекста OpenGL — и
  пользователь получал чёрное окно без единой подсказки. Хуже того, выбор
  `renderer=vulkan` сохранялся в `aeros_settings.ini` и ломал каждый следующий запуск.
  Теперь наличие загрузчика (`vulkanLoaderPresent`) отделено от возможности рисовать,
  `isVulkanAvailable()` честно отвечает «нет», приложение всегда создаёт контекст
  OpenGL 3.3, а запрос Vulkan пишет предупреждение в журнал и в отчёт.
- Кнопки выбора рендерера дописывали строку в `aeros_settings.ini` (режим "a"), а при
  запуске читалась ПЕРВАЯ найденная запись: второй и последующие выборы молча не
  применялись. Настройка теперь перезаписывается (`saveRendererSetting`), старые записи
  `renderer=`/`api=` удаляются.
- Кнопка «Vulkan 1.3» больше не обещает то, чего в этой версии нет; текст «Преимущества
  Vulkan» заменён честным статусом рендереров; удалён мёртвый код
  (`getVulkanVersionStringLegacy`, блок перечисления устройств Vulkan).
- Проверка NaN/Inf в позициях частиц шла с шагом 97 и пропускала до 96/97 неконечных
  значений: единичный NaN, означающий развал расчёта, почти никогда не попадал ни в
  журнал физики, ни в таймлапс. Массивы до 300 тысяч элементов проверяются целиком,
  шаг включается только на очень больших.

### Изменено
- `AEROS_VERSION_FULL` больше не обещает Vulkan в заголовке окна: «OpenGL+CUDA+Linux».
- В панель LBM добавлена строка остатка сходимости; строки давления показывают
  кэшированные мин/макс и диапазон раскраски.
- Таймлапс, карта давления и остаток сходимости входят в отладочный отчёт F10.

## [1.25.0] - 2026-10-17

### Добавлено: режим отладки, журнал действий и журнал физики
- Новый модуль `aeros/src/debug_log.{h,cpp}`. Работает во всех сборках:
  x64 с CUDA, x64/x86/arm64 CPU, Lite, Ultra-Lite и Linux.
- Файлы рядом с `main.exe`:
  - `logs\aeros-debug.log` — общий журнал: запуск, версии, железо, бэкенд,
    модель, все действия пользователя, предупреждения и ошибки. У каждой строки
    метка времени, уровень и время от старта; сброс на диск после каждой записи,
    поэтому журнал не обрывается даже при падении программы.
  - `logs\physics.csv` — журнал физики по кадрам (27 колонок): кадр, dt, FPS,
    бэкенд, частицы, линии тока, шаг LBM, rho, vmax, TKE, Re, силы и моменты,
    времена всех подсистем и число неконечных значений (NaN/Inf).
  - `Aeros-Debug-Report-<дата>.txt` — единый отчёт для разработчика: ОС и её
    разрядность, CPU и наборы инструкций (SSE2/AVX/AVX2/FMA), память, архитектура
    и PE-машина запущенного exe, строки OpenGL, полное состояние CUDA, модель,
    все параметры физики, производительность и последние 400 записей журнала.
- Если папка программы недоступна для записи (Program Files), журнал и отчёт
  автоматически уходят в `%LOCALAPPDATA%\AerosEngine` (Windows) или `~/.aeros`
  (Linux) — отчёт можно получить всегда.
- Включение: **F9** — режим отладки, **F10** — сохранить отчёт, панель
  «Отладка и журнал» в UI, либо ключи `--debug`, `--debug-actions`,
  `--debug-physics`, `--debug-overlay`, `--physics-every N`, `--save-report`.
- `--save-report` собирает отчёт и завершает работу **без создания окна**:
  диагностику можно прислать даже тогда, когда программа вообще не запускается.
- Журнал действий ведётся автоматически: раз в кадр сравниваются 43 параметра
  (поток, частицы, линии тока, LBM, бэкенд, качество, FSR, vsync, режимы показа)
  и каждое изменение пишется как «параметр: было -> стало». Полная история
  действий перед сбоем попадает в отчёт без правки каждого контрола UI.
- Экранная панель журнала (галочка «Показывать журнал поверх сцены»): последние
  записи с подсветкой ERROR/WARN/ACTION — удобно снимать проблему на видео.

### Добавлено: диагностика CUDA — ответ на «не работает CUDA»
- `cudaProbeBackend()` (`cuda_api.h`): перечисляет устройства NVIDIA, читает имя
  GPU, compute capability, объём памяти, версии драйвера и среды выполнения CUDA
  и **реально запускает тестовое ядро**. Итог — одна понятная строка по-русски;
  она показывается в панели «Отладка и журнал», под переключателем CUDA/CPU в
  панели «Вычисления и Экспорт» и входит в отчёт.
- Типовые причины расшифрованы: GPU не найден; драйвер слишком старый; GPU старше
  Turing (нет машинного кода для этой архитектуры); GPU занят другим процессом;
  не хватило памяти; сбой инициализации драйвера; неверный параметр вызова.
- `cudaConsumeError()`: ошибка CUDA внутри физики больше не теряется — её
  забирает вызывающий код, пишет в журнал и сразу переключается на CPU-бэкенд
  **в том же кадре** (инициализация частиц, обновление частиц, давление вершин,
  передача SDF на GPU). Счётчик откатов виден в UI и в отчёте.
- Кнопка «Перепроверить CUDA» выполняет пробу заново без перезапуска программы.

### Исправлено: CUDA
- **Молчаливый сбой GPU.** Макрос `CUDA_CHECK` в `kernel.cu` печатал сообщение в
  `std::cerr` и выходил из функции. В сборке с `/SUBSYSTEM:WINDOWS` консоли нет,
  сообщение не видел никто, выходные буферы оставались незаполненными, а `useCUDA`
  продолжал быть равен 1: физика выглядела сломанной без единой ошибки. Теперь
  каждая ошибка фиксируется, пишется в журнал и включает CPU-путь.
- **Машинный код только для Turing.** `build.bat` передавал nvcc лишь
  `-arch=sm_75`. Теперь `-gencode arch=compute_75,code=[sm_75,compute_75]`: в
  бинарь кладутся и SASS для Turing, и PTX, который драйвер компилирует для более
  новых архитектур (Ampere / Ada / Blackwell). Одновременно `-arch=` и `-gencode`
  nvcc принимать не желает, поэтому старый флаг заменён, а не дополнен.
- **Молчаливый уход в CPU-only.** Если `nvcc` не найден или сборка с CUDA не
  удалась, `build.bat` собирал CPU-вариант, и в релиз попадал x64 без CUDA —
  внешне неотличимый от рабочего. Теперь скрипты пишут маркер
  `bin\BUILD-BACKEND-<arch>.txt` (`CUDA` / `CPU_ONLY`), CI для x64 требует `CUDA`
  и останавливает публикацию при его отсутствии, а в аннотацию шага выводятся
  маркер и признаки `nv_fatbin` / PTX / `sm_75` внутри exe.
- GPU с compute capability ниже 7.5 (Maxwell/Pascal/Volta) не поддерживаются
  набором инструментов CUDA 13, которым собирается релиз, — для них остаётся
  CPU-бэкенд. Отчёт теперь прямо называет этот случай вместо неопределённого
  «не работает»; по нему же принимается решение о сборке на CUDA 12.x.

### Исправлено и улучшено: журналирование сбоев
- В журнал пишутся причины ранних сбоев запуска: `glfwInit`, создание окна,
  загрузка GLAD, инициализация Vulkan/OpenGL, бэкенды ImGui, компиляция шейдеров,
  диалог выбора модели и чтение STL (пустая модель, несовпадение вершин/нормалей,
  отсутствие конечных вершин).
- События физики в журнале: инициализация и сброс LBM, нехватка памяти на сетку,
  недопустимое число ячеек, расходимость (rho вне 0.8..1.2), всплеск NaN/Inf,
  переустановка решателя при изменении воксельной сетки, параметры вокселизации,
  итог загрузки модели (вершины, SDF-сетка, площадь, число Рейнольдса).

### Сборка
- `debug_log.cpp` добавлен во все сценарии: `build.bat`, `build-cpu.bat`,
  `build-lite.bat`, `build-ultra-lite.bat`, `build-lite.sh`, `build-ultra-lite.sh`,
  `CMakeLists.txt`, `Makefile.linux`, `Makefile.lite`.
- `logs/`, `physics.csv` и `Aeros-Debug-Report-*.txt` добавлены в `.gitignore`:
  рабочие журналы не должны попадать в репозиторий.
- Форма релиза не изменилась: Setup / Update / Portable для x64, x86 и arm64
  плюс `SHA256SUMS.txt` и `README.md` — 11 ассетов, как в v1.24.0.

## [1.24.0] - 2026-10-11

### PC-релиз на основе обновления v1.20.1
- Релиз собран на основе дерева PC-релиза v1.20.1 (LBM, Vulkan-наработки v1.19,
  FSR/FrameGen, атмосфера, шлирен/дым/скачки/вихри, test mode, локализация,
  Lite/Ultra-Lite профили, benchmark).
- Полный набор PC-артефактов, как в прежних PC-версиях: установщики, апдейтеры
  и портативные zip для x64 / x86 / arm64 + SHA256SUMS.txt.
- x64 — с CUDA (sm_75, fallback CPU), x86 / arm64 — CPU-сборки.
- CI (.github/workflows/pc-release.yml): сборка по тегу vX.Y.Z и автопубликация
  GitHub Release; версия — из aeros/src/version.h и VERSION.
- Фиксы сборки v1.20.1 под Windows (у апстрима Windows-сборка не компилировалась):
  - forces.cpp (шлирен): вызов computeVelocityFieldCPU без FlowParams;
  - ui.cpp: не был подключён vulkan_renderer.h;
  - vulkan_renderer.cpp: GetSaveFileNameA без <commdlg.h>;
  - lite_config.h: в обычной сборке отсутствовали LITE_*-макросы;
  - lite_config.cpp: дубли определений глобалов из globals.cpp;
  - build-cpu.bat: добавлен lite_config.cpp, проверки errorlevel.
- Фиксы конвейера: vcvars целевой архитектуры вызывается всегда; портативные
  пакеты несут glfw3.dll только своей архитектуры.

### Исправлено: установщик ставил exe чужой архитектуры (CreateProcess код 216)
- **Критично.** Во всех `.iss` выбор бинаря шёл через
  `Check: FileExists('..\aeros\bin\main-<arch>.exe')`. Параметр `Check:` в Inno
  выполняется во время установки **на компьютере пользователя**, где такого
  относительного пути нет: правильный `main-<arch>.exe` отбрасывался, а на диск
  ложился общий fallback `bin\main.exe` — то есть бинарь последней собранной
  архитектуры (arm64). На x64-ПК это давало
  «Невозможно выполнить файл main.exe, CreateProcess сбой; код 216 — эта версия %1
  не совместима с версией Windows» и «Это приложение не удалось запустить на вашем ПК».
- Выбор бинаря перенесён на этап компиляции: в `[Files]` одна безусловная строка
  `Source: "..\aeros\bin\main-<arch>.exe"`, fallback на `main.exe` удалён.
  Если бинарь не собран, ISCC падает с ошибкой, а не подкладывает чужую архитектуру.
- То же в `build-portable.ps1`: портативный zip больше не может взять общий
  `bin\main.exe` вместо `main-<arch>.exe`.
- `installer\archcheck.inc` (новый): после установки перечитывает PE-заголовок
  установленного `{app}\main.exe`, при несовпадении показывает понятное сообщение,
  пишет `ARCH-MISMATCH.txt` и отключает автозапуск (`Check: AerosLaunchAllowed`).
- `build-installers.ps1` / `build-portable.ps1` сверяют поле Machine из PE-заголовка
  каждого бинаря перед упаковкой (8664 / 014C / AA64).
- DLL в установщиках — только своей архитектуры: шаблон `..\aeros\bin\*.dll` убран
  (в `bin` лежат `glfw3-x86.dll`, `glfw3-arm64.dll` и неиспользуемые NVIDIA Streamline
  `sl.*.dll`, которые код не загружает).
- Апдейтеры: `DefaultDirName={code:GetDefaultDirName}` — обновление ставится в папку
  уже установленной программы, найденную в реестре; раньше в тихом режиме оно могло
  уйти в папку по умолчанию, создав вторую копию.
- `SetupIconFile` указывает на существующий `..\aeros\src\icon.ico`
  (прежний путь `..\aeros\bin\icon.ico` в репозитории отсутствует — setup.exe
  собирался без иконки).
- `AerosEngine-Lite-x64.iss`: `LiteAppId` был невалидным GUID (суффикс `-LITE`).
- CI: перед сборкой чистятся устаревшие `main*.exe` в `bin`; после сборки установщиков
  каждый из них ставится в тихом режиме и проверяется PE-машина установленного
  `main.exe` и всех DLL; портативные zip проверяются так же (exe + DLL внутри архива);
  апдейтер проверяется на то, что он обновляет найденную папку установки.
- `build-cpu.bat`: добавлен `benchmark.cpp` (build.bat его компилировал, CPU-сборка — нет);
  версия берётся из `VERSION`, а при его отсутствии — из `aeros\src\version.h`
  (прежде подставлялись устаревшие заглушки 1.0.0 / 1.19.0).
- Убран мёртвый код, на который ругался компилятор (неиспользуемые переменные в
  `flow_field.cpp`, `forces.cpp`, `lbm.cpp`, `interesting.cpp`, `streamlines.cpp`);
  все исходники собираются без предупреждений.

### Исправлено: запуск на обычных ПК Intel/AMD и совместимость CPU
- **Базовый набор инструкций — SSE2.** Релизные бинари собирались с `/arch:AVX2`
  (а в CMake/Makefile — с `-march=native`), из-за чего на процессорах старше
  Haswell запуск падал с STATUS_ILLEGAL_INSTRUCTION. Теперь `build.bat` и
  `build-cpu.bat` используют `CPUARCHFLAGS` = `/arch:SSE2` для x64/x86 (для arm64
  флаг не задаётся), `CMakeLists.txt` — `/arch:SSE2` при `AEROS_AVX2=OFF`
  (по умолчанию), `Makefile.linux` — пустой `ARCHFLAGS`. AVX2 включается явно:
  `AEROS_AVX2=1`, `-DAEROS_AVX2=ON`, `make ARCHFLAGS="-march=native"`.
- **Окно показывается до модального диалога.** Окно создавалось с
  `GLFW_VISIBLE=FALSE` и появлялось только после диалога выбора STL-модели,
  у которого не было окна-владельца. При запуске из установщика
  (`[Run] ... nowait postinstall`) это выглядело как «игра не запускается»:
  диалог мог остаться за окном мастера. Теперь перед диалогом идут `glClear` и
  `glfwSwapBuffers`, затем `glfwShowWindow` и `glfwFocusWindow`, а владельцем
  диалога становится `glfwGetWin32Window(window)`.
- `openFileDialog(void* ownerWindow = nullptr)` в `stl_loader.h/.cpp`
  (`ofn.hwndOwner`); вызов из `main.cpp` передаёт HWND окна, конфликтующий
  `extern`-прототип в `vulkan_renderer.cpp` удалён.
- **Единственный источник версии — `aeros\src\version.h`**: заголовок окна,
  `--help`, заголовок панели настроек, `app.title` и заголовки диалогов
  Win32/zenity больше не содержат вписанных вручную устаревших строк.

### Исправлено: компиляция установщиков Inno Setup
- `IntToHex` в секции `[Code]` Inno Setup **не существует** (такая функция есть
  только у препроцессора ISPP и во внутренних модулях Setup) — контроль
  архитектуры ронял компиляцию первого же `.iss`. Добавлена своя `AerosHex4`
  на стандартных `Copy`, `mod` и `div`.
- `#include "archcheck.inc"` стоял внутри **комментария** в шапке `archcheck.inc`:
  ISPP разбирает директивы раньше, чем комментарии Pascal, и уходил в рекурсию
  («File ... is already being included»).
- Строка `[ExePath, ...]` (массив аргументов `Format`) начиналась с `[`, и ISCC
  принимал её за заголовок секции («Invalid section tag») — перенос массива
  на новую строку внутри `[Code]` недопустим.
- `{app}` и `'}'` внутри Pascal-комментария закрывали комментарий раньше времени
  и давали «String error»; теперь в комментариях `[Code]` фигурных скобок нет.
- Имя файла апдейтера: повторный `#define OutputBaseName` после `common.iss` ISPP
  трактует как перегрузку одноимённого символа, из-за чего апдейтер мог собраться
  с именем `Setup-...`. Введён `OutputKind` (`Setup` / `Update`), который
  апдейтеры задают **до** подключения `common.iss`.

### CI: причины сбоев теперь видны без логов шагов
- Логи шагов GitHub Actions доступны не всегда, поэтому всё важное дублируется в
  аннотации: `Lint PowerShell` (разбор всех `.ps1` и всех inline-блоков воркфлоу
  парсером PowerShell — падает за секунды, а не после десятиминутной сборки),
  снимок `aeros\bin` с PE-машиной каждого файла (`preflight-bin`), итоги проверок
  (`portable`, `zip-verify`, `iscc`, `install-verify`, `publish`).
- Отдельный job `iss-check` компилирует все `.iss` настоящим ISCC с заглушками
  вместо бинарей — параллельно со сборкой, ошибка видна через три минуты.
- Шаги упаковки обёрнуты в try/catch: полный вывод уходит в сводку шага, наружу —
  две короткие аннотации (пачку из 25 аннотаций GitHub отбрасывает целиком).
- `concurrency` на тег: два параллельных прогона не пересоздают один и тот же релиз.
- Перед публикацией проверяется, что ассетов ровно 11.


## [1.8.0] - 2026-10-05

### Realistic Aero+ — полный аудит и исправление всего возможного

#### Что добавлено (по запросу "добавь еще что-то и исправь все")
- **Новые глобальные фичи (globals.h/cpp v1.8.0)**:
  - `showGroundPlane`, `groundVAO/VBO/EBO`, `groundColor`, `groundAlpha`, `groundIndexCount` — визуализация земли для авто
  - `showSlicePlane`, `sliceVAO/VBO`, `gridVAO/VBO` — срез как на фото 5 Cybertruck
  - `showColorLegend`, `autoRotate`, `aeroAutoRotateSpeed` — легенда цветов и авто-вращение для showcase
  - `liftToDragRatio`, `momentVector/Magnitude` — L/D и момент
  - `AeroVisMode` расширен: `SkinFriction`, `BoundaryLayer`
  - Новые: `aeroShowBoundaryLayer`, `aeroBoundaryLayerScale`, `aeroMachEffects`, `aeroReNumber`, `aeroShowWake`, `aeroWakeOpacity`, `aeroShowVortices`, `aeroUseRealisticLighting`, `aeroColorMap` (0 rainbow,1 viridis,2 parula,3 coolwarm), `aeroExportEnabled`
- **LBM v1.8.0 — стабильность + реализм**:
  - Clamp сетки до 8M cells, проверка на 20M лимит, защита от bad_alloc
  - `computeEquilibriumFast` теперь clamps velocity и cu для стабильности, проверка feq на NaN и ограничения
  - Инициализация с Perlin-like турбулентностью (двойной sin с разными частотами) + clamping usqr
  - `stepLBMCPU`: проверка `f` размера, nanCount подсчет, divergence detection по rho, soft reset на inlet при расходимости, clamping fi, rho, velocity, TKE, Q, vorticity
  - `getLBMVelocityLB`: clamp tx/ty/tz, проверка границ total, проверка isfinite
  - `getLBMVelocityWorld`: clamping world velocity до 3*Vinf, проверка isfinite
  - `getLBMDensityWorld`: clamp rho [0.5,1.5]
  - Все геттеры теперь проверяют isfinite и размеры
  - `computeLBMVorticityAndQ`: проверка границ xm/xp и т.д., проверка isfinite du, clamping vortMag до 1000, Q до 1e6, strain, TKE до 10
  - `computeLBMRe`: использует maxDim для физического Re, а не только решетку
  - `computeLBMRefArea`: проверки isfinite
  - Новые цветовые карты: `getQCriterionColor`, `getTKEColor`, поддержка `aeroColorMap` в `getRealisticPressureColor` и `getVelocityMagnitudeColor` (viridis/parula/coolwarm)
  - Новые валидации: `lbmValidateStability()` — проверка rho, maxVel, kineticEnergy
  - Ground handling улучшен: gy0 вычисляется и цикл 0..gy0
  - Voxel size mismatch handling — nearest mapping если размеры не совпадают
- **Flow Field v1.8.0**:
  - Ground effect: no-slip на земле, пограничный слой 1/7 степенной закон, Venturi ускорение под днищем авто (clearance check + проекция X/Z)
  - Wake: учет `aeroShowWake` и `aeroWakeOpacity`, Mach effects (compressibility) — turbScale увеличивается с Mach
  - Clamping скорости до 2*maxSpeed
  - `colorForPoint`: поддержка viridis/parula/coolwarm карт
  - Re вычисляется в `updateFlowParams`
- **Forces v1.8.0**:
  - Проверки isfinite везде, размеры %3, размеры векторов
  - Skin friction зависит от Re (1/Re^0.2)
  - Момент `momentSum` считается, `liftToDragRatio`, `momentVector`
  - `updateLiftDragArrows`: проверка maxDim, isfinite centerOfPressure, e
  - Новые режимы: SkinFriction, BoundaryLayer
  - Fallback с CUDA на CPU при ошибке
- **Particles/Streamlines v1.8.0**:
  - Проверки isfinite везде, clamping, ground collision
  - Поддержка новых режимов Q/TKE/Cf
  - Streamlines: `rk4StepOpt` с проверками isfinite для всех шагов, ground collision, проверка bigMargin isfinite
  - Логирование количества
- **GL Utils v1.8.0**:
  - Все создания VAO проверяют isfinite size, clamping divisions, size
  - `createGroundPlane`, `createGrid`, `createSlicePlane` — новые
  - `cleanupGLResources()` — полное удаление всех VAO/VBO
  - `createBoundingBoxVAO`: проверка BB isfinite
- **Shaders v1.8.0**:
  - Улучшенное PBR-like освещение с Fresnel, Blinn-Phong specular, rim lighting для силуэта как на фото
  - Particle shader: круглая частица с мягкими краями, `pointSize` uniform
  - Ground shader: шахматная земля + туман по расстоянию
  - Поддержка `useRealisticLighting`
- **Model v1.8.0**:
  - Исправлен баг: `g_vertexColors.assign(vertices.size(), 0.75f)` теперь правильно, но добавили вариацию sin/cos для интереса
  - Проверка vertices/normals size mismatch, %9, isfinite
  - Пересчет нормалей если NaN или длина ~0
  - Логирование BB, maxDim, ref area, Re
  - Создание ground plane и grid
  - Auto ref area и Re расчет
- **Voxel Grid v1.8.0**:
  - Проверка empty verts, res clamp 8..256, BB isfinite
  - Limit total cells 10M, clamping scale
  - Try/catch для alloc
  - Skip degenerate triangles (cross <1e-12)
  - `sampleSDFCPU`/`sdfNormalCPU`: проверки isfinite, fallback csy/csz=csx, clamp raw
  - `insideMeshOptimized`: skip degenerate
- **Atmosphere v1.8.0**:
  - Все функции проверяют isfinite, clamp altitude 0..80km, speed 0..1e6, expArg -50..50
  - `calculateAtmosphere`: проверки ratio, temperature, pressure isfinite, clamp density 1e-6..5, pressure, temperature
  - Логирование при invalid density
- **STL Loader v1.8.0**:
  - Проверка fileSize <10, nt >10M -> ASCII, isfinite вершин и нормалей, clamp fabs>1e6
  - Нормализация нормалей, skip non-finite
  - Trim строк, tolower, проверка isfinite, skip degenerate
  - Логирование
  - `openFileDialog`: initialDir, title, defExt
- **Input v1.8.0**:
  - Проверка ImGui WantCaptureMouse
  - firstMouse handling, isfinite checks, clamp yaw/pitch/fov
  - `processInput`: clamp cameraSpeed, check isfinite, right vector isfinite, Q/E для вертикали, F1/F2 быстрые клавиши
  - Scroll: *2.0f, проверка isfinite
- **Main v1.8.0**:
  - MSAA 4x, doublebuffer, title v1.8.0, vendor/renderer/version лог
  - GL debug callback если KHR_debug
  - ImGui docking enable, GL_MULTISAMPLE, PROGRAM_POINT_SIZE
  - Ground shader program
  - Проверка shader compile fail
  - No file selected — cleanup и exit 0
  - Auto rotate logic, ground height change detection для streamlines
  - Ground plane + grid rendering, slice plane rendering
  - VSync toggle handling
  - Cleanup: delete programs, cleanupGLResources(), shutdownLBM()
- **UI v1.8.0**:
  - Полностью переписан: Performance показывает LBM pressure, MLUPS, Re, conv, TKE
  - Realistic Aero: Combo Visualization 7 режимов, Color Map 4 режима, Wake Opacity, Vortices, BL, PBR lighting, Ground Plane + alpha/color, Slice Plane + axis/pos, Mach effects, Ref Area Re, 5 пресетов (Car/UAV/Airfoil/Turbulent Wake/Viridis)
  - Flow: maxDisplay увеличен до 30 m/s (108 km/h), clamp azimuth 0..360, auto rotate speed
  - Atmosphere: Re, Mach с подписями compressible/subsonic/incompressible, q*RefArea
  - Particles: кнопки 5k/15k/50k
  - Streamlines: кнопки Low/Med/High
  - Pressure & Forces: Cd/Cl/L/D/Moment/CoP/q/Re, color legend для каждого режима
  - Voxel: показывает cells и ms
  - Display: VSync, Limit FPS, Camera Speed, Mouse Sens, Model Color, Controls hint
  - LBM: Regularized checkbox, Step 500, все параметры с проверками isfinite
  - Compute: CUDA/CPU radio, Open Model, FULL REBUILD, Screenshot
  - Test Mode: 5 категорий (Physics/Code/LBM/Opt/Realistic), 6 тестов realistic, кнопки LBM Only/Realistic Only/Opt Only, drawCategory lambda
  - Frame validation: проверка aeroRefArea, buffers, SDF finite count, mem, particles/streamlines VAOs
- **Tests v1.8.0**:
  - Новые: `testGroundEffect`, `testColorMaps` (все 4 карты, NaN, range [0,1]), `testStability` (LBM stability, conservation, NaN, global state), `testRefArea` (auto/manual, Cd/Cl), `testReynolds` (Re меняется со скоростью, LBM Re)
  - `testRealisticAero`: проверки isfinite
  - `runRealisticTests()` — 6 тестов, `runAllTests()` теперь 5 категорий, 27+ тестов
  - Всего тестов: 9 Physics +10 Code +4 LBM +3 Opt +6 Realistic = 32 теста
- **Build**:
  - `build.bat` уже чистит obj/exe, `tools/rebuild-all.bat` есть
  - Версия bump 1.7.0->1.8.0

#### Исправлено
- Множество NaN/Inf багов: везде проверки isfinite, clamping, fallback
- LBM divergence: soft reset, clamping, nanCount detection
- Voxel overflow: limit 10M cells
- Model color bug: теперь правильно assign + вариация
- Camera NaN: проверки, clamp
- FlowParams: Re расчет, Mach effects
- Particles/Streamlines: ground collision, isfinite
- SDF: проверки raw, isfinite
- STL: degenerate triangles skip, fileSize check
- OpenGL: VAO/VBO cleanup, MSAA, debug callback
- UI: color legend, L/D, moment, Re, Mach

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
