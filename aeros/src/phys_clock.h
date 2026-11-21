#ifndef PHYS_CLOCK_H
#define PHYS_CLOCK_H
// =====================================================
// Aeros Engine v1.29.0 — фиксированный такт физики (как в BeamNG.drive).
//
// ЗАЧЕМ. Раньше физика была привязана к кадру: за кадр решатель делал
// lbmParams.stepsPerFrame шагов решётки, а частицы интегрировались на deltaTime
// кадра. Отсюда две настоящие проблемы:
//   1) частота физики = частота кадров. На мониторе 60 Гц и на 144 Гц одно и то
//      же обтекание развивалось с разной скоростью (в 2.4 раза), то есть результат
//      зависел от железа и от того, успевает ли GPU;
//   2) шаг интегрирования «плавал» вместе с deltaTime: просадка кадра до 100 мс
//      давала один огромный шаг вместо многих маленьких — ровно то, из-за чего
//      жёсткие системы (вихри, отрыв, частицы у стенки) теряют точность.
//
// КАК В ИНЖЕНЕРНЫХ ДВИЖКАХ. BeamNG.drive считает физику с фиксированным шагом
// 0.5 мс (2000 Гц) независимо от рендера: на кадр в 60 Гц приходится ~33 такта,
// а если машина не успевает — модельное время отстаёт от реального (slow motion),
// но шаг остаётся прежним и решение не «рвётся». Здесь тот же приём:
//   accumulator += frameDt;                       // копим реальное время
//   n = accumulator / (1/hz);                     // сколько тактов пора сделать
//   n = min(n, maxSubsteps);                      // защита от спирали смерти
//   accumulator -= n / hz;                        // остаток — на следующий кадр
//   simTime += n / hz;                            // модельное время
// Каждый такт — одинаковой длины, поэтому частота физики задаётся пользователем
// (Гц) и обычно ВЫШЕ герцовки монитора: displayRatio = hz / fps.
//
// ОТДЕЛЬНО ПРО РЕШАТЕЛЬ LBM. Явный LBM нельзя «ускорить по времени»: длительность
// одного шага решётки фиксирована и равна latticeDt = Δx_физ / velScale (в
// решёточных единицах c = Δx/Δt = 1). Поэтому тактовый генератор считает шаги
// решётки по накопленному МОДЕЛЬНОМУ времени: за такт 240 Гц при latticeDt = 5 мс
// набирается 1.2 шага решётки — дробный остаток хранится в latticeAccum и не
// теряется, так что за минуту расчёт делает ровно simTime/latticeDt шагов.
// Итог: поле течёт с постоянной физической скоростью при любой герцовке монитора,
// а частицы и динамика полёта интегрируются маленьким фиксированным шагом.
//
// Модуль намеренно header-only и не зависит от движка, OpenGL и ImGui: всю
// арифметику можно проверить отдельным тестом (aeros/tests/phys_clock_headless.cpp).
// =====================================================

#include <cmath>

struct PhysClock {
    // ---------- настройки (сохраняются в aeros_settings.ini) ----------
    bool  fixed            = true;      // фиксированный такт; false = прежнее «на кадр»
    float hz               = 240.0f;    // целевая частота тактов физики, Гц
    int   maxSubsteps      = 64;        // предел тактов на кадр (спираль смерти)
    int   particleSubsteps = 8;         // предел подшагов адвекции частиц на кадр
    float maxFrameDt       = 0.25f;     // дольше этого за кадр не «догоняем» (alt-tab)

    // ---------- состояние ----------
    float  accumulator  = 0.0f;         // неистраченное реальное время, с
    float  latticeAccum = 0.0f;         // дробный остаток шагов решётки
    double simTime      = 0.0;          // модельное время, с
    double realTime     = 0.0;          // УЧТЁННОЕ реальное время, с
    double droppedTime  = 0.0;          // сколько реального времени намеренно пропущено, с
    long long totalTicks       = 0;     // всего тактов за сессию
    long long totalLatticeSteps = 0;    // всего шагов решётки за сессию

    // ---------- измерения за последний кадр ----------
    int    ticks        = 0;            // тактов физики на этом кадре
    int    latticeSteps = 0;            // шагов решётки LBM на этом кадре
    int    partSteps    = 0;            // подшагов адвекции частиц на этом кадре
    float  tickDt       = 1.0f / 240.0f;// длительность такта, с
    float  achievedHz   = 0.0f;         // фактическая частота тактов, Гц
    float  timeScale    = 1.0f;         // simTime/realTime (по учтённому времени)
    float  wallScale    = 1.0f;         // simTime/(realTime+droppedTime): против НАСТОЯЩИХ часов
    float  displayRatio = 1.0f;         // hz/fps: во сколько раз физика чаще рендера
    bool   saturated    = false;        // уперлись в maxSubsteps — модельное время отстаёт

    void reset() {
        accumulator = 0.0f; latticeAccum = 0.0f;
        simTime = 0.0; realTime = 0.0; droppedTime = 0.0;
        totalTicks = 0; totalLatticeSteps = 0;
        ticks = latticeSteps = partSteps = 0;
        saturated = false;
    }

    // Привести настройки к рабочим диапазонам. Вызывается и из advance(), чтобы
    // ни одно внешнее значение (ini, CLI, ImGui) не могло сломать арифметику.
    void sanitize() {
        if (!std::isfinite(hz) || hz < 1.0f)    hz = 1.0f;
        if (hz > 5000.0f)                       hz = 5000.0f;
        if (maxSubsteps < 1)                    maxSubsteps = 1;
        if (maxSubsteps > 4096)                 maxSubsteps = 4096;
        if (particleSubsteps < 1)               particleSubsteps = 1;
        if (particleSubsteps > 64)              particleSubsteps = 64;
        if (!std::isfinite(maxFrameDt) || maxFrameDt < 1e-4f) maxFrameDt = 1e-4f;
        if (maxFrameDt > 5.0f)                  maxFrameDt = 5.0f;
        if (!std::isfinite(accumulator))        accumulator = 0.0f;
        if (!std::isfinite(latticeAccum))       latticeAccum = 0.0f;
        if (accumulator < 0.0f)                 accumulator = 0.0f;
        if (latticeAccum < 0.0f)                latticeAccum = 0.0f;
    }

    // Главный вход: решить, сколько тактов физики сделать на этом кадре.
    //   frameDt   — длительность кадра, с
    //   latticeDt — физических секунд в одном шаге решётки LBM (<=0/NaN = решатель
    //               выключен или ещё не инициализирован, тогда шагов решётки нет)
    //   fps       — частота кадров, нужна только для отчёта displayRatio
    //   lbmSteps  — КУДА положить число шагов решётки на этот кадр;
    //               в режиме fixed=false туда пишется -1 = «считать по stepsPerFrame,
    //               как раньше».
    // Возврат: число тактов (>= 0).
    int advance(float frameDt, float latticeDt, float fps, int* lbmSteps) {
        sanitize();
        if (!std::isfinite(frameDt) || frameDt < 0.0f) frameDt = 0.0f;
        // Настоящую длительность кадра запоминаем ДО обрезки: разница между ней и
        // учтённым временем — это честно пропущенное время (droppedTime).
        const double dtTrue = (double)frameDt;
        // Длинный кадр (сворачивание окна, пауза, отладчик) не должен превращаться
        // в тысячи догоняющих тактов: ограничиваем и запоминаем, что отстали.
        bool clamped = false;
        if (frameDt > maxFrameDt) { frameDt = maxFrameDt; clamped = true; }

        tickDt = 1.0f / hz;

        if (!fixed) {
            // Прежнее поведение: один «такт» на кадр, число шагов решётки решает
            // сам решатель (stepsPerFrame), частицы идут на deltaTime кадра.
            ticks = 1;
            partSteps = 1;
            latticeSteps = 0;
            if (lbmSteps) *lbmSteps = -1;
            simTime += (double)frameDt;
            realTime += (double)frameDt;
            if (clamped) droppedTime += dtTrue - (double)frameDt;
            totalTicks += 1;
            timeScale = 1.0f;
            {
                const double wall = realTime + droppedTime;
                wallScale = (wall > 1e-9) ? (float)(simTime / wall) : 1.0f;
            }
            achievedHz = (fps > 0.5f) ? fps : 0.0f;
            displayRatio = 1.0f;
            saturated = clamped;
            return ticks;
        }

        // Настоящая длительность кадра отличается от учтённой: всё, что длиннее
        // maxFrameDt, сознательно выбрасывается (иначе после alt-tab физика
        // «догоняла» бы часы тысячами тактов). Выброшенное время копится в
        // droppedTime, чтобы отставание от НАСТОЯЩИХ часов было видно в отчёте.
        accumulator += frameDt;
        realTime += (double)frameDt;
        if (clamped) droppedTime += dtTrue - (double)frameDt;

        // Целое число тактов считаем умножением на hz (а не делением на 1/hz) и с
        // крошечным допуском. Без допуска кадр, ровно кратный такту, периодически
        // давал на один такт МЕНЬШЕ: в float 0.25/(1/240) = 59.99999 -> 59, а при
        // 60 к/с и 240 Гц деление 0.0166667/0.004166667 oscillates около 4. Из-за
        // этого частота физики дрожала (то 4, то 3 такта на кадр) — ровно то, ради
        // чего вводился фиксированный такт. Допуск 1e-4 такта — это доли
        // микросекунды, лишнего такта он создать не может.
        int n = (int)((double)accumulator * (double)hz + 1e-4);
        if (n < 0) n = 0;
        saturated = clamped;
        if (n > maxSubsteps) { n = maxSubsteps; saturated = true; }
        accumulator -= (float)n * tickDt;
        if (accumulator < 0.0f) accumulator = 0.0f;
        // Остаток не должен расти бесконечно, если расчёт хронически не успевает:
        // то, что не влезло, честно записываем в пропущенное время.
        if (accumulator > maxFrameDt) {
            droppedTime += (double)accumulator - (double)maxFrameDt;
            accumulator = maxFrameDt;
        }

        const double dtSim = (double)n * (double)tickDt;
        simTime += dtSim;
        ticks = n;
        totalTicks += n;

        // Шаги решётки — по модельному времени, дробный остаток не теряется.
        latticeSteps = 0;
        if (std::isfinite(latticeDt) && latticeDt > 1e-9) {
            latticeAccum += (float)(dtSim / (double)latticeDt);
            latticeSteps = (int)latticeAccum;
            if (latticeSteps < 0) latticeSteps = 0;
            if (latticeSteps > 4096) latticeSteps = 4096;
            latticeAccum -= (float)latticeSteps;
            if (latticeAccum < 0.0f) latticeAccum = 0.0f;
            totalLatticeSteps += latticeSteps;
        } else {
            latticeAccum = 0.0f;
        }
        if (lbmSteps) *lbmSteps = latticeSteps;

        // Адвекция частиц: шаг = tickDt, но не дороже particleSubsteps вызовов на
        // кадр — при 2000 Гц и 5000 частиц иначе весь выигрыш съест адвекция.
        partSteps = n;
        if (partSteps > particleSubsteps) partSteps = particleSubsteps;

        timeScale = (realTime > 1e-9) ? (float)(simTime / realTime) : 1.0f;
        {
            const double wall = realTime + droppedTime;
            wallScale = (wall > 1e-9) ? (float)(simTime / wall) : 1.0f;
        }
        achievedHz = (frameDt > 1e-9f) ? (float)((double)n / (double)frameDt) : 0.0f;
        displayRatio = (fps > 0.5f) ? (hz / fps) : 0.0f;
        return n;
    }

    // Длительность одного подшага адвекции частиц на этом кадре, с.
    // Равна всему модельному времени кадра, делённому на число подшагов.
    float particleDt() const {
        if (!fixed) return 0.0f;
        if (partSteps <= 0) return 0.0f;
        float dt = (float)((double)ticks * (double)tickDt) / (float)partSteps;
        if (!std::isfinite(dt) || dt < 0.0f) return 0.0f;
        if (dt > maxFrameDt) dt = maxFrameDt;
        return dt;
    }

    // Модельное время, прошедшее за этот кадр, с (для динамики полёта и отчётов).
    float frameSimDt() const {
        if (!fixed) return 0.0f;
        float dt = (float)((double)ticks * (double)tickDt);
        if (!std::isfinite(dt) || dt < 0.0f) return 0.0f;
        return dt;
    }
};

#endif // PHYS_CLOCK_H
