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
HEAD_SCALE   = 0.90   # 頭の大きさ（元のモデルの頭に対する倍率）。実測から：頭の高さ＝身長の約 1/7.8（1/7.5 ±10% の中）になる値
BODY_DIFF_K  = 0.6    # 選手ごとの体型の差（data.c の値の差に掛ける。1.6 だと筋肉が付きすぎた）
# 元のマネキンは胸・肩・僧帽筋が大きく、首と顔が太い。テニス選手らしく全体を細くする
SLIM_TORSO_W = 0.86   # 胴の横幅
SLIM_TORSO_D = 0.86   # 胴の厚み
SLIM_ARM     = 0.80   # 腕の太さ
SLIM_LEG     = 0.92   # 脚の太さ
# ---- 頭・顔・首・肩の比率 ----
#   縦は「頭の高さ h ＝ 頭頂（髪を含む）〜あご先」を 1 とした、頭頂からの位置。横は「目の幅 E ＝ 頬骨の幅 / 5」の単位。
#   目 0.50・三等分（生え際→眉＝眉→鼻の下＝鼻の下→あご先）・頭頂→生え際 0.30 は同時には満たせない（0.30 だと眉が 0.53 で目より下になる）。
#   どれも目標の ±10% に入る値にしている：生え際 0.28、眉 0.495、目 0.535、鼻の下 0.7475、口は鼻の下→あご先の上から 1/3
HAIRLINE_F   = 0.28   # 前の生え際
TEMPLE_HAIR_F = 0.40  # こめかみ（前から 60°）の生え際の高さ。眉より上を肌にして、こめかみの幅を顔の形で出す
BROW_F       = 0.495  # 眉（＝耳の上端）
EYE_F        = 0.535  # 目の高さ
NOSE_F       = 0.7475 # 鼻の下（＝耳の下端）
MOUTH_F      = NOSE_F + (1.0 - NOSE_F) / 3 - 0.006   # 口（鼻の下→あご先の上から 1/3。口の線は画面で少し下に出るので 0.006 上に）
CHEEK_F      = 0.64   # 頬骨の幅が一番広い高さ（目と鼻の下の間）
HEAD_WH      = 0.775  # 頭の最大幅（髪を含む）/ 頭の高さ（0.75〜0.80）
HEADW_CHEEK  = 1.075  # 頭の最大幅 / 頬骨の幅（1.05〜1.10）
TEMPLE_K     = 0.90   # こめかみの幅（眉の高さ）/ 頬骨の幅
JAW_K        = 0.725  # あごの幅（口の高さ）/ 頬骨の幅（0.70〜0.75）
JAW_CAL      = 0.95   # あごの幅の目標に掛ける補正（口の高さの輪郭は、上の頬の面の傾きで画面では約 5% 広く出る：実測）
CHIN_W       = (0.48,)  # あご先の手前（頭の高さの 0.93）の幅 / 頬骨の幅（口の高さから、あご先へ細く）
HEAD_DEPTH   = 0.975  # 横から見た奥行き（鼻先→後頭部。髪を含む）/ 頭の高さ（0.95〜1.00）
NOSE_PROJ    = 0.085  # 鼻先が顔の面から前に出る量（頭の高さに対して）
CHIN_BACK    = 0.0    # あご先が唇（口の高さの顔の前の面）より後ろに下がる量（頭の高さに対して。0 でも、あごの丸みで画面では少し後ろになる）
JAW_DEG      = 27.5   # あごの下の線：あご先から後ろへ上がる角度（下顎の底辺。水平から 25〜30°）
EAR_POS      = 0.575  # 耳の真ん中：鼻先→後頭部のうち、鼻先からの割合（45:55 の範囲で少し後ろ。耳の下→あご先の線を 25〜30° にするため）
HAIR_TOP     = {"hair_short": 0.017, "hair_wave": 0.031, "hair_tied": 0.012, "hair_buzz": 0.005, "": 0.0}   # 頭頂の上の髪の厚み（m。髪を作って測った値）
HAIR_SIDE    = {"hair_short": 0.008, "hair_wave": 0.025, "hair_tied": 0.060, "hair_buzz": 0.004, "": 0.0}   # 横の髪の厚み（頭の高さに対して。頭の最大幅の所。実測）。頭の形の目標から引く
HAIR_BACK    = {"hair_short": 0.055, "hair_wave": 0.13, "hair_tied": 0.055, "hair_buzz": 0.015, "": 0.0}    # 後ろの髪の厚み（頭の高さに対して。実測）
CHIN_CORNER  = 0.98   # 画面で「あご先」と見える角（前の面が引っ込み始める所）は、メッシュの前下の点より頭の高さの 2% 上（実測）。顔の部品はこの角を基準に置く
NECK_W       = 0.62   # 首の左右の幅（真ん中）/ 頭の最大幅（0.60 ±10%）。あごの幅（頭の最大幅の約 0.67）とほぼ同じにするため少し太め
NECK_TAPER   = (0.94, 1.08)   # 首の幅の、上端（あごの下）・付け根の倍率（真ん中に対して。上が細く、付け根が少し太い）
NECK_DEPTH_K = 0.90   # 首の前後の厚み / 左右の幅（作るときの値。ゲームの画面では約 0.89 倍に見えるので、画面で 0.80 になる値）
SHOULDER_DEG = 16.5   # 肩線：首の付け根から肩先へ下がる角度（作るときの T ポーズで。ゲームの画面では選手と構えで ±3° ほど変わる：実測で 15〜20° の真ん中に来る値）
SHOULDER_TIP = 0.030  # 肩先の上の面の高さ（腕の付け根の関節から上へ、m）
SHOULDER_DX  = 0.20   # 肩幅の倍率 1 につき、腕を横へ動かす量の基準（m）
SLEEVE_LEN   = 0.42   # 袖の長さ（上腕の長さに対する割合）
SHORTS_LEN   = 0.50   # パンツの裾（腿の長さに対する割合）
SOCK_LEN     = 0.24   # 靴下の高さ（すねの長さに対する割合）
WRIST_BAND   = (0.74, 0.90)   # リストバンド（前腕の長さに対する割合の範囲）
WAIST_K      = 0.55   # シャツとパンツの境（腰の骨の長さに対する割合）
COLLAR_T     = 0.45   # 襟：首の骨の付け根からこの割合まではシャツ（首の付け根より少し上まで。首の肌の見える所を少なめに）
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
PAL = {"skin": (0, 0), "shirt": (1, 0), "accent": (2, 0), "pants": (3, 0), "shoes": (4, 0), "socks": (5, 0), "hair": (6, 0), "wrist": (7, 0), "eye": (8, 0), "sole": (9, 0), "nose": (10, 0), "ear": (11, 0)}
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
    # 同じ位置に重なった頂点（読み込みで UV の継ぎ目ごとに分かれたもの）を 1 つに（形を動かしたときに裂けないように）
    bm = bmesh.new(); bm.from_mesh(body.data)
    bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=1e-6)
    bm.to_mesh(body.data); bm.free()
    # 元のモデルの法線（読み込んだときの決まった法線）を消す。残すと、形を変えても元の筋肉の凹凸の明暗（首の縦の筋など）が出る。法線は形から計算し直す
    bpy.context.view_layer.objects.active = body
    if body.data.has_custom_normals: bpy.ops.mesh.customdata_custom_splitnormals_clear()
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

def subdivide_head(body):
    """頭の面（頭の骨の重みが 0.25 以上）を細かく分ける（元の頭は粗く、こめかみ・あごの細さが出ないため）。重みは分けた辺の両端から補間される"""
    W = vgroup_weights(body)
    hw = [w.get("head", 0.0) for w in W]
    bm = bmesh.new(); bm.from_mesh(body.data); bm.verts.ensure_lookup_table()
    edges = list({e for f in bm.faces if sum(hw[v.index] for v in f.verts) / len(f.verts) >= 0.25 for e in f.edges})
    n0 = len(bm.verts)
    bmesh.ops.subdivide_edges(bm, edges=edges, cuts=1, use_grid_fill=True)
    bm.to_mesh(body.data); bm.free()
    print("head: subdivided, verts %d → %d" % (n0, len(body.data.vertices)))

def apply_head_scale(arm, body, k):
    """頭の大きさ（k）：頭の付け根を中心に、頭の骨の重みの分だけ大きさを変える（形は shape_head で）"""
    W = vgroup_weights(body)
    mw = body.matrix_world; mi = mw.inverted()
    hb, ht = bone_ws(arm, "head")
    for v, w in zip(body.data.vertices, W):
        wt = w.get("head", 0.0)
        if wt <= 0: continue
        p = mw @ v.co
        v.co = mi @ (p + (p - hb) * (k - 1.0) * wt)

def _dom(w): return max(w, key=w.get) if w else ""

def head_landmarks(arm, body, hair_style=""):
    """頭の目印（世界）：頭のてっぺん（髪の厚みを足す）・あご先・頭の中心・頭の高さ h"""
    W = vgroup_weights(body); mw = body.matrix_world
    hp = [mw @ v.co for v, w in zip(body.data.vertices, W) if _dom(w) == "head"]
    c = sum(hp, Vector((0, 0, 0))) / len(hp)
    miny = min(p.y for p in hp)
    front = [p for p in hp if p.y < c.y - 0.4 * (c.y - miny)]
    # あご先：顔の前の部分で、前下（前 2：下 1）の向きに一番出ている点（左右の真ん中に近いもの）。あごの下（首との境）は拾わない
    xw = max(abs(p.x - c.x) for p in hp)
    chin = max((p for p in front if abs(p.x - c.x) < 0.35 * xw), key=lambda p: -2.0 * p.y - p.z)
    skull = max(p.z for p in hp)
    top = skull + HAIR_TOP.get(hair_style, 0.0)
    h = top - chin.z
    return dict(c=c, chin_z=chin.z, chin_y=chin.y, skull=skull, top=top, h=h)

class HeadRays:
    """頭の面（頭の骨の重みが 0.3 以上の面）に光線を当てて測る（頭のメッシュは粗いので、頂点ではなく面で）"""
    def __init__(self, body):
        W = vgroup_weights(body); mw = body.matrix_world
        hw = [w.get("head", 0.0) for w in W]
        bm = bmesh.new(); bm.from_mesh(body.data); bm.transform(mw)
        bm.verts.ensure_lookup_table()
        bmesh.ops.delete(bm, geom=[f for f in bm.faces if sum(hw[v.index] for v in f.verts) / len(f.verts) < 0.3], context="FACES")
        self.ys = [v.co.y for v in bm.verts]; self.bvh = BVHTree.FromBMesh(bm)
        self.edges = [(e.verts[0].co.copy(), e.verts[1].co.copy()) for e in bm.edges]; bm.free()
    def half_width(self, z, cx):   # 高さ z の、正面から見た輪郭の半分の幅（面を高さ z で切った線の左右の端）
        best = 0.0
        for a, b in self.edges:
            if (a.z - z) * (b.z - z) <= 0 and a.z != b.z:
                t = (z - a.z) / (b.z - a.z); best = max(best, abs(a.x + (b.x - a.x) * t - cx))
        return best
    def front(self, z, x=0.0):   # 高さ z・横 x の、顔の前の面の y（前が -y）
        hit = self.bvh.ray_cast(Vector((x, -0.5, z)), Vector((0, 1, 0)), 1.0)
        return hit[0].y if hit[0] is not None else None
    def back(self, z, x=0.0):
        hit = self.bvh.ray_cast(Vector((x, 0.5, z)), Vector((0, -1, 0)), 1.0)
        return hit[0].y if hit[0] is not None else None

def shape_head(arm, body, hair_style):
    """頭の形を比率の目標に合わせる（頭の骨の重みの分だけ動かす）：
       ① 高さごとの横幅（頭の最大幅・こめかみ・頬骨・あご）② 奥行き（鼻先→後頭部）③ あご先を唇の少し後ろへ ④ あごの下を後ろへ上がる線に"""
    W = vgroup_weights(body); mw = body.matrix_world; mi = mw.inverted()
    L = head_landmarks(arm, body, hair_style); h, top, c = L["h"], L["top"], L["c"]
    f_of = lambda z: (top - z) / h
    z_of = lambda f: top - f * h
    zf = lambda f: top - f * h * CHIN_CORNER   # 顔の部品の高さ（画面のあご先の角を 1 とした割合）→ z
    def heads(): return [(i, mw @ v.co, w) for i, (v, w) in enumerate(zip(body.data.vertices, W)) if w.get("head", 0.0) > 0]
    # ① 横幅：目標（画面の輪郭の半分の幅。髪の所は髪の厚みを引いた頭の形）÷ 今の幅 ＝ 高さごとの倍率
    HR = HeadRays(body)
    cheek = HEAD_WH / HEADW_CHEEK
    cur = lambda f: HR.half_width(zf(f), c.x)
    fs = [0.14 + 0.02 * i for i in range(14)]
    fmax = max(fs, key=cur)
    ctrl = [(fmax, (HEAD_WH / 2 - HAIR_SIDE.get(hair_style, 0.02)) * h), (BROW_F, TEMPLE_K * cheek / 2 * h), (CHEEK_F, cheek / 2 * h), (MOUTH_F, JAW_K * JAW_CAL * cheek / 2 * h),
            (0.93, CHIN_W[0] * cheek / 2 * h)]
    sc = [(f * CHIN_CORNER, t * CHIN_CORNER / cur(f)) for f, t in ctrl]   # 目標の幅も画面の頭の高さ（あご先の角まで）に対して
    def s_at(f):
        if f <= sc[0][0]: return sc[0][1]
        for (f0, s0), (f1, s1) in zip(sc, sc[1:]):
            if f <= f1: return lerp(s0, s1, (f - f0) / (f1 - f0))
        return sc[-1][1]
    print("head: h %.4f  top %.4f  chin z %.4f y %.4f  skull max at f %.2f  width scale %s" % (h, top, L["chin_z"], L["chin_y"], fmax, " ".join("%.2f@%.3f" % (s0, f0) for f0, s0 in sc)))
    for i, p, w in heads():
        wt = w["head"]; s = s_at(f_of(p.z))
        body.data.vertices[i].co = mi @ Vector((c.x + (p.x - c.x) * (1 + (s - 1) * wt), p.y, p.z))
    # ② 奥行き：顔の前の面（鼻の下の高さ）〜後頭部 ＝ 目標の奥行き − 鼻の出 − 後ろの髪
    HR = HeadRays(body)
    front_n = HR.front(zf(NOSE_F), c.x)
    back = max(HR.back(z_of(f), c.x) or -9 for f in (0.2, 0.3, 0.4, 0.5, 0.6))
    need = (HEAD_DEPTH - NOSE_PROJ - HAIR_BACK.get(hair_style, 0.0)) * h
    sy = need / (back - front_n)
    print("head: depth face→back %.4f → %.4f (x%.3f)" % (back - front_n, need, sy))
    for i, p, w in heads():
        body.data.vertices[i].co = mi @ Vector((p.x, c.y + (p.y - c.y) * (1 + (sy - 1) * w["head"]), p.z))
    # ③ あご先：唇の前の面（口の高さ）から CHIN_BACK だけ後ろへ（口〜あご先を少しずつ）
    HR = HeadRays(body)
    lip_y = HR.front(zf(MOUTH_F), c.x)
    L2 = head_landmarks(arm, body, hair_style); chin_y = L2["chin_y"]
    dy = (lip_y + CHIN_BACK * h) - chin_y
    print("head: lip y %.4f chin y %.4f → move chin %.4f" % (lip_y, chin_y, dy))
    for i, p, w in heads():
        if p.y < c.y:
            t = smooth((f_of(p.z) - MOUTH_F) / (1.0 - MOUTH_F))
            body.data.vertices[i].co = mi @ Vector((p.x, p.y + dy * t * w["head"], p.z))
    # ④ あごの下：あご先から後ろへ JAW_DEG で上がる面より下にある頭の頂点を、その面まで上げる（首の前の面より前だけ）
    cp = head_landmarks(arm, body, hair_style); cp = Vector((0, cp["chin_y"], cp["chin_z"]))
    nb, nt = bone_ws(arm, "neck")
    neck_front = nt.y - 0.5 * NECK_W * HEAD_WH * h * NECK_DEPTH_K   # 首の上端の前の面（目標の首の太さから）
    tn = math.tan(math.radians(JAW_DEG)); n = 0
    for i, p, w in heads():
        if _dom(w) != "head" or p.y <= cp.y + 0.002 or p.y > neck_front + 0.01 or f_of(p.z) < MOUTH_F: continue
        zl = cp.z + (p.y - cp.y) * tn
        if p.z < zl:
            body.data.vertices[i].co = mi @ Vector((p.x, p.y, zl)); n += 1
    print("head: chin (y %.4f z %.4f), jaw line lifted %d verts" % (cp.y, cp.z, n))
    # ⑤ 後頭部の下：首の後ろの面へなめらかにつなぐ（首の後ろがふくらんで見えないように）
    HR = HeadRays(body)
    neck_back = nt.y + 0.5 * NECK_W * HEAD_WH * h * NECK_DEPTH_K
    b0 = HR.back(z_of(0.62), c.x)
    n = 0
    for i, p, w in heads():
        f = f_of(p.z)
        if f < 0.62: continue
        ym = lerp(b0, neck_back, smooth((f - 0.62) / (0.97 - 0.62)))
        if p.y > ym:
            body.data.vertices[i].co = mi @ Vector((p.x, ym, p.z)); n += 1
    print("head: back of head into neck back (y %.4f → %.4f), %d verts" % (b0, neck_back, n))
    HR = HeadRays(body)
    print("head: width/h by f: " + " ".join("%.2f:%.3f" % (f, 2 * HR.half_width(z_of(f), c.x) / h) for f in (0.3, 0.4, 0.45, 0.5, 0.55, 0.6, 0.65, 0.7, 0.75, 0.8, 0.85, 0.9, 0.95)))
    return head_landmarks(arm, body, hair_style)

# ---- 首：首の骨の軸に中心をそろえた楕円柱（上端が一番細く、付け根へ少し広がる。太さは頭の最大幅に対する比）----
def reshape_neck(arm, body, L):
    W = vgroup_weights(body)
    mw = body.matrix_world; mi = mw.inverted()
    mid = NECK_W * HEAD_WH * L["h"]
    A, Bt = bone_ws(arm, "neck")
    ax = (Bt - A); Ln = ax.length; ax.normalize()
    side = Vector((1, 0, 0)); side = (side - ax * side.dot(ax)).normalized()
    dep = ax.cross(side).normalized()
    items = []
    for i, (v, w) in enumerate(zip(body.data.vertices, W)):
        wn = w.get("neck", 0.0)
        if wn <= 0.0: continue
        p = mw @ v.co
        t = max(0.0, min(1.0, (p - A).dot(ax) / Ln))
        c = A + ax * (t * Ln)
        r = p - c
        items.append((i, wn, t, r.dot(side), r.dot(dep), c))
    bins = 6
    mx = [1e-3] * bins; md = [1e-3] * bins
    for i, wn, t, rx, rd, c in items:
        if _dom(W[i]) != "neck": continue
        k = min(int(t * bins), bins - 1)
        mx[k] = max(mx[k], abs(rx)); md[k] = max(md[k], abs(rd))
    for k in range(1, bins):
        if mx[k] <= 1e-3: mx[k] = mx[k - 1]; md[k] = md[k - 1]
    for i, wn, t, rx, rd, c in items:
        k = min(int(t * bins), bins - 1)
        if abs(rx) > mx[k] * 1.15 or abs(rd) > md[k] * 1.15: continue   # 首の外（肩・胸）は動かさない
        taper = lerp(NECK_TAPER[1], 1.0, smooth(t / 0.5)) if t < 0.5 else lerp(1.0, NECK_TAPER[0], smooth((t - 0.5) / 0.5))
        hw = mid * taper * 0.5
        sx = max(0.5, min(hw / mx[k], 1.6)); sd = max(0.5, min(hw * NECK_DEPTH_K / md[k], 1.6))
        tgt = c + side * (rx * sx) + dep * (rd * sd)
        p = mw @ body.data.vertices[i].co
        body.data.vertices[i].co = mi @ (p + (tgt - p) * min(wn * 1.6, 1.0))
    print("neck: mid width %.4f (top %.4f base %.4f) depth %.4f  was half widths %s" % (mid, mid * NECK_TAPER[0], mid * NECK_TAPER[1], mid * NECK_DEPTH_K, " ".join("%.3f" % m for m in mx)))

# ---- 首の重み：首の円柱より外（僧帽筋・胸の上）の頂点は、首ではなく胸（spine3）に付ける ----
#   首の骨の重みが残っていると、ゲームで頭の位置を上げたとき僧帽筋が首と一緒に引き上げられ、あごの下から急な坂（首がない形）になる
def fix_neck_weights(arm, body, L):
    W = vgroup_weights(body); mw = body.matrix_world
    nb, nt = bone_ws(arm, "neck")
    half = NECK_W * HEAD_WH * L["h"] * NECK_TAPER[1] / 2
    gN = body.vertex_groups["neck"]; gS = body.vertex_groups["spine3"]
    n = 0
    for i, (v, w) in enumerate(zip(body.data.vertices, W)):
        wn = w.get("neck", 0.0)
        if wn <= 0 or w.get("head", 0.0) > 0.3: continue
        p = mw @ v.co
        t = max(0.0, min(1.0, (p.z - nb.z) / max(nt.z - nb.z, 1e-3)))
        c = nb + (nt - nb) * t
        dax = math.hypot((p.x - c.x), (p.y - c.y) / NECK_DEPTH_K)   # 首の楕円の外か（前後は厚みの比で）
        k = smooth((dax - half * 1.02) / (half * 0.25))
        if k <= 0: continue
        mv = wn * k
        gN.add([i], wn - mv, "REPLACE"); gS.add([i], mv, "ADD"); n += 1
    print("neck weights: moved to spine3 on %d verts (outside half width %.3f)" % (n, half))

# ---- 僧帽筋の三角：首の横・後ろの高い所（あごの下の高さ）から肩へ直接張った大きな三角の面を、首の付け根で止める ----
#   元のメッシュは、首の横の頂点が肩の頂点と直接つながっていて、正面から見るとあごの下から肩へ急な坂（首がない形）になる。
#   その頂点を肩線の高さまで下げ、重みを首と胸に付け替える（頭と一緒に動かない）
def fix_trap_apex(arm, body, L):
    W = vgroup_weights(body); mw = body.matrix_world; mi = mw.inverted()
    nb, nt = bone_ws(arm, "neck")
    ua = {sd: bone_ws(arm, "upperarm" + sd)[0] for sd in (".R", ".L")}
    tipx = (abs(ua[".R"].x - nb.x) + abs(ua[".L"].x - nb.x)) * 0.5
    tip_z = (ua[".R"].z + ua[".L"].z) * 0.5 + SHOULDER_TIP
    half = NECK_W * HEAD_WH * L["h"] * NECK_TAPER[1] / 2
    corner_z = tip_z + (tipx - half) * math.tan(math.radians(SHOULDER_DEG))
    bm = bmesh.new(); bm.from_mesh(body.data); bm.verts.ensure_lookup_table()
    P = [mw @ v.co for v in bm.verts]
    low = lambda d: d not in ("neck", "head") and d != ""
    gH, gN, gS = body.vertex_groups["head"], body.vertex_groups["neck"], body.vertex_groups["spine3"]
    n = 0
    for v in bm.verts:
        i = v.index; w = W[i]; d = _dom(w)
        if d != "neck" and not (d == "head" and w.get("neck", 0) > 0.3): continue
        p = P[i]
        if p.y < nb.y - 0.015 or p.z < corner_z + 0.008: continue   # のどの前・もう低いものは動かさない
        ax = abs(p.x - nb.x)
        if ax < 0.55 * half: continue
        nbrs = [e.other_vert(v).index for e in v.link_edges]
        if not any(low(_dom(W[j])) and P[j].z < p.z - 0.015 and abs(P[j].x - nb.x) > ax + 0.008 for j in nbrs): continue
        v.co = mi @ Vector((p.x, p.y, corner_z + 0.004))
        n += 1
        for g, val in ((gH, 0.0), (gN, 0.35), (gS, 0.65)): g.add([i], val, "REPLACE")
    bm.to_mesh(body.data); bm.free()
    print("trap apex: corner z %.3f, lowered %d neck verts" % (corner_z, n))

# ---- 首の表面をなめらかな楕円柱に：元の首の縦の筋（筋肉の出っ張り）を取る ----
#   首の骨に重みのある頂点（頭が主のものは除く）を、その高さの目標の楕円（幅・厚み・先細り）の上へ、向きは変えずに移す
NECK_UP_T = 1.5   # 首の楕円柱を、頭の付け根より上（耳の下・あごの角の後ろ）へも、首の骨の長さのこの倍まで延ばして形と法線をそろえる

def _neck_ellipse(w, p, A, ax, Ln, side, dep, mid):
    """首の楕円柱の上の点・外向きの法線・合わせる強さ（1 = 全部）。首の外・あごの前の面は None"""
    wn, wh = w.get("neck", 0.0), w.get("head", 0.0)
    t = (p - A).dot(ax) / Ln
    if t < -0.15 or t > NECK_UP_T: return None
    if t <= 1.0 and (wn < 0.2 or (_dom(w) == "head" and wh > 0.8)): return None
    if t > 1.0 and wh <= 0: return None
    tc = max(0.0, min(1.0, t))
    taper = lerp(NECK_TAPER[1], 1.0, smooth(tc / 0.5)) if tc < 0.5 else lerp(1.0, NECK_TAPER[0], smooth((tc - 0.5) / 0.5))
    hw = mid * taper * 0.5; hd = hw * NECK_DEPTH_K
    c = A + ax * (t * Ln)
    r = p - c; rx, rd = r.dot(side), r.dot(dep)
    e = math.hypot(rx / hw, rd / hd)
    lim = 1.4 if t <= 1.0 else 1.25
    if e > lim or e < 0.5: return None
    if t > 1.0 and rd < -0.35 * hd: return None    # あごの下・前の面は動かさない（あごの線を残す）
    th = math.atan2(rd / hd, rx / hw)
    q = c + side * (hw * math.cos(th)) + dep * (hd * math.sin(th))
    nr = (side * (math.cos(th) / hw) + dep * (math.sin(th) / hd)).normalized()
    k = 1.0 if t <= 1.0 else smooth((NECK_UP_T - t) / (NECK_UP_T - 1.0)) * smooth((lim - e) / 0.15)
    return q, nr, k

def smooth_neck(arm, body, L):
    W = vgroup_weights(body); mw = body.matrix_world; mi = mw.inverted()
    mid = NECK_W * HEAD_WH * L["h"]
    A, Bt = bone_ws(arm, "neck")
    ax = (Bt - A); Ln = ax.length; ax.normalize()
    side = Vector((1, 0, 0)); side = (side - ax * side.dot(ax)).normalized()
    dep = ax.cross(side).normalized()
    n = 0
    for i, (v, w) in enumerate(zip(body.data.vertices, W)):
        g = _neck_ellipse(w, mw @ v.co, A, ax, Ln, side, dep, mid)
        if g is None: continue
        q, nr, k = g
        p = mw @ v.co
        body.data.vertices[i].co = mi @ (p + (q - p) * k); n += 1
    print("neck smooth: %d verts onto ellipse" % n)

def neck_normals(arm, body, L):
    """首の楕円柱の頂点の法線を、楕円の外向き（高さ方向の成分なし）にそろえる。ほかの頂点は形から計算したなめらかな法線。
       首の三角は細長く不ぞろいなので、形から計算した法線のままだと、くさび形の明暗が出る"""
    W = vgroup_weights(body); mw = body.matrix_world
    mid = NECK_W * HEAD_WH * L["h"]
    A, Bt = bone_ws(arm, "neck")
    ax = (Bt - A); Ln = ax.length; ax.normalize()
    side = Vector((1, 0, 0)); side = (side - ax * side.dot(ax)).normalized()
    dep = ax.cross(side).normalized()
    me = body.data
    nrm = [v.normal.copy() for v in me.vertices]
    n = 0
    for i, (v, w) in enumerate(zip(me.vertices, W)):
        g = _neck_ellipse(w, mw @ v.co, A, ax, Ln, side, dep, mid)
        if g is None: continue
        q, nr, k = g
        nrm[i] = (nr * k + nrm[i] * (1 - k)).normalized(); n += 1
    me.normals_split_custom_set_from_vertices(nrm)
    print("neck normals: %d verts" % n)

# ---- 肩線：首の付け根から肩先へ SHOULDER_DEG で下がる線（僧帽筋の斜面）。上の面だけを高さ方向に合わせる ----
def shape_shoulders(arm, body, L):
    W = vgroup_weights(body)
    mw = body.matrix_world; mi = mw.inverted()
    nb, nt = bone_ws(arm, "neck")
    ua = {sd: bone_ws(arm, "upperarm" + sd)[0] for sd in (".R", ".L")}
    tipx = (abs(ua[".R"].x - nb.x) + abs(ua[".L"].x - nb.x)) * 0.5
    tip_z = (ua[".R"].z + ua[".L"].z) * 0.5 + SHOULDER_TIP
    tn = math.tan(math.radians(SHOULDER_DEG))
    names = ("spine3", "clavicle.R", "clavicle.L", "upperarm.R", "upperarm.L", "spine2")
    pts = [(i, mw @ v.co, w) for i, (v, w) in enumerate(zip(body.data.vertices, W)) if _dom(w) in names or (_dom(w) == "neck" and w.get("neck", 0) < 0.7)]
    # 首の付け根の幅（首の頂点の、付け根の高さの左右の端）
    neck_half = NECK_W * HEAD_WH * L["h"] * NECK_TAPER[1] / 2   # 首の付け根の半分の幅（目標）
    band = 0.10   # 首の付け根から前後にこの範囲（肩の上の尾根）
    def line(ax): return tip_z + (tipx - ax) * tn if ax < tipx else tip_z
    # 今の上の面（横の距離ごとの一番上）
    bins = {}
    for i, p, w in pts:
        if abs(p.y - nb.y) > band: continue
        ax = abs(p.x - nb.x); k = int(ax / 0.01)
        bins[k] = max(bins.get(k, -9), p.z)
    n = 0
    for i, p, w in pts:
        ax = abs(p.x - nb.x)
        if ax < neck_half * 0.85 or abs(p.y - nb.y) > band: continue
        topo = bins.get(int(ax / 0.01))
        if topo is None: continue
        zl = line(ax)
        if ax > tipx and topo <= zl: continue   # 肩先より外は、高すぎる所だけ下げる
        base = min(zl, topo) - 0.035
        if p.z <= base: continue
        z = base + (p.z - base) * (zl - base) / max(topo - base, 1e-4)
        k = 1.0
        body.data.vertices[i].co = mi @ Vector((p.x, p.y, p.z + (z - p.z) * k)); n += 1
    print("shoulders: neck half %.3f tip x %.3f tip z %.3f corner z %.3f (%.1f deg) moved %d" % (neck_half, tipx, tip_z, line(neck_half), SHOULDER_DEG, n))

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
def paint(arm, body, L):
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
    neck_r = NECK_W * HEAD_WH * L["h"] * NECK_TAPER[1] / 2 + 0.012   # 首の付け根の半分の幅（目標）＋ 少し。これより外の胸・肩はシャツ
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
            dax = math.hypot(c.x - na.x, c.y - na.y)
            if n in ("neck", "spine3", "head") or n.startswith("clavicle"):
                if n != "head" and n != "neck" and dax > neck_r: cell = "shirt"
                elif n != "head" and above < -COLLAR_BAND: cell = "shirt"
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
    def surf(lx, ly, lz, axis):   # 頭の座標の点から、axis（頭の座標の単位ベクトル）の外側から当てた頭の表面（頭の座標）
        o = c + right * lx + up * (ly - MP.HC_Y) + fwd * lz
        dw = right * axis[0] + up * axis[1] + fwd * axis[2]
        hit = bvh.ray_cast(o + dw * 0.5, -dw, 1.0)
        if hit[0] is None: return None
        q = hit[0] - c
        return Vector((q.dot(right), MP.HC_Y + q.dot(up), q.dot(fwd)))
    head_frame.surf = surf
    return c, R, right, up, fwd, radius

def to_world(c, right, up, fwd, v):   # 頭の座標（x 右・y 上・z 前）→ 世界
    return c + right * v.x + up * v.y + fwd * v.z

def build_hair_sk(b, style):
    """make_player の髪型と同じ流れ。横の毛束はこめかみの生え際（TEMPLE_HAIR_F）より上で止める。ウェーブの前髪も生え際より上で"""
    th0 = MP.HAIR_THICK; d = MP.dir_from; C = MP.CROWN
    if style == "hair_buzz": MP.build_shell(b, style, th0 * 0.22); return
    if style == "hair_short":
        th = th0 * 0.65; MP.build_shell(b, style, th)
        for az in (-34, 0, 34): MP.tuft(b, style, th, C, d(40, az), 0.36, 0.09, bulge=0.02)
        for sx in (-1, 1):      MP.tuft(b, style, th, C, d(34, sx * 98), 0.30, 0.01, bulge=0.010)
        for az in (160, 200):   MP.tuft(b, style, th, C, d(-20, az), 0.32, 0.01, bulge=0.012)
    elif style == "hair_wave":
        th = th0 * 1.3; MP.build_shell(b, style, th)
        for az in (-42, 0, 42): MP.tuft(b, style, th, C, d(32, az), 0.56, 0.08, bulge=0.06, curl=0.55)
        for sx in (-1, 1):      MP.tuft(b, style, th, C, d(-22, sx * 100), 0.56, 0.05, bulge=0.06, curl=0.55)
        for az in (150, 180, 210): MP.tuft(b, style, th, C, d(-48, az), 0.54, 0.04, bulge=0.035, curl=0.55)
    elif style == "hair_tied":
        th = th0 * 0.55; MP.build_shell(b, style, th)
        knot = d(-8, 180)
        for az in (-40, -14, 14, 40): MP.tuft(b, style, th, d(44, az), knot, 0.38, 0.0, bulge=0.012)
        for sx in (-1, 1):            MP.tuft(b, style, th, d(30, sx * 85), knot, 0.34, 0.0, bulge=0.010)
        MP.tuft(b, style, th, d(80, 0), knot, 0.36, 0.0, bulge=0.015)
        MP.ponytail(b)

# 顔の絵の範囲（スキニングのモデル）：縦は頭の高さの割合 FACE_F0〜FACE_F1、横は目の幅 E の ±FACE_HALF_E
FACE_F0, FACE_F1, FACE_HALF_E, FACE_N = 0.30, 1.03, 2.75, 256

class HeadGeo:   # 頭の座標（x 右・y 上・z 前。頭の付け根の下 HC_Y が原点）と、高さの割合 f・目の幅 E の換算
    def __init__(self, c, up, top_z, h):
        h = h * CHIN_CORNER   # 画面のあご先の角まで
        self.c, self.up, self.top, self.h = c, up, top_z, h
        self.E = HEAD_WH / HEADW_CHEEK * h / 5
    def f(self, ly): return (self.top - (self.c + self.up * (ly - MP.HC_Y)).z) / self.h
    def y(self, f):  return MP.HC_Y + (self.top - f * self.h - self.c.z) / self.up.z

def head_prep(arm, body, L, hair_style):
    """髪の生え際を比率に合わせる（前の生え際の高さ・耳の上）。MP.hairline を置き換える"""
    c, R, right, up, fwd, radius = head_frame(arm, body)
    G = HeadGeo(c, up, L["top"], L["h"])
    def dy_at(f, axis):   # 高さ f の頭の表面の点の、頭の中心からの向きの上の成分
        p = head_frame.surf(0.0, G.y(f), 0.0, axis) if axis[2] else head_frame.surf(0.0, G.y(f), 0.0, axis)
        q = Vector((p.x, p.y - MP.HC_Y, p.z)); return q.normalized().y
    front = dy_at(HAIRLINE_F, (0, 0, 1))
    ear_top = dy_at(BROW_F - 0.03, (1, 0, 0))
    s60 = math.sin(math.radians(60)); temple = dy_at(TEMPLE_HAIR_F, (s60, 0, 0.5))
    def hairline(d, style="hair_short"):
        az = math.atan2(d.x, d.z); cc = math.cos(az)
        fr, tp, side, back = front, temple, ear_top + 0.05, -0.62
        if style == "hair_buzz": fr = front + 0.05
        if style == "hair_wave": fr, tp, side, back = front, temple, -0.25, -0.85
        if style == "hair_tied": fr = front - 0.03
        if cc >= 0.5: y0 = lerp(tp, fr, (cc - 0.5) / 0.5)          # 前〜こめかみ
        elif cc >= 0.0: y0 = lerp(side, tp, cc / 0.5)              # こめかみ〜真横
        else: y0 = lerp(side, back, -cc) if style != "hair_tied" else lerp(side, back * 0.9, -cc)   # 真横〜後ろ
        if style != "hair_wave":
            ek = smooth((0.45 - abs(cc + 0.12)) / 0.25)   # 耳のあたり（真横の少し後ろ）は耳の上端より上に
            y0 = max(y0, lerp(y0, ear_top, ek))
        return d.y - y0
    MP.hairline = hairline
    print("hairline: front dy %.3f (f %.3f)  temple dy %.3f  ear top dy %.3f" % (front, HAIRLINE_F, temple, ear_top))

def build_head_extras(arm, body, hair_style, L):
    c, R, right, up, fwd, radius = head_frame(arm, body)
    surf = head_frame.surf
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
        # 法線を外へ（頭の中心から。耳は耳の中心から）
        oc = getattr(b, "center", None)
        bm = bmesh.new(); bm.from_mesh(me)
        for f in bm.faces:
            ref = to_world(hc, right, up, fwd, oc) if oc is not None else c
            if f.normal.dot(f.calc_center_median() - ref) < 0: f.normal_flip()
        bm.to_mesh(me); bm.free()
        g = ob.vertex_groups.new(name="head"); g.add(list(range(len(me.vertices))), 1.0, "REPLACE")
        mod = ob.modifiers.new("Armature", "ARMATURE"); mod.object = arm
        ob.parent = arm
        objs.append(ob)
        return ob
    # 髪（先に作って、てっぺんと後ろの端を測る）
    top_z = L["skull"]; back = min((surf(0.0, MP.HC_Y + y, 0.0, (0, 0, -1)) or Vector((0, 0, 0))).z for y in (0.0, 0.02, 0.04))
    if hair_style:
        bh = MP.Builder(); build_hair_sk(bh, hair_style); ho = make_obj("Hair", bh, MAT_PAL)
        top_z = max(top_z, max(v.co.z for v in ho.data.vertices))
        back = min(back, min(v.z for v in bh.v))
    h = top_z - L["chin_z"]
    G = HeadGeo(c, up, top_z, h); E = G.E
    # 顔の絵を貼る面：UV は正面からの投影（横は E、縦は頭の高さの割合）
    def face_uv(p):
        u = 0.5 + (p.x / E) / (2 * FACE_HALF_E)
        v = (G.f(p.y) - FACE_F0) / (FACE_F1 - FACE_F0)
        return (u, 1.0 - v)
    MP.face_uv = face_uv
    bf = MP.Builder(); MP.build_face(bf); make_obj("Face", bf, MAT_FACE)
    # 鼻：眉の高さから鼻の下まで。下の幅 E、鼻先は顔の面から NOSE_PROJ・h 前
    def fz(f): return surf(0.0, G.y(f), 0.0, (0, 0, 1)).z
    bn = MP.Builder()
    yt, yb = G.y(BROW_F + 0.015), G.y(NOSE_F)
    zt, zb = fz(BROW_F + 0.015) - 0.003, fz(NOSE_F) - 0.003
    w0, w1, h0, h1 = 0.10 * E, 0.50 * E, 0.02 * h, NOSE_PROJ * h + 0.003
    tl, tr, tc = bn.add((-w0, yt, zt)), bn.add((w0, yt, zt)), bn.add((0, yt, zt + h0))
    bl, br, bc = bn.add((-w1, yb + 0.004, zb)), bn.add((w1, yb + 0.004, zb)), bn.add((0, yb, zb + h1))
    bn.face([tl, tc, bc, bl], "nose"); bn.face([tc, tr, br, bc], "nose"); bn.face([bl, bc, br], "nose"); bn.face([tl, tr, tc], "skin")
    make_obj("Nose", bn, MAT_PAL)
    tip = zb + h1; D = tip - back
    # 耳：眉の高さ〜鼻の下の高さ、鼻先から後頭部までの EAR_POS の所。頭の横の面から外へ半分の楕円体
    ye0, ye1 = G.y(BROW_F), G.y(NOSE_F)
    yc, ze = (ye0 + ye1) / 2, tip - EAR_POS * D
    a, bb, t = (ye0 - ye1) / 2, 0.30 * (ye0 - ye1), 0.30 * (ye0 - ye1)
    tilt = math.radians(12)   # 上を少し後ろへ
    for sx in (-1, 1):
        sp = surf(0.0, yc, ze, (sx, 0, 0))
        xs = sp.x if sp is not None else sx * R
        be = MP.Builder()
        rings = []
        K, Nn = 4, 12
        for k in range(K):
            ang = (k / K) * math.pi / 2
            sk, xk = math.cos(ang), xs - sx * 0.004 + sx * (t + 0.004) * math.sin(ang)
            ring = []
            for j in range(Nn):
                u = 2 * math.pi * j / Nn
                dy, dz = a * sk * math.cos(u), bb * sk * math.sin(u)
                ring.append(be.add((xk, yc + dy * math.cos(tilt) + dz * math.sin(tilt), ze + dz * math.cos(tilt) - dy * math.sin(tilt))))
            rings.append(ring)
        apex = be.add((xs + sx * t, yc, ze))
        for k in range(K - 1):
            for j in range(Nn):
                be.face([rings[k][j], rings[k][(j + 1) % Nn], rings[k + 1][(j + 1) % Nn], rings[k + 1][j]], "ear")
        for j in range(Nn): be.face([rings[-1][j], rings[-1][(j + 1) % Nn], apex], "ear")
        be.center = Vector((xs - sx * 0.02, yc, ze))
        make_obj("Ear", be, MAT_PAL)
    print("head extras: h %.4f E %.4f  depth %.3f h  hair top +%.4f over skull  nose tip %.4f back %.4f ear z %.4f" % (h, E, D / h, top_z - L["skull"], tip, back, ze))
    return objs

# ---- 顔の絵（スキニングのモデル用。models/players/faces/）：縦は頭の高さの割合、横は目の幅 E で配置 ----
#   目：幅 E、両目の間 E（中心は ±E）、目の高さ EYE_F。眉：BROW_F。口：幅 1.5E、MOUTH_F。
#   measure=True：測る用（目＝水色の楕円・眉＝青・口＝赤だけ）
def make_faces(out_dir, measure=False):
    os.makedirs(out_dir, exist_ok=True)
    N = FACE_N
    Eh = HEAD_WH / HEADW_CHEEK / 5               # E を頭の高さの単位で
    px_x = N / (2 * FACE_HALF_E)                 # E あたりのピクセル（横）
    px_y = N / (FACE_F1 - FACE_F0) * Eh          # E あたりのピクセル（縦）
    X = lambda xe: (0.5 + xe / (2 * FACE_HALF_E)) * N
    Y = lambda f: (f - FACE_F0) / (FACE_F1 - FACE_F0) * N
    def save(cv, name):
        img = bpy.data.images.new(name, N, N, alpha=True); img.pixels = cv.px
        img.filepath_raw = os.path.join(out_dir, name + ".png"); img.file_format = "PNG"; img.save()
    sh, dark, white, brow, mouth, blush = (0.20, 0.12, 0.10), (0.12, 0.08, 0.07), (1.0, 1.0, 1.0), (0.15, 0.10, 0.07), (0.62, 0.30, 0.28), (0.85, 0.32, 0.28)
    for name, prm in MP.FACES.items():
        cv = MP.Canvas(N)
        ew = prm["ew"]; rx = 0.5 * px_x; ry = 0.5 * px_y * prm["eh"] / ew; k = prm["shade"]
        lw = lambda old: old / ew * rx          # 前の絵の太さ（目の半分の幅 ew ピクセルのとき）→ 今の太さ
        if not measure:
            for sx in (-1, 1):
                cv.soft(X(sx * 1.05), Y(BROW_F + 0.025), 1.1 * px_x, 0.30 * px_y, sh, 0.08 * k)                   # 眉の下（彫り）
                cv.soft(X(sx * 1.75), Y(0.69), 0.85 * px_x, 0.40 * px_y, sh, 0.05 * k, rot=-sx * 0.4)            # 頬骨の下
                cv.soft(X(sx * 1.45), Y(0.64), 0.80 * px_x, 0.50 * px_y, blush, 0.07)                           # 頬の赤み
        for sx in (-1, 1):
            ex, ey = X(sx * 1.0), Y(EYE_F)
            if measure:
                cv.ellipse(ex, ey, rx, ry, (0.0, 1.0, 1.0))
            else:
                cv.ellipse(ex, ey, rx, ry, white)
                ir = 0.5 * prm["ir"] / ew
                cv.ellipse(ex - sx * 0.03 * px_x, ey - 0.02 * px_y, ir * px_x, ir * px_y * 1.04, MP.IRIS_KEY)
                cv.ellipse(ex - sx * 0.03 * px_x, ey - 0.02 * px_y, ir * 0.45 * px_x, ir * 0.47 * px_y, (0.03, 0.03, 0.03))
                cv.ellipse(ex - sx * 0.03 * px_x + 0.13 * rx, ey - 0.15 * ry * 2, 0.12 * rx, 0.12 * rx, white)
                top = ey - ry + lw(0.4)
                cv.line([(ex - rx, top + lw(1.4)), (ex - rx * 0.3, top - lw(0.2)), (ex + rx, top + lw(1.2))], lw(1.1 if not prm["lash"] else 1.6), dark)
                cv.line([(ex - rx * 0.8, ey + ry - lw(0.3)), (ex + rx * 0.8, ey + ry - lw(0.3))], lw(0.5), dark, 0.35)
            bx, by = X(sx * 1.05), Y(BROW_F)
            ang = math.radians(prm["brow"]); hl = 1.15 * rx
            cv.line([(bx - sx * hl * math.cos(ang), by + hl * math.sin(ang)), (bx + sx * hl * math.cos(ang), by - hl * math.sin(ang))], lw(prm["bt"] * 0.5 + 0.4), (0.0, 0.0, 1.0) if measure else brow)
        mx, my = X(0.0), Y(MOUTH_F)
        mwid = 0.75 * px_x
        cv.line([(mx - mwid, my + lw(0.4)), (mx, my), (mx + mwid, my + lw(0.4))], lw(1.2), (1.0, 0.0, 0.0) if measure else mouth)
        save(cv, name)
    if measure: return
    # 剃り跡・ひげ：あごの輪郭の内側、頬の下からあご先へ。鼻の下（口ひげ）。唇はあける
    cheek_e = 2.5; jaw_e = JAW_K * cheek_e
    def jaw_half(f):   # 頬骨〜あご〜あご先の、顔の輪郭の半分の幅（E）
        if f < CHEEK_F: return cheek_e
        if f < MOUTH_F: return lerp(cheek_e, jaw_e, smooth((f - CHEEK_F) / (MOUTH_F - CHEEK_F)))
        return lerp(jaw_e, 0.75, smooth((f - MOUTH_F) / (1.0 - MOUTH_F)))
    for level in (1, 2):
        cv = MP.Canvas(N)
        col = (0.36, 0.42, 0.50) if level == 1 else (0.10, 0.08, 0.06)
        amax = 0.12 if level == 1 else 0.35
        for py in range(N):
            for pxl in range(N):
                xe = ((pxl + 0.5) / N - 0.5) * 2 * FACE_HALF_E; f = FACE_F0 + (py + 0.5) / N * (FACE_F1 - FACE_F0)
                edge = 0.92 * jaw_half(f) - abs(xe)
                a = smooth((f - (NOSE_F + 0.035)) / 0.06) * smooth(edge / 0.35)
                a = max(a, smooth((0.95 - abs(xe)) / 0.25) * smooth((f - NOSE_F - 0.008) / 0.01) * smooth((MOUTH_F - 0.018 - f) / 0.01))
                lip = smooth((0.85 - abs(xe)) / 0.15) * smooth((0.016 - abs(f - MOUTH_F)) / 0.008)
                a *= 1.0 - lip
                if a > 0.01: cv.put(pxl, py, col, amax * a)
        save(cv, "beard_%d" % level)

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
    make_faces(os.path.join(ROOT, "models", "players", "faces"))
    if os.environ.get("MEASURE_FACES"): make_faces(os.environ["MEASURE_FACES"], measure=True)   # 確認用：測る色の顔の絵
    for i in ids:
        arm, body, found = load_base()
        materials()
        hs = HAIR_NAME.get(hair_tab.get(i, ""), hair_default(i))
        apply_body(arm, body, body_tab[i])
        subdivide_head(body)
        apply_head_scale(arm, body, HEAD_SCALE)
        L = shape_head(arm, body, hs)
        reshape_neck(arm, body, L)
        fix_neck_weights(arm, body, L)
        fix_trap_apex(arm, body, L)
        shape_shoulders(arm, body, L)
        smooth_neck(arm, body, L)
        cut_lines(arm, body)
        paint(arm, body, L)
        body.data.materials.clear(); body.data.materials.append(MAT_PAL)
        for p in body.data.polygons: p.material_index = 0
        head_prep(arm, body, L, hs)
        scalp_hair_color(body, arm, hs)
        build_head_extras(arm, body, hs, L)
        neck_normals(arm, body, L)
        out = os.path.join(ROOT, "models", "players", "p%02d.glb" % i)
        export(arm, out)
        print("wrote", out, "hair", hs, "verts", len(body.data.vertices))
    with open(os.path.join(ROOT, "models", "players", "bones.txt"), "w", encoding="utf-8") as f:
        f.write("# 骨の対応表：決まった名前（ゲームが見る名前） ← 元のモデルの骨の名前（%s）\n" % os.path.basename(BASE))
        for k in BONE_ALIASES: f.write("%-12s <- %s\n" % (k, found.get(k, "?") if found else "?"))
