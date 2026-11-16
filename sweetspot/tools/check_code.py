#!/usr/bin/env python3
# SWEET SPOT — 体験版の勝利の証明コードを確かめる
#   使い方（sweetspot/ で）: python3 tools/check_code.py SS-XXXXX-XXXXX-XXXXX-XXXXXX [もう 1 つ ...]
#   web/demo_secret.txt（ブラウザ版を作ったときの秘密の鍵）が要る。
#   正しいコードは web/used_codes.txt に書き足し、同じコードが 2 回来たら知らせる（使い回しを防ぐ）
import datetime
import os
import sys

HERE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
SECRET = os.path.join(HERE, "web", "demo_secret.txt")
USED = os.path.join(HERE, "web", "used_codes.txt")
B32 = "0123456789ABCDEFGHJKMNPQRSTVWXYZ"
LEVELS = ["EASY", "NORMAL", "PRO", "?"]
STYLES = ["REAL", "CASUAL", "?", "?"]
M64 = (1 << 64) - 1


def keyed_hash(key, data):
    h = 1469598103934665603
    for b in key + data + key:
        h ^= b
        h = (h * 1099511628211) & M64
    h ^= h >> 30; h = (h * 0xbf58476d1ce4e5b9) & M64
    h ^= h >> 27; h = (h * 0x94d049bb133111eb) & M64
    h ^= h >> 31
    return h


def decode(code):
    s = code.strip().upper().replace("-", "")
    if s.startswith("SS"):
        s = s[2:]
    s = s.replace("O", "0").replace("I", "1").replace("L", "1")   # 読み間違えやすい字
    if len(s) != 21 or any(c not in B32 for c in s):
        return None
    v = 0
    for c in s:
        v = (v << 5) | B32.index(c)
    v >>= 1                                  # 最後の 1 文字は 4 ビット分だけ使う（105 - 104）
    return v.to_bytes(13, "big")


def check(code, key):
    b = decode(code)
    if b is None:
        return False, "形がちがう（SS- のあとに 21 文字）"
    h = keyed_hash(key, b[:8])
    if bytes((h >> (8 * i)) & 255 for i in range(5)) != b[8:]:
        return False, "確かめの数字が合わない（打ち間違いか、作られたコード）"
    ver, level, style = b[0] >> 4, (b[0] >> 2) & 3, b[0] & 3
    day = datetime.date(2026, 1, 1) + datetime.timedelta(days=(b[5] << 8) | b[6])
    info = (f"版 {ver} / AI {LEVELS[level]} / 難しさ {STYLES[style]} / 相手の番号 {b[1]} / "
            f"ゲーム {b[2] >> 4}-{b[2] & 15} / 取ったポイント {b[3]}-{b[4]} / 日付 {day}")
    ok = ver == 1 and level >= 1 and style == 0 and (b[2] >> 4) > (b[2] & 15)
    return ok, info + ("" if ok else "  ← 条件を満たしていない")


def main():
    if len(sys.argv) < 2:
        print(__doc__ or "使い方: python3 tools/check_code.py <コード>")
        sys.exit(1)
    if not os.path.exists(SECRET):
        print("web/demo_secret.txt がない（ブラウザ版を作ったときの鍵が要る）")
        sys.exit(1)
    key = open(SECRET).read().strip().encode()
    used = set()
    if os.path.exists(USED):
        used = {ln.split()[0] for ln in open(USED) if ln.strip()}
    for code in sys.argv[1:]:
        ok, info = check(code, key)
        norm = code.strip().upper()
        if ok and norm in used:
            print(f"使用済み  {norm}  {info}")
        elif ok:
            print(f"OK        {norm}  {info}")
            with open(USED, "a") as f:
                f.write(f"{norm}  {datetime.date.today()}  {info}\n")
        else:
            print(f"だめ      {norm}  {info}")


if __name__ == "__main__":
    main()
