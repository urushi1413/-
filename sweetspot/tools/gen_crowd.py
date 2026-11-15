#!/usr/bin/env python3
# SWEET SPOT — 観客のブーイングと「おお〜」を合成する（録音素材が見つからなかったため）
#   大勢の声（声の高さ・長さ・始まりを少しずつ変えた「ウー」「オー」）を重ね、会場の響きを足す
#   使い方: python3 tools/gen_crowd.py   → sounds/boo*.ogg, sounds/ooh*.ogg
#   必要: numpy, scipy, soundfile
import os
import numpy as np
from scipy.signal import lfilter, fftconvolve
import soundfile as sf

SR = 44100
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "sounds")


def resonator(x, f, bw):
    """2 次の共振フィルタ（声の「母音」を作る）"""
    r = np.exp(-np.pi * bw / SR)
    th = 2 * np.pi * f / SR
    a = [1.0, -2 * r * np.cos(th), r * r]
    b = [1.0 - r]
    return lfilter(b, a, x)


def voice(rng, dur, f0, f0_curve, formants, breath=0.15):
    """1 人分の声。f0_curve(t) は 0〜1 の時間に対する声の高さの倍率"""
    n = int(dur * SR)
    t = np.arange(n) / SR
    u = t / dur
    vib = 1 + 0.018 * np.sin(2 * np.pi * rng.uniform(4, 6.5) * t + rng.uniform(0, 6.3))
    drift = 1 + 0.01 * np.cumsum(rng.normal(0, 1, n)) / np.sqrt(SR)   # ゆっくりした揺れ
    f = f0 * f0_curve(u) * vib * drift
    ph = np.cumsum(f) / SR
    saw = 2 * (ph % 1.0) - 1                    # のどの振動（のこぎり波）
    src = lfilter([1.0], [1.0, -0.92], saw) * 0.1  # 高い音を弱めて柔らかく
    src += rng.normal(0, breath, n) * 0.25          # 息の音
    y = np.zeros(n)
    for (ff, bw, g) in formants:
        y += g * resonator(src, ff, bw)
    att = rng.uniform(0.12, 0.35)
    rel = rng.uniform(0.3, 0.6)
    env = np.minimum(1, t / att) * np.clip((dur - t) / rel, 0, 1)
    env *= 0.7 + 0.3 * np.sin(np.pi * np.clip(u, 0, 1)) ** 0.5
    return y * env


def reverb(x, rt=0.9, wet=0.35, seed=7):
    """会場の響き（指数関数的に減るノイズを畳み込む）"""
    rng = np.random.default_rng(seed)
    n = int(rt * SR)
    ir = rng.normal(0, 1, n) * np.exp(-6.9 * np.arange(n) / n)
    ir = lfilter([0.3], [1, -0.7], ir)   # 響きは低めに
    ir /= np.sqrt(np.sum(ir ** 2))
    w = fftconvolve(x, ir)[: len(x)]
    return (1 - wet) * x + wet * w


def crowd(seed, total, n_voices, make):
    rng = np.random.default_rng(seed)
    out = np.zeros(int(total * SR))
    for i in range(n_voices):
        y, start = make(rng, i)
        s = int(start * SR)
        e = min(len(out), s + len(y))
        out[s:e] += y[: e - s] * rng.uniform(0.5, 1.0)
    out = reverb(out)
    # 始まりと終わりをなめらかに
    fade = int(0.03 * SR)
    out[:fade] *= np.linspace(0, 1, fade)
    out[-int(0.4 * SR):] *= np.linspace(1, 0, int(0.4 * SR))
    return out / np.max(np.abs(out)) * 0.85


# 母音（声道の共振）：男性の値。女性は 1.17 倍
U_ = [(330, 70, 1.0), (870, 90, 0.7), (2300, 140, 0.25)]   # ウー（ブーイング）
O_ = [(480, 80, 1.0), (880, 90, 0.6), (2500, 150, 0.12)]   # オー
A_ = [(700, 90, 1.0), (1150, 100, 0.55), (2600, 160, 0.12)]  # アー


def shift(fm, k):
    return [(f * k, bw * k, g) for (f, bw, g) in fm]


def boo(seed, total=3.2, n=70):
    def make(rng, i):
        female = rng.random() < 0.3
        f0 = rng.uniform(180, 250) if female else rng.uniform(95, 150)
        fk = 1.17 if female else 1.0
        dur = rng.uniform(1.2, 2.6)
        start = rng.uniform(0, 0.55) + (0 if rng.random() < 0.8 else rng.uniform(0.5, 1.0))
        dur = min(dur, total - start - 0.05)
        curve = lambda u: 1.0 + 0.04 * (1 - u) - 0.06 * u ** 2   # 少し下がる
        return voice(rng, dur, f0, curve, shift(U_, fk * rng.uniform(0.95, 1.05)), breath=0.2), start
    return crowd(seed, total, n, make)


def ooh(seed, total=2.0, n=60, rise=False):
    """惜しい！の「おお〜」：一斉に高めに出て下がる（rise は驚きの「おおっ」で一度上がる）"""
    def make(rng, i):
        female = rng.random() < 0.35
        f0 = rng.uniform(190, 280) if female else rng.uniform(110, 170)
        fk = 1.17 if female else 1.0
        dur = rng.uniform(0.9, 1.7)
        start = abs(rng.normal(0, 0.08))
        dur = min(dur, total - start - 0.05)
        if rise:
            curve = lambda u: 1.0 + 0.35 * np.sin(np.pi * np.clip(u * 1.6, 0, 1)) * (1 - u) - 0.15 * u
        else:
            curve = lambda u: 1.25 - 0.4 * u
        fm = shift(O_ if (i % 3) else A_, fk * rng.uniform(0.95, 1.05))
        return voice(rng, dur, f0, curve, fm, breath=0.25), start
    return crowd(seed, total, n, make)


if __name__ == "__main__":
    os.makedirs(OUT, exist_ok=True)
    files = {
        "boo": boo(11), "boo_2": boo(23, 3.6, 80), "boo_3": boo(37, 2.6, 55),
        "ooh": ooh(5), "ooh_2": ooh(19, rise=True), "ooh_3": ooh(41, 1.8, 50),
    }
    for name, y in files.items():
        p = os.path.join(OUT, name + ".ogg")
        sf.write(p, y.astype(np.float32), SR, format="OGG", subtype="VORBIS")
        print(p, f"{len(y) / SR:.2f}s")
