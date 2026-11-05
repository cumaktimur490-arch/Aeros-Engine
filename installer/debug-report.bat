@echo off
rem =====================================================
rem Aeros Engine — сохранение отладочного отчёта.
rem Делает то же, что клавиша F10 в программе, но без запуска окна:
rem удобно, когда Aeros Engine не стартует или CUDA не работает.
rem Результат — файл Aeros-Debug-Report-<дата>.txt в этой папке
rem (если папка только для чтения — в %LOCALAPPDATA%\AerosEngine).
rem Этот файл нужно прислать разработчику вместе с описанием проблемы.
rem =====================================================
chcp 65001 >nul
cd /d "%~dp0"
title Aeros Engine - debug report

echo.
echo Aeros Engine: собираю отладочный отчёт...
echo.

if not exist "%~dp0main.exe" (
    echo [ОШИБКА] В этой папке нет main.exe.
    echo Запустите этот файл из папки, куда установлен Aeros Engine.
    echo.
    pause
    exit /b 1
)

"%~dp0main.exe" --save-report
set RC=%ERRORLEVEL%

echo.
if "%RC%"=="0" (
    echo Готово. Путь к отчёту показан в окне выше.
    echo Файл лежит в папке программы либо в %%LOCALAPPDATA%%\AerosEngine.
) else (
    echo Не удалось сохранить отчёт, код %RC%.
    echo Пришлите разработчику файл logs\aeros-debug.log из этой папки.
)
echo.
pause
