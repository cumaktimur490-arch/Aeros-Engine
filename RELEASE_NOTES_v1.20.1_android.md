# Aeros Engine v1.20.1 Android — самая оптимизированная APK для слабых телефонов

## 🤖 Android версия — НОВОЕ! Для самых слабых телефонов

### Для кого

#### Potato — самые слабые телефоны 1GB RAM Adreno 306 Android 5.0+ 2014
- **CPU:** Snapdragon 410 4x A53 1.2GHz, MT6582 4x A7 1.3GHz, Spreadtrum SC7731, 1-2GB RAM
- **GPU:** Adreno 306 24 ALUs ES 3.0, Mali-400 MP2 ES 2.0 2010, PowerVR SGX 544, 320x480 — 720x1280
- **Примеры:** Samsung Galaxy Ace 4, J1, Huawei Y5, Xiaomi Redmi 2, Lenovo A328, Android Go
- **Настройки:** 300 частиц max 500, 3x30 линий max 6x60, voxel 12, LBM OFF 12 res, 15 FPS, 800x480, 1 поток, <150 MB RAM, APK <10 MB

#### Lite — слабые телефоны 2-4GB RAM Adreno 405+ Android 6.0+ 2015+
- **CPU:** Snapdragon 615/616/625 8x A53 1.5-2.0GHz, MT6753, 2-4GB RAM
- **GPU:** Adreno 405/506 48-96 ALUs ES 3.1, Mali-T720/T820, 720x1280 — 1080x1920
- **Примеры:** Galaxy J5/J7, Redmi 3/4, Huawei P8 Lite, Moto G4
- **Настройки:** 800 частиц max 1500, 6x60 линий max 12x120, voxel 20, LBM OFF 20 res, 25 FPS, 1280x720, 2 потока, <300 MB, APK <20 MB

### Оптимизации для слабых телефонов

#### APK размер — самый маленький
- R8 minify + shrinkResources — убирает неиспользуемый код
- Только 3 ABI: arm64-v8a (основной), armeabi-v7a (старые 32-bit), x86_64 (эмулятор)
- O1 + Os — оптимизация размера, не O3
- Без лишних зависимостей — только android, log, EGL, GLESv3, m, z
- **Potato <10 MB, Lite <20 MB** (Full ~30-40 MB)

#### CPU — 1-2 потока
- Snapdragon 410 4x A53 — не грузит все ядра, меньше нагрев, экономия батареи
- NEON — ARM SIMD для ускорения
- O1 — меньше нагрев, быстрее запуск
- BELOW_NORMAL / IDLE приоритет

#### GPU — ES 3.0 / 2.0
- Adreno 306 / Mali-400 — только ES 2.0/3.0, не 3.1+
- 16-bit depth — экономит VRAM
- No stencil, no MSAA — экономит fillrate
- Simple shaders — без PBR, без теней

#### RAM — <150/300 MB
- Частицы 300/800 вместо 15000 — в 50x/18x меньше
- Линии 3x30 / 6x60 вместо 24x300 — в 24x/8x меньше
- Voxel 12/20 вместо 48 — 12³=1728 vs 48³=110592 — в 64x/13x меньше
- LBM OFF 12/20 res вместо ON 128 — 12³=1728 vs 128³=2M — в 1200x меньше

#### Батарея
- VSync ON — не крутит 300 FPS вхолостую
- 15/25 FPS target вместо 60 — в 4x/2.4x меньше нагрузка
- Frame pacing + sleep — если кадр быстрее цели, спим
- Low memory handling — при APP_CMD_LOW_MEMORY снижаем качество
- Pause saves config

#### Touch
- Один палец drag — вращение модели
- Два пальца pinch — zoom 0.5-2.0
- Touch slop 8px — игнорируем дрожания
- Большие UI кнопки для пальца

#### Совместимость
- minSdk 21 Android 5.0 Lollipop 2014 — поддерживает телефоны 10-летней давности
- targetSdk 34 Android 14 — работает на новых
- 16KB page size для Android 15+
- No permissions — только READ_EXTERNAL_STORAGE для STL
- SAF file picker — Android 5.0+

### Файлы Android

#### Проект
- `aeros/android/app/src/main/AndroidManifest.xml` — minSdk 21 ES 3.0 NativeActivity + MainActivity file picker, no permissions
- `app/build.gradle` — Lite/Potato build types, R8 minify shrinkResources, 3 ABI, O1 Os, NEON, 16KB page
- `build.gradle`, `settings.gradle`, `gradle/wrapper/gradle-wrapper.properties`
- `app/proguard-rules.pro` — minify для маленького APK
- `app/src/main/res/` — strings.xml, layout, mipmap icons

#### C++ JNI
- `app/src/main/cpp/CMakeLists.txt` — O1 Os, NEON, 1-2 threads, EGL GLESv3, gc-sections, icf, native_app_glue from NDK
- `android_main.cpp` — EGL init 16-bit depth no MSAA, context ES 3.0/2.0 fallback, weak GPU detect isWeakGPU, render loop FPS limit sleep для батареи, low memory handling, printLiteSystemInfo
- `android_bridge.h/cpp` — device info via __system_property_get, low ram, api level
- `android_input.h/cpp` — touch 1 finger rotate delta, 2 fingers pinch zoom 0.5-2.0, touch slop
- `android_file.h/cpp` — AAssetManager load from assets and file system, list models, copy asset to cache

#### Java
- `java/com/aos/aerosengine/MainActivity.java` — SAF file picker ACTION_OPEN_DOCUMENT mimeTypes, copyUriToCache 4KB buffer для слабых, launchNativeActivity with preset detect via ActivityManager totalMem cores lowMemory

#### Config
- `src/android_config.h/cpp` — ANDROID_POTATO 300 particles 3x30 voxel12 15 FPS 800x480 1 thread 150MB, ANDROID_LITE 800 particles 6x60 voxel20 25 FPS 1280x720 2 threads 300MB, isAndroidLowRamDevice, isAndroidPotatoDevice, applyAndroidPotato/LiteDefaults, getAndroidDeviceInfo

#### Build scripts
- `android/build-android.sh` — check_sdk ANDROID_HOME NDK, build_apk Lite/Potato via gradlew assembleRelease/assemblePotatoRelease, find APK copy to release, install_deps sdkmanager
- `android/build-android.bat` — Windows версия
- `android/README.md` — полный гайд для слабых телефонов, troubleshooting, сравнение

### Сборка APK

```bash
cd aeros/android
./build-android.sh check          # проверка SDK/NDK
./build-android.sh deps           # установка NDK build-tools platforms
./build-android.sh lite           # Lite APK <20 MB 2-4GB RAM
./build-android.sh potato         # Potato APK <10 MB 1GB RAM Adreno 306
./build-android.sh all            # оба
```

```cmd
cd aeros\android
build-android.bat lite
build-android.bat potato
```

Android Studio: Open `aeros/android` → Sync Gradle → Build APK

### Установка на телефон

```bash
adb install release/aeros-engine-android-lite-v1.20.1.apk
adb install release/aeros-engine-android-potato-v1.20.1.apk
```

Или скопируйте APK на телефон и откройте через файл менеджер — разрешите установку из неизвестных источников.

### Использование

- Запуск — сразу NativeActivity с движком, или file picker для STL
- Touch: один палец drag — вращение, два пальца pinch — zoom 0.5-2.0
- STL: через file picker из Download/Documents, или Intent Share → Aeros Engine, или встроенные в assets/models/
- Если FPS <15 — автоматически снижает качество, или вручную Preset Potato

### Сравнение

| | Android Potato | Android Lite | Desktop Lite | Desktop Full |
|---|---|---|---|---|
| CPU | SD 410 4x A53 1.2GHz | SD 625 8x A53 2GHz | i3-3xxx 2C/4T | i5+ 6C+ |
| GPU | Adreno 306 Mali-400 ES2.0 | Adreno 405 ES3.1 | HD4000 GT620M | GTX1060+ |
| RAM | 1GB | 2-4GB | 4GB | 8GB+ |
| Particles | 300/500 | 800/1500 | 1500/5000 | 15000/100k |
| Streamlines | 3x30 /6 | 6x60 /12 | 8x80 /16 | 24x300 /64 |
| Voxel | 12 1.7k | 20 8k | 24 13k | 48 110k |
| LBM | OFF 12 | OFF 20 | OFF 32 | ON 128 |
| FPS | 15 | 25 | 30 | 60 |
| Window | 800x480 | 1280x720 | 1024x600 | 1280x720 |
| Threads | 1 | 2 | 2 | all |
| RAM | <150 MB | <300 MB | <512 MB | 1-2 GB |
| APK/Binary | <10 MB | <20 MB | ~5-10 MB | ~20-30 MB |
| GL | ES2.0 | ES3.0 | GL3.3 | GL4.6+Vulkan |

### Troubleshooting

- **Parse error** — нужен Android 5.0+ API21+, включите неизвестные источники
- **Черный экран / вылетает** — проверьте ES version через CPU-Z, если ES2.0 only — используйте Potato APK, проверьте RAM 1GB свободно, логи `adb logcat | grep AerosEngine`
- **FPS <10** — Potato preset 300 particles 3x30 voxel12 15 FPS, закройте браузер Facebook, Battery Saver, уменьшите яркость, дайте остыть
- **Вылетает при STL** — STL слишком большой, Potato max 1MB Lite max 5MB, упростите в Blender Decimate, используйте бинарный STL
- **Не видит STL** — Android 10+ SAF, скопируйте в Download, или в assets/models/ при сборке
- **Греется / батарея** — Potato 15 FPS VSync ON, уменьшите яркость, закройте другие, играйте на зарядке без чехла
- **Adreno 306 / Mali-400 артефакты** — Potato GPU ES2.0 16-bit depth no stencil, используйте Potato APK, обновите ПО

### Что нового v1.20.1 Android

- Android проект полностью — самая оптимизированная APK для слабых телефонов
- Potato <10 MB для 1GB RAM Adreno 306 Android 5.0+ 2014 — 300 частиц 3x30 voxel12 15 FPS 800x480 1 поток
- Lite <20 MB для 2-4GB RAM Adreno 405+ — 800 частиц 6x60 voxel20 25 FPS 1280x720 2 потока
- minSdk 21 Android 5.0 Lollipop — поддерживает 10-летние телефоны
- ES 3.0 / 2.0 fallback, 16-bit depth no MSAA no stencil, simple shaders no PBR shadows
- R8 minify shrinkResources 3 ABI arm64-v8a armeabi-v7a x86_64 O1 Os NEON gc-sections
- EGL init weak GPU detect isWeakGPU, render loop FPS limit sleep для батареи, low memory handling
- Touch 1 finger rotate 2 fingers pinch zoom 0.5-2.0, big UI buttons
- SAF file picker, copyUriToCache 4KB buffer, preset detect via RAM cores lowMemory
- android_config.h/cpp Android Potato/Lite defaults
- build-android.sh/bat check deps lite potato all clean help
- README.md полный гайд troubleshooting сравнение

---
GoGonam AoS. 2026 — теперь и на самых слабых Android телефонах!
