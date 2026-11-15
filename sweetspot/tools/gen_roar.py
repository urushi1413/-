#!/usr/bin/env python3
# SWEET SPOT — ジョコビッチの雄叫び（喜ぶ声）を合成する
#   録音は使わず、実際の雄叫びを測った数値だけをまねて一から作る：
#     ・叫ぶ高い声：声の高さ 360〜385Hz で始まり、だんだん 400Hz ほどへ上がる
#     ・母音は「ア」：声の響きの山が 760 / 1520 / 2400 / 3200Hz あたり
#     ・初めの 1 秒ほどははっきりした声、そこから少しかすれて、最後に下がって消える
#   人の声らしく：のどの開き閉じの形の波、声の高さと大きさの小さな揺らぎ、のどが開くたびに混ざる息、
#   「ヤ」から「ア」へ移る母音（「アイデ！」のように）。うなるような低い音やひずみは入れない
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


def glottal(ph, oq=0.40, cq=0.16):
    """のどの開き閉じ（Rosenberg の形）。ph は周期の位相（0〜1 を繰り返す）。微分して口から出る音に"""
    p = ph % 1.0
    g = np.where(p < oq, 0.5 * (1 - np.cos(np.pi * p / oq)),
                 np.where(p < oq + cq, np.cos(0.5 * np.pi * (p - oq) / cq), 0.0))
    return np.diff(g, prepend=g[0]) * SR / 1000.0, g


def tv_resonator(x, f, bw):
    """時間とともに変わる共鳴（母音が「ヤ」→「ア」と移る）。f, bw は x と同じ長さ"""
    y = np.zeros_like(x)
    y1 = y2 = 0.0
    r = np.exp(-np.pi * bw / SR)
    a1 = -2 * r * np.cos(2 * np.pi * f / SR)
    a2 = r * r
    g = 1 - r
    for i in range(len(x)):
        v = g[i] * x[i] - a1[i] * y1 - a2[i] * y2
        y2, y1 = y1, v
        y[i] = v
    return y


def roar(seed, dur=1.7, f_start=365.0, f_peak=400.0, clean=0.6):
    """叫ぶ「（ヤ）アーッ！」。clean：はっきりした声の長さの割合（そのあと少しかすれる）"""
    rng = np.random.default_rng(seed)
    n = int(dur * SR)
    t = np.arange(n) / SR
    u = t / dur
    rough = np.clip((u - clean) / 0.3, 0, 1)
    # 声の高さ：測った値（365Hz から 400Hz へ上がる）。終わりで少し下がる。人の声の小さな揺らぎ（1〜2%）
    f0 = f_start + (f_peak - f_start) * (1 - np.exp(-u / 0.25)) - 70.0 * np.clip((u - 0.85) / 0.15, 0, 1) ** 2
    drift = lfilter([0.002], [1, -0.998], rng.normal(0, 1, n))
    f0 *= 1 + 0.008 * np.sin(2 * np.pi * 5.0 * t + 1.0) + drift * (1 + 2.0 * rough)
    ph = np.cumsum(f0) / SR
    # 周期ごとの細かい揺らぎ（ジッター・シマー）
    per = np.floor(ph).astype(int)
    jit = rng.normal(0, 1, per.max() + 2)
    ph = ph + (0.004 + 0.012 * rough) * jit[per]
    dg, g = glottal(ph, oq=0.38, cq=0.14)            # 叫ぶ声はのどの閉じが速い（高い倍音が強い）
    shim = 1 + (0.03 + 0.06 * rough) * rng.normal(0, 1, per.max() + 2)[per]
    src = dg * shim
    # 息：のどが開くたびに混ざる（かすれた声の出方）
    breath = hp(rng.normal(0, 1, n), 800) * (0.25 + 0.9 * g) * (0.04 + 0.12 * rough)
    src = src / np.max(np.abs(src)) + breath
    # 母音：最初の 80ms で「ヤ」→「ア」（測った「ア」の響き 760 / 1520 / 2400 / 3200Hz）
    k = np.clip(t / 0.09, 0, 1)
    k = k * k * (3 - 2 * k)
    F = [(300 + 470 * k, 80 + 30 * k), (2100 - 580 * k, 120 + 0 * k), (2700 - 300 * k, 170 + 0 * k), (3300 - 100 * k, 220 + 0 * k)]
    G = [1.0, 0.7, 0.35, 0.2]
    y = sum(gn * tv_resonator(src, f, bw) for (f, bw), gn in zip(F, G))
    # 大きさ：すっと立ち上がり、少し強まって、最後に息が抜けて消える
    env = np.clip(t / 0.05, 0, 1) * (0.85 + 0.15 * np.sin(np.pi * np.clip(u / 0.6, 0, 1))) * np.clip((dur - t) / 0.3, 0, 1) ** 1.5
    y = y / np.max(np.abs(y)) * env
    return np.tanh(1.3 * y) / np.tanh(1.3)         # 叫んで、ほんの少しだけひずむ


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
             "djokovik_celebrate_2": finish(roar(8, 1.15, 375.0, 395.0, 0.7), 8)}
    for name, y in files.items():
        sf.write(os.path.join(OUT, name + ".wav"), y.astype(np.float32), SR, subtype="PCM_16")
        print(f"{name:22s} {len(y) / SR:.2f}s")
