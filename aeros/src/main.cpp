// =====================================================
// AeroS Engine — точка входа, главный цикл и рендер v1.19.0 GoGonam AoS.
// =====================================================

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#ifdef _WIN32
// glfwGetWin32Window() — HWND окна как владелец диалога выбора модели
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#endif
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include <iostream>
#include <thread>
#include <chrono>
#include <cmath>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <ctime>

#include "globals.h"
#include "settings_store.h"   // v1.32.0 — реестр настроек: сохранение, сброс, профили
#include "version.h"
#include "shaders.h"
#include "gl_utils.h"
#include "input.h"
#include "ui.h"
#include "vulkan_scene.h"
#include "ui_layout.h"   // v1.31.0 — компоновка оболочки CAD/CAE (сохранение между запусками)
#include "model.h"
#include "stl_loader.h"
#include "particles.h"
#include "streamlines.h"
#include "forces.h"
#include "atmosphere.h"
#include "test_mode.h"
#include "lbm.h"
#include "flow_field.h"   // v1.30.0 — aeroCpNonFinite, airSutherlandViscosity
#include "lang.h"
#include "fsr.h"
#include "framegen.h"
#include "interesting.h"
#include "vulkan_renderer.h"
#include "lite_config.h"
#include "debug_log.h"   // v1.25.0 — режим отладки: журнал событий, действий и физики
#include "timelapse.h"   // v1.26.0 — режим вычислений: таймлапс физики (F7/F8)

#include <cstdio>
#include <cstdlib>

#ifdef _OPENMP
#include <omp.h>
#endif

// GL debug callback — v1.19.0 fixed to use GLAD_GL_VERSION_4_3
static void APIENTRY glDebugCallback(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar* message, const void* userParam) {
    (void)source; (void)id; (void)length; (void)userParam;
    if (severity == GL_DEBUG_SEVERITY_NOTIFICATION) return;
    const char* sevStr = "UNKNOWN";
    if (severity == GL_DEBUG_SEVERITY_HIGH) sevStr = "HIGH";
    else if (severity == GL_DEBUG_SEVERITY_MEDIUM) sevStr = "MEDIUM";
    else if (severity == GL_DEBUG_SEVERITY_LOW) sevStr = "LOW";
    if (type == GL_DEBUG_TYPE_ERROR) {
        std::cerr << "[GL ERROR] [" << sevStr << "] " << message << std::endl;
    } else {
        std::cout << "[GL Debug] type=" << type << " severity=" << sevStr << " msg=" << message << std::endl;
    }
}

// Screenshot BMP export — v1.9.0 new feature
static bool saveScreenshotBMP(const std::string& path) {
    if (display_w <= 0 || display_h <= 0) return false;
    int w = display_w;
    int h = display_h;
    std::vector<unsigned char> pixels(3 * w * h);
    glReadPixels(0, 0, w, h, GL_BGR, GL_UNSIGNED_BYTE, pixels.data());
    // Flip vertically
    std::vector<unsigned char> flipped(3 * w * h);
    for (int y = 0; y < h; ++y) {
        memcpy(flipped.data() + y * 3 * w, pixels.data() + (h - 1 - y) * 3 * w, 3 * w);
    }
    // BMP header
    int filesize = 54 + 3 * w * h;
    unsigned char header[54] = {0};
    header[0] = 'B'; header[1] = 'M';
    header[2] = filesize & 0xFF; header[3] = (filesize >> 8) & 0xFF; header[4] = (filesize >> 16) & 0xFF; header[5] = (filesize >> 24) & 0xFF;
    header[10] = 54;
    header[14] = 40;
    header[18] = w & 0xFF; header[19] = (w >> 8) & 0xFF; header[20] = (w >> 16) & 0xFF; header[21] = (w >> 24) & 0xFF;
    header[22] = h & 0xFF; header[23] = (h >> 8) & 0xFF; header[24] = (h >> 16) & 0xFF; header[25] = (h >> 24) & 0xFF;
    header[26] = 1; header[28] = 24;
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    f.write((char*)header, 54);
    f.write((char*)flipped.data(), 3 * w * h);
    return f.good();
}

static std::string generateTimestampFilename(const std::string& prefix, const std::string& ext) {
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm;
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    std::ostringstream oss;
    oss << prefix << "_" << std::put_time(&tm, "%Y%m%d_%H%M%S") << ext;
    return oss.str();
}

// Export forces to CSV — v1.9.0 new
static bool exportForcesCSV(const std::string& path) {
    std::ofstream f(path);
    if (!f) return false;
    f << "Parameter,Value,Unit\n";
    f << "FlowSpeed," << flowSpeed << ",m/s\n";
    f << "Altitude," << altitude << ",m\n";
    f << "AirDensity," << airDensity << ",kg/m3\n";
    f << "AirPressure," << airPressure << ",Pa\n";
    f << "Temperature," << airTemperature << ",K\n";
    f << "SpeedOfSound," << speedOfSound << ",m/s\n";
    f << "Mach," << (flowSpeed / (speedOfSound + 1e-6f)) << ",\n";
    f << "Reynolds," << aeroReNumber << ",\n";
    f << "RefArea," << aeroRefArea << ",m2\n";
    float q = 0.5f * airDensity * flowSpeed * flowSpeed;
    f << "DynamicPressure," << q << ",Pa\n";
    f << "Drag," << dragMagnitude << ",N\n";
    f << "Lift," << liftMagnitude << ",N\n";
    float Cd = (q > 1e-6f && aeroRefArea > 1e-6f) ? dragMagnitude / (q * aeroRefArea) : 0;
    float Cl = (q > 1e-6f && aeroRefArea > 1e-6f) ? liftMagnitude / (q * aeroRefArea) : 0;
    f << "Cd," << Cd << ",\n";
    f << "Cl," << Cl << ",\n";
    f << "L/D," << liftToDragRatio << ",\n";
    f << "Moment," << momentMagnitude << ",Nm\n";
    f << "CoP_X," << centerOfPressure.x << ",m\n";
    f << "CoP_Y," << centerOfPressure.y << ",m\n";
    f << "CoP_Z," << centerOfPressure.z << ",m\n";
    f << "Azimuth," << flowAzimuth << ",deg\n";
    f << "Elevation," << flowElevation << ",deg\n";
    f << "LBM_Enabled," << (lbmParams.enabled ? 1 : 0) << ",\n";
    f << "LBM_Steps," << lbmCurrentStep << ",\n";
    f << "LBM_Reynolds," << lbmReynolds << ",\n";
    f << "LBM_Reynolds_Lattice," << lbmReynoldsLB << ",\n";
    f << "LBM_Tau," << lbmParams.tau << ",\n";
    f << "LBM_TKE," << lbmTKE << ",\n";
    return f.good();
}

// Settings save/load — v1.19.0 Multilingual (non-static for UI)
// =====================================================
// v1.32.0 — сохранение и загрузка настроек через единый реестр
// (aeros/src/settings_store.h + settings_table.inc).
//
// Раньше здесь был ручной список из 28 ключей, тогда как в интерфейсе
// привязано более сотни переменных: цвета, прозрачности, эффекты, FSR,
// генерация кадров и оптимизация под машину после перезапуска возвращались
// к стандарту. Теперь список ключей, диапазоны и стандартные значения живут
// в одной таблице, а сохранение, загрузка, сброс и профили берут данные из неё.
// Формат файла прежний (`ключ=значение`), поэтому старые aeros_settings.ini
// читаются без потерь.
// =====================================================
bool saveSettings(const std::string& path) {
    std::ofstream f(path);
    if (!f) return false;
    f << "# Aeros Engine v" AEROS_VERSION_STRING " Settings\n";
    settingsWrite(f);
    // Ключи компоновки оболочки пишет ui_layout.h — единый источник состояния
    // панелей; читаются они тем же uiLayoutApplyValue() при загрузке.
    uiLayoutWrite(f, uiLayoutState());
    return f.good();
}

bool loadSettings(const std::string& path) {
    std::ifstream f(path);
    if (!f) return false;
    std::string line;
    int applied = 0, unknown = 0, broken = 0;
    while (std::getline(f, line)) {
        const std::string t = settingsTrim(line);
        if (t.empty() || t[0] == '#') continue;
        const size_t pos = t.find('=');
        if (pos == std::string::npos) { ++broken; continue; }
        const std::string key = settingsTrim(t.substr(0, pos));
        const std::string val = settingsTrim(t.substr(pos + 1));
        // Сначала ключи компоновки (включая устаревшие имена v1.28.0):
        // состояние панелей одно, в ui_layout.h.
        if (uiLayoutApplyValue(uiLayoutState(), key, val)) { ++applied; continue; }
        const int r = settingsApplyLine(t);
        if (r == 1) ++applied;
        else if (r == 0) ++unknown;
        else ++broken;
    }
    // Мусор в файле больше не проходит молча: сколько принято и сколько
    // пропущено — попадает в журнал отладки.
    if (unknown > 0 || broken > 0)
        DBG_WARN("настройки: применено %d, неизвестных ключей %d, испорченных строк %d",
                 applied, unknown, broken);
    else
        DBG_INFO("настройки: применено ключей %d", applied);
    return true;
}

// =====================================================
// v1.25.0 — режим отладки (F9) и отладочный отчёт (F10)
//
// Всё, что раньше уходило в std::cout/std::cerr, в сборке /SUBSYSTEM:WINDOWS
// не видно никому. Поэтому состояние системы, бэкенда вычислений и физики
// пишется в logs\aeros-debug.log и в logs\physics.csv, а по F10 сохраняется
// единый файл Aeros-Debug-Report-<дата>.txt, который можно прислать разработчику.
// =====================================================
static std::string g_debugModelPath;     // путь к загруженной модели (для отчёта)
static std::string g_debugGLInfo;        // GL_VENDOR / GL_RENDERER / GL_VERSION
static int         g_debugMSAA = 0;
static long long   g_debugFrameCounter = 0;

// Собирает актуальные параметры перед формированием отчёта
static void collectDebugFacts() {
    char buf[512];

    const char* buildKind = "полная (OpenGL/Vulkan + CUDA-бэкенд)";
#ifdef CPU_ONLY
    buildKind = "CPU_ONLY — собрано без CUDA";
#endif
    debugLogAddFact("Версия", "Aeros Engine", AEROS_VERSION_FULL);
    debugLogAddFact("Версия", "Тип сборки", buildKind);
#ifdef AEROS_LITE
    debugLogAddFact("Версия", "Профиль", "Lite (облегчённая сборка)");
#endif
    debugLogAddFact("Версия", "Рендерер", getRendererName() ? getRendererName() : "не определён");
    debugLogAddFact("Версия", "Потоков OpenMP", std::to_string(perfOpenMPThreads));

    // --- Бэкенд вычислений: главный раздел для вопроса «почему не работает CUDA» ---
    debugLogAddFact("Бэкенд", "Выбран в настройках", useCUDA ? "CUDA (GPU)" : "CPU");
    debugLogAddFact("Бэкенд", "Бинарь собран с CUDA", g_cudaInfo.compiledWithCuda ? "да" : "нет (CPU_ONLY)");
    debugLogAddFact("Бэкенд", "Устройств NVIDIA найдено", std::to_string(g_cudaInfo.deviceCount));
    debugLogAddFact("Бэкенд", "Имя GPU", g_cudaInfo.deviceName[0] ? g_cudaInfo.deviceName : "не определено");
    std::snprintf(buf, sizeof(buf), "compute %d.%d, память %lld МБ",
                  g_cudaInfo.ccMajor, g_cudaInfo.ccMinor, g_cudaInfo.totalMemoryMB);
    debugLogAddFact("Бэкенд", "Характеристики GPU", buf);
    std::snprintf(buf, sizeof(buf), "драйвер CUDA %d.%d, среда выполнения CUDA %d.%d",
                  g_cudaInfo.driverVersion / 1000, (g_cudaInfo.driverVersion % 1000) / 10,
                  g_cudaInfo.runtimeVersion / 1000, (g_cudaInfo.runtimeVersion % 1000) / 10);
    debugLogAddFact("Бэкенд", "Версии CUDA", buf);
    debugLogAddFact("Бэкенд", "Тестовое ядро на GPU выполнилось", g_cudaInfo.usable ? "да" : "нет");
    if (g_cudaInfo.lastError != 0)
        debugLogAddFact("Бэкенд", "Код последней ошибки CUDA", std::to_string(g_cudaInfo.lastError));
    debugLogAddFact("Бэкенд", "Вывод диагностики",
                    g_cudaStatusText.empty() ? std::string("(проба не выполнялась)") : g_cudaStatusText);
    debugLogAddFact("Бэкенд", "Откатов GPU на CPU за сессию", std::to_string(g_debugCudaFallbacks));

    // --- Рендерер и окно ---
    debugLogAddFact("Рендерер", "OpenGL",
                    g_debugGLInfo.empty() ? std::string("(Vulkan или строки GL не получены)") : g_debugGLInfo);
    std::snprintf(buf, sizeof(buf), "%d x %d, MSAA=%d, vsync=%s, лимит FPS=%s (%.0f)",
                  display_w, display_h, g_debugMSAA, vsyncEnabled ? "да" : "нет",
                  limitFPS ? "да" : "нет", maxFPS);
    debugLogAddFact("Рендерер", "Окно", buf);
    debugLogAddFact("Рендерер", "FSR", fsrEnabled ? "включён" : "выключен");

    // --- Модель ---
    debugLogAddFact("Модель", "Файл", g_debugModelPath.empty() ? std::string("(не загружена)") : g_debugModelPath);
    debugLogAddFact("Модель", "Вершин", std::to_string(modelVertexCount));
    debugLogAddFact("Модель", "Разрешение воксельной сетки", std::to_string(voxelResolution));

    // --- Физика ---
    std::snprintf(buf, sizeof(buf), "скорость %.3f м/с, азимут %.1f, возвышение %.1f, масштаб времени %.2f",
                  flowSpeed, flowAzimuth, flowElevation, timeScale);
    debugLogAddFact("Физика", "Поток", buf);
    std::snprintf(buf, sizeof(buf), "частиц %d (рисуется %d, показ %s), линий тока %d (вершин %d, показ %s)",
                  numParticles, particleDrawCount, showParticles ? "да" : "нет",
                  numStreamlines, streamlineVertexCount, showStreamlines ? "да" : "нет");
    debugLogAddFact("Физика", "Частицы и линии тока", buf);
    std::snprintf(buf, sizeof(buf), "LBM %s, сетка %dx%dx%d, шаг %d, шагов на кадр %d",
                  lbmParams.enabled ? "включён" : "выключен", lbmNx, lbmNy, lbmNz,
                  lbmCurrentStep, lbmParams.stepsPerFrame);
    debugLogAddFact("Физика", "LBM", buf);
    // v1.29.0 — с какой частотой на самом деле считалась физика. Это первое, что
    // нужно знать при жалобе «на моём ПК обтекание развивается быстрее/медленнее»:
    // до 1.29.0 частота физики была равна частоте кадров.
    {
        float lat = (lbmParams.enabled && lbmInitialized) ? lbmLatticeDtWorld() : 0.0f;
        std::snprintf(buf, sizeof(buf),
                      "%s, %.0f Гц (факт %.1f Гц), во сколько раз выше кадров: %.2f, тактов на кадр: %d, "
                      "предел на кадр: %d, подшагов частиц: %d",
                      physClock.fixed ? "фиксированный такт" : "покадровый режим",
                      physClock.hz, physClock.achievedHz, physClock.displayRatio,
                      physClock.ticks, physClock.maxSubsteps, physClock.particleSubsteps);
        debugLogAddFact("Физика", "Такт физики", buf);
        std::snprintf(buf, sizeof(buf),
                      "шаг решётки %.4f мс, шагов решётки на кадр %d (всего %lld), модельное время %.3f с, "
                      "темп к учтённому времени %.4f, к настоящим часам %.4f, пропущено %.3f с%s",
                      lat * 1000.0f, physClock.latticeSteps, physClock.totalLatticeSteps,
                      physClock.simTime, physClock.timeScale, physClock.wallScale,
                      physClock.droppedTime,
                      physClock.saturated ? ", ОТСТАВАНИЕ (не успеваем)" : "");
        debugLogAddFact("Физика", "Модельное время", buf);
    }
    // v1.34.0 — оба числа Рейнольдса рядом: физическое (что обтекается) и
    // решёточное (что реально разрешает сетка). Расхождение — ограничение
    // метода, и оно не должно выглядеть как одно и то же число.
    std::snprintf(buf, sizeof(buf),
                  "rho %.5f, vmax %.4f м/с, TKE %.5f, Re_phys %.1f, Re_LB %.1f (tau %.3f), сходимость %s",
                  lbmAvgRho, lbmMaxVelocityWorld, lbmTKE, lbmReynolds, lbmReynoldsLB, lbmParams.tau,
                  lbmConverged ? "достигнута" : "нет");
    debugLogAddFact("Физика", "Состояние LBM", buf);
    std::snprintf(buf, sizeof(buf), "подъёмная (%.3f; %.3f; %.3f), сопротивление (%.3f; %.3f; %.3f), момент (%.3f; %.3f; %.3f)",
                  liftVector.x, liftVector.y, liftVector.z,
                  dragVector.x, dragVector.y, dragVector.z,
                  momentVector.x, momentVector.y, momentVector.z);
    debugLogAddFact("Физика", "Силы", buf);

    // --- Производительность ---
    std::snprintf(buf, sizeof(buf), "кадр %.2f мс, LBM %.2f, частицы %.2f, силы %.2f, линии тока %.2f, воксели %.2f",
                  perfFrameMs, perfLBMms, perfParticlesMs, perfForcesMs, perfStreamlinesMs, perfVoxelMs);
    debugLogAddFact("Производительность", "Время подсистем", buf);
    debugLogAddFact("Производительность", "Средний FPS", std::to_string((int)g_currentFPSAverage));
    debugLogAddFact("Производительность", "Кадров отрисовано", std::to_string(g_debugFrameCounter));

    // --- Таймлапс физики и карта давления (v1.26.0) ---
    std::snprintf(buf, sizeof(buf), "запись %s, фоновая история %s, ёмкость %d кадров, в буфере %d, снимков поля %d",
                  tlEnabled ? "включена (F7)" : "выключена", tlAutoRecord ? "да" : "нет",
                  tlCapacity, timelapseFrameCount(), timelapseFieldCount());
    debugLogAddFact("Таймлапс", "Состояние", buf);
    std::snprintf(buf, sizeof(buf), "скорость %.2fx, позиция %d, зацикливание %s",
                  tlSpeed, timelapsePlayIndex(), tlLoop ? "да" : "нет");
    debugLogAddFact("Таймлапс", "Воспроизведение", buf);
    std::snprintf(buf, sizeof(buf), "%s, диапазон Cp [%.3f; %.3f], схема %d, давление LBM [%.1f; %.1f] Па",
                  aeroCpAutoRange ? "авто-диапазон" : "ручной диапазон", g_cpLo, g_cpHi,
                  aeroColorMap, lbmPressureMin, lbmPressureMax);
    debugLogAddFact("Карта давления", "Настройка", buf);
    std::snprintf(buf, sizeof(buf), "RMS изменения плотности %.3e, изменение макс. скорости %.3e (за кадр)",
                  lbmResidualRho, lbmResidualU);
    debugLogAddFact("LBM", "Остаток сходимости", buf);
    // v1.27.0 — счётчики аномалий решателя. Нули означают, что расчёт шёл чисто;
    // рост говорит о потере устойчивости раньше, чем она станет видна по rho.
    std::snprintf(buf, sizeof(buf), "отрицательных популяций %lld, заменённых нефизических %lld (за последний шаг)",
                  lbmNegativePops, lbmBadPops);
    debugLogAddFact("LBM", "Аномалии популяций", buf);
    // v1.30.0 — в отчёт добавлено всё, что решатель раньше правил молча:
    // обрезка плотности до [0.8; 1.2] в равновесных популяциях и обрезка
    // физического давления по скоростному напору удалены, а вмешательства
    // (неконечные rho/p, выход rho за пределы устойчивости, ограничитель |u|<0.3,
    // неконечный Cp в силах) считаются и попадают сюда.
    std::snprintf(buf, sizeof(buf),
                  "rho вне [0.5;1.5] %lld, rho NaN %lld, |u|>0.3 ограничено %lld, p NaN %lld, Cp NaN у треугольников %lld",
                  lbmRhoOutOfRange, lbmRhoNonFinite, lbmULimited, lbmPressureNonFinite, aeroCpNonFinite);
    debugLogAddFact("LBM", "Подмены и ограничения за шаг", buf);
    // v1.30.0 — настройки, показанные в UI, но не реализованные в решателе.
    // Раньше useZouHeBC по умолчанию был true, и журнал писал «Zou/He (LBM): вкл»,
    // хотя схемы Zou/He в коде нет.
    debugLogAddFact("LBM", "Настройки без эффекта",
                    lbmInertSettings()[0] ? lbmInertSettings() : "нет (все видимые настройки работают)");
    std::snprintf(buf, sizeof(buf), "подсеточная вязкость Смагоринского Cs=%.3f, TKE %.3e м2/с2 (модель в решёточных единицах)",
                  lbmParams.smagorinskyC, lbmTKE);
    debugLogAddFact("LBM", "Модель турбулентности", buf);

    // --- Режим качества ---
    std::snprintf(buf, sizeof(buf), "Lite=%s, Ultra-Lite=%s, пресет=%s, авто-качество=%s, энергосбережение=%s",
                  g_isLiteMode ? "да" : "нет", g_isUltraLiteMode ? "да" : "нет",
                  getPresetName(g_currentPreset), g_autoQualityScaling ? "да" : "нет",
                  g_batterySaver ? "да" : "нет");
    debugLogAddFact("Режим качества", "Состояние", buf);
}

// Проба CUDA-бэкенда: честно отвечает, работает ли GPU и почему нет
static void probeCudaAndLog() {
    cudaProbeBackend(&g_cudaInfo);
    g_cudaProbeDone = true;
    g_cudaStatusText = g_cudaInfo.status;
    DBG_INFO("проба CUDA-бэкенда: %s", g_cudaStatusText.c_str());
    debugLogAddFact("Бэкенд", "Вывод диагностики", g_cudaStatusText);
    if (!g_cudaInfo.usable && useCUDA) {
        useCUDA = 0;
        DBG_WARN("CUDA недоступна — расчёт переключён на CPU-бэкенд");
    }
}

// v1.25.0 — журнал действий «всего».
// Вместо того чтобы вешать лог на каждый контрол UI, раз в кадр сравниваем
// текущие параметры с предыдущими и пишем каждое изменение (что и на какое
// значение поменяли). Так в отчёте появляется полная история действий
// пользователя перед тем, как что-то пошло не так.
static void trackParamChangesForJournal() {
    if (!debugLogEnabled() || !debugLogActionsEnabled()) return;

    static const char* kFN[] = {
        "скорость потока (м/с)", "азимут потока", "возвышение потока", "масштаб времени",
        "число Струхаля", "сила следа", "длина следа", "высота (м)", "лимит FPS",
        "скорость камеры", "чувствительность мыши", "скорость автоповорота",
        "турбулентность на входе (LBM)", "высота земли (LBM)"
    };
    static float* kFP[] = {
        &flowSpeed, &flowAzimuth, &flowElevation, &timeScale, &strouhal, &wakeStrength,
        &wakeLength, &altitude, &maxFPS, &cameraSpeedMultiplier, &mouseSensitivity,
        &aeroAutoRotateSpeed, &lbmParams.inletTurbulence, &lbmParams.groundHeight
    };
    static const char* kIN[] = {
        "число частиц", "число линий тока", "разрешение воксельной сетки", "шагов LBM на кадр",
        "сетка LBM X", "сетка LBM Y", "сетка LBM Z", "бэкенд вычислений"
    };
    static int* kIP[] = {
        &numParticles, &numStreamlines, &voxelResolution, &lbmParams.stepsPerFrame,
        &lbmNx, &lbmNy, &lbmNz, &useCUDA
    };
    static const char* kBN[] = {
        "показ модели", "показ давления", "показ линий тока", "показ частиц", "LBM включён",
        "Zou/He (LBM, не реализовано)", "конвективный выход (LBM)", "земля (LBM)", "vsync", "лимит FPS",
        "FSR", "авто-качество", "энергосбережение", "отсечение по пирамиде", "динамические частицы",
        "адаптивный шаг LBM", "автоповорот", "режим полёта", "режим тестов", "Lite", "Ultra-Lite"
    };
    static bool* kBP[] = {
        &showModel, &showPressure, &showStreamlines, &showParticles, &lbmParams.enabled,
        &lbmParams.useZouHeBC, &lbmParams.useConvectiveOutlet, &lbmParams.useGround,
        &vsyncEnabled, &limitFPS, &fsrEnabled, &g_autoQualityScaling, &g_batterySaver,
        &optFrustumCulling, &optDynamicParticles, &aeroAdaptiveLBM, &autoRotate,
        &aeroFlightMode, &testModeEnabled, &g_isLiteMode, &g_isUltraLiteMode
    };

    const size_t NF = sizeof(kFP) / sizeof(kFP[0]);
    const size_t NI = sizeof(kIP) / sizeof(kIP[0]);
    const size_t NB = sizeof(kBP) / sizeof(kBP[0]);

    static std::vector<float> prevF;
    static std::vector<int>   prevI;
    static std::vector<char>  prevB;
    static std::vector<double> lastLog;

    if (prevF.size() != NF || prevI.size() != NI || prevB.size() != NB) {
        prevF.resize(NF); prevI.resize(NI); prevB.resize(NB); lastLog.assign(NF + NI + NB, -1e9);
        for (size_t i = 0; i < NF; ++i) prevF[i] = *kFP[i];
        for (size_t i = 0; i < NI; ++i) prevI[i] = *kIP[i];
        for (size_t i = 0; i < NB; ++i) prevB[i] = (*kBP[i]) ? 1 : 0;
        DBG_INFO("журнал действий следит за %zu параметрами", NF + NI + NB);
        return;   // первый кадр — только запоминаем исходные значения
    }

    double now = glfwGetTime();
    const double throttle = 0.25;   // не чаще 4 записей в секунду на параметр (ползунки)

    for (size_t i = 0; i < NF; ++i) {
        float v = *kFP[i];
        if (!std::isfinite(v)) continue;
        float eps = std::fabs(v) * 0.002f + 1e-4f;
        if (std::fabs(v - prevF[i]) > eps && (now - lastLog[i]) > throttle) {
            debugLogAction("изменён параметр", "%s: %.4f -> %.4f", kFN[i], prevF[i], v);
            prevF[i] = v;
            lastLog[i] = now;
        }
    }
    for (size_t i = 0; i < NI; ++i) {
        int v = *kIP[i];
        if (v == prevI[i]) continue;
        if ((now - lastLog[NF + i]) <= throttle) continue;
        if (i == NI - 1)   // бэкенд вычислений — словами, а не цифрой
            debugLogAction("изменён параметр", "бэкенд вычислений: %s -> %s",
                           prevI[i] ? "CUDA" : "CPU", v ? "CUDA" : "CPU");
        else
            debugLogAction("изменён параметр", "%s: %d -> %d", kIN[i], prevI[i], v);
        prevI[i] = v;
        lastLog[NF + i] = now;
    }
    for (size_t i = 0; i < NB; ++i) {
        char v = (*kBP[i]) ? 1 : 0;
        if (v == prevB[i]) continue;
        debugLogAction("переключено", "%s: %s", kBN[i], v ? "ВКЛ" : "выкл");
        prevB[i] = v;
    }
}

// v1.33.0 — иконка окна ставится из одного места: раньше код был вставлен
// дважды (при создании окна и при его пересоздании для отката на OpenGL), а
// icon_data.h с #pragma once нельзя включить второй раз в другой области
// видимости — второй блок просто не компилировался.
static void applyWindowIcon(GLFWwindow* window) {
    #include "icon_data.h"
    GLFWimage iconImg;
    iconImg.width = ICON_WIDTH;
    iconImg.height = ICON_HEIGHT;
    iconImg.pixels = (unsigned char*)ICON_DATA;
    glfwSetWindowIcon(window, 1, &iconImg);
    std::cout << "[Icon] Window icon set: AoS ENG. " << ICON_WIDTH << "x" << ICON_HEIGHT << std::endl;
}

int main(int argc, char** argv) {
    // v1.25.0 — журнал отладки открывается до всего остального, чтобы в него
    // попали причины любых ранних сбоев (GLFW, окно, GLAD, шейдеры, модель).
    debugLogInit();
    debugLogSetFactCallback(collectDebugFacts);
    DBG_INFO("=== запуск %s ===", AEROS_VERSION_FULL);
    {
        std::string argsLine;
        for (int i = 1; i < argc; ++i) { argsLine += argv[i]; argsLine += " "; }
        DBG_INFO("аргументы: %s", argsLine.empty() ? "(нет)" : argsLine.c_str());
        DBG_INFO("папка журнала: %s", debugLogDir().empty() ? "(недоступна для записи)" : debugLogDir().c_str());
    }
#ifdef _OPENMP
    perfOpenMPThreads = omp_get_max_threads();
    std::cout << "[Perf] OpenMP enabled with " << perfOpenMPThreads << " threads (v1.20.0 Lite Multilingual)" << std::endl;
#else
    perfOpenMPThreads = 1;
    std::cout << "[Perf] OpenMP not enabled (single thread)" << std::endl;
#endif

    // v1.20.0 Lite — проверка режима и применение оптимизаций для слабых устройств
    printLiteSystemInfo();
    if (isLiteMode()) {
        g_isLiteMode = true;
        std::cout << "[Lite] Lite mode detected — applying optimizations for i3-3xxx / HD 4000" << std::endl;
        applyLiteDefaults();
        applyLiteOptimizations();
        display_w = 1024;
        display_h = 600;
    }

    // v1.20.1 — авто-детект слабого железа и загрузка конфига
    loadLiteConfig("aeros-lite.ini");
    // Если не Lite и слабое железо — авто-переключение
    if (!g_isLiteMode) {
        if (detectAndApplyLiteIfNeeded()) {
            display_w = (g_currentPreset == LiteQualityPreset::Potato) ? ULTRA_LITE_WINDOW_W : LITE_WINDOW_WIDTH;
            display_h = (g_currentPreset == LiteQualityPreset::Potato) ? ULTRA_LITE_WINDOW_H : LITE_WINDOW_HEIGHT;
        }
    }

    // Parse command line for renderer selection — v1.19.0 Vulkan support
    RendererAPI requestedAPI = RendererAPI::Auto;
#ifdef AEROS_LITE
    requestedAPI = RendererAPI::OpenGL; // Lite — только OpenGL по умолчанию
    std::cout << "[Lite] Forcing OpenGL renderer for weak GPUs (HD 4000)" << std::endl;
#endif
    // v1.25.0 — ключи режима отладки
    bool cliDebug = false, cliActions = false, cliPhysics = false, cliOverlay = false, cliSaveReport = false;
    int  cliPhysicsEvery = 0;
    // v1.26.0 — ключи таймлапса физики
    bool  cliTimelapse = false;          // начать запись сразу
    bool  cliNoAutoTL  = false;          // не вести фоновую историю
    int   cliTLCap     = 0;              // ёмкость буфера, кадров
    int   cliTLField   = -1;             // снимок поля каждый N-й кадр (0 = не снимать)
    float cliTLSpeed   = 0.0f;           // скорость воспроизведения
    // v1.29.0 — ключи фиксированного такта физики (как в BeamNG.drive)
    bool  cliPhysFixed    = false;       // --fixed-physics
    bool  cliPhysNoFixed  = false;       // --no-fixed-physics (прежний покадровый режим)
    float cliPhysHz       = 0.0f;        // --physics-hz N
    int   cliPhysSubsteps = 0;           // --physics-substeps N
    for (int i=1; i<argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--vulkan" || arg == "-vulkan" || arg == "--vk") {
            requestedAPI = RendererAPI::Vulkan;
            std::cout << "[Main] Requested Vulkan renderer via CLI" << std::endl;
        } else if (arg == "--opengl" || arg == "-opengl" || arg == "--gl") {
            requestedAPI = RendererAPI::OpenGL;
            std::cout << "[Main] Requested OpenGL renderer via CLI" << std::endl;
        } else if (arg == "--debug") {
            cliDebug = true;
        } else if (arg == "--debug-actions") {
            cliActions = true;
        } else if (arg == "--debug-physics") {
            cliPhysics = true;
        } else if (arg == "--debug-overlay") {
            cliOverlay = true;
        } else if (arg == "--physics-every" && i+1 < argc) {
            cliPhysicsEvery = std::atoi(argv[++i]);
        } else if (arg == "--fixed-physics") {
            cliPhysFixed = true;
        } else if (arg == "--no-fixed-physics") {
            cliPhysNoFixed = true;
        } else if (arg == "--physics-hz" && i+1 < argc) {
            cliPhysHz = (float)std::atof(argv[++i]);
        } else if (arg == "--physics-substeps" && i+1 < argc) {
            cliPhysSubsteps = std::atoi(argv[++i]);
        } else if (arg == "--timelapse") {
            cliTimelapse = true;
        } else if (arg == "--no-timelapse") {
            cliNoAutoTL = true;
        } else if (arg == "--timelapse-capacity" && i+1 < argc) {
            cliTLCap = std::atoi(argv[++i]);
        } else if (arg == "--timelapse-field" && i+1 < argc) {
            cliTLField = std::atoi(argv[++i]);
        } else if (arg == "--timelapse-speed" && i+1 < argc) {
            cliTLSpeed = (float)std::atof(argv[++i]);
        } else if (arg == "--save-report" || arg == "--report" || arg == "--debug-report") {
            cliSaveReport = true;
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "Aeros Engine v" AEROS_VERSION_STRING " — Airflow Visualization\n";
            std::cout << "Full: Vulkan+OpenGL, CUDA, FSR, FG, all features\n";
            std::cout << "Lite: i3-3xxx / HD 4000 / GT 620M / 4GB RAM / No CUDA / SSE2 / 30 FPS\n";
            std::cout << "Ultra-Lite: Atom/Celeron 2GB RAM / HD 3000 / 500 particles / 20 FPS / 800x450\n";
            std::cout << "Usage: " << argv[0] << " [options] [model.stl]\n";
            std::cout << "Options:\n";
            std::cout << "  --vulkan, --vk    — force Vulkan renderer (full only)\n";
            std::cout << "  --opengl, --gl    — force OpenGL renderer\n";
            std::cout << "  --lite            — force Lite mode (1500 particles, 8x80 streamlines, voxel 24, 30 FPS)\n";
            std::cout << "  --ultra-lite, --potato — force Ultra-Lite mode (500 particles, 4x40, voxel 16, 20 FPS, 800x450)\n";
            std::cout << "  --preset potato|low|medium|full — set quality preset\n";
            std::cout << "  --auto-lite       — auto-detect weak hardware and switch to Lite if needed (default ON)\n";
            std::cout << "  --no-auto-quality — disable dynamic FPS scaling\n";
            std::cout << "  --battery-saver   — force battery saver (30 FPS, low power)\n";
            std::cout << "  --debug           — debug mode: write logs/aeros-debug.log (same as F9)\n";
            std::cout << "  --debug-physics   — also write the physics journal logs/physics.csv\n";
            std::cout << "  --debug-actions   — log every user action (already ON with --debug)\n";
            std::cout << "  --debug-overlay   — show the last log lines on screen\n";
            std::cout << "  --physics-every N — one physics row per N frames (default 4)\n";
            std::cout << "  --save-report     — write Aeros-Debug-Report-<date>.txt and exit (works with no window)\n";
            std::cout << "  --timelapse       — start recording the physics timelapse at once (same as F7)\n";
            std::cout << "  --no-timelapse    — do not keep the background physics history\n";
            std::cout << "  --timelapse-capacity N — ring buffer size in frames (default 6000)\n";
            std::cout << "  --timelapse-field N    — capture a pressure-field snapshot every N frames (0 = off)\n";
            std::cout << "  --timelapse-speed X    — playback speed, 1.0 = real time (same as the panel slider)\n";
            std::cout << "  --help, -h        — this help\n";
            std::cout << "  model.stl         — STL file to load\n";
            std::cout << "Presets:\n";
            std::cout << "  Potato (Ultra-Lite): Atom/Celeron 2GB HD3000 — 500 particles 4x40 voxel16 20 FPS 800x450 1 thread\n";
            std::cout << "  Low (Lite): i3-3xxx HD4000 GT620M 4GB — 1500 particles 8x80 voxel24 30 FPS 1024x600 SSE2 No CUDA\n";
            std::cout << "  Medium: i5-4xxx HD4600 GT740M 8GB — 5000 particles 16x150 voxel32 45 FPS 1280x720\n";
            std::cout << "  Full: i5+ GTX1060+ 8GB+ — 15000 particles 24x300 voxel48 60 FPS Vulkan+CUDA\n";
            std::cout << "Lite: Optimized for weak devices — auto-detect, battery saver, dynamic quality scaling\n";
            std::cout << "Full: 15000 particles, 24x300 streamlines, voxel 48, LBM ON, 60 FPS, AVX2\n";
            std::cout << "Renderer: Auto-selects Vulkan if available, else OpenGL (both fully working)\n";
            std::cout << "Linux: ./build-linux.sh --deps to install dependencies\n";
            std::cout << "       ./build-linux.sh all — build full\n";
            std::cout << "       ./build-lite.sh all — build Lite for weak devices\n";
            std::cout << "Windows: build.bat deps — download dependencies\n";
            std::cout << "         build.bat — build full\n";
            std::cout << "         build-lite.bat — build Lite\n";
            std::cout << "         build-lite.bat ultra — build Ultra-Lite\n";
            return 0;
        } else if (arg == "--lite") {
            g_isLiteMode = true;
            applyLiteDefaults();
            display_w = 1024; display_h = 600;
            std::cout << "[Main] Lite mode forced via CLI" << std::endl;
        } else if (arg == "--ultra-lite" || arg == "--ultralite" || arg == "--potato") {
            g_isLiteMode = true;
            g_isUltraLiteMode = true;
            applyUltraLiteDefaults();
            display_w = ULTRA_LITE_WINDOW_W; display_h = ULTRA_LITE_WINDOW_H;
            std::cout << "[Main] Ultra-Lite (Potato) mode forced via CLI" << std::endl;
        } else if (arg == "--preset" && i+1 < argc) {
            std::string presetStr = argv[++i];
            if (presetStr == "potato" || presetStr == "ultra-lite" || presetStr == "ultra") {
                g_isLiteMode = true; g_isUltraLiteMode = true;
                applyPreset(LiteQualityPreset::Potato);
                display_w = ULTRA_LITE_WINDOW_W; display_h = ULTRA_LITE_WINDOW_H;
            } else if (presetStr == "low" || presetStr == "lite") {
                g_isLiteMode = true;
                applyPreset(LiteQualityPreset::Low);
                display_w = 1024; display_h = 600;
            } else if (presetStr == "medium") {
                applyPreset(LiteQualityPreset::Medium);
            } else if (presetStr == "full" || presetStr == "high") {
                applyPreset(LiteQualityPreset::Full);
                g_isLiteMode = false;
            }
            std::cout << "[Main] Preset " << presetStr << " applied via CLI" << std::endl;
        } else if (arg == "--auto-lite") {
            detectAndApplyLiteIfNeeded();
            std::cout << "[Main] Auto Lite detection forced" << std::endl;
        } else if (arg == "--no-auto-quality") {
            g_autoQualityScaling = false;
            std::cout << "[Main] Auto quality scaling disabled" << std::endl;
        } else if (arg == "--battery-saver") {
            g_batterySaver = true;
            g_litePowerSaving = true;
            limitFPS = true;
            maxFPS = 30.0f;
            std::cout << "[Main] Battery saver forced" << std::endl;
        }
    }

    // v1.25.0 — применяем ключи отладки
    if (cliDebug)            debugLogSetEnabled(true);
    if (cliActions)          debugLogSetActions(true);
    if (cliPhysics)          debugLogSetPhysics(true);
    if (cliOverlay)          debugLogSetOverlay(true);
    if (cliPhysicsEvery > 0) g_debugPhysicsEveryNFrames = cliPhysicsEvery;
    DBG_INFO("режим отладки=%s, журнал действий=%s, журнал физики=%s, шаг записи физики=каждый %d кадр",
             debugLogEnabled() ? "да" : "нет", debugLogActionsEnabled() ? "да" : "нет",
             debugLogPhysicsEnabled() ? "да" : "нет", g_debugPhysicsEveryNFrames);

    // v1.26.0 — применяем ключи таймлапса и готовим буфер истории физики
    if (cliTLCap > 0)   tlCapacity   = cliTLCap;
    if (cliTLField >= 0) tlFieldEvery = cliTLField;
    if (cliTLSpeed > 0.0f) tlSpeed    = cliTLSpeed;
    if (cliNoAutoTL)    tlAutoRecord = false;
    if (cliTimelapse)   tlEnabled    = true;
    timelapseInit();
    debugLogAddSectionCallback(timelapseSummary);
    DBG_INFO("таймлапс физики: ёмкость %d кадров, снимок поля каждый %d-й кадр, фоновая запись %s",
             tlCapacity, tlFieldEvery, tlAutoRecord ? "включена" : "выключена");

    // --save-report: собрать отчёт и выйти. Работает без окна и без графики —
    // именно так можно прислать диагностику, если программа вообще не запускается.
    if (cliSaveReport) {
        probeCudaAndLog();
        std::string reportPath = debugLogSaveReport();
        if (reportPath.empty()) {
            DBG_ERROR("--save-report: не удалось сохранить отчёт (нет папки, доступной для записи)");
#ifdef _WIN32
            MessageBoxA(nullptr, "Не удалось сохранить отладочный отчёт: нет папки, доступной для записи.",
                        "Aeros Engine — отладка", MB_ICONERROR);
#endif
            debugLogClose();
            return 2;
        }
        std::cout << "[Debug] Отчёт сохранён: " << reportPath << std::endl;
#ifdef _WIN32
        std::string msg = "Отладочный отчёт сохранён:\n" + reportPath +
                          "\n\nПришлите этот файл разработчику вместе с описанием проблемы.";
        MessageBoxA(nullptr, msg.c_str(), "Aeros Engine — отладочный отчёт", MB_ICONINFORMATION);
#endif
        debugLogClose();
        return 0;
    }

    // Try load settings
    if (aeroSaveSettings) {
        loadSettings("aeros_settings.ini");
        std::cout << "[Settings] Loaded aeros_settings.ini if exists" << std::endl;
    }

    // v1.34.0 — стартовый экран (главное меню) показывается только если так
    // записано в настройках. Флаг живёт в globals, поэтому здесь достаточно
    // применить сохранённое значение; закрытие экрана его не переключает —
    // иначе одно нажатие «Продолжить» навсегда отключало бы экран.
    aeroStartScreen = aeroStartScreenOnStartup;

    // v1.29.0 — ключи такта физики применяем ПОСЛЕ ini: командная строка важнее
    // сохранённых настроек (иначе --physics-hz молча перекрывался файлом).
    if (cliPhysFixed)   physClock.fixed = true;
    if (cliPhysNoFixed) physClock.fixed = false;
    if (cliPhysHz > 0.5f) {
        physClock.hz = cliPhysHz;
        if (physClock.hz < 1.0f) physClock.hz = 1.0f;
        if (physClock.hz > 5000.0f) physClock.hz = 5000.0f;
    }
    if (cliPhysSubsteps > 0) {
        physClock.maxSubsteps = cliPhysSubsteps;
        if (physClock.maxSubsteps > 4096) physClock.maxSubsteps = 4096;
    }
    physClock.sanitize();
    DBG_INFO("такт физики: %s, %.0f Гц, предел %d тактов на кадр (%s)",
             physClock.fixed ? "фиксированный" : "покадровый (как до v1.29.0)",
             physClock.hz, physClock.maxSubsteps,
             physClock.fixed ? "не зависит от герцовки монитора" : "частота физики = частоте кадров");

    // Settings file can override renderer if CLI is Auto
    if (requestedAPI == RendererAPI::Auto) {
        // Check settings for renderer preference
        std::ifstream f("aeros_settings.ini");
        std::string line;
        while (std::getline(f, line)) {
            if (line.find("renderer=vulkan") != std::string::npos || line.find("api=vulkan") != std::string::npos) {
                requestedAPI = RendererAPI::Vulkan;
                break;
            } else if (line.find("renderer=opengl") != std::string::npos || line.find("api=opengl") != std::string::npos) {
                requestedAPI = RendererAPI::OpenGL;
                break;
            }
        }
    }

    if (!glfwInit()) {
        DBG_ERROR("glfwInit() не удалась — окно создать невозможно");
#ifdef _WIN32
        MessageBoxA(nullptr, "Failed to init GLFW", "Error", MB_ICONERROR);
#else
        std::cerr << "Failed to init GLFW" << std::endl;
#endif
        return -1;
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_SAMPLES, 4);
    glfwWindowHint(GLFW_DOUBLEBUFFER, GLFW_TRUE);
#ifdef _DEBUG
    glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GLFW_TRUE);
#endif

    // v1.19.0 Vulkan support — check if Vulkan requested, then don't create OpenGL context
    bool useVulkanWindow = false;
    if (requestedAPI == RendererAPI::Vulkan && !isVulkanAvailable()) {
        // v1.26.0 — явное сообщение вместо молчаливого отката. Особенно важно,
        // потому что renderer=vulkan сохраняется в aeros_settings.ini и без
        // этого сообщения пользователь каждый запуск получал бы чёрное окно
        // (или непонятный OpenGL) без объяснения причины.
        // v1.33.0 — причина пишется точная: чего именно не хватило (загрузчик,
        // устройство с графикой или поддержка вывода в окно).
        const std::string why = getVulkanDecisionReason();
        DBG_WARN("Vulkan запрошен (ключ --vulkan или aeros_settings.ini), но недоступен: %s "
                 "— окно создаётся с контекстом OpenGL 3.3", why.c_str());
        debugLogAddFact("Рендерер", "Vulkan",
                        vulkanLoaderPresent()
                            ? ("запрошен, но недоступен: " + why + " — используется OpenGL").c_str()
                            : "запрошен, но загрузчик Vulkan не найден — используется OpenGL");
    }
    if (requestedAPI == RendererAPI::Vulkan && isVulkanAvailable()) {
        // For Vulkan, we need no API
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        useVulkanWindow = true;
        std::cout << "[Main] Окно создаётся без контекста OpenGL (Vulkan): "
                  << getVulkanDecisionReason() << std::endl;
    } else {
        glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_API);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        std::cout << "[Main] Creating OpenGL window" << std::endl;
    }

    // Заголовок окна — из version.h (единый источник версии), а не захардкожен
    // v1.35.0 — перетаскивание файла в окно: путь запоминается в глобальной
    // переменной, а обрабатывается в drawUI(), где есть и loadModel, и
    // загрузчик исследования. Сам колбэк ничего не грузит: GLFW вызывает его
    // вне кадра, и загрузка модели (вокселизация, SDF, LBM) там была бы гонкой.
    auto dropCallback = [](GLFWwindow*, int count, const char** paths) {
        if (count <= 0 || !paths || !paths[0]) return;
        std::snprintf(aeroDroppedFile, sizeof(aeroDroppedFile), "%s", paths[0]);
    };
    GLFWwindow* window = glfwCreateWindow(SCR_WIDTH, SCR_HEIGHT, AEROS_VERSION_FULL, nullptr, nullptr);
    if (!window) {
        DBG_ERROR("glfwCreateWindow(%u x %u) вернула NULL — драйвер не дал OpenGL 3.3 core",
                  SCR_WIDTH, SCR_HEIGHT);
#ifdef _WIN32
        MessageBoxA(nullptr, "Failed to create GLFW window", "Error", MB_ICONERROR);
#else
        std::cerr << "Failed to create GLFW window" << std::endl;
#endif
        glfwTerminate();
        return -1;
    }

    // v1.19.0: Set window icon — AoS ENG.
    applyWindowIcon(window);

    // Init renderer abstraction — v1.19.0 Vulkan+OpenGL
    RendererConfig rendCfg;
    rendCfg.api = requestedAPI;
    rendCfg.vsync = vsyncEnabled;
    if (!useVulkanWindow) {
        glfwSetDropCallback(window, dropCallback);   // v1.35.0 — drag&drop STL/проекта
    glfwMakeContextCurrent(window);
        glfwSwapInterval(vsyncEnabled ? 1 : 0);
    }
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
    glfwSetCursorPosCallback(window, mouse_callback);
    glfwSetScrollCallback(window, scroll_callback);
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);

    if (!useVulkanWindow) {
        if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
            DBG_ERROR("gladLoadGLLoader не удалась — функции OpenGL не загружены");
#ifdef _WIN32
            MessageBoxA(nullptr, "Failed to init GLAD", "Error", MB_ICONERROR);
#else
            std::cerr << "Failed to init GLAD" << std::endl;
#endif
            glfwTerminate();
            return -1;
        }
    }

    // v1.33.0 — инициализация рендерера. Если Vulkan запрошен, но не поднялся
    // (например, драйвер не дал поверхность для окна), окно придётся создать
    // заново: в окне с GLFW_NO_API контекст OpenGL создать нельзя, а без него
    // GL-функции не загрузить.
    if (!initRenderer(window, requestedAPI)) {
        std::cerr << "[Main] Рендерер не инициализировался, пробуем OpenGL" << std::endl;
        if (useVulkanWindow) {
            glfwDestroyWindow(window);
            glfwDefaultWindowHints();
            glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_API);
            glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
            glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
            glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
            glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
            glfwWindowHint(GLFW_SAMPLES, 4);
            glfwWindowHint(GLFW_DOUBLEBUFFER, GLFW_TRUE);
            window = glfwCreateWindow(SCR_WIDTH, SCR_HEIGHT, AEROS_VERSION_FULL, nullptr, nullptr);
            if (!window) {
                DBG_ERROR("окно OpenGL не создалось после отказа Vulkan — выход");
                glfwTerminate();
                return -1;
            }
            applyWindowIcon(window);   // окно пересоздано — иконку ставим заново
            glfwMakeContextCurrent(window);
            glfwSwapInterval(vsyncEnabled ? 1 : 0);
            glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
            glfwSetCursorPosCallback(window, mouse_callback);
            glfwSetScrollCallback(window, scroll_callback);
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
            if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
                DBG_ERROR("gladLoadGLLoader не удалась после пересоздания окна — выход");
                glfwTerminate();
                return -1;
            }
            useVulkanWindow = false;
            requestedAPI = RendererAPI::OpenGL;
            DBG_WARN("Vulkan не запустился, окно пересоздано — используется OpenGL");
        }
        if (!initRenderer(window, RendererAPI::OpenGL)) {
            std::cerr << "[Main] Both renderers failed" << std::endl;
            DBG_ERROR("не удалось инициализировать ни Vulkan, ни OpenGL — выход");
            glfwTerminate();
            return -1;
        }
    }
    // v1.33.0 — единый признак «рисуем через Vulkan» для всей главной функции.
    const bool vulkanMode = isRendererVulkan();
    // v1.33.0 — фактический размер кадрового буфера. Матрица проекции считается
    // по display_w/display_h, а колбэк изменения размера срабатывает только при
    // последующем изменении окна: без этой строки на HiDPI-экране пропорции
    // сцены были бы неверными до первого ресайза.
    glfwGetFramebufferSize(window, &display_w, &display_h);
    std::cout << "[Main] Renderer: " << getRendererName() << std::endl;
    DBG_INFO("рендерер: %s", getRendererName() ? getRendererName() : "?");

    // OpenGL debug — v1.9.0 fixed: use GLAD_GL_VERSION_4_3 — only for OpenGL
    if (!vulkanMode && GLAD_GL_VERSION_4_3) {
        glEnable(GL_DEBUG_OUTPUT);
        glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
        glDebugMessageCallback(glDebugCallback, nullptr);
        glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DEBUG_SEVERITY_NOTIFICATION, 0, nullptr, GL_FALSE);
        std::cout << "[GL] Debug output enabled (GL 4.3+)" << std::endl;
    } else {
        std::cout << "[GL] Debug output not available (GL < 4.3)" << std::endl;
    }

    if (!vulkanMode) {
        glEnable(GL_DEPTH_TEST);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glEnable(GL_MULTISAMPLE);
        glEnable(GL_PROGRAM_POINT_SIZE);

        // Check MSAA support
        GLint msaaSamples = 0;
        glGetIntegerv(GL_SAMPLES, &msaaSamples);
        std::cout << "[GL] Vendor: " << glGetString(GL_VENDOR) << " Renderer: " << glGetString(GL_RENDERER) << " Version: " << glGetString(GL_VERSION) << " MSAA: " << msaaSamples << std::endl;
        // v1.25.0 — строки GL идут в отладочный отчёт: по ним видно, какой драйвер
        // и какая видеокарта реально отвечают за отрисовку
        {
            const char* glVen = (const char*)glGetString(GL_VENDOR);
            const char* glRen = (const char*)glGetString(GL_RENDERER);
            const char* glVer = (const char*)glGetString(GL_VERSION);
            g_debugGLInfo = std::string(glVen ? glVen : "?") + " | " +
                            (glRen ? glRen : "?") + " | " + (glVer ? glVer : "?");
            g_debugMSAA = (int)msaaSamples;
            DBG_INFO("OpenGL: %s | MSAA=%d", g_debugGLInfo.c_str(), g_debugMSAA);
        }
    } else {
        std::cout << "[Main] OpenGL-состояние не настраивается — рисует Vulkan" << std::endl;
        g_debugGLInfo = "Vulkan: " + getVulkanVersionString();
        DBG_INFO("%s", g_debugGLInfo.c_str());
    }

    if (!vulkanMode) createObstacleSphere(48, 48);   // геометрия GL; Vulkan строит сам

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    // Docking disabled for old ImGui version (no DockingEnable flag) — v1.19.0 still compatible
    // io.ConfigFlags |= ImGuiConfigFlags_DockingEnable; // old ImGui doesn't have this

    // v1.19.0: Initialize localization with Cyrillic font support
    initLocalization();

    ImGui::StyleColorsDark();
    // Improve ImGui style for v1.19.0
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 4.0f;
    style.FrameRounding = 3.0f;
    style.GrabRounding = 3.0f;
    style.ScrollbarRounding = 3.0f;

    // v1.33.0 — у каждого бэкенда свой ввод и свой рисовальщик интерфейса.
    // Для Vulkan типы Vulkan нужны только рендереру: main.cpp вызывает
    // initUiBackend/uiRender и не тянет vulkan.h.
    const bool imguiGlfwOk = vulkanMode ? ImGui_ImplGlfw_InitForVulkan(window, true)
                                        : ImGui_ImplGlfw_InitForOpenGL(window, true);
    if (!imguiGlfwOk) {
        std::cerr << "[ImGui] Failed to init GLFW backend" << std::endl;
        DBG_ERROR("ImGui_ImplGlfw_InitFor%s не удалась", vulkanMode ? "Vulkan" : "OpenGL");
        glfwTerminate();
        return -1;
    }
    if (vulkanMode) {
        if (!g_renderer || !g_renderer->initUiBackend()) {
            std::cerr << "[ImGui] Failed to init Vulkan backend" << std::endl;
            DBG_ERROR("ImGui_ImplVulkan_Init не удалась — выход");
            ImGui_ImplGlfw_Shutdown();
            ImGui::DestroyContext();
            glfwTerminate();
            return -1;
        }
    } else if (!ImGui_ImplOpenGL3_Init("#version 330 core")) {
        std::cerr << "[ImGui] Failed to init OpenGL3 backend" << std::endl;
        DBG_ERROR("ImGui_ImplOpenGL3_Init не удалась");
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        glfwTerminate();
        return -1;
    }

    // v1.33.0 — GLSL-программы нужны только OpenGL-ветке. В Vulkan-режиме они
    // не компилируются вообще: рисовать ими нечего, а их отсутствие не ошибка.
    unsigned int modelShaderProgram    = 0;
    unsigned int particleShaderProgram = 0;
    unsigned int lineShaderProgram     = 0;
    unsigned int groundShaderProgram   = 0;
    if (!vulkanMode) {
        modelShaderProgram    = compileProgram(vertexShaderSource, fragmentShaderSource);
        particleShaderProgram = compileProgram(particleVertexShaderSource, particleFragmentShaderSource);
        lineShaderProgram     = compileProgram(lineVertexShaderSource, lineFragmentShaderSource);
        groundShaderProgram   = compileProgram(groundVertexShaderSource, groundFragmentShaderSource);
    }
    if (!vulkanMode && (modelShaderProgram == 0 || particleShaderProgram == 0 || lineShaderProgram == 0 || groundShaderProgram == 0)) {
        std::cerr << "[Main] Shader compile failed" << std::endl;
        DBG_ERROR("компиляция шейдеров не удалась (model=%u particle=%u line=%u ground=%u)",
                  modelShaderProgram, particleShaderProgram, lineShaderProgram, groundShaderProgram);
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        glfwTerminate();
        return -1;
    }

    // v1.19.0 FSR init — только OpenGL. FSR/FG построены на кадровых буферах и
    // фрагментных шейдерах OpenGL; в Vulkan-режиме они не включаются, и об этом
    // прямо сообщается (иначе настройка выглядела бы включённой, но не работала).
    if (vulkanMode) {
        fsrEnabled = false;
        fgEnabled  = false;
        DBG_WARN("FSR и генерация кадров доступны только в OpenGL-режиме — в Vulkan они отключены");
        std::cout << "[Main] FSR/FG отключены: в Vulkan-режиме они не поддерживаются" << std::endl;
    }
    if (!vulkanMode && !initFSR(display_w, display_h)) {
        std::cerr << "[FSR] Failed to init, disabling" << std::endl;
        fsrEnabled = false;
    } else if (!vulkanMode) {
        fsrRenderScale = getFSRScale((int)fsrMode);
        fsrCurrentScale = fsrRenderScale;
        std::cout << "[FSR] Ready — mode " << getFSRModeName((int)fsrMode) << " scale " << fsrRenderScale << std::endl;
    }

    // v1.19.0 Frame Generation init — тоже только OpenGL
    if (!vulkanMode && !initFrameGen(display_w, display_h)) {
        std::cerr << "[FG] Failed to init, disabling" << std::endl;
        fgEnabled = false;
    } else if (!vulkanMode) {
        std::cout << "[FG] Ready — mode " << getFGModeName((int)fgMode) << std::endl;
    }

    // v1.19.0 Interesting Features init
    initInterestingFeatures();
    std::cout << "[Interesting] v1.19.0 features initialized — Vulkan+OpenGL+Linux" << std::endl;

    // Показываем окно и забираем передний план ДО диалога выбора модели.
    // Окно создаётся скрытым (GLFW_VISIBLE=FALSE), а запускает программу обычно
    // установщик ([Run] ... nowait) — такой процесс не получает передний план,
    // и диалог без владельца остаётся за окном установки. Пользователь видит
    // «программа не запускается», хотя она ждёт выбора STL-модели.
    if (!vulkanMode) {
        glClearColor(0.04f, 0.05f, 0.07f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glfwSwapBuffers(window);
    }
    glfwShowWindow(window);
    glfwFocusWindow(window);

    // Модель из командной строки или через диалог выбора файла
    std::string modelPath = "";
    for (int i=1; i<argc; ++i) {
        std::string arg = argv[i];
        if (arg.size() > 4 && (arg.substr(arg.size()-4) == ".stl" || arg.substr(arg.size()-4) == ".STL")) {
            modelPath = arg;
            std::cout << "[Main] Model from CLI: " << modelPath << std::endl;
            break;
        }
    }
    // v1.35.1 — ПРИ ЗАПУСКЕ БОЛЬШЕ НЕ ОТКРЫВАЕТСЯ СВОЙ ДИАЛОГ ВЫБОРА STL.
    //
    // Раньше здесь всегда вызывался openFileDialog(): системное окно «Select STL
    // Model» появлялось ПОВЕРХ главного меню ещё до его отрисовки, а Esc/«Отмена»
    // завершали программу целиком — до меню с демо-телами и недавними файлами
    // было не добраться. Теперь первым показывается главное меню (start_screen.h),
    // а модель грузится его кнопками или перетаскиванием файла в окно.
    //
    // Путь из командной строки (AerosEngine.exe model.stl) сохраняется: он
    // по-прежнему загружается сразу и открывает уже рабочую оболочку.
    if (!modelPath.empty()) {
        if (!loadModel(modelPath)) {
            DBG_ERROR("не удалось загрузить модель из командной строки: %s", modelPath.c_str());
#ifdef _WIN32
            MessageBoxA(nullptr, ("Failed to load model: " + modelPath).c_str(), "Error", MB_ICONERROR);
#else
            std::cerr << "Failed to load model: " << modelPath << std::endl;
#endif
        } else {
            g_debugModelPath = modelPath;
            aeroModelPath = modelPath;   // v1.31.0 — исследование знает источник геометрии
            // Модель пришла аргументом — сразу к работе, без главного меню.
            aeroStartScreen = false;
            DBG_INFO("модель загружена: %s (вершин %d, габарит %.3f)",
                     modelPath.c_str(), modelVertexCount, maxDim);
            debugLogAction("загружена модель", "%s, вершин %d", modelPath.c_str(), modelVertexCount);
        }
    } else {
        // Модели нет: приложение живёт с открытым главным меню. Физика и сцена
        // до загрузки геометрии не считаются (см. hasModel в цикле кадра).
        // Экран показываем обязательно, даже если он отключён в настройках:
        // иначе пользователь получил бы пустую оболочку без модели и без
        // возможности её открыть (диалог при запуске мы убрали).
        aeroStartScreen = true;
        DBG_INFO("модель не задана — открыто главное меню");
    }

    // Окно показано и активировано выше — до диалога выбора модели

    float prevSpeed = flowSpeed;
    float prevAz    = flowAzimuth;
    float prevEl    = flowElevation;
    float prevWake  = wakeStrength;
    float prevStro  = strouhal;
    float prevWL    = wakeLength;
    float prevAlt   = altitude;
    float prevGroundH = aeroGroundHeight;
    bool prevGroundEn = aeroGroundEffect;

    float autoRotateAngle = 0.0f;
    float lastInteractionTime = (float)glfwGetTime();

    // v1.25.0 — проверяем CUDA до первого кадра: раньше ошибка GPU всплывала
    // внутри физики и молча портила расчёт. Теперь причина пишется в журнал,
    // а при недоступном GPU сразу включается CPU-бэкенд.
    probeCudaAndLog();
    DBG_INFO("старт расчёта: частиц %d, линий тока %d, LBM %s (сетка %dx%dx%d), воксели %d, бэкенд %s",
             numParticles, numStreamlines, lbmParams.enabled ? "включён" : "выключен",
             lbmNx, lbmNy, lbmNz, voxelResolution, useCUDA ? "CUDA" : "CPU");
    if (debugLogPhysicsEnabled())
        DBG_INFO("журнал физики пишется в %s", debugLogPhysicsFilePath().c_str());

    while (!glfwWindowShouldClose(window)) {
        float currentFrame = (float)glfwGetTime();
        deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;
        if (deltaTime > 0.1f)   deltaTime = 0.1f;
        if (deltaTime <= 0.0f)  deltaTime = 0.0001f;
        if (!std::isfinite(deltaTime)) deltaTime = 0.016f;

        if (limitFPS) {
            double target = 1.0 / maxFPS;
            double elapsed = glfwGetTime() - currentFrame;
            if (elapsed < target) {
                int sleepMs = (int)((target - elapsed)*1000.0);
                if (sleepMs > 0) std::this_thread::sleep_for(std::chrono::milliseconds(sleepMs));
            }
        }

        if (autoRotate) {
            // Pause auto-rotate if user interacted recently (v1.9.0 improvement)
            float timeSinceInteraction = currentFrame - lastInteractionTime;
            if (timeSinceInteraction > 2.0f) {
                autoRotateAngle += deltaTime * aeroAutoRotateSpeed;
                if (autoRotateAngle > 360.0f) autoRotateAngle -= 360.0f;
                flowAzimuth = autoRotateAngle;
            }
        }

        processInput(window);
        // v1.31.0 — пункт главного меню «Файл → Выход»: закрываем окно штатно,
        // тем же путём, что и Esc в processInput (сохранение настроек дальше по циклу).
        if (aeroExitRequested) {
            aeroExitRequested = false;
            glfwSetWindowShouldClose(window, true);
        }
        // v1.25.0 — пишем в журнал действия пользователя (изменения параметров)
        trackParamChangesForJournal();
        // Detect interaction for auto-rotate pause
        if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS ||
            glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS ||
            glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS ||
            glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_MIDDLE) == GLFW_PRESS) {
            lastInteractionTime = currentFrame;
        }

        auto frameStart = std::chrono::high_resolution_clock::now();

        auto tLBM0 = std::chrono::high_resolution_clock::now();
        // =====================================================
        // v1.29.0 — ФИКСИРОВАННЫЙ ТАКТ ФИЗИКИ (как в BeamNG.drive).
        // Частота тактов задаётся в Гц (physClock.hz) и не зависит от частоты
        // кадров: на кадр в 60 Гц при 240 Гц физики приходится 4 такта, а шаги
        // решётки LBM считаются по накопленному МОДЕЛЬНОМУ времени. Раньше за кадр
        // делалось stepsPerFrame шагов решётки, то есть на мониторе 144 Гц одно и
        // то же обтекание развивалось в 2.4 раза быстрее, чем на 60 Гц, — результат
        // зависел от железа. Теперь не зависит.
        // =====================================================
        const float fpsNow = (deltaTime > 1e-6f) ? 1.0f / deltaTime : 0.0f;
        const float latticeDt = (lbmParams.enabled && lbmInitialized) ? lbmLatticeDtWorld() : 0.0f;
        int lbmStepsThisFrame = -1;   // -1 = «как раньше», по stepsPerFrame
        const int physTicks = physClock.advance(deltaTime, latticeDt, fpsNow, &lbmStepsThisFrame);
        const float simDtFrame = physClock.frameSimDt();   // модельных секунд за кадр

        // Если физика упирается в предел тактов, модельное время начинает отставать
        // от реального (как slow motion в BeamNG). Это видно в панели и в строке
        // состояния, но пользователь может и не смотреть туда — пишем в журнал один
        // раз за эпизод, с причиной и советом.
        static bool s_physSatWarned = false;
        if (physClock.fixed && physClock.saturated && !s_physSatWarned) {
            s_physSatWarned = true;
            DBG_WARN("физика не успевает за реальным временем: %d тактов (предел %d), "
                     "модельное время идёт в %.3f× реального — уменьшите частоту тактов "
                     "или нагрузку (сетка, частицы)",
                     physTicks, physClock.maxSubsteps, physClock.timeScale);
        } else if (!physClock.saturated) {
            s_physSatWarned = false;
        }

        // v1.35.1 — пока модель не загружена, физику не считаем: главное меню
        // может быть открыто без геометрии («Продолжить без модели»), и пустой
        // домен LBM/частиц давал бы нули, NaN и лишнюю нагрузку на кадр.
        const bool hasModel = !g_vertices.empty();

        if (hasModel && lbmParams.enabled) {
            // v1.9.0 adaptive LBM stepping — только в прежнем покадровом режиме:
            // при фиксированном такте он менял бы частоту физики за спиной пользователя.
            if (!physClock.fixed && aeroAdaptiveLBM) {
                // Adjust steps based on frame time
                if (perfFrameMs > 33.0f && lbmParams.stepsPerFrame > 1) {
                    lbmParams.stepsPerFrame = std::max(1, lbmParams.stepsPerFrame - 1);
                } else if (perfFrameMs < 16.0f && lbmParams.stepsPerFrame < 20) {
                    lbmParams.stepsPerFrame++;
                }
            }
            updateLBM(physClock.tickDt, lbmStepsThisFrame);
        }
        auto tLBM1 = std::chrono::high_resolution_clock::now();
        perfLBMms = std::chrono::duration<float, std::milli>(tLBM1-tLBM0).count();

        auto tPart0 = std::chrono::high_resolution_clock::now();
        if (hasModel && showParticles) {
            if (physClock.fixed) {
                // Адвекция частиц идёт тем же фиксированным шагом: много маленьких
                // подшагов вместо одного большого (просадка кадра больше не даёт
                // «прыжок» частиц через половину расчётной области).
                const float dtPart = physClock.particleDt();
                const int nPart = physClock.partSteps;
                if (nPart > 0 && dtPart > 0.0f)
                    for (int k = 0; k < nPart; ++k) updateParticles(dtPart);
            } else {
                updateParticles(deltaTime);
            }
        }
        auto tPart1 = std::chrono::high_resolution_clock::now();
        perfParticlesMs = std::chrono::duration<float, std::milli>(tPart1-tPart0).count();

        auto tForce0 = std::chrono::high_resolution_clock::now();
        if (hasModel && showPressure) updateVertexColors();
        if (hasModel) computeLiftDrag();
        if (hasModel && lbmParams.enabled && lbmInitialized) computeLBMForcesFromLBM();
        if (hasModel) updateLiftDragArrows();
        // v1.19.0 Flight dynamics — обновляем после расчета сил
        if (hasModel && aeroFlightMode) {
            // v1.29.0 — динамика полёта интегрируется на модельном времени такта,
            // а не на deltaTime кадра: при фиксированном такте это тот же шаг, что
            // у решателя и частиц, поэтому 6DOF не «ускоряется» на быстром мониторе.
            const float dtFlight = physClock.fixed ? simDtFrame : deltaTime;
            if (dtFlight > 0.0f)
                updateFlightDynamics(dtFlight, liftVector, dragVector, momentVector, centerOfPressure);
        }
        auto tForce1 = std::chrono::high_resolution_clock::now();
        perfForcesMs = std::chrono::duration<float, std::milli>(tForce1-tForce0).count();

        // v1.19.0 Interesting features
        if (hasModel) updateInterestingFeatures(deltaTime);

        // Streamlines need recompute on flow change or ground change
        bool needStreamlines = false;
        if (hasModel && showStreamlines) {
            if (fabs(prevSpeed-flowSpeed) > 1e-3f ||
                fabs(prevAz-flowAzimuth) > 0.5f ||
                fabs(prevEl-flowElevation) > 0.5f ||
                fabs(prevWake-wakeStrength) > 1e-3f ||
                fabs(prevStro-strouhal) > 1e-3f ||
                fabs(prevWL-wakeLength) > 1e-3f ||
                fabs(prevAlt-altitude) > 10.0f ||
                fabs(prevGroundH-aeroGroundHeight) > 1e-3f ||
                prevGroundEn != aeroGroundEffect) {
                needStreamlines = true;
            }
        }
        if (needStreamlines) {
            auto tSL0 = std::chrono::high_resolution_clock::now();
            prevSpeed = flowSpeed;
            prevAz = flowAzimuth;
            prevEl = flowElevation;
            prevWake = wakeStrength;
            prevStro = strouhal;
            prevWL = wakeLength;
            prevAlt = altitude;
            prevGroundH = aeroGroundHeight;
            prevGroundEn = aeroGroundEffect;
            computeStreamlines();
            auto tSL1 = std::chrono::high_resolution_clock::now();
            perfStreamlinesMs = std::chrono::duration<float, std::milli>(tSL1-tSL0).count();
        }

        auto frameEnd = std::chrono::high_resolution_clock::now();
        perfFrameMs = std::chrono::duration<float, std::milli>(frameEnd-frameStart).count();

        if (testContinuous) validateFrame();

        // ===================== v1.25.0 — журнал отладки =====================
        g_debugFrameCounter++;

        // 1) Ошибки CUDA больше не теряются: забираем их из бэкенда, пишем в журнал
        //    и переключаемся на CPU, чтобы физика не продолжала считать мусор.
        {
            char cudaErr[900];
            cudaErr[0] = '\0';
            int code = cudaConsumeError(cudaErr, sizeof(cudaErr));
            if (code != 0) {
                g_debugCudaFallbacks++;
                DBG_ERROR("ошибка CUDA (код %d): %s", code, cudaErr);
                g_cudaStatusText = cudaErr;
                debugLogAddFact("Бэкенд", "Вывод диагностики", g_cudaStatusText);
                if (useCUDA) {
                    useCUDA = 0;
                    DBG_WARN("после ошибки CUDA расчёт переключён на CPU-бэкенд");
                }
                // При первой же ошибке GPU сохраняем отчёт сами: пользователю
                // не нужно ничего помнить, файл уже готов к отправке.
                if (g_debugCudaFallbacks == 1 && g_lastDebugReportPath.empty()) {
                    g_lastDebugReportPath = debugLogSaveReport();
                    if (!g_lastDebugReportPath.empty())
                        DBG_WARN("отладочный отчёт сохранён автоматически: %s", g_lastDebugReportPath.c_str());
                }
            }
        }

        // 2) Журнал физики: строка CSV на каждый N-й кадр + проверка на NaN/Inf
        if (debugLogPhysicsEnabled()) {
            static bool physicsHeaderWritten = false;
            int everyN = (g_debugPhysicsEveryNFrames > 0) ? g_debugPhysicsEveryNFrames : 1;
            if (!physicsHeaderWritten) {
                debugLogPhysicsHeader("frame,time_s,dt_ms,fps,backend,particles,drawn,streamlines,"
                                      "sl_verts,lbm_step,lbm_rho,lbm_vmax_world,lbm_tke,reynolds,"
                                      "lift_x,lift_y,lift_z,drag_x,drag_y,drag_z,"
                                      "frame_ms,lbm_ms,particles_ms,forces_ms,streamlines_ms,voxel_ms,nonfinite");
                physicsHeaderWritten = true;
            }
            if ((g_debugFrameCounter % everyN) == 0) {
                // v1.26.0: полный проход по позициям, пока массив небольшой.
                // Прежний шаг 97 пропускал до 96/97 неконечных значений, поэтому
                // потеря устойчивости могла не попасть в журнал физики вовсе.
                int nonFiniteStride = (particlePositions.size() <= 300000) ? 1 : 16;
                int nonFinite = debugCountNonFinite(particlePositions.data(), particlePositions.size(),
                                                    (std::size_t)nonFiniteStride);
                debugLogPhysicsRow("%lld,%.3f,%.3f,%.2f,%s,%d,%d,%d,%d,%d,%.6f,%.6f,%.6f,%.2f,"
                                   "%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%d",
                                   g_debugFrameCounter, currentFrame, deltaTime * 1000.0f,
                                   (deltaTime > 1e-6f) ? (1.0f / deltaTime) : 0.0f,
                                   useCUDA ? "CUDA" : "CPU",
                                   numParticles, particleDrawCount, numStreamlines, streamlineVertexCount,
                                   lbmCurrentStep, lbmAvgRho, lbmMaxVelocityWorld, lbmTKE, lbmReynolds,
                                   liftVector.x, liftVector.y, liftVector.z,
                                   dragVector.x, dragVector.y, dragVector.z,
                                   perfFrameMs, perfLBMms, perfParticlesMs, perfForcesMs,
                                   perfStreamlinesMs, perfVoxelMs, nonFinite);
                if (nonFinite > 0)
                    DBG_WARN("физика неустойчива: %d неконечных значений (NaN/Inf) в позициях частиц", nonFinite);
            }
        }

        // 3) Таймлапс физики (v1.26.0): двигаем курсор воспроизведения и пишем кадр
        timelapseAdvance(deltaTime);
        timelapseRecordFrame(g_debugFrameCounter, currentFrame);

        // 4) Краткая сводка в общий журнал (не засоряет файл: раз в 300 кадров)
        if (debugLogEnabled() && (g_debugFrameCounter % 300) == 0) {
            DBG_PHYS("кадр %lld: FPS %.1f, кадр %.1f мс (LBM %.1f, частицы %.1f, силы %.1f), "
                     "частиц %d, LBM шаг %d, rho %.5f, vmax %.4f, бэкенд %s",
                     g_debugFrameCounter, (deltaTime > 1e-6f) ? (1.0f / deltaTime) : 0.0f,
                     perfFrameMs, perfLBMms, perfParticlesMs, perfForcesMs,
                     particleDrawCount, lbmCurrentStep, lbmAvgRho, lbmMaxVelocityWorld,
                     useCUDA ? "CUDA" : "CPU");
        }
        // ===================================================================

        // Handle screenshot and CSV export requests (v1.9.0)
        if (aeroScreenshotRequested) {
            aeroScreenshotRequested = false;
            // v1.33.0 — снимок экрана в Vulkan-режиме пока не реализован: чтение
            // пикселей идёт из видеопамяти отдельного изображения swapchain, а не
            // через glReadPixels. Говорим об этом прямо вместо пустого файла.
            if (vulkanMode) {
                std::cerr << "[Export] Скриншот в Vulkan-режиме не поддерживается — "
                             "переключитесь на OpenGL (renderer=opengl)" << std::endl;
                DBG_WARN("скриншот запрошен, но в Vulkan-режиме не поддерживается — переключитесь на OpenGL");
                debugLogAddFact("Скриншот", "Vulkan", "не поддерживается в Vulkan-режиме");
            } else {
            std::string fname = generateTimestampFilename("screenshot", ".bmp");
            if (saveScreenshotBMP(fname)) {
                aeroLastScreenshotPath = fname;
                std::cout << "[Export] Screenshot saved: " << fname << std::endl;
                DBG_INFO("скриншот сохранён: %s", fname.c_str());
            } else {
                std::cerr << "[Export] Failed to save screenshot" << std::endl;
                DBG_ERROR("не удалось сохранить скриншот: %s", fname.c_str());
            }
            }
        }
        if (aeroCSVExportRequested) {
            aeroCSVExportRequested = false;
            std::string fname = generateTimestampFilename("forces", ".csv");
            if (exportForcesCSV(fname)) {
                aeroLastCSVPath = fname;
                std::cout << "[Export] CSV saved: " << fname << std::endl;
                DBG_INFO("CSV сил сохранён: %s", fname.c_str());
            } else {
                std::cerr << "[Export] Failed to save CSV" << std::endl;
                DBG_ERROR("не удалось сохранить CSV сил: %s", fname.c_str());
            }
        }

        if (vulkanMode) g_renderer->uiNewFrame();
        else            ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        drawUI();

        // v1.19.0 FSR — update scale
        if (fsrEnabled) {
            if (!fsrDynamicRes) {
                fsrRenderScale = getFSRScale((int)fsrMode);
                fsrCurrentScale = fsrRenderScale;
            }
        }

        // Matrices
        glm::mat4 view = glm::lookAt(cameraPos, cameraPos + cameraFront, cameraUp);
        float aspect = (display_h > 0) ? (float)display_w/(float)display_h : 1.0f;
        glm::mat4 projection = glm::perspective(glm::radians(fov), aspect, 0.05f, maxDim*50.0f);
        glm::mat4 model = glm::mat4(1.0f);
        glm::mat4 vp = projection * view;

        // v1.19.0 Optimizations — Frustum culling
        auto tCull0 = std::chrono::high_resolution_clock::now();
        bool modelInFrustum = true;
        if (optFrustumCulling) {
            modelInFrustum = isBoxInFrustum(minBB, maxBB, vp);
            if (!modelInFrustum) perfCulledTriangles = modelVertexCount/3;
            else perfCulledTriangles = 0;
        }
        int effectiveParticleCount = particleDrawCount;
        if (optDynamicParticles) {
            float distToCenter = glm::length(cameraPos - center);
            int lod = computeLODLevel(distToCenter, maxDim);
            if (lod==1) effectiveParticleCount = particleDrawCount/2;
            else if (lod==2) effectiveParticleCount = particleDrawCount/4;
            else if (lod>=3) effectiveParticleCount = particleDrawCount/8;
            if (perfFrameMs > 20.0f) effectiveParticleCount = (int)(effectiveParticleCount * 0.7f);
            if (effectiveParticleCount < 100) effectiveParticleCount = 100;
        }
        auto tCull1 = std::chrono::high_resolution_clock::now();
        perfCullingMs = std::chrono::duration<float, std::milli>(tCull1-tCull0).count();

        bool useFSR = fsrEnabled && fsrMode != FSRMode::Off && fsrLowResFBO != 0;
        bool useFG = fgEnabled && fgMode != FGMode::Off && fgRealFBO != 0;

        // =====================================================
        // v1.33.0 — КАДР В VULKAN-РЕЖИМЕ
        // Всё, что ниже до конца ветки, — OpenGL-отрисовка; Vulkan рисует сцену
        // через собственный бэкенд (swapchain, render pass, конвейеры) и интерфейс
        // через imgui_impl_vulkan, без единого вызова OpenGL.
        // =====================================================
        if (vulkanMode) {
            // Начало кадра: ждём fence своего кадра, берём изображение swapchain,
            // открываем render pass с очисткой цвета и глубины.
            if (!g_renderer || !g_renderer->beginFrame()) {
                // Кадр пропущен (окно свёрнуто или swapchain устарел). Кадр
                // интерфейса всё равно надо закрыть: ImGui требует Render()
                // перед следующим NewFrame(), иначе сработает assert.
                ImGui::Render();
                glfwPollEvents();
            } else {
                float groundYVk = g_voxMinY + aeroGroundHeight;
                if (!std::isfinite(groundYVk)) groundYVk = minBB.y - maxDim * 0.1f;
                VulkanSceneFrame vkFrame;
                vkFrame.view = view;
                vkFrame.projection = projection;
                vkFrame.model = model;
                vkFrame.cameraPos = cameraPos;
                // То же положение света, что и в OpenGL-ветке.
                vkFrame.lightPos = center + glm::vec3(maxDim * 2.0f, maxDim * 2.5f, maxDim * 2.0f);
                vkFrame.maxDim = maxDim;
                vkFrame.groundY = groundYVk;
                vkFrame.effectiveParticleCount = effectiveParticleCount;
                vkFrame.modelVisibleInFrustum = modelInFrustum;
                g_renderer->clearColor(glm::vec3(0.05f, 0.06f, 0.09f));
                buildVulkanScene(g_renderer, vkFrame);   // сцена + flushScene внутри
                ImGui::Render();
                g_renderer->uiRender();                  // интерфейс поверх сцены
                g_renderer->endFrame();                  // submit + present
                glfwPollEvents();
            }
        } else {
            // v1.19.0 FG — begin real frame
            if (useFG) {
                beginRealFrame(view, projection, cameraPos);
            }

            // v1.19.0 FSR — begin low-res render if enabled
            if (useFG && useFSR) {
                // Both FSR+FG: render to FSR low-res, then upscale to FG real FBO
                beginFSRRender(display_w, display_h);
            } else if (useFG) {
                // Only FG: render directly to FG real FBO
                fgBindRealFBO();
                glViewport(0,0,display_w,display_h);
            } else if (useFSR) {
                beginFSRRender(display_w, display_h);
            } else {
                glBindFramebuffer(GL_FRAMEBUFFER, 0);
                glViewport(0,0,display_w,display_h);
            }

            glClearColor(bgColor.x, bgColor.y, bgColor.z, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

            glm::vec3 lightPos = center + glm::vec3(maxDim*2.0f, maxDim*2.5f, maxDim*2.0f);
            glm::vec3 lightColor(1.0f);

            if (optEarlyZ) {
                glEnable(GL_DEPTH_TEST);
                glDepthFunc(GL_LESS);
                glDepthMask(GL_TRUE);
            }

            // === Scene render lambda to avoid duplication ===
            auto renderScene = [&]() {
                if (showGroundPlane || aeroGroundEffect) {
                    float groundY = g_voxMinY + aeroGroundHeight;
                    if (!std::isfinite(groundY)) groundY = minBB.y - maxDim*0.1f;
                    glm::mat4 groundModel = glm::translate(glm::mat4(1.0f), glm::vec3(center.x, groundY, center.z));
                    groundModel = glm::scale(groundModel, glm::vec3(maxDim*2.5f, 1.0f, maxDim*2.5f));
                    glUseProgram(groundShaderProgram);
                    glUniformMatrix4fv(glGetUniformLocation(groundShaderProgram, "model"), 1, GL_FALSE, glm::value_ptr(groundModel));
                    glUniformMatrix4fv(glGetUniformLocation(groundShaderProgram, "view"), 1, GL_FALSE, glm::value_ptr(view));
                    glUniformMatrix4fv(glGetUniformLocation(groundShaderProgram, "projection"), 1, GL_FALSE, glm::value_ptr(projection));
                    glUniform3fv(glGetUniformLocation(groundShaderProgram, "lightPos"), 1, &lightPos[0]);
                    glUniform3fv(glGetUniformLocation(groundShaderProgram, "viewPos"), 1, &cameraPos[0]);
                    glUniform3fv(glGetUniformLocation(groundShaderProgram, "lightColor"), 1, &lightColor[0]);
                    glUniform3fv(glGetUniformLocation(groundShaderProgram, "groundColor"), 1, &groundColor[0]);
                    glUniform1f(glGetUniformLocation(groundShaderProgram, "alpha"), groundAlpha);
                    glBindVertexArray(groundVAO);
                    glDrawElements(GL_TRIANGLES, groundIndexCount, GL_UNSIGNED_INT, 0);
                    glBindVertexArray(0);
                    if (gridVAO != 0) {
                        glUseProgram(lineShaderProgram);
                        glUniformMatrix4fv(glGetUniformLocation(lineShaderProgram, "model"), 1, GL_FALSE, glm::value_ptr(groundModel));
                        glUniformMatrix4fv(glGetUniformLocation(lineShaderProgram, "view"), 1, GL_FALSE, glm::value_ptr(view));
                        glUniformMatrix4fv(glGetUniformLocation(lineShaderProgram, "projection"), 1, GL_FALSE, glm::value_ptr(projection));
                        glUniform1i(glGetUniformLocation(lineShaderProgram, "useVertexColor"), 0);
                        glUniform1f(glGetUniformLocation(lineShaderProgram, "alpha"), 0.18f);
                        glUniform3f(glGetUniformLocation(lineShaderProgram, "lineColor"), 0.3f, 0.3f, 0.35f);
                        glBindVertexArray(gridVAO);
                        glDrawArrays(GL_LINES, 0, 82*2);
                        glBindVertexArray(0);
                    }
                }
                if (showModel && modelInFrustum) {
                    glUseProgram(modelShaderProgram);
                    glUniformMatrix4fv(glGetUniformLocation(modelShaderProgram, "model"), 1, GL_FALSE, glm::value_ptr(model));
                    glUniformMatrix4fv(glGetUniformLocation(modelShaderProgram, "view"), 1, GL_FALSE, glm::value_ptr(view));
                    glUniformMatrix4fv(glGetUniformLocation(modelShaderProgram, "projection"), 1, GL_FALSE, glm::value_ptr(projection));
                    glUniform3fv(glGetUniformLocation(modelShaderProgram, "lightPos"), 1, &lightPos[0]);
                    glUniform3fv(glGetUniformLocation(modelShaderProgram, "viewPos"), 1, &cameraPos[0]);
                    glUniform3fv(glGetUniformLocation(modelShaderProgram, "lightColor"), 1, &lightColor[0]);
                    glUniform3fv(glGetUniformLocation(modelShaderProgram, "objectColor"), 1, &modelColor[0]);
                    glUniform1i(glGetUniformLocation(modelShaderProgram, "useLighting"), lightingEnabled ? 1 : 0);
                    glUniform1i(glGetUniformLocation(modelShaderProgram, "useVertexColor"), showPressure ? 1 : 0);
                    glUniform1i(glGetUniformLocation(modelShaderProgram, "useRealisticLighting"), aeroUseRealisticLighting ? 1 : 0);
                    glUniform1f(glGetUniformLocation(modelShaderProgram, "alpha"), 1.0f);
                    glBindVertexArray(modelVAO);
                    glDrawArrays(GL_TRIANGLES, 0, modelVertexCount);
                    glBindVertexArray(0);
                }
                if (showSlicePlane && aeroShowSlice) {
                    createSlicePlane(aeroSliceAxis, aeroSlicePos);
                    glUseProgram(lineShaderProgram);
                    glUniformMatrix4fv(glGetUniformLocation(lineShaderProgram, "model"), 1, GL_FALSE, glm::value_ptr(model));
                    glUniformMatrix4fv(glGetUniformLocation(lineShaderProgram, "view"), 1, GL_FALSE, glm::value_ptr(view));
                    glUniformMatrix4fv(glGetUniformLocation(lineShaderProgram, "projection"), 1, GL_FALSE, glm::value_ptr(projection));
                    glUniform1i(glGetUniformLocation(lineShaderProgram, "useVertexColor"), 0);
                    glUniform1f(glGetUniformLocation(lineShaderProgram, "alpha"), 0.4f);
                    glUniform3f(glGetUniformLocation(lineShaderProgram, "lineColor"), 0.2f, 0.8f, 1.0f);
                    glLineWidth(2.0f);
                    glBindVertexArray(sliceVAO);
                    glDrawArrays(GL_LINE_LOOP, 0, 4);
                    glBindVertexArray(0);
                    glLineWidth(1.0f);
                }
                glUseProgram(lineShaderProgram);
                glUniformMatrix4fv(glGetUniformLocation(lineShaderProgram, "model"), 1, GL_FALSE, glm::value_ptr(model));
                glUniformMatrix4fv(glGetUniformLocation(lineShaderProgram, "view"), 1, GL_FALSE, glm::value_ptr(view));
                glUniformMatrix4fv(glGetUniformLocation(lineShaderProgram, "projection"), 1, GL_FALSE, glm::value_ptr(projection));
                glUniform1f(glGetUniformLocation(lineShaderProgram, "alpha"), 1.0f);
                glUniform1i(glGetUniformLocation(lineShaderProgram, "useVertexColor"), 0);
                if (showBoundingBox) {
                    glUniform3fv(glGetUniformLocation(lineShaderProgram, "lineColor"), 1, &bboxColor[0]);
                    glBindVertexArray(bboxVAO);
                    glDrawArrays(GL_LINES, 0, 24);
                    glBindVertexArray(0);
                }
                if (showAxes) {
                    glBindVertexArray(axesVAO);
                    glUniform3f(glGetUniformLocation(lineShaderProgram, "lineColor"), 1,0,0);
                    glDrawArrays(GL_LINES, 0, 2);
                    glUniform3f(glGetUniformLocation(lineShaderProgram, "lineColor"), 0,1,0);
                    glDrawArrays(GL_LINES, 2, 2);
                    glUniform3f(glGetUniformLocation(lineShaderProgram, "lineColor"), 0,0,1);
                    glDrawArrays(GL_LINES, 4, 2);
                    glBindVertexArray(0);
                }
                if (showStreamlines && streamlineVertexCount > 0) {
                    glUseProgram(lineShaderProgram);
                    glUniformMatrix4fv(glGetUniformLocation(lineShaderProgram, "model"), 1, GL_FALSE, glm::value_ptr(model));
                    glUniformMatrix4fv(glGetUniformLocation(lineShaderProgram, "view"), 1, GL_FALSE, glm::value_ptr(view));
                    glUniformMatrix4fv(glGetUniformLocation(lineShaderProgram, "projection"), 1, GL_FALSE, glm::value_ptr(projection));
                    glUniform1i(glGetUniformLocation(lineShaderProgram, "useVertexColor"), 1);
                    glUniform1f(glGetUniformLocation(lineShaderProgram, "alpha"), streamlineAlpha);
                    glLineWidth(streamlineWidth);
                    glBindVertexArray(streamlineVAO);
                    glDrawArrays(GL_LINES, 0, streamlineVertexCount);
                    glBindVertexArray(0);
                    glLineWidth(1.0f);
                }
                if (showObstacle) {
                    glDepthMask(GL_FALSE);
                    glUseProgram(modelShaderProgram);
                    glm::mat4 om = glm::translate(glm::mat4(1.0f), center) *
                                   glm::scale(glm::mat4(1.0f), glm::vec3(flowParams.radiusX, flowParams.radiusY, flowParams.radiusZ));
                    glUniformMatrix4fv(glGetUniformLocation(modelShaderProgram, "model"), 1, GL_FALSE, glm::value_ptr(om));
                    glUniformMatrix4fv(glGetUniformLocation(modelShaderProgram, "view"), 1, GL_FALSE, glm::value_ptr(view));
                    glUniformMatrix4fv(glGetUniformLocation(modelShaderProgram, "projection"), 1, GL_FALSE, glm::value_ptr(projection));
                    glUniform3fv(glGetUniformLocation(modelShaderProgram, "lightPos"), 1, &lightPos[0]);
                    glUniform3fv(glGetUniformLocation(modelShaderProgram, "viewPos"), 1, &cameraPos[0]);
                    glUniform3fv(glGetUniformLocation(modelShaderProgram, "lightColor"), 1, &lightColor[0]);
                    glUniform3fv(glGetUniformLocation(modelShaderProgram, "objectColor"), 1, &obstacleColor[0]);
                    glUniform1i(glGetUniformLocation(modelShaderProgram, "useLighting"), 1);
                    glUniform1i(glGetUniformLocation(modelShaderProgram, "useVertexColor"), 0);
                    glUniform1i(glGetUniformLocation(modelShaderProgram, "useRealisticLighting"), 0);
                    glUniform1f(glGetUniformLocation(modelShaderProgram, "alpha"), obstacleAlpha);
                    glBindVertexArray(obstacleVAO);
                    glDrawElements(GL_TRIANGLES, obstacleIndexCount, GL_UNSIGNED_INT, 0);
                    glBindVertexArray(0);
                    glDepthMask(GL_TRUE);
                }
                if (showParticles) {
                    glUseProgram(particleShaderProgram);
                    glUniformMatrix4fv(glGetUniformLocation(particleShaderProgram, "model"), 1, GL_FALSE, glm::value_ptr(model));
                    glUniformMatrix4fv(glGetUniformLocation(particleShaderProgram, "view"), 1, GL_FALSE, glm::value_ptr(view));
                    glUniformMatrix4fv(glGetUniformLocation(particleShaderProgram, "projection"), 1, GL_FALSE, glm::value_ptr(projection));
                    glUniform1f(glGetUniformLocation(particleShaderProgram, "pointSize"), particleSize);
                    glBindVertexArray(particleVAO);
                    glDrawArrays(GL_POINTS, 0, effectiveParticleCount);
                    glBindVertexArray(0);
                    perfCulledParticles = particleDrawCount - effectiveParticleCount;
                }
                if (showLiftDrag) {
                    glUseProgram(lineShaderProgram);
                    glUniformMatrix4fv(glGetUniformLocation(lineShaderProgram, "model"), 1, GL_FALSE, glm::value_ptr(model));
                    glUniformMatrix4fv(glGetUniformLocation(lineShaderProgram, "view"), 1, GL_FALSE, glm::value_ptr(view));
                    glUniformMatrix4fv(glGetUniformLocation(lineShaderProgram, "projection"), 1, GL_FALSE, glm::value_ptr(projection));
                    glUniform1i(glGetUniformLocation(lineShaderProgram, "useVertexColor"), 0);
                    glUniform1f(glGetUniformLocation(lineShaderProgram, "alpha"), 1.0f);
                    glLineWidth(3.0f);
                    if (fabs(dragMagnitude) > 1e-6f) {
                        glUniform3f(glGetUniformLocation(lineShaderProgram, "lineColor"), 1,0.35f,0.15f);
                        glBindVertexArray(liftDragVAO);
                        glDrawArrays(GL_LINES, 0, 2);
                        glBindVertexArray(0);
                    }
                    if (fabs(liftMagnitude) > 1e-6f) {
                        glUniform3f(glGetUniformLocation(lineShaderProgram, "lineColor"), 0.3f,1,0.3f);
                        glBindVertexArray(liftDragVAO);
                        glDrawArrays(GL_LINES, 2, 2);
                        glBindVertexArray(0);
                    }
                    if (fabs(momentMagnitude) > 1e-9f) {
                        glUniform3f(glGetUniformLocation(lineShaderProgram, "lineColor"), 0.8f,0.3f,1.0f);
                        glBindVertexArray(liftDragVAO);
                        glDrawArrays(GL_LINES, 4, 2);
                        glBindVertexArray(0);
                    }
                    glLineWidth(1.0f);
                }
            };

            renderScene();

            // Handle FSR+FG combination
            if (useFG && useFSR) {
                // FSR low-res -> intermediate (EASU)
                glDisable(GL_DEPTH_TEST);
                glDisable(GL_BLEND);
                glBindFramebuffer(GL_FRAMEBUFFER, fsrIntermediateFBO);
                glViewport(0,0,display_w,display_h);
                glUseProgram(fsrEASUProgram);
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, fsrLowResColorTex);
                glUniform1i(glGetUniformLocation(fsrEASUProgram, "uLowRes"), 0);
                float lowW = display_w * fsrCurrentScale;
                float lowH = display_h * fsrCurrentScale;
                glUniform2f(glGetUniformLocation(fsrEASUProgram, "uLowResSize"), lowW, lowH);
                glUniform2f(glGetUniformLocation(fsrEASUProgram, "uDisplaySize"), (float)display_w, (float)display_h);
                glUniform2f(glGetUniformLocation(fsrEASUProgram, "uInputViewport"), lowW, lowH);
                glUniform2f(glGetUniformLocation(fsrEASUProgram, "uOutputViewport"), (float)display_w, (float)display_h);
                glBindVertexArray(fsrQuadVAO);
                glDrawArrays(GL_TRIANGLES, 0, 6);

                // intermediate -> FG real FBO (RCAS or blit)
                glBindFramebuffer(GL_FRAMEBUFFER, fgRealFBO);
                glViewport(0,0,display_w,display_h);
                if (fsrUseRCAS) {
                    glUseProgram(fsrRCASProgram);
                    glActiveTexture(GL_TEXTURE0);
                    glBindTexture(GL_TEXTURE_2D, fsrIntermediateTex);
                    glUniform1i(glGetUniformLocation(fsrRCASProgram, "uImage"), 0);
                    glUniform2f(glGetUniformLocation(fsrRCASProgram, "uTexelSize"), 1.0f/display_w, 1.0f/display_h);
                    glUniform1f(glGetUniformLocation(fsrRCASProgram, "uSharpness"), fsrSharpness);
                    glBindVertexArray(fsrQuadVAO);
                    glDrawArrays(GL_TRIANGLES, 0, 6);
                } else {
                    glBindFramebuffer(GL_READ_FRAMEBUFFER, fsrIntermediateFBO);
                    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fgRealFBO);
                    glBlitFramebuffer(0,0,display_w,display_h, 0,0,display_w,display_h, GL_COLOR_BUFFER_BIT, GL_LINEAR);
                }
                glBindVertexArray(0);
                glEnable(GL_DEPTH_TEST);
                glEnable(GL_BLEND);
                glBindFramebuffer(GL_FRAMEBUFFER, 0);
            } else if (useFSR) {
                endFSRRenderAndUpscale(display_w, display_h);
            } else if (useFG) {
                // Already rendered to FG real FBO, unbind
                glBindFramebuffer(GL_FRAMEBUFFER, 0);
                glViewport(0,0,display_w,display_h);
            }

            // v1.19.0 Frame Generation presentation
            if (useFG) {
                int mult = getFGMultiplier((int)fgMode);
                // If we have history, generate interpolated frames
                if (hasFGHistory() && mult > 1) {
                    for (int i=0;i<mult-1;i++) {
                        float alpha = (float)(i+1) / (float)mult;
                        generateInterpolatedFrame(alpha, display_w, display_h);
                        // Present interpolated
                        presentInterpolatedFrame(display_w, display_h);
                        // UI for interpolated frame (optional, but we render UI to avoid flicker)
                        ImGui::Render();
                        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
                        glfwSwapBuffers(window);
                        glfwPollEvents();
                        // Small pacing
                        if (optFramePacing) {
                            float targetMs = 1000.0f / (optTargetFPS * mult);
                            std::this_thread::sleep_for(std::chrono::milliseconds((int)(targetMs*0.5f)));
                        }
                    }
                }
                // Present real frame
                presentRealFrame(display_w, display_h);
                // UI on top of real
                ImGui::Render();
                ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
                glfwSwapBuffers(window);
                glfwPollEvents();

                // Update history for next frame
                updateFGHistory(view, projection, cameraPos);
                fgEffectiveFPS = (deltaTime>1e-6f) ? (1.0f/deltaTime)*mult : 0.0f;
            } else {
                // No FG — normal UI + swap
                ImGui::Render();
                ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
                glfwSwapBuffers(window);
                glfwPollEvents();
            }

        }

        // VSync toggle handling
        static bool lastVsync = vsyncEnabled;
        if (lastVsync != vsyncEnabled) {
            if (vulkanMode) {
                // В Vulkan режим показа задаётся в swapchain — рендерер
                // пересоздаст её на следующем кадре.
                if (g_renderer) g_renderer->setVSync(vsyncEnabled);
            } else {
                glfwSwapInterval(vsyncEnabled ? 1 : 0);
            }
            lastVsync = vsyncEnabled;
        }
    }

    // Save settings on exit
    if (aeroSaveSettings) {
        saveSettings("aeros_settings.ini");
        std::cout << "[Settings] Saved to aeros_settings.ini" << std::endl;
        DBG_INFO("настройки сохранены в aeros_settings.ini");
    }

    // Cleanup
    std::cout << "[Main] Cleaning up v1.19.0..." << std::endl;
    shutdownInterestingFeatures();
    // v1.33.0 — FSR/FG и GL-ресурсы существуют только в OpenGL-режиме.
    if (!vulkanMode) {
        shutdownFrameGen();
        shutdownFSR();
        glDeleteProgram(modelShaderProgram);
        glDeleteProgram(particleShaderProgram);
        glDeleteProgram(lineShaderProgram);
        glDeleteProgram(groundShaderProgram);
        cleanupGLResources();
    }
    shutdownLBM();

    // Порядок важен: сначала бэкенд интерфейса (он владеет пулом дескрипторов и
    // конвейером ImGui), потом сам контекст ImGui, и только затем рендерер —
    // иначе Vulkan-объекты уничтожались бы после того, как их уже нельзя трогать.
    if (vulkanMode) {
        if (g_renderer) g_renderer->shutdownUiBackend();
    } else {
        ImGui_ImplOpenGL3_Shutdown();
    }
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    shutdownRenderer();
    glfwTerminate();
    DBG_INFO("=== завершение работы: кадров %lld, ошибок в журнале %d, откатов GPU на CPU %d ===",
             g_debugFrameCounter, debugLogErrorCount(), g_debugCudaFallbacks);
    DBG_INFO("файл журнала: %s", debugLogFilePath().c_str());

    // v1.26.0 — освобождаем память GPU (раньше поле SDF не освобождалось никогда)
    cudaReleaseAll();

    // История физики сохраняется автоматически, если был включён режим отладки:
    // вместе с отчётом это даёт полную картину того, что происходило в расчёте.
    if (debugLogEnabled() && timelapseFrameCount() > 0) {
        std::string tlPath = timelapseExportCSV();
        if (!tlPath.empty()) {
            debugLogAddFact("Таймлапс", "Файл истории", tlPath);
            DBG_INFO("таймлапс физики сохранён: %s", tlPath.c_str());
        }
    }
    timelapseShutdown();
    debugLogClose();
    std::cout << "[Main] Exit OK v1.9.0" << std::endl;
    return 0;
}
