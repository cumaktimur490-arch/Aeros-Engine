# Release папка

Сюда попадают все собранные релизные файлы:

- `Aeros-Engine-Setup-x64-v*.exe` — установщик 64-bit
- `Aeros-Engine-Setup-x86-v*.exe` — установщик 32-bit
- `Aeros-Engine-Portable-x64-v*.zip` — портативная 64-bit
- `Aeros-Engine-Portable-x86-v*.zip` — портативная 32-bit
- `Aeros-Engine-Update-x64-v*.exe` — апдейтер 64-bit
- `Aeros-Engine-Update-x86-v*.exe` — апдейтер 32-bit
- `SHA256SUMS.txt` — контрольные суммы

Папка создаётся автоматически при сборке:

```bat
cd tools
build-all.bat
```

Или через GitHub Actions при пуше тега `v*`.

Файлы из этой папки загружаются в GitHub Release автоматически.
