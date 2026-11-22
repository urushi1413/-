import sys
from PIL import Image
import numpy as np
def load(p): return np.asarray(Image.open(p).convert('RGB')).astype(int)
def analyze(P, A):
    img = load(f'vm_{P}_{A}_0.png'); bg = load(f'vm_{P}_{A}_1.png')
    m = np.abs(img - bg).sum(axis=2) > 45
    m[:, :640] = False                                 # 左のメニューは使わない
    ys, xs = np.nonzero(m)
    top = ys.min()
    bright = (img.sum(axis=2) > 600) & m               # 白い靴・靴下
    feet = np.nonzero(bright)[0].max()
    cx = int(xs[ys < top + 12].mean())                 # 頭の真ん中の列
    def run(r, x):                                     # x を含む横の続き（左端・右端）
        if not m[r, x]: return None
        l = x
        while l > 0 and m[r, l - 1]: l -= 1
        rr = x
        while rr < m.shape[1] - 1 and m[r, rr + 1]: rr += 1
        return l, rr
    out = {"top": top, "feet": feet, "H": feet - top}
    if A == 0:
        # 頭から下へ：幅が一番狭い所（首）を見つけ、その下で幅が首の 1.6 倍を超えた最初の行＝肩線
        widths = []
        for r in range(top, top + int(0.35 * (feet - top))):
            rn = run(r, cx); widths.append((r, (rn[1] - rn[0] + 1) if rn else 0))
        headw = max(w for r, w in widths[:int(0.12 * (feet - top))])
        # 首：頭の幅が最大の行より下で、幅が最小の行
        rmax = max(widths[:int(0.12 * (feet - top))], key=lambda t: t[1])[0]
        neck_r, neck_w = min(((r, w) for r, w in widths if r > rmax and w > 0 and r < top + 0.25 * (feet - top)), key=lambda t: t[1])
        sh = next(r for r, w in widths if r > neck_r and w > 1.6 * neck_w)
        wd = dict(widths)
        mid_r = (neck_r + sh) // 2
        out.update({"headW": headw, "neckW": neck_w, "neck_row": neck_r, "shoulder": sh, "neckMid": wd.get(mid_r, 0), "neckBase": wd.get(sh - 2, 0)})
    else:
        # 横：前の端（右）を行ごとに。鼻の先（頭の上の方で一番前）から下で、前の端が一番大きく引っ込む行＝あご
        fr = {}
        for r in range(top, top + int(0.25 * (feet - top))):
            row = np.nonzero(m[r, cx - 120: cx + 120])[0]
            if len(row): fr[r] = cx - 120 + row.max()
        rows = sorted(fr)
        upper = [r for r in rows if r < top + 0.14 * (feet - top)]
        nose = max(upper, key=lambda r: fr[r])
        cands = [r for r in rows if nose + 3 < r < nose + 0.10 * (feet - top) and r + 3 in fr]
        chin = max(cands, key=lambda r: fr[r] - fr[r + 3])
        out.update({"nose": nose, "chin": chin})
    return out
for P in sys.argv[1:]:
    f = analyze(P, 0); s = analyze(P, 90)
    head = s["chin"] - s["top"]                        # 頭のてっぺん → あご（横の絵）
    neck = (f["shoulder"] - f["top"]) - head           # あご → 肩線（正面の絵の肩線を、てっぺんからの距離でそろえる）
    print(f"player {P}: FRONT top={f['top']} feet={f['feet']} H={f['H']}px  shoulderLine={f['shoulder']}  headW={f['headW']}px neckW={f['neckW']}px (row {f['neck_row']})")
    print(f"           SIDE  top={s['top']} feet={s['feet']} H={s['H']}px  nose={s['nose']} chin={s['chin']}")
    print(f"           head(top->chin)={head}px  chin->shoulder={neck}px")
    print(f"           head/H = {head/f['H']:.4f} (1/{f['H']/head:.2f})  target 1/7.5 ±10% -> 1/6.82..1/8.33   {'OK' if 6.818 <= f['H']/head <= 8.333 else 'NG'}")
    print(f"           (chin->shoulder)/head = {neck/head:.3f}  target 0.40 ±10% -> 0.36..0.44   {'OK' if 0.36 <= neck/head <= 0.44 else 'NG'}")
    print(f"           neckW/headW = {f['neckW']/f['headW']:.3f}  target 0.60 ±10% -> 0.54..0.66   {'OK' if 0.54 <= f['neckW']/f['headW'] <= 0.66 else 'NG'}   (taper: top {f['neckW']}px / mid {f['neckMid']}px / base {f['neckBase']}px)")
