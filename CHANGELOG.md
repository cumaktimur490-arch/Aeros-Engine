# Changelog — Aeros Engine

Все значимые изменения проекта документируются здесь.

Формат основан на [Keep a Changelog](https://keepachangelog.com/ru/1.0.0/),
версии — [Semantic Versioning](https://semver.org/lang/ru/).

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
