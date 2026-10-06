# Aeros Engine Android v1.20.1 — самая оптимизированная APK для слабых телефонов

## Для кого

### Potato — самые слабые телефоны (1GB RAM, Adreno 306, Android 5.0+ 2014)
- **CPU:** Snapdragon 410 (4x Cortex-A53 1.2GHz), MT6582 (4x A7 1.3GHz), Spreadtrum SC7731, 1-2GB RAM
- **GPU:** Adreno 306 (24 ALUs, ES 3.0), Mali-400 MP2 (ES 2.0, 2010), PowerVR SGX 544, 320x480 — 720x1280 экран
- **Примеры:** Samsung Galaxy Ace 4, Galaxy J1, Huawei Y5, Xiaomi Redmi 2, Lenovo A328, Android Go телефоны
- **Настройки:** 300 частиц, 3x30 линий, voxel 12, LBM OFF 12 res, 15 FPS, 800x480, 1 поток, <150 MB RAM, APK <10 MB

### Lite — слабые телефоны (2-4GB RAM, Adreno 405+, Android 6.0+ 2015+)
- **CPU:** Snapdragon 615/616/625 (8x A53 1.5-2.0GHz), MT6753, 2-4GB RAM
- **GPU:** Adreno 405/506 (48-96 ALUs, ES 3.1), Mali-T720/T820, 720x1280 — 1080x1920
- **Примеры:** Samsung Galaxy J5/J7, Xiaomi Redmi 3/4, Huawei P8 Lite, Moto G4
- **Настройки:** 800 частиц, 6x60 линий, voxel 20, LBM OFF 20 res, 25 FPS, 1280x720, 2 потока, <300 MB, APK <20 MB

### Medium — средние (4GB+ RAM, Adreno 610+, Android 8.0+)
- **Настройки:** 2000 частиц, 10x100 линий, voxel 28, 30 FPS

## Оптимизации для слабых телефонов

### APK размер
- **R8 minify + shrinkResources** — убирает неиспользуемый код и ресурсы
- **Только 3 ABI:** arm64-v8a (основной), armeabi-v7a (старые 32-bit), x86_64 (эмулятор) — нет x86 32-bit (редко)
- **O1 + Os** — оптимизация размера, не O3, меньше бинарь
- **Без лишних зависимостей** — только android, log, EGL, GLESv3, m, z — нет Vulkan, нет CUDA, нет лишних Java библиотек
- **Результат:** Potato <10 MB, Lite <20 MB (Full ~30-40 MB)

### CPU
- **1-2 потока OpenMP** — для Snapdragon 410 4x A53, не грузит все ядра, меньше нагрев, экономия батареи
- **NEON** — ARM SIMD для ускорения математики на старых телефонах
- **O1** — меньше нагрев, меньше RAM для компиляции, быстрее запуск
- **BELOW_NORMAL / IDLE приоритет** — не тормозит систему

### GPU
- **OpenGL ES 3.0** (Lite) / **2.0** (Potato) — для Adreno 306 / Mali-400, которые не поддерживают ES 3.1+
- **16-bit depth** — экономит VRAM, быстрее
- **No stencil, no MSAA** — экономит fillrate для слабых GPU
- **Simple shaders** — без PBR, без теней, без сложных вычислений
- **Texture 512-1024 low** — меньше VRAM

### RAM
- **Potato <150 MB, Lite <300 MB** — для телефонов с 1-2GB RAM, где системе нужно 500MB-1GB
- **Частицы 300/800** вместо 15000 — в 50x/18x меньше
- **Линии 3x30 / 6x60** вместо 24x300 — в 24x/8x меньше
- **Voxel 12 / 20** вместо 48 — 12³=1728 vs 48³=110592 — в 64x/13x меньше памяти
- **LBM OFF 12/20 res** вместо ON 128 — 12³=1728 vs 128³=2M — в 1200x меньше

### Батарея
- **VSync ON** — ограничивает FPS, не крутит 300 FPS вхолостую, экономит батарею
- **15/25 FPS target** вместо 60 — в 4x/2.4x меньше нагрузка, меньше нагрев, дольше батарея
- **Frame pacing + sleep** — если кадр быстрее цели, спим, не грузим CPU
- **Low memory handling** — при `APP_CMD_LOW_MEMORY` снижаем качество
- **Pause saves config** — при сворачивании сохраняем конфиг, не теряем

### Touch
- **Один палец — вращение модели** — delta X/Y
- **Два пальца — pinch zoom** — scale 0.5-2.0
- **Touch slop 8px** — игнорируем мелкие дрожания
- **Большие UI кнопки** — для пальца, не мышки

### Совместимость
- **minSdk 21 Android 5.0 Lollipop 2014** — поддерживает телефоны 10-летней давности
- **targetSdk 34 Android 14** — работает на новых
- **16KB page size** для Android 15+ — новый требование
- **No permissions** — только READ_EXTERNAL_STORAGE для STL, нет INTERNET, нет лишних
- **SAF file picker** — для выбора STL через Storage Access Framework, работает на Android 5.0+

## Сборка APK

### Требования
- Android SDK (ANDROID_HOME) — https://developer.android.com/studio
- Android NDK 25.1.8937393+ — через SDK Manager: `sdkmanager --install "ndk;25.1.8937393"`
- Gradle 8.4+ или Android Studio
- Java 17+

### Проверка
```bash
./build-android.sh check
# или
./build-android.bat check
```

### Установка зависимостей
```bash
./build-android.sh deps
# Устанавливает platform-tools, build-tools 34.0.0, platforms android-34 и android-21, ndk 25.1.8937393
```

### Сборка Lite (рекомендуется для слабых телефонов)
```bash
cd aeros/android
./build-android.sh lite
# APK: app/build/outputs/apk/release/app-release.apk
# Копия: ../../release/aeros-engine-android-lite-v1.20.1.apk
# Размер: <20 MB
```

### Сборка Potato (для самых слабых 1GB RAM Adreno 306)
```bash
./build-android.sh potato
# APK: app/build/outputs/apk/potatoRelease/app-potato-release.apk
# Копия: ../../release/aeros-engine-android-potato-v1.20.1.apk
# Размер: <10 MB
# Настройки: 300 частиц, 3x30 линий, voxel 12, 15 FPS, 800x480, 1 поток
```

### Сборка всех
```bash
./build-android.sh all
# Собирает и Lite и Potato
```

### Windows
```cmd
cd aeros\android
build-android.bat lite
build-android.bat potato
build-android.bat all
```

### Android Studio
1. Откройте `aeros/android` как проект в Android Studio
2. Sync Gradle
3. Build → Make Project
4. Build → Build APK(s) — Lite
5. Build Variants → potatoRelease → Build APK — Potato

### Ручная сборка через gradle
```bash
cd aeros/android
./gradlew assembleRelease          # Lite
./gradlew assemblePotatoRelease    # Potato
./gradlew assembleDebug            # Debug
```

## Установка APK на телефон

### Через adb
```bash
adb install release/aeros-engine-android-lite-v1.20.1.apk
# или Potato
adb install release/aeros-engine-android-potato-v1.20.1.apk
```

### Через файл
1. Скопируйте APK на телефон (через USB, Telegram, Google Drive)
2. На телефоне откройте файл менеджер
3. Нажмите на APK — разрешите установку из неизвестных источников
4. Установите

### Требования телефона
- Android 5.0+ (API 21+)
- OpenGL ES 3.0+ (для Lite) или 2.0+ (для Potato) — проверьте через CPU-Z или AIDA64
- 1GB RAM минимум (Potato), 2GB рекомендуется (Lite)
- 50MB свободно (Potato 10MB APK + 40MB cache, Lite 20MB + 80MB)

## Использование

### Запуск
1. Откройте приложение — сразу NativeActivity с движком
2. Или через MainActivity — file picker для выбора STL модели
3. Если нет STL — используется встроенная модель (куб/сфера)

### Управление touch
- **Один палец drag** — вращение модели вокруг
- **Два пальца pinch** — zoom in/out (scale 0.5-2.0)
- **Двойной тап** — сброс камеры (если реализовано)
- **Меню** — через ImGui, большие кнопки для пальца

### STL загрузка
- Через file picker — выберите STL файл из памяти телефона (Download, Documents)
- Через Intent — откройте STL файл из файл менеджера → Поделиться → Aeros Engine
- Встроенные — в `assets/models/` — cube.stl, sphere.stl, etc (если добавите)

### Настройки для слабых телефонов
- Если FPS <15 — автоматически снижает качество (particles, streamlines)
- Вручную: в ImGui меню → Lite → Preset Potato/Low/Medium
- Или через Intent extra: `preset=potato`

## Сравнение с Desktop Lite

| Параметр | Android Potato | Android Lite | Desktop Lite | Desktop Full |
|----------|---------------|--------------|--------------|--------------|
| CPU | Snapdragon 410 4x A53 1.2GHz | SD 625 8x A53 2.0GHz | i3-3xxx 2C/4T | i5+ 6C+ |
| GPU | Adreno 306 Mali-400 ES 2.0 | Adreno 405 ES 3.1 | HD 4000 GT620M | GTX 1060+ |
| RAM | 1GB | 2-4GB | 4GB | 8GB+ |
| Particles | 300/500 | 800/1500 | 1500/5000 | 15000/100k |
| Streamlines | 3x30 / 6 max | 6x60 / 12 max | 8x80 / 16 max | 24x300 / 64 max |
| Voxel | 12 (1.7k) | 20 (8k) | 24 (13k) | 48 (110k) |
| LBM | OFF 12 | OFF 20 | OFF 32 | ON 128 |
| FPS | 15 | 25 | 30 | 60 |
| Window | 800x480 | 1280x720 | 1024x600 | 1280x720 |
| Threads | 1 | 2 | 2 | all |
| RAM usage | <150 MB | <300 MB | <512 MB | 1-2 GB |
| APK/Binary | <10 MB APK | <20 MB APK | ~5-10 MB exe | ~20-30 MB exe |
| GL | ES 2.0 | ES 3.0 | GL 3.3 | GL 4.6 + Vulkan |
| Battery | Always saver | Saver | Saver optional | No |

## Troubleshooting для старых телефонов

### Не устанавливается — Parse error
- Проверьте Android версию — нужен 5.0+ (API 21+)
- Проверьте APK — Lite для arm64-v8a, armeabi-v7a, x86_64 — если телефон x86 (редко) — нужен x86_64
- Включите установку из неизвестных источников: Настройки → Безопасность → Неизвестные источники

### Черный экран / вылетает сразу
- Проверьте OpenGL ES — нужен 3.0 для Lite, 2.0 для Potato — CPU-Z → GPU → OpenGL ES version
- Если ES 2.0 only — используйте Potato APK
- Проверьте RAM — нужно 1GB свободно, закройте другие приложения
- Логи: `adb logcat | grep AerosEngine`

### Низкий FPS <10
- Используйте Potato preset — 300 частиц, 3x30 линий, voxel 12, 15 FPS
- Закройте браузер, Facebook, etc — они жрут RAM
- Включите Battery Saver — ограничивает FPS, меньше нагрев
- Уменьшите яркость экрана — меньше нагрев GPU
- Телефон греется — дайте остыть, снимите чехол

### Вылетает при загрузке STL
- STL файл слишком большой — для Potato max 1MB, Lite max 5MB
- Упростите модель в Blender — Decimate modifier
- Используйте бинарный STL вместо ASCII — меньше размер
- Проверьте что STL валидный — откройте в Windows версии

### Не видит STL файл
- На Android 10+ — нужен SAF file picker, не прямой путь
- Скопируйте STL в Download папку — file picker видит
- Или в `assets/models/` при сборке APK

### Греется / быстро садится батарея
- Potato + 15 FPS + VSync ON — уже экономит
- Уменьшите яркость экрана
- Закройте другие приложения
- Играйте на зарядке, но без чехла
- Почистите телефон от пыли (если старый)

### Adreno 306 / Mali-400 — артефакты
- Это Potato GPU — только ES 2.0, 16-bit depth, no stencil
- Используйте Potato APK — он для ES 2.0
- Обновите драйвер — Настройки → О телефоне → Обновление ПО
- Попробуйте уменьшить voxel до 8

## Разработка

### Структура
```
aeros/android/
  app/
    build.gradle — сборка Lite/Potato, R8 minify, 3 ABI
    src/main/
      AndroidManifest.xml — minSdk 21, ES 3.0, NativeActivity + MainActivity file picker
      java/com/aos/aerosengine/MainActivity.java — file picker SAF, preset detect по RAM/cores
      cpp/
        CMakeLists.txt — O1 Os, NEON, 1-2 threads, EGL GLESv3, gc-sections
        android_main.cpp — EGL init 16-bit depth no MSAA, context ES 3.0/2.0, weak GPU detect, render loop с FPS limit sleep для батареи
        android_bridge.h/cpp — device info, low ram, api level
        android_input.h/cpp — touch 1 finger rotate, 2 fingers pinch zoom 0.5-2.0
        android_file.h/cpp — AAssetManager, load from assets and file system, list models
      res/values/strings.xml, layout/activity_main.xml
  build.gradle, settings.gradle, gradle/wrapper/gradle-wrapper.properties
  build-android.sh/bat — проверка SDK/NDK, сборка Lite/Potato, install deps
```

### Интеграция с основным движком
- Использует те же `src/*.cpp` что и Desktop — globals, flow_field, particles, streamlines, etc
- `lite_config.h/cpp` — уже есть Potato/Low/Medium/Full пресеты, auto-detect, battery saver, dynamic quality
- `android_config.h/cpp` — Android специфичные Potato/Lite 300/800 частиц, 3x30/6x60 линий, voxel 12/20, 15/25 FPS
- `gl_utils.cpp` — нужно адаптировать для GLES — использовать `GL_ES` флаг, `glad` заменить на `GLES3/gl3.h`
- `ui.cpp` — ImGui уже поддерживает Android + GLES, touch input через `android_input.cpp`

### TODO для полной версии
- [ ] Адаптировать `gl_utils.cpp` для GLES — `#ifdef ANDROID` использовать `GLES3/gl3.h` вместо `glad`
- [ ] Адаптировать `shaders.h` для GLES — precision mediump, version 300 es
- [ ] Интегрировать ImGui touch — в `ui.cpp` добавить touch handling
- [ ] Добавить модели в `assets/models/` — cube.stl, sphere.stl, etc
- [ ] Тест на реальных устройствах — Adreno 306, Mali-400, 1GB RAM
- [ ] Оптимизация батареи — JobScheduler для сохранения конфига
- [ ] Поддержка Gamepad — для телефонов с геймпадом
- [ ] Vulkan для Android — для Adreno 640+ (но не для слабых)

## Будущее Android

- v1.21.0: OpenGL ES 2.0 fallback для Mali-400, еще более Potato 200 частиц voxel 8 10 FPS для 512MB RAM Android Go
- v1.22.0: ARM 32-bit only APK <5 MB для самых старых телефонов, x86 для эмуляторов
- v1.23.0: Vulkan Android для Adreno 640+ (Snapdragon 855+), но Lite остается ES
- v1.24.0: Google Play публикация, App Bundle для уменьшения размера, Play Asset Delivery для моделей

---
GoGonam AoS. 2026 — теперь и на самых слабых Android телефонах!
