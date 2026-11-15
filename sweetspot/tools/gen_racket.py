#!/usr/bin/env python3
# SWEET SPOT — ラケットを地面に叩きつける音・投げたラケットが落ちる音を合成する
#   録音素材は使わず、実際の音の特徴（数値）だけをまねて一から作る：
#     ・2〜6ms で消える鋭い「パキッ」（カーボンのフレームがコートに当たる、広い帯域の音）
#     ・800〜900Hz あたりのフレームの響き（約 20ms で消える）と、少し高い倍音
#     ・約 24ms おきに 2〜3 回、跳ね返りの小さな当たりが続く
#     ・コートに当たる低い「ドン」（150〜400Hz が強い）、約 30ms 後にほぼ同じ強さの 2 回目の当たり
#     ・会場の響き（ピークより 15〜20dB 小さい音が長く残る）
#   使い方: python3 tools/gen_racket.py   → sounds/racket_smash*.wav, sounds/racket_land*.wav
#   必要: numpy, scipy, soundfile
import os

import numpy as np
import soundfile as sf
from scipy.signal import butter, sosfilt, fftconvolve

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
    click = rng.normal(0, 1, m) * decay(m, 0.0011 + 0.0006 * rng.random())
    click = lp(hp(click, 900) + bright * hp(click, 3500) * 2.2, 10000)
    y[:m] += click * 4.0
    # カーボンが割れる「バキッ」：最初の 6ms に細かい割れの音がいくつも重なる
    for k in range(int(3 + 4 * bright)):
        s = int(rng.uniform(0.0005, 0.006) * SR)
        mm = int(0.004 * SR)
        y[s:s + mm] += hp(rng.normal(0, 1, mm), 2500) * decay(mm, 0.0005) * rng.uniform(1.0, 2.5) * bright
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


def arena_ir(rng, length=1.6):
    """会場の響き：少し遅れて届く跳ね返り（客席・壁）と、長く残る響き"""
    n = int(length * SR)
    ir = np.zeros(n)
    ir[0] = 1.0
    for d in rng.uniform(0.012, 0.09, 10):                      # 近くの壁・客席からの跳ね返り
        ir[int(d * SR)] += rng.uniform(0.04, 0.12) * rng.choice([-1, 1])
    tail = rng.normal(0, 1, n) * np.exp(-np.arange(n) / (0.32 * SR))   # 長い響き（約 1 秒で消える）
    tail[: int(0.02 * SR)] = 0
    ir += lp(tail, 5000) * 0.022
    return ir


def smash(seed, bright=1.0, dull=0.0):
    """叩きつけ 1 回分。実際の音に合わせて：
         ・低い「ドン」（150〜400Hz）がいちばん強い帯域と同じくらい大きい
         ・約 30ms 後にラケットのもう一方（ヘッドかグリップ）がほぼ同じ強さで当たる（「バ・キン」）
         ・会場に響いて、ピークより 15〜20dB 小さい音が長く残る"""
    rng = np.random.default_rng(seed)
    n = int(0.5 * SR)
    t = np.arange(n) / SR
    ring = 0.035 * (1 - 0.3 * dull)

    def body(amp, br):
        y = contact(rng, amp, br, ring, n)
        # 低い「ドン」：コートとラケットの胴の響き（ノイズを 150〜400Hz に絞ったもの＋低い共鳴）
        m = int(0.12 * SR)
        thump = band(rng.normal(0, 1, m), 150, 420) * decay(m, 0.03) * 2.4
        thump += 1.0 * np.sin(2 * np.pi * rng.uniform(170, 260) * t[:m]) * decay(m, 0.04)
        y[:m] += thump * amp
        return y

    y = body(1.0, bright * (1 - dull))
    s2 = int(rng.uniform(0.024, 0.036) * SR)                     # 2 回目の当たり
    y[s2:] += body(rng.uniform(0.7, 0.95), 0.7 * bright)[: n - s2]
    tt = s2 / SR
    for k in range(rng.integers(2, 4)):                          # 跳ねて転がるカタカタ
        tt += rng.uniform(0.05, 0.09)
        s = int(tt * SR)
        if s >= n:
            break
        y[s:] += contact(rng, 0.25 * (0.7 ** k), 0.3, 0.012, n - s)
    if dull > 0:
        y = lp(y, 7000 - 3000 * dull)
    wet = fftconvolve(y, arena_ir(rng))
    return wet


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


def finish(y, drive=1.0):
    y = hp(y, 90)
    f = int(0.03 * SR)
    y[-f:] *= np.linspace(1, 0, f)
    y = y / np.max(np.abs(y))
    if drive > 1.0:   # 頭をつぶして全体を大きく聞かせる
        y = np.tanh(drive * y) / np.tanh(drive)
    return y * 0.95


if __name__ == "__main__":
    os.makedirs(OUT, exist_ok=True)
    # 聞き比べで選んだ「最初の一撃」（鋭く高いパキッ）の作り方で 5 種類。叩くたびに順に鳴らす
    files = {
        "racket_smash": (smash(1, 1.0), 1.4), "racket_smash_2": (smash(2, 1.0, 0.05), 1.4),
        "racket_smash_3": (smash(3, 1.0, 0.1), 1.4), "racket_smash_4": (smash(4, 0.95, 0.12), 1.4),
        "racket_smash_5": (smash(7, 0.9, 0.15), 1.4),
    }
    # 投げたラケットが落ちる音（land）は聞き比べで外した
    for name, (y, drive) in files.items():
        p = os.path.join(OUT, name + ".wav")
        sf.write(p, finish(y, drive).astype(np.float32), SR, subtype="PCM_16")
        print(f"{name:16s} {len(y) / SR:.2f}s")
