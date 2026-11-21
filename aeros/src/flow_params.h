#ifndef FLOW_PARAMS_H
#define FLOW_PARAMS_H

struct FlowParams {
    // Модель (bounding box)
    float centerX, centerY, centerZ;
    float radiusX, radiusY, radiusZ;

    // Поток
    float vx, vy, vz;
    float time, timeScale;

    // Вихри
    float strouhal;
    float wakeStrength;
    float wakeLength;

    // Границы
    float minX, minY, minZ;
    float maxX, maxY, maxZ;
    float maxSpeed;

    // Воксельная сетка
    int gridNx, gridNy, gridNz;
    float gridMinX, gridMinY, gridMinZ;
    float gridMaxX, gridMaxY, gridMaxZ;
    float cellSizeX, cellSizeY, cellSizeZ;
    int   gridCellCount;

    // Атмосфера
    float altitude;      // м
    float airDensity;    // кг/м³
    float airPressure;   // Pa
    float airTemperature;// K
    float speedOfSound;  // м/с

    // v1.26.0 — параметры цветовой карты давления, общие для CPU и GPU.
    // Раньше у ядра CUDA была своя зашитая шкала (cp+1)/2, поэтому при
    // переключении бэкенда модель меняла цвет: один и тот же Cp на GPU
    // рисовался иначе, чем на CPU. Теперь диапазон и номер схемы передаются
    // вместе с параметрами потока, и оба пути красят одинаково.
    float cpLo;          // нижняя граница диапазона Cp
    float cpHi;          // верхняя граница диапазона Cp
    int   colorMap;      // 0 — расходящаяся «радуга», 1 — Viridis, 2 — Plasma, 3 — Coolwarm
};

#endif