#!/usr/bin/env python3
# SWEET SPOT — 選手の声の録音を、打つ前（_pre）と打った後（_voice）に切り分けて sounds/ に書き出す
#   使い方: python3 tools/cut_voices.py <録音フォルダ>
#   録音 1 つにつき、打った瞬間（contact 秒）と、その何秒前から声を鳴らし始めるか（lead 秒）を下の表に書く。
#   ゲームは打つ lead 秒前に _pre を鳴らし始め、打った瞬間に _voice（打球音と声の続き）を鳴らすので、元の録音どおりにつながる。
#   打つ前と打った後は同じ倍率で大きさをそろえる（元の録音の大きさの比のまま）
import glob
import os
import sys

import numpy as np
import soundfile as sf

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from build_sounds import SR, OUT, load, lp, hp, trim  # noqa: E402
from scipy.signal import fftconvolve  # noqa: E402

# (録音ファイル名の末尾, 書き出す名前, 打った瞬間（秒）, 打つ前の声の長さ（秒）)
#   打った瞬間は、無音から急に立ち上がる声（サーブ）、マイクに当たる低い「ボン」や強い立ち上がり（ストローク）から決めた
#   録音は近くで録った声だけ（打球音なし）なので、遠くに聞こえるように加工して、打った瞬間に打球音（hit_good）を重ねる
CUTS = [
    ("alcaraz.m4a",        "alcarazz_grunt", 0.770, 0.20),
    ("alcaraz_serve.m4a",  "alcarazz_serve", 1.690, 0.40),
    ("zverev.m4a",         "zverew_grunt",   0.680, 0.235),
    ("zverev2.m4a",        "zverew_grunt",   1.040, 0.40),
    ("zverev_serve.m4a",   "zverew_serve",   2.237, 0.48),
    ("djokovic_serve.m4a", "djokovik_serve", 2.221, 0.40),
]
HIT_PEAK = 0.89   # 打った後の声の大きさ（いまの hit_voice と同じ）
HIT_MIX = 3.0     # 重ねる打球音の大きさ（声のピークに対して）。録音に打球音が入っていないので足す。声は打球音の 3 分の 1 ほど（遠くの声）


# 選手ごとの遠さ（1 = ふつう。大きいほど遠い：響きが多く、声が小さく、こもる）
FAR = {"alcarazz": 1.8}


def far_away(x, seed, amt=1.0):
    """近くで録った声を、コートの向こうで聞こえるように：
         マイクが近いと強くなる低い音を削り、高い音を少し丸め、コートの跳ね返りと響きを足す"""
    rng = np.random.default_rng(seed)
    y = lp(hp(x, 300 * amt ** 0.3), 4800 / amt ** 0.4)
    n = int((0.8 + 0.3 * (amt - 1)) * SR)
    ir = np.zeros(n)
    ir[0] = 0.7 / amt                                 # 直接届く声は小さめ（遠い）
    for d, g in ((0.013, 0.45), (0.022, 0.35), (0.034, 0.3), (0.051, 0.22), (0.072, 0.17), (0.095, 0.12)):   # 地面・後ろの壁・客席
        ir[int(d * SR)] += g * amt ** 0.5 * rng.choice([-1, 1])
    tail = rng.normal(0, 1, n) * np.exp(-np.arange(n) / ((0.16 + 0.06 * (amt - 1)) * SR))
    tail[: int(0.02 * SR)] = 0
    ir += lp(tail, 3500) * 0.06 * amt
    return fftconvolve(y, ir)[: len(y)]


def make_pair(x, s, c, e, seed, amt, ball_idx):
    """録音 x の s〜e を遠くにして、打った瞬間 c に打球音を重ね、打つ前と後に分ける"""
    seg = far_away(x[s:e], seed, amt)   # 打つ前と後をまとめて加工（響きがつながるように）
    cc = c - s
    goods = sorted(glob.glob(os.path.join(OUT, "hit_good*.wav")))
    ball = trim(load(goods[ball_idx % len(goods)]))
    ball = ball / max(np.max(np.abs(ball)), 1e-6) * HIT_MIX * amt * np.max(np.abs(seg[cc:]))   # 遠い選手ほど声が小さい
    seg[cc:cc + len(ball)] += ball[: len(seg) - cc]
    pre, hit = seg[:cc].copy(), seg[cc:].copy()
    g = HIT_PEAK / max(np.max(np.abs(hit)), 1e-6)       # 打つ前と同じ倍率
    pre, hit = pre * g, hit * g
    n = int(0.01 * SR)
    pre[:n] *= np.linspace(0, 1, n)                     # 鳴り始めをなめらかに（つなぎ目は切らない）
    f = int(0.06 * SR)
    hit[-f:] *= np.linspace(1, 0, f)
    return pre, hit


def tail_end(x, start):
    """声が消えるところ（20ms ごとの大きさが最大の 3% 未満になって 0.1 秒続いたら終わり。長くても 1.3 秒）"""
    w = int(0.02 * SR)
    env = np.array([np.sqrt(np.mean(x[i:i + w] ** 2)) for i in range(start, len(x) - w, w)])
    if not len(env):
        return len(x)
    thr = 0.03 * env.max()
    quiet = 0
    for k, v in enumerate(env):
        quiet = quiet + 1 if v < thr else 0
        if quiet >= 5:
            return min(start + (k - 4) * w, start + int(1.3 * SR))
    return min(len(x), start + int(1.3 * SR))


def main():
    src = sys.argv[1]
    # いまの共通の声（grunt_pre / hit_voice）はアルカラスの録音なので、アルカラスの 1 つ目にする。
    #   共通の打つ前の声は録音が小さく、ゲームで 4 倍にしていた。選手本人の声は倍率なしで鳴らすので、ここで 4 倍にしておく
    #   打球音も入った録音なので、全体を遠くにしてから、はっきりした打球音を重ね直す
    pre, _ = sf.read(os.path.join(OUT, "grunt_pre.wav"))
    hit, _ = sf.read(os.path.join(OUT, "hit_voice.wav"))
    x = np.concatenate([pre * 4.0, hit])
    pre, hit = make_pair(x, 0, len(pre), len(x), 1, FAR["alcarazz"], 1)
    sf.write(os.path.join(OUT, "alcarazz_grunt_pre.wav"), np.clip(pre, -1, 1).astype(np.float32), SR, subtype="PCM_16")
    sf.write(os.path.join(OUT, "alcarazz_hit_voice.wav"), np.clip(hit, -1, 1).astype(np.float32), SR, subtype="PCM_16")
    print(f"alcarazz_grunt_pre         {len(pre) / SR:.2f}s   alcarazz_hit_voice         {len(hit) / SR:.2f}s   (いまの共通の声)")
    count = {"alcarazz_grunt": 1}
    for suffix, base, contact, lead in CUTS:
        path = [p for p in glob.glob(os.path.join(src, "*" + suffix)) if os.path.basename(p).split("-", 1)[-1] == suffix
                or os.path.basename(p) == suffix]
        if not path:
            print("見つからない:", suffix)
            continue
        x = load(path[0])
        c, s = int(contact * SR), int((contact - lead) * SR)
        e = tail_end(x, c)
        who = base.rsplit("_", 1)[0]
        pre, hit = make_pair(x, s, c, e, len(count) + int(contact * 1000), FAR.get(who, 1.0), int(contact * 1000))
        g = 1.0
        k = count.get(base, 0) + 1
        count[base] = k
        suf = "" if k == 1 else f"_{k}"
        kind = "serve" if base.endswith("_serve") else "grunt"
        who = base.rsplit("_", 1)[0]
        names = (f"{who}_serve_pre{suf}", f"{who}_serve_voice{suf}") if kind == "serve" else (f"{who}_grunt_pre{suf}", f"{who}_hit_voice{suf}")
        for nm, y in zip(names, (pre, hit)):
            sf.write(os.path.join(OUT, nm + ".wav"), np.clip(y, -1, 1).astype(np.float32), SR, subtype="PCM_16")
        print(f"{names[0]:26s} {len(pre) / SR:.2f}s   {names[1]:26s} {len(hit) / SR:.2f}s   (x{g:.1f})")


if __name__ == "__main__":
    main()
