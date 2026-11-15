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
from build_sounds import SR, OUT, load  # noqa: E402

# (録音ファイル名の末尾, 書き出す名前, 打った瞬間（秒）, 打つ前の声の長さ（秒）)
#   打った瞬間は、無音から急に立ち上がる打球音（サーブ）、マイクに当たる低い「ボン」や強い立ち上がり（ストローク）から決めた
CUTS = [
    ("alcaraz.m4a",        "alcarazz_grunt", 0.770, 0.20),
    ("alcaraz_serve.m4a",  "alcarazz_serve", 1.690, 0.40),
    ("zverev.m4a",         "zverew_grunt",   0.680, 0.235),
    ("zverev2.m4a",        "zverew_grunt",   1.040, 0.40),
    ("zverev_serve.m4a",   "zverew_serve",   2.237, 0.48),
    ("djokovic_serve.m4a", "djokovik_serve", 2.221, 0.40),
]
HIT_PEAK = 0.89   # 打った後の声の大きさ（いまの hit_voice と同じ）


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
    pre, _ = sf.read(os.path.join(OUT, "grunt_pre.wav"))
    hit, _ = sf.read(os.path.join(OUT, "hit_voice.wav"))
    sf.write(os.path.join(OUT, "alcarazz_grunt_pre.wav"), np.clip(pre * 4.0, -1, 1).astype(np.float32), SR, subtype="PCM_16")
    sf.write(os.path.join(OUT, "alcarazz_hit_voice.wav"), hit.astype(np.float32), SR, subtype="PCM_16")
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
        pre, hit = x[s:c].copy(), x[c:e].copy()
        g = HIT_PEAK / max(np.max(np.abs(hit)), 1e-6)       # 打つ前と同じ倍率
        pre, hit = pre * g, hit * g
        n = int(0.01 * SR)
        pre[:n] *= np.linspace(0, 1, n)                     # 鳴り始めをなめらかに（つなぎ目は切らない）
        f = int(0.06 * SR)
        hit[-f:] *= np.linspace(1, 0, f)
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
