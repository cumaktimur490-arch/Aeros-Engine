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

// v1.18.0 Physics Logic Fix — точная ISA до 80км с вычисляемыми pBase для непрерывности
// Использует данные ICAO Standard Atmosphere
// Lapse rates по слоям, pBase вычисляется последовательно для точной непрерывности
struct ISALayerDef {
    float hBase;   // м
    float lapse;   // K/m
};

static AtmosphereParams calculateISA(float h) {
    AtmosphereParams atm;
    atm.altitude = h;
    if (!std::isfinite(h)) h = 0;
    h = std::max(0.0f, std::min(h, 80000.0f));

    // Определения слоев по ICAO — только hBase и lapse, остальное вычисляем
    const ISALayerDef defs[] = {
        {0.0f,     -0.0065f},  // тропосфера
        {11000.0f,  0.0f},     // тропопауза
        {20000.0f,  0.001f},   // стратосфера 1
        {32000.0f,  0.0028f},  // стратосфера 2
        {47000.0f,  0.0f},     // стратопауза
        {51000.0f, -0.0028f},  // мезосфера 1
        {71000.0f, -0.002f},   // мезосфера 2
    };
    const int numLayers = sizeof(defs)/sizeof(defs[0]);

    // Вычисляем последовательно T и P для каждого базового уровня для непрерывности
    float T_base[8], P_base[8];
    T_base[0] = 288.15f;
    P_base[0] = 101325.0f;
    for (int i=1; i<numLayers; ++i) {
        float h0 = defs[i-1].hBase;
        float h1 = defs[i].hBase;
        float dh = h1 - h0;
        float L = defs[i-1].lapse;
        float T0_ = T_base[i-1];
        float P0_ = P_base[i-1];
        float T1;
        if (fabsf(L) < 1e-8f) {
            T1 = T0_;
            float expArg = -g0*dh/(R*T0_);
            expArg = std::max(-50.0f, std::min(expArg, 50.0f));
            P_base[i] = P0_ * expf(expArg);
        } else {
            T1 = T0_ + L*dh;
            if (T1 < 50.0f) T1 = 50.0f;
            float ratio = T1 / T0_;
            if (ratio < 1e-6f) ratio = 1e-6f;
            float expnt = -g0/(L*R);
            P_base[i] = P0_ * powf(ratio, expnt);
        }
        T_base[i] = T1;
    }

    // Находим слой для h
    int idx = 0;
    for (int i=numLayers-1; i>=0; --i) {
        if (h >= defs[i].hBase) { idx = i; break; }
    }

    float hBase = defs[idx].hBase;
    float L = defs[idx].lapse;
    float Tb = T_base[idx];
    float Pb = P_base[idx];
    float dh = h - hBase;

    if (fabsf(L) < 1e-8f) {
        atm.temperature = Tb;
        float expArg = -g0*dh/(R*Tb);
        expArg = std::max(-50.0f, std::min(expArg, 50.0f));
        atm.pressure = Pb * expf(expArg);
    } else {
        atm.temperature = Tb + L*dh;
        if (atm.temperature < 50.0f) atm.temperature = 50.0f;
        float ratio = atm.temperature / Tb;
        if (ratio < 1e-6f) ratio = 1e-6f;
        float exponent = -g0/(L*R);
        if (!std::isfinite(exponent)) exponent = 5.255f;
        atm.pressure = Pb * powf(ratio, exponent);
    }

    if (!std::isfinite(atm.temperature) || atm.temperature < 50.0f) atm.temperature = 216.65f;
    if (!std::isfinite(atm.pressure) || atm.pressure < 0.01f) atm.pressure = 0.01f;

    atm.density = atm.pressure / (R * atm.temperature);
    if (!std::isfinite(atm.density) || atm.density < 1e-6f) atm.density = 1e-6f;
    atm.speedOfSound = sqrtf(gamma_air * R * atm.temperature);
    if (!std::isfinite(atm.speedOfSound) || atm.speedOfSound < 50.0f) atm.speedOfSound = 340.3f;

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
