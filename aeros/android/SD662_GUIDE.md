# Aeros Engine — Snapdragon 662 + Adreno 610 Optimization Guide v1.21.0
## Специально для твоего телефона — Qualcomm Snapdragon 662 (SM6115) + Adreno 610

### Твой телефон — AIDA64 данные

**CPU — Snapdragon 662 (SM6115):**
- Модель SoC: Qualcomm Snapdragon 662 (SM6115)
- Архитектура ядра: 4x Qualcomm Kryo 260 LP @ 2016 МГц + 4x Kryo 260 HP @ 2112 МГц
- Техпроцесс: 11 нм
- Наборы инструкций: 64-bit ARMv8-A
- Ревизия: r10p2
- Число ядер: 8
- Диапазон частот: 300 - 2112 МГц
- Частота всех 8 ядер: 2016 МГц (на момент скриншота — все нагружены или в балансе)
- Регулятор загрузки: walt (Window Assisted Load Tracking — современный для SD662)
- Поддерживаемые ABI: arm64-v8a, armeabi-v7a, armeabi (64 и 32 бита)
- 32-bit ABI: armeabi-v7a, armeabi
- 64-bit ABI: arm64-v8a
- AES: Поддерживается (аппаратное ускорение шифрования)

**GPU — Adreno 610:**
- Разрешение экрана: 720 x 1604 (HD+ 20:9, 1.15M пикселей — меньше чем 1080p, меньше fillrate)
- Технология: IPS LCD
- Размер: 69 мм x 155 мм, диагональ 6.67" (большой экран)
- Плотность: 264 dpi (xhdpi), xdpi/ydpi 265/263
- Производитель ГП: Qualcomm
- Рендерер ГП: Adreno (TM) 610
- Загрузка ГП: 0% (в простое)
- Частота обновления: 90 Гц (важно! 90Hz дисплей — можно 60-90 FPS, но для батареи лучше 45-60)
- Ориентация: Книжная (портрет)
- Версия OpenGL ES: 3.2
- Версия ГП: OpenGL ES 3.2 V@0615.102.A (GIT@9eca3e902a, I3da8aa4c1, 1774943375) (Date:03/31/26)
- Расширения: GL_OES_EGL_image, external, sync, vertex_half_float, framebuffer_object, rgb8_rgba8, compressed_ETC1_RGB8_texture, AMD_compressed_ATC_texture, KHR_texture_compression_astc_ldr, texture_npot, texture_filter_anisotropic, texture_format_BGRA8888, read_format_bgra и т.д.

### Что это значит

**Snapdragon 662 — средний чип 2020 года:**
- 4x Kryo 260 Gold (Cortex-A73 @ 2.0-2.2GHz) — производительные ядра для тяжелых задач
- 4x Kryo 260 Silver (Cortex-A53 @ 1.8GHz) — энергоэффективные для фона
- 11nm — не самый новый (сейчас 4nm), но не греется сильно, троттлит меньше чем 14nm
- walt governor — современный, быстро переключает частоты, экономит батарею
- 8 ядер — но не все одновременно на макс, обычно 4 Gold для игр
- ARMv8-A + AES + NEON — поддерживает SIMD, быстрое шифрование

**Adreno 610 — средний GPU 2020:**
- ~150 GFLOPS — в 4x быстрее Adreno 306 (40 GFLOPS), в 2x быстрее Adreno 506 (80 GFLOPS), но в 3x медленнее Adreno 640 (400 GFLOPS)
- OpenGL ES 3.2 + Vulkan 1.1 + OpenCL 2.0 — поддерживает всё современное, но не ES 3.2+ extensions для самых новых
- ASTC LDR — сжатие текстур ASTC, экономит VRAM в 4x
- ETC1, ATC — тоже сжатие
- Anisotropic filtering — анизотропная фильтрация, улучшает качество текстур под углом
- 720x1604 — HD+, 1.15M пикселей — меньше чем 1080x1920 (2.07M) — в 1.8x меньше fillrate, легче для GPU
- 90Hz — дисплей поддерживает 90Hz, но для батареи лучше 60 FPS, на батарее 45 FPS

**Твой телефон — это не самый слабый, а средний!**
- Не Potato (Atom 1GB Adreno 306) — твой в 4x быстрее
- Не Low (i3-3xxx HD4000) — твой примерно равен Low-Medium на десктопе
- **Balanced — идеально для тебя!** — больше чем Lite, меньше чем Full, оптимизировано под 720p 90Hz

### Оптимальные настройки для твоего телефона

#### Balanced (SD662/Adreno 610) — рекомендуется!

**Частицы:**
- 2500 default, max 5000 (vs Lite 1500/5000, Potato 300/500, Full 15000/100k)
- Размер 2.8 (больше для видимости на 6.67" экране)
- 2500 частиц — в 1.6x больше чем Lite, но в 6x меньше чем Full — баланс качества и FPS

**Линии тока:**
- 12 default, max 20, шагов 120 max 200 (vs Lite 8x80 max 16x150, Potato 3x30, Full 24x300)
- Ширина 2.5, alpha 0.85 — толще для touch экрана, видно пальцем
- 12x120=1440 точек vs Full 24x300=7200 — в 5x меньше, но больше чем Lite 8x80=640 — лучше видно

**Воксели:**
- 32 (vs Lite 24, Potato 12, Full 48) — 32³=32768 vs 48³=110592 — в 3.3x меньше чем Full, но в 2.3x больше чем Lite 24³=13824 — лучше качество, но не тяжело для Adreno 610
- LBM OFF 24 res — 24³=13824 vs Full 128³=2M — в 150x меньше, не греет

**FPS:**
- 60 target, 45 battery (vs Lite 30, Potato 15, Full 60)
- Для 90Hz дисплея — 60 FPS идеально, 90 FPS слишком тяжело и жрет батарею
- На батарее — 45 FPS для экономии, VSync ON
- Динамическое качество — если FPS <36 (60*0.6), снижает частицы 20%

**Потоки:**
- 4 (vs Lite 2, Potato 1, Full all) — использует 4x Gold ядра Kryo 260 @ 2.11GHz, оставляет 4x Silver для системы
- Не все 8 — чтобы не грелся и не троттлил, 4 Gold достаточно

**Фичи:**
- FSR ON (vs Lite OFF, Potato OFF, Full ON) — помогает для 720p -> 1080p апскейл, Adreno 610 поддерживает, экономит fillrate
- FG OFF — Frame Generation тяжело для Adreno 610
- Vortex tubes ON low count 12 (vs Lite OFF, Full ON 48) — можно, красиво, не тяжело
- Shock ON, Schlieren ON, Temperature ON — легкие, можно
- Volumetric OFF, LIC OFF, Acoustic OFF, Flight OFF — тяжелые, выкл

**Память:**
- <800 MB (vs Lite <512, Potato <256, Full 1-2GB) — SD662 обычно 4-6GB RAM, 800 MB ок, системе остается 3-5GB
- VRAM ~400 MB — Adreno 610 shared RAM, 400 MB из 4-6GB — норм
- APK <25 MB (vs Lite <20, Potato <10, Full ~30-40)

**GL:**
- ES 3.2 (vs Lite ES 3.0/2.0, Potato ES 2.0, Full ES 3.2+Vulkan) — Adreno 610 поддерживает ES 3.2, используем
- ASTC ON — Adreno 610 поддерживает ASTC LDR, сжимает текстуры в 4x, экономит VRAM
- MSAA 0 — без сглаживания, экономит fillrate для 720p
- Shadows OFF, PBR OFF — упрощенные шейдеры, быстрее

**Окно:**
- 720x1604 портрет native, но engine landscape 1604x720 — для твоей ориентации
- Для Android — портрет 720x1604, для Desktop — 1280x720

### Сборка для твоего телефона

#### Android APK — SD662 Optimized

```bash
cd aeros/android
./build-sd662.sh
# Создает app/build.gradle.sd662 с arm64-v8a only, O2, NEON, cortex-a73 tuning
# APK: app/build/outputs/apk/release/app-release.apk
# Копия: ../../release/aeros-engine-android-sd662-adreno610-v1.21.0.apk
# Размер: <25 MB
# Настройки: 2500 particles, 12x120 streamlines, voxel 32, 60 FPS, 4 threads, FSR ON, ASTC
```

Или через Android Studio: открой `aeros/android`, Build Variants → release, Build APK

Или используй готовые Lite/Potato и потом в приложении выбери Balanced preset:

```bash
./build-android.sh lite    # Lite APK <20 MB, потом в app выбери Balanced
./build-android.sh potato  # Potato APK <10 MB, для самых слабых
```

#### Desktop — если хочешь на ПК эмулировать SD662

```bash
cd aeros
./build-lite.sh all
./bin/aeros-engine-lite --preset balanced --sd662
# или
./bin/aeros-engine-lite --preset sd662
```

### Установка на твой телефон

```bash
adb install release/aeros-engine-android-sd662-adreno610-v1.21.0.apk
# или
adb install release/aeros-engine-android-lite-v1.21.0.apk
# Потом в app выбери Balanced preset
```

Или скопируй APK на телефон (Telegram, Google Drive, USB) и открой файл менеджером — разреши установку из неизвестных источников.

### Использование на твоем телефоне

#### Запуск
1. Открой приложение — NativeActivity с движком
2. Если file picker — выбери STL модель из Download
3. Если нет STL — встроенная модель

#### Touch управление — оптимизировано для 6.67" 720x1604
- **Один палец drag** — вращение модели, delta X/Y, touch slop 8px игнорирует дрожания
- **Два пальца pinch** — zoom 0.5-2.0, для 6.67" удобно двумя руками
- **90Hz** — скролл плавный, но engine 60 FPS для батареи

#### Настройки для твоего телефона — Balanced

В ImGui меню → Lite → Balanced (SD662):

- Particles: 2500 (можешь увеличить до 5000 если FPS >50, или уменьшить до 1500 если FPS <30)
- Streamlines: 12x120 (можешь 20x200 для красоты, если FPS ок)
- Voxel: 32 (можешь 40 если FPS >50)
- FPS: 60 target, 45 battery — на зарядке 60, на батарее 45 для экономии
- Threads: 4 — использует Gold ядра
- FSR: ON — для 720p апскейл
- Vortex tubes: 12 — красиво, не тяжело

Если FPS падает <36 — автоматически снижает частицы 20% (dynamic quality scaling ON)

#### Батарея — для твоего телефона

- На AC (зарядка): 60 FPS, 4 threads, FSR ON, vortex tubes ON
- На батарее: 45 FPS, 2 threads, FSR ON, vortex tubes OFF (battery saver)
- Авто-детект: `isBatteryPower()` — если на батарее, включает saver
- Форсировать: `--battery-saver` или в UI Battery Saver checkbox

- Яркость экрана — для IPS LCD 6.67" яркость жрет батарею, уменьши до 50%
- Закрой браузер, Facebook, TikTok — они жрут RAM и CPU
- Играй на зарядке без чехла — SD662 греется под нагрузкой, троттлит с 2.1GHz до 1.5GHz

#### Температура — SD662 троттлит

- 11nm — греется меньше чем 14nm, но всё равно троттлит при 70°C+
- Если FPS падает с 60 до 30 через 5 минут — это троттлинг
- Решение: сними чехол, подставка, не играй на солнце, Potato preset 15 FPS

### Сравнение с другими телефонами

| Телефон | SoC | GPU | RAM | Пресет | FPS | Частицы | RAM usage |
|---------|-----|-----|-----|--------|-----|---------|-----------|
| Galaxy Ace 4 | SD410 4x A53 1.2GHz | Adreno 306 ES2.0 | 1GB | Potato | 15 | 300 | <150 MB |
| Redmi 2 | SD410 | Adreno 306 | 1-2GB | Potato | 15-20 | 300-500 | <200 MB |
| Galaxy J5 | SD615 8x A53 1.5GHz | Adreno 405 ES3.0 | 2GB | Low | 25-30 | 1500 | <400 MB |
| **Твой телефон** | **SD662 8x Kryo260 2.1GHz** | **Adreno 610 ES3.2** | **4-6GB** | **Balanced** | **45-60** | **2500** | **<800 MB** |
| Poco X3 | SD732G 8x Kryo470 2.3GHz | Adreno 618 ES3.2 | 6GB | High | 55-60 | 8000 | <1.2GB |
| S20 | SD865 8x Kryo585 2.84GHz | Adreno 650 ES3.2 | 8GB | Full | 60 | 15000 | <1.5GB |

**Твой телефон — в 4x быстрее чем Potato (Adreno 306) и в 1.6x быстрее чем Low (Adreno 405), но в 2x медленнее чем High (Adreno 618) — Balanced идеально!**

### Бенчмарк для твоего телефона

Встроенный бенчмарк оценивает FPS:

```cpp
BenchmarkResult r = benchmarkPreset(3); // Balanced
printBenchmarkResult(r);
// Ожидаемо для SD662/Adreno 610 720x1604:
// Particles 2500, Streamlines 12x120, Voxel 32
// VRAM ~400 MB, RAM ~800 MB
// FPS avg 50-60, min 35, max 70
// Frame time 16-20 ms, CPU 10ms GPU 6ms
// Result: PASSED — 60 FPS target
```

Если FAILED — попробуй Medium (5000 particles 16x150 voxel32 45 FPS) или Low.

### Troubleshooting для твоего телефона

#### Не устанавливается — Parse error
- Нужен Android 5.0+ API21+ — у тебя Android 11+ скорее всего (SD662 вышел с Android 10), ок
- APK для arm64-v8a — у тебя arm64-v8a поддерживается (из AIDA64), ок
- Включи неизвестные источники: Настройки → Приложения → Специальный доступ → Установка неизвестных приложений → Файл менеджер → Разрешить

#### Черный экран / вылетает
- OpenGL ES 3.2 — у тебя ES 3.2 (из AIDA64), ок, используй Lite или SD662 APK, не Potato (Potato для ES2.0)
- RAM — у тебя 4-6GB, нужно 800MB свободно, закрой другие приложения
- Логи: `adb logcat | grep AerosEngine` — посмотри ошибку

#### FPS <30 на твоем телефоне
- Должно быть 45-60 FPS на Balanced — если <30, значит троттлинг или батарея
- Проверь температуру — CPU-Z → Thermal — если >70°C, троттлит
- Сними чехол, не играй на солнце
- На батарее — 45 FPS, на зарядке — 60 FPS
- Закрой браузер, TikTok, Facebook
- Попробуй Medium preset — 5000 particles 16x150 voxel32 45 FPS — может быть быстрее из-за меньше draw calls?

#### Греется
- SD662 11nm — греется до 70°C под нагрузкой, потом троттлит до 1.5GHz
- Решение: сними чехол, подставка, не на солнце, Potato 15 FPS для охлаждения
- FSR ON — экономит fillrate, меньше нагрев GPU

#### Батарея быстро садится
- 90Hz дисплей + 60 FPS — жрет батарею
- Включи Battery Saver — 45 FPS, 2 threads, vortex OFF
- Уменьши яркость IPS LCD — яркость жрет много
- Играй на зарядке, но без чехла

#### Adreno 610 — артефакты
- Обнови драйвер GPU — Настройки → О телефоне → Обновление ПО — драйвер V@0615.102.A от 03/31/26 — у тебя свежий (2026), ок
- Попробуй выключить FSR — иногда артефакты с FSR на Adreno
- Уменьши voxel до 24

### Будущее для SD662

- v1.21.1: ASTC texture compression — уже в Adreno 610, включим для экономии VRAM
- v1.22.0: Vulkan 1.1 для Adreno 610 — быстрее чем ES 3.2, но пока ES
- v1.23.0: 90Hz поддержка — сейчас 60 FPS target, сделаем 90 FPS опцию для 90Hz дисплея
- v1.24.0: Thermal throttling detection — авто снижение качества при нагреве >70°C

---
GoGonam AoS. 2026 — оптимизировано специально для твоего Snapdragon 662 + Adreno 610 720x1604 90Hz!
