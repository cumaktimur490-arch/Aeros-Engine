// =====================================================
// Aeros Engine v1.29.0 — headless-тест фиксированного такта физики.
//
// Проверяется PhysClock из aeros/src/phys_clock.h: тот самый механизм, который
// делает физику независимой от герцовки монитора (как в BeamNG.drive — физика
// 2000 Гц при рендере 60 к/с). Модуль header-only и не тянет движок, поэтому
// тест собирается одной командой без OpenGL/GLFW/CUDA.
//
// Что принципиально важно и что здесь проверяется:
//   1) число тактов на кадр = frameDt*hz, то есть частота физики НЕ зависит от FPS;
//   2) модельное время не «уезжает» от реального ни на длинном прогоне, ни при
//      плавающем FPS — иначе обтекание развивалось бы с разной скоростью на
//      разных машинах (ровно тот дефект, который устраняет v1.29.0);
//   3) шаги решётки LBM считаются по модельному времени, дробный остаток
//      накапливается и не теряется (нет дрейфа на длинной дистанции);
//   4) защита от спирали смерти: длинный кадр не порождает тысячи догоняющих
//      тактов, а честно помечается как отставание;
//   5) мусор на входе (NaN, inf, отрицательный dt, hz=0) не ломает арифметику.
//
// Сборка и запуск: make physclock   (или g++ -std=c++17 -O1 -Wall -Wextra
//   -I../src phys_clock_headless.cpp -o /tmp/aeros-test-physclock && /tmp/...)
// =====================================================

#include "phys_clock.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <string>
#include <utility>

static int g_checks = 0;
static int g_failed = 0;

static void check(bool ok, const std::string& what) {
    ++g_checks;
    if (!ok) ++g_failed;
    std::printf("  %s %s\n", ok ? "ok:  " : "ОШИБКА:", what.c_str());
}

static bool nearD(double a, double b, double tol) { return std::fabs(a - b) <= tol; }

int main() {
    std::printf("=== Aeros Engine v1.29.0: фиксированный такт физики ===\n\n");

    // ---------- 1) значения по умолчанию ----------
    std::printf("-- настройки по умолчанию --\n");
    {
        PhysClock c;
        check(c.fixed, "по умолчанию фиксированный такт включён");
        check(nearD(c.hz, 240.0, 1e-6), "по умолчанию 240 Гц (выше герцовки обычного монитора)");
        check(c.maxSubsteps == 64, "предел тактов на кадр по умолчанию 64");
        check(c.particleSubsteps == 8, "предел подшагов частиц по умолчанию 8");
        check(nearD(c.timeScale, 1.0, 1e-6), "начальное отношение модельного времени к реальному = 1");
    }

    // ---------- 2) 60 к/с и 240 Гц: ровно 4 такта на кадр ----------
    std::printf("\n-- 60 к/с, физика 240 Гц --\n");
    {
        PhysClock c; c.hz = 240.0f;
        int lbm = 0;
        int ticks = c.advance(1.0f / 60.0f, 0.0f, 60.0f, &lbm);
        check(ticks == 4, "на кадр 60 к/с приходится 4 такта (получилось " + std::to_string(ticks) + ")");
        check(c.partSteps == 4, "подшагов частиц 4 (в пределах лимита 8)");
        check(nearD(c.frameSimDt(), 1.0 / 60.0, 1e-4),
              "модельное время кадра = длительности кадра (16.667 мс)");
        check(nearD(c.particleDt(), 1.0 / 240.0, 1e-5),
              "шаг адвекции частиц = 1/240 с = 4.167 мс");
        check(nearD(c.displayRatio, 4.0, 1e-4), "физика в 4 раза выше частоты кадров");
    }

    // ---------- 3) 2000 Гц как в BeamNG: ~33 такта на кадр ----------
    std::printf("\n-- 60 к/с, физика 2000 Гц (режим BeamNG) --\n");
    {
        PhysClock c; c.hz = 2000.0f;
        int lbm = 0;
        int ticks = c.advance(1.0f / 60.0f, 0.0f, 60.0f, &lbm);
        check(ticks == 33, "на кадр 60 к/с приходится 33 такта (получилось " + std::to_string(ticks) + ")");
        check(c.partSteps == c.particleSubsteps,
              "подшаги частиц уперлись в лимит (иначе 33 вызова адвекции на кадр)");
        check(nearD(c.displayRatio, 2000.0 / 60.0, 1e-3), "отношение к частоте кадров ≈ 33.3");
    }

    // ---------- 4) частота физики не зависит от FPS ----------
    std::printf("\n-- независимость от герцовки монитора --\n");
    {
        // Один и тот же интервал реального времени при 30, 60 и 144 к/с должен
        // дать ОДИНАКОВОЕ число тактов и одинаковое модельное время.
        auto run = [](float fps) {
            PhysClock c; c.hz = 240.0f;
            int lbm = 0;
            const float dt = 1.0f / fps;
            int frames = (int)(fps * 2.0f);          // 2 секунды реального времени
            long long ticks = 0;
            for (int i = 0; i < frames; ++i) ticks += c.advance(dt, 0.0f, fps, &lbm);
            return std::make_pair(ticks, c.simTime);
        };
        auto r30 = run(30.0f), r60 = run(60.0f), r144 = run(144.0f);
        // Целых тактов может быть на один меньше, если в конце прогона остался
        // дробный остаток в накопителе — он НЕ теряется, а переносится на следующий
        // кадр. Поэтому сравниваем с допуском в один такт и отдельно проверяем,
        // что время не потерялось: simTime + остаток накопителя = учтённое реальное.
        check(std::llabs(r30.first - r60.first) <= 1 && std::llabs(r60.first - r144.first) <= 1,
              "за 2 с при 30/60/144 к/с число тактов одинаково (±1 такт на перенос остатка): " +
              std::to_string(r30.first) + "/" + std::to_string(r60.first) + "/" + std::to_string(r144.first));
        check(nearD(r30.second, 2.0, 0.01) && nearD(r144.second, 2.0, 0.01),
              "модельное время за 2 с реального = 2 с при любой герцовке");
        check(nearD(r60.first, 480, 2), "за 2 с при 240 Гц получилось ~480 тактов");
    }

    // ---------- 5) шаги решётки LBM по модельному времени ----------
    std::printf("\n-- шаги решётки LBM --\n");
    {
        // latticeDt = 5 мс (Δx=0.1 м, velScale=20 м/с), такт 240 Гц:
        // за такт набирается 0.2 шага решётки, за кадр 60 к/с — 0.8 шага.
        PhysClock c; c.hz = 240.0f;
        const float lat = 0.005f;
        int lbm = 0, total = 0;
        for (int i = 0; i < 600; ++i) {           // 10 секунд при 60 к/с
            c.advance(1.0f / 60.0f, lat, 60.0f, &lbm);
            total += lbm;
        }
        // За 10 с модельного времени при шаге 5 мс должно быть ровно 2000 шагов.
        check(total == 2000, "за 10 с модельного времени 2000 шагов решётки (получилось "
                             + std::to_string(total) + ")");
        check(c.totalLatticeSteps == total, "счётчик totalLatticeSteps совпадает с суммой по кадрам");
        check(nearD(c.simTime, 10.0, 0.01), "модельное время 10 с");
        check(c.latticeAccum >= 0.0f && c.latticeAccum < 1.0f,
              "дробный остаток шага решётки в пределах [0,1) — дрейфа нет");
    }
    {
        // Частота тактов МЕНЬШЕ частоты шагов решётки быть не может: при 60 Гц
        // тактов и latticeDt 5 мс за один такт (16.7 мс) набирается 3 шага.
        PhysClock c; c.hz = 60.0f;
        int lbm = 0;
        c.advance(1.0f / 60.0f, 0.005f, 60.0f, &lbm);
        check(lbm == 3, "при такте 60 Гц и шаге решётки 5 мс за такт делается 3 шага (получилось "
                        + std::to_string(lbm) + ")");
    }
    {
        // Решатель выключен: шагов решётки нет, но такты идут.
        PhysClock c; c.hz = 240.0f;
        int lbm = -12345;
        int t = c.advance(1.0f / 60.0f, 0.0f, 60.0f, &lbm);
        check(t == 4 && lbm == 0, "при выключенном решателе такты есть (4), шагов решётки 0");
        int lbm2 = -1;
        c.advance(1.0f / 60.0f, std::nanf(""), 60.0f, &lbm2);
        check(lbm2 == 0, "NaN вместо длительности шага решётки не ломает счётчик");
    }

    // ---------- 6) защита от спирали смерти ----------
    std::printf("\n-- длинные кадры и предел тактов --\n");
    {
        // Здесь maxFrameDt поднят до 5 с, чтобы проверить именно ПРЕДЕЛ ТАКТОВ,
        // а не обрезку длинного кадра (она проверяется ниже отдельно).
        PhysClock c; c.hz = 240.0f; c.maxSubsteps = 64; c.maxFrameDt = 5.0f;
        int lbm = 0;
        int t = c.advance(1.0f, 0.0f, 1.0f, &lbm);     // кадр длиной 1 секунда
        check(t == 64, "кадр в 1 с не порождает 240 тактов — сработал предел 64 (получилось "
                       + std::to_string(t) + ")");
        check(c.saturated, "помечено отставание от реального времени");
        check(c.timeScale < 0.5f, "учтённое модельное время < 0.5 от реального (мы отстали)");
        check(nearD(c.simTime, 64.0 / 240.0, 1e-4), "модельное время = 64 такта по 1/240 с");
        check(c.accumulator > 0.7f && c.accumulator < 0.75f,
              "недоигранные ~0.73 с остались в накопителе и будут доиграны (время не потеряно)");
        check(nearD(c.simTime + c.accumulator, c.realTime, 1e-3),
              "simTime + остаток накопителя = учтённое реальное время");
    }
    {
        PhysClock c; c.hz = 240.0f;
        int lbm = 0;
        c.advance(10.0f, 0.0f, 0.1f, &lbm);            // alt-tab: кадр «длиной» 10 с
        check(nearD(c.realTime, 0.25, 1e-4),
              "кадр длиннее maxFrameDt обрезается до 0.25 с — часы не догоняются");
        check(c.saturated, "обрезка длинного кадра помечается как отставание");
        check(nearD(c.droppedTime, 9.75, 1e-3),
              "пропущенные 9.75 с учтены в droppedTime (получилось "
              + std::to_string(c.droppedTime) + ")");
        check(nearD(c.wallScale, 0.025, 1e-3),
              "против НАСТОЯЩИХ часов модельное время идёт в 0.025× — это видно в отчёте");
        check(nearD(c.timeScale, 1.0, 1e-4),
              "по учтённому времени отставания нет: физика не пытается догнать 10 секунд");
        check(c.ticks == 60, "за учтённые 0.25 с сделано 60 тактов (240 Гц × 0.25 с)");
    }
    {
        // После восстановления физика должна снова идти в реальном времени.
        PhysClock c; c.hz = 240.0f;
        int lbm = 0;
        c.advance(2.0f, 0.0f, 0.5f, &lbm);
        for (int i = 0; i < 600; ++i) c.advance(1.0f / 60.0f, 0.0f, 60.0f, &lbm);
        check(!c.saturated, "после длинного кадра и 10 с нормальной работы отставания нет");
        // Метрик две: timeScale — против УЧТЁННОГО времени (пропущенное сознательно
        // не догоняем), wallScale — против НАСТОЯЩИХ часов. Именно wallScale честно
        // показывает, что 1.75 с реального времени физика не считала.
        check(nearD(c.timeScale, 1.0, 1e-4),
              "по учтённому времени физика снова идёт в реальном темпе (1.0000×)");
        check(c.wallScale > 0.25f && c.wallScale < 1.0f,
              "против настоящих часов отставание на пропущенный интервал видно (0.25..1)");
        check(nearD(c.droppedTime, 1.75, 1e-3),
              "пропущено ровно 1.75 с: кадр в 2 с обрезан до 0.25 с");
        check(nearD(c.wallScale, 10.25 / 12.0, 1e-3),
              "wallScale = 10.25/12.0 — совпадает с ручной оценкой");
    }

    // ---------- 7) мусор на входе ----------
    std::printf("\n-- защита от мусорных значений --\n");
    {
        PhysClock c;
        int lbm = 0;
        int t1 = c.advance(std::nanf(""), 0.0f, 60.0f, &lbm);
        int t2 = c.advance(-5.0f, 0.0f, 60.0f, &lbm);
        int t3 = c.advance(std::numeric_limits<float>::infinity(), 0.0f, 60.0f, &lbm);
        check(t1 == 0 && t2 == 0, "NaN и отрицательный dt дают 0 тактов, а не падение");
        check(t3 >= 0 && t3 <= c.maxSubsteps, "бесконечный dt ограничен пределом тактов");
        check(std::isfinite(c.simTime) && std::isfinite(c.timeScale) && std::isfinite(c.achievedHz),
              "после мусора все измеренные величины конечны");
    }
    {
        PhysClock c;
        c.hz = 0.0f;              c.sanitize(); float h0 = c.hz;
        c.hz = -100.0f;           c.sanitize(); float h1 = c.hz;
        c.hz = std::nanf("");     c.sanitize(); float h2 = c.hz;
        c.hz = 1e9f;              c.sanitize(); float h3 = c.hz;
        check(h0 >= 1.0f && h1 >= 1.0f && h2 >= 1.0f, "частота 0/отрицательная/NaN приводится к >= 1 Гц");
        check(h3 <= 5000.0f, "частота 1e9 ограничена сверху 5000 Гц");
        c.maxSubsteps = 0;        c.sanitize();
        check(c.maxSubsteps == 1, "предел тактов 0 приводится к 1");
        c.maxSubsteps = 1000000;  c.sanitize();
        check(c.maxSubsteps == 4096, "предел тактов 1e6 ограничен 4096");
        c.particleSubsteps = -3;  c.sanitize();
        check(c.particleSubsteps == 1, "подшаги частиц -3 приводятся к 1");
        c.particleSubsteps = 10000; c.sanitize();
        check(c.particleSubsteps == 64, "подшаги частиц 10000 ограничены 64");
        c.accumulator = std::nanf(""); c.latticeAccum = -7.0f; c.sanitize();
        check(c.accumulator == 0.0f && c.latticeAccum == 0.0f,
              "испорченные накопители сбрасываются в 0");
    }

    // ---------- 8) покадровый режим (обратная совместимость) ----------
    std::printf("\n-- прежний покадровый режим --\n");
    {
        PhysClock c; c.fixed = false;
        int lbm = 0;
        int t = c.advance(1.0f / 60.0f, 0.005f, 60.0f, &lbm);
        check(t == 1, "без фиксированного такта — один «такт» на кадр");
        check(lbm == -1, "lbmSteps = -1 означает «считать по stepsPerFrame, как раньше»");
        check(c.partSteps == 1, "подшаг частиц один — как раньше, на deltaTime кадра");
        check(nearD(c.timeScale, 1.0, 1e-6), "модельное время равно реальному");
        check(nearD(c.particleDt(), 0.0, 1e-9), "particleDt в покадровом режиме = 0 (используется dt кадра)");
    }

    // ---------- 9) плавающий FPS: дрейфа модельного времени нет ----------
    std::printf("\n-- плавающий FPS (30..144 к/с) --\n");
    {
        PhysClock c; c.hz = 500.0f;
        int lbm = 0;
        long long ticks = 0;
        double realSum = 0.0;
        // Детерминированная «пила» длительностей кадра: 6.9..33 мс.
        for (int i = 0; i < 3000; ++i) {
            float dt = 0.0069f + 0.0261f * (float)((i * 37) % 100) / 100.0f;
            ticks += c.advance(dt, 0.002f, 1.0f / dt, &lbm);
            realSum += dt;
        }
        check(nearD(c.simTime, realSum, 0.01),
              "за 3000 «рваных» кадров модельное время совпало с реальным (разница < 10 мс)");
        check(nearD((double)ticks, realSum * 500.0, 2.0),
              "число тактов = реальному времени * 500 Гц (дрейфа нет)");
        long long wantSteps = (long long)(realSum / 0.002);
        check(std::llabs(c.totalLatticeSteps - wantSteps) <= 1,
              "шагов решётки за прогон = модельное время / шаг решётки (±1)");
        check(c.timeScale > 0.999f && c.timeScale < 1.001f, "отношение модельного к реальному ≈ 1");
    }

    // ---------- 10) очень высокий FPS: часть кадров без тактов ----------
    std::printf("\n-- 1000 к/с при физике 240 Гц --\n");
    {
        PhysClock c; c.hz = 240.0f;
        int lbm = 0, zeroFrames = 0, ticks = 0;
        for (int i = 0; i < 1000; ++i) {              // 1 секунда при 1000 к/с
            int t = c.advance(0.001f, 0.0f, 1000.0f, &lbm);
            ticks += t;
            if (t == 0) ++zeroFrames;
        }
        check(ticks == 240, "за секунду при 1000 к/с сделано ровно 240 тактов");
        check(zeroFrames == 760, "в 760 кадрах тактов не было — физика не «дублируется» на каждый кадр");
        check(nearD(c.simTime, 1.0, 0.01), "модельное время = 1 с");
        check(c.partSteps >= 0, "подшаги частиц не отрицательные");
    }

    // ---------- 11) монотонность и сброс ----------
    std::printf("\n-- монотонность и reset --\n");
    {
        PhysClock c; c.hz = 240.0f;
        int lbm = 0;
        double prev = 0.0; bool mono = true;
        for (int i = 0; i < 200; ++i) {
            c.advance(1.0f / 60.0f, 0.005f, 60.0f, &lbm);
            if (c.simTime < prev) mono = false;
            prev = c.simTime;
        }
        check(mono, "модельное время монотонно растёт");
        check(c.totalTicks > 0, "счётчик всего тактов растёт");
        c.reset();
        check(c.simTime == 0.0 && c.realTime == 0.0 && c.accumulator == 0.0f &&
              c.latticeAccum == 0.0f && c.totalTicks == 0 && c.totalLatticeSteps == 0,
              "reset() обнуляет всё состояние");
        check(c.hz == 240.0f && c.fixed, "reset() НЕ сбрасывает настройки пользователя");
    }

    std::printf("\n=== %s (проверок: %d, ошибок: %d) ===\n",
                g_failed == 0 ? "ТАКТ ФИЗИКИ В ПОРЯДКЕ" : "ЕСТЬ ОШИБКИ", g_checks, g_failed);
    return g_failed == 0 ? 0 : 1;
}
