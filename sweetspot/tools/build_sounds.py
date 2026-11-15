#!/usr/bin/env python3
# SWEET SPOT — ダウンロードした効果音素材を、ゲームで使う名前・長さ・音量にして sounds フォルダに書き出す
#   使い方: python3 tools/build_sounds.py <素材フォルダ>
#   素材フォルダの中身（どれも CC0 か CC-BY。出典は sounds/CREDITS.txt）:
#     impact-sounds/   Kenney「Impact Sounds」(CC0)       https://kenney.nl/assets/impact-sounds
#     gq/              Gregor Quendel「Free Crowd Cheering Sounds」(CC-BY 4.0) https://opengameart.org/content/free-crowd-cheering-sounds
#     sports/          Aimee Smith「Announcer Voice Pack (Sports, Female)」(CC-BY 4.0) https://opengameart.org/content/announcer-voice-pack-sports-female
#     oga_applause-clapping-church-crowd-immersive.wav  (CC0) https://opengameart.org/content/applause-in-a-large-hall-or-church
#     oga_whoosh2.wav  (CC0) https://opengameart.org/content/air-whoosh
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
    os.makedirs(OUT, exist_ok=True)
    rng = np.random.default_rng(3)

    # ---- 打球音：芯で打った音（hit_good）から、当たりの悪い音・スライスを作る ----
    good = [trim(load(p)) for p in sorted(glob.glob(os.path.join(OUT, "hit_good*.wav")))]
    variants("hit_ok", [fade(lp(resample(g, 0.96), 4500)) for g in good])
    variants("hit_bad", [fade(lp(resample(g, 0.88), 1700)) for g in good], peak=0.75)
    variants("hit_slice", [fade(lp(hp(resample(g, 1.06), 500), 3200)[: int(0.25 * SR)]) for g in good], peak=0.8)
    # フレームに当たった鈍い「コッ」
    variants("hit_frame", [fade(hp(trim(load(p)), 250)) for p in kenney("impactWood_light")[:4]], peak=0.8)

    # ---- バウンド：ハードは軽い「ポン」、クレーは鈍く、芝は柔らかく ----
    variants("bounce_hard", [fade(hp(trim(load(p)), 300)) for p in kenney("impactGeneric_light")[:5]], peak=0.7)
    variants("bounce_clay", [fade(lp(trim(load(p)), 2600)) for p in kenney("impactSoft_medium")[:5]], peak=0.7)
    variants("bounce_grass", [fade(lp(trim(load(p)), 1300)) for p in kenney("impactSoft_medium")[:5]], peak=0.55)

    # ---- ネットに当たった「バサッ」 ----
    variants("net", [fade(lp(trim(load(p)), 3000)) for p in kenney("impactSoft_heavy")[:4]], peak=0.85)

    # ---- 空振り（風を切る音）：繰り返しの「ヒュン」から 1 回分ずつ切り出す ----
    wh = load(os.path.join(SRC, "oga_whoosh2.wav"))
    # 1 回分は約 1 秒おき。ゆっくりなので、速くして（高く短く）ラケットの振りに合わせる
    env = np.convolve(np.abs(wh), np.ones(2000) / 2000, "same")
    cuts = []
    for k in range(4):
        a, b = int((k * 1.05) * SR), int((k * 1.05 + 1.0) * SR)
        pk = a + int(np.argmax(env[a:b]))
        s = max(0, pk - int(0.32 * SR))
        cuts.append(fade(hp(resample(wh[s:s + int(0.56 * SR)], 1.6), 400), 0.02, 0.08))
    variants("whiff", cuts, peak=0.6)

    # ---- ラケットを地面に叩きつける：「バキッ」と、跳ねて転がる「カラン」 ----
    crack = [hp(trim(load(p)), 200) for p in kenney("impactPlank_medium")]
    plate = [trim(load(p)) for p in kenney("impactPlate_heavy")]
    clat = [hp(trim(load(p)), 400) for p in kenney("impactPlate_light")]
    sm = []
    for i in range(4):
        sm.append(fade(mix((crack[i], 0, 1.0), (plate[i], 0.004, 0.55),
                           (clat[i], 0.20 + 0.03 * i, 0.45), (clat[(i + 2) % 5], 0.34 + 0.02 * i, 0.25),
                           (clat[(i + 1) % 5], 0.46, 0.12)), 0.001, 0.08))
    variants("racket_smash", sm, peak=0.95)
    # 投げたラケットが落ちて転がる
    variants("racket_land", [fade(mix((clat[i], 0, 0.9), (clat[(i + 3) % 5], 0.15, 0.4), (clat[(i + 1) % 5], 0.27, 0.18)), 0.001, 0.06)
                             for i in range(3)], peak=0.8)

    # ---- 観客 ----
    gq = os.path.join(SRC, "gq", "Gregor Quendel - Free Crowd Cheering Sounds - MP3", "Gregor Quendel - Crowd Cheering Sounds - ")
    amb = load(gq + "10 - Ambience.mp3")
    write("crowd", loop(seg(amb, 1, 33)), peak=0.6, ogg=True)
    strong2 = load(gq + "04 - Strong cheering - II - Short.mp3")
    write("applause_big_2", fade(seg(strong2, 0, 7), 0.15, 2.0), ogg=True)
    strong1 = load(gq + "03 - Strong cheering - I.mp3")
    write("match_win", fade(seg(strong1, 0, 13), 0.2, 4.0), ogg=True)
    rhythm = load(gq + "01 - Strong cheering and strong rhythmic cheering.mp3")
    write("match_win_2", fade(seg(rhythm, 0, 14), 0.2, 4.0), ogg=True)
    church = load(os.path.join(SRC, "oga_applause-clapping-church-crowd-immersive.wav"))
    write("applause_2", fade(seg(church, 6, 10), 0.1, 1.5), peak=0.8, ogg=True)
    write("applause_3", fade(seg(church, 20, 23), 0.1, 1.3), peak=0.8, ogg=True)

    # ---- 主審のコール（スコア） ----
    tn = os.path.join(SRC, "sports", "Sports", "tennis")
    calls = {
        "call_love": "love", "call_15": "fifteen", "call_30": "thirty", "call_40": "fourty_five",
        "call_adv_server": "advantage_server", "call_adv_receiver": "advantage_receiver",
        "call_game_server": "game_server", "call_game_receiver": "game_receiver",
        "call_set_server": "set_server", "call_set_receiver": "set_receiver",
    }
    for name, f in calls.items():
        write(name, fade(trim(load(os.path.join(tn, f + ".wav")), 0.03), 0.002, 0.05), peak=0.85)


if __name__ == "__main__":
    main()
