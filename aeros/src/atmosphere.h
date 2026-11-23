#pragma once
// =====================================================
// Атмосфера — ISA модель плотности воздуха по высоте
// =====================================================

#include <glm/glm.hpp>

enum SpeedUnit {
    SPEED_MS = 0,      // м/с
    SPEED_KMH = 1,     // км/ч
    SPEED_MPH = 2,     // миль/ч
    SPEED_KNOTS = 3,   // узлы
    SPEED_FTS = 4,     // фут/с
    SPEED_UNIT_COUNT = 5
};

struct AtmosphereParams {
    float altitude;      // высота над уровнем моря, м
    float temperature;   // K
    float pressure;      // Pa
    float density;       // кг/м³
    float speedOfSound;  // м/с
};

// Конвертация скорости
float speedToMS(float value, SpeedUnit unit);
float speedFromMS(float ms, SpeedUnit unit);
const char* speedUnitName(SpeedUnit unit);
const char* speedUnitShort(SpeedUnit unit);

// Атмосфера ISA
AtmosphereParams calculateAtmosphere(float altitudeMeters);
float getAirDensity(float altitudeMeters);
void updateAtmosphereParams();

// Глобальные параметры атмосферы (обновляются из UI)
extern float altitude;           // м, 0..20000
extern SpeedUnit speedUnit;      // выбранная единица
extern float airDensity;         // кг/м³ (вычисляется)
extern float airTemperature;     // K
extern float airPressure;        // Pa
extern float speedOfSound;       // м/с
extern bool useRealDensity;      // учитывать плотность в расчёте сил
