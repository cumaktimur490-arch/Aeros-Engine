# Aeros Engine — Установщики

Эта папка содержит скрипты для сборки установщиков и портативных версий.

## 📦 Что собирается

| Файл | Описание |
|------|----------|
| `Aeros-Engine-Setup-x64-vX.Y.Z.exe` | Полный установщик 64-bit |
| `Aeros-Engine-Setup-x86-vX.Y.Z.exe` | Полный установщик 32-bit |
| `Aeros-Engine-Setup-arm64-vX.Y.Z.exe` | Полный установщик ARM64 |
| `Aeros-Engine-Portable-x64-vX.Y.Z.zip` | Портативная 64-bit (без установки) |
| `Aeros-Engine-Portable-x86-vX.Y.Z.zip` | Портативная 32-bit |
| `Aeros-Engine-Portable-arm64-vX.Y.Z.zip` | Портативная ARM64 |
| `Aeros-Engine-Update-x64-vX.Y.Z.exe` | Обновление для установленной 64-bit версии |
| `Aeros-Engine-Update-x86-vX.Y.Z.exe` | Обновление для установленной 32-bit версии |
| `Aeros-Engine-Update-arm64-vX.Y.Z.exe` | Обновление для установленной ARM64 версии |

## 🛠 Требования для сборки

- **Windows 10/11**
- **Visual Studio 2022** с C++ workload
- **CUDA Toolkit 11+** (для сборки бинарей)
- **Inno Setup 6** — для установщиков: https://jrsoftware.org/isinfo.php
- **GLFW** — уже в `libs/`, для x86 дополнительно нужен `libs/glfw/lib-vc2022-x86/`

## 🚀 Быстрая сборка (всё сразу)

```bat
cd tools
build-all.bat
```

Или PowerShell:

```powershell
.\tools\build-all.ps1 -Version 1.0.0
```

Результат в папке `release/`.

## 🔨 Сборка по шагам

### 1. Собрать бинари

```bat
cd aeros
build.bat x64   — 64-bit
build.bat x86   — 32-bit
```

Для x86 нужен 32-битный GLFW:

```powershell
.\tools\get-glfw-x86.ps1
```

### 2. Портативные версии

```bat
cd installer
build-portable.bat both 1.0.0
```

Или:

```powershell
.\build-portable.ps1 -Arch both -Version 1.0.0
```

Создаст `release/Aeros-Engine-Portable-*-v*.zip`

### 3. Установщики

Требует Inno Setup 6.

```bat
cd installer
build-installers.bat both all 1.0.0
```

Или:

```powershell
.\build-installers.ps1 -Arch both -Type all -Version 1.0.0
```

Опции:
- `-Arch x64|x86|both` — архитектура
- `-Type full|update|all` — тип установщика
- `-Version 1.0.0` — версия

## 📝 Версионирование

Версия хранится в:
- `VERSION` — основной файл (X.Y.Z)
- `aeros/src/version.h` — для C++ кода

Для обновления версии:

```powershell
.\tools\version-bump.ps1 1.1.0
```

## 🔄 Апдейт установщик

- Проверяет наличие установленной программы в реестре (HKLM/HKCU, `InstallLocation`)
- Папка установки берётся из реестра через `DefaultDirName={code:GetDefaultDirName}` —
  раньше найденный путь подставлялся только в поле на странице выбора папки, и в тихом
  режиме обновление уезжало в папку по умолчанию вместо реальной
- Если установка не найдена — предлагает поставить полную версию или выбрать папку
- Обновляет только бинарь и DLL, не трогая пользовательские модели
- Не создаёт новую запись в "Установка и удаление программ"

## 🌐 GitHub Release (автоматически)

При пуше тега `v*` (например `v1.0.0`) автоматически:

1. Собираются бинари x64 (CUDA, fallback CPU), x86 и arm64 (CPU); PE-заголовок каждого
   сверяется с целевой архитектурой — иначе сборка падает
2. Создаются портативные ZIP и проверяются: PE-машина exe и всех DLL внутри архива
3. Создаются установщики и апдейтеры (6 штук), затем каждый установщик ставится в тихом
   режиме и проверяется, какой именно `main.exe` лёг на диск
4. Создаётся GitHub Release со всеми файлами (x64 первыми в списке ассетов)

Ручной запуск: Actions → Release Build → Run workflow

Создание релиза локально:

```bash
git tag v1.0.0
git push origin v1.0.0
```

Или через gh CLI:

```bash
gh release create v1.0.0 release/* --title "v1.0.0" --notes "Release"
```

## 📂 Структура ISS скриптов

- `common.iss` — общие определения (AppId, версия, имя exe)
- `archcheck.inc` — проверка PE-архитектуры установленного exe (подключается в `[Code]`)
- `AerosEngine-x64.iss` / `-x86.iss` / `-arm64.iss` — полные установщики
- `AerosEngine-Updater-x64.iss` / `-x86.iss` / `-arm64.iss` — апдейтеры
- `AerosEngine-Lite-x64.iss` — Lite-установщик для слабых ПК

Все используют `OutputDir=..\release`.
`build-installers.ps1` запускает ISCC с рабочей папкой `installer\`, чтобы
относительные пути трактовались одинаково и препроцессором, и самим Inno.

## ⚙️ Кастомизация иконки

Сейчас `SetupIconFile=..\aeros\src\icon.ico` (файл есть в репозитории).
Раньше указывался `..\aeros\bin\icon.ico`, которого в репозитории нет
(`bin/` в `.gitignore`) — из-за этого setup.exe собирался без иконки.
Чтобы заменить иконку, положите свой `icon.ico` в `aeros/src/`.

Можно создать иконку из PNG:

```powershell
# Используя ImageMagick или онлайн конвертер
```

Или убрать строку `SetupIconFile` из ISS.

## 🔒 Правило архитектуры (важно)

Каждый установщик и каждый портативный zip несёт бинарь **строго своей архитектуры**:
`main-x64.exe`, `main-x86.exe`, `main-arm64.exe`. Общий `aeros\bin\main.exe`
в упаковку **не берётся никогда** — при последовательной сборке трёх архитектур в нём
лежит последний собранный exe (arm64), и установщик для x64 молча ставил бы его.

Именно так возникала ошибка у пользователей:

> Невозможно выполнить файл: D:\Aeros Engine\main.exe
> CreateProcess:: сбой; код 216 — «эта версия %1 не совместима с версией Windows»

Корень проблемы был в `.iss`:

```ini
; ТАК ДЕЛАТЬ НЕЛЬЗЯ
Source: "..\aeros\bin\main-x64.exe"; ...; Check: FileExists(ExpandConstant('..\aeros\bin\main-x64.exe'))
Source: "..\aeros\bin\main.exe";     ...; Check: not FileExists(ExpandConstant('..\aeros\bin\main-x64.exe'))
```

Параметр `Check:` выполняется **во время установки на компьютере пользователя**,
а не при компиляции установщика. Относительного пути `..\aeros\bin\main-x64.exe`
на пользовательском ПК не существует, поэтому первая строка всегда отбрасывалась,
а вторая — всегда срабатывала: на диск ложился `main.exe` (arm64).

Сейчас выбор бинаря делается **на этапе компиляции**: в `[Files]` одна безусловная
строка `Source: "..\aeros\bin\main-<arch>.exe"`. Если файла нет, ISCC падает
с ошибкой «File ... does not exist» — упаковать чужую архитектуру невозможно.

Дополнительные уровни защиты:

1. `build-installers.ps1` и `build-portable.ps1` сверяют поле `Machine` из
   PE-заголовка каждого бинаря перед упаковкой (8664 = x64, 014C = x86, AA64 = arm64)
   и прерывают сборку при несовпадении.
2. `installer\archcheck.inc` (подключается в `[Code]` каждого `.iss`) после установки
   перечитывает PE-заголовок уже установленного `{app}\main.exe`. При несовпадении
   пользователь видит понятное сообщение вместо кода 216, в папке установки
   появляется `ARCH-MISMATCH.txt`, а автозапуск отключается (`Check: AerosLaunchAllowed`).
3. В CI установщики реально ставятся в тихом режиме, и PE-заголовок установленного
   `main.exe` сверяется с архитектурой установщика; то же делается для exe и DLL
   внутри портативных zip.

DLL в установщиках — тоже только свои: шаблон `..\aeros\bin\*.dll` убран,
потому что в `bin` лежат `glfw3-x86.dll` / `glfw3-arm64.dll` (чужие архитектуры)
и неиспользуемые NVIDIA Streamline `sl.*.dll` (код их не загружает — FrameGen
реализован внутри движка).

## 🐛 Troubleshooting

**ISCC.exe not found**
- Установите Inno Setup 6: https://jrsoftware.org/isinfo.php
- Или укажите путь: `.\build-installers.ps1 -ISCCPath "C:\Program Files (x86)\Inno Setup 6\ISCC.exe"`

**x86 build fails**
- Запустите `.\tools\get-glfw-x86.ps1` для скачивания 32-bit GLFW
- Или скопируйте вручную `glfw3.lib` и `glfw3.dll` в `libs/glfw/lib-vc2022-x86/`

**CUDA not found**
- Установите CUDA Toolkit
- Проверьте `nvcc --version`

**«Не найден aeros\bin\main-<arch>.exe»**
- Сначала соберите нужную архитектуру: `cd aeros && build.bat x64`
  или `build-cpu.bat x64|x86|arm64`
- Общий `bin\main.exe` намеренно не используется как замена: он может быть чужой архитектуры

## 📄 Лицензия

GPL-3.0 — см. LICENSE
