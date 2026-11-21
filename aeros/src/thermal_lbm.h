#pragma once
// ============================================================================
// ТЕПЛОПЕРЕНОС И ПЛАВУЧЕСТЬ (v1.37.0)
//
// Отдельный модуль: пассивный скаляр (температура) переносится тем же полем
// скорости, которое считает LBM, и через силу плавучести возвращается в
// решатель. Реализация написана с нуля по классическим работам, никакой код
// сторонних решателей (в том числе FluidX3D) не заимствован:
//
//   * решётка D3Q7 для уравнения конвекции-диффузии, веса 1/4 и 1/8,
//     cs^2 = 1/4, коэффициент диффузии alpha = cs^2 (tau_T - 1/2)
//     (Wolf-Gladrow, "Lattice-Gas Cellular Automata and Lattice Boltzmann
//      Models", 2000; Sukop & Thorne, 2006, гл. 5);
//   * приближение Буссинеска: сила только по Y и только от отклонения
//     температуры, F = rho g beta (T - T_ref) (Landau & Lifshitz, т. 6, §56);
//   * сила вводится в решатель по схеме Го (Guo, Zheng & Shi, PRE 65, 2002),
//     той же, что уже используется для объёмной силы;
//   * границы: адиабата — bounce-back, заданная температура —
//     anti-bounce-back g_i = -g_opp + 2 w_i T_wall (Ziegler, J. Stat. Phys. 71,
//     1993).
//
// Всё, что модуль считает, проверяется точными решениями в
// tests/thermal_headless.cpp: затухание синусоиды, фазовая скорость при
// равномерном потоке, линейный профиль при чистой проводимости и порог
// конвекции Рэлея–Бенара.
//
// Единицы: решёточные. Температура безразмерная, hotTemp = 1, coldTemp = 0;
// число Рэлея и Прандтля задаются пользователем, из них выводятся
// alpha = nu / Pr и g = Ra * nu * alpha / (beta * dT * H^3).
// ============================================================================
#include <vector>

struct ThermalParams {
    bool  enabled = false;          // модуль включён
    float rayleigh = 1.0e5f;        // Ra = g beta dT H^3 / (nu alpha)
    float prandtl = 0.71f;          // Pr = nu / alpha (0.71 — воздух)
    bool  fixedWallTemps = true;    // false = адиабатические стенки
    float hotTemp = 1.0f;           // температура нижней стенки
    float coldTemp = 0.0f;          // температура верхней стенки
    float perturbation = 0.02f;     // начальное возмущение (доля dT) для Рэлея–Бенара
};

extern ThermalParams thermalParams;

// Поле температуры (решёточные единицы) — для интерфейса и постобработки.
extern std::vector<float> thermalT;
// Сила плавучести по Y, плотность силы в решёточных единицах; ноль, если
// модуль выключен.
extern std::vector<float> thermalBuoyancyY;

extern bool  thermalReady;
extern float thermalNu;         // nu решателя, взятая при инициализации
extern float thermalAlpha;      // коэффициент температуропроводности
extern float thermalRayleigh;   // фактическое Ra (после подстановки alpha)
extern float thermalPrandtl;    // фактическое Pr
extern float thermalGravityLb;  // g в решёточных единицах
extern float thermalNusselt;    // число Нуссельта по тепловому потоку у нижней стенки
extern float thermalMeanT;      // средняя температура жидкости

// Инициализация на уже готовой сетке LBM. solidMask — 1 для твёрдых ячеек
// (включая «землю»). nuLB — вязкость решателя в решёточных единицах.
bool thermalInit(int nx, int ny, int nz, const std::vector<char>& solidMask, float nuLB);
void thermalShutdown();
// Перезапуск поля: проводимость + синусоидальное возмущение (режим Рэлея–Бенара).
void thermalResetField();
// Загрузить произвольное поле: популяции ставятся в равновесие с нулевой
// скоростью. Нужно тестам (синусоида, ступенька) и смене начальных условий.
bool thermalUpload(const std::vector<float>& T);
// Шаг переноса температуры на текущем поле скорости (решёточные единицы).
void thermalStep(const float* ux, const float* uy, const float* uz);
// Запись в журнал физики: что включено и с какими числами.
void thermalLogSummary();
