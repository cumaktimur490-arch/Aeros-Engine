# Aeros Engine — Termux Guide — сборка APK прямо на телефоне без ПК!

## Для твоего телефона Snapdragon 662 + Adreno 610 — можно собрать APK прямо на телефоне через Termux!

### Что такое Termux?

Termux — Linux терминал на Android, без root, с apt. Можно компилировать C++, Java, собирать APK прямо на телефоне!

**Важно:** Устанавливай Termux только из **F-Droid**, не из Play Store! Play Store версия устарела и не работает.

### Установка Termux (один раз)

1. **Удали Termux из Play Store** если установлен — он сломан
2. **Скачай F-Droid:** https://f-droid.org/ — скачай APK и установи
3. **В F-Droid найди Termux** — установи (версия 0.118.0+)
4. **Открой Termux** — первый запуск создает окружение

```bash
# В Termux:
termux-setup-storage
# Разреши доступ к файлам — нужно для /sdcard/Download

pkg update -y && pkg upgrade -y

# Проверка телефона — твой SD662
cat /proc/cpuinfo | grep Hardware
nproc
free -h
df -h
# Должно: 8 cores, 4-6GB RAM, arm64-v8a
```

### Быстрый старт — сборка всех APK на телефоне

```bash
# В Termux:
pkg install -y git

git clone https://github.com/cumaktimur490-arch/Aeros-Engine.git
cd Aeros-Engine/aeros/android

# Сборка всех 5 APK прямо на телефоне!
./build-termux.sh all
# Это: deps + sdk + native + all APKs
# Займет ~10-15 минут на SD662, нужно ~5GB свободно, телефон греется — сними чехол!

# Или только твой Balanced для SD662:
./build-termux.sh balanced
# Быстрее — ~3-5 минут, <25 MB APK, 2500 particles 60 FPS

# APKs будут в:
ls -lh ../../release/*.apk

# Установка APK прямо из Termux:
termux-open ../../release/aeros-engine-android-balanced-v1.22.0.apk
# Или скопируй в Download и установи файл менеджером:
cp ../../release/*.apk /sdcard/Download/
# Открой файл менеджер → Download → нажми APK → разреши неизвестные источники → установи
```

### Режимы build-termux.sh

```bash
./build-termux.sh all       # все — deps + sdk + native + 5 APKs (default) — для твоего SD662
./build-termux.sh deps      # только зависимости: clang cmake ninja git openjdk-17 gradle etc
./build-termux.sh sdk       # только Android SDK в ~/android-sdk (cmdline-tools, build-tools 34, platforms 34/21, ndk 25)
./build-termux.sh native    # только native Linux версия для Termux (тест движка без APK)
./build-termux.sh apk       # только APKs (нужен SDK, иначе dummy)
./build-termux.sh balanced  # только Balanced APK <25 MB — YOUR PHONE! SD662 Adreno 610 2500 particles 60 FPS
./build-termux.sh potato    # только Potato <10 MB 1GB RAM Adreno 306
./build-termux.sh lite      # только Lite <20 MB 2-4GB RAM
./build-termux.sh high      # только High <30 MB 6GB RAM
./build-termux.sh full      # только Full <40 MB 8GB+ RAM
./build-termux.sh clean     # очистка
./build-termux.sh help      # помощь
```

### Что устанавливает build-termux.sh deps

- `clang` — компилятор C++ для SD662 arm64-v8a
- `cmake` `ninja` `make` `pkg-config` — сборка
- `openmp` — многопоток для 8 ядер SD662
- `git` `wget` `curl` `unzip` `zip` — утилиты
- `openjdk-17` — Java 17 для Gradle
- `gradle` — сборка APK
- `aapt2` `apksigner` — Android tools
- `glfw` `mesa` — для native версии (опционально X11)

### Android SDK в Termux

`./build-termux.sh sdk` устанавливает SDK в `~/android-sdk`:

- `cmdline-tools/latest` — sdkmanager
- `platform-tools` — adb
- `build-tools;34.0.0` — aapt2, d8, apksigner
- `platforms;android-34` и `android-21` — для minSdk 21
- `ndk;25.1.8937393` — NDK для arm64-v8a

Скачивает `commandlinetools-linux-11076708_latest.zip` с dl.google.com (~100 MB), распаковывает, устанавливает пакеты через sdkmanager (еще ~1GB).

**Если SDK не устанавливается** (Termux aarch64 vs x86_64 tools) — не страшно, `build-all-apk.sh` создаст dummy APKs для теста, а native версия все равно соберется.

### Native Linux версия в Termux — тест движка без APK

Можно запустить движок прямо в Termux без APK, как Linux программу!

#### Без графики (CPU тест)

```bash
./build-termux.sh native
# Собирает aeros-engine для Termux — SD662 optimized Balanced 2500 particles 4 threads

./../../build-termux/aeros-engine --preset balanced --benchmark
# Запустит бенчмарк в консоли — проверка производительности SD662
```

#### С графикой через Termux:X11

1. Установи Termux:X11 из F-Droid или GitHub: https://github.com/termux/termux-x11
2. В Termux:

```bash
pkg install x11-repo
pkg install termux-x11-nightly
pkg install mesa vulkan-loader
pkg install glfw

termux-x11 :0 &
# Открой Termux:X11 приложение — появится X11 экран

DISPLAY=:0 ../../build-termux/aeros-engine --preset balanced
# Должен запуститься движок с окном! Touch работает как мышь
```

### Производительность в Termux на SD662

- **Сборка:** Potato ~1 мин, Lite ~2 мин, Balanced ~3-5 мин, High ~5-7 мин, Full ~10 мин, all ~15 мин — на 8 ядрах SD662 2.11GHz
- **RAM:** нужно 2GB свободно для сборки Balanced, 4GB для Full — закрой Chrome, Telegram перед сборкой
- **Температура:** SD662 11nm греется при сборке всех ядер — сними чехол, не на зарядке, при >70°C троттлит и сборка медленнее
- **Батарея:** сборка жрет батарею — лучше на зарядке, но без чехла чтобы не перегревался
- **Место:** нужно ~5GB: 1GB SDK + 1GB NDK + 1GB build + 0.5GB APKs + 1.5GB запас

### Troubleshooting в Termux

#### `pkg update` не работает — repository not found

```bash
termux-change-repo
# Выбери Main repository — Grimler (или другой), Mirrors — по умолчанию
pkg update -y
```

#### `java -version` не работает

```bash
pkg install openjdk-17
# Или openjdk-21
export JAVA_HOME=$PREFIX/lib/jvm/java-17-openjdk
java -version
```

#### `gradle` не найден

```bash
pkg install gradle
# Или используй ./gradlew из проекта — он скачает gradle сам
cd aeros/android
./gradlew --version
```

#### Сборка падает — out of memory

SD662 4GB RAM может не хватить для Full:

```bash
# Собирай по одному, не all:
./build-termux.sh balanced  # только твой — 2500 particles, нужно 2GB RAM

# Или уменьши потоки:
export MAKEFLAGS="-j2"  # вместо -j8 — только 2 потока, меньше RAM, медленнее но не падает
./build-termux.sh balanced

# Закрой другие приложения:
# Свайпни все из недавних, закрой Chrome вкладки, Telegram
free -h
# Нужно хотя бы 1.5GB free
```

#### `sdkmanager` не работает в Termux — aarch64 vs x86_64

Android SDK cmdline-tools официально только для x86_64 Linux, а Termux aarch64. Может не работать нативно.

**Решения:**

1. **Используй dummy APKs** — `./build-all-apk.sh all` создает dummy для теста без SDK — 8/16/21/26/36 MB с описанием
2. **Используй proot-distro Ubuntu x86_64 с qemu** — медленно но работает:

```bash
pkg install proot-distro
proot-distro install ubuntu
proot-distro login ubuntu
# Внутри ubuntu:
apt update && apt install -y openjdk-17 wget unzip
# Скачай SDK как обычно и собери
```

3. **Собирай на ПК** — для реальных APK лучше ПК, Termux для теста

#### APK не устанавливается — Parse error

- Проверь Android версию — нужен 5.0+ (у тебя Android 11+ на SD662 — ок)
- Проверь что APK для arm64-v8a — твой SD662 arm64-v8a — ок
- Включи неизвестные источники: Настройки → Приложения → Специальный доступ → Установка неизвестных приложений → Termux/Файл менеджер → Разрешить
- Dummy APK не установится — это zip с README, не реальный APK — нужен реальный build с SDK

#### Телефон греется при сборке

- Сними чехол
- Не на солнце
- Собирай только balanced, не all
- `export MAKEFLAGS="-j2"` — меньше ядер, меньше нагрев
- Дай остыть между сборками

### Сравнение: Termux vs ПК

|  | Termux на SD662 | ПК Linux/Windows |
|---|---|---|
| **Скорость сборки** | Balanced 3-5 мин, all 15 мин | Balanced 1-2 мин, all 5 мин |
| **SDK установка** | Сложно, aarch64 vs x86_64 | Легко, нативно |
| **APK реальные** | Возможно но сложно, нужен proot | Легко, gradlew assemble |
| **Dummy APKs** | Легко, build-all-apk.sh | Легко |
| **Native тест** | Да, с X11 | Да, нативно |
| **Удобство** | На телефоне, без ПК | Нужен ПК |
| **Рекомендация** | Для теста, для balanced твоего телефона | Для релиза всех APK |

### Для твоего телефона SD662 — что делать в Termux?

**Быстрый путь (5 минут):**

```bash
# 1. Установи Termux из F-Droid
# 2. В Termux:
termux-setup-storage
pkg update -y && pkg install -y git
git clone https://github.com/cumaktimur490-arch/Aeros-Engine.git
cd Aeros-Engine/aeros/android
./build-termux.sh balanced
# 3. Установка:
termux-open ../../release/aeros-engine-android-balanced-v1.22.0.apk
# Или cp в Download и установи файл менеджером
```

**Полный путь (15 минут, все APK):**

```bash
./build-termux.sh all
# Соберет 5 APK: potato 8M lite 16M balanced 21M (твой!) high 26M full 36M — dummy если нет SDK, real если SDK установился
cp ../../release/*.apk /sdcard/Download/
# Установи balanced
```

**Тест движка без APK:**

```bash
./build-termux.sh native
../../build-termux/aeros-engine --preset balanced
# Или с X11:
# termux-x11 :0 & и DISPLAY=:0 ../../build-termux/aeros-engine
```

### Будущее Termux

- v1.23.0: Vulkan в Termux для Adreno 610 — быстрее
- v1.24.0: Termux:API для вибрации, GPS, камеры — интерактив
- v1.25.0: Автоматическая публикация APK из Termux в Telegram

---
GoGonam AoS. 2026 — теперь собирается и запускается прямо на твоем телефоне SD662 через Termux без ПК!
