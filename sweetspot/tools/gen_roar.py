#!/usr/bin/env python3
# SWEET SPOT — ジョコビッチの雄叫び（喜ぶ声）を合成する
#   録音は使わず、実際の雄叫びを測った数値だけをまねて一から作る：
#     ・叫ぶ高い声：声の高さ 360〜385Hz で始まり、だんだん 400Hz ほどへ上がる
#     ・母音は「ア」：声の響きの山が 760 / 1520 / 2400 / 3200Hz あたり
#     ・初めの 1 秒ほどははっきりした声、そこからかすれて（声がつぶれて）、最後に下がって消える
#     ・音の 7 割が 500〜1200Hz（叫んだ声で倍音が強い）
#   使い方: python3 tools/gen_roar.py   → sounds/djokovik_celebrate.wav, _2.wav
import os
import sys

import numpy as np
import soundfile as sf
from scipy.signal import lfilter

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from build_sounds import SR, OUT, lp, hp  # noqa: E402
from cut_voices import far_away  # noqa: E402


def resonator(x, f, bw):
    r = np.exp(-np.pi * bw / SR)
    th = 2 * np.pi * f / SR
    return lfilter([1.0 - r], [1.0, -2 * r * np.cos(th), r * r], x)


def roar(seed, dur=1.7, f_start=365.0, f_peak=400.0, clean=0.55):
    """叫ぶ「アーッ！」。clean：はっきりした声の長さの割合（そのあとはかすれる）"""
    rng = np.random.default_rng(seed)
    n = int(dur * SR)
    t = np.arange(n) / SR
    u = t / dur
    # 声の高さ：始まりから少し上がり、最後に下がる。揺れ（ビブラート・細かい揺らぎ）
    f0 = f_start + (f_peak - f_start) * np.clip(u / 0.6, 0, 1) - 90.0 * np.clip((u - 0.82) / 0.18, 0, 1) ** 2
    f0 *= 1 + 0.012 * np.sin(2 * np.pi * 5.5 * t) + 0.004 * np.sin(2 * np.pi * 1.3 * t + rng.uniform(0, 6.3))
    # かすれ：はっきりした部分のあとで増える（声の揺らぎ・半分の高さの音・息）
    rough = np.clip((u - clean) / 0.25, 0, 1)
    jit = lfilter([0.02], [1, -0.98], rng.normal(0, 1, n))          # 声の揺らぎ（かすれると大きく）
    ph = np.cumsum(f0 * (1 + (0.01 + 0.6 * rough) * jit)) / SR
    pulse = 2 * (ph % 1.0) - 1
    sub = np.sign(np.sin(np.pi * ph))                      # 半分の高さ（声がつぶれた感じ）
    src = lfilter([1.0], [1.0, -0.45], pulse + 0.5 * rough * sub)   # 叫んだ声は高い倍音が強い
    src *= 1 + 0.6 * rough * lfilter([0.05], [1, -0.95], rng.normal(0, 1, n))   # 声の大きさも揺れる（つぶれた声）
    src += rng.normal(0, 1, n) * (0.12 + 2.6 * rough)      # 息とかすれ
    # 母音「ア」の響き（測った値）
    #   叫ぶ声は響きの山の幅が広い（1 つ目の山は 760〜1100Hz に広がる）
    y = (1.0 * resonator(src, 780, 220) + 0.75 * resonator(src, 1050, 240) + 0.55 * resonator(src, 1520, 200) +
         0.3 * resonator(src, 2400, 240) + 0.2 * resonator(src, 3200, 260))
    # 大きさ：すっと立ち上がり、途中で一度強く、最後に消える
    env = np.clip(t / 0.06, 0, 1) * (0.8 + 0.2 * np.sin(np.pi * np.clip(u / 0.5, 0, 1))) * np.clip((dur - t) / 0.35, 0, 1)
    y = np.tanh(2.0 * y / np.max(np.abs(y))) * env   # 叫んで少しひずむ
    return y


# 実際の雄叫びを測った、帯域ごとの音の割合（%）
BANDS = [150, 300, 500, 800, 1200, 2000, 3000, 4500, 8000]
TARGET = [0.3, 2, 39, 33, 20, 4, 2, 0.3]


def match_bands(y):
    """帯域ごとの強さを、測った割合に合わせる（なめらかにつないで）"""
    Y = np.fft.rfft(y)
    f = np.fft.rfftfreq(len(y), 1 / SR)
    P = np.abs(Y) ** 2
    tot = P.sum()
    cent, gain = [], []
    for i in range(len(TARGET)):
        m = (f >= BANDS[i]) & (f < BANDS[i + 1])
        cur = P[m].sum() / tot * 100 + 1e-6
        cent.append(np.sqrt(BANDS[i] * BANDS[i + 1]))
        gain.append(np.sqrt(TARGET[i] / cur))
    g = np.interp(np.log2(np.maximum(f, 20)), np.log2(cent), np.log2(gain))
    g = 2 ** np.clip(g, -6, 4)
    g[f < BANDS[0]] *= 0.1
    g[f > BANDS[-1]] *= 0.1
    return np.fft.irfft(Y * g, len(y))


def finish(y, seed):
    # 測った音の割合に合わせる：声の一番低い音（380Hz あたり）は弱く、1.2〜2.4kHz は強く
    y = hp(hp(y, 520), 520)
    y = match_bands(match_bands(y))   # 2 回で十分そろう
    y = far_away(y, seed, 1.0)                           # コートの向こうで叫んでいるように
    f = int(0.05 * SR)
    y[-f:] *= np.linspace(1, 0, f)
    return y / np.max(np.abs(y)) * 0.9


if __name__ == "__main__":
    files = {"djokovik_celebrate": finish(roar(3), 3),
             "djokovik_celebrate_2": finish(roar(8, 1.15, 375.0, 395.0, 0.65), 8)}
    for name, y in files.items():
        sf.write(os.path.join(OUT, name + ".wav"), y.astype(np.float32), SR, subtype="PCM_16")
        print(f"{name:22s} {len(y) / SR:.2f}s")
