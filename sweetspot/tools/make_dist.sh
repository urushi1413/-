#!/bin/sh
# SWEET SPOT — 配る形を作る（Mac / Linux）。make dist から呼ばれる。売る版（sweetspot_sale）が先にできている前提
set -e
cd "$(dirname "$0")/.."
OS_NAME=$(uname -s)
OUT=dist/SweetSpot
rm -rf dist
mkdir -p "$OUT/sounds" "$OUT/fonts" "$OUT/models"
cp sweetspot_sale "$OUT/SweetSpot"
# 音：選手の声のファイル名は番号に（売る版は v01〜v16 の名前で読む）
i=1
for n in federerr djokovik nadar murraye alcarazz sinnerr medvedeb kirgios williamz swiontek sabalenco gauph sharapowa bartie nishikoli zverew; do
  v=$(printf "v%02d" $i)
  for f in sounds/${n}_*; do [ -e "$f" ] && cp "$f" "$OUT/sounds/${v}_${f#sounds/${n}_}"; done
  i=$((i+1))
done
for f in sounds/*.wav sounds/*.ogg; do
  b=$(basename "$f"); case "$b" in federerr_*|djokovik_*|nadar_*|murraye_*|alcarazz_*|sinnerr_*|medvedeb_*|kirgios_*|williamz_*|swiontek_*|sabalenco_*|gauph_*|sharapowa_*|bartie_*|nishikoli_*|zverew_*) continue;; esac
  cp "$f" "$OUT/sounds/"
done
cp fonts/* "$OUT/fonts/"
cp models/player.glb models/palette.png models/player_skin.glb models/players.txt "$OUT/models/"
mkdir -p "$OUT/models/faces" && cp models/faces/*.png "$OUT/models/faces/"
cp dist_files/CREDITS.txt dist_files/README_JA.txt dist_files/README_EN.txt "$OUT/"
(cd dist && if command -v zip >/dev/null; then zip -qr "SweetSpot_${OS_NAME}.zip" SweetSpot; echo "dist/SweetSpot_${OS_NAME}.zip"; else tar czf "SweetSpot_${OS_NAME}.tar.gz" SweetSpot; echo "dist/SweetSpot_${OS_NAME}.tar.gz"; fi)
