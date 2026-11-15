#!/usr/bin/env python3
# SWEET SPOT — 録音した主審の声を、会場のスピーカーを通した音にする
#   使い方: python3 tools/umpire_pa.py <録音フォルダ>
#     録音フォルダに call_love.wav, call_15.wav ... を置く（名前は sounds/README.txt）。
#     sounds/ に同じ名前で書き出す。.wav / .m4a / .mp3 など、ffmpeg で読める形式なら何でもよい
#   すること: 頭と終わりの無音を切る → 電話のように低音と高音を削る（スピーカーの帯域）
#            → 少しだけ歪ませて大きさをそろえる → 会場で跳ね返る短いこだまと響きを足す
import glob
import os
import sys

import numpy as np
import soundfile as sf
from scipy.signal import fftconvolve

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from build_sounds import SR, OUT, load, lp, hp, trim, fade, norm  # noqa: E402


def pa(x):
    x = trim(x, 0.03)
    y = lp(hp(x, 280), 4200)                     # スピーカーの帯域
    y = norm(y, 1.0)
    y = np.tanh(2.2 * y) / np.tanh(2.2)          # 少し歪ませて、小さい音を持ち上げる
    rng = np.random.default_rng(1)
    n = int(1.4 * SR)
    ir = np.zeros(n)
    ir[0] = 1.0
    for d, g in ((0.085, 0.35), (0.16, 0.18), (0.24, 0.08)):   # 観客席の向こう側のスピーカーから遅れて届く声
        ir[int(d * SR)] += g
    tail = rng.normal(0, 1, n) * np.exp(-5.0 * np.arange(n) / n) * 0.05   # 会場の響き
    ir += lp(tail, 3000)
    y = fftconvolve(y, ir)[: len(y) + int(0.9 * SR)]
    return fade(norm(y, 0.85), 0.002, 0.3)


def main():
    if len(sys.argv) < 2:
        print(__doc__ or "usage: umpire_pa.py <録音フォルダ>")
        return
    for p in sorted(glob.glob(os.path.join(sys.argv[1], "call_*.*"))):
        name = os.path.splitext(os.path.basename(p))[0]
        y = pa(load(p))
        sf.write(os.path.join(OUT, name + ".wav"), y.astype(np.float32), SR, subtype="PCM_16")
        print(f"{name:22s} {len(y) / SR:5.2f}s")


if __name__ == "__main__":
    main()
