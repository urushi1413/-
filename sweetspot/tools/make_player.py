# SWEET SPOT — 選手のモデル（パーツ式）を作る Blender のスクリプト
#
#   blender --background --python tools/make_player.py      （sweetspot/ で実行。pip の bpy でも動く：python3 tools/make_player.py）
#   できるもの：models/player.glb（ゲームが読む）・models/player.blend（手で直す元）・models/palette.png（色の表）・
#               models/faces/face_01.png〜face_04.png（顔の絵）・models/faces/beard_1.png（剃り跡）・beard_2.png（ひげ）
#
# しくみ
#   ・体は部位ごとの別のオブジェクト（頭・首・胴・腰・上腕・前腕・手・腿・すね・靴。左右は .L / .R）。スキニングはしない
#   ・各パーツの原点（ピボット）は親の関節。パーツの向きは「骨の向き」：+Y が子の関節の方（上腕なら肘の方）、
#     +X が選手の右、+Z は X×Y（上向きの骨では前、下向きの骨では後ろ）。ゲームは関節の位置と向きにパーツを置くだけ
#   ・関節（肩・肘・股関節・膝・手首・足首）では、隣り合うパーツの端を「関節を中心にした同じ半径・同じ色の半球」で閉じる。
#     2 つの半球が合わさって関節の球になるので、どの角度に曲げても継ぎ目が見えない
#   ・頭のパーツ（head・face・hair_*）は頭の付け根がピボット。face は顔の前に重ねる薄い面で、顔の絵を貼る。
#     髪は「頭の形に沿ったベースの髪」＋「同じ流れの大きめの毛束 5〜8 個」。hair_short / wave / tied / buzz の 4 種から、ゲームが選手ごとに選ぶ
#   ・関節の位置はゲームのコード（render.c の DrawFigureBody）の立った姿勢と同じ。単位は m、身長 185cm の選手の大きさ
#   ・色は 16x16 のパレット（palette.png）。面ごとに UV をパレットのマスの真ん中に置く。マスの割り当ては PAL の表
#   ・スムーズシェーディング。袖口・パンツの裾などの段はシャープエッジ（色の境目がにじまない）
import bpy, bmesh, os, math
from mathutils import Vector, Matrix

# ======================= 調整用の数値 =======================
HEAD_SCALE   = 1.10   # 頭の大きさ（今の頭＝半径 10.5cm に対する倍率）
FACE_LONG    = 1.14   # 顔の縦の長さ（下半分。大きいほど面長＝大人びる）
JAW_W        = 0.88   # あごの角（エラ）の幅（頬の幅に対する割合。大きいほど角ばる）
BROW_RIDGE   = 0.07   # 眉骨の張り出し（頭の半径に対する割合）
NECK_K       = 0.30   # 首の長さ：首の付け根から頭の中心までのうち、首にする割合
NECK_R       = 0.060  # 首の太さ（半径 m）
LIMB_THICK   = 0.90   # 手足の太さの倍率（下の基本の太さに掛ける）
SLEEVE_K     = 1.22   # 袖（＝三角筋を覆う丸み）の太さ：上腕の付け根の太さに対する倍率
SLEEVE_LEN   = 0.40   # 袖の長さ（上腕の長さに対する割合）
SHORTS_LEN   = 0.50   # パンツの裾（腿の長さに対する割合）
SHORTS_FLARE = 1.08   # パンツの裾の広がり（裾の所の脚の太さに対する倍率）
SOCK_LEN     = 0.26   # 靴下の高さ（すねの長さに対する割合）
SHOE_SCALE   = 1.20   # 靴の大きさの倍率
HAND_SCALE   = 1.05   # 手の大きさの倍率
TORSO_W      = 1.00   # 胴の横幅の倍率
SHOULDER_W   = 1.06   # 肩の横幅（胴の上の方だけ。少し広め）
SIDES        = 16     # 断面の角の数（手足・胴・腰・首・頭）
DOME_RINGS   = 4      # 関節の半球の縦の分割
HEAD_RINGS   = 14     # 頭の縦の分割
HAIR_THICK   = 0.055  # ベースの髪の厚み（頭の半径に対する割合）
# 手足の基本の太さ（半径 m。LIMB_THICK を掛ける）：先細り。関節の半径は隣のパーツと同じ値を使う
R_HIP, R_KNEE, R_ANKLE = 0.080, 0.050, 0.031     # 股関節（パンツの色）・膝・足首
R_SHOULDER, R_ELBOW, R_WRIST = 0.050, 0.039, 0.030
ARM_MID, FORE_BULGE, THIGH_MID, CALF = 0.045, 0.044, 0.068, 0.058
FACE_TEX = 256        # 顔の絵の大きさ（ピクセル。128 では近くで見ると粗かった）
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
    "nose":     ((10, 0), (232, 170, 130)),  # 鼻：肌にごく薄い赤み（ゲームが肌の色から作る）
}
IRIS_KEY = (1.0, 0.0, 1.0)   # 顔の絵の瞳のところの色（ゲームが選手の瞳の色に置き換える）
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
    def __init__(self): self.v = []; self.f = []; self.sharp = set(); self.uv = {}
    def add(self, p): self.v.append(Vector(p)); return len(self.v) - 1
    def face(self, idx, cell): self.f.append((list(idx), cell)); return len(self.f) - 1
    def ring(self, y, rx, rz, n=SIDES, rot=None):
        rot = math.pi / n if rot is None else rot
        return [self.add((rx * math.cos(2 * math.pi * i / n + rot), y, rz * math.sin(2 * math.pi * i / n + rot))) for i in range(n)]
    def mark(self, r):
        for i in range(len(r)): self.sharp.add(tuple(sorted((r[i], r[(i + 1) % len(r)]))))
    def bridge(self, a, b, cell):
        n = len(a)
        for i in range(n): self.face([a[i], a[(i + 1) % n], b[(i + 1) % n], b[i]], cell)
    def fan(self, r, pole, cell):
        for i in range(len(r)): self.face([r[i], r[(i + 1) % len(r)], pole], cell)
    def cap(self, r, cell):
        c = Vector((0, 0, 0))
        for i in r: c += self.v[i]
        c /= len(r); self.fan(r, self.add(c), cell); self.mark(r)
    def capsule(self, prof, dome0=True, dome1=True, n=SIDES):
        """prof = [(y, rx, rz, マス)]。両端は、端の輪と同じ半径の半球（関節の位置が中心）で閉じる。
           区間の色は下の端の輪のマス。同じ高さの輪が続くところ（袖口・裾）は段にして、縁をシャープに"""
        rings = []
        y0, rx0, rz0, c0 = prof[0]
        if dome0:   # 始まりの半球（y0 から -y 側へ）
            pole = self.add((0, y0 - min(rx0, rz0), 0))
            dr = []
            for k in range(1, DOME_RINGS):
                a = math.pi / 2 * k / DOME_RINGS   # 0 = 極、π/2 = 赤道
                dr.append(self.ring(y0 - math.cos(a) * min(rx0, rz0), rx0 * math.sin(a), rz0 * math.sin(a), n))
            self.fan(dr[0], pole, c0)
            for i in range(len(dr) - 1): self.bridge(dr[i], dr[i + 1], c0)
            first = dr[-1]
        rings = [self.ring(y, rx, rz, n) for y, rx, rz, _ in prof]
        if dome0: self.bridge(first, rings[0], c0)
        else: self.cap(rings[0], c0)
        for i in range(len(rings) - 1):
            self.bridge(rings[i], rings[i + 1], prof[i][3])
            if abs(prof[i][0] - prof[i + 1][0]) < 1e-6: self.mark(rings[i]); self.mark(rings[i + 1])
        y1, rx1, rz1, c1 = prof[-1]
        if dome1:   # 終わりの半球（y1 から +y 側へ）
            prev = rings[-1]
            for k in range(DOME_RINGS - 1, 0, -1):
                a = math.pi / 2 * k / DOME_RINGS
                r = self.ring(y1 + math.cos(a) * min(rx1, rz1), rx1 * math.sin(a), rz1 * math.sin(a), n)
                self.bridge(prev, r, c1); prev = r
            pole = self.add((0, y1 + min(rx1, rz1), 0))
            self.fan(prev, pole, c1)
        else: self.cap(rings[-1], c1)
        return rings

# ---- 頭の形：面長で、あごは角ばり（エラ）少し前へ。頬骨の下をわずかに削り、後頭部を出す ----
R_HEAD = 0.105 * HEAD_SCALE
HC_Y = R_HEAD * 1.02   # 頭の中心の高さ（頭の付け根から）
def jaw_width(uy):
    """頭の高さ（-1 あご先 〜 +1 頭頂）ごとの横幅の倍率"""
    if uy > -0.30: return 1.0
    if uy > -0.70: return lerp(1.0, JAW_W, smooth((-uy - 0.30) / 0.40))   # 頬からエラへ：あまり細くしない（角ばる）
    return lerp(JAW_W, 0.52, smooth((-uy - 0.70) / 0.30))                  # エラからあご先へ：急に細く
def head_shape(d, rho=1.0):
    """単位球の向き d（x 右・y 上・z 前）→ 頭の表面の点（頭の付け根が原点）。rho > 1 は表面から外（髪）"""
    x, y, z = d.x * 0.88, d.y, d.z
    y *= 1.06 if d.y > 0 else FACE_LONG                    # 下半分（顔）を縦に長く
    x *= jaw_width(d.y)
    t = smooth((-d.y - 0.55) / 0.45)
    if d.z > 0: z += 0.12 * t * d.z                        # あご先を少し前へ
    else: z *= 1.0 - 0.40 * smooth((-d.y - 0.25) / 0.6)    # あごの後ろ（首）は細く
    ck = smooth((d.z - 0.30) / 0.35) * smooth((0.20 - abs(d.y + 0.33)) / 0.20)   # 頬骨の下をわずかに削る
    x *= 1.0 - 0.06 * ck
    oc = smooth((-d.z - 0.30) / 0.5) * smooth((0.55 - abs(d.y - 0.12)) / 0.5)    # 後頭部を少し出す
    z -= 0.13 * oc
    if d.z > 0.55 and d.y > -0.6: z *= 0.96                # 顔は少し平らに
    br = smooth((d.z - 0.55) / 0.25) * smooth((0.11 - abs(d.y - 0.27)) / 0.11) * smooth((0.70 - abs(d.x)) / 0.25)
    z += BROW_RIDGE * br                                   # 眉骨の張り出し（目の上の影は形で出す）
    return Vector((x * R_HEAD * rho, HC_Y + y * R_HEAD * rho, z * R_HEAD * rho))

def sphere_dir(i, j, n=SIDES * 2, m=HEAD_RINGS):
    th = math.pi * j / m; ph = 2 * math.pi * i / n + math.pi / n
    return Vector((math.sin(th) * math.sin(ph), math.cos(th), math.sin(th) * math.cos(ph)))

def hairline(d, style="hair_short"):
    """髪の生え際：方位ごとの高さ（前は額の上、横は耳の上、後ろは襟足）。プラスなら髪の中"""
    az = math.atan2(d.x, d.z)                     # 前 0、横 ±π/2、後ろ ±π
    c = math.cos(az)                              # 前 1、横 0、後ろ -1
    front, side, back = 0.50, 0.05, -0.62
    if style == "hair_buzz": front = 0.56
    y0 = side + (front - side) * max(c, 0) - (side - back) * max(-c, 0) * (1.0 if style != "hair_tied" else 0.9)
    if abs(c) < 0.5 and d.z < 0.2: y0 = min(y0, 0.10)   # 耳の上（もみあげの所）
    return d.y - y0

def build_head(b):
    n, m = SIDES * 2, HEAD_RINGS
    rings = [[b.add(head_shape(sphere_dir(i, j, n, m))) for i in range(n)] for j in range(1, m)]
    top = b.add(head_shape(Vector((0, 1, 0)))); bot = b.add(head_shape(Vector((0, -1, 0))))
    def cell(idx):   # 頭の皮膚：生え際より上は髪の色（ベースの髪の下に隠れる。すき間から肌が見えないように）
        c = Vector((0, 0, 0))
        for i in idx: c += b.v[i]
        c /= len(idx)
        d = (c - Vector((0, HC_Y, 0))).normalized()
        return "hair" if hairline(d) > 0.06 else "skin"
    for i in range(n): idx = [top, rings[0][(i + 1) % n], rings[0][i]]; b.face(idx, cell(idx))
    for k in range(m - 2):
        for i in range(n):
            idx = [rings[k][i], rings[k][(i + 1) % n], rings[k + 1][(i + 1) % n], rings[k + 1][i]]; b.face(idx, cell(idx))
    for i in range(n): idx = [rings[-1][i], rings[-1][(i + 1) % n], bot]; b.face(idx, cell(idx))
    # 耳：横の小さな平たい半球
    for sx in (-1, 1):
        c = head_shape(Vector((sx * 0.99, -0.10, -0.08)).normalized(), 0.93)
        e = Builder(); e.capsule([(0.0, 0.014, 0.024, "skin"), (0.004, 0.014, 0.024, "skin")], dome0=False, dome1=True, n=10)
        R = Matrix.Rotation(-sx * math.pi / 2, 3, "Z")   # +Y を横（外）へ
        base = len(b.v)
        for v in e.v: b.v.append(c + R @ v)
        for f, cl in e.f: b.f.append(([i + base for i in f], cl))
    build_nose(b)

def build_nose(b):
    """鼻：目の高さから始まる低い鼻筋 → 丸みのある鼻先 → 小鼻（横に広がる）。
       前の三角柱（眉間から長く尖った形）は、串を刺したように見えたので作り直した"""
    fz = lambda x, u: head_shape(Vector((x, u, math.sqrt(max(1 - u * u - x * x, 0))))).z
    Y = lambda u: HC_Y + u * R_HEAD
    R = R_HEAD
    yt, ytip, yb = 0.06, -0.30, -0.40                 # 鼻筋の始まり（目の高さ）・鼻先・鼻の下
    tl = b.add((-0.045 * R, Y(yt), fz(-0.04, yt) - 0.004)); tr = b.add((0.045 * R, Y(yt), fz(0.04, yt) - 0.004))
    tc = b.add((0.0, Y(yt), fz(0.0, yt) + 0.025 * R))
    ml = b.add((-0.16 * R, Y(-0.34), fz(-0.15, -0.34) - 0.004)); mr = b.add((0.16 * R, Y(-0.34), fz(0.15, -0.34) - 0.004))   # 小鼻（頬に付く所）
    al = b.add((-0.09 * R, Y(-0.33), fz(-0.08, -0.33) + 0.07 * R)); ar = b.add((0.09 * R, Y(-0.33), fz(0.08, -0.33) + 0.07 * R))   # 小鼻のふくらみ
    tip = b.add((0.0, Y(ytip), fz(0.0, ytip) + 0.17 * R))
    bc = b.add((0.0, Y(yb), fz(0.0, yb) + 0.06 * R))   # 鼻の下（鼻の穴の間）
    b.face([tl, tc, tip, al], "skin"); b.face([tc, tr, ar, tip], "skin")      # 鼻筋の両側
    b.face([tl, al, ml], "skin"); b.face([tr, mr, ar], "skin")                # 小鼻の上
    b.face([al, tip, bc], "nose"); b.face([tip, ar, bc], "nose")              # 鼻先の下（少し赤み）
    b.face([ml, al, bc], "nose"); b.face([ar, mr, bc], "nose")                # 鼻の穴の所
    b.face([tl, tr, tc], "skin")
    b.mark([ml, bc, mr])

# 顔の絵を貼る面：頭の前の面を少し外に出して重ねる。UV は正面からの投影（頭の半径の単位）
FACE_TOP, FACE_BOT, FACE_HALFW = 0.62, -1.10, 0.82
def face_uv(p):
    u = 0.5 + (p.x / R_HEAD) / (2 * FACE_HALFW)
    vt = (FACE_TOP - (p.y - HC_Y) / R_HEAD) / (FACE_TOP - FACE_BOT)
    return (u, 1.0 - vt)
def build_face(b):
    n, m = SIDES * 2, HEAD_RINGS
    grid = {(i, j): head_shape(sphere_dir(i, j, n, m)) for j in range(1, m) for i in range(n)}
    def inside(p): u, v = face_uv(p); return 0.01 < u < 0.99 and 0.01 < v < 0.99 and p.z > 0.20 * R_HEAD
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

# ---- 髪：頭の形に沿ったベースの髪 ＋ 同じ流れの大きめの毛束 ----
def dir_from(el, az):   # 仰角（水平から上へ・度）と方位（前 0°、右へプラス・度）→ 単位ベクトル
    e, a = math.radians(el), math.radians(az)
    return Vector((math.cos(e) * math.sin(a), math.sin(e), math.cos(e) * math.cos(a)))
def shell_rho(d, style, thick):
    f = hairline(d, style)
    return 1.004 + thick * smooth(f / 0.22)
def build_shell(b, style, thick):
    """ベースの髪：頭の皮膚の生え際から上を、少し外に膨らませた面で覆う（生え際では頭に沿って厚みが 0 になる）"""
    n, m = SIDES * 2, HEAD_RINGS
    keep = {}
    for j in range(0, m):
        for i in range(n):
            d = sphere_dir(i, j, n, m) if j > 0 else Vector((0, 1, 0))
            if hairline(d, style) > -0.04: keep[(i, j)] = b.add(head_shape(d, shell_rho(d, style, thick)))
    top = keep.get((0, 0))
    for i in range(n):
        if (i, 1) in keep and ((i + 1) % n, 1) in keep: b.face([top, keep[((i + 1) % n, 1)], keep[(i, 1)]], "hair")
    for j in range(1, m - 1):
        for i in range(n):
            q = [(i, j), ((i + 1) % n, j), ((i + 1) % n, j + 1), (i, j + 1)]
            if all(k in keep for k in q): b.face([keep[k] for k in q], "hair")
def tuft(b, style, thick, root, end, width, lift, bulge=0.0, curl=0.0, segs=7):
    """大きめの毛束：根元（root）から先（end）まで頭の形に沿って流れる、くさび形。根元と先は細く、途中が広い。
       lift：先が浮く量、bulge：途中のふくらみ（頭の半径の単位）"""
    r0, r1 = root.normalized(), end.normalized()
    ang = r0.angle(r1); axis = r0.cross(r1)
    if axis.length < 1e-5: axis = Vector((1, 0, 0))
    axis.normalize()
    rings = []
    for k in range(segs):
        s = k / segs
        d = (Matrix.Rotation(ang * s, 3, axis) @ r0).normalized()
        if curl: d = (Matrix.Rotation(curl * math.sin(math.pi * 2 * s), 3, d) @ d).normalized()
        rho = shell_rho(d, style, thick) + 0.01 + bulge * math.sin(math.pi * s) + lift * s ** 2
        p = head_shape(d, rho)
        nrm = (p - Vector((0, HC_Y, 0))).normalized()
        tan = (Matrix.Rotation(ang * s, 3, axis) @ (axis.cross(r0))).normalized()
        side = tan.cross(nrm).normalized()
        w = width * R_HEAD * (0.45 + 0.55 * math.sin(math.pi * min(1.0, 0.15 + s * 1.1)))
        t = w * 0.30
        rings.append([b.add(p + side * w), b.add(p + nrm * t), b.add(p - side * w), b.add(p - nrm * t * 0.4)])
    dt = (Matrix.Rotation(ang, 3, axis) @ r0).normalized()
    tip = b.add(head_shape(dt, shell_rho(dt, style, thick) + 0.01 + lift))
    b.cap(rings[0], "hair")
    for i in range(len(rings) - 1): b.bridge(rings[i], rings[i + 1], "hair")
    for i in range(4): b.face([rings[-1][i], rings[-1][(i + 1) % 4], tip], "hair")
def ponytail(b):
    """後ろで束ねた髪：結び目（ゴム）から下へ垂れる。ゴムはリストバンドの色"""
    root = head_shape(dir_from(-8, 180), 1.03 + HAIR_THICK)
    b2 = Builder()
    b2.capsule([(0.0, 0.034, 0.030, "wrist"), (0.016, 0.034, 0.030, "wrist"), (0.016, 0.030, 0.027, "hair"), (0.09, 0.042, 0.034, "hair"),
                (0.20, 0.033, 0.026, "hair"), (0.26, 0.012, 0.010, "hair")], dome0=True, dome1=False, n=12)
    R = Matrix.Rotation(math.radians(162), 3, "X")   # 下へ、少し後ろへ
    base = len(b.v)
    for v in b2.v: b.v.append(root + R @ v)
    for f, c in b2.f: b.f.append(([i + base for i in f], c))
    for e in b2.sharp: b.sharp.add((e[0] + base, e[1] + base))

CROWN = dir_from(68, 180)   # つむじ（頭の上の後ろ寄り）
def build_hair(b, style):
    if style == "hair_buzz":   # 坊主寄り：頭に沿った薄い髪だけ
        build_shell(b, style, HAIR_THICK * 0.30); return
    th = HAIR_THICK * (1.5 if style == "hair_wave" else 1.0)
    build_shell(b, style, th)
    if style == "hair_short":   # 短髪：つむじから外へ流れる 8 束（前髪へ 3・横へ 2・後ろへ 3）
        # 前髪：長さと向きをそろえない（一直線に切りそろえた形はヘルメットに見えた）。少し右へ流す
        for az, el, w in ((-46, 30, 0.38), (-18, 24, 0.44), (10, 27, 0.44), (36, 31, 0.40)): tuft(b, style, th, CROWN, dir_from(el, az), w, 0.04, bulge=0.03)
        for sx in (-1, 1):      tuft(b, style, th, CROWN, dir_from(8, sx * 100), 0.40, 0.02, bulge=0.025)
        for az in (150, 180, 210): tuft(b, style, th, CROWN, dir_from(-28, az), 0.38, 0.02, bulge=0.025)
    elif style == "hair_wave":  # ウェーブ：同じ流れで、ふくらんで、うねる 8 束
        for az in (-40, 0, 40): tuft(b, style, th, CROWN, dir_from(30, az), 0.50, 0.10, bulge=0.08, curl=0.35)
        for sx in (-1, 1):      tuft(b, style, th, CROWN, dir_from(-2, sx * 102), 0.46, 0.06, bulge=0.07, curl=0.35)
        for az in (150, 180, 210): tuft(b, style, th, CROWN, dir_from(-34, az), 0.44, 0.05, bulge=0.06, curl=0.35)
    elif style == "hair_tied":  # 長めを後ろで束ねる：前から後ろへ、結び目に集まる 7 束
        knot = dir_from(-8, 180)
        for az in (-40, -14, 14, 40): tuft(b, style, th, dir_from(44, az), knot, 0.42, 0.0, bulge=0.02)
        for sx in (-1, 1):            tuft(b, style, th, dir_from(12, sx * 85), knot, 0.40, 0.0, bulge=0.015)
        tuft(b, style, th, dir_from(80, 0), knot, 0.40, 0.0, bulge=0.03)
        ponytail(b)

def build(name, L):
    T = LIMB_THICK
    b = Builder()
    if name == "pelvis":   # 腰（パンツそのもの）。股関節の球（腿の付け根の半球）を中に包む
        w = TORSO_W
        b.capsule([(-0.14, 0.10 * w, 0.075, "pants"), (-0.09, 0.152 * w, 0.102, "pants"), (0.03, 0.160 * w, 0.106, "pants"), (L + 0.03, 0.145 * w, 0.098, "pants")], dome0=False, dome1=False)
    elif name == "torso":  # 胴（シャツそのもの）。上は台形：首の付け根から肩へ下がる線。襟・裾・胸のラインは差し色
        w, sw = TORSO_W, TORSO_W * SHOULDER_W
        ys = J["shoulder.R"][1] - J["waist"][1]   # 肩の高さ（ウエストから）
        b.capsule([(-0.07, 0.168 * w, 0.112, "accent"), (-0.05, 0.166 * w, 0.111, "shirt"), (0.15, 0.180 * w, 0.114, "accent"),
                   (0.18, 0.184 * w, 0.115, "shirt"), (ys - 0.07, 0.205 * sw, 0.112, "shirt"), (ys - 0.025, 0.200 * sw, 0.104, "shirt"),
                   (ys + 0.012, 0.150 * sw, 0.090, "shirt"), (L - 0.005, 0.085, 0.068, "accent"), (L + 0.025, 0.064, 0.052, "accent")], dome0=False, dome1=False)
    elif name == "neck":   # 首：太め
        r = NECK_R
        b.capsule([(-0.03, r * 1.08, r * 1.0, "skin"), (L + 0.045, r, r * 0.95, "skin")], dome0=False, dome1=False)
    elif name == "head":
        build_head(b)
    elif name == "face":
        build_face(b)
    elif name in HAIRS:
        build_hair(b, name)
    elif name.startswith("upperarm"):   # 上腕：付け根は三角筋の丸み（＝袖。肩の関節が中心の半球）。袖口はまっすぐな段。肘は関節の半球
        rs, rc, re = R_SHOULDER * T * SLEEVE_K, lerp(R_SHOULDER, ARM_MID, 0.8) * T, R_ELBOW * T
        cuff = L * SLEEVE_LEN
        b.capsule([(0.0, rs, rs, "shirt"), (cuff - 0.022, rs * 0.90, rs * 0.90, "accent"), (cuff, rs * 0.89, rs * 0.89, "accent"),
                   (cuff, rc, rc, "skin"), (L * 0.75, lerp(ARM_MID * T, re, 0.5), lerp(ARM_MID * T, re, 0.5), "skin"), (L, re, re, "skin")])
    elif name.startswith("forearm"):    # 前腕：肘は上腕と同じ半径の半球。少し下がふくらみ、手首へ細く。手首の近くにリストバンド
        re, rb, rw = R_ELBOW * T, FORE_BULGE * T, R_WRIST * T
        b.capsule([(0.0, re, re, "skin"), (L * 0.22, rb, rb * 0.95, "skin"), (L * 0.55, lerp(rb, rw, 0.45), lerp(rb, rw, 0.45) * 0.93, "skin"),
                   (L * 0.72, rw * 1.08, rw * 1.02, "skin"), (L * 0.72, rw * 1.30, rw * 1.25, "wrist"), (L * 0.90, rw * 1.30, rw * 1.25, "wrist"),
                   (L * 0.90, rw * 1.02, rw, "skin"), (L, rw, rw, "skin")])
    elif name.startswith("hand"):       # 手：手首は前腕と同じ半径の半球から。握った形
        h = HAND_SCALE * T
        rw = R_WRIST * T
        b.capsule([(0.0, rw, rw, "skin"), (L * 0.40, 0.046 * h, 0.034 * h, "skin"), (L * 0.80, 0.038 * h, 0.027 * h, "skin")], dome0=True, dome1=True)
    elif name.startswith("thigh"):      # 腿：股関節は腰の中の半球（パンツの色）。付け根が太く膝へ細く。裾はまっすぐな段で、広げすぎない
        rh, rm, rk = R_HIP * T, THIGH_MID * T, R_KNEE * T
        hem = L * SHORTS_LEN
        rhem = lerp(rh, rm, hem / (L * 0.5)) if hem <= L * 0.5 else lerp(rm, rk, (hem - L * 0.5) / (L * 0.5))
        rp = rhem * SHORTS_FLARE
        b.capsule([(0.0, rh, rh, "pants"), (hem, rp, rp, "pants"), (hem, rhem * 0.98, rhem * 0.98, "skin"),
                   (L * 0.78, lerp(rm, rk, 0.55), lerp(rm, rk, 0.55), "skin"), (L, rk, rk, "skin")])
    elif name.startswith("shin"):       # すね：膝は腿と同じ半径の半球。ふくらはぎ（後ろ）を膨らませて、足首へ細く。足首の上は靴下
        rk, rc, ra = R_KNEE * T, CALF * T, R_ANKLE * T
        sock = L * (1.0 - SOCK_LEN)
        b.capsule([(0.0, rk, rk, "skin"), (L * 0.28, rc * 0.92, rc, "skin"), (L * 0.55, lerp(rc, ra, 0.45), lerp(rc, ra, 0.45), "skin"),
                   (sock, ra * 1.10, ra * 1.10, "socks"), (L, ra * 1.10, ra * 1.10, "socks")])
        for v in b.v:   # ふくらはぎ：後ろ（下向きの骨では +Z）を少し出す
            t = v.y / L
            if v.z > 0 and 0.0 < t < 0.65: v.z *= 1.0 + 0.25 * smooth(1 - abs(t - 0.3) / 0.3)
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

# ---- 顔の絵（128x128、背景は透明） ----
#   線で描くのは目・眉・口だけ（頬・口元・額の線は描かない：シワに見えて老けるので）。
#   白目（明るく）＋瞳（IRIS_KEY の色で描き、ゲームが選手の瞳の色に置き換える）＋瞳孔＋光、上まぶたの線、太めの眉（外を上げる）、短い線の口。
#   ほかは大きくぼかした楕円だけ：眉の下の影・頬骨の下の影（どちらも 10% 以下）、頬の赤み（5〜8%）。年齢感は形（首・あご・眉骨）で出す
FACES = {   # 目の横幅・高さ、瞳の半径、眉の角度（外を上げる・度）・太さ、口の幅、まつげ、影の濃さ（ピクセル・0〜1）
    "face_01": dict(ew=10.0, eh=5.6, ir=4.5, brow=14, bt=4.6, mw=8,  lash=False, shade=1.0),   # 真剣（男子の基本）
    "face_02": dict(ew=9.6,  eh=4.6, ir=4.0, brow=8,  bt=5.0, mw=9,  lash=False, shade=1.0),   # 細い目
    "face_03": dict(ew=10.6, eh=6.2, ir=4.9, brow=10, bt=3.0, mw=7,  lash=True,  shade=0.7),   # 女子の基本：まつげ
    "face_04": dict(ew=9.6,  eh=5.2, ir=4.3, brow=4,  bt=4.0, mw=10, lash=False, shade=0.9),   # 落ち着いた顔
}
class Canvas:
    def __init__(self, n=FACE_TEX): self.n = n; self.px = [0.0] * (n * n * 4)
    def put(self, x, y, c, a=1.0):
        n = self.n
        if 0 <= x < n and 0 <= y < n and a > 0:
            i = ((n - 1 - y) * n + x) * 4   # Blender の画像は下の行から
            a0 = self.px[i + 3]; ao = a + a0 * (1 - a)
            for k in range(3): self.px[i + k] = (c[k] * a + self.px[i + k] * a0 * (1 - a)) / max(ao, 1e-6)
            self.px[i + 3] = ao
    def soft(self, cx, cy, rx, ry, c, a, rot=0.0):   # ぼかした楕円（影）
        cr, sr = math.cos(rot), math.sin(rot)
        for y in range(int(cy - ry - rx - 2), int(cy + ry + rx + 3)):
            for x in range(int(cx - rx - ry - 2), int(cx + rx + ry + 3)):
                dx, dy = x + 0.5 - cx, y + 0.5 - cy
                u, v = (dx * cr + dy * sr) / rx, (-dx * sr + dy * cr) / ry
                d = u * u + v * v
                if d < 1.0: self.put(x, y, c, a * (1 - d) ** 1.5)
    def ellipse(self, cx, cy, rx, ry, c, edge=None):
        for y in range(int(cy - ry - 2), int(cy + ry + 3)):
            for x in range(int(cx - rx - 2), int(cx + rx + 3)):
                d = ((x + 0.5 - cx) / rx) ** 2 + ((y + 0.5 - cy) / ry) ** 2
                if d <= 1.0:
                    self.put(x, y, c, 1.0)
                    if edge and d > 0.62: self.put(x, y, edge, 0.85)
    def line(self, pts, th, c, a=1.0):   # 折れ線（同じピクセルに何度も重ねて濃くならないよう、濃さの一番大きい値で 1 回だけ塗る）
        mask = {}
        for (x0, y0), (x1, y1) in zip(pts, pts[1:]):
            L = max(abs(x1 - x0), abs(y1 - y0), 1)
            for s in range(int(L * 3) + 1):
                t = s / (L * 3); x = x0 + (x1 - x0) * t; y = y0 + (y1 - y0) * t
                for yy in range(int(y - th - 1), int(y + th) + 2):
                    for xx in range(int(x - th - 1), int(x + th) + 2):
                        d = math.hypot(xx + 0.5 - x, yy + 0.5 - y)
                        w = 1.0 if d <= th else (0.35 if d <= th + 0.7 else 0.0)
                        if w > mask.get((xx, yy), 0.0): mask[(xx, yy)] = w
        for (xx, yy), w in mask.items(): self.put(xx, yy, c, a * w)
    def save(self, name):
        img = bpy.data.images.new(name, self.n, self.n, alpha=True)
        img.pixels = self.px
        img.filepath_raw = os.path.join(OUT, "faces", name + ".png"); img.file_format = "PNG"; img.save()
        return img
def to_px(xh, yh):   # 頭の座標（頭の半径の単位、中心から）→ 絵のピクセル
    u = 0.5 + xh / (2 * FACE_HALFW); vt = (FACE_TOP - yh) / (FACE_TOP - FACE_BOT)
    return u * FACE_TEX, vt * FACE_TEX
def make_face_png(name, prm):
    """顔の絵。数値（FACES）は 128 ピクセルのときの大きさで、FACE_TEX に合わせて拡大する。
       目：はっきり開いた白目と、上まぶたに少しかかる瞳（眠そうに見えないよう、上まぶたは目頭寄りが高く、左右の目で鏡写し）。
       眉：細めで、ゆるい山形（吊り上げすぎると怒って見えた）。口：上唇の線＋下唇の淡い影。鼻の下に淡い影"""
    cv = Canvas(); K = FACE_TEX / 128.0
    sh, dark, white, brow, lip, blush = (0.20, 0.12, 0.10), (0.10, 0.07, 0.06), (0.97, 0.96, 0.95), (0.16, 0.11, 0.08), (0.55, 0.27, 0.25), (0.85, 0.32, 0.28)
    k = prm["shade"]
    # 大きくぼかした影と赤み（線は描かない）
    for sx in (-1, 1):
        x, y = to_px(sx * 0.36, 0.12); cv.soft(x, y, 16 * K, 6 * K, sh, 0.07 * k)                   # 眉の下（彫り）
        x, y = to_px(sx * 0.56, -0.36); cv.soft(x, y, 15 * K, 8 * K, sh, 0.04 * k, rot=-sx * 0.4)   # 頬骨の下
        x, y = to_px(sx * 0.46, -0.24); cv.soft(x, y, 14 * K, 9 * K, blush, 0.06)                   # 頬の赤み
    x, y = to_px(0.0, -0.47); cv.soft(x, y, 7 * K, 2.6 * K, sh, 0.16)                               # 鼻の下の影
    ew, eh, ir = prm["ew"] * K, prm["eh"] * K * 1.12, prm["ir"] * K * 0.92
    for sx in (-1, 1):
        ex, ey = to_px(sx * 0.36, 0.0)
        inner = -sx   # 目頭の向き（絵の x で）。左右の目で鏡写しにする
        cv.ellipse(ex, ey, ew, eh, white)                                                            # 白目
        ix, iy = ex + inner * 0.5 * K, ey - 0.6 * K
        cv.ellipse(ix, iy, ir, ir * 1.04, IRIS_KEY)                                                  # 瞳（あとで選手の瞳の色に）
        cv.ellipse(ix, iy, ir * 0.42, ir * 0.44, (0.03, 0.03, 0.03))                                 # 瞳孔
        cv.ellipse(ix - inner * 1.4 * K, iy - 1.5 * K, 1.3 * K, 1.3 * K, (1, 1, 1))                  # 目の光
        cv.ellipse(ix + inner * 1.2 * K, iy + 1.6 * K, 0.6 * K, 0.6 * K, (1, 1, 1))                  # 小さな光
        # 上まぶた：白目の上の縁に沿った弧（目頭寄りが高い）。白目の上にかぶせて、瞳の上を少し隠す
        top = []
        for t in range(11):
            u = -1 + t / 5.0                                    # -1 目頭 … +1 目尻（絵の上では inner の向きで反転）
            h = math.sqrt(max(0.0, 1 - u * u)) * eh * (1.0 + 0.10 * (-u))
            top.append((ex - inner * u * ew * 1.02, ey - h + 0.2 * K))
        cv.line(top, (0.9 if not prm["lash"] else 1.3) * K, dark)
        if prm["lash"]: cv.line([top[-1], (top[-1][0] - inner * 2.4 * K, top[-1][1] - 1.6 * K)], 0.7 * K, dark)   # 目尻のまつげ
        cv.line([(ex - ew * 0.6, ey + eh * 0.92), (ex + ew * 0.6, ey + eh * 0.92)], 0.4 * K, dark, 0.25)          # 下まぶた（ごく薄く）
        # 眉：3 点のゆるい山形。目頭側が太く、目尻へ細く
        bx, by = to_px(sx * 0.37, 0.30)
        a = math.radians(prm["brow"] * 0.55); hl = 10.5 * K
        p0 = (bx + inner * hl, by + 0.8 * K)                                    # 眉頭
        p1 = (bx - inner * hl * 0.25, by - 1.6 * K - hl * math.sin(a) * 0.5)    # 眉山
        p2 = (bx - inner * hl * 1.05, by - hl * math.sin(a) + 0.6 * K)          # 眉尻
        bt = prm["bt"] * K * 0.36
        cv.line([p0, p1], bt + 0.4 * K, brow); cv.line([p1, p2], bt * 0.75 + 0.3 * K, brow)
    # 口：上唇の線（端は少しだけ上げる）＋下唇の淡い影と明るさ
    mx, my = to_px(0.0, -0.66); mw = prm["mw"] * K * 1.25
    cv.soft(mx, my + 3.4 * K, mw * 0.75, 2.4 * K, lip, 0.22)                                         # 下唇
    cv.soft(mx, my + 6.5 * K, mw * 0.6, 2.0 * K, sh, 0.10)                                           # 下唇の下の影
    cv.line([(mx - mw, my - 0.5 * K), (mx - mw * 0.45, my + 0.5 * K), (mx, my + 0.2 * K), (mx + mw * 0.45, my + 0.5 * K), (mx + mw, my - 0.5 * K)], 0.75 * K, lip)
    return cv.save(name)
def make_beard_png(name, level):
    """1 = 剃り跡：あご〜口のまわりに、なめらかな青灰色のグラデーション（不透明度 12%）。点は打たない。
       2 = ひげ：同じ形で濃く（35%。ゲームが髪の色にする）"""
    cv = Canvas()
    col = (0.36, 0.42, 0.50) if level == 1 else (0.10, 0.08, 0.06)
    amax = 0.12 if level == 1 else 0.35
    n = FACE_TEX
    for py in range(n):
        for pxl in range(n):
            u = (pxl + 0.5) / n; vt = (py + 0.5) / n
            xh = (u - 0.5) * 2 * FACE_HALFW; yh = FACE_TOP - vt * (FACE_TOP - FACE_BOT)
            edge = 0.80 * jaw_width(yh) - abs(xh)                       # あごの輪郭の内側
            a = smooth((-0.46 - yh) / 0.14) * smooth(edge / 0.10)       # 頬の下からあごへ
            a = max(a, smooth((0.26 - abs(xh)) / 0.08) * smooth((yh + 0.64) / 0.04) * smooth((-0.50 - yh) / 0.04))   # 鼻の下
            a *= smooth((0.66 - abs(xh)) / 0.16) * smooth((FACE_BOT + 0.12 - yh) / -0.12)   # 絵の端（頬の横・あごの下）では消す（端でまっすぐ切れてマスクに見えた）
            mouth = smooth((0.20 - abs(xh)) / 0.05) * smooth((0.05 - abs(yh + 0.68)) / 0.03)
            a *= 1.0 - mouth                                           # 唇はあける
            if a > 0.01: cv.put(pxl, py, col, amax * a)
    return cv.save(name)

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

def main():
    global img, face_imgs, mat, mat_face
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
    make_beard_png("beard_1", 1); make_beard_png("beard_2", 2)
    face_imgs["face_01"].pack()

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
        if b.sharp:
            attr = me.attributes.get("sharp_edge") or me.attributes.new("sharp_edge", "BOOLEAN", "EDGE")
            for e in me.edges:
                if tuple(sorted(e.vertices)) in b.sharp: attr.data[e.index].value = True
        ob = bpy.data.objects.new(name, me)
        scene.collection.objects.link(ob)
        ob.matrix_world = Matrix.Translation(C @ Vector(J[hj])) @ (C @ R @ C.inverted()).to_4x4()
        ob["len"] = L
        bm = bmesh.new(); bm.from_mesh(me)
        bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=1e-7)
        bm.faces.ensure_lookup_table(); bm.faces.index_update()
        # 法線を外向きに：つながった塊ごとに、閉じた形は「外向きにそろえる」、開いた形（ベースの髪・顔の絵・鼻）は頭の中心から外へ
        hc = C @ Vector((0, HC_Y, 0)) if (name in HAIRS or name in ("head", "face")) else None
        seen = set()
        for f0 in bm.faces:
            if f0.index in seen: continue
            isl, stack = [], [f0]; seen.add(f0.index)
            while stack:
                f = stack.pop(); isl.append(f)
                for e in f.edges:
                    for g in e.link_faces:
                        if g.index not in seen: seen.add(g.index); stack.append(g)
            open_ = any(len(e.link_faces) < 2 for f in isl for e in f.edges)
            if open_ and hc is not None:
                for f in isl:
                    if f.normal.dot(f.calc_center_median() - hc) < 0: f.normal_flip()
            else: bmesh.ops.recalc_face_normals(bm, faces=isl)
        bmesh.ops.triangulate(bm, faces=bm.faces)
        bm.to_mesh(me); bm.free()
        for poly in me.polygons: poly.use_smooth = True

    bpy.ops.wm.save_as_mainfile(filepath=os.path.join(OUT, "player.blend"))
    bpy.ops.export_scene.gltf(filepath=os.path.join(OUT, "player.glb"), export_format="GLB", export_apply=True, export_yup=True,
                              export_texcoords=True, export_normals=True, export_extras=True, export_animations=False,
                              export_skins=False, export_morph=False, export_materials="EXPORT", export_image_format="AUTO")
    print("parts", len(PARTS), "tris", sum(len(o.data.polygons) for o in scene.objects if o.type == "MESH"))


if __name__ == "__main__":
    main()
