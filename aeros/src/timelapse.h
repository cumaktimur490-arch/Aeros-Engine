#ifndef TIMELAPSE_H
#define TIMELAPSE_H
// =====================================================
// Aeros Engine v1.26.0 — «Режим вычислений»: таймлапс физики.
//
// Зачем: физику (LBM, частицы, силы) нельзя «отмотать назад». Когда расчёт
// разваливается (NaN, отрыв, расходимость) или пользователь хочет показать,
// как развивалось обтекание, нужен записанный след. Этот модуль:
//   1) пишет кольцевой буфер кадров: все скаляры физики, силы, времена
//      подсистем, счётчик NaN/Inf, остаток сходимости LBM, диапазон Cp;
//   2) периодически снимает огрубленное поле давления (куб TL_FIELD_RES^3,
//      квантованное в 1 байт на ячейку), чтобы таймлапс можно было СМОТРЕТЬ;
//   3) умеет воспроизводить запись с любой скоростью и показывать срез поля;
//   4) выгружает всё в CSV (logs\timelapse-<дата>.csv) и даёт сводку, которая
//      попадает в отладочный отчёт (F10) — то есть таймлапс можно прислать
//      разработчику вместе с проблемой.
//
// Клавиши: F7 — запись (старт/стоп), F8 — воспроизведение/пауза.
// Модуль намеренно не зависит от OpenGL и ImGui: всю отрисовку делает
// drawTimelapseUI() в ui.cpp, а логику можно проверить отдельным тестом.
// =====================================================

#include <string>
#include <vector>

// Сторона куба снимка поля. 24^3 = 13824 байта на снимок — дёшево, но
// достаточно, чтобы увидеть зарождение и срыв вихрей.
#define TL_FIELD_RES 24
// Значение «нет данных» (твёрдое тело / вне расчётной области)
#define TL_FIELD_NODATA 255

// Один записанный кадр. Порядок полей совпадает с physics.csv, плюс новые
// величины, которых в CSV v1.25.0 не было (остаток, диапазон Cp).
struct TLFrame {
    long long frame;          // номер кадра симуляции
    float time_s;             // время симуляции, с
    float dt_ms;              // длительность кадра, мс
    float fps;                // кадры/с
    int   backendCuda;        // 1 = CUDA, 0 = CPU
    int   particles;          // сколько частиц существует
    int   drawn;              // сколько реально рисуется
    int   streamlines;        // число линий тока
    int   slVerts;            // вершин в линиях тока
    int   lbmStep;            // номер шага LBM
    float lbmRho;             // средняя плотность (решёточные единицы)
    float lbmVmax;            // максимальная скорость, м/с
    float lbmTke;             // турбулентная кинетическая энергия
    float reynolds;           // число Рейнольдса
    float lift[3];            // вектор подъёмной силы, Н
    float drag[3];            // вектор сопротивления, Н
    float moment[3];          // вектор момента, Н*м
    float cop[3];             // центр давления, мировые координаты
    float frameMs;            // время кадра, мс
    float lbmMs;              // время LBM, мс
    float partMs;             // время частиц, мс
    float forceMs;            // время сил, мс
    float streamMs;           // время линий тока, мс
    float voxelMs;            // время вокселей, мс
    int   nonfinite;          // сколько NaN/Inf насчитали в этом кадре
    float residualRho;        // RMS изменения плотности за шаг LBM (сходимость)
    float residualU;          // max изменения скорости за шаг LBM
    long long negPops;        // v1.27.0: сколько популяций f ушло в минус за шаг
    long long badPops;        // v1.27.0: сколько значений заменено как нефизические
    float cpLo;               // нижняя граница диапазона карты давления
    float cpHi;               // верхняя граница диапазона карты давления
    long long fieldFrameNo;   // номер кадра, для которого есть снимок поля (0 = нет)

    TLFrame()
        : frame(0), time_s(0), dt_ms(0), fps(0), backendCuda(0), particles(0),
          drawn(0), streamlines(0), slVerts(0), lbmStep(0), lbmRho(0), lbmVmax(0),
          lbmTke(0), reynolds(0), frameMs(0), lbmMs(0), partMs(0), forceMs(0),
          streamMs(0), voxelMs(0), nonfinite(0), residualRho(0), residualU(0),
          negPops(0), badPops(0),
          cpLo(0), cpHi(0), fieldFrameNo(0) {
        lift[0]=lift[1]=lift[2]=0; drag[0]=drag[1]=drag[2]=0;
        moment[0]=moment[1]=moment[2]=0; cop[0]=cop[1]=cop[2]=0;
    }
};

// --- жизненный цикл -------------------------------------------------------
void timelapseInit();                 // выделить буферы под текущую ёмкость
void timelapseShutdown();             // освободить всё
void timelapseClear();                // стереть запись (ёмкость сохраняется)
bool timelapseReady();                // буферы выделены

// --- запись ---------------------------------------------------------------
// Вызывается из главного цикла один раз в кадр, после расчёта физики.
// Сам читает глобальные переменные состояния (см. globals.h: tlEnabled и др.).
void timelapseRecordFrame(long long frameNumber, float timeSeconds);
long long timelapseFramesRecorded();  // всего записано кадров (с учётом перезаписи)
int  timelapseFrameCount();           // сколько кадров сейчас в буфере
long long timelapseTotalDropped();    // сколько кадров было перезаписано (кольцо)

// --- доступ к кадрам ------------------------------------------------------
// idx = 0 — самый старый из хранящихся, count-1 — самый свежий.
bool timelapseGetFrame(int idx, TLFrame* out);
int  timelapseIndexNearestFrame(long long frameNumber);  // по номеру кадра симуляции
float timelapseDurationSeconds();     // длительность записи по time_s

// --- снимки поля давления -------------------------------------------------
int  timelapseFieldCount();                            // сколько снимков
bool timelapseFieldGet(int idx, std::vector<unsigned char>* out, float* lo, float* hi);
long long timelapseFieldFrameNo(int idx);              // кадру какой снимок принадлежит
// Снимок для конкретного кадра записи (ищем ближайший в пределах допуска)
bool timelapseFieldForFrame(long long frameNumber, std::vector<unsigned char>* out,
                            float* lo, float* hi, long long* usedFrameNo);
int  timelapseFieldRes();                              // TL_FIELD_RES
// Значение ячейки снимка (0..250 = квантованный Cp, TL_FIELD_NODATA = нет данных).
// Индексация x + y*R + z*R*R; выход за границы и пустой буфер дают TL_FIELD_NODATA.
unsigned char timelapseFieldAt(const std::vector<unsigned char>& field, int ix, int iy, int iz);

// --- воспроизведение ------------------------------------------------------
int   timelapsePlayIndex();             // текущая позиция (0..count-1), -1 = нет записи
void  timelapseSetPlayIndex(int idx);
void  timelapseAdvance(float frameDt);  // двигает позицию согласно tlSpeed
void  timelapsePlayFromStart();
void  timelapsePlayFromEnd();
bool  timelapseGetPlayFrame(TLFrame* out);       // кадр под курсором
bool  timelapseGetPlayField(std::vector<unsigned char>* out, float* lo, float* hi,
                            long long* usedFrameNo);
float timelapseRecordedFps();       // средняя частота записи (для скорости 1x = реальное время)

// --- экспорт и сводка -----------------------------------------------------
std::string timelapseExportCSV();   // путь к файлу, пусто = не удалось
std::string timelapseSummary();     // многострочная сводка для отладочного отчёта
// Заголовок и строка CSV (используются и экспортом, и отладочным журналом)
const char* timelapseCsvHeader();
void timelapseCsvRow(const TLFrame& f, std::string* out);

#endif // TIMELAPSE_H
