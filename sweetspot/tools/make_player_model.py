# SWEET SPOT — 選手の体のモデル（試作）を作る Blender のスクリプト
#
#   使い方（どちらか）：
#     blender --background --python tools/make_player_model.py
#     python3 tools/make_player_model.py        （pip install bpy した Python で）
#   できるもの：models/player_skin.blend（手で直す元）と models/player_skin.glb（ゲームが読む）
#
#   手で直すときは models/player_skin.blend を開いて直し、glTF（.glb）で models/player_skin.glb に書き出す。
#   書き出しの設定：Format = glTF Binary、Include → Selected Objects は外す、Transform → +Y Up はオン（初めのまま）、
#                   Data → Mesh → Apply Modifiers はオン、Armature → Export Deformation Bones Only は外す（_end の骨も要る）
#   守ること（models/README_skin.md にも書いた）
#     ・骨の名前を変えない（ゲームは名前で骨を探す）。骨を足しても使わないだけで害はない
#     ・マテリアルの名前で色を決める：skin（肌）shirt（シャツ）shorts（ショーツ）socks（ソックス）shoes（シューズ）sole（靴底）hair（髪）
#       それ以外の名前のマテリアルは、Blender で付けた色のまま出る
#     ・大きさは 1 = 1 m。身長 1.85 m の選手の大きさで作る（ゲームが選手ごとの身長に合わせて大きさを変える）
#     ・正面は -Y（Blender の Front ビューでこちらを向く）。選手の右（.R の骨）は +X 側
#     ・頭・顔・髪・ラケットは、今はゲームのコードが描く（このモデルは首から下）
import bpy, bmesh, os, math
from mathutils import Vector

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "models")
os.makedirs(OUT, exist_ok=True)

# ゲームの体の座標（x 右、y 上、z 前、m）→ Blender（x 右、y 後ろ、z 上）
def G(x, y, z): return Vector((x, -z, y))

# ---- 骨（ゲームの関節の位置と同じ。render.c の DrawFigureBody の立った姿勢）----
J = {
    "pelvis": (0, 0.95, 0), "waist": (0, 1.13, 0), "chest": (0, 1.35, 0), "neck": (0, 1.51, 0),
    "head": (0, 1.72, 0.01), "headtop": (0, 1.84, 0.01),
}
for s, k in ((".R", 1), (".L", -1)):
    J["shoulder" + s] = (k * 0.20, 1.48, 0.0)
    J["elbow" + s]    = (k * 0.24, 1.18, -0.03)
    J["wrist" + s]    = (k * 0.26, 0.89, 0.02)
    J["handtip" + s]  = (k * 0.27, 0.80, 0.03)
    J["hip" + s]      = (k * 0.10, 0.93, 0.0)
    J["knee" + s]     = (k * 0.105, 0.50, 0.03)
    J["ankle" + s]    = (k * 0.11, 0.07, 0.0)
    J["toe" + s]      = (k * 0.11, 0.025, 0.16)

BONES = [   # 名前、頭、しっぽ、親、つながっているか
    ("hips", "pelvis", "waist", None, False),
    ("spine", "waist", "chest", "hips", True),
    ("chest", "chest", "neck", "spine", True),
    ("neck", "neck", "head", "chest", True),
    ("head", "head", "headtop", "neck", True),
    ("head_end", "headtop", None, "head", True),
]
for s in (".R", ".L"):
    BONES += [
        ("upperarm" + s, "shoulder" + s, "elbow" + s, "chest", False),
        ("forearm" + s, "elbow" + s, "wrist" + s, "upperarm" + s, True),
        ("hand" + s, "wrist" + s, "handtip" + s, "forearm" + s, True),
        ("hand_end" + s, "handtip" + s, None, "hand" + s, True),
        ("thigh" + s, "hip" + s, "knee" + s, "hips", False),
        ("shin" + s, "knee" + s, "ankle" + s, "thigh" + s, True),
        ("foot" + s, "ankle" + s, "toe" + s, "shin" + s, True),
        ("foot_end" + s, "toe" + s, None, "foot" + s, True),
    ]

# ---- 空の場面から ----
bpy.ops.wm.read_factory_settings(use_empty=True)
scene = bpy.context.scene

# ---- 骨組み ----
arm_data = bpy.data.armatures.new("PlayerRig")
rig = bpy.data.objects.new("PlayerRig", arm_data)
scene.collection.objects.link(rig)
bpy.context.view_layer.objects.active = rig
bpy.ops.object.mode_set(mode="EDIT")
eb = {}
for name, h, t, parent, conn in BONES:
    b = arm_data.edit_bones.new(name)
    b.head = G(*J[h])
    if t: b.tail = G(*J[t])
    else:   # 端の骨：親の向きに少し伸ばす
        p = eb[parent]; d = (p.tail - p.head).normalized()
        b.tail = b.head + d * 0.05
    b.roll = 0.0
    if parent: b.parent = eb[parent]; b.use_connect = conn and (eb[parent].tail - b.head).length < 1e-4
    b.use_deform = not name.endswith("_end") and name != "head"
    eb[name] = b
bpy.ops.object.mode_set(mode="OBJECT")

# ---- 体の形：骨組みの線に「スキン」の太さをつけて、なめらかにする ----
#   (位置, 横の太さ, 前後の太さ)。胴は横に広く前後に薄い
SK = [
    ("pelvis", 0.165, 0.110), ("waist", 0.150, 0.100), ("chest", 0.185, 0.115), ("neck", 0.055, 0.055),
    ("neck2", 0.050, 0.050),
]
for s in (".R", ".L"):
    SK += [
        ("shoulder" + s, 0.060, 0.060), ("elbow" + s, 0.044, 0.044), ("wrist" + s, 0.034, 0.030), ("handtip" + s, 0.036, 0.018),
        ("hip" + s, 0.085, 0.085), ("knee" + s, 0.056, 0.056), ("ankle" + s, 0.042, 0.042), ("toe" + s, 0.042, 0.030),
        ("heel" + s, 0.042, 0.035),
    ]
P = dict(J)
P["neck2"] = (0, 1.62, 0.005)   # 首の上（頭の中に隠れる）
for s, k in ((".R", 1), (".L", -1)):
    P["heel" + s] = (k * 0.11, 0.04, -0.06)
EDGES = [("pelvis", "waist"), ("waist", "chest"), ("chest", "neck"), ("neck", "neck2")]
for s in (".R", ".L"):
    EDGES += [("chest", "shoulder" + s), ("shoulder" + s, "elbow" + s), ("elbow" + s, "wrist" + s), ("wrist" + s, "handtip" + s),
              ("pelvis", "hip" + s), ("hip" + s, "knee" + s), ("knee" + s, "ankle" + s), ("ankle" + s, "toe" + s), ("ankle" + s, "heel" + s)]
names = [n for n, _, _ in SK]
me = bpy.data.meshes.new("Body")
me.from_pydata([G(*P[n]) for n in names], [(names.index(a), names.index(b)) for a, b in EDGES], [])
body = bpy.data.objects.new("Body", me)
scene.collection.objects.link(body)
bpy.context.view_layer.objects.active = body
body.select_set(True)
mod = body.modifiers.new("Skin", "SKIN")
mod.use_smooth_shade = True
mod.branch_smoothing = 0.6
for i, (n, rx, rz) in enumerate(SK):
    me.skin_vertices[0].data[i].radius = (rx, rz)
me.skin_vertices[0].data[names.index("pelvis")].use_root = True
sub = body.modifiers.new("Subsurf", "SUBSURF"); sub.levels = 1; sub.render_levels = 1
bpy.ops.object.modifier_apply(modifier="Skin")
bpy.ops.object.modifier_apply(modifier="Subsurf")

# ---- マテリアル：場所で分ける（シャツ・ショーツ・肌・ソックス・シューズ）----
def mat(name, rgb):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    bsdf = m.node_tree.nodes.get("Principled BSDF")
    if bsdf: bsdf.inputs["Base Color"].default_value = (*rgb, 1.0); bsdf.inputs["Roughness"].default_value = 0.8
    m.diffuse_color = (*rgb, 1.0)
    return m
MATS = [mat("skin", (0.84, 0.55, 0.36)), mat("shirt", (0.9, 0.9, 0.92)), mat("shorts", (0.1, 0.1, 0.25)),
        mat("socks", (0.95, 0.95, 0.95)), mat("shoes", (0.97, 0.97, 0.98)), mat("sole", (0.2, 0.2, 0.25))]
for m in MATS: me.materials.append(m)
MI = {m.name: i for i, m in enumerate(MATS)}

def seg_t(p, a, b):   # 線分 a→b の上で p に近い所（0〜1）と距離
    ab = b - a; t = max(0.0, min(1.0, (p - a).dot(ab) / max(ab.length_squared, 1e-9)))
    return t, (a + ab * t - p).length

bm = bmesh.new(); bm.from_mesh(me)
for f in bm.faces:
    c = f.calc_center_median()
    gy = c.z   # ゲームの高さ
    side = ".R" if c.x >= 0 else ".L"
    sh, el = G(*J["shoulder" + side]), G(*J["elbow" + side])
    t_arm, d_arm = seg_t(c, sh, el)
    _, d_fore = seg_t(c, el, G(*J["wrist" + side]))
    if gy < 0.105: m = "sole" if gy < 0.035 else "shoes"
    elif gy < 0.20: m = "socks"
    elif gy > 1.49 and abs(c.x) < 0.09: m = "skin"                       # 首
    elif min(d_arm, d_fore) < 0.075 and abs(c.x) > 0.16:                  # 腕：上腕の上の半分は袖
        m = "shirt" if (d_arm <= d_fore and t_arm < 0.45) else "skin"
    elif gy > 1.00: m = "shirt"
    elif gy > 0.68: m = "shorts"                                          # 太ももの半分まで
    else: m = "skin"
    f.material_index = MI[m]
bm.to_mesh(me); bm.free()

# ---- 骨の重み：ふつうは Blender の自動の重み（熱の広がり）。だめなら近い骨から ----
rig.select_set(True); body.select_set(True)
bpy.context.view_layer.objects.active = rig
try:
    bpy.ops.object.parent_set(type="ARMATURE_AUTO")
    ok = all(len(v.groups) > 0 for v in me.vertices)
except Exception as e:
    print("auto weights failed:", e); ok = False
if not ok:
    print("auto weights incomplete -> nearest-bone weights")
    for vg in list(body.vertex_groups): body.vertex_groups.remove(vg)
    if not body.parent: body.parent = rig
    if not any(m.type == "ARMATURE" for m in body.modifiers):
        body.modifiers.new("Armature", "ARMATURE").object = rig
    deform = [(n, G(*J[h]), G(*J[t])) for n, h, t, p, c in BONES if t and not n.endswith("_end") and n != "head"]
    groups = {n: body.vertex_groups.new(name=n) for n, _, _ in deform}
    for v in me.vertices:
        ds = sorted(((seg_t(v.co, a, b)[1], n) for n, a, b in deform))[:3]
        ws = [(n, 1.0 / max(d, 0.01) ** 4) for d, n in ds]
        tot = sum(w for _, w in ws)
        for n, w in ws: groups[n].add([v.index], w / tot, "REPLACE")

bpy.ops.object.select_all(action="DESELECT")
bpy.ops.wm.save_as_mainfile(filepath=os.path.abspath(os.path.join(OUT, "player_skin.blend")))
bpy.ops.export_scene.gltf(filepath=os.path.abspath(os.path.join(OUT, "player_skin.glb")), export_format="GLB",
                          export_skins=True, export_def_bones=False, export_apply=True, export_yup=True,
                          export_animations=False, export_morph=False, export_texcoords=False)
print("verts", len(me.vertices), "faces", len(me.polygons))
