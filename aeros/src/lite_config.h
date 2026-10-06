#pragma once
// v1.20.0 Lite — конфигурация для слабых устройств i3-3xxx, встроенная графика, ноутбучные GPU
// Без CUDA, оптимизировано для Intel HD 4000 / GT 620M / AMD APU

// Определяем Lite режим компиляции
#ifdef AEROS_LITE

// --- CPU оптимизации для i3-3xxx (Ivy Bridge, 2012) ---
// i3-3xxx: 2C/4T, SSE4.2, AVX (нет AVX2), 3MB cache, 35W TDP
// Отключаем AVX2, используем SSE2/SSE4.2, ограничиваем OpenMP
#define LITE_MAX_THREADS 2          // i3-3xxx — 2 ядра, 4 потока, но для тепла — 2
#define LITE_USE_SSE2 1             // Базовый для всех x64
#define LITE_USE_AVX2 0             // Отключаем AVX2 — нет на Ivy Bridge
#define LITE_USE_AVX 0              // Даже AVX отключаем для совместимости, только SSE
#define LITE_OPT_LEVEL 1            // O1 вместо O2/O3 для меньшего бинаря и тепла

// --- GPU оптимизации для Intel HD 4000 / GT 620M ---
// HD 4000: OpenGL 4.0, 16 EUs, shared RAM, нет Vulkan 1.3 (только 1.0)
// GT 620M: 96 CUDA cores, 1GB VRAM, OpenGL 4.5, слабый fillrate
#define LITE_OPENGL_VERSION_MAJOR 3
#define LITE_OPENGL_VERSION_MINOR 3 // 3.3 для максимальной совместимости с HD 4000
#define LITE_MSAA_SAMPLES 0         // Без MSAA — экономит VRAM и fillrate
#define LITE_ENABLE_VULKAN 0        // Отключаем Vulkan по умолчанию — HD 4000 плохо поддерживает
#define LITE_ENABLE_FSR 0           // FSR тяжеловат для встроек
#define LITE_ENABLE_FG 0            // FG тяжеловат
#define LITE_ENABLE_PBR 0           // Упрощенное освещение, без Fresnel/rim
#define LITE_SHADOWS 0              // Без теней

// --- Визуализация — сниженные настройки ---
#define LITE_DEFAULT_PARTICLES 1500     // Было 15000 — теперь 1500 для слабых GPU
#define LITE_MAX_PARTICLES 5000         // Лимит
#define LITE_DEFAULT_STREAMLINES 8      // Было 24
#define LITE_MAX_STREAMLINES 16
#define LITE_DEFAULT_STREAMLINE_STEPS 80  // Было 300
#define LITE_MAX_STREAMLINE_STEPS 150
#define LITE_VOXEL_RESOLUTION 24        // Было 48 — в 8 раз меньше памяти (24³ vs 48³)
#define LITE_LBM_RESOLUTION 32          // Было 64-128
#define LITE_LBM_ENABLED_DEFAULT 0      // LBM выключен по умолчанию — тяжелый
#define LITE_LBM_STEPS_PER_FRAME 1      // Минимум

// --- Память ---
#define LITE_MAX_MEMORY_MB 512          // Лимит RAM — для ноутбуков с 4GB
#define LITE_TEXTURE_QUALITY 0          // 0=low, 1=medium, 2=high — low для встроек
#define LITE_SMOKE_RESOLUTION 16        // Было 48 — для объемного дыма
#define LITE_VORTEX_RESOLUTION 12       // Было 24

// --- CPU/GPU баланс ---
#define LITE_TARGET_FPS 30              // Цель 30 FPS вместо 60 — меньше нагрузка
#define LITE_VSYNC_DEFAULT 1            // VSync вкл — меньше нагрев
#define LITE_LIMIT_FPS_DEFAULT 1
#define LITE_MAX_FPS 30.0f

// --- Фичи — что выключить для слабых ---
#define LITE_ENABLE_VOLUMETRIC 0        // Объемный дым — тяжело
#define LITE_ENABLE_VORTEX_TUBES 0      // Вихревые трубки — тяжело
#define LITE_ENABLE_FLIGHT 0            // Полет — можно, но легко
#define LITE_ENABLE_LIC 0               // LIC — тяжело
#define LITE_ENABLE_AEROACOUSTIC 0      // Акустика — тяжело
#define LITE_ENABLE_SCHLIEREN 1         // Шлирен — можно, легкая
#define LITE_ENABLE_SHOCK 1             // Скачки — легкие
#define LITE_ENABLE_INTERESTING_DEFAULT 0 // Интересные фичи выкл по умолчанию

// --- Шейдеры — упрощенные ---
#define LITE_SIMPLE_SHADERS 1           // Упрощенные шейдеры без PBR
#define LITE_MAX_LIGHTS 1               // Один источник света

// --- Другое ---
#define LITE_ENABLE_CUDA 0              // Без CUDA
#define LITE_ENABLE_OPENMP_LIMIT 1      // Ограничить OpenMP
#define LITE_POWER_SAVING 1             // Энергосбережение
#define LITE_BINARY_NAME "aeros-engine-lite"
#define LITE_WINDOW_WIDTH 1024
#define LITE_WINDOW_HEIGHT 600          // Меньше окно по умолчанию — для 1366x768 ноутов

#else

// Обычная версия — значения по умолчанию
#define LITE_MAX_THREADS 0 // auto
#define LITE_DEFAULT_PARTICLES 15000
#define LITE_DEFAULT_STREAMLINES 24
#define LITE_DEFAULT_STREAMLINE_STEPS 300
#define LITE_VOXEL_RESOLUTION 48
#define LITE_BINARY_NAME "aeros-engine"
#define LITE_WINDOW_WIDTH 1280
#define LITE_WINDOW_HEIGHT 720

#endif

// Функции для Lite режима
bool isLiteMode();
void applyLiteDefaults();
void applyLiteOptimizations();
const char* getLiteInfoString();
int getLiteMaxThreads();
bool isLiteGPU();
void printLiteSystemInfo();
