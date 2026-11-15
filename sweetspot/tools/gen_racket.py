#!/usr/bin/env python3
# SWEET SPOT — ラケットを地面に叩きつける音・投げたラケットが落ちる音を合成する
#   録音素材は使わず、実際の音を測った数値だけをまねて一から作る（もらった音声 3 回分の平均）：
#     ・音程のないノイズのような「パキッ」。500Hz〜8kHz にほぼ平らに広がり、5〜6kHz がいちばん強い。低い音は弱い
#     ・1ms で立ち上がり、2ms で -14dB、10ms で -30dB。フレームの響き（音程）はほとんどない
#     ・約 18ms・35ms・55ms に、14〜20dB 小さい「パキッ」が続く（跳ね返り・割れ）
#     ・会場の響きは小さい（ピークより約 30dB 小さい）
#   使い方: python3 tools/gen_racket.py   → sounds/racket_smash*.wav
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


# 実際の叩きつけ音（ブーイングが少ない 3 回分）を 1/3 オクターブで測った「パキッ」の最初の 8ms の音色（dB）。
# 1kHz 以下は後ろのブーイングが混ざっていたので、その分を下げてある
CRACK_FREQ = [125, 250, 500, 800, 1000, 1600, 2000, 2500, 3150, 4000, 5000, 6300, 8000, 10000, 12500, 16000]
CRACK_DB = [-26, -22, -16, -11, -9, -5, -5, -5, -5, -4, -1, 0, -2, -9, -16, -60]


def shaped_noise(rng, n, freqs=CRACK_FREQ, dbs=CRACK_DB, tilt=0.0):
    """測った音色どおりのノイズ（音程のない音）。tilt：高い音を増やす（+）・減らす（-）dB/オクターブ"""
    X = np.fft.rfft(rng.normal(0, 1, n))
    f = np.fft.rfftfreq(n, 1 / SR)
    lf = np.log2(np.maximum(f, 20))
    g = np.interp(lf, np.log2(freqs), dbs, left=dbs[0], right=dbs[-1]) + tilt * (lf - np.log2(2000))
    return np.fft.irfft(X * 10 ** (g / 20), n)


def crack(rng, amp, tilt=0.0):
    """「パキッ」1 回：1ms で立ち上がり、2ms で -14dB、10ms で -30dB。最初の 3ms に細かい割れが重なる"""
    n = int(0.03 * SR)
    t = np.arange(n) / SR
    env = np.minimum(1, t / 0.0006) * (np.exp(-t / 0.00085) + 0.015 * np.exp(-t / 0.006))
    for k in range(rng.integers(2, 5)):                       # 細かい割れ
        t0 = rng.uniform(0.0004, 0.003)
        env += rng.uniform(0.2, 0.5) * np.exp(-np.maximum(t - t0, 0) / 0.0004) * (t >= t0)
    y = hp(shaped_noise(rng, n, tilt=tilt), 300) * env
    return y / np.max(np.abs(y)) * amp


def arena_ir(rng, length=1.0):
    """会場の響き：跳ね返りは小さく、響きはピークより 30dB ほど小さく残る"""
    n = int(length * SR)
    ir = np.zeros(n)
    ir[0] = 1.0
    tail = rng.normal(0, 1, n) * np.exp(-np.arange(n) / (0.12 * SR))
    tail[: int(0.012 * SR)] = 0
    ir += lp(tail, 6000) * 0.006
    return ir


def smash(seed, bright=1.0, dull=0.0):
    """叩きつけ 1 回分：大きな「パキッ」のあと、約 18ms・35ms・55ms に小さな「パキッ」（-14〜-20dB）"""
    rng = np.random.default_rng(seed)
    n = int(0.12 * SR)
    y = np.zeros(n)
    tilt = 1.5 * (bright - 1.0) - 1.5 * dull
    c = crack(rng, 1.0, tilt)
    y[:len(c)] += c
    for t0, db in ((rng.uniform(0.016, 0.020), rng.uniform(-18, -14)),
                   (rng.uniform(0.032, 0.038), rng.uniform(-22, -18)),
                   (rng.uniform(0.050, 0.058), rng.uniform(-19, -15))):
        c = crack(rng, 10 ** (db / 20), tilt - 2.0)
        s = int(t0 * SR)
        y[s:s + len(c)] += c[: n - s]
    return fftconvolve(y, arena_ir(rng))


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
    y = hp(y, 150)
    f = int(0.03 * SR)
    y[-f:] *= np.linspace(1, 0, f)
    y = y / np.max(np.abs(y))
    if drive > 1.0:   # 頭をつぶして全体を大きく聞かせる
        y = np.tanh(drive * y) / np.tanh(drive)
    return y * 0.95


if __name__ == "__main__":
    os.makedirs(OUT, exist_ok=True)
    # 5 種類（毎回少しずつ違う割れ方）。叩くたびに順に鳴らす
    files = {
        "racket_smash": (smash(1, 1.0), 1.0), "racket_smash_2": (smash(2, 1.0, 0.05), 1.0),
        "racket_smash_3": (smash(3, 1.0, 0.1), 1.0), "racket_smash_4": (smash(4, 0.95, 0.12), 1.0),
        "racket_smash_5": (smash(7, 0.9, 0.15), 1.0),
    }
    # 投げたラケットが落ちる音（land）は聞き比べで外した
    for name, (y, drive) in files.items():
        p = os.path.join(OUT, name + ".wav")
        sf.write(p, finish(y, drive).astype(np.float32), SR, subtype="PCM_16")
        print(f"{name:16s} {len(y) / SR:.2f}s")
