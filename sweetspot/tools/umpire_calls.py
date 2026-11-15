#!/usr/bin/env python3
# SWEET SPOT — 録音した主審・線審のコールを、ゲームで使う名前にして sounds/ に書き出す
#   使い方: python3 tools/umpire_calls.py <録音フォルダ>
#   主審のコールは会場のスピーカーを通した音にする（umpire_pa.py の加工）。
#   アウト：電子判定（スピーカーから流れる）は加工したもの、線審の叫び（ローランドギャロスは人がコールする）は加工しない
import glob
import os
import sys

import numpy as np
import soundfile as sf

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from build_sounds import SR, OUT, load, trim, fade, norm  # noqa: E402
from umpire_pa import pa  # noqa: E402

# 録音ファイル名の末尾 → ゲームでの名前（スコアはサーバーの点が先）
CALLS = {
    "15-0": "call_15_0", "30-0": "call_30_0", "40-0": "call_40_0",
    "15-15": "call_15_15", "30-30": "call_30_30", "15-30": "call_15_30",
    "30-40": "call_30_40", "40-30": "call_40_30", "40-15": "call_40_15",
    "deuce": "call_deuce", "A_server": "call_adv_server", "A_receiver": "call_adv_receiver",
    "game_set_match_server": "call_gsm_server", "game_set_match_receiver": "call_gsm_receiver",
}


def find(src, name):
    for p in glob.glob(os.path.join(src, "*.m4a")):
        base = os.path.splitext(os.path.basename(p))[0]
        if base == name or base.split("-", 1)[-1] == name or base.endswith("-" + name):
            return p
    return None


def write(name, y):
    sf.write(os.path.join(OUT, name + ".wav"), np.clip(y, -1, 1).astype(np.float32), SR, subtype="PCM_16")
    print(f"{name:22s} {len(y) / SR:5.2f}s")


def main():
    src = sys.argv[1]
    for rec, name in CALLS.items():
        p = find(src, rec)
        if p:
            write(name, pa(load(p)))
        else:
            print("見つからない:", rec)
    # アウト：電子判定（スピーカー）2 種類と、線審の叫び（加工しない。前後の無音を切って大きさだけそろえる）
    o1, o2 = find(src, "out1"), find(src, "out2")
    if o2:
        write("call_out", pa(load(o2)))
    if o1:
        write("call_out_2", pa(load(o1)))
        write("call_out_line", fade(norm(trim(load(o1), 0.03), 0.9), 0.003, 0.08))


if __name__ == "__main__":
    main()
