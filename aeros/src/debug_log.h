#ifndef DEBUG_LOG_H
#define DEBUG_LOG_H
// =====================================================
// Aeros Engine — режим отладки: журнал событий, действий пользователя и физики.
//
// Зачем: приложение собирается с /SUBSYSTEM:WINDOWS, поэтому std::cout/std::cerr
// (включая сообщения об ошибках CUDA из kernel.cu) пользователь не видит НИКОГДА.
// Этот модуль пишет всё в файлы:
//   <папка exe>\logs\aeros-debug.log   — общий журнал (события, действия, ошибки)
//   <папка exe>\logs\physics.csv       — журнал физики по шагам (для анализа)
//   Aeros-Debug-Report-<дата>.txt      — единый отчёт, который можно прислать
//                                        разработчику (F10 или кнопка в панели)
// Если папка программы недоступна для записи (Program Files), логи кладутся в
// %LOCALAPPDATA%\AerosEngine\logs (Windows) или ~/.aeros/logs (Linux).
//
// Включение: F9, галочка «Режим отладки» в панели «Отладка и журнал»,
//            либо ключ --debug в командной строке.
// =====================================================

#include <cstddef>
#include <string>

// Уровни записи (чем больше, тем важнее)
enum DebugLogLevel {
    DBG_TRACE  = 0,
    DBG_INFO   = 1,
    DBG_PHYS   = 2,
    DBG_ACTION = 3,
    DBG_WARN   = 4,
    DBG_ERROR  = 5
};

// Инициализация журнала. dirPath — папка exe (пустая строка = определить самому).
// Безопасно вызывать повторно: файл будет переоткрыт.
void debugLogInit(const std::string& dirPath = std::string());
void debugLogClose();
bool debugLogReady();          // удалось ли открыть файл журнала

bool debugLogEnabled();        void debugLogSetEnabled(bool on);
bool debugLogActionsEnabled(); void debugLogSetActions(bool on);
bool debugLogPhysicsEnabled(); void debugLogSetPhysics(bool on);
bool debugLogOverlayEnabled(); void debugLogSetOverlay(bool on);

// Общая запись (printf-стиль). Потокобезопасно, каждый вызов сбрасывается на диск,
// чтобы журнал не обрывался при падении.
void debugLogWrite(int level, const char* fmt, ...);

// Журнал действий пользователя: что именно изменено и какое значение получилось
void debugLogAction(const char* what, const char* fmt, ...);

// Журнал физики: заголовок CSV пишется один раз, дальше по строке на кадр/шаг
void debugLogPhysicsHeader(const char* commaSeparatedColumns);
void debugLogPhysicsRow(const char* fmt, ...);

// Факты для отчёта (система, CUDA, рендерер, модель, параметры).
// Перезапись по ключу: повторный вызов с той же категорией+именем заменяет значение.
void debugLogAddFact(const std::string& category, const std::string& key, const std::string& value);

// Колбэк, который вызывается перед формированием отчёта, чтобы собрать свежие
// параметры (main.cpp регистрирует туда свою функцию).
typedef void (*DebugFactCallback)();
void debugLogSetFactCallback(DebugFactCallback cb);

// v1.26.0 — дополнительные разделы отчёта (многострочный свободный текст).
// Сводка таймлапса физики — это таблица, а не пара «ключ: значение», поэтому
// модули могут добавить в отчёт свой собственный блок. Колбэков может быть
// несколько, они вызываются в порядке регистрации после списка фактов.
typedef std::string (*DebugSectionCallback)();
void debugLogAddSectionCallback(DebugSectionCallback cb);

// Экран: последние записи журнала (0 = самая свежая)
int         debugLogRecentCount();
std::string debugLogRecent(int indexFromNewest);
long long   debugLogTotalCount();
int         debugLogErrorCount();
void        debugLogClearErrors();

// Отчёт: текст + сохранение в файл (возвращает путь, пусто = не удалось)
std::string debugLogBuildReport();
std::string debugLogSaveReport();
std::string debugLogDir();
std::string debugLogFilePath();
std::string debugLogPhysicsFilePath();

// Сколько элементов массива не являются конечными числами (NaN/Inf) —
// проверка устойчивости физики. stride позволяет смотреть каждый N-й элемент.
int debugCountNonFinite(const float* data, std::size_t count, std::size_t stride = 1);

// Удобные макросы
#define DBG_INFO(...)  debugLogWrite(DBG_INFO,  __VA_ARGS__)
#define DBG_TRACE(...) debugLogWrite(DBG_TRACE, __VA_ARGS__)
#define DBG_WARN(...)  debugLogWrite(DBG_WARN,  __VA_ARGS__)
#define DBG_ERROR(...) debugLogWrite(DBG_ERROR, __VA_ARGS__)
#define DBG_PHYS(...)  debugLogWrite(DBG_PHYS,  __VA_ARGS__)

#endif // DEBUG_LOG_H
