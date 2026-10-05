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
static const float L = 0.0065f;
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

AtmosphereParams calculateAtmosphere(float h) {
    AtmosphereParams atm;
    atm.altitude = h;
    if (!std::isfinite(h)) h = 0;
    h = std::max(0.0f, std::min(h, 80000.0f));

    if (h <= 11000.0f) {
        atm.temperature = T0 - L * h;
        if (atm.temperature < 10.0f) atm.temperature = 10.0f;
        float exponent = g0 / (L * R);
        if (!std::isfinite(exponent)) exponent = 5.255f;
        float ratio = atm.temperature / T0;
        if (ratio < 1e-6f) ratio = 1e-6f;
        atm.pressure = P0 * powf(ratio, exponent);
    } else if (h <= 20000.0f) {
        float T11 = T0 - L * 11000.0f;
        float P11 = P0 * powf(T11 / T0, g0 / (L * R));
        atm.temperature = T11;
        float expArg = -g0 * (h - 11000.0f) / (R * T11);
        if (expArg < -50.0f) expArg = -50.0f;
        if (expArg > 50.0f) expArg = 50.0f;
        atm.pressure = P11 * expf(expArg);
    } else if (h <= 32000.0f) {
        float T11 = 216.65f;
        float P11 = P0 * powf(T11 / T0, g0 / (L * R)) * expf(-g0 * (20000.0f - 11000.0f) / (R * T11));
        const float L2 = 0.001f;
        const float T20 = T11;
        atm.temperature = T20 + L2 * (h - 20000.0f);
        float ratio = atm.temperature / T20;
        if (ratio < 1e-6f) ratio = 1e-6f;
        atm.pressure = P11 * powf(ratio, -g0 / (L2 * R));
    } else {
        atm.temperature = 228.65f + 0.0028f * (h - 32000.0f);
        if (atm.temperature < 150.0f) atm.temperature = 150.0f;
        if (atm.temperature > 500.0f) atm.temperature = 500.0f;
        const float P32 = 868.02f;
        float expArg = -g0 * (h - 32000.0f) / (R * atm.temperature);
        if (expArg < -50.0f) expArg = -50.0f;
        atm.pressure = P32 * expf(expArg);
    }

    if (!std::isfinite(atm.temperature) || atm.temperature < 10.0f) atm.temperature = 216.65f;
    if (!std::isfinite(atm.pressure) || atm.pressure < 0.01f) atm.pressure = 0.01f;

    atm.density = atm.pressure / (R * atm.temperature);
    if (!std::isfinite(atm.density) || atm.density < 1e-6f) atm.density = 1e-6f;
    atm.speedOfSound = sqrtf(gamma_air * R * atm.temperature);
    if (!std::isfinite(atm.speedOfSound) || atm.speedOfSound < 1.0f) atm.speedOfSound = 340.3f;

    if (atm.density < 0.00001f) atm.density = 0.00001f;
    if (atm.pressure < 0.1f) atm.pressure = 0.1f;
    if (atm.density > 5.0f) atm.density = 5.0f;

    return atm;
}

float getAirDensity(float altitudeMeters) {
    if (!std::isfinite(altitudeMeters)) return 1.225f;
    return calculateAtmosphere(altitudeMeters).density;
}

void updateAtmosphereParams() {
    if (!std::isfinite(altitude)) altitude = 0.0f;
    if (altitude < 0) altitude = 0;
    if (altitude > 80000.0f) altitude = 80000.0f;
    AtmosphereParams atm = calculateAtmosphere(altitude);
    airDensity = atm.density;
    airTemperature = atm.temperature;
    airPressure = atm.pressure;
    speedOfSound = atm.speedOfSound;
    if (!std::isfinite(airDensity)) {
        std::cerr << "[Atmosphere] Invalid density, resetting" << std::endl;
        airDensity = 1.225f;
        airTemperature = 288.15f;
        airPressure = 101325.0f;
        speedOfSound = 340.3f;
    }
}
