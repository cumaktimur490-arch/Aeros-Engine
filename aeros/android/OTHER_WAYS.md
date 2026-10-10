# Другие способы собрать/установить APK — без Termux
## Способ 1 — Скачать готовый APK из Releases (1 минута, без сборки!)
https://github.com/cumaktimur490-arch/Aeros-Engine/releases
Скачай balanced для SD662 и установи через файл менеджер.

## Способ 2 — GitHub Codespaces через браузер телефона (5 минут, без ПК!)
1. Открой в Chrome: https://github.com/cumaktimur490-arch/Aeros-Engine
2. Code -> Codespaces -> Create codespace
3. В терминале: cd aeros/android && ./build-all-apk.sh balanced
4. Скачай APK из release/

## Способ 3 — Termux (bash, не ./)
cd "/storage/emulated/0/Download/Aeros-Engine-1.22.0 (1)/Aeros-Engine-1.22.0/aeros/android"
bash build-termux.sh balanced
Или скопируй в ~/Aeros-Engine и собирай оттуда (chmod не работает на /sdcard)

## Способ 4 — ПК
Windows: build-all-apk.bat balanced
Linux: ./build-all-apk.sh balanced
adb install release/*.apk

## Способ 5 — AIDE на телефоне
Установи AIDE, открой aeros/android как проект, Build APK

Для SD662 всегда balanced — 2500 частиц 60 FPS <25 MB!
