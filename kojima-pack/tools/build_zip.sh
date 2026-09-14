#!/usr/bin/env bash
# Собирает KOJIMA-PACK-v0.1.zip из папки resourcepack/ (кладёт в корень kojima-pack/).
# Использование:  bash tools/build_zip.sh
set -e
cd "$(dirname "$0")/.."
rm -f KOJIMA-PACK-v0.1.zip
(cd resourcepack && zip -qr ../KOJIMA-PACK-v0.1.zip .)
echo "built KOJIMA-PACK-v0.1.zip"
unzip -l KOJIMA-PACK-v0.1.zip
