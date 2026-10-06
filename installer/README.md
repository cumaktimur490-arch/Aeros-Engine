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

- Проверяет наличие установленной программы в реестре
- Если не найдена — предлагает установить полную версию или выбрать папку
- Обновляет только бинарь и DLL, не трогая пользовательские модели
- Не создаёт новую запись в "Установка и удаление программ"

## 🌐 GitHub Release (автоматически)

При пуше тега `v*` (например `v1.0.0`) автоматически:

1. Собираются x64 и x86 бинари
2. Создаются портативные ZIP
3. Создаются установщики и апдейтеры
4. Создаётся GitHub Release со всеми файлами

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

- `common.iss` — общие определения (AppId, версия)
- `AerosEngine-x64.iss` — полный установщик x64
- `AerosEngine-x86.iss` — полный установщик x86
- `AerosEngine-Updater-x64.iss` — апдейтер x64
- `AerosEngine-Updater-x86.iss` — апдейтер x86

Все используют `OutputDir=..\release`

## ⚙️ Кастомизация иконки

Сейчас `SetupIconFile=..\aeros\bin\icon.ico` — если файла нет, Inno Setup использует иконку по умолчанию.
Чтобы добавить свою иконку, положите `icon.ico` в `aeros/bin/`.

Можно создать иконку из PNG:

```powershell
# Используя ImageMagick или онлайн конвертер
```

Или убрать строку `SetupIconFile` из ISS.

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

**bin/main.exe not found**
- Сначала соберите: `cd aeros && build.bat x64`

## 📄 Лицензия

GPL-3.0 — см. LICENSE
