#!/usr/bin/env python3
# SWEET SPOT — ラケットを地面に叩きつける音・投げたラケットが落ちる音を合成する
#   録音素材は使わず、実際の音の特徴（数値）だけをまねて一から作る：
#     ・2〜6ms で消える鋭い「パキッ」（カーボンのフレームがコートに当たる、広い帯域の音）
#     ・800〜900Hz あたりのフレームの響き（約 20ms で消える）と、少し高い倍音
#     ・約 24ms おきに 2〜3 回、跳ね返りの小さな当たりが続く
#     ・コートに当たる低い「ドッ」
#   使い方: python3 tools/gen_racket.py   → sounds/racket_smash*.wav, sounds/racket_land*.wav
#   必要: numpy, scipy, soundfile
import os

import numpy as np
import soundfile as sf
from scipy.signal import butter, sosfilt

SR = 44100
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "sounds")


def band(x, lo, hi):
    return sosfilt(butter(2, [lo, hi], "band", fs=SR, output="sos"), x)


def hp(x, f):
    return sosfilt(butter(2, f, "high", fs=SR, output="sos"), x)


def lp(x, f):
    return sosfilt(butter(2, f, "low", fs=SR, output="sos"), x)


def decay(n, tau):
    return np.exp(-np.arange(n) / (tau * SR))


def contact(rng, amp, bright, ring_tau, n):
    """1 回の当たり：パキッ＋フレームの響き＋低いドッ"""
    y = np.zeros(n)
    # パキッ（広い帯域のノイズ。明るいほど高い音が多い）
    m = int(0.012 * SR)
    click = rng.normal(0, 1, m) * decay(m, 0.0016 + 0.001 * rng.random())
    click = lp(hp(click, 900) + bright * hp(click, 4000) * 1.6, 9000)
    y[:m] += click * 2.6
    # フレームの響き（800〜900Hz に集まった数本の共鳴＋少し高い倍音）
    t = np.arange(n) / SR
    for k in range(5):
        f = rng.uniform(420, 950)
        y += 0.16 * np.sin(2 * np.pi * f * t + rng.uniform(0, 6.3)) * decay(n, ring_tau * rng.uniform(0.7, 1.2))
    for f in (rng.uniform(1250, 1600), rng.uniform(1700, 1950), rng.uniform(2200, 2700)):
        y += 0.2 * np.sin(2 * np.pi * f * t) * decay(n, ring_tau * 0.5)
    # 低いドッ（コート）
    f0 = rng.uniform(110, 170)
    y += 0.12 * np.sin(2 * np.pi * f0 * t) * decay(n, 0.02)
    return y * amp


def smash(seed, bright=1.0, dull=0.0):
    """叩きつけ 1 回分。bright：最初の一撃の高い「パキッ」、dull：曲がったラケットのにぶさ"""
    rng = np.random.default_rng(seed)
    n = int(0.45 * SR)
    y = np.zeros(n)
    ring = 0.020 * (1 - 0.4 * dull)
    y += contact(rng, 1.0, bright * (1 - dull), ring, n)
    # 跳ね返り：約 24ms おきに 2〜3 回、だんだん小さく
    t = 0.0
    for k in range(rng.integers(2, 4)):
        t += rng.uniform(0.020, 0.030)
        s = int(t * SR)
        y[s:] += contact(rng, rng.uniform(0.25, 0.55) * (0.75 ** k), 0.3 * bright, ring * 0.8, n - s)
    # ラケットが転がって止まるカタカタ（小さく）
    t += rng.uniform(0.06, 0.1)
    for k in range(rng.integers(2, 5)):
        s = int(t * SR)
        if s >= n:
            break
        y[s:] += contact(rng, 0.12 * (0.7 ** k), 0.0, 0.008, n - s)
        t += rng.uniform(0.035, 0.06)
    if dull > 0:
        y = lp(y, 6000 - 3500 * dull)
    return y


def land(seed):
    """投げたラケットが落ちる：当たり＋コートをこする音＋小さく 2 回跳ねる"""
    rng = np.random.default_rng(seed)
    n = int(0.7 * SR)
    y = contact(rng, 0.75, 0.5, 0.015, n)
    m = int(0.35 * SR)
    scrape = band(rng.normal(0, 1, m), 1200, 3500) * decay(m, 0.09) * 0.25
    s = int(0.01 * SR)
    y[s:s + m] += scrape
    t = 0.0
    for k in range(2):
        t += rng.uniform(0.12, 0.17)
        s = int(t * SR)
        y[s:] += contact(rng, 0.3 * (0.6 ** k), 0.2, 0.01, n - s)
    return y


def finish(y):
    y = hp(y, 220)
    f = int(0.03 * SR)
    y[-f:] *= np.linspace(1, 0, f)
    return y / np.max(np.abs(y)) * 0.92


if __name__ == "__main__":
    os.makedirs(OUT, exist_ok=True)
    files = {
        "racket_smash": smash(1, 1.0), "racket_smash_2": smash(2, 0.8),
        "racket_smash_3": smash(3, 0.4, 0.4), "racket_smash_4": smash(4, 0.2, 0.7),
        "racket_land": land(5), "racket_land_2": land(6),
    }
    for name, y in files.items():
        p = os.path.join(OUT, name + ".wav")
        sf.write(p, finish(y).astype(np.float32), SR, subtype="PCM_16")
        print(f"{name:16s} {len(y) / SR:.2f}s")
