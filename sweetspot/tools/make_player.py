# SWEET SPOT — 選手のモデル（パーツ式）を作る Blender のスクリプト
#
#   blender --background --python tools/make_player.py      （sweetspot/ で実行。pip の bpy でも動く：python3 tools/make_player.py）
#   できるもの：models/player.glb（ゲームが読む）・models/player.blend（手で直す元）・models/palette.png（色の表）・
#               models/faces/face_01.png〜face_04.png（顔の絵。128x128）
#
# しくみ
#   ・体は部位ごとの別のオブジェクト（頭・首・胴・腰・上腕・前腕・手・腿・すね・靴。左右は .L / .R）。スキニングはしない
#   ・各パーツの原点（ピボット）は親の関節。パーツの向きは「骨の向き」：+Y が子の関節の方（上腕なら肘の方）、
#     +X が選手の右、+Z は X×Y（上向きの骨では前、下向きの骨では後ろ）。ゲームは関節の位置と向きにパーツを置くだけ
#   ・頭のパーツ（head・face・hair_*）は頭の付け根がピボット。face は顔の前に重ねる薄い面で、顔の絵（faces/face_XX.png）を貼る。
#     hair_short / hair_wave / hair_tied / hair_buzz は髪型の候補（細長いくさび形の毛束）。ゲームが選手ごとに 1 つ選ぶ
#   ・関節の位置はゲームのコード（render.c の DrawFigureBody）の立った姿勢と同じ。単位は m、身長 185cm の選手の大きさ
#   ・色は 16x16 のパレット（palette.png）。面ごとに UV をパレットのマスの真ん中に置く。マスの割り当ては PAL の表
#   ・スムーズシェーディング。袖口・パンツの裾・ふたの縁などの段はシャープエッジ（色の境目がにじまない）
import bpy, bmesh, os, math
from mathutils import Vector, Matrix

# ======================= 調整用の数値 =======================
HEAD_SCALE   = 1.10   # 頭の大きさ（今の頭＝半径 10.5cm に対する倍率）
NECK_K       = 0.30   # 首の長さ：首の付け根から頭の中心までのうち、首にする割合
LIMB_THICK   = 0.90   # 手足の太さの倍率（下の基本の太さに掛ける）
SLEEVE_THICK = 1.15   # 袖の太さ（上腕の付け根の太さに対する倍率）
SLEEVE_LEN   = 0.42   # 袖の長さ（上腕の長さに対する割合）
SHORTS_LEN   = 0.50   # パンツの裾（腿の長さに対する割合）
SHORTS_FLARE = 1.10   # パンツの裾の広がり（裾の所の脚の太さに対する倍率。大きいほど広がる）
SOCK_LEN     = 0.26   # 靴下の高さ（すねの長さに対する割合）
SHOE_SCALE   = 1.20   # 靴の大きさの倍率
HAND_SCALE   = 1.05   # 手の大きさの倍率
TORSO_W      = 1.00   # 胴の横幅の倍率
SHOULDER_W   = 1.06   # 肩の横幅（胴の上の方だけに掛ける。少し広め）
OVERLAP      = 0.03   # パーツの端を隣のパーツに重ねる長さ（m）
SIDES        = 16     # 断面の角の数（手足・胴・腰・首・頭）
HEAD_RINGS   = 12     # 頭の縦の分割
# 手足の基本の太さ（半径 m。LIMB_THICK を掛ける）：先細り
ARM_R   = (0.052, 0.046, 0.040)          # 上腕：付け根・真ん中・肘
FORE_R  = (0.042, 0.044, 0.038, 0.030)   # 前腕：肘・少し下のふくらみ・真ん中・手首
THIGH_R = (0.082, 0.070, 0.050)          # 腿：付け根・真ん中・膝
SHIN_R  = (0.050, 0.058, 0.044, 0.031)   # すね：膝・ふくらはぎ・下の方・足首
FACE_TEX = 128        # 顔の絵の大きさ（ピクセル）
# パレットのマス（列, 行）と初めの色（ゲームが選手ごとに上書きする）
PAL = {
    "skin":     ((0, 0), (226, 178, 135)),
    "shirt":    ((1, 0), (235, 235, 240)),
    "accent":   ((2, 0), (40, 90, 200)),     # シャツの差し色（襟・裾・胸のライン・袖口）
    "pants":    ((3, 0), (30, 35, 60)),
    "shoes":    ((4, 0), (250, 250, 252)),
    "socks":    ((5, 0), (245, 245, 245)),
    "hair":     ((6, 0), (70, 50, 35)),
    "wrist":    ((7, 0), (230, 60, 60)),     # リストバンド・髪を結ぶゴム
    "eye":      ((8, 0), (30, 25, 25)),
    "sole":     ((9, 0), (120, 120, 130)),
}
# ============================================================

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.abspath(os.path.join(HERE, "..", "models"))
os.makedirs(os.path.join(OUT, "faces"), exist_ok=True)

# ---- ゲームの立った姿勢の関節（体の座標：x 右・y 上・z 前、m）。render.c の DrawFigureBody（腰の高さ 0.93）と同じ ----
J = {"pelvis": (0.0, 0.95, 0.0), "waist": (0.0, 1.13, 0.0), "neck": (0.0, 1.51, 0.0), "headc": (0.0, 1.72, 0.01)}
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

HAIRS = ["hair_short", "hair_wave", "hair_tied", "hair_buzz"]
PARTS = [("pelvis", "pelvis", "waist"), ("torso", "waist", "neck"), ("neck", "neck", "headbase"), ("head", "headbase", "headtop"), ("face", "headbase", "headtop")]
PARTS += [(h, "headbase", "headtop") for h in HAIRS]
for s in (".R", ".L"):
    PARTS += [("upperarm" + s, "shoulder" + s, "elbow" + s), ("forearm" + s, "elbow" + s, "wrist" + s), ("hand" + s, "wrist" + s, "handtip" + s),
              ("thigh" + s, "hip" + s, "knee" + s), ("shin" + s, "knee" + s, "ankle" + s), ("shoe" + s, "ankle" + s, "toe" + s)]

def frame(h, t):
    """ゲームの BodyFrame() と同じ：Y = 頭→しっぽ、X = 右（+x）を骨に直角に、Z = X×Y。列が軸の 3x3"""
    h, t = Vector(h), Vector(t)
    y = (t - h); L = y.length; y.normalize()
    x = Vector((1, 0, 0)); x = x - y * x.dot(y); x.normalize()
    z = x.cross(y)
    return Matrix(((x.x, y.x, z.x), (x.y, y.y, z.y), (x.z, y.z, z.z))), L

def lerp(a, b, t): return a + (b - a) * t
def smooth(t): t = max(0.0, min(1.0, t)); return t * t * (3 - 2 * t)

# ---- 形の部品（パーツの座標で作る）----
class Builder:
    def __init__(self): self.v = []; self.f = []; self.sharp = set(); self.uv = {}   # f = (頂点の番号の並び, マス)、uv = 面の番号 → 角ごとの UV（顔の絵）
    def add(self, p): self.v.append(Vector(p)); return len(self.v) - 1
    def face(self, idx, cell): self.f.append((list(idx), cell)); return len(self.f) - 1
    def ring(self, y, rx, rz, n=SIDES, cx=0.0, cz=0.0, rot=None):
        rot = math.pi / n if rot is None else rot
        return [self.add((cx + rx * math.cos(2 * math.pi * i / n + rot), y, cz + rz * math.sin(2 * math.pi * i / n + rot))) for i in range(n)]
    def mark(self, r):   # 輪の辺をシャープに
        for i in range(len(r)): self.sharp.add(tuple(sorted((r[i], r[(i + 1) % len(r)]))))
    def bridge(self, a, b, cell):
        n = len(a)
        for i in range(n): self.face([a[i], a[(i + 1) % n], b[(i + 1) % n], b[i]], cell)
    def cap(self, r, cell):
        c = Vector((0, 0, 0))
        for i in r: c += self.v[i]
        c /= len(r); ci = self.add(c)
        for i in range(len(r)): self.face([r[i], r[(i + 1) % len(r)], ci], cell)
        self.mark(r)
    def tube(self, prof, n=SIDES):
        """prof = [(y, rx, rz, マス)]。区間の色は下の端の輪のマス。同じ高さの輪が続くところ（袖口・裾）は段にして、縁をシャープに"""
        rings = [self.ring(y, rx, rz, n) for y, rx, rz, _ in prof]
        self.cap(rings[0], prof[0][3])
        for i in range(len(rings) - 1):
            self.bridge(rings[i], rings[i + 1], prof[i][3])
            if abs(prof[i][0] - prof[i + 1][0]) < 1e-6: self.mark(rings[i]); self.mark(rings[i + 1])
            elif prof[i][3] != prof[i + 1][3] and i + 1 < len(rings) - 1: pass
        self.cap(rings[-1], prof[-1][3])
        return rings

# ---- 頭の形：球を、あごを細く少し前へ、頬をわずかに削り、後頭部を少し出す ----
R_HEAD = 0.105 * HEAD_SCALE
HC_Y = R_HEAD * 0.95   # 頭の中心の高さ（頭の付け根から）
def head_shape(d, rho=1.0):
    """単位球の向き d（x 右・y 上・z 前）→ 頭の表面の点（頭の付け根が原点）。rho > 1 は表面から外（髪）"""
    x, y, z = d.x, d.y, d.z
    x *= 0.88; y *= 1.08                                   # 縦に少し長く、横に少し細く
    t = smooth((-d.y - 0.15) / 0.85)                       # あご：下ほど細く、前へ
    x *= 1.0 - 0.42 * t
    if d.z > 0: z += 0.16 * t * d.z
    else: z *= 1.0 - 0.45 * t                              # あごの後ろ（首）は細く
    ck = smooth((d.z - 0.25) / 0.4) * smooth((0.25 - abs(d.y + 0.25)) / 0.25)   # 頬：前の中ほどを少し削る
    x *= 1.0 - 0.07 * ck
    oc = smooth((-d.z - 0.3) / 0.5) * smooth((0.55 - abs(d.y - 0.12)) / 0.5)    # 後頭部：少し出す
    z -= 0.13 * oc
    if d.z > 0.55 and d.y > -0.5: z *= 0.96                # 顔は少し平らに
    return Vector((x * R_HEAD * rho, HC_Y + y * R_HEAD * rho, z * R_HEAD * rho))

def sphere_dir(i, j, n=SIDES, m=HEAD_RINGS):
    th = math.pi * j / m; ph = 2 * math.pi * i / n + math.pi / n
    return Vector((math.sin(th) * math.sin(ph), math.cos(th), math.sin(th) * math.cos(ph)))

def build_head(b):
    n, m = SIDES, HEAD_RINGS
    rings = [[b.add(head_shape(sphere_dir(i, j))) for i in range(n)] for j in range(1, m)]
    top = b.add(head_shape(Vector((0, 1, 0)))); bot = b.add(head_shape(Vector((0, -1, 0))))
    def cell(idx):
        c = Vector((0, 0, 0))
        for i in idx: c += b.v[i]
        c /= len(idx)
        u = (c.y - HC_Y) / R_HEAD; zf = c.z / R_HEAD; xf = abs(c.x) / R_HEAD
        # 髪の生え際（頭の皮膚を髪の色にする。形は毛束が作る）：額の上、こめかみ、後ろ
        if u > 0.52 or (zf < 0.15 and u > 0.18) or (zf < -0.20 and u > -0.62) or (xf > 0.55 and zf < 0.40 and u > -0.12): return "hair"
        return "skin"
    for i in range(n): idx = [top, rings[0][(i + 1) % n], rings[0][i]]; b.face(idx, cell(idx))
    for k in range(m - 2):
        for i in range(n):
            idx = [rings[k][i], rings[k][(i + 1) % n], rings[k + 1][(i + 1) % n], rings[k + 1][i]]; b.face(idx, cell(idx))
    for i in range(n): idx = [rings[-1][i], rings[-1][(i + 1) % n], bot]; b.face(idx, cell(idx))
    # 鼻：顔の真ん中の小さな三角柱（上は眉間、下は鼻先）
    fz = lambda y: head_shape(Vector((0, y, math.sqrt(max(1 - y * y, 0))))).z
    yt, yb = 0.02, -0.38
    zt, zb = fz(yt) - 0.004, fz(yb) - 0.004
    w0, w1, h0, h1 = 0.06 * R_HEAD, 0.12 * R_HEAD, 0.03 * R_HEAD, 0.20 * R_HEAD
    Y = lambda u: HC_Y + u * R_HEAD * 1.08
    tl, tr, tc = b.add((-w0, Y(yt), zt)), b.add((w0, Y(yt), zt)), b.add((0, Y(yt), zt + h0))
    bl, br, bc = b.add((-w1, Y(yb), zb)), b.add((w1, Y(yb), zb)), b.add((0, Y(yb) - 0.02 * R_HEAD, zb + h1))
    b.face([tl, tc, bc, bl], "skin"); b.face([tc, tr, br, bc], "skin"); b.face([bl, bc, br], "skin"); b.face([tl, tr, tc], "skin")
    b.mark([bl, bc, br])

# 顔の絵を貼る面：頭の前の面を少し外に出して重ねる。UV は正面からの投影
FACE_TOP, FACE_BOT, FACE_HALFW = 0.62, -1.0, 0.78   # 絵の上の端・下の端（頭の中心からの高さ。頭の半径の単位）、横の半分
def face_uv(p):
    u = 0.5 + (p.x / R_HEAD) / (2 * FACE_HALFW)
    vt = (FACE_TOP - (p.y - HC_Y) / R_HEAD) / (FACE_TOP - FACE_BOT)   # 絵の上から下へ 0→1
    return (u, 1.0 - vt)
def build_face(b):
    n, m = SIDES, HEAD_RINGS
    grid = {}
    for j in range(1, m):
        for i in range(n):
            d = sphere_dir(i, j)
            p = head_shape(d, 1.0)
            grid[(i, j)] = p
    def inside(p): u, v = face_uv(p); return 0.02 < u < 0.98 and 0.02 < v < 0.98 and p.z > 0.25 * R_HEAD
    shared = {}
    for j in range(1, m - 1):
        for i in range(n):
            q = [(i, j), ((i + 1) % n, j), ((i + 1) % n, j + 1), (i, j + 1)]
            ps = [grid[k] for k in q]
            if not all(inside(p) for p in ps): continue
            idx = []
            for kk, p in zip(q, ps):
                if kk not in shared:
                    nrm = (p - Vector((0, HC_Y, 0))).normalized()
                    shared[kk] = b.add(p + nrm * 0.003)
                idx.append(shared[kk])
            fi = b.face(idx, "skin")
            b.uv[fi] = [face_uv(b.v[k]) for k in idx]

# ---- 髪：細長いくさび形の毛束 ----
def dir_from(el, az):   # 仰角（水平から上へ・度）と方位（前 0°、右へプラス・度）→ 単位ベクトル
    e, a = math.radians(el), math.radians(az)
    return Vector((math.cos(e) * math.sin(a), math.sin(e), math.cos(e) * math.cos(a)))
def tuft(b, el, az, head_az, length, width, lift, thick=0.35, curl=0.0, cell="hair", segs=4, sink=0.04):
    """根元は頭の表面（仰角 el・方位 az）。head_az = 伸びる向き（表面に沿って、北＝上へ 0°、右回り。度）。
       length は弧の長さ（頭の半径の単位）。lift：先が浮く量。curl：横へうねる量"""
    d0 = dir_from(el, az)
    north = Vector((0, 1, 0)) - d0 * d0.y
    if north.length < 1e-4: north = Vector((0, 0, -1))
    north.normalize(); east = north.cross(d0) * -1.0
    ha = math.radians(head_az)
    tdir = (north * math.cos(ha) + east * math.sin(ha)).normalized()
    axis = d0.cross(tdir).normalized()
    pts, sides = [], []
    for k in range(segs + 1):
        s = k / segs
        ang = length * s
        d = (Matrix.Rotation(ang, 3, axis) @ d0).normalized()
        if curl: d = (Matrix.Rotation(curl * math.sin(math.pi * 2 * s), 3, d) @ d).normalized()
        rho = 1.0 - sink + lift * s * s
        p = head_shape(d, rho)
        nrm = (p - Vector((0, HC_Y, 0))).normalized()
        tan = (Matrix.Rotation(ang, 3, axis) @ tdir).normalized()
        side = tan.cross(nrm).normalized()
        w = width * R_HEAD * (1.0 - s) ** 0.8
        t = width * thick * R_HEAD * (1.0 - s) ** 0.8
        pts.append((p, nrm, side, w, t))
    # 断面はひし形（横に広く、厚みは薄い）。先は 1 点
    rings = []
    for p, nrm, side, w, t in pts[:-1]:
        rings.append([b.add(p + side * w), b.add(p + nrm * t), b.add(p - side * w), b.add(p - nrm * t * 0.5)])
    tip = b.add(pts[-1][0])
    b.cap(rings[0], cell)
    for i in range(len(rings) - 1): b.bridge(rings[i], rings[i + 1], cell)
    for i in range(4): b.face([rings[-1][i], rings[-1][(i + 1) % 4], tip], cell)

def ponytail(b, cell="hair"):
    """後ろで束ねた髪：後頭部の結び目から下へ垂れるくさび（とゴム）"""
    root = head_shape(dir_from(-5, 180), 1.02)
    b2 = Builder()
    prof = [(0.0, 0.040, 0.034, "wrist"), (0.018, 0.040, 0.034, "wrist"), (0.018, 0.034, 0.030, cell), (0.09, 0.045, 0.034, cell), (0.20, 0.034, 0.026, cell), (0.27, 0.012, 0.010, cell)]
    b2.tube(prof, 10)
    R = Matrix.Rotation(math.radians(160), 3, "X")   # 下へ、少し後ろへ
    base = len(b.v)
    for v in b2.v: b.v.append(root + R @ v)
    for f, c in b2.f: b.f.append(([i + base for i in f], c))
    for e in b2.sharp: b.sharp.add((e[0] + base, e[1] + base))

def build_hair(b, style):
    if style == "hair_short":   # 短髪：前髪・トップ・サイド・襟足に 15 束
        for az in (-42, -14, 14, 42):  tuft(b, 50, az, 180 + az * 0.4, 0.62, 0.34, 0.18)                     # 前髪：前へ下りる
        for az in (-70, -25, 25, 70):  tuft(b, 72, az, -10 + az * 0.5, 0.75, 0.38, 0.22)                     # トップ：後ろへ
        for az in (150, 180, 210):     tuft(b, 30, az, 180, 0.60, 0.36, 0.12)                                 # 後ろ・襟足：下へ
        for sx in (-1, 1):
            tuft(b, 22, sx * 88, 180 - sx * 25, 0.45, 0.30, 0.08)                                            # サイド：下へ少し後ろ
            tuft(b, 22, sx * 118, 180 - sx * 15, 0.48, 0.30, 0.08)
    elif style == "hair_wave":  # ウェーブ：長めで、うねって、浮く
        for az in (-48, -18, 12, 40):  tuft(b, 52, az, 185 + az * 0.5, 0.85, 0.40, 0.34, curl=0.5)
        for az in (-80, -35, 0, 35, 80): tuft(b, 74, az, -15 + az * 0.6, 0.95, 0.42, 0.40, curl=0.6)
        for az in (145, 180, 215):     tuft(b, 28, az, 180, 0.80, 0.40, 0.26, curl=0.5)
        for sx in (-1, 1):
            tuft(b, 20, sx * 90, 180 - sx * 30, 0.65, 0.34, 0.22, curl=0.5)
            tuft(b, 22, sx * 122, 180 - sx * 20, 0.65, 0.34, 0.22, curl=0.5)
    elif style == "hair_tied":  # 長めを後ろで束ねる：前・上・横の毛束は後ろへなでつけ、後ろで結んで垂らす
        for az in (-40, -12, 15, 42):  tuft(b, 48, az, 0, 1.25, 0.36, 0.05, thick=0.28)
        for az in (-75, -30, 30, 75):  tuft(b, 70, az, 0, 1.05, 0.38, 0.05, thick=0.28)
        for sx in (-1, 1):
            tuft(b, 25, sx * 85, sx * 75, 1.00, 0.34, 0.04, thick=0.28)
            tuft(b, 28, sx * 120, sx * 40, 0.80, 0.34, 0.04, thick=0.28)
        tuft(b, 10, 180, 0, 0.30, 0.36, 0.02)
        ponytail(b)
    elif style == "hair_buzz":  # 坊主寄り：頭の皮膚の色（髪の色）に、ごく短い毛束を少し
        for el, az in ((70, -40), (70, 40), (80, 0), (45, -60), (45, 60), (35, 180), (55, 150), (55, 210)):
            tuft(b, el, az, 180 if az == 180 else 0, 0.30, 0.30, 0.02, thick=0.25, sink=0.03)

def build(name, L):
    T, S = LIMB_THICK, SLEEVE_THICK
    b = Builder()
    if name == "pelvis":   # 腰（パンツそのもの）。ピボットは腰の中心、+Y が上（ウエスト）
        w = TORSO_W
        b.tube([(-0.15, 0.10 * w, 0.075, "pants"), (-0.10, 0.150 * w, 0.100, "pants"), (0.03, 0.160 * w, 0.106, "pants"), (L + OVERLAP, 0.145 * w, 0.098, "pants")])
    elif name == "torso":  # 胴（シャツそのもの）。裾はパンツに少しかぶせる。襟・裾・胸のラインは差し色。肩は少し広め
        w, sw = TORSO_W, TORSO_W * SHOULDER_W
        b.tube([(-0.07, 0.168 * w, 0.112, "accent"), (-0.05, 0.166 * w, 0.111, "shirt"), (0.15, 0.180 * w, 0.114, "accent"),
                (0.18, 0.184 * w, 0.115, "shirt"), (0.30, 0.205 * sw, 0.110, "shirt"), (L - 0.01, 0.13 * sw, 0.075, "accent"), (L + 0.025, 0.065, 0.052, "accent")])
    elif name == "neck":
        r = 0.046
        b.tube([(-OVERLAP, r * 1.05, r, "skin"), (L + OVERLAP * 1.5, r, r * 0.95, "skin")])
    elif name == "head":
        build_head(b)
    elif name == "face":
        build_face(b)
    elif name in HAIRS:
        build_hair(b, name)
    elif name.startswith("upperarm"):   # 上腕：肘に向かって細く。上は袖（少し太いリング）。袖口はまっすぐな段
        r0, rm, r1 = (r * T for r in ARM_R)
        rs = r0 * S
        cuff = L * SLEEVE_LEN
        rc = lerp(r0, rm, cuff / (L * 0.5))
        b.tube([(-OVERLAP - 0.02, rs * 0.88, rs * 0.88, "shirt"), (0.02, rs, rs, "shirt"), (cuff - 0.02, rs * 0.97, rs * 0.97, "accent"), (cuff, rs * 0.97, rs * 0.97, "accent"),
                (cuff, rc, rc, "skin"), (L * 0.75, lerp(rm, r1, 0.5), lerp(rm, r1, 0.5), "skin"), (L + OVERLAP, r1, r1, "skin")])
    elif name.startswith("forearm"):    # 前腕：肘の少し下がふくらみ、手首へ細く。手首の近くにリストバンド
        r0, rb, rm, r1 = (r * T for r in FORE_R)
        b.tube([(-OVERLAP, r0, r0, "skin"), (L * 0.22, rb, rb * 0.95, "skin"), (L * 0.55, rm, rm * 0.92, "skin"), (L * 0.74, r1 * 1.08, r1, "skin"),
                (L * 0.74, r1 * 1.30, r1 * 1.25, "wrist"), (L * 0.92, r1 * 1.30, r1 * 1.25, "wrist"), (L * 0.92, r1 * 1.04, r1, "skin"), (L + OVERLAP, r1, r1 * 0.95, "skin")])
    elif name.startswith("hand"):       # 手：握った形（少し平たい）
        h = HAND_SCALE * T
        b.tube([(-OVERLAP, 0.034 * h, 0.028 * h, "skin"), (L * 0.45, 0.046 * h, 0.034 * h, "skin"), (L + 0.025, 0.034 * h, 0.026 * h, "skin")])
    elif name.startswith("thigh"):      # 腿：付け根が太く膝へ細く。上はパンツ（腰と同じ色）。裾はまっすぐな段で、広げすぎない
        r0, rm, r1 = (r * T for r in THIGH_R)
        hem = L * SHORTS_LEN
        rh = lerp(r0, rm, hem / (L * 0.5)) if hem <= L * 0.5 else lerp(rm, r1, (hem - L * 0.5) / (L * 0.5))
        rp = rh * SHORTS_FLARE
        b.tube([(-OVERLAP - 0.03, r0 * 1.06, r0 * 1.06, "pants"), (0.04, r0 * 1.06, r0 * 1.06, "pants"), (hem, rp, rp, "pants"), (hem, rh * 0.98, rh * 0.98, "skin"),
                (L * 0.78, lerp(rm, r1, 0.55), lerp(rm, r1, 0.55), "skin"), (L + OVERLAP, r1, r1, "skin")])
    elif name.startswith("shin"):       # すね：ふくらはぎ（後ろ）を膨らませて、足首へ細く。足首の上は靴下
        r0, rc, rl, r1 = (r * T for r in SHIN_R)
        sock = L * (1.0 - SOCK_LEN)
        # ふくらはぎは後ろ（下向きの骨では +Z が後ろ）に寄せる
        b.tube([(-OVERLAP, r0, r0, "skin"), (L * 0.28, rc * 0.92, rc, "skin"), (L * 0.55, rl, rl, "skin"), (sock, r1 * 1.10, r1 * 1.10, "socks"), (L + OVERLAP, r1 * 1.08, r1 * 1.08, "socks")])
        for v in b.v:   # ふくらはぎ：後ろ側を少し出す
            t = v.y / L
            if v.z > 0: v.z *= 1.0 + 0.25 * smooth(1 - abs(t - 0.3) / 0.3)
    elif name.startswith("shoe"):       # 靴：足首から前へ。+Y がつま先、+Z が下。下の面は靴底
        k = SHOE_SCALE
        hw, top, bot = 0.048 * k, -0.032 * k, 0.072
        prof = [(-0.07 * k, 0.70), (-0.04 * k, 0.95), (0.05, 1.0), (L * 0.85, 1.0), (L + 0.04 * k, 0.80), (L + 0.07 * k, 0.42)]
        rings = []
        for y, wk in prof:   # 断面：角を丸めた四角（16 点の超楕円）
            hz = (bot - top) * 0.5; cz = (bot + top) * 0.5
            if y > L * 0.85: cz += (bot - top) * 0.10; hz *= 0.80
            r = []
            for i in range(SIDES):
                a = 2 * math.pi * i / SIDES + math.pi / SIDES
                cx_, cz_ = math.cos(a), math.sin(a)
                px = math.copysign(abs(cx_) ** 0.5, cx_); pz = math.copysign(abs(cz_) ** 0.5, cz_)
                r.append(b.add((hw * wk * px, y, cz + hz * pz)))
            rings.append(r)
        def scell(idx):
            c = Vector((0, 0, 0))
            for i in idx: c += b.v[i]
            c /= len(idx)
            return "sole" if c.z > bot - 0.014 else "shoes"
        b.cap(rings[0], "shoes")
        for i in range(len(rings) - 1):
            a, c = rings[i], rings[i + 1]
            for q in range(SIDES):
                idx = [a[q], a[(q + 1) % SIDES], c[(q + 1) % SIDES], c[q]]; b.face(idx, scell(idx))
        b.cap(rings[-1], "shoes")
    return b

# ---- 顔の絵（128x128、背景は透明）：白目＋黒目＋太めの眉（少しつり上げる）＋短い線の口 ----
FACES = {   # 目の横幅・高さ、黒目の半径、眉の角度（外を上げる・度）・太さ、口の幅、まつげ（ピクセル）
    "face_01": dict(ew=10.5, eh=6.2, ir=4.4, brow=14, bt=4.4, mw=8, lash=False),   # 真剣（男子の基本）
    "face_02": dict(ew=10.0, eh=4.6, ir=3.8, brow=8, bt=5.0, mw=9, lash=False),    # 細い目
    "face_03": dict(ew=11.0, eh=6.8, ir=4.8, brow=10, bt=3.0, mw=7, lash=True),    # 女子の基本：まつげ
    "face_04": dict(ew=10.0, eh=5.6, ir=4.2, brow=4, bt=4.0, mw=10, lash=False),   # 落ち着いた顔
}
def make_face_png(name, prm):
    N = FACE_TEX
    px = [0.0] * (N * N * 4)
    def put(x, y, c, a=1.0):
        if 0 <= x < N and 0 <= y < N:
            i = ((N - 1 - y) * N + x) * 4   # Blender の画像は下の行から
            for k in range(3): px[i + k] = px[i + k] * (1 - a) + c[k] * a
            px[i + 3] = max(px[i + 3], a)
    def to_px(xh, yh):   # 頭の座標（頭の半径の単位、中心から）→ 絵のピクセル
        u = 0.5 + xh / (2 * FACE_HALFW); vt = (FACE_TOP - yh) / (FACE_TOP - FACE_BOT)
        return u * N, vt * N
    def ellipse(cx, cy, rx, ry, c, edge=None):
        for y in range(int(cy - ry - 2), int(cy + ry + 3)):
            for x in range(int(cx - rx - 2), int(cx + rx + 3)):
                d = ((x + 0.5 - cx) / rx) ** 2 + ((y + 0.5 - cy) / ry) ** 2
                if d <= 1.0:
                    put(x, y, c, 1.0)
                    if edge and d > 0.62: put(x, y, edge, 0.85)
    def line(x0, y0, x1, y1, th, c):
        L = max(abs(x1 - x0), abs(y1 - y0), 1)
        for s in range(int(L * 2) + 1):
            t = s / (L * 2); x = x0 + (x1 - x0) * t; y = y0 + (y1 - y0) * t
            for yy in range(int(y - th), int(y + th) + 1):
                for xx in range(int(x - th), int(x + th) + 1):
                    if (xx + 0.5 - x) ** 2 + (yy + 0.5 - y) ** 2 <= th * th: put(xx, yy, c, 1.0)
    dark, white, iris, brow, mouth = (0.10, 0.07, 0.06), (0.97, 0.97, 0.95), (0.16, 0.10, 0.07), (0.16, 0.11, 0.08), (0.45, 0.20, 0.18)
    for sx in (-1, 1):
        ex, ey = to_px(sx * 0.36, 0.0)
        ellipse(ex, ey, prm["ew"], prm["eh"], white, edge=dark)                          # 白目（縁は濃く）
        ellipse(ex - sx * 0.8, ey - 0.3, prm["ir"], prm["ir"] * 1.05, iris)              # 黒目（少し内側）
        ellipse(ex - sx * 0.8 + 1.0, ey - 1.3, 1.0, 1.0, white)                          # 目の光
        line(ex - prm["ew"], ey - prm["eh"] + 0.5, ex + prm["ew"], ey - prm["eh"] + 0.5, 1.0 if not prm["lash"] else 1.6, dark)   # 上まぶた
        bx, by = to_px(sx * 0.36, 0.30)
        a = math.radians(prm["brow"])
        hl = 9.5
        # 眉：内側は下、外側は上（つり上げる）。太めの棒
        line(bx - sx * hl * math.cos(a), by + hl * math.sin(a), bx + sx * hl * math.cos(a), by - hl * math.sin(a), prm["bt"] * 0.5 + 0.4, brow)
    mx, my = to_px(0.0, -0.62)
    line(mx - prm["mw"], my, mx + prm["mw"], my, 1.3, mouth)                              # 口：短い線
    img = bpy.data.images.new(name, N, N, alpha=True)
    img.pixels = px
    img.filepath_raw = os.path.join(OUT, "faces", name + ".png"); img.file_format = "PNG"; img.save()
    return img

# ---- 空の場面から ----
bpy.ops.wm.read_factory_settings(use_empty=True)
scene = bpy.context.scene

img = bpy.data.images.new("palette", 16, 16, alpha=False)
px = [0.5] * (16 * 16 * 4)
for i in range(16 * 16): px[4 * i + 3] = 1.0
for name, ((col, row), rgb) in PAL.items():
    i = ((15 - row) * 16 + col) * 4
    for k in range(3): px[i + k] = rgb[k] / 255.0
img.pixels = px
img.filepath_raw = os.path.join(OUT, "palette.png"); img.file_format = "PNG"; img.save()
img.pack()
face_imgs = {k: make_face_png(k, v) for k, v in FACES.items()}
face_imgs["face_01"].pack()

def make_mat(name, image, blend=False):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    nt = m.node_tree
    bsdf = nt.nodes.get("Principled BSDF")
    tex = nt.nodes.new("ShaderNodeTexImage"); tex.image = image; tex.interpolation = "Closest" if not blend else "Linear"
    nt.links.new(tex.outputs["Color"], bsdf.inputs["Base Color"])
    if blend: nt.links.new(tex.outputs["Alpha"], bsdf.inputs["Alpha"])
    bsdf.inputs["Roughness"].default_value = 0.9
    return m
mat = make_mat("palette", img)
mat_face = make_mat("face", face_imgs["face_01"], blend=True)

C = Matrix(((1, 0, 0), (0, 0, -1), (0, 1, 0)))   # ゲーム（x 右・y 上・z 前）→ Blender（x 右・y 奥・z 上）

for name, hj, tj in PARTS:
    R, L = frame(J[hj], J[tj])
    b = build(name, L)
    me = bpy.data.meshes.new(name)
    me.from_pydata([C @ v for v in b.v], [], [f for f, _ in b.f])
    me.materials.append(mat_face if name == "face" else mat)
    uv = me.uv_layers.new(name="UVMap")
    for fi, (poly, (_, cell)) in enumerate(zip(me.polygons, b.f)):
        if fi in b.uv:
            for k, li in enumerate(poly.loop_indices): uv.data[li].uv = b.uv[fi][k]
        else:
            (col, row), _ = PAL[cell]
            u, v = (col + 0.5) / 16.0, 1.0 - (row + 0.5) / 16.0
            for li in poly.loop_indices: uv.data[li].uv = (u, v)
        poly.use_smooth = True
    # シャープエッジ（段・ふた）
    if b.sharp:
        attr = me.attributes.get("sharp_edge") or me.attributes.new("sharp_edge", "BOOLEAN", "EDGE")
        for e in me.edges:
            if tuple(sorted(e.vertices)) in b.sharp: attr.data[e.index].value = True
    ob = bpy.data.objects.new(name, me)
    scene.collection.objects.link(ob)
    ob.matrix_world = Matrix.Translation(C @ Vector(J[hj])) @ (C @ R @ C.inverted()).to_4x4()
    ob["len"] = L
    bm = bmesh.new(); bm.from_mesh(me)
    if name != "face": bmesh.ops.recalc_face_normals(bm, faces=bm.faces)   # 法線を外向きに
    if name in ("head", "face"):   # 頭・顔の絵・鼻（開いた形）：頭の中心から外を向くように
        hc = C @ Vector((0, HC_Y, 0))
        for f in bm.faces:
            if f.normal.dot(f.calc_center_median() - hc) < 0: f.normal_flip()
    bmesh.ops.triangulate(bm, faces=bm.faces)
    bm.to_mesh(me); bm.free()
    for poly in me.polygons: poly.use_smooth = True
    if name in HAIRS[1:]: ob.hide_set(True) if hasattr(ob, "hide_set") else None   # Blender では短髪だけ見せる（書き出しには全部入る）

bpy.ops.wm.save_as_mainfile(filepath=os.path.join(OUT, "player.blend"))
for ob in scene.objects:
    try: ob.hide_set(False)
    except Exception: pass
bpy.ops.export_scene.gltf(filepath=os.path.join(OUT, "player.glb"), export_format="GLB", export_apply=True, export_yup=True,
                          export_texcoords=True, export_normals=True, export_extras=True, export_animations=False,
                          export_skins=False, export_morph=False, export_materials="EXPORT", export_image_format="AUTO")
print("parts", len(PARTS), "tris", sum(len(o.data.polygons) for o in scene.objects if o.type == "MESH"))
