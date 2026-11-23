# 頭・顔・首・肩の比率を、ゲームの画面のピクセルから測る（確かめる用）。
#   使い方：python3 tools/measure_head_px.py <画像のフォルダ> 2 3 6 11
#   画像は、確認用の版（カメラを PV・PVD・PVF などで動かし、MPAL で鼻・耳・髪・顔の部品を決まった色にする版）で撮る：
#   M_<選手>_<0|90>_0.png（選手あり）・M_<選手>_<0|90>_1.png（選手なし）。0 = 真正面、90 = 真横。fovy 6°・距離 8m のほぼ平行投影
# 頭・顔・首・肩の比率を、ほぼ平行投影の正面（0°）・真横（90°）の画像のピクセルから測る。
# 画像：M_<P>_<A>_0.png（測る色：鼻＝緑、耳＝赤紫、髪＝黄、目＝水色、眉＝青、口＝赤）、M_<P>_<A>_1.png（選手なし）
import sys, math, json
import numpy as np
from PIL import Image
def load(p): return np.asarray(Image.open(p).convert('RGB')).astype(float)
def classes(img):
    r, g, b = img[..., 0], img[..., 1], img[..., 2]; mx = np.maximum(np.maximum(r, g), b) + 1e-6
    rn, gn, bn = r / mx, g / mx, b / mx; br = mx > 60
    return {"green": br & (gn > 0.9) & (rn < 0.35) & (bn < 0.35), "magenta": br & (rn > 0.8) & (bn > 0.8) & (gn < 0.35),
            "yellow": br & (rn > 0.8) & (gn > 0.8) & (bn < 0.35), "cyan": br & (gn > 0.8) & (bn > 0.8) & (rn < 0.35),
            "blue": br & (bn > 0.9) & (rn < 0.35) & (gn < 0.35), "red": br & (rn > 0.9) & (gn < 0.35) & (bn < 0.35), "black": mx < 22}
def runw(row, x):   # x を含む続き（左端・右端）
    if not row[x]: return None
    l = x
    while l > 0 and row[l - 1]: l -= 1
    r = x
    while r < len(row) - 1 and row[r + 1]: r += 1
    return l, r
def analyze(d, P):
    F = load(f'{d}/M_{P}_0_0.png'); Fb = load(f'{d}/M_{P}_0_1.png'); S = load(f'{d}/M_{P}_90_0.png'); Sb = load(f'{d}/M_{P}_90_1.png')
    mF = np.abs(F - Fb).sum(2) > 45; mS = np.abs(S - Sb).sum(2) > 45
    mF[:, :700] = False; mS[:, :700] = False; mF[:40] = False; mS[:40] = False   # 左のメニュー・右上の版
    cF, cS = classes(F), classes(S)
    o = {}
    ys, xs = np.nonzero(mF); topF = ys.min(); cx = int(round(xs[ys < topF + 25].mean()))
    ys, xs = np.nonzero(mS); topS = ys.min()
    # ---- 横：鼻先・あご ----
    front = {}
    for r in range(topS, topS + 400):
        row = np.nonzero(mS[r, 700:])[0]
        if len(row): front[r] = 700 + row.max()
    gy, gx = np.nonzero(cS["green"])
    noseTipX = gx.max(); noseTipY = int(gy[gx == gx.max()].mean()); noseBotS = gy.max()
    # ---- 正面：顔の部品 ----
    ey, ex = np.nonzero(cF["cyan"]); eyeY = ey.mean()
    L = ex < cx; Rr = ex >= cx
    eL = (ex[L].min(), ex[L].max()); eR = (ex[Rr].min(), ex[Rr].max())
    E = ((eL[1] - eL[0] + 1) + (eR[1] - eR[0] + 1)) / 2
    gap = eR[0] - eL[1] - 1
    by, bx = np.nonzero(cF["blue"] & (np.arange(F.shape[0])[:, None] < eyeY)); browY = by.mean()
    ny, nx = np.nonzero(cF["green"]); noseBot = ny.max(); noseW = nx.max() - nx.min() + 1
    my, mxs = np.nonzero(cF["red"] & (np.arange(F.shape[0])[:, None] > noseBot)); mouthY = my.mean(); mouthW = mxs.max() - mxs.min() + 1
    # あご先（横の絵）：口より下で、前の端が 3 行で 3px 以上引っ込み始める行（顔の前 → あごの下へ曲がる角）
    chin = next(r for r in range(int(mouthY) + 2, int(mouthY) + 200) if r in front and r + 3 in front and front[r] - front[r + 3] >= 3)
    chinX = front[chin]
    top = min(topF, topS); h = chin - top
    o.update(top=top, topF=topF, topS=topS, chin=chin, h=h, noseTipX=noseTipX, noseTipY=noseTipY, chinX=chinX)
    # 生え際：目の中心の間の列ごとに、眉より上で、下から見て最初の髪（黄）の行。真ん中の値
    hl = []
    xl, xr = int((eL[0] + eL[1]) / 2), int((eR[0] + eR[1]) / 2)
    for x in range(xl, xr + 1):
        col = np.nonzero(cF["yellow"][top:int(browY), x])[0]
        if len(col): hl.append(top + col.max() + 1)
    hairline = float(np.median(hl)) if hl else float('nan')
    # 耳（横の絵）
    ay, axs = np.nonzero(cS["magenta"])
    if len(ay) > 20: earTop, earBot, earX = ay.min(), ay.max(), axs.mean()
    else: earTop = earBot = earX = None
    # 幅（正面）：耳の色を抜いた、真ん中の列を含む続き
    mNoEar = mF & ~cF["magenta"]
    def width(r):
        q = runw(mNoEar[int(r)], cx); return (q[1] - q[0] + 1) if q else 0
    cheekRows = range(int(eyeY), int(noseBot) + 1)
    cheekW = max(width(r) for r in cheekRows); cheekRow = max(cheekRows, key=width)
    templeW = width(browY); jawW = width(mouthY)
    headW = max(width(r) for r in range(top, int(eyeY)))
    o.update(eyeY=eyeY, browY=browY, noseBot=noseBot, mouthY=mouthY, hairline=hairline, E=E, gap=gap, noseW=noseW, mouthW=mouthW,
             cheekW=cheekW, cheekRow=cheekRow, templeW=templeW, jawW=jawW, headW=headW, earTop=earTop, earBot=earBot, earX=earX)
    # ---- 首と肩（正面） ----
    # 首の付け根（正面の真ん中）：あご先より下で、真ん中の列に初めて襟（黒）が出る行
    col = np.nonzero(cF["black"][chin:chin + int(1.2 * h), cx - 3:cx + 4].any(axis=1))[0]
    collar = chin + int(col.min()) if len(col) else None
    wrow = {r: width(r) for r in range(chin, chin + int(1.3 * h))}
    nr = min(range(chin + 2, (collar or chin + int(0.6 * h))), key=lambda r: wrow[r])
    # 首と僧帽筋の角：一番細い行より下で、幅が 3 行続けて 1 行に 2px 以上広がり始める行
    start = (chin + collar) // 2 if collar else nr
    corner = next((r for r in range(start, chin + int(1.1 * h)) if all(wrow[r + k + 1] - wrow[r + k] >= 2 for k in range(3))), None)
    neckMidY = (chin + collar) // 2 if collar else (chin + corner) // 2
    top_n = chin + max(2, int(0.15 * ((collar or corner) - chin)))
    o.update(neckNarrow=nr, shoulder=collar, corner=corner, neckW=wrow[neckMidY], neckTopW=wrow[top_n], neckBaseW=wrow[corner - 2] if corner else None)
    # 肩幅：角から下 0.35h までで一番広い行（腕が体の前に重なる所は含む）
    shW, shRow = max(((wrow.get(r, 0), r) for r in range(corner, corner + int(0.35 * h))))
    xL1, xR1 = runw(mNoEar[shRow], cx)
    xL0, xR0 = runw(mNoEar[corner], cx)
    # 肩線の傾き：角（首の横の端）から外へ 0.35h（約 8cm：僧帽筋の長さ。腕の構えで動く肩先の丸みは入れない）の、上の端までの直線の角度
    angs = []
    for x0, sgn in ((xL0, -1), (xR0, 1)):
        xt = int(x0 + sgn * 0.35 * h)
        colm = np.nonzero(mF[corner - 5:, xt])[0]; yt = corner - 5 + colm.min()
        angs.append(math.degrees(math.atan2(yt - corner, abs(xt - x0))))
    o.update(shoulderW=shW, shoulderSlopeL=angs[0], shoulderSlopeR=angs[1])
    # ---- 横：奥行き・耳の位置・あごの線・首の厚み ----
    backX = min(700 + np.nonzero(mS[r, 700:])[0].min() for r in range(top, int(o['browY'])) if mS[r, 700:].any())   # 後頭部：眉より上の行（束ねた髪・ポニーテールは入れない）
    o['depth'] = noseTipX - backX
    o['earPos'] = (noseTipX - earX) / (noseTipX - backX) if earX is not None else None
    lipX = front.get(int(round(mouthY)))
    o['lipX'] = lipX
    # あごの下の線：あご先から後ろへ、列ごとに口の高さより下で初めて背景になる行（あごの下の面）。首にぶつかる（あご先の 0.25h 下まで背景がない）列で止める
    pts = []
    for x in range(int(chinX) - 1, int(chinX - 0.3 * h), -1):
        col = mS[int(mouthY):int(mouthY + 0.45 * h), x]
        off = np.nonzero(~col)[0]
        if not len(off) or mouthY + off[0] > chin + 0.25 * h: break
        pts.append((chinX - x, mouthY + off[0]))
    if len(pts) >= 4:
        k = np.polyfit([p[0] for p in pts], [p[1] for p in pts], 1)[0]
        o['jawAngle'] = -math.degrees(math.atan(k)); o['throatX'] = chinX - pts[-1][0]; o['throatY'] = pts[-1][1]
    else:
        o['jawAngle'] = None; o['throatX'] = chinX; o['throatY'] = chin
    o['jawAngleEar'] = math.degrees(math.atan2(chin - earBot, chinX - earX)) if earX is not None else None
    q = None
    rowS = mS[neckMidY] & ~cS["yellow"][neckMidY]   # 髪（束ねた髪）は入れない
    fx = 700 + np.nonzero(rowS[700:])[0].max()      # 首の前の端
    q = runw(rowS, fx); best = q[1] - q[0] + 1      # 前の端から続く続き（後ろに離れた髪は入れない）
    o['neckDepth'] = best
    return o
def judge(v, lo, hi): return "OK" if v is not None and lo <= v <= hi else "NG"
def report(d, P, name):
    o = analyze(d, P); h = o['h']; top = o['top']; E = o['E']
    fr = lambda y: (y - top) / h
    rows = []
    def add(item, v, lo, hi, tgt, fmt="{:.3f}"):
        rows.append((item, (fmt.format(v) if v is not None else "-"), tgt, judge(v, lo, hi) if v is not None else "測れない"))
    third = lambda a, b: (b - a) / h
    add("目の高さ（頭頂から）", fr(o['eyeY']), 0.45, 0.55, "0.50 ±10%")
    add("頭頂→生え際", fr(o['hairline']), 0.27, 0.33, "0.30 ±10%")
    add("生え際→眉", third(o['hairline'], o['browY']), 0.207, 0.275, "0.23〜0.25 ±10%")
    add("眉→鼻の下", third(o['browY'], o['noseBot']), 0.207, 0.275, "0.23〜0.25 ±10%")
    add("鼻の下→あご先", third(o['noseBot'], o['chin']), 0.207, 0.275, "0.23〜0.25 ±10%")
    add("口（鼻の下→あご先のうち上から）", (o['mouthY'] - o['noseBot']) / (o['chin'] - o['noseBot']), 0.30, 0.367, "1/3 ±10%")
    add("耳の上端 − 眉（頭の高さ比）", (o['earTop'] - o['browY']) / h if o['earTop'] is not None else None, -0.03, 0.03, "0 ±0.03")
    add("耳の下端 − 鼻の下（頭の高さ比）", (o['earBot'] - o['noseBot']) / h if o['earBot'] is not None else None, -0.03, 0.03, "0 ±0.03")
    add("目の幅 E（px）", E, 0, 1e9, "基準", "{:.1f}")
    add("目の間 / E", o['gap'] / E, 0.9, 1.1, "1.0 ±10%")
    add("頬骨の幅 / E", o['cheekW'] / E, 4.5, 5.5, "5 ±10%")
    add("鼻の幅 / E", o['noseW'] / E, 0.9, 1.1, "1.0 ±10%")
    add("口の幅 / E", o['mouthW'] / E, 1.35, 1.65, "1.5 ±10%")
    add("あごの幅 / 頬骨の幅", o['jawW'] / o['cheekW'], 0.70, 0.75, "0.70〜0.75")
    add("こめかみの幅 / 頬骨の幅", o['templeW'] / o['cheekW'], 0.81, 0.99, "0.90 ±10%")
    add("頭の最大幅 / 頬骨の幅", o['headW'] / o['cheekW'], 1.05, 1.10, "1.05〜1.10")
    add("頭の幅 / 頭の高さ", o['headW'] / h, 0.75, 0.80, "0.75〜0.80")
    add("奥行き（鼻先→後頭部）/ 頭の高さ", o['depth'] / h, 0.95, 1.00, "0.95〜1.00")
    add("耳の位置（鼻先から後頭部までのうち）", o['earPos'], 0.50, 0.60, "0.55（45:55）±0.05")
    add("あご先が鼻先より後ろ（px）", o['noseTipX'] - o['chinX'], 1, 1e9, "> 0", "{:.0f}")
    add("あご先が唇より後ろ（px）", (o['lipX'] - o['chinX']) if o['lipX'] else None, 0, 0.05 * h, "0〜少し", "{:.0f}")
    add("あごの線の角度（耳の下→あご先, 度）", o['jawAngleEar'], 25, 30, "25〜30°", "{:.1f}")
    add("首の長さ（あご先→首の付け根・正面の真ん中）/ 頭の高さ", (o['shoulder'] - o['chin']) / h if o['shoulder'] else None, 0.40, 0.50, "0.40〜0.50")
    add("（参考）あご先→首と肩の角 / 頭の高さ", (o['corner'] - o['chin']) / h if o['corner'] else None, -9, 9, "—")
    add("首の幅 / 頭の幅", o['neckW'] / o['headW'], 0.54, 0.66, "0.60 ±10%")
    add("首の幅 / あごの幅", o['neckW'] / o['jawW'], 0.9, 1.1, "ほぼ同じ（1.0 ±10%）")
    add("首の厚み / 首の幅", o['neckDepth'] / o['neckW'], 0.72, 0.88, "0.80 ±10%")
    add("肩線の傾き 左（度。首の角→外へ 0.35h）", o['shoulderSlopeL'], 15, 20, "15〜20°", "{:.1f}")
    add("肩線の傾き 右（度。首の角→外へ 0.35h）", o['shoulderSlopeR'], 15, 20, "15〜20°", "{:.1f}")
    add("肩幅 / 頭の幅", o['shoulderW'] / o['headW'], 2.3, 2.5, "2.3〜2.5")
    add("肩幅 / 頭の高さ", o['shoulderW'] / h, 1.8, 1.9, "1.8〜1.9")
    print(f"== {name}（{P}）  頭の高さ h = {h}px（頭頂 {top}、あご先 {o['chin']}）  首の幅 上/中/下 = {o['neckTopW']}/{o['neckW']}/{o['neckBaseW']}px")
    for it, v, t, j in rows: print(f"  {it:<34} {v:>8}   目標 {t:<20} {j}")
    return o, rows
if __name__ == "__main__":
    d = sys.argv[1]; names = {"2": "ジョコビッチ", "3": "ナダル", "6": "シナー", "11": "サバレンカ"}
    allo = {}
    for P in sys.argv[2:]:
        o, rows = report(d, P, names.get(P, P)); allo[P] = {k: (float(v) if v is not None else None) for k, v in o.items()}
    json.dump(allo, open(f'{d}/measure.json', 'w'), indent=1)
def profile(d, P):
    """正面の輪郭の幅（耳を抜く）/ h を、頭頂からの割合ごとに"""
    import numpy as np
    o = analyze(d, P)
    F = load(f'{d}/M_{P}_0_0.png'); Fb = load(f'{d}/M_{P}_0_1.png'); mF = np.abs(F - Fb).sum(2) > 45; mF[:, :700] = False
    mNoEar = mF & ~classes(F)["magenta"]
    ys, xs = np.nonzero(mF); cx = int(round(xs[ys < o['top'] + 25].mean()))
    out = []
    for f in (0.3, 0.4, 0.45, 0.5, 0.55, 0.6, 0.65, 0.7, 0.75, 0.8, 0.85, 0.9, 0.95):
        r = int(o['top'] + f * o['h']); q = runw(mNoEar[r], cx); out.append("%.2f:%.3f" % (f, (q[1] - q[0] + 1) / o['h'] if q else 0))
    return " ".join(out)
