#!/bin/sh
# SWEET SPOT — ブラウザ版（make web / make mobile）に入れる音を build/web_sounds に用意する。売る版と同じく、選手の声のファイル名は番号に（v01〜v16）
#   前は sounds をそのまま入れていて、売る版の名前（v01_…）で探す声が見つからず鳴らなかった。音の説明の txt も入れない（配る形と同じ）
set -e
cd "$(dirname "$0")/.."
OUT=build/web_sounds
rm -rf "$OUT"; mkdir -p "$OUT"
i=1
for n in federerr djokovik nadar murraye alcarazz sinnerr medvedeb kirgios williamz swiontek sabalenco gauph sharapowa bartie nishikoli zverew; do
  v=$(printf "v%02d" $i)
  for f in sounds/${n}_*; do [ -e "$f" ] && cp "$f" "$OUT/${v}_${f#sounds/${n}_}"; done
  i=$((i+1))
done
for f in sounds/*.wav sounds/*.ogg; do
  b=$(basename "$f"); case "$b" in federerr_*|djokovik_*|nadar_*|murraye_*|alcarazz_*|sinnerr_*|medvedeb_*|kirgios_*|williamz_*|swiontek_*|sabalenco_*|gauph_*|sharapowa_*|bartie_*|nishikoli_*|zverew_*) continue;; esac
  cp "$f" "$OUT/"
done
