#!/usr/bin/env bash
# Прогон всех headless-наборов по одному (v1.36.0).
#
# Зачем скрипт, а не `make`: так можно передать EXTRA_CXXFLAGS (например, для
# ASan/UBSan) и один и тот же список наборов использовать и в CI, и локально.
# Каждый набор собирается и запускается отдельно — падение видно сразу, а хвост
# вывода уходит в аннотацию GitHub (скачать полный лог иногда невозможно).
#
#   ./run-suites.sh                       # обычный прогон
#   EXTRA_CXXFLAGS="-fsanitize=address" ./run-suites.sh
set -uo pipefail

SUITES=(timelapse lbm physclock fem ui settings vulkan particles startup glyphs hotkeys validate openurl voxel stl physref thermal)
JOBS=${JOBS:-1}
fail=0

cd "$(dirname "$0")"

for t in "${SUITES[@]}"; do
    echo "===== $t"
    if ! make "$t" -j"$JOBS" 2>&1 | tee "/tmp/aeros-suite-$t.log"; then
        echo "::error::набор '$t' завершился с ошибкой"
        tail -12 "/tmp/aeros-suite-$t.log" | while IFS= read -r line; do
            echo "::error file=aeros/tests/Makefile::[$t] $line"
        done
        fail=1
    fi
done

exit $fail
