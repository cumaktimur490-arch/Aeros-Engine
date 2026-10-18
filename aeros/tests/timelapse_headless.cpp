// Headless-тест модуля таймлапса (v1.26.0).
// Компилируется на Linux без OpenGL: все внешние глобальные переменные и функции
// движка заменены заглушками, сам timelapse.cpp и debug_log.cpp — настоящие.
#include "timelapse.h"
#include "debug_log.h"
#include "lbm.h"
#include "flow_field.h"
#include "globals.h"
#include "atmosphere.h"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

// ---- заглушки глобальных данных движка -----------------------------------
float deltaTime = 1.0f / 60.0f;
int   useCUDA = 1;
int   numParticles = 1500, particleDrawCount = 1500;
int   numStreamlines = 8, streamlineVertexCount = 640;
std::vector<float> particlePositions;
glm::vec3 liftVector(0.0f, 12.5f, 0.0f), dragVector(3.0f, 0.0f, 0.0f);
glm::vec3 momentVector(0.1f, 0.2f, 0.3f), centerOfPressure(0.5f, 0.5f, 0.5f);
float perfFrameMs = 8.0f, perfLBMms = 3.0f, perfParticlesMs = 1.0f;
float perfForcesMs = 1.5f, perfStreamlinesMs = 0.5f, perfVoxelMs = 0.2f;
float airDensity = 1.225f, flowSpeed = 12.0f;
glm::vec3 minBB(-1, -1, -1), maxBB(1, 1, 1);
float maxDim = 2.0f;
FlowParams flowParams;

// v1.26.0 — новые глобальные переменные (в движке живут в globals.cpp)
bool  tlEnabled = false, tlPlaying = false, tlLoop = true, tlShowField = true;
bool  tlAutoRecord = true, tlShowWindow = false;
float tlSpeed = 4.0f;
int   tlCapacity = 6000, tlFieldEvery = 20, tlChartFrames = 600;
bool  aeroCpAutoRange = true;
float aeroCpMin = -2.0f, aeroCpMax = 2.0f;
float g_cpLo = -2.0f, g_cpHi = 2.0f;
float lbmPressureMin = 0.0f, lbmPressureMax = 0.0f;
float lbmResidualRho = 0.0f, lbmResidualU = 0.0f;
long long lbmNegativePops = 0, lbmBadPops = 0;   // v1.27.0
std::string g_lastTimelapsePath;

LBMParams lbmParams;
bool lbmInitialized = true;
int  lbmCurrentStep = 0;
float lbmAvgRho = 1.0f, lbmMaxVelocityWorld = 14.0f, lbmTKE = 0.01f, lbmReynolds = 1e5f;
int  lbmNx = 8, lbmNy = 8, lbmNz = 8;
float lbmMinX = -2, lbmMinY = -2, lbmMinZ = -2;
float lbmMaxX =  2, lbmMaxY =  2, lbmMaxZ =  2;
float lbmCellSizeX = 0.5f, lbmCellSizeY = 0.5f, lbmCellSizeZ = 0.5f;
std::vector<float> lbmPressure;
std::vector<char>  lbmIsSolid, lbmIsGround;

bool isLBMSolidWorld(const glm::vec3& p) { (void)p; return false; }
bool getLBMPressureWorldChecked(const glm::vec3& p, float* out) {
    // синтетическое поле: давление растёт вдоль X
    if (!out) return false;
    *out = 5.0f * (float)p.x;
    return true;
}
float getLBMPressureWorld(const glm::vec3& p) { float v = 0; getLBMPressureWorldChecked(p, &v); return v; }
glm::vec3 computeVelocityFieldCPU(const glm::vec3& p, const FlowParams& prm) {
    (void)p; return glm::vec3(prm.vx, prm.vy, prm.vz);
}

// ---- проверки ------------------------------------------------------------
static int g_failed = 0;
static void check(bool cond, const std::string& what) {
    if (!cond) { std::printf("  FAIL: %s\n", what.c_str()); ++g_failed; }
    else       { std::printf("  ok:   %s\n", what.c_str()); }
}

int main() {
    std::printf("=== тест таймлапса физики ===\n");
    debugLogInit("/tmp/tl/app");

    lbmParams.enabled = true;
    flowParams.vx = 12.0f; flowParams.vy = 0.0f; flowParams.vz = 0.0f;
    particlePositions.assign(1500 * 3, 0.5f);
    particlePositions[7] = std::nanf("");           // чтобы проверить счётчик NaN/Inf
    lbmPressure.assign((size_t)lbmNx * lbmNy * lbmNz, 1.0f);
    lbmIsSolid.assign((size_t)lbmNx * lbmNy * lbmNz, 0);
    lbmIsGround.assign((size_t)lbmNx * lbmNy * lbmNz, 0);

    tlCapacity = 100; tlFieldEvery = 5; tlAutoRecord = false; tlEnabled = true;
    tlLoop = true; tlSpeed = 4.0f; tlChartFrames = 60;
    aeroCpAutoRange = false; aeroCpMin = -2.0f; aeroCpMax = 2.0f;

    timelapseInit();
    check(timelapseReady(), "буфер готов после timelapseInit");

    for (long long fr = 0; fr < 250; ++fr) {
        lbmCurrentStep = (int)fr;
        perfFrameMs = 8.0f + (float)(fr % 7);
        lbmTKE = 0.01f * (float)(1 + fr % 5);
        timelapseRecordFrame(fr, (float)fr * (1.0f / 60.0f));
    }
    check(timelapseFrameCount() == 100, "в кольце ровно 100 кадров (ёмкость)");
    check(timelapseFramesRecorded() == 250, "всего записано 250 кадров");
    check(timelapseTotalDropped() == 150, "150 кадров перезаписаны");

    TLFrame f;
    check(timelapseGetFrame(0, &f) && f.frame == 150, "самый старый кадр в буфере — №150");
    check(timelapseGetFrame(99, &f) && f.frame == 249, "самый свежий кадр — №249");
    check(timelapseGetFrame(100, &f) == false, "выход за границы буфера отклоняется");
    check(timelapseGetFrame(-1, &f) == false, "отрицательный индекс отклоняется");
    check(f.frame == 249 && timelapseIndexNearestFrame(200) == 50, "поиск по номеру кадра: 200 -> индекс 50");

    timelapseGetFrame(50, &f);
    check(f.frame == 200 && std::fabs(f.time_s - 200.0f / 60.0f) < 1e-4f, "время кадра записано верно");
    check(f.backendCuda == 1 && f.drawn == 1500, "бэкенд и число частиц записаны");
    check(f.nonfinite > 0, "счётчик NaN/Inf увидел подмешанный NaN");
    check(std::fabs(f.lift[1] - 12.5f) < 1e-5f, "вектор подъёмной силы записан");

    check(timelapseFieldCount() == 20, "снимков поля ровно 20 (ёмкость кольца снимков)");
    std::vector<unsigned char> field; float lo = 0, hi = 0; long long used = 0;
    check(timelapseFieldForFrame(200, &field, &lo, &hi, &used), "снимок для кадра 200 найден");
    check(used == 200, "снимок принадлежит именно кадру 200");
    check((int)field.size() == TL_FIELD_RES * TL_FIELD_RES * TL_FIELD_RES, "размер снимка = 24^3");
    int filled = 0;
    for (size_t i = 0; i < field.size(); ++i) if (field[i] != TL_FIELD_NODATA) ++filled;
    check(filled > 1000, "в снимке есть данные (заполнено ячеек: " + std::to_string(filled) + ")");
    check(std::fabs(lo + 2.0f) < 1e-4f && std::fabs(hi - 2.0f) < 1e-4f, "диапазон снимка [-2, 2]");
    check(timelapseFieldForFrame(10, &field, &lo, &hi, &used) == false,
          "для кадра, который уже перезаписан, снимок честно не отдаётся");

    // воспроизведение
    timelapseSetPlayIndex(0);
    check(timelapsePlayIndex() == 0, "курсор в начале");
    float recFps = timelapseRecordedFps();
    check(recFps > 55.0f && recFps < 65.0f, "частота записи определена (~60 к/с)");
    tlPlaying = true;
    for (int i = 0; i < 10; ++i) timelapseAdvance(1.0f / 60.0f);   // 10 кадров по 4x
    check(timelapsePlayIndex() == 40, "скорость 4x: за 10 кадров экранного времени курсор ушёл на 40");
    timelapseSetPlayIndex(99);
    for (int i = 0; i < 5; ++i) timelapseAdvance(1.0f / 60.0f);
    check(timelapsePlayIndex() < 99, "по кругу: курсор вернулся в начало");
    tlLoop = false; tlPlaying = true;
    timelapseSetPlayIndex(99);
    timelapseAdvance(1.0f / 60.0f);
    check(tlPlaying == false, "без цикла воспроизведение остановилось в конце");
    tlLoop = true; tlPlaying = false;

    // экспорт и сводка
    std::string csv = timelapseExportCSV();
    check(!csv.empty(), "CSV экспортирован: " + csv);
    if (!csv.empty()) {
        std::ifstream in(csv.c_str());
        std::string line; int lines = 0; bool headerOk = false;
        while (std::getline(in, line)) {
            ++lines;
            if (line.compare(0, 3, "idx") == 0) headerOk = true;
        }
        check(headerOk, "в CSV есть строка заголовка");
        check(lines == 100 + 3, "в CSV 100 строк данных + заголовок + 2 комментария (получилось " + std::to_string(lines) + ")");
    }
    std::string sum = timelapseSummary();
    check(sum.find("Кадров в буфере: 100") != std::string::npos, "сводка содержит число кадров");
    check(sum.find("nan") == std::string::npos && sum.find("inf") == std::string::npos,
          "в сводке нет nan/inf");
    check(sum.find("ВНИМАНИЕ: в записи есть кадры с неконечными значениями") != std::string::npos,
          "сводка предупреждает о NaN/Inf");

    // смена ёмкости пересоздаёт буфер
    tlCapacity = 200;
    check(timelapseReady(), "timelapseReady заметил смену ёмкости");
    check(timelapseFrameCount() == 0, "после смены ёмкости буфер пуст");
    timelapseRecordFrame(1, 0.0f);
    check(timelapseFrameCount() == 1, "запись после пересоздания работает");

    // фоновая запись и пауза
    tlEnabled = false; tlAutoRecord = true;
    timelapseRecordFrame(2, 0.1f);
    check(timelapseFrameCount() == 2, "фоновая история пишется без явной записи (F7)");
    tlPlaying = true;
    timelapseRecordFrame(3, 0.2f);
    check(timelapseFrameCount() == 2, "во время воспроизведения буфер заморожен");
    tlPlaying = false;

    tlEnabled = false; tlAutoRecord = false;
    timelapseRecordFrame(4, 0.3f);
    check(timelapseFrameCount() == 2, "при выключенных записи и фоновой истории кадры не пишутся");

    timelapseClear();
    check(timelapseFrameCount() == 0 && timelapseFramesRecorded() == 0, "очистка стирает запись");
    timelapseShutdown();
    check(!timelapseReady(), "после shutdown модуль не готов");

    std::printf("\n=== %s (ошибок: %d) ===\n", g_failed == 0 ? "ВСЕ ПРОВЕРКИ ПРОЙДЕНЫ" : "ЕСТЬ ПРОВАЛЫ", g_failed);
    return g_failed == 0 ? 0 : 1;
}
