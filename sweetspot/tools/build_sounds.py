#!/usr/bin/env python3
# SWEET SPOT — ダウンロードした効果音素材を、ゲームで使う名前・長さ・音量にして sounds フォルダに書き出す
#   使い方: python3 tools/build_sounds.py <素材フォルダ>
#   素材フォルダの中身（どれも CC0 か CC-BY。出典は sounds/CREDITS.txt）:
#     impact-sounds/   Kenney「Impact Sounds」(CC0)       https://kenney.nl/assets/impact-sounds
#     gq/              Gregor Quendel「Free Crowd Cheering Sounds」(CC-BY 4.0) https://opengameart.org/content/free-crowd-cheering-sounds
#     oga_applause-clapping-church-crowd-immersive.wav  (CC0) https://opengameart.org/content/applause-in-a-large-hall-or-church
#     oga_bamboo.mp3   (CC0) https://opengameart.org/content/swish-bamboo-stick-weapon-swhoshes
import glob
import os
import subprocess
import sys

import numpy as np
import soundfile as sf
from scipy.signal import butter, sosfilt

SR = 44100
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "sounds")
SRC = sys.argv[1] if len(sys.argv) > 1 else "."


def load(path):
    """どの形式でも 44.1kHz・モノラルの float で読む"""
    raw = subprocess.run(["ffmpeg", "-v", "error", "-i", path, "-ac", "1", "-ar", str(SR), "-f", "f32le", "-"],
                         check=True, capture_output=True).stdout
    return np.frombuffer(raw, dtype=np.float32).astype(np.float64)


def lp(x, f):
    return sosfilt(butter(4, f, "low", fs=SR, output="sos"), x)


def hp(x, f):
    return sosfilt(butter(2, f, "high", fs=SR, output="sos"), x)


def trim(x, thr=0.02):
    """頭の無音を切る（打球音が遅れて聞こえないように）。終わりの無音も切る"""
    a = np.abs(x)
    m = a.max() if a.size else 0
    if m == 0:
        return x
    idx = np.nonzero(a > thr * m)[0]
    s = max(0, idx[0] - int(0.002 * SR))
    e = min(len(x), idx[-1] + int(0.02 * SR))
    return x[s:e]


def fade(x, fin=0.003, fout=0.03):
    x = x.copy()
    n1, n2 = int(fin * SR), int(fout * SR)
    if n1:
        x[:n1] *= np.linspace(0, 1, n1)
    if n2 and n2 < len(x):
        x[-n2:] *= np.linspace(1, 0, n2)
    return x


def norm(x, peak=0.9):
    m = np.max(np.abs(x))
    return x / m * peak if m > 0 else x


def resample(x, k):
    """音の高さを k 倍に（長さは 1/k）"""
    n = int(len(x) / k)
    return np.interp(np.arange(n) * k, np.arange(len(x)), x)


def write(name, x, peak=0.9, ogg=False):
    x = norm(x, peak).astype(np.float32)
    p = os.path.join(OUT, name + (".ogg" if ogg else ".wav"))
    if ogg:
        sf.write(p, x, SR, format="OGG", subtype="VORBIS")
    else:
        sf.write(p, x, SR, subtype="PCM_16")
    print(f"{name:22s} {len(x) / SR:5.2f}s")


def seg(x, a, b):
    return x[int(a * SR):int(b * SR)]


def loop(x, xf=2.0):
    """終わりを始まりに重ねて、繰り返してもつなぎ目が分からないように"""
    n = int(xf * SR)
    body = x[:-n].copy()
    w = np.linspace(0, 1, n)
    body[:n] = body[:n] * w + x[-n:] * (1 - w)
    return body


def kenney(name):
    return sorted(glob.glob(os.path.join(SRC, "impact-sounds", "Audio", name + "_*.ogg")))


def variants(base, xs, **kw):
    for i, x in enumerate(xs):
        write(base if i == 0 else f"{base}_{i + 1}", x, **kw)


def mix(*parts):
    """(音, 始まりの秒, 音量) を重ねる"""
    n = max(int(t * SR) + len(x) for x, t, g in parts)
    y = np.zeros(n)
    for x, t, g in parts:
        s = int(t * SR)
        y[s:s + len(x)] += x * g
    return y


def main():
    """聞き比べページで選んだ音だけを書き出す（2026-11 の選択。外した候補は書き出さない）"""
    os.makedirs(OUT, exist_ok=True)

    # ---- 打球音：芯で打った音（hit_good）から、まあまあの当たりを作る ----
    #   外した当たり・スライス・フレームは候補を外したので、hit_ok → hit_good で代わりに鳴る
    good = [trim(load(p)) for p in sorted(glob.glob(os.path.join(OUT, "hit_good*.wav")))]
    write("hit_ok", fade(lp(resample(good[0], 0.96), 4500)))

    # ---- バウンド：クレーは鈍く、芝は柔らかく（ハードは候補を外した） ----
    write("bounce_clay", fade(lp(trim(load(kenney("impactSoft_medium")[0])), 2600)), peak=0.7)
    write("bounce_grass", fade(lp(trim(load(kenney("impactSoft_medium")[0])), 1300)), peak=0.55)

    # ---- ネットに当たった「バサッ」 ----
    write("net", fade(lp(trim(load(kenney("impactSoft_heavy")[0])), 3000)), peak=0.85)

    # ---- 空振り：竹の棒の素振りから 3 回目の「ヒュン」を切り出す ----
    bm = load(os.path.join(SRC, "oga_bamboo.mp3"))
    env = np.convolve(np.abs(bm), np.ones(1500) / 1500, "same")
    thr, i, cuts = 0.35 * env.max(), 0, []
    while i < len(env) and len(cuts) < 3:
        if env[i] > thr:
            pk = i + int(np.argmax(env[i:i + int(0.4 * SR)]))
            s0 = max(0, pk - int(0.2 * SR))
            cuts.append(bm[s0:s0 + int(0.45 * SR)])
            i = pk + int(0.6 * SR)
        else:
            i += 200
    write("whiff", fade(hp(cuts[2], 300), 0.002, 0.03))

    # ---- 観客 ----
    gq = os.path.join(SRC, "gq", "Gregor Quendel - Free Crowd Cheering Sounds - MP3", "Gregor Quendel - Crowd Cheering Sounds - ")
    write("crowd", loop(seg(load(gq + "10 - Ambience.mp3"), 1, 33)), peak=0.6, ogg=True)
    write("crowd_2", loop(seg(load(gq + "07 - Soft cheering and chatter.mp3"), 1, 37)), peak=0.6, ogg=True)
    # 勝利の歓声：録音は盛り上がるまで 4〜6 秒かかるので、盛り上がる直前から切り出す（1 秒以内に最高潮）
    write("match_win", fade(seg(load(gq + "03 - Strong cheering - I.mp3"), 5.6, 19), 0.12, 4.0), ogg=True)
    write("match_win_2", fade(seg(load(gq + "02 - Strong cheering and soft rhythmic cheering.mp3"), 4.2, 16), 0.12, 4.0), ogg=True)
    church = load(os.path.join(SRC, "oga_applause-clapping-church-crowd-immersive.wav"))
    write("applause_2", fade(seg(church, 6, 10), 0.1, 1.5), peak=0.8, ogg=True)
    write("applause_3", fade(seg(church, 20, 23), 0.1, 1.3), peak=0.8, ogg=True)
    # 主審のコールは候補（女性の声）を外した。録り直した声を call_*.wav として置く


if __name__ == "__main__":
    main()
