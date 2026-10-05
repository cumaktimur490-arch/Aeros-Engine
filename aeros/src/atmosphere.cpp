#include "atmosphere.h"
#include <cmath>
#include <algorithm>
#include <limits>
#include <iostream>

float altitude = 0.0f;
SpeedUnit speedUnit = SPEED_MS;
float airDensity = 1.225f;
float airTemperature = 288.15f;
float airPressure = 101325.0f;
float speedOfSound = 340.3f;
bool useRealDensity = true;

static const float T0 = 288.15f;
static const float P0 = 101325.0f;
static const float R = 287.05f;
static const float g0 = 9.80665f;
static const float gamma_air = 1.4f;

float speedToMS(float value, SpeedUnit unit) {
    if (!std::isfinite(value)) return 0.0f;
    if (value < 0) value = 0;
    if (value > 1e6f) value = 1e6f;
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
    if (!std::isfinite(ms)) return 0.0f;
    if (ms < 0) ms = 0;
    if (ms > 1e6f) ms = 1e6f;
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

// v1.14.0 Physics Ultra — полная ISA атмосфера до 80км с 7 слоями
// Источник: International Standard Atmosphere (ICAO)
// Слои: 0-11км тропосфера L=-6.5K/km, 11-20км тропопауза, 20-32км стратосфера L=+1K/km,
// 32-47км L=+2.8K/km, 47-51км L=0, 51-71км L=-2.8K/km, 71-80км L=-2K/km
struct ISALayer {
    float hBase;   // м
    float tBase;   // K
    float pBase;   // Pa
    float lapse;   // K/m
};

static AtmosphereParams calculateISA(float h) {
    AtmosphereParams atm;
    atm.altitude = h;
    if (!std::isfinite(h)) h = 0;
    h = std::max(0.0f, std::min(h, 80000.0f));

    // Предварительно рассчитанные базовые значения по ISA
    // Рассчитаны по формулам, но для стабильности захардкожены
    const ISALayer layers[] = {
        {0.0f,     288.15f, 101325.0f, -0.0065f},
        {11000.0f, 216.65f, 22632.1f,   0.0f},
        {20000.0f, 216.65f, 5474.89f,   0.001f},
        {32000.0f, 228.65f, 868.02f,    0.0028f},
        {47000.0f, 270.65f, 110.91f,    0.0f},
        {51000.0f, 270.65f, 66.94f,    -0.0028f},
        {71000.0f, 214.65f, 3.96f,     -0.002f},
    };
    const int numLayers = sizeof(layers)/sizeof(layers[0]);

    // Находим слой
    int idx = 0;
    for (int i = numLayers-1; i >=0; --i) {
        if (h >= layers[i].hBase) { idx = i; break; }
    }

    const ISALayer& L = layers[idx];
    float dh = h - L.hBase;

    if (fabsf(L.lapse) < 1e-8f) {
        // Изотермический слой: P = Pb * exp(-g0*dh/(R*Tb))
        atm.temperature = L.tBase;
        float expArg = -g0 * dh / (R * L.tBase);
        expArg = std::max(-50.0f, std::min(expArg, 50.0f));
        atm.pressure = L.pBase * expf(expArg);
    } else {
        // Градиентный слой: T = Tb + L*dh, P = Pb * (T/Tb)^(-g0/(L*R))
        atm.temperature = L.tBase + L.lapse * dh;
        if (atm.temperature < 50.0f) atm.temperature = 50.0f;
        float ratio = atm.temperature / L.tBase;
        if (ratio < 1e-6f) ratio = 1e-6f;
        float exponent = -g0 / (L.lapse * R);
        if (!std::isfinite(exponent)) exponent = 5.255f;
        atm.pressure = L.pBase * powf(ratio, exponent);
    }

    if (!std::isfinite(atm.temperature) || atm.temperature < 50.0f) atm.temperature = 216.65f;
    if (!std::isfinite(atm.pressure) || atm.pressure < 0.01f) atm.pressure = 0.01f;

    atm.density = atm.pressure / (R * atm.temperature);
    if (!std::isfinite(atm.density) || atm.density < 1e-6f) atm.density = 1e-6f;
    atm.speedOfSound = sqrtf(gamma_air * R * atm.temperature);
    if (!std::isfinite(atm.speedOfSound) || atm.speedOfSound < 50.0f) atm.speedOfSound = 340.3f;

    // Ограничения физически разумные
    atm.density = std::max(0.00001f, std::min(atm.density, 5.0f));
    atm.pressure = std::max(0.1f, atm.pressure);
    atm.temperature = std::max(50.0f, std::min(atm.temperature, 350.0f));

    return atm;
}

AtmosphereParams calculateAtmosphere(float h) {
    return calculateISA(h);
}

float getAirDensity(float altitudeMeters) {
    if (!std::isfinite(altitudeMeters)) return 1.225f;
    return calculateAtmosphere(altitudeMeters).density;
}

void updateAtmosphereParams() {
    if (!std::isfinite(altitude)) altitude = 0.0f;
    altitude = std::max(0.0f, std::min(altitude, 80000.0f));
    AtmosphereParams atm = calculateAtmosphere(altitude);
    airDensity = atm.density;
    airTemperature = atm.temperature;
    airPressure = atm.pressure;
    speedOfSound = atm.speedOfSound;
    if (!std::isfinite(airDensity) || airDensity < 1e-6f) {
        std::cerr << "[Atmosphere] Invalid density, resetting to sea level" << std::endl;
        airDensity = 1.225f;
        airTemperature = 288.15f;
        airPressure = 101325.0f;
        speedOfSound = 340.3f;
    }
}
