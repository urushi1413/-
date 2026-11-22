# SWEET SPOT — 選手のモデル（パーツ式）を作る Blender のスクリプト
#
#   blender --background --python tools/make_player.py      （sweetspot/ で実行。pip の bpy でも動く：python3 tools/make_player.py）
#   できるもの：models/player.glb（ゲームが読む）・models/player.blend（手で直す元）・models/palette.png（色の表）
#
# しくみ
#   ・体は部位ごとの別のオブジェクト（頭・首・胴・腰・上腕・前腕・手・腿・すね・靴。左右は .L / .R）。スキニングはしない
#   ・各パーツの原点（ピボット）は親の関節。パーツの向きは「骨の向き」：+Y が子の関節の方（上腕なら肘の方）、
#     +X が選手の右、+Z は X×Y（上向きの骨では前、下向きの骨では後ろ）。ゲームは関節の位置と向きにパーツを置くだけ
#   ・関節の位置はゲームのコード（render.c の DrawFigureBody）の立った姿勢と同じ。単位は m、身長 185cm の選手の大きさ
#     （ゲームが選手ごとの身長に合わせて全体の大きさを変える）
#   ・色は 16x16 のパレット（palette.png）。面ごとに UV をパレットのマスの真ん中に置く。マスの割り当ては PAL の表。
#     ゲームは選手ごとに肌・シャツ・パンツ・髪などのマスの色を入れ替える（models/palettes/<選手の短い名前>.png があればそれを使う）
#   ・ローポリのフラットシェーディング。服は体そのもの（シャツ＝胴、パンツ＝腰）。継ぎ目が割れないよう、端を少し重ねる
import bpy, bmesh, os, math
from mathutils import Vector, Matrix

# ======================= 調整用の数値 =======================
HEAD_SCALE   = 1.40   # 頭の大きさ（今の頭＝半径 10.5cm に対する倍率。1.3〜1.5）
NECK_K       = 0.30   # 首の長さ：首の付け根から頭の中心までのうち、首にする割合（短いほどデフォルメ）
LIMB_THICK   = 1.25   # 手足の太さの倍率
SLEEVE_THICK = 1.18   # 袖の太さ（上腕の太さに対する倍率）
SLEEVE_LEN   = 0.42   # 袖の長さ（上腕の長さに対する割合）
SHORTS_LEN   = 0.50   # パンツの裾（腿の長さに対する割合）
SOCK_LEN     = 0.28   # 靴下の高さ（すねの長さに対する割合）
SHOE_SCALE   = 1.30   # 靴の大きさの倍率
HAND_SCALE   = 1.20   # 手の大きさの倍率
TORSO_W      = 1.00   # 胴の横幅の倍率
OVERLAP      = 0.03   # パーツの端を隣のパーツに重ねる長さ（m）
SIDES_LIMB   = 8      # 手足の断面の角の数
SIDES_BODY   = 10     # 胴・腰の断面の角の数
SIDES_HEAD   = 10     # 頭の横の分割
RINGS_HEAD   = 7      # 頭の縦の分割
# パレットのマス（列, 行）と初めの色（ゲームが選手ごとに上書きする）
PAL = {
    "skin":     ((0, 0), (226, 178, 135)),
    "shirt":    ((1, 0), (235, 235, 240)),
    "accent":   ((2, 0), (40, 90, 200)),     # シャツの差し色（襟・裾・胸のライン）
    "pants":    ((3, 0), (30, 35, 60)),
    "shoes":    ((4, 0), (250, 250, 252)),
    "socks":    ((5, 0), (245, 245, 245)),
    "hair":     ((6, 0), (70, 50, 35)),
    "wrist":    ((7, 0), (230, 60, 60)),     # リストバンド
    "eye":      ((8, 0), (30, 25, 25)),
    "sole":     ((9, 0), (120, 120, 130)),
}
# ============================================================

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.abspath(os.path.join(HERE, "..", "models"))
os.makedirs(OUT, exist_ok=True)

# ---- ゲームの立った姿勢の関節（体の座標：x 右・y 上・z 前、m）。render.c の DrawFigureBody（腰の高さ 0.93）と同じ ----
J = {
    "pelvis": (0.0, 0.95, 0.0), "waist": (0.0, 1.13, 0.0), "neck": (0.0, 1.51, 0.0), "headc": (0.0, 1.72, 0.01),
}
J["headbase"] = tuple(J["neck"][i] + (J["headc"][i] - J["neck"][i]) * NECK_K for i in range(3))
J["headtop"] = (J["headbase"][0], J["headbase"][1] + 0.30, J["headbase"][2])
for s, k in ((".R", 1), (".L", -1)):
    J["shoulder" + s] = (k * 0.20, 1.48, 0.0)
    J["elbow" + s]    = (k * 0.24, 1.18, -0.03)
    J["wrist" + s]    = (k * 0.26, 0.89, 0.02)
    J["handtip" + s]  = (k * 0.27, 0.80, 0.03)
    J["hip" + s]      = (k * 0.10, 0.93, 0.0)
    J["knee" + s]     = (k * 0.105, 0.50, 0.03)
    J["ankle" + s]    = (k * 0.11, 0.07, 0.0)
    J["toe" + s]      = (k * 0.11, 0.025, 0.16)

# パーツ：名前（ゲームのコードと同じ）、ピボットの関節、向きを決める子の関節
PARTS = [("pelvis", "pelvis", "waist"), ("torso", "waist", "neck"), ("neck", "neck", "headbase"), ("head", "headbase", "headtop")]
for s in (".R", ".L"):
    PARTS += [("upperarm" + s, "shoulder" + s, "elbow" + s), ("forearm" + s, "elbow" + s, "wrist" + s), ("hand" + s, "wrist" + s, "handtip" + s),
              ("thigh" + s, "hip" + s, "knee" + s), ("shin" + s, "knee" + s, "ankle" + s), ("shoe" + s, "ankle" + s, "toe" + s)]

def frame(h, t):
    """ゲームの Frame() と同じ：Y = 頭→しっぽ、X = 右（+x）を骨に直角に、Z = X×Y。列が軸の 3x3"""
    h, t = Vector(h), Vector(t)
    y = (t - h); L = y.length; y.normalize()
    x = Vector((1, 0, 0)); x = x - y * x.dot(y); x.normalize()
    z = x.cross(y)
    return Matrix(((x.x, y.x, z.x), (x.y, y.y, z.y), (x.z, y.z, z.z))), L

# ---- 形の部品（パーツの座標で作る）----
class Builder:
    def __init__(self): self.v = []; self.f = []   # f = (頂点の番号の並び, マス)
    def add(self, p): self.v.append(Vector(p)); return len(self.v) - 1
    def face(self, idx, cell): self.f.append((list(idx), cell))
    def ring(self, y, rx, rz, n, cx=0.0, cz=0.0, rot=0.0):
        return [self.add((cx + rx * math.cos(2 * math.pi * i / n + rot), y, cz + rz * math.sin(2 * math.pi * i / n + rot))) for i in range(n)]
    def bridge(self, a, b, cell):
        n = len(a)
        for i in range(n): self.face([a[i], a[(i + 1) % n], b[(i + 1) % n], b[i]], cell)
    def cap(self, r, cell):   # 輪をふさぐ（中心に 1 点）
        c = Vector((0, 0, 0))
        for i in r: c += self.v[i]
        c /= len(r); ci = self.add(c)
        n = len(r)
        for i in range(n): self.face([r[i], r[(i + 1) % n], ci], cell)
    def tube(self, prof, n, rot=None):
        """prof = [(y, rx, rz, マス)]。各区間の色は下の端の輪のマス。両端はふさぐ。輪の角を少し回して角を正面に向けない"""
        rot = math.pi / n if rot is None else rot
        rings = [self.ring(y, rx, rz, n, rot=rot) for y, rx, rz, _ in prof]
        self.cap(rings[0], prof[0][3])
        for i in range(len(rings) - 1): self.bridge(rings[i], rings[i + 1], prof[i][3])
        self.cap(rings[-1], prof[-1][3])
        return rings

def limb(L, r0, r1, cell, n=SIDES_LIMB):
    b = Builder(); b.tube([(-OVERLAP, r0, r0, cell), (L * 0.5, (r0 + r1) * 0.52, (r0 + r1) * 0.52, cell), (L + OVERLAP, r1, r1, cell)], n); return b

def build(name, L):
    T, S = LIMB_THICK, SLEEVE_THICK
    b = Builder()
    if name == "pelvis":   # 腰（パンツそのもの）。ピボットは腰の中心、+Y が上（ウエスト）
        w = TORSO_W
        b.tube([(-0.15, 0.10 * w, 0.075, "pants"), (-0.10, 0.155 * w, 0.105, "pants"), (0.03, 0.168 * w, 0.112, "pants"), (L + OVERLAP, 0.150 * w, 0.102, "pants")], SIDES_BODY)
    elif name == "torso":  # 胴（シャツそのもの）。ピボットはウエスト。裾はパンツに少しかぶせる。襟・裾・胸のラインは差し色
        w = TORSO_W
        b.tube([(-0.07, 0.175 * w, 0.118, "accent"), (-0.045, 0.172 * w, 0.116, "shirt"), (0.16, 0.185 * w, 0.118, "accent"),
                (0.19, 0.188 * w, 0.118, "shirt"), (0.31, 0.205 * w, 0.112, "shirt"), (L - 0.01, 0.12 * w, 0.075, "accent"), (L + 0.025, 0.07, 0.055, "accent")], SIDES_BODY)
    elif name == "neck":
        r = 0.050 * T
        b.tube([(-OVERLAP, r * 1.05, r, "skin"), (L + OVERLAP * 1.5, r, r * 0.95, "skin")], SIDES_LIMB)
    elif name == "head":   # 頭：ローポリの球（縦に少し長く、横に少し細い）。上と後ろは髪、前に目
        R = 0.105 * HEAD_SCALE
        cy, ky, kx = R * 0.92, 1.06, 0.92
        rings = []
        for j in range(1, RINGS_HEAD):
            th = math.pi * j / RINGS_HEAD
            rings.append(b.ring(cy + R * ky * math.cos(th), R * kx * math.sin(th), R * math.sin(th), SIDES_HEAD, rot=math.pi / SIDES_HEAD))
        top = b.add((0, cy + R * ky, 0)); bot = b.add((0, cy - R * ky, 0))
        def cell_at(idx):
            c = Vector((0, 0, 0))
            for i in idx: c += b.v[i]
            c /= len(idx)
            u = (c.y - cy) / R
            front = c.z > 0.55 * R and u < 0.35   # 顔
            return "hair" if (u > 0.30 or (c.z < -0.15 * R and u > -0.50) or (u > 0.05 and not front and abs(c.x) > 0.6 * R)) else "skin"
        n = SIDES_HEAD
        for i in range(n): idx = [rings[0][(i + 1) % n], rings[0][i], top]; b.face(idx, cell_at(idx))
        for k in range(len(rings) - 1):
            for i in range(n):
                idx = [rings[k][i], rings[k][(i + 1) % n], rings[k + 1][(i + 1) % n], rings[k + 1][i]]; b.face(idx[::-1], cell_at(idx))
        for i in range(n): idx = [rings[-1][i], rings[-1][(i + 1) % n], bot]; b.face(idx[::-1], cell_at(idx))
        for sx in (-1, 1):   # 目：顔の表面から少し出した小さな板（前が +Z）
            ex, ey, ez = sx * 0.34 * R, cy + 0.02 * R, R * 0.93
            w2, h2, d = 0.075 * R, 0.13 * R, 0.05 * R
            p = [b.add((ex - w2, ey - h2, ez - d)), b.add((ex + w2, ey - h2, ez - d)), b.add((ex + w2, ey + h2, ez - d)), b.add((ex - w2, ey + h2, ez - d)),
                 b.add((ex - w2, ey - h2, ez + d)), b.add((ex + w2, ey - h2, ez + d)), b.add((ex + w2, ey + h2, ez + d)), b.add((ex - w2, ey + h2, ez + d))]
            for q in ((4, 5, 6, 7), (0, 3, 2, 1), (0, 1, 5, 4), (2, 3, 7, 6), (1, 2, 6, 5), (0, 4, 7, 3)): b.face([p[i] for i in q], "eye")
    elif name.startswith("upperarm"):   # 上腕：上は袖（少し太いリング）。袖口はまっすぐな段（同じ高さで太さだけ変える）
        rs, r0, r1 = 0.050 * T * S, 0.050 * T, 0.044 * T
        cuff = L * SLEEVE_LEN
        b.tube([(-OVERLAP - 0.02, rs * 0.85, rs * 0.85, "shirt"), (0.02, rs, rs, "shirt"), (cuff, rs * 0.98, rs * 0.98, "accent"), (cuff, r0 * 0.97, r0 * 0.97, "skin"),
                (L * 0.75, (r0 + r1) * 0.5, (r0 + r1) * 0.5, "skin"), (L + OVERLAP, r1, r1, "skin")], SIDES_LIMB)
    elif name.startswith("forearm"):    # 前腕：手首の近くにリストバンド
        r0, r1 = 0.043 * T, 0.034 * T
        b.tube([(-OVERLAP, r0, r0, "skin"), (L * 0.40, r0 * 1.02, r0 * 0.95, "skin"), (L * 0.74, r1 * 1.05, r1, "wrist"), (L * 0.74, r1 * 1.28, r1 * 1.25, "wrist"),
                (L * 0.92, r1 * 1.28, r1 * 1.25, "skin"), (L * 0.92, r1 * 1.02, r1, "skin"), (L + OVERLAP, r1, r1 * 0.95, "skin")], SIDES_LIMB)
    elif name.startswith("hand"):       # 手：握った形（少し平たい）
        h = HAND_SCALE
        b.tube([(-OVERLAP, 0.034 * h, 0.028 * h, "skin"), (L * 0.45, 0.045 * h, 0.034 * h, "skin"), (L + 0.025, 0.034 * h, 0.026 * h, "skin")], SIDES_LIMB)
    elif name.startswith("thigh"):      # 腿：上はパンツ（腰と同じ色）。裾はまっすぐな段
        rp, r0, r1 = 0.088 * T, 0.068 * T, 0.054 * T
        hem = L * SHORTS_LEN
        b.tube([(-OVERLAP - 0.03, rp * 0.95, rp * 0.95, "pants"), (0.05, rp, rp, "pants"), (hem, rp * 0.97, rp * 0.97, "pants"), (hem, r0 * 0.95, r0 * 0.95, "skin"),
                (L + OVERLAP, r1, r1, "skin")], SIDES_LIMB)
    elif name.startswith("shin"):       # すね：ふくらはぎを少し太く。足首の上は靴下
        r0, r1 = 0.054 * T, 0.040 * T
        sock = L * (1.0 - SOCK_LEN)
        b.tube([(-OVERLAP, r0, r0, "skin"), (L * 0.30, r0 * 1.08, r0 * 1.12, "skin"), (sock, r1 * 1.08, r1 * 1.08, "socks"), (L + OVERLAP, r1 * 1.05, r1 * 1.05, "socks")], SIDES_LIMB)
    elif name.startswith("shoe"):       # 靴：足首から前へ。+Y がつま先、+Z が下。下の面は靴底
        k = SHOE_SCALE
        hw, top, bot = 0.052 * k, -0.035 * k, 0.075   # 横の半分、上の面（足首の少し上）、靴底（足首から下へ）
        prof = [(-0.075 * k, 0.70), (-0.04 * k, 0.95), (0.05, 1.0), (L * 0.85, 1.0), (L + 0.045 * k, 0.78), (L + 0.075 * k, 0.40)]
        rings = []
        for y, wk in prof:   # 断面：角を丸めた四角（8 点）
            hz = (bot - top) * 0.5; cz = (bot + top) * 0.5
            if y > L * 0.85: cz += (bot - top) * 0.10; hz *= 0.80   # つま先は低く
            pts = [(1.0, -0.6), (1.0, 0.6), (0.7, 1.0), (-0.7, 1.0), (-1.0, 0.6), (-1.0, -0.6), (-0.7, -1.0), (0.7, -1.0)]
            rings.append([b.add((hw * wk * px, y, cz + hz * pz)) for px, pz in pts])
        def scell(idx):
            nz = 0.0
            c = Vector((0, 0, 0))
            for i in idx: c += b.v[i]
            c /= len(idx)
            return "sole" if c.z > bot - 0.012 else "shoes"
        b.cap(rings[0], "shoes")
        for i in range(len(rings) - 1):
            a, c = rings[i], rings[i + 1]
            for q in range(8):
                idx = [a[q], a[(q + 1) % 8], c[(q + 1) % 8], c[q]]; b.face(idx, scell(idx))
        b.cap(rings[-1], "shoes")
    return b

# ---- 空の場面から ----
bpy.ops.wm.read_factory_settings(use_empty=True)
scene = bpy.context.scene

# パレットの画像（16x16。Blender の画像は下の行から）
img = bpy.data.images.new("palette", 16, 16, alpha=False)
px = [0.5] * (16 * 16 * 4)
for i in range(16 * 16): px[4 * i + 3] = 1.0
def srgb_to_lin(c):
    c /= 255.0
    return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4
for name, ((col, row), rgb) in PAL.items():
    i = ((15 - row) * 16 + col) * 4
    for k in range(3): px[i + k] = rgb[k] / 255.0
img.pixels = px
img.filepath_raw = os.path.join(OUT, "palette.png"); img.file_format = "PNG"; img.save()
img.pack()

mat = bpy.data.materials.new("palette")
mat.use_nodes = True
nt = mat.node_tree
bsdf = nt.nodes.get("Principled BSDF")
tex = nt.nodes.new("ShaderNodeTexImage"); tex.image = img; tex.interpolation = "Closest"
nt.links.new(tex.outputs["Color"], bsdf.inputs["Base Color"])
bsdf.inputs["Roughness"].default_value = 0.9

# ゲームの座標（x 右・y 上・z 前）→ Blender（x 右・y 奥・z 上）。glTF の書き出し（+Y up）でちょうど元に戻る
C = Matrix(((1, 0, 0), (0, 0, -1), (0, 1, 0)))

for name, hj, tj in PARTS:
    R, L = frame(J[hj], J[tj])
    b = build(name, L)
    me = bpy.data.meshes.new(name)
    me.from_pydata([C @ v for v in b.v], [], [f for f, _ in b.f])
    me.materials.append(mat)
    uv = me.uv_layers.new(name="UVMap")
    for poly, (_, cell) in zip(me.polygons, b.f):
        (col, row), _ = PAL[cell]
        u, v = (col + 0.5) / 16.0, 1.0 - (row + 0.5) / 16.0
        for li in poly.loop_indices: uv.data[li].uv = (u, v)
        poly.use_smooth = False
    ob = bpy.data.objects.new(name, me)
    scene.collection.objects.link(ob)
    M = Matrix.Translation(C @ Vector(J[hj])) @ (C @ R @ C.inverted()).to_4x4()
    ob.matrix_world = M
    ob["len"] = L   # 骨の長さ（ゲームが伸び縮みの基準に使う）
    # 法線を外向きにそろえて、三角にする（書き出しでも三角にする）
    bm = bmesh.new(); bm.from_mesh(me)
    bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=1e-6)
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    bmesh.ops.triangulate(bm, faces=bm.faces)
    bm.to_mesh(me); bm.free()
    for poly in me.polygons: poly.use_smooth = False
    ob.modifiers.new("Triangulate", "TRIANGULATE")

bpy.ops.wm.save_as_mainfile(filepath=os.path.join(OUT, "player.blend"))
bpy.ops.export_scene.gltf(filepath=os.path.join(OUT, "player.glb"), export_format="GLB", export_apply=True, export_yup=True,
                          export_texcoords=True, export_normals=True, export_extras=True, export_animations=False,
                          export_skins=False, export_morph=False, export_materials="EXPORT", export_image_format="AUTO")
print("parts", len(PARTS), "tris", sum(len(o.data.polygons) for o in scene.objects if o.type == "MESH"))
