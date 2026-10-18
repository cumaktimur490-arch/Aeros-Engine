// =====================================================
// Aeros Engine v1.26.0 — «Режим вычислений»: таймлапс физики (реализация).
//
// Логика намеренно отделена от отрисовки: здесь только запись, кольцевые
// буферы, воспроизведение, экспорт CSV и сводка для отладочного отчёта.
// Панель рисует drawTimelapseUI() в ui.cpp.
//
// Память: кадр ~170 байт, снимок поля TL_FIELD_RES^3 = 13824 байт.
// При ёмкости 6000 кадров и снимке каждые 20 кадров это ~1 МБ + ~4 МБ.
// =====================================================
#include "timelapse.h"
#include "globals.h"
#include "lbm.h"
#include "flow_field.h"
#include "debug_log.h"
#include "version.h"
#include "atmosphere.h"

#include <algorithm>
#include <cstdlib>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <string>
#include <vector>

namespace {

// Кадр симуляции, к которому привязан снимок поля. Храним номер кадра, а не
// индекс: оба буфера кольцевые и перезаписываются независимо, поэтому по номеру
// всегда можно проверить, не затёрт ли снимок (иначе показали бы чужие данные).
struct TLField {
    long long frameNo;
    float lo;
    float hi;
    std::vector<unsigned char> data;
    TLField() : frameNo(0), lo(0.0f), hi(0.0f) {}
};

std::vector<TLFrame> g_frames;          // кольцевой буфер кадров
std::vector<TLField> g_fields;          // кольцевой буфер снимков поля
int       g_head        = 0;            // куда писать следующий кадр
int       g_count       = 0;            // сколько кадров занято
int       g_fieldHead   = 0;
int       g_fieldCount  = 0;
long long g_totalRecorded = 0;          // всего записано (с учётом перезаписанных)
long long g_totalDropped  = 0;          // сколько кадров было перезаписано
bool      g_ready       = false;
float     g_playPos     = -1.0f;        // дробная позиция воспроизведения
float     g_playAccum   = 0.0f;
int       g_capacityUsed = 0;

inline float fin(float v) { return std::isfinite(v) ? v : 0.0f; }

// Логический индекс (0 = самый старый из хранящихся) в позицию кольца
int ringPos(int logicalIdx) {
    if (g_count <= 0) return 0;
    int start = (g_head - g_count + (int)g_frames.size()) % (int)g_frames.size();
    if (start < 0) start += (int)g_frames.size();
    return (start + logicalIdx) % (int)g_frames.size();
}

int fieldRingPos(int logicalIdx) {
    if (g_fieldCount <= 0) return 0;
    int cap = (int)g_fields.size();
    int start = (g_fieldHead - g_fieldCount + cap) % cap;
    if (start < 0) start += cap;
    return (start + logicalIdx) % cap;
}

std::string stampNow() {
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm;
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%04d%02d%02d-%02d%02d%02d",
                  tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                  tm.tm_hour, tm.tm_min, tm.tm_sec);
    return std::string(buf);
}

// Активный диапазон Cp для квантования снимков (тот же, что у раскраски модели)
void activeCpRange(float* lo, float* hi) {
    float a = aeroCpAutoRange ? g_cpLo : aeroCpMin;
    float b = aeroCpAutoRange ? g_cpHi : aeroCpMax;
    if (!std::isfinite(a)) a = -2.0f;
    if (!std::isfinite(b)) b =  2.0f;
    if (b - a < 1e-4f) { a = -1.0f; b = 1.0f; }
    *lo = a; *hi = b;
}

// Снимок поля давления: огрубленный куб TL_FIELD_RES^3, 1 байт на ячейку.
// Если LBM включён — берём его давление (трилинейно). Если нет — считаем Cp по
// аналитическому полю скоростей, чтобы таймлапс работал и без решателя.
bool captureField(std::vector<unsigned char>* out, float* lo, float* hi) {
    if (!out) return false;
    const int R = TL_FIELD_RES;
    out->assign((size_t)R * R * R, (unsigned char)TL_FIELD_NODATA);

    float xmin, ymin, zmin, xmax, ymax, zmax;
    const bool lbm = (lbmParams.enabled && lbmInitialized && !lbmPressure.empty()
                      && lbmNx > 1 && lbmNy > 1 && lbmNz > 1);
    if (lbm) {
        xmin = lbmMinX; ymin = lbmMinY; zmin = lbmMinZ;
        xmax = lbmMaxX; ymax = lbmMaxY; zmax = lbmMaxZ;
    } else {
        // область модели, расширенная на 40%, чтобы видеть след
        float cx = 0.5f * (minBB.x + maxBB.x);
        float cy = 0.5f * (minBB.y + maxBB.y);
        float cz = 0.5f * (minBB.z + maxBB.z);
        float ex = 0.7f * std::max(1e-3f, maxBB.x - minBB.x);
        float ey = 0.7f * std::max(1e-3f, maxBB.y - minBB.y);
        float ez = 0.7f * std::max(1e-3f, maxBB.z - minBB.z);
        xmin = cx - ex; xmax = cx + ex;
        ymin = cy - ey; ymax = cy + ey;
        zmin = cz - ez; zmax = cz + ez;
    }
    if (!(std::isfinite(xmin) && std::isfinite(xmax)) || xmax - xmin < 1e-6f) return false;
    if (!(std::isfinite(ymin) && std::isfinite(ymax)) || ymax - ymin < 1e-6f) return false;
    if (!(std::isfinite(zmin) && std::isfinite(zmax)) || zmax - zmin < 1e-6f) return false;

    float cpLo, cpHi;
    activeCpRange(&cpLo, &cpHi);
    float invSpan = 1.0f / (cpHi - cpLo);

    float vinf = std::sqrt(flowParams.vx * flowParams.vx +
                           flowParams.vy * flowParams.vy +
                           flowParams.vz * flowParams.vz);
    if (!std::isfinite(vinf) || vinf < 1e-4f) vinf = std::max(1e-4f, fin(flowSpeed));
    float invV2 = 1.0f / std::max(1e-8f, vinf * vinf);

    float dx = (xmax - xmin) / (float)(R - 1);
    float dy = (ymax - ymin) / (float)(R - 1);
    float dz = (zmax - zmin) / (float)(R - 1);

    int filled = 0;
    for (int iz = 0; iz < R; ++iz) {
        float wz = zmin + dz * (float)iz;
        for (int iy = 0; iy < R; ++iy) {
            float wy = ymin + dy * (float)iy;
            for (int ix = 0; ix < R; ++ix) {
                float wx = xmin + dx * (float)ix;
                glm::vec3 p(wx, wy, wz);
                float cp = 0.0f;
                bool valid = false;
                if (lbm) {
                    float pa = 0.0f;
                    // твёрдые ячейки считаем «нет данных» — на срезе они серые
                    if (!isLBMSolidWorld(p) && getLBMPressureWorldChecked(p, &pa)) {
                        float q = 0.5f * airDensity * vinf * vinf;
                        if (q < 1e-6f) q = 1e-6f;
                        cp = pa / q;
                        valid = true;
                    }
                } else {
                    glm::vec3 v = computeVelocityFieldCPU(p, flowParams);
                    if (std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z)) {
                        // Бернулли: Cp = 1 - (V/Vinf)^2
                        cp = 1.0f - (v.x * v.x + v.y * v.y + v.z * v.z) * invV2;
                        valid = true;
                    }
                }
                if (!valid) continue;
                if (!std::isfinite(cp)) continue;
                float t = (cp - cpLo) * invSpan;
                t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
                (*out)[(size_t)(iz * R + iy) * R + ix] = (unsigned char)(t * 250.0f);
                ++filled;
            }
        }
    }
    *lo = cpLo; *hi = cpHi;
    return filled > 0;
}

} // namespace

// --- жизненный цикл -------------------------------------------------------
void timelapseInit() {
    int cap = tlCapacity;
    if (cap < 60) cap = 60;
    if (cap > 200000) cap = 200000;
    try {
        g_frames.assign((size_t)cap, TLFrame());
    } catch (...) {
        g_frames.clear();
        g_ready = false;
        DBG_ERROR("таймлапс: не удалось выделить буфер на %d кадров", cap);
        return;
    }
    int fieldCap = (tlFieldEvery > 0) ? std::max(8, cap / tlFieldEvery) : 8;
    if (fieldCap > 4000) fieldCap = 4000;
    try {
        g_fields.assign((size_t)fieldCap, TLField());
    } catch (...) {
        g_fields.clear();
        fieldCap = 0;
    }
    g_head = 0; g_count = 0; g_fieldHead = 0; g_fieldCount = 0;
    g_totalRecorded = 0; g_totalDropped = 0;
    g_playPos = -1.0f; g_playAccum = 0.0f;
    g_capacityUsed = cap;
    g_ready = !g_frames.empty();
    if (g_ready)
        DBG_INFO("таймлапс: буфер готов — %d кадров, %d снимков поля по %d^3 ячеек (~%.1f МБ)",
                 cap, fieldCap, TL_FIELD_RES,
                 (cap * (float)sizeof(TLFrame) + (float)fieldCap * (float)(TL_FIELD_RES * TL_FIELD_RES * TL_FIELD_RES)) / 1048576.0f);
}

void timelapseShutdown() {
    std::vector<TLFrame>().swap(g_frames);
    std::vector<TLField>().swap(g_fields);
    g_head = g_count = g_fieldHead = g_fieldCount = 0;
    g_totalRecorded = g_totalDropped = 0;
    g_ready = false;
    g_playPos = -1.0f;
}

void timelapseClear() {
    g_head = 0; g_count = 0; g_fieldHead = 0; g_fieldCount = 0;
    g_totalRecorded = 0; g_totalDropped = 0;
    g_playPos = -1.0f; g_playAccum = 0.0f;
    debugLogAction("таймлапс", "запись очищена");
}

bool timelapseReady() {
    // Ёмкость могла измениться в панели — пересоздаём буфер и пишем об этом.
    if (g_ready && tlCapacity != g_capacityUsed) {
        DBG_INFO("таймлапс: ёмкость изменена %d -> %d кадров, буфер пересоздан (запись очищена)",
                 g_capacityUsed, tlCapacity);
        timelapseInit();
    }
    return g_ready;
}

// --- запись ---------------------------------------------------------------
void timelapseRecordFrame(long long frameNumber, float timeSeconds) {
    // tlAutoRecord держит историю всегда (кольцо перезаписывается само),
    // tlEnabled — явная запись по F7. Для истории важна хотя бы одна из них.
    if (!tlEnabled && !tlAutoRecord) return;
    // Просмотр замораживает буфер: иначе кольцо перезаписывалось бы прямо под
    // курсором, и пользователь видел бы «плывущую» историю вместо записи.
    if (tlPlaying) return;
    if (!timelapseReady()) timelapseInit();
    if (!g_ready) return;

    TLFrame f;
    f.frame       = frameNumber;
    f.time_s      = fin(timeSeconds);
    f.dt_ms       = fin(deltaTime) * 1000.0f;
    f.fps         = (deltaTime > 1e-6f) ? (1.0f / deltaTime) : 0.0f;
    f.backendCuda = useCUDA ? 1 : 0;
    f.particles   = numParticles;
    f.drawn       = particleDrawCount;
    f.streamlines = numStreamlines;
    f.slVerts     = streamlineVertexCount;
    f.lbmStep     = lbmCurrentStep;
    f.lbmRho      = fin(lbmAvgRho);
    f.lbmVmax     = fin(lbmMaxVelocityWorld);
    f.lbmTke      = fin(lbmTKE);
    f.reynolds    = fin(lbmReynolds);
    f.lift[0]   = fin(liftVector.x);   f.lift[1]   = fin(liftVector.y);   f.lift[2]   = fin(liftVector.z);
    f.drag[0]   = fin(dragVector.x);   f.drag[1]   = fin(dragVector.y);   f.drag[2]   = fin(dragVector.z);
    f.moment[0] = fin(momentVector.x); f.moment[1] = fin(momentVector.y); f.moment[2] = fin(momentVector.z);
    f.cop[0]    = fin(centerOfPressure.x); f.cop[1] = fin(centerOfPressure.y); f.cop[2] = fin(centerOfPressure.z);
    f.frameMs   = fin(perfFrameMs);
    f.lbmMs     = fin(perfLBMms);
    f.partMs    = fin(perfParticlesMs);
    f.forceMs   = fin(perfForcesMs);
    f.streamMs  = fin(perfStreamlinesMs);
    f.voxelMs   = fin(perfVoxelMs);
    f.residualRho = fin(lbmResidualRho);
    f.residualU   = fin(lbmResidualU);
    f.negPops     = lbmNegativePops;
    f.badPops     = lbmBadPops;
    f.cpLo      = fin(g_cpLo);
    f.cpHi      = fin(g_cpHi);
    // Проверка устойчивости (NaN/Inf). Шаг 97, как в журнале физики v1.25.0,
    // пропускал до 96/97 неконечных значений: единичный NaN в позициях частиц
    // почти никогда не попадал в выборку, а именно он означает, что расчёт
    // развалился. Теперь массивы обычного размера проверяются целиком, а шаг
    // включается только на очень больших, чтобы не тратить миллисекунды на кадр.
    if (particlePositions.empty()) {
        f.nonfinite = 0;
    } else {
        std::size_t sz = particlePositions.size();
        std::size_t stride = (sz <= 300000) ? 1 : 16;
        f.nonfinite = debugCountNonFinite(particlePositions.data(), sz, stride);
    }

    // Снимок поля — каждый N-й кадр (и только если буфер снимков есть)
    if (tlFieldEvery > 0 && !g_fields.empty() && (frameNumber % (long long)tlFieldEvery) == 0) {
        std::vector<unsigned char> buf;
        float lo = 0.0f, hi = 0.0f;
        if (captureField(&buf, &lo, &hi)) {
            TLField& slot = g_fields[(size_t)g_fieldHead];
            slot.frameNo = frameNumber;
            slot.lo = lo; slot.hi = hi;
            slot.data.swap(buf);
            g_fieldHead = (g_fieldHead + 1) % (int)g_fields.size();
            if (g_fieldCount < (int)g_fields.size()) ++g_fieldCount;
            f.fieldFrameNo = frameNumber;
        }
    }

    g_frames[(size_t)g_head] = f;
    g_head = (g_head + 1) % (int)g_frames.size();
    if (g_count < (int)g_frames.size()) ++g_count;
    else ++g_totalDropped;
    ++g_totalRecorded;

    // Курсор воспроизведения по умолчанию смотрит на самый свежий кадр
    if (!tlPlaying) g_playPos = (float)(g_count - 1);
    if (g_playPos >= (float)g_count) g_playPos = (float)(g_count - 1);
}

long long timelapseFramesRecorded() { return g_totalRecorded; }
int       timelapseFrameCount()     { return g_count; }
long long timelapseTotalDropped()   { return g_totalDropped; }

// --- доступ к кадрам ------------------------------------------------------
bool timelapseGetFrame(int idx, TLFrame* out) {
    if (!out || idx < 0 || idx >= g_count || g_frames.empty()) return false;
    *out = g_frames[(size_t)ringPos(idx)];
    return true;
}

int timelapseIndexNearestFrame(long long frameNumber) {
    if (g_count <= 0) return -1;
    int best = -1;
    long long bestDiff = -1;
    for (int i = 0; i < g_count; ++i) {
        long long d = std::llabs(g_frames[(size_t)ringPos(i)].frame - frameNumber);
        if (bestDiff < 0 || d < bestDiff) { bestDiff = d; best = i; }
    }
    return best;
}

float timelapseDurationSeconds() {
    if (g_count < 2) return 0.0f;
    TLFrame a, b;
    if (!timelapseGetFrame(0, &a)) return 0.0f;
    if (!timelapseGetFrame(g_count - 1, &b)) return 0.0f;
    float d = b.time_s - a.time_s;
    return std::isfinite(d) && d > 0.0f ? d : 0.0f;
}

float timelapseRecordedFps() {
    if (g_count < 2) return 0.0f;
    // Средняя частота кадров записи: по суммарному dt (устойчиво к выбросам FPS)
    double sumDt = 0.0;
    int n = 0;
    for (int i = 0; i < g_count; ++i) {
        float dtms = g_frames[(size_t)ringPos(i)].dt_ms;
        if (dtms > 0.01f && dtms < 100000.0f) { sumDt += dtms; ++n; }
    }
    if (n <= 0 || sumDt <= 0.0) return 0.0f;
    float fps = (float)(1000.0 * (double)n / sumDt);
    return std::isfinite(fps) ? fps : 0.0f;
}

// --- снимки поля ----------------------------------------------------------
int timelapseFieldCount() { return g_fieldCount; }
int timelapseFieldRes()   { return TL_FIELD_RES; }

long long timelapseFieldFrameNo(int idx) {
    if (idx < 0 || idx >= g_fieldCount || g_fields.empty()) return 0;
    return g_fields[(size_t)fieldRingPos(idx)].frameNo;
}

bool timelapseFieldGet(int idx, std::vector<unsigned char>* out, float* lo, float* hi) {
    if (!out || idx < 0 || idx >= g_fieldCount || g_fields.empty()) return false;
    const TLField& f = g_fields[(size_t)fieldRingPos(idx)];
    if (f.data.empty()) return false;
    *out = f.data;
    if (lo) *lo = f.lo;
    if (hi) *hi = f.hi;
    return true;
}

bool timelapseFieldForFrame(long long frameNumber, std::vector<unsigned char>* out,
                            float* lo, float* hi, long long* usedFrameNo) {
    if (!out || g_fieldCount <= 0) return false;
    int best = -1;
    long long bestDiff = -1;
    for (int i = 0; i < g_fieldCount; ++i) {
        long long d = std::llabs(g_fields[(size_t)fieldRingPos(i)].frameNo - frameNumber);
        if (bestDiff < 0 || d < bestDiff) { bestDiff = d; best = i; }
    }
    // Допуск: не больше половины интервала между снимками, иначе показываем
    // «нет данных» вместо кадра, который к этому моменту не относится.
    long long tol = (tlFieldEvery > 0) ? (long long)tlFieldEvery : 8;
    if (tol < 1) tol = 1;
    tol = tol * 2 + 4;
    if (best < 0 || bestDiff > tol) return false;
    if (!timelapseFieldGet(best, out, lo, hi)) return false;
    if (usedFrameNo) *usedFrameNo = timelapseFieldFrameNo(best);
    return true;
}

unsigned char timelapseFieldAt(const std::vector<unsigned char>& field, int ix, int iy, int iz) {
    const int R = TL_FIELD_RES;
    if ((int)field.size() != R * R * R) return (unsigned char)TL_FIELD_NODATA;
    if (ix < 0 || iy < 0 || iz < 0) return (unsigned char)TL_FIELD_NODATA;
    if (ix >= R || iy >= R || iz >= R) return (unsigned char)TL_FIELD_NODATA;
    return field[(size_t)(iz * R + iy) * R + ix];
}

// --- воспроизведение ------------------------------------------------------
int timelapsePlayIndex() {
    if (g_count <= 0) return -1;
    int i = (int)(g_playPos + 0.5f);
    if (i < 0) i = 0;
    if (i > g_count - 1) i = g_count - 1;
    return i;
}

void timelapseSetPlayIndex(int idx) {
    if (g_count <= 0) { g_playPos = -1.0f; return; }
    if (idx < 0) idx = 0;
    if (idx > g_count - 1) idx = g_count - 1;
    g_playPos = (float)idx;
    g_playAccum = 0.0f;
}

void timelapsePlayFromStart() { timelapseSetPlayIndex(0); }
void timelapsePlayFromEnd()   { timelapseSetPlayIndex(g_count - 1); }

void timelapseAdvance(float frameDt) {
    if (!tlPlaying || g_count <= 0) return;
    if (!std::isfinite(frameDt) || frameDt <= 0.0f) frameDt = 1.0f / 60.0f;
    if (frameDt > 0.25f) frameDt = 0.25f;      // защита от «отлипшего» окна
    float recFps = timelapseRecordedFps();
    if (recFps < 1.0f) recFps = 60.0f;
    float speed = tlSpeed;
    if (!std::isfinite(speed) || speed <= 0.0f) speed = 1.0f;
    if (speed > 256.0f) speed = 256.0f;
    // speed = 1.0 соответствует реальному времени записи
    g_playAccum += frameDt * recFps * speed;
    int step = (int)g_playAccum;
    if (step <= 0) return;
    g_playAccum -= (float)step;
    float next = g_playPos + (float)step;
    if (next >= (float)(g_count - 1)) {
        if (tlLoop) {
            // переносим остаток в начало, чтобы цикл был плавным
            float over = next - (float)(g_count - 1);
            g_playPos = over;
            if (g_playPos > (float)(g_count - 1)) g_playPos = 0.0f;
        } else {
            g_playPos = (float)(g_count - 1);
            tlPlaying = false;
            debugLogAction("таймлапс", "воспроизведение дошло до конца и остановлено");
        }
    } else {
        g_playPos = next;
    }
}

bool timelapseGetPlayFrame(TLFrame* out) {
    return timelapseGetFrame(timelapsePlayIndex(), out);
}

bool timelapseGetPlayField(std::vector<unsigned char>* out, float* lo, float* hi,
                           long long* usedFrameNo) {
    TLFrame f;
    if (!timelapseGetPlayFrame(&f)) return false;
    return timelapseFieldForFrame(f.frame, out, lo, hi, usedFrameNo);
}

// --- экспорт и сводка -----------------------------------------------------
const char* timelapseCsvHeader() {
    return "idx,frame,time_s,dt_ms,fps,backend,particles,drawn,streamlines,sl_verts,"
           "lbm_step,lbm_rho,lbm_vmax_world,lbm_tke,reynolds,"
           "lift_x,lift_y,lift_z,drag_x,drag_y,drag_z,moment_x,moment_y,moment_z,"
           "cop_x,cop_y,cop_z,frame_ms,lbm_ms,particles_ms,forces_ms,streamlines_ms,"
           "voxel_ms,nonfinite,residual_rho,residual_u,cp_lo,cp_hi,"
           "lbm_neg_pops,lbm_bad_pops,has_field";
}

void timelapseCsvRow(const TLFrame& f, std::string* out) {
    if (!out) return;
    char buf[1024];
    std::snprintf(buf, sizeof(buf),
                  "%lld,%.6f,%.4f,%.3f,%s,%d,%d,%d,%d,%d,"
                  "%.6f,%.6f,%.6f,%.6f,%.3f,"
                  "%.5f,%.5f,%.5f,%.5f,%.5f,%.5f,%.5f,%.5f,"
                  "%.5f,%.5f,%.5f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,"
                  "%d,%.3e,%.3e,%.4f,%.4f,%lld,%lld,%d",
                  f.frame, f.time_s, f.dt_ms, f.fps, f.backendCuda ? "CUDA" : "CPU",
                  f.particles, f.drawn, f.streamlines, f.slVerts, f.lbmStep,
                  f.lbmRho, f.lbmVmax, f.lbmTke, f.reynolds,
                  f.lift[0], f.lift[1], f.lift[2],
                  f.drag[0], f.drag[1], f.drag[2],
                  f.moment[0], f.moment[1], f.moment[2],
                  f.cop[0], f.cop[1], f.cop[2],
                  f.frameMs, f.lbmMs, f.partMs, f.forceMs, f.streamMs, f.voxelMs,
                  f.nonfinite, f.residualRho, f.residualU, f.cpLo, f.cpHi,
                  f.negPops, f.badPops, f.fieldFrameNo ? 1 : 0);
    *out = buf;
}

std::string timelapseExportCSV() {
    if (g_count <= 0) {
        DBG_WARN("таймлапс: экспорт CSV не выполнен — запись пуста");
        return std::string();
    }
    std::string dir = debugLogDir();
    std::string path = dir.empty() ? std::string("timelapse-") + stampNow() + ".csv"
                                   : dir + "/timelapse-" + stampNow() + ".csv";
    std::ofstream f(path.c_str(), std::ios::out | std::ios::trunc);
    if (!f) {
        DBG_ERROR("таймлапс: не удалось открыть %s для записи", path.c_str());
        return std::string();
    }
    f << "# Aeros Engine v" AEROS_VERSION_STRING " — таймлапс физики\n";
    f << "# кадров: " << g_count << " из " << g_totalRecorded
      << " записанных; снимков поля: " << g_fieldCount << "\n";
    f << timelapseCsvHeader() << "\n";
    std::string row;
    for (int i = 0; i < g_count; ++i) {
        TLFrame fr;
        if (!timelapseGetFrame(i, &fr)) continue;
        timelapseCsvRow(fr, &row);
        f << i << "," << row << "\n";
    }
    bool ok = f.good();
    f.close();
    if (!ok) {
        DBG_ERROR("таймлапс: ошибка записи %s", path.c_str());
        return std::string();
    }
    debugLogAction("таймлапс", "экспортировано %d кадров в %s", g_count, path.c_str());
    return path;
}

std::string timelapseSummary() {
    std::string s;
    char buf[512];
    if (g_count <= 0) {
        s = "таймлапс: записи нет (буфер пуст)\n";
        return s;
    }
    // Статистика по ключевым величинам
    double sumFps = 0, sumFrame = 0, sumLbm = 0, sumTke = 0, sumRes = 0;
    float minFps = 1e30f, maxFps = -1e30f;
    float maxFrame = 0, maxLbm = 0, maxV = 0, maxTke = 0, maxRes = 0;
    float liftMin = 1e30f, liftMax = -1e30f, dragMin = 1e30f, dragMax = -1e30f;
    double liftSum = 0, dragSum = 0;
    long long nonfiniteFrames = 0, nonfiniteTotal = 0, cudaFrames = 0, fieldFrames = 0;
    long long negPopsTotal = 0, badPopsTotal = 0, negPopsFrames = 0;
    int n = 0;
    for (int i = 0; i < g_count; ++i) {
        TLFrame f;
        if (!timelapseGetFrame(i, &f)) continue;
        ++n;
        if (f.fps > 0.0f && f.fps < 100000.0f) {
            sumFps += f.fps;
            if (f.fps < minFps) minFps = f.fps;
            if (f.fps > maxFps) maxFps = f.fps;
        }
        sumFrame += f.frameMs; if (f.frameMs > maxFrame) maxFrame = f.frameMs;
        sumLbm   += f.lbmMs;   if (f.lbmMs   > maxLbm)   maxLbm   = f.lbmMs;
        sumTke   += f.lbmTke;  if (f.lbmTke  > maxTke)   maxTke   = f.lbmTke;
        sumRes   += f.residualRho; if (f.residualRho > maxRes) maxRes = f.residualRho;
        if (f.lbmVmax > maxV) maxV = f.lbmVmax;
        float liftLen = std::sqrt(f.lift[0]*f.lift[0] + f.lift[1]*f.lift[1] + f.lift[2]*f.lift[2]);
        float dragLen = std::sqrt(f.drag[0]*f.drag[0] + f.drag[1]*f.drag[1] + f.drag[2]*f.drag[2]);
        if (liftLen < liftMin) liftMin = liftLen;
        if (liftLen > liftMax) liftMax = liftLen;
        if (dragLen < dragMin) dragMin = dragLen;
        if (dragLen > dragMax) dragMax = dragLen;
        liftSum += liftLen; dragSum += dragLen;
        if (f.nonfinite > 0) { ++nonfiniteFrames; nonfiniteTotal += f.nonfinite; }
        negPopsTotal += f.negPops;
        badPopsTotal += f.badPops;
        if (f.negPops > 0 || f.badPops > 0) ++negPopsFrames;
        if (f.backendCuda) ++cudaFrames;
        if (f.fieldFrameNo) ++fieldFrames;
    }
    if (n <= 0) return std::string("таймлапс: записи нет\n");
    TLFrame first, last;
    timelapseGetFrame(0, &first);
    timelapseGetFrame(g_count - 1, &last);
    if (minFps > maxFps) { minFps = 0; maxFps = 0; }
    if (liftMin > liftMax) liftMin = liftMax = 0;
    if (dragMin > dragMax) dragMin = dragMax = 0;

    s += "--- Таймлапс физики (v1.26.0) ---\n";
    std::snprintf(buf, sizeof(buf),
                  "Кадров в буфере: %d (всего записано %lld, перезаписано %lld), снимков поля: %d (по %d^3)\n",
                  g_count, g_totalRecorded, g_totalDropped, g_fieldCount, TL_FIELD_RES);
    s += buf;
    std::snprintf(buf, sizeof(buf),
                  "Диапазон кадров симуляции: %lld..%lld, длительность %.2f с, средняя частота записи %.1f к/с\n",
                  first.frame, last.frame, timelapseDurationSeconds(), timelapseRecordedFps());
    s += buf;
    std::snprintf(buf, sizeof(buf),
                  "FPS: сред %.1f, мин %.1f, макс %.1f | кадр: сред %.2f мс, макс %.2f мс | LBM: сред %.2f мс, макс %.2f мс\n",
                  sumFps / n, minFps, maxFps, sumFrame / n, maxFrame, sumLbm / n, maxLbm);
    s += buf;
    std::snprintf(buf, sizeof(buf),
                  "|Подъёмная|: сред %.3f Н, мин %.3f, макс %.3f | |Сопротивление|: сред %.3f Н, мин %.3f, макс %.3f\n",
                  liftSum / n, liftMin, liftMax, dragSum / n, dragMin, dragMax);
    s += buf;
    std::snprintf(buf, sizeof(buf),
                  "vmax LBM: макс %.4f м/с | TKE: сред %.3e, макс %.3e | сходимость (RMS drho): сред %.3e, макс %.3e\n",
                  maxV, sumTke / n, maxTke, sumRes / n, maxRes);
    s += buf;
    std::snprintf(buf, sizeof(buf),
                  "Кадры на CUDA: %lld из %d | кадров с NaN/Inf: %lld (всего значений %lld)\n",
                  cudaFrames, n, nonfiniteFrames, nonfiniteTotal);
    s += buf;
    std::snprintf(buf, sizeof(buf),
                  "Аномалии решателя LBM: отрицательных популяций %lld, заменённых нефизических %lld, кадров с аномалиями %lld\n",
                  negPopsTotal, badPopsTotal, negPopsFrames);
    s += buf;
    if (nonfiniteFrames > 0)
        s += "ВНИМАНИЕ: в записи есть кадры с неконечными значениями — физика теряла устойчивость.\n";
    if (negPopsTotal > 0 || badPopsTotal > 0)
        s += "ВНИМАНИЕ: решатель LBM выходил за физический диапазон популяций — уменьшите шаг/скорость или увеличьте tau.\n";
    if (cudaFrames > 0 && cudaFrames < n)
        s += "ВНИМАНИЕ: бэкенд переключался между CUDA и CPU внутри записи.\n";
    return s;
}
