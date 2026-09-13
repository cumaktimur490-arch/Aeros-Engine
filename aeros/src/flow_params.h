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
};

#endif