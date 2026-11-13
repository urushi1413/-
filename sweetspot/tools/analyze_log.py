"""playlog.csv を集計して、どこで失敗しているかを出す。

使い方（sweetspot フォルダで）:
    python tools/analyze_log.py playlog.csv

標準ライブラリだけで動く。
"""
import csv
import sys
from collections import Counter


def f(row, key):
    v = row.get(key, "")
    try:
        return float(v)
    except ValueError:
        return None


def pct(a, b):
    return f"{a * 100 / b:.0f}%" if b else "-"


def bar(n, total, width=30):
    return "#" * (round(n * width / total) if total else 0)


def main(path):
    with open(path, newline="", encoding="utf-8") as fp:
        rows = list(csv.DictReader(fp))
    swings = [r for r in rows if r["event"] == "swing"]
    noswing = [r for r in rows if r["event"] == "no_swing"]
    serves = [r for r in rows if r["event"] == "serve"]

    print(f"=== {path}: swing {len(swings)} / no_swing {len(noswing)} / serve {len(serves)} ===\n")

    # 1. 振った球の結末
    hits = [r for r in swings if r["miss"] == ""]
    print("[1] 振った球の結末")
    out = Counter(r["result"] for r in swings)
    for k in ["IN", "OUT", "NET", "WHIFF", "?"]:
        print(f"  {k:6s} {out[k]:4d} {pct(out[k], len(swings)):>5s} {bar(out[k], len(swings))}")
    whiff = Counter(r["miss"] for r in swings if r["miss"])
    if whiff:
        print("  空振りの理由: " + ", ".join(f"{k} {v}" for k, v in whiff.most_common()))
    print()

    # 2. 原因：位置（足）かタイミング（手）か
    print("[2] 原因の切り分け（その立ち位置で一番いいタイミングなら何%だったか）")
    pos = tim = ok = 0
    for r in swings:
        best = f(r, "best_quality_here") or 0.0
        q = f(r, "quality") or 0.0
        if best < 0.40:
            pos += 1
        elif r["miss"] or q < best * 0.5:
            tim += 1
        else:
            ok += 1
    n = len(swings)
    print(f"  立ち位置が悪い（どのタイミングでも 40% 未満）  {pos:4d} {pct(pos, n):>5s} {bar(pos, n)}")
    print(f"  タイミングが悪い（位置はいいのに質が半分以下） {tim:4d} {pct(tim, n):>5s} {bar(tim, n)}")
    print(f"  まあまあ                                     {ok:4d} {pct(ok, n):>5s} {bar(ok, n)}")
    print()

    # 3. タイミングのずれの分布
    offs = [f(r, "timing_offset_ms") for r in swings if f(r, "timing_offset_ms") is not None]
    if offs:
        print("[3] タイミングのずれ（マイナス＝早い、プラス＝遅い）")
        bins = [(-9999, -150), (-150, -75), (-75, -25), (-25, 25), (25, 75), (75, 150), (150, 9999)]
        labels = ["<-150ms", "-150..-75", "-75..-25", "-25..+25", "+25..+75", "+75..+150", ">+150ms"]
        for (lo, hi), lab in zip(bins, labels):
            c = sum(1 for o in offs if lo <= o < hi)
            print(f"  {lab:>10s} {c:4d} {pct(c, len(offs)):>5s} {bar(c, len(offs))}")
        offs.sort()
        print(f"  中央値 {offs[len(offs) // 2]:+.0f} ms\n")

    # 4. 打点
    if hits:
        print("[4] 当たった球の打点")
        dist = sorted(f(r, "dist_m") for r in hits)
        hgt = sorted(f(r, "height_m") for r in hits)
        ang = sorted(f(r, "angle_deg") for r in hits)
        mid = len(hits) // 2
        print(f"  距離 中央値 {dist[mid]:.2f} m（理想 0.94）  向き 中央値 {ang[mid]:+.0f}°（理想 25）  高さ 中央値 {hgt[mid]:.2f} m")
        jam = sum(1 for d in dist if d < 0.79)
        st = sum(1 for d in dist if d > 1.09)
        print(f"  詰まり（近すぎ） {pct(jam, len(hits))}   伸びきり（遠すぎ） {pct(st, len(hits))}")
        strokes = Counter(r["stroke"] for r in hits)
        print("  打ち方: " + ", ".join(f"{k} {v}" for k, v in strokes.most_common()))
        print()

    # 5. 来た球の速さ別の成功率
    print("[5] 来た球の速さ別（打った瞬間の速さ）")
    for lo, hi in [(0, 40), (40, 55), (55, 70), (70, 90), (90, 999)]:
        grp = [r for r in swings if (f(r, "in_kmh") or 0) >= lo and (f(r, "in_kmh") or 0) < hi]
        if not grp:
            continue
        good = sum(1 for r in grp if r["result"] == "IN")
        print(f"  {lo:3d}-{hi if hi < 999 else '':<3} km/h  {len(grp):4d} 球  IN {pct(good, len(grp)):>5s}")
    print()

    # 6. 振らなかった球
    if noswing:
        print("[6] 振らなかった球")
        inball = [r for r in noswing if r["result"] in ("IN", "-")]
        reach = [r for r in inball if r["miss"] == "reachable"]
        print(f"  入る球なのに振らなかった {len(inball)}（うち届く距離を通った {len(reach)}）")
        print(f"  アウトの球を見送った {len(noswing) - len(inball)}")
        if inball:
            d = sorted(f(r, "dist_m") for r in inball)
            print(f"  届かなかった球の、一番近づいた距離 中央値 {d[len(d) // 2]:.2f} m（届くのは 2.1 m まで）")
        print()

    # 7. 反応
    fm = [f(r, "first_move_s") for r in rows if (f(r, "first_move_s") or -1) >= 0]
    cs = [f(r, "charge_start_s") for r in rows if (f(r, "charge_start_s") or -1) >= 0]
    if fm or cs:
        print("[7] 反応（相手が打ってから）")
        if fm:
            fm.sort()
            print(f"  動き出し 中央値 {fm[len(fm) // 2]:.2f} s")
        if cs:
            cs.sort()
            print(f"  溜め始め 中央値 {cs[len(cs) // 2]:.2f} s")
        print()

    # 8. サーブ
    if serves:
        print("[8] サーブ")
        for name in sorted(set(r["shot"] for r in serves)):
            g = [r for r in serves if r["shot"] == name]
            c = Counter(r["result"] for r in g)
            print(f"  {name:12s} {len(g):4d} 本  IN {pct(c['IN'], len(g)):>5s}  OUT {c['OUT']}  NET {c['NET']}  空振り {c['WHIFF']}")


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "playlog.csv")
