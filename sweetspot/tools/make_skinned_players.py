# SWEET SPOT — リグ付きの人体モデル（assets/base_body.glb）から、選手ごとのスキニング用モデルを書き出す Blender のスクリプト
#
#   blender --background --python tools/make_skinned_players.py      （sweetspot/ で実行。pip の bpy でも動く）
#   入力：assets/base_body.glb（商用利用できるリグ付きのローポリ人体。例：Quaternius の CC0 の素材。環境変数 BASE_BODY で別の場所も指定できる）
#   出力：models/players/p00.glb〜p16.glb（選手の番号ごと。p00 は癖なし）・models/players/bones.txt（骨の対応表）
#
# しくみ
#   ・骨の名前を、決まった名前（hips spine1 spine2 spine3 neck head、clavicle upperarm forearm hand thigh shin foot toe の .R/.L）に付け替える。
#     元の名前は Rigify（DEF-…）・Mixamo（mixamorig:…）・UE（upperarm_r …）などを見て探す（BONE_ALIASES）。ゲームはこの決まった名前だけを見る
#   ・指などの細かい骨は、重みを親（手・足）に足して消す（骨の数を減らす：ブラウザ版の GPU で使える数に収める）
#   ・服は体と一体のメッシュのまま、面ごとに UV をパレット（models/palette.png）のマスに置いて色分けする（シャツ・パンツ・靴下・靴・肌）
#   ・髪・鼻・顔の絵を貼る面は、今のパーツモデルの作り方を流用して、元の頭の形に沿わせて作る（頭の骨に重み 100%）
#   ・体型（肩幅・胴の厚み・腕・脚・腰回り）はゲームの選手の値（data.c の BODY）を読んで頂点を動かす。頭の倍率は HEAD_SCALE。
#     身長はゲームが関節の位置で合わせる（モデルは元の身長のまま）
import bpy, bmesh, os, re, math, sys
from mathutils import Vector, Matrix
from mathutils.bvhtree import BVHTree

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, ".."))
sys.path.insert(0, HERE)

# ======================= 調整用の数値 =======================
HEAD_SCALE   = 0.92   # 頭の大きさ（元のモデルの頭に対する倍率）。全身で 7〜7.5 頭身
BODY_DIFF_K  = 0.6    # 選手ごとの体型の差（data.c の値の差に掛ける。1.6 だと筋肉が付きすぎた）
# 元のマネキンは胸・肩・僧帽筋が大きく、首と顔が太い。テニス選手らしく全体を細くする
SLIM_TORSO_W = 0.86   # 胴の横幅
SLIM_TORSO_D = 0.86   # 胴の厚み
SLIM_ARM     = 0.80   # 腕の太さ
SLIM_LEG     = 0.92   # 脚の太さ
TRAP_K       = 0.20   # 僧帽筋：首の付け根より上に出ている胸・鎖骨の部分の高さの倍率（首の付け根より下に収める。首そのものは動かさない）
NECK_W_TOP   = 0.096  # 首の左右の幅：上端（あごの下。一番細い）（m）
NECK_W_BASE  = 0.116  # 首の左右の幅：付け根（胸・鎖骨につながる所）（m）。頭の幅の約 0.6 倍
NECK_DEPTH_K = 0.90   # 首の前後の厚み（左右の幅に対する倍率。楕円柱）
HEAD_W       = 0.90   # 頭（顔）の横幅
HEAD_BACK_K  = 0.88   # 後頭部の前後の長さ（元のマネキンは前後に長い）
JAW_NARROW   = 0.82   # あごのあたりの横幅（頬から下を細く）
SHOULDER_DX  = 0.20   # 肩幅の倍率 1 につき、腕を横へ動かす量の基準（m）
SLEEVE_LEN   = 0.42   # 袖の長さ（上腕の長さに対する割合）
SHORTS_LEN   = 0.50   # パンツの裾（腿の長さに対する割合）
SOCK_LEN     = 0.24   # 靴下の高さ（すねの長さに対する割合）
WRIST_BAND   = (0.74, 0.90)   # リストバンド（前腕の長さに対する割合の範囲）
WAIST_K      = 0.55   # シャツとパンツの境（腰の骨の長さに対する割合）
COLLAR_T     = 0.30   # 襟：首の骨の付け根からこの割合まではシャツ
COLLAR_BAND  = 0.012  # 襟の差し色の幅（m）
HAIR_THICK   = 0.055  # ベースの髪の厚み（頭の半径に対する割合）
# ============================================================

BONE_ALIASES = {   # 決まった名前 → 元のモデルの骨の名前の候補（大文字小文字は区別しない）
    "hips":   ["DEF-hips", "hips", "pelvis", "mixamorig:Hips", "Hips", "root_pelvis"],
    "spine1": ["DEF-spine.001", "spine", "mixamorig:Spine", "spine_01", "Spine"],
    "spine2": ["DEF-spine.002", "spine.001", "chest", "mixamorig:Spine1", "spine_02", "Spine1"],
    "spine3": ["DEF-spine.003", "spine.002", "upper_chest", "mixamorig:Spine2", "spine_03", "Spine2"],
    "neck":   ["DEF-neck", "neck", "mixamorig:Neck", "neck_01", "Neck"],
    "head":   ["DEF-head", "head", "mixamorig:Head", "Head"],
}
for s, mx, ue in ((".R", "Right", "_r"), (".L", "Left", "_l")):
    BONE_ALIASES.update({
        "clavicle" + s: ["DEF-shoulder" + s, "shoulder" + s, "mixamorig:%sShoulder" % mx, "clavicle" + ue, mx + "Shoulder"],
        "upperarm" + s: ["DEF-upper_arm" + s, "upper_arm" + s, "mixamorig:%sArm" % mx, "upperarm" + ue, mx + "Arm"],
        "forearm" + s:  ["DEF-forearm" + s, "forearm" + s, "mixamorig:%sForeArm" % mx, "lowerarm" + ue, mx + "ForeArm"],
        "hand" + s:     ["DEF-hand" + s, "hand" + s, "mixamorig:%sHand" % mx, "hand" + ue, mx + "Hand"],
        "thigh" + s:    ["DEF-thigh" + s, "thigh" + s, "mixamorig:%sUpLeg" % mx, "thigh" + ue, mx + "UpLeg"],
        "shin" + s:     ["DEF-shin" + s, "shin" + s, "mixamorig:%sLeg" % mx, "calf" + ue, mx + "Leg"],
        "foot" + s:     ["DEF-foot" + s, "foot" + s, "mixamorig:%sFoot" % mx, "foot" + ue, mx + "Foot"],
        "toe" + s:      ["DEF-toe" + s, "toe" + s, "mixamorig:%sToeBase" % mx, "ball" + ue, mx + "ToeBase"],
    })

# パレット（make_player.py と同じマス）
PAL = {"skin": (0, 0), "shirt": (1, 0), "accent": (2, 0), "pants": (3, 0), "shoes": (4, 0), "socks": (5, 0), "hair": (6, 0), "wrist": (7, 0), "eye": (8, 0), "sole": (9, 0), "nose": (10, 0)}
def pal_uv(cell):
    c, r = PAL[cell]
    return ((c + 0.5) / 16.0, 1.0 - (r + 0.5) / 16.0)

def smooth(t): t = max(0.0, min(1.0, t)); return t * t * (3 - 2 * t)
def lerp(a, b, t): return a + (b - a) * t

# ---- 選手の体型（data.c の BODY）と髪型（models/players.txt）----
def read_body_table():
    src = open(os.path.join(ROOT, "data.c"), encoding="utf-8").read()
    m = re.search(r"static const float BODY\[PERSONA_COUNT\]\[5\] = \{(.*?)\};", src, re.S)
    rows = re.findall(r"\{\s*([0-9.]+)f,\s*([0-9.]+)f,\s*([0-9.]+)f,\s*([0-9.]+)f,\s*([0-9.]+)f\s*\}", m.group(1))
    return [tuple(1.0 + BODY_DIFF_K * (float(x) - 1.0) for x in r) for r in rows]   # 肩幅・胴の厚み・腕・脚・腰回り
def read_hair_table():
    hair = {}
    p = os.path.join(ROOT, "models", "players.txt")
    if os.path.exists(p):
        for line in open(p, encoding="utf-8"):
            line = line.split("#")[0] if not "=#" in line else line
            m = re.match(r"\s*(\d+)\s.*hair=(\w+)", line)
            if m: hair[int(m.group(1))] = m.group(2)
    return hair
WOMEN = set(range(9, 15))

# ---- 元のモデルを読む ----
BASE = os.environ.get("BASE_BODY") or os.path.join(ROOT, "assets", "base_body.glb")
if not os.path.exists(BASE):
    print("ERROR: %s がない。商用利用できるリグ付きの人体モデル（glb）をここに置く" % BASE); sys.exit(1)

def load_base():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.gltf(filepath=BASE)
    arms = [o for o in bpy.data.objects if o.type == "ARMATURE"]
    if not arms: raise SystemExit("ERROR: 骨（アーマチュア）がない")
    arm = max(arms, key=lambda a: len(a.data.bones))
    meshes = [o for o in bpy.data.objects if o.type == "MESH" and any(m.type == "ARMATURE" and m.object == arm for m in o.modifiers)]
    if not meshes: raise SystemExit("ERROR: 骨に付いたメッシュがない")
    for o in list(bpy.data.objects):   # 使わないもの（ほかのメッシュ・空のオブジェクト）を消す
        if o not in meshes and o != arm and o.type in ("MESH", "EMPTY") and o.parent != arm: bpy.data.objects.remove(o, do_unlink=True)
    for a in list(bpy.data.actions): bpy.data.actions.remove(a)   # 動き（アニメーション）は使わない
    if arm.animation_data: arm.animation_data_clear()
    for pb in arm.pose.bones: pb.matrix_basis = Matrix.Identity(4)
    # メッシュを 1 つに
    bpy.ops.object.select_all(action="DESELECT")
    for o in meshes: o.select_set(True)
    bpy.context.view_layer.objects.active = meshes[0]
    if len(meshes) > 1: bpy.ops.object.join()
    body = bpy.context.view_layer.objects.active
    body.name = "Body"
    # 骨の名前を決まった名前に
    names = {b.name.lower(): b.name for b in arm.data.bones}
    found = {}
    for canon, cands in BONE_ALIASES.items():
        for c in cands:
            if c.lower() in names: found[canon] = names[c.lower()]; break
    missing = [k for k in BONE_ALIASES if k not in found]
    if missing: raise SystemExit("ERROR: 見つからない骨：%s（BONE_ALIASES に元の名前を足す）" % missing)
    for canon, orig in found.items():
        vg = body.vertex_groups.get(orig)
        arm.data.bones[orig].name = canon   # 頂点グループも一緒に名前が変わる（Blender が付け替える）
        if vg and vg.name != canon: vg.name = canon
    # 指など：重みを、決まった名前の一番近い親に足して、骨を消す
    keep = set(BONE_ALIASES)
    root_names = [b.name for b in arm.data.bones if b.parent is None]
    def mapped_parent(b):
        p = b.parent
        while p and p.name not in keep: p = p.parent
        return p.name if p else None
    extra = [b.name for b in arm.data.bones if b.name not in keep and b.name not in root_names]
    for en in extra:
        tgt = mapped_parent(arm.data.bones[en])
        g = body.vertex_groups.get(en)
        if g and tgt:
            tg = body.vertex_groups.get(tgt) or body.vertex_groups.new(name=tgt)
            for v in body.data.vertices:
                for ge in v.groups:
                    if ge.group == g.index and ge.weight > 0: tg.add([v.index], ge.weight, "ADD")
        if g: body.vertex_groups.remove(g)
    bpy.context.view_layer.objects.active = arm
    bpy.ops.object.mode_set(mode="EDIT")
    for en in extra:
        eb = arm.data.edit_bones.get(en)
        if eb: arm.data.edit_bones.remove(eb)
    bpy.ops.object.mode_set(mode="OBJECT")
    return arm, body, found

def bone_ws(arm, name):   # 骨の頭・しっぽ（世界）
    b = arm.data.bones[name]
    return arm.matrix_world @ b.head_local, arm.matrix_world @ b.tail_local

def vgroup_weights(body):
    """頂点ごとの {骨の名前: 重み}"""
    gn = {g.index: g.name for g in body.vertex_groups}
    return [{gn[g.group]: g.weight for g in v.groups if g.group in gn and g.weight > 0} for v in body.data.vertices]

def seg_t(p, a, b):
    ab = b - a; L2 = max(ab.length_squared, 1e-9)
    t = max(0.0, min(1.0, (p - a).dot(ab) / L2))
    return t, a + ab * t

# ---- 体型：頂点を動かす ----
def apply_body(arm, body, prm):
    sh, bulk, armk, leg, waist = prm
    W = vgroup_weights(body)
    mw = body.matrix_world; mi = mw.inverted()
    B = {n: bone_ws(arm, n) for n in BONE_ALIASES}
    side = lambda n: 1.0 if n.endswith(".L") else (-1.0 if n.endswith(".R") else 0.0)
    sh_z = (B["upperarm.L"][0].z + B["upperarm.R"][0].z) * 0.5   # 肩の高さ
    neck_r = NECK_W_BASE * 0.5   # 首の付け根の半分の幅より外（肩寄り）だけ低くする
    nb_z, nb_x, nb_y = B["neck"][0].z, B["neck"][0].x, B["neck"][0].y   # 首の付け根
    for v, w in zip(body.data.vertices, W):
        p = mw @ v.co
        d = Vector((0, 0, 0))
        for n, wt in w.items():
            if n not in B: continue
            a, b = B[n]
            t, c = seg_t(p, a, b)
            r = p - c
            if n.startswith(("upperarm", "forearm")): k = (armk * SLIM_ARM, armk * SLIM_ARM, armk * SLIM_ARM)
            elif n.startswith("hand"): k = (1.0, 1.0, 1.0)
            elif n.startswith(("thigh", "shin")): k = (leg * SLIM_LEG, leg * SLIM_LEG, leg * SLIM_LEG)
            elif n in ("spine3",) or n.startswith("clavicle"): k = (sh * SLIM_TORSO_W, bulk * SLIM_TORSO_D, 1.0)
            elif n in ("spine1", "spine2"): k = (lerp(waist, sh, 0.5) * SLIM_TORSO_W, bulk * SLIM_TORSO_D, 1.0)
            elif n == "hips": k = (waist * 0.94, bulk * 0.94, 1.0)
            else: k = (1.0, 1.0, 1.0)
            d += wt * Vector((r.x * (k[0] - 1), r.y * (k[1] - 1), r.z * (k[2] - 1)))
            # 肩幅：腕と鎖骨の外側を横へ
            sd = side(n)
            if sd and n.startswith(("clavicle", "upperarm", "forearm", "hand")):
                f = 0.5 if n.startswith("clavicle") else 1.0
                d.x += wt * sd * (sh - 1.0) * SHOULDER_DX * f
        p = p + d
        # 僧帽筋：肩の高さより上の、首のまわり（胸の上・鎖骨）の盛り上がりを低く
        # 首の付け根より上に出た胸・鎖骨（僧帽筋）を、付け根の下へ。首かどうかは重みでなく、首の軸からの距離で決める（首の円柱そのものは動かさない）
        tw = sum(w.get(n2, 0.0) for n2 in ("spine3", "clavicle.R", "clavicle.L", "neck"))
        dax = math.hypot(p.x - nb_x, (p.y - nb_y) * 1.2)   # 首の軸からの水平の距離（前後は少し長めに数える）
        if tw > 0.3 and w.get("head", 0.0) < 0.5 and p.z > nb_z - 0.012 and dax > neck_r:
            k2 = smooth((dax - neck_r) / 0.03)
            z0 = nb_z - 0.012
            p.z = z0 + (p.z - z0) * lerp(1.0, TRAP_K, k2)
        v.co = mi @ p
    # 骨も同じだけ動かす（腕を横へ）
    bpy.context.view_layer.objects.active = arm
    bpy.ops.object.mode_set(mode="EDIT")
    for eb in arm.data.edit_bones:
        sd = side(eb.name)
        if sd and eb.name.startswith(("upperarm", "forearm", "hand")):
            dx = sd * (sh - 1.0) * SHOULDER_DX
            eb.head.x += dx; eb.tail.x += dx
        elif sd and eb.name.startswith("clavicle"):
            eb.tail.x += sd * (sh - 1.0) * SHOULDER_DX
    bpy.ops.object.mode_set(mode="OBJECT")

def apply_head_scale(arm, body, k):
    """頭の大きさ（k）と形：横幅を細く（HEAD_W）、頬から下を細く（JAW_NARROW）、頬の前後を少し薄く"""
    W = vgroup_weights(body)
    mw = body.matrix_world; mi = mw.inverted()
    hb, ht = bone_ws(arm, "head")
    pts = [mw @ v.co for v, w in zip(body.data.vertices, W) if w.get("head", 0) > 0.6]
    c = sum(pts, Vector((0, 0, 0))) / max(len(pts), 1)
    zs = sorted(p.z for p in pts)
    zlo, zhi = zs[0], zs[-1]
    for v, w in zip(body.data.vertices, W):
        wt = w.get("head", 0.0)
        if wt <= 0: continue
        p = mw @ v.co
        q = p + (p - hb) * (k - 1.0)
        u = (p.z - c.z) / max(zhi - c.z, 1e-3)              # 頭の中心から上 +、下 -
        xk = HEAD_W * (lerp(1.0, JAW_NARROW, smooth((-u - 0.05) / 0.75)) if u < -0.05 else 1.0)
        yk = HEAD_BACK_K if (p.y - c.y) > 0 else 1.0   # 後ろ（+Y）だけ短く。顔（前）は平らにしない
        cc = c + (c - hb) * (k - 1.0)
        q = Vector((cc.x + (q.x - cc.x) * xk, cc.y + (q.y - cc.y) * yk, q.z))
        v.co = mi @ (p + (q - p) * wt)

# ---- 首：首の骨の軸に中心をそろえた楕円柱にする（上端が一番細く、付け根へ少し広がる）----
#   元のマネキンの首は頭より太く、中心が骨の軸より 4cm 後ろにあった（顔が前に突き出て見えた）
def reshape_neck(arm, body):
    W = vgroup_weights(body)
    mw = body.matrix_world; mi = mw.inverted()
    A, Bt = bone_ws(arm, "neck")               # 首の骨（付け根 → 頭の付け根）
    ax = (Bt - A); L = ax.length; ax.normalize()
    side = Vector((1, 0, 0)); side = (side - ax * side.dot(ax)).normalized()
    dep = ax.cross(side).normalized()          # 前後（Blender の y 寄り）
    items = []
    for i, (v, w) in enumerate(zip(body.data.vertices, W)):
        wn = w.get("neck", 0.0)
        if wn <= 0.0: continue
        p = mw @ v.co
        t = max(0.0, min(1.0, (p - A).dot(ax) / L))
        c = A + ax * (t * L)
        r = p - c
        items.append((i, wn, t, r.dot(side), r.dot(dep), c))
    # 高さごとの今の太さ（首の骨が一番重い頂点だけで測る）
    bins = 6
    mx = [1e-3] * bins; md = [1e-3] * bins
    for i, wn, t, rx, rd, c in items:
        if max(W[i], key=W[i].get) != "neck": continue
        k = min(int(t * bins), bins - 1)
        mx[k] = max(mx[k], abs(rx)); md[k] = max(md[k], abs(rd))
    for k in range(1, bins):   # 測れなかった高さは下の値
        if mx[k] <= 1e-3: mx[k] = mx[k - 1]; md[k] = md[k - 1]
    for i, wn, t, rx, rd, c in items:
        k = min(int(t * bins), bins - 1)
        if abs(rx) > mx[k] * 1.15 or abs(rd) > md[k] * 1.15: continue   # 首の外（肩・胸）は動かさない
        hw = lerp(NECK_W_BASE, NECK_W_TOP, t) * 0.5
        sx = min(hw / mx[k], 1.0); sd = min(hw * NECK_DEPTH_K / md[k], 1.0)   # 細くするだけ
        tgt = c + side * (rx * sx) + dep * (rd * sd)
        p = mw @ body.data.vertices[i].co
        body.data.vertices[i].co = mi @ (p + (tgt - p) * min(wn * 1.6, 1.0))

# ---- 服の境目に、まっすぐな切れ目を入れる（袖口・パンツの裾・靴下・リストバンド・腰）----
#   元のメッシュは境目の所に辺がないので、そのまま面ごとに塗るとギザギザになる。骨に直角な面で、その骨の部分だけを切る
def cut_lines(arm, body):
    W = vgroup_weights(body)
    mw = body.matrix_world; mi = mw.inverted()
    B = {n: bone_ws(arm, n) for n in BONE_ALIASES}
    cuts = []
    for sd in (".R", ".L"):
        cuts += [("upperarm" + sd, SLEEVE_LEN), ("forearm" + sd, WRIST_BAND[0]), ("forearm" + sd, WRIST_BAND[1]),
                 ("thigh" + sd, SHORTS_LEN), ("shin" + sd, 1.0 - SOCK_LEN)]
    bm = bmesh.new(); bm.from_mesh(body.data)
    dl = bm.verts.layers.deform.active
    gi = {g.name: g.index for g in body.vertex_groups}
    def bone_w(v, n): return v[dl].get(gi[n], 0.0) if dl and n in gi else 0.0
    for n, t in cuts:
        a, b = B[n]
        co = mi @ (a + (b - a) * t); no = (mi.to_3x3() @ (b - a)).normalized()
        faces = [f for f in bm.faces if sum(bone_w(v, n) for v in f.verts) / len(f.verts) > 0.25]
        if not faces: continue
        geom = list({e for f in faces for e in f.edges}) + faces + list({v for f in faces for v in f.verts})
        bmesh.ops.bisect_plane(bm, geom=geom, dist=1e-5, plane_co=co, plane_no=no)
    # 襟：首の骨に直角な面で、首と胸の上の部分を切る（襟の線と、その少し下の差し色の線）
    na, nb2 = B["neck"]
    nd = (nb2 - na).normalized()
    for off in (0.0, -COLLAR_BAND):
        co = na + (nb2 - na) * COLLAR_T + nd * off
        faces = [f for f in bm.faces if sum(bone_w(v, "neck") + bone_w(v, "spine3") for v in f.verts) / len(f.verts) > 0.25 and
                 abs((mw @ f.calc_center_median() - co).dot(nd)) < 0.08]
        if faces:
            geom = list({e for f in faces for e in f.edges}) + faces + list({v for f in faces for v in f.verts})
            bmesh.ops.bisect_plane(bm, geom=geom, dist=1e-5, plane_co=mi @ co, plane_no=(mi.to_3x3() @ nd).normalized())
    # 腰：シャツとパンツの境（水平）
    ha, hb = B["hips"]
    zc = lerp(ha.z, hb.z, WAIST_K)
    faces = [f for f in bm.faces if sum(bone_w(v, "hips") + bone_w(v, "spine1") for v in f.verts) / len(f.verts) > 0.25]
    if faces:
        geom = list({e for f in faces for e in f.edges}) + faces + list({v for f in faces for v in f.verts})
        bmesh.ops.bisect_plane(bm, geom=geom, dist=1e-5, plane_co=mi @ Vector((0, 0, zc)), plane_no=(mi.to_3x3() @ Vector((0, 0, 1))).normalized())
    bm.to_mesh(body.data); bm.free()

# ---- 服の色分け：面ごとにパレットのマス ----
def paint(arm, body):
    W = vgroup_weights(body)
    mw = body.matrix_world
    B = {n: bone_ws(arm, n) for n in BONE_ALIASES}
    me = body.data
    uv = me.uv_layers.active or me.uv_layers.new(name="UVMap")
    # ほかの UV は消す（パレットだけ使う）
    for l in list(me.uv_layers):
        if l != uv: me.uv_layers.remove(l)
    hips_a, hips_b = B["hips"]
    waist_z = lerp(hips_a.z, hips_b.z, WAIST_K)
    na, nb2 = B["neck"]; nd = (nb2 - na).normalized(); collar = na + (nb2 - na) * COLLAR_T
    for poly in me.polygons:
        acc = {}
        for vi in poly.vertices:
            for n, wt in W[vi].items(): acc[n] = acc.get(n, 0.0) + wt
        if not acc: cell = "skin"
        else:
            n = max(acc, key=acc.get)
            c = mw @ poly.center
            nrm = (mw.to_3x3() @ poly.normal).normalized()
            t = seg_t(c, *B[n])[0] if n in B else 0.0
            above = (c - collar).dot(nd)   # 襟の面より上（プラス）か
            if n in ("neck", "spine3", "head") or n.startswith("clavicle"):
                if n != "head" and above < -COLLAR_BAND: cell = "shirt"
                elif n != "head" and above < 0.0: cell = "accent"
                elif n == "head": cell = "skin"
                else: cell = "skin"
            elif n in ("hips", "spine1") and c.z < waist_z: cell = "pants"
            elif n in ("spine1", "spine2", "spine3") or n.startswith("clavicle"): cell = "shirt"
            elif n == "hips": cell = "shirt" if c.z > waist_z else "pants"
            elif n.startswith("upperarm"): cell = "shirt" if t < SLEEVE_LEN else "skin"
            elif n.startswith("forearm"): cell = "wrist" if WRIST_BAND[0] < t < WRIST_BAND[1] else "skin"
            elif n.startswith("thigh"): cell = "pants" if t < SHORTS_LEN else "skin"
            elif n.startswith("shin"): cell = "socks" if t > 1.0 - SOCK_LEN else "skin"
            elif n.startswith(("foot", "toe")): cell = "sole" if nrm.z < -0.6 else "shoes"
            else: cell = "skin"
        u = pal_uv(cell)
        for li in poly.loop_indices: uv.data[li].uv = u
    for poly in me.polygons: poly.use_smooth = True

# ---- 頭：形に沿った面（髪・顔の絵・鼻）を作る ----
import make_player as MP   # 髪の毛束・顔の絵の作り方を流用

def head_frame(arm, body):
    """頭の中心と半径（世界）、前（-Y）・上（+Z）"""
    W = vgroup_weights(body); mw = body.matrix_world
    pts = [mw @ v.co for v, w in zip(body.data.vertices, W) if w.get("head", 0) > 0.6]
    c = sum(pts, Vector((0, 0, 0))) / max(len(pts), 1)
    hb, ht = bone_ws(arm, "head")
    up = (ht - hb).normalized()
    fwd = Vector((0, -1, 0)); fwd = (fwd - up * fwd.dot(up)).normalized()
    right = fwd.cross(up).normalized()   # ゲームの x（選手の右）
    bm = bmesh.new(); bm.from_mesh(body.data); bm.transform(mw)
    bm.verts.ensure_lookup_table()
    hw = [w.get("head", 0.0) + w.get("neck", 0.0) * 0.0 for w in W]
    bmesh.ops.delete(bm, geom=[f for f in bm.faces if sum(hw[v.index] for v in f.verts) / len(f.verts) < 0.3], context="FACES")   # 頭の面だけに当てる
    bvh = BVHTree.FromBMesh(bm)
    def radius(d):
        hit = bvh.ray_cast(c + d * 0.5, -d, 0.6)   # 外から中心へ向かって当てる（いちばん外の面）
        return (hit[0] - c).length if hit[0] is not None else 0.1
    R = radius(right)
    return c, R, right, up, fwd, radius

def to_world(c, right, up, fwd, v):   # 頭の座標（x 右・y 上・z 前）→ 世界
    return c + right * v.x + up * v.y + fwd * v.z

def build_head_extras(arm, body, hair_style):
    c, R, right, up, fwd, radius = head_frame(arm, body)
    # make_player の頭の形の関数を、実際の頭の表面（光線で測った半径）に置き換える
    def head_shape(d, rho=1.0):
        dw = (right * d.x + up * d.y + fwd * d.z).normalized()
        r = radius(dw)
        p = dw * r * rho
        return Vector((p.dot(right), MP.HC_Y + p.dot(up), p.dot(fwd)))
    MP.R_HEAD = R; MP.head_shape = head_shape
    objs = []
    hc = c - up * MP.HC_Y
    def make_obj(name, b, mat):
        me = bpy.data.meshes.new(name)
        me.from_pydata([to_world(hc, right, up, fwd, v) for v in b.v], [], [f for f, _ in b.f])
        me.materials.append(mat)
        uvl = me.uv_layers.new(name="UVMap")
        for fi, (poly, (_, cell)) in enumerate(zip(me.polygons, b.f)):
            uvs = b.uv.get(fi)
            for k, li in enumerate(poly.loop_indices): uvl.data[li].uv = uvs[k] if uvs else pal_uv(cell)
            poly.use_smooth = True
        ob = bpy.data.objects.new(name, me); bpy.context.scene.collection.objects.link(ob)
        # 法線を外へ（頭の中心から）
        bm = bmesh.new(); bm.from_mesh(me)
        for f in bm.faces:
            if f.normal.dot(f.calc_center_median() - c) < 0: f.normal_flip()
        bm.to_mesh(me); bm.free()
        g = ob.vertex_groups.new(name="head"); g.add(list(range(len(me.vertices))), 1.0, "REPLACE")
        mod = ob.modifiers.new("Armature", "ARMATURE"); mod.object = arm
        ob.parent = arm
        objs.append(ob)
        return ob
    # 顔の絵を貼る面
    bf = MP.Builder(); MP.build_face(bf); make_obj("Face", bf, MAT_FACE)
    # 鼻（頭の形の作り方から鼻だけ）
    bn = MP.Builder(); _nose(bn); make_obj("Nose", bn, MAT_PAL)
    # 髪
    if hair_style:
        bh = MP.Builder(); MP.build_hair(bh, hair_style); make_obj("Hair", bh, MAT_PAL)
    return objs

def _nose(b):
    yt, yb = 0.24, -0.40
    fz = lambda u: MP.head_shape(Vector((0, u, math.sqrt(max(1 - u * u, 0))))).z
    R = MP.R_HEAD
    zt, zb = fz(yt) - 0.003, fz(yb) - 0.003
    w0, w1, h0, h1 = 0.05 * R, 0.14 * R, 0.10 * R, 0.42 * R   # 鼻筋：横顔で鼻が見えるように前へ出す
    Y = lambda u: MP.HC_Y + u * R
    tl, tr, tc = b.add((-w0, Y(yt), zt)), b.add((w0, Y(yt), zt)), b.add((0, Y(yt), zt + h0))
    bl, br, bc = b.add((-w1, Y(yb), zb)), b.add((w1, Y(yb), zb)), b.add((0, Y(yb) - 0.03 * R, zb + h1))
    b.face([tl, tc, bc, bl], "nose"); b.face([tc, tr, br, bc], "nose"); b.face([bl, bc, br], "nose"); b.face([tl, tr, tc], "skin")

def scalp_hair_color(body, arm, hair_style):
    """頭の皮膚の生え際から上を髪の色に（ベースの髪の下。すき間から肌が見えないように）"""
    if not hair_style: return
    c, R, right, up, fwd, radius = head_frame(arm, body)
    W = vgroup_weights(body); mw = body.matrix_world
    uv = body.data.uv_layers.active
    for poly in body.data.polygons:
        if sum(W[vi].get("head", 0) for vi in poly.vertices) / len(poly.vertices) < 0.5: continue
        d = (mw @ poly.center - c).normalized()
        dl = Vector((d.dot(right), d.dot(up), d.dot(fwd)))
        if MP.hairline(dl, hair_style) > 0.06:
            for li in poly.loop_indices: uv.data[li].uv = pal_uv("hair")

def materials():
    global MAT_PAL, MAT_FACE
    pal = bpy.data.images.load(os.path.join(ROOT, "models", "palette.png")); pal.pack()
    face = bpy.data.images.load(os.path.join(ROOT, "models", "faces", "face_01.png")); face.pack()
    MAT_PAL = MP.make_mat("palette", pal) if hasattr(MP, "make_mat") else None
    MAT_FACE = MP.make_mat("face", face, blend=True)

HAIR_NAME = {"short": "hair_short", "wave": "hair_wave", "tied": "hair_tied", "buzz": "hair_buzz"}
def hair_default(i):
    return "hair_tied" if i in WOMEN else "hair_short"

def export(arm, path):
    bpy.ops.object.select_all(action="DESELECT")
    arm.select_set(True)
    for o in bpy.data.objects:
        if o.type == "MESH" and (o.parent == arm or any(m.type == "ARMATURE" and m.object == arm for m in o.modifiers)): o.select_set(True)
    bpy.ops.export_scene.gltf(filepath=path, export_format="GLB", use_selection=True, export_apply=False, export_yup=True,
                              export_texcoords=True, export_normals=True, export_skins=True, export_animations=False,
                              export_morph=False, export_materials="EXPORT", export_def_bones=False)

if __name__ == "__main__":
    os.makedirs(os.path.join(ROOT, "models", "players"), exist_ok=True)
    body_tab = read_body_table(); hair_tab = read_hair_table()
    only = os.environ.get("ONLY")   # 確認用：一部の選手だけ（例 ONLY=0,2,6）
    ids = [int(x) for x in only.split(",")] if only else list(range(len(body_tab)))
    found = None
    for i in ids:
        arm, body, found = load_base()
        materials()
        hs = HAIR_NAME.get(hair_tab.get(i, ""), hair_default(i))
        apply_body(arm, body, body_tab[i])
        apply_head_scale(arm, body, HEAD_SCALE)
        reshape_neck(arm, body)
        cut_lines(arm, body)
        paint(arm, body)
        body.data.materials.clear(); body.data.materials.append(MAT_PAL)
        for p in body.data.polygons: p.material_index = 0
        scalp_hair_color(body, arm, hs)
        build_head_extras(arm, body, hs)
        out = os.path.join(ROOT, "models", "players", "p%02d.glb" % i)
        export(arm, out)
        print("wrote", out, "hair", hs, "verts", len(body.data.vertices))
    with open(os.path.join(ROOT, "models", "players", "bones.txt"), "w", encoding="utf-8") as f:
        f.write("# 骨の対応表：決まった名前（ゲームが見る名前） ← 元のモデルの骨の名前（%s）\n" % os.path.basename(BASE))
        for k in BONE_ALIASES: f.write("%-12s <- %s\n" % (k, found.get(k, "?") if found else "?"))
