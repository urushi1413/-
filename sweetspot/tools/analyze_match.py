#!/usr/bin/env python3
# SWEET SPOT — 試合の記録（matchlog.csv）を集計して、AI の何が強いのか（どうやってポイントを失っているか）を調べる
#   python tools/analyze_match.py matchlog.csv            全部の試合
#   python tools/analyze_match.py matchlog.csv --last 3   最後の 3 試合だけ
import csv, sys
from collections import Counter, defaultdict

def f(x, d=0.0):
    try: return float(x)
    except (TypeError, ValueError): return d

def pct(a, b): return f"{100.0 * a / b:5.1f}%" if b else "   - "

def bucket(v, edges, names):
    for e, n in zip(edges, names):
        if v < e: return n
    return names[-1]

def main():
    if len(sys.argv) < 2:
        print("usage: python tools/analyze_match.py matchlog.csv [--last N]"); return
    rows = list(csv.DictReader(open(sys.argv[1], encoding="utf-8")))
    matches = []
    for r in rows:
        if r["match"] not in matches: matches.append(r["match"])
    if "--last" in sys.argv:
        n = int(sys.argv[sys.argv.index("--last") + 1]); keep = set(matches[-n:])
        rows = [r for r in rows if r["match"] in keep]; matches = matches[-n:]
    shots = [r for r in rows if r["event"] == "shot"]
    points = [r for r in rows if r["event"] == "point"]
    ends = [r for r in rows if r["event"] == "match"]
    if not points:
        print("ポイントの記録がありません（試合モードで遊んでください）"); return
    # 試合の情報
    p0 = points[-1]
    print(f"試合 {len(matches)}  ポイント {len(points)}   （最後の試合：相手 persona={p0['persona']} level={p0['level']} {p0['surface']} women={p0['women']}）")
    for e in ends: print(f"  試合 {e['match']}: 勝ち {e['who']}  {e['type']}")
    won = sum(1 for p in points if p["winner"] == "YOU")
    print(f"\n== 取ったポイント： {won}/{len(points)} = {pct(won, len(points))}")
    for sv in ("YOU", "AI"):
        ps = [p for p in points if p["server"] == sv]
        w = sum(1 for p in ps if p["winner"] == "YOU")
        print(f"  {'自分のサーブ' if sv == 'YOU' else '相手のサーブ'}: {w}/{len(ps)} = {pct(w, len(ps))}")
    # ラリーの長さ別
    print("\n== ラリーの長さ別（サーブも 1 打）")
    by = defaultdict(lambda: [0, 0])
    for p in points:
        k = bucket(int(f(p["rally"])), [3, 5, 9, 999], ["1-2", "3-4", "5-8", "9+"])
        by[k][0] += 1; by[k][1] += p["winner"] == "YOU"
    for k in ["1-2", "3-4", "5-8", "9+"]:
        n, w = by[k]; print(f"  {k:>4}打: {n:4d} ポイント（{pct(n, len(points))}）  取った {pct(w, n)}")
    # 失ったポイントの内訳
    lost = [p for p in points if p["winner"] == "AI"]
    gained = [p for p in points if p["winner"] == "YOU"]
    def how(p):
        if p["ender"] == "AI":
            return {"ace": "AI のエース・返せなかったサーブ", "winner": "AI のウィナー"}.get(p["end_kind"], p["end_kind"])
        return {"error_net": "自分のミス（ネット）", "error_out": "自分のミス（アウト）", "double_fault": "自分のダブルフォールト"}.get(p["end_kind"], p["end_kind"])
    print(f"\n== 失ったポイント {len(lost)} の内訳")
    c = Counter(how(p) for p in lost)
    for k, v in c.most_common(): print(f"  {k:28s} {v:4d}  {pct(v, len(lost))}")
    print(f"\n== 取ったポイント {len(gained)} の内訳")
    def how2(p):
        if p["ender"] == "YOU":
            return {"ace": "自分のエース・返されなかったサーブ", "winner": "自分のウィナー"}.get(p["end_kind"], p["end_kind"])
        return {"error_net": "AI のミス（ネット）", "error_out": "AI のミス（アウト）", "double_fault": "AI のダブルフォールト"}.get(p["end_kind"], p["end_kind"])
    c2 = Counter(how2(p) for p in gained)
    for k, v in c2.most_common(): print(f"  {k:28s} {v:4d}  {pct(v, len(gained))}")

    # AI のウィナー：どんな球で、どれだけ遠かったか
    aw = [p for p in lost if p["ender"] == "AI" and p["end_kind"] == "winner"]
    if aw:
        print(f"\n== AI のウィナー {len(aw)} 本")
        print("  球種:", ", ".join(f"{k} {v}" for k, v in Counter(p["ender_type"] for p in aw).most_common()))
        print("  速さ:", ", ".join(f"{k} {v}" for k, v in Counter(bucket(f(p['ender_kmh']), [90, 120, 150, 999], ['<90', '90-120', '120-150', '150+']) for p in aw).most_common()))
        print("  自分が一番近づいた距離:", ", ".join(f"{k} {v}" for k, v in Counter(bucket(f(p['you_min_dist']), [1.0, 2.0, 3.5, 99], ['1m 未満（届いたはず）', '1-2m', '2-3.5m', '3.5m 以上（完全に逆を突かれた）']) for p in aw).most_common()))
    # AI のサーブ
    ai_serves = [s for s in shots if s["who"] == "AI" and s["type"] == "SERVE"]
    if ai_serves:
        kmh = [f(s["kmh"]) for s in ai_serves]; inn = [s for s in ai_serves if s["result"] == "IN"]
        aces = [p for p in lost if p["ender"] == "AI" and p["end_kind"] == "ace"]
        ret_pts = [p for p in points if p["server"] == "AI"]
        print(f"\n== AI のサーブ：平均 {sum(kmh) / len(kmh):.0f} km/h、入った {pct(len(inn), len(ai_serves))}、返せなかった {len(aces)}/{len(ret_pts)} = {pct(len(aces), len(ret_pts))}")
    # 自分のミス：どこから、どんな球で、来た球の速さ
    yshots = [s for s in shots if s["who"] == "YOU" and s["type"] != "SERVE"]
    if yshots:
        print(f"\n== 自分の打球 {len(yshots)} 本（サーブ以外）")
        def zone(s):
            z = abs(f(s["hit_z"]))
            return "ネット前(<5m)" if z < 5 else "中間(5-9m)" if z < 9 else "ベースライン(9m+)"
        for title, key in (("立ち位置", zone), ("球種", lambda s: s["type"]), ("当たりの質", lambda s: bucket(f(s["quality"]), [0.3, 0.5, 0.7, 2], ["<0.3", "0.3-0.5", "0.5-0.7", "0.7+"]))):
            print(f"  -- {title}別：入った / ネット / アウト")
            g = defaultdict(list)
            for s in yshots: g[key(s)].append(s["result"])
            for k, v in sorted(g.items(), key=lambda kv: -len(kv[1])):
                n = len(v); print(f"     {k:16s} {n:4d}本  IN {pct(v.count('IN'), n)}  NET {pct(v.count('NET'), n)}  OUT {pct(v.count('OUT'), n)}")
        sp = [f(s["kmh"]) for s in yshots]; q = [f(s["quality"]) for s in yshots]
        print(f"  平均の速さ {sum(sp) / len(sp):.0f} km/h、平均の質 {sum(q) / len(q):.2f}")
    # AI の打球
    ashots = [s for s in shots if s["who"] == "AI" and s["type"] != "SERVE"]
    if ashots:
        sp = [f(s["kmh"]) for s in ashots]; q = [f(s["quality"]) for s in ashots]
        att = [s for s in ashots if s["attack"] == "1"]; lap = [s for s in ashots if s["lapse"] == "1"]
        err = [s for s in ashots if s["result"] in ("NET", "OUT")]
        print(f"\n== AI の打球 {len(ashots)} 本：平均 {sum(sp) / len(sp):.0f} km/h、平均の質 {sum(q) / len(q):.2f}、ミス {pct(len(err), len(ashots))}")
        print(f"  決めにいった球 {len(att)}（{pct(len(att), len(ashots))}）：入った {pct(sum(1 for s in att if s['result'] == 'IN'), len(att))}")
        print(f"  打ち損じ {len(lap)}（{pct(len(lap), len(ashots))}）：ミスになった {pct(sum(1 for s in lap if s['result'] in ('NET', 'OUT')), len(lap))}")
        print("  球種:", ", ".join(f"{k} {v}" for k, v in Counter(s["type"] for s in ashots).most_common()))
        # 深さ：AI の球がどこに落ちたか（自分のコートは z>0）
        deep = [s for s in ashots if s["result"] == "IN" and f(s["land_z"]) > 8.0]
        print(f"  ベースラインから 4m 以内に落ちた深い球：{pct(len(deep), sum(1 for s in ashots if s['result'] == 'IN'))}")
    # まとめ：失点の大きな原因
    print("\n== まとめ（失ったポイントの多い順）")
    for k, v in c.most_common(4): print(f"  {k}: {v} 本（失点の {pct(v, len(lost)).strip()}）")

if __name__ == "__main__":
    main()
