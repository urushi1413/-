#!/usr/bin/env python3
# SWEET SPOT — 画面に出す日本語の文字を集めて ja_glyphs.h を作る（sweetspot/ で実行）
#   python3 tools/gen_glyphs.py
# ソース（*.c）の文字列（"..."）に出てくる ASCII 以外の文字を全部集める。日本語の表示を足したら作り直す
import glob, re
chars = set()
for f in sorted(glob.glob("*.c")):
    if f == "lang.c" or True:
        src = open(f, encoding="utf-8").read()
    for lit in re.findall(r'"((?:[^"\\\n]|\\.)*)"', src):
        for ch in lit:
            if ord(ch) > 127: chars.add(ch)
# よく使う記号とかなは全部入れておく
for c in range(0x3041, 0x3097): chars.add(chr(c))   # ひらがな
for c in range(0x30A1, 0x30FD): chars.add(chr(c))   # カタカナ
for ch in "、。・ー「」（）！？：＋－／％〜…　": chars.add(ch)
s = "".join(sorted(chars))
with open("ja_glyphs.h", "w", encoding="utf-8") as o:
    o.write("// 自動で作るファイル（tools/gen_glyphs.py）。画面に出す日本語の文字の一覧\n")
    o.write("static const char JA_GLYPHS[] = \"" + s.replace("\\", "\\\\").replace('"', '\\"') + "\";\n")
print(len(chars), "chars")
