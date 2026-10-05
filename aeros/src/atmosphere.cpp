#include "atmosphere.h"
#include <cmath>
#include <algorithm>

// Глобальные переменные
float altitude = 0.0f;              // м
SpeedUnit speedUnit = SPEED_MS;
float airDensity = 1.225f;
float airTemperature = 288.15f;
float airPressure = 101325.0f;
float speedOfSound = 340.3f;
bool useRealDensity = true;

// Константы ISA
static const float T0 = 288.15f;        // K, температура на уровне моря
static const float P0 = 101325.0f;      // Pa, давление на уровне моря
static const float L = 0.0065f;         // K/м, температурный градиент
static const float R = 287.05f;         // Дж/(кг*К), газовая постоянная
static const float g0 = 9.80665f;       // м/с²
static const float gamma = 1.4f;        // показатель адиабаты

// Конвертация скорости
float speedToMS(float value, SpeedUnit unit) {
    switch (unit) {
        case SPEED_MS:    return value;
        case SPEED_KMH:   return value / 3.6f;
        case SPEED_MPH:   return value * 0.44704f;
        case SPEED_KNOTS: return value * 0.514444f;
        case SPEED_FTS:   return value * 0.3048f;
        default: return value;
    }
}

float speedFromMS(float ms, SpeedUnit unit) {
    switch (unit) {
        case SPEED_MS:    return ms;
        case SPEED_KMH:   return ms * 3.6f;
        case SPEED_MPH:   return ms / 0.44704f;
        case SPEED_KNOTS: return ms / 0.514444f;
        case SPEED_FTS:   return ms / 0.3048f;
        default: return ms;
    }
}

const char* speedUnitName(SpeedUnit unit) {
    switch (unit) {
        case SPEED_MS:    return "м/с";
        case SPEED_KMH:   return "км/ч";
        case SPEED_MPH:   return "миль/ч (mph)";
        case SPEED_KNOTS: return "узлы (kts)";
        case SPEED_FTS:   return "фут/с (ft/s)";
        default: return "м/с";
    }
}

const char* speedUnitShort(SpeedUnit unit) {
    switch (unit) {
        case SPEED_MS:    return "m/s";
        case SPEED_KMH:   return "km/h";
        case SPEED_MPH:   return "mph";
        case SPEED_KNOTS: return "kts";
        case SPEED_FTS:   return "ft/s";
        default: return "m/s";
    }
}

AtmosphereParams calculateAtmosphere(float h) {
    AtmosphereParams atm;
    atm.altitude = h;
    h = std::max(0.0f, std::min(h, 80000.0f)); // ограничим 80км

    if (h <= 11000.0f) {
        // Тропосфера 0-11км
        atm.temperature = T0 - L * h;
        float exponent = g0 / (L * R);
        atm.pressure = P0 * powf(atm.temperature / T0, exponent);
    } else if (h <= 20000.0f) {
        // Нижняя стратосфера 11-20км — изотермический слой T=216.65K
        float T11 = T0 - L * 11000.0f; // 216.65K
        float P11 = P0 * powf(T11 / T0, g0 / (L * R));
        atm.temperature = T11;
        atm.pressure = P11 * expf(-g0 * (h - 11000.0f) / (R * T11));
    } else if (h <= 32000.0f) {
        // 20-32км — температура растёт +1K/км
        float T11 = 216.65f;
        float P11 = P0 * powf(T11 / T0, g0 / (L * R)) * expf(-g0 * (20000.0f - 11000.0f) / (R * T11));
        const float L2 = 0.001f; // K/м, рост температуры
        const float T20 = T11; // 216.65K на 20км
        atm.temperature = T20 + L2 * (h - 20000.0f); // растёт до 228.65K на 32км
        atm.pressure = P11 * powf(atm.temperature / T20, -g0 / (L2 * R));
    } else {
        // Выше 32км — упрощённо
        atm.temperature = 228.65f + 0.0028f * (h - 32000.0f);
        if (atm.temperature < 200.0f) atm.temperature = 200.0f;
        // Очень низкое давление — P32 примерно на 32км
        const float P32 = 868.02f;
        atm.pressure = P32 * expf(-g0 * (h - 32000.0f) / (R * atm.temperature));
    }

    atm.density = atm.pressure / (R * atm.temperature);
    atm.speedOfSound = sqrtf(gamma * R * atm.temperature);

    // Защита от NaN
    if (atm.density < 0.0001f) atm.density = 0.0001f;
    if (atm.pressure < 1.0f) atm.pressure = 1.0f;

    return atm;
}

float getAirDensity(float altitudeMeters) {
    return calculateAtmosphere(altitudeMeters).density;
}

void updateAtmosphereParams() {
    AtmosphereParams atm = calculateAtmosphere(altitude);
    airDensity = atm.density;
    airTemperature = atm.temperature;
    airPressure = atm.pressure;
    speedOfSound = atm.speedOfSound;
}
