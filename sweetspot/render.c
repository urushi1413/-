// SWEET SPOT — コートと人とラケットの描画、表示用の文字
#include "game.h"
#include "rlgl.h"

// ---- 陰影：光の向き（左上前の太陽）から、面ごとに明るさを変えて描く（一色ののっぺりした見た目をやめる） ----
static Color Shade(Color c, Vector3 n)
{
    const Vector3 L = { 0.348f, 0.846f, 0.403f };   // 光の来る向き（正規化済み）
    float l = 0.55f + 0.50f * fmaxf(Vector3DotProduct(n, L), 0.0f) + 0.06f * fmaxf(-n.y, 0.0f) * 0.0f;
    l = fminf(l, 1.05f);
    return (Color){ (unsigned char)fminf(c.r * l, 255.0f), (unsigned char)fminf(c.g * l, 255.0f), (unsigned char)fminf(c.b * l, 255.0f), c.a };
}
static void LitVertex(Vector3 p, Vector3 n, Color c) { Color s = Shade(c, n); rlColor4ub(s.r, s.g, s.b, s.a); rlVertex3f(p.x, p.y, p.z); }
// 太さの変わる筒（側面とふた）
void LitCylinderEx(Vector3 a, Vector3 b, float ra, float rb, int sl, Color col)
{
    Vector3 ax = Vector3Subtract(b, a);
    float len = Vector3Length(ax);
    if (len < 1e-5f) return;
    ax = Vector3Scale(ax, 1.0f / len);
    if (sl < 6) sl = 6;
    Vector3 u = fabsf(ax.y) < 0.9f ? Vector3Normalize(Vector3CrossProduct(ax, (Vector3){ 0, 1, 0 })) : (Vector3){ 1, 0, 0 };
    Vector3 w = Vector3CrossProduct(ax, u);
    rlCheckRenderBatchLimit(sl * 12);
    rlBegin(RL_TRIANGLES);
    for (int j = 0; j < sl; j++) {
        float p0 = 2 * PI * j / sl, p1 = 2 * PI * (j + 1) / sl;
        Vector3 d0 = Vector3Add(Vector3Scale(u, cosf(p0)), Vector3Scale(w, sinf(p0)));
        Vector3 d1 = Vector3Add(Vector3Scale(u, cosf(p1)), Vector3Scale(w, sinf(p1)));
        Vector3 a0 = Vector3Add(a, Vector3Scale(d0, ra)), a1 = Vector3Add(a, Vector3Scale(d1, ra));
        Vector3 b0 = Vector3Add(b, Vector3Scale(d0, rb)), b1 = Vector3Add(b, Vector3Scale(d1, rb));
        LitVertex(a0, d0, col); LitVertex(b1, d1, col); LitVertex(b0, d0, col);
        LitVertex(a0, d0, col); LitVertex(a1, d1, col); LitVertex(b1, d1, col);
        Vector3 nb = ax, na = Vector3Negate(ax);
        LitVertex(b, nb, col); LitVertex(b0, nb, col); LitVertex(b1, nb, col);
        LitVertex(a, na, col); LitVertex(a1, na, col); LitVertex(a0, na, col);
    }
    rlEnd();
}
void LitSphereEx(Vector3 c, float r, int rings, int slices, Color col)
{
    if (rings < 4) rings = 4;
    if (slices < 6) slices = 6;
    rlCheckRenderBatchLimit(rings * slices * 6);
    rlBegin(RL_TRIANGLES);
    for (int i = 0; i < rings; i++) {
        float t0 = PI * i / rings, t1 = PI * (i + 1) / rings;
        for (int j = 0; j < slices; j++) {
            float q0 = 2 * PI * j / slices, q1 = 2 * PI * (j + 1) / slices;
            Vector3 n00 = { sinf(t0) * cosf(q0), cosf(t0), sinf(t0) * sinf(q0) }, n01 = { sinf(t0) * cosf(q1), cosf(t0), sinf(t0) * sinf(q1) };
            Vector3 n10 = { sinf(t1) * cosf(q0), cosf(t1), sinf(t1) * sinf(q0) }, n11 = { sinf(t1) * cosf(q1), cosf(t1), sinf(t1) * sinf(q1) };
            Vector3 p00 = Vector3Add(c, Vector3Scale(n00, r)), p01 = Vector3Add(c, Vector3Scale(n01, r));
            Vector3 p10 = Vector3Add(c, Vector3Scale(n10, r)), p11 = Vector3Add(c, Vector3Scale(n11, r));
            LitVertex(p00, n00, col); LitVertex(p11, n11, col); LitVertex(p10, n10, col);
            LitVertex(p00, n00, col); LitVertex(p01, n01, col); LitVertex(p11, n11, col);
        }
    }
    rlEnd();
}
// 箱（面ごとに明るさを変える）
void LitCube(Vector3 p, float wx, float hy, float lz, Color col)
{
    float x = wx * 0.5f, y = hy * 0.5f, z = lz * 0.5f;
    Vector3 P[8];
    for (int i = 0; i < 8; i++) P[i] = (Vector3){ p.x + ((i & 1) ? x : -x), p.y + ((i & 2) ? y : -y), p.z + ((i & 4) ? z : -z) };
    static const int F[6][4] = { { 0, 2, 3, 1 }, { 4, 5, 7, 6 }, { 0, 1, 5, 4 }, { 2, 6, 7, 3 }, { 0, 4, 6, 2 }, { 1, 3, 7, 5 } };
    static const Vector3 N[6] = { { 0, 0, -1 }, { 0, 0, 1 }, { 0, -1, 0 }, { 0, 1, 0 }, { -1, 0, 0 }, { 1, 0, 0 } };
    rlCheckRenderBatchLimit(36);
    rlBegin(RL_TRIANGLES);
    for (int f = 0; f < 6; f++) {
        Vector3 a = P[F[f][0]], b = P[F[f][1]], c = P[F[f][2]], d = P[F[f][3]];
        LitVertex(a, N[f], col); LitVertex(b, N[f], col); LitVertex(c, N[f], col);
        LitVertex(a, N[f], col); LitVertex(c, N[f], col); LitVertex(d, N[f], col);
    }
    rlEnd();
}
// このファイルの中では、筒・球・箱をすべて陰影つきで描く
static void LitSphere(Vector3 c, float r, Color col) { LitSphereEx(c, r, 8, 10, col); }
#define DrawCylinderEx(...) LitCylinderEx(__VA_ARGS__)
#define DrawSphere(...)     LitSphere(__VA_ARGS__)
#define DrawSphereEx(...)   LitSphereEx(__VA_ARGS__)
#define DrawCube(...)       LitCube(__VA_ARGS__)

// ---- 描画まわり ----
void LineAlongX(float z, float x0, float x1)
{
    DrawCube((Vector3){ (x0 + x1) * 0.5f, 0.002f, z }, fabsf(x1 - x0), 0.004f, LINE_W, RAYWHITE);
}

void LineAlongZ(float x, float z0, float z1)
{
    DrawCube((Vector3){ x, 0.002f, (z0 + z1) * 0.5f }, LINE_W, 0.004f, fabsf(z1 - z0), RAYWHITE);
}

void DrawCourt(void)
{
    const Surface *sf = &SURFACES[gSurface];
    DrawPlane((Vector3){ 0, -0.001f, 0 }, (Vector2){ 40.0f, 60.0f }, sf->outside);
    DrawPlane((Vector3){ 0, 0.0f, 0 }, (Vector2){ DOUBLES_W, COURT_LEN }, sf->court);
    if (gSurface == SURF_GRASS) {
        // 芝の刈り目（縞）
        for (int i = 0; i < 12; i++) {
            float z = -HALF_LEN + COURT_LEN * (i + 0.5f) / 12.0f;
            if (i % 2) DrawPlane((Vector3){ 0, 0.0012f, z }, (Vector2){ DOUBLES_W, COURT_LEN / 12.0f }, (Color){ 80, 155, 68, 255 });
        }
        // ベースラインのあたりは芝がすり切れて土が見える
        for (int e = -1; e <= 1; e += 2)
            for (int k = 0; k < 7; k++) {
                float x = -2.4f + 0.8f * k, r = 0.9f - 0.12f * fabsf(k - 3.0f);
                DrawCylinder((Vector3){ x, 0.0015f, e * (HALF_LEN + 0.3f) }, r, r, 0.0005f, 16, (Color){ 165, 150, 95, 150 });
                DrawCylinder((Vector3){ x * 0.6f, 0.0016f, e * (HALF_LEN - 0.4f) }, r * 0.6f, r * 0.6f, 0.0005f, 14, (Color){ 150, 145, 90, 110 });
            }
    }

    // ラインは規則どおりコートの内側に引く（コートの寸法はラインの外側まで。ラインに触れればイン）
    float dx = DOUBLES_W * 0.5f, sx = SINGLES_W * 0.5f, h = LINE_W * 0.5f;
    LineAlongX( HALF_LEN - h, -dx, dx);
    LineAlongX(-HALF_LEN + h, -dx, dx);
    LineAlongZ(-dx + h, -HALF_LEN, HALF_LEN);
    LineAlongZ( dx - h, -HALF_LEN, HALF_LEN);
    LineAlongZ(-sx + h, -HALF_LEN, HALF_LEN);
    LineAlongZ( sx - h, -HALF_LEN, HALF_LEN);
    LineAlongX( SERVICE_Z - h, -sx, sx);
    LineAlongX(-SERVICE_Z + h, -sx, sx);
    LineAlongZ(0.0f, -SERVICE_Z, SERVICE_Z);
    LineAlongZ(0.0f,  HALF_LEN - 0.10f,  HALF_LEN);   // センターマーク
    LineAlongZ(0.0f, -HALF_LEN, -HALF_LEN + 0.10f);

    // ネット（たるみを再現するため細かく分けて描く）
    const int segs = 24;
    for (int i = 0; i < segs; i++) {
        float x0 = -NET_POST_X + (2.0f * NET_POST_X) * i / segs;
        float x1 = -NET_POST_X + (2.0f * NET_POST_X) * (i + 1) / segs;
        float h = NetHeightAt((x0 + x1) * 0.5f);
        DrawCube((Vector3){ (x0 + x1) * 0.5f, h * 0.5f, 0 }, x1 - x0, h, 0.01f, (Color){ 20, 20, 20, 160 });
        DrawCube((Vector3){ (x0 + x1) * 0.5f, h - 0.025f, 0 }, x1 - x0, 0.05f, 0.015f, RAYWHITE);
    }
    // ネットの網目（たるみに沿って）
    {
        Color mesh = { 235, 235, 235, 70 };
        for (float x = -NET_POST_X; x <= NET_POST_X; x += 0.10f)
            DrawLine3D((Vector3){ x, 0.0f, 0.0f }, (Vector3){ x, NetHeightAt(x) - 0.05f, 0.0f }, mesh);
        for (float y = 0.10f; y < NET_H_POST; y += 0.10f) {
            for (int i = 0; i < segs; i++) {
                float x0 = -NET_POST_X + (2.0f * NET_POST_X) * i / segs, x1 = -NET_POST_X + (2.0f * NET_POST_X) * (i + 1) / segs;
                if (y < NetHeightAt(x0) - 0.05f && y < NetHeightAt(x1) - 0.05f) DrawLine3D((Vector3){ x0, y, 0.0f }, (Vector3){ x1, y, 0.0f }, mesh);
            }
        }
    }
    DrawCylinder((Vector3){ -NET_POST_X, 0, 0 }, 0.05f, 0.05f, NET_H_POST, 8, DARKGRAY);
    DrawCylinder((Vector3){  NET_POST_X, 0, 0 }, 0.05f, 0.05f, NET_H_POST, 8, DARKGRAY);
}

// ================= 観客席と審判 =================
// 観客席：コートの両横と奥に段々の席。カメラとコートの間に来る側は描かない（視界をふさがないように）
// 観客は 1 人ずつ形（頭・髪・目・口・肩・腕・脚）を作り、席ごとに 1 つの 3D モデルにまとめて描く（速い）。
//   盛り上がったときは、上げた腕（拍手）だけを上に描き足す
typedef struct { Vector3 p, f, r; Color shirt, skin; float seed; } Fan;
#define FAN_MAX 1800
static Fan gFans[FAN_MAX];
static int gFanCount = 0;
static float gStandX0, gStandZ0;   // 横の席の内側の x、奥の席の内側の z
#define STAND_ROWS  9
#define STAND_STEP  0.80f   // 1 段の奥行き
#define STAND_RISE  0.45f   // 1 段の高さ
static Model gCrowd[3];             // 0 = 左, 1 = 右, 2 = 奥
static bool  gCrowdOk = false;

// ---- 頂点を積み上げて 1 つのメッシュにする（色は面の向きで陰をつける） ----
typedef struct { float *v, *n; unsigned char *c; int count, cap; } MeshB;
static void MBPush(MeshB *m, Vector3 p, Vector3 n, Color c)
{
    if (m->count >= m->cap) {
        m->cap = m->cap ? m->cap * 2 : 4096;
        m->v = MemRealloc(m->v, m->cap * 3 * sizeof(float));
        m->n = MemRealloc(m->n, m->cap * 3 * sizeof(float));
        m->c = MemRealloc(m->c, m->cap * 4);
    }
    int i = m->count++;
    m->v[i * 3] = p.x; m->v[i * 3 + 1] = p.y; m->v[i * 3 + 2] = p.z;
    m->n[i * 3] = n.x; m->n[i * 3 + 1] = n.y; m->n[i * 3 + 2] = n.z;
    m->c[i * 4] = c.r; m->c[i * 4 + 1] = c.g; m->c[i * 4 + 2] = c.b; m->c[i * 4 + 3] = 255;
}
static void MBTri(MeshB *m, Vector3 a, Vector3 b, Vector3 c, Color col)
{
    Vector3 n = Vector3Normalize(Vector3CrossProduct(Vector3Subtract(b, a), Vector3Subtract(c, a)));
    float l = 0.70f + 0.30f * fmaxf(Vector3DotProduct(n, (Vector3){ 0.30f, 0.85f, 0.42f }), 0.0f);
    Color s = { (unsigned char)(col.r * l), (unsigned char)(col.g * l), (unsigned char)(col.b * l), 255 };
    MBPush(m, a, n, s); MBPush(m, b, n, s); MBPush(m, c, n, s);
}
static void MBQuad(MeshB *m, Vector3 a, Vector3 b, Vector3 c, Vector3 d, Color col) { MBTri(m, a, b, c, col); MBTri(m, a, c, d, col); }
// 向きのある箱：中心、3 つの軸（半分の長さを掛けたもの）
static void MBBox(MeshB *m, Vector3 o, Vector3 ax, Vector3 ay, Vector3 az, Color col)
{
    Vector3 P[8];
    for (int i = 0; i < 8; i++)
        P[i] = Vector3Add(o, Vector3Add(Vector3Scale(ax, (i & 1) ? 1.0f : -1.0f),
                             Vector3Add(Vector3Scale(ay, (i & 2) ? 1.0f : -1.0f), Vector3Scale(az, (i & 4) ? 1.0f : -1.0f))));
    MBQuad(m, P[0], P[2], P[3], P[1], col); MBQuad(m, P[4], P[5], P[7], P[6], col);   // -z, +z
    MBQuad(m, P[0], P[1], P[5], P[4], col); MBQuad(m, P[2], P[6], P[7], P[3], col);   // -y, +y
    MBQuad(m, P[0], P[4], P[6], P[2], col); MBQuad(m, P[1], P[3], P[7], P[5], col);   // -x, +x
}
static void MBSphere(MeshB *m, Vector3 c, float r, int rings, int slices, Color col)
{
    for (int i = 0; i < rings; i++) {
        float t0 = PI * i / rings, t1 = PI * (i + 1) / rings;
        for (int j = 0; j < slices; j++) {
            float p0 = 2 * PI * j / slices, p1 = 2 * PI * (j + 1) / slices;
            #define SP(t, p) (Vector3){ c.x + r * sinf(t) * cosf(p), c.y + r * cosf(t), c.z + r * sinf(t) * sinf(p) }
            Vector3 a = SP(t0, p0), b = SP(t0, p1), d = SP(t1, p0), e = SP(t1, p1);
            #undef SP
            if (i > 0) MBTri(m, a, b, e, col);
            if (i < rings - 1) MBTri(m, a, e, d, col);
        }
    }
}
// 太さの変わる筒（側面とふた）
static void MBCyl(MeshB *m, Vector3 a, Vector3 b, float ra, float rb, int sl, Color col)
{
    Vector3 ax = Vector3Normalize(Vector3Subtract(b, a));
    Vector3 u = fabsf(ax.y) < 0.9f ? Vector3Normalize(Vector3CrossProduct(ax, (Vector3){ 0, 1, 0 })) : (Vector3){ 1, 0, 0 };
    Vector3 w = Vector3CrossProduct(ax, u);
    for (int j = 0; j < sl; j++) {
        float p0 = 2 * PI * j / sl, p1 = 2 * PI * (j + 1) / sl;
        Vector3 d0 = Vector3Add(Vector3Scale(u, cosf(p0)), Vector3Scale(w, sinf(p0)));
        Vector3 d1 = Vector3Add(Vector3Scale(u, cosf(p1)), Vector3Scale(w, sinf(p1)));
        Vector3 a0 = Vector3Add(a, Vector3Scale(d0, ra)), a1 = Vector3Add(a, Vector3Scale(d1, ra));
        Vector3 b0 = Vector3Add(b, Vector3Scale(d0, rb)), b1 = Vector3Add(b, Vector3Scale(d1, rb));
        MBQuad(m, a0, a1, b1, b0, col);
        MBTri(m, b, b0, b1, col);
    }
}

// 座った観客 1 人。p：座面、f：顔の向き（コートの方）、r：右
static void BuildFan(MeshB *m, const Fan *fn, unsigned int *rs)
{
    #define RND() ((*rs = *rs * 1103515245u + 12345u) >> 8 & 0xffff) / 65535.0f
    Vector3 p = fn->p, f = fn->f, r = fn->r, up = { 0, 1, 0 };
    #define AT(fw, rt, y) Vector3Add(p, Vector3Add(Vector3Scale(f, (fw)), Vector3Add(Vector3Scale(r, (rt)), Vector3Scale(up, (y)))))
    const Color hairs[] = { { 40, 30, 25, 255 }, { 90, 60, 35, 255 }, { 200, 170, 110, 255 }, { 150, 150, 150, 255 }, { 20, 20, 20, 255 }, { 120, 60, 30, 255 } };
    const Color pants[] = { { 50, 55, 70, 255 }, { 200, 190, 170, 255 }, { 30, 30, 35, 255 }, { 80, 90, 120, 255 } };
    Color hair = hairs[(int)(RND() * 5.99f)], leg = pants[(int)(RND() * 3.99f)];
    float w = 0.85f + 0.3f * RND();   // 体格
    Color dark = { 30, 25, 25, 255 };
    // 脚：太ももは前へ、すねは下の段へ
    for (int s = -1; s <= 1; s += 2) {
        MBBox(m, AT(0.18f, s * 0.09f * w, 0.07f), Vector3Scale(f, 0.20f), Vector3Scale(up, 0.06f), Vector3Scale(r, 0.065f * w), leg);
        MBBox(m, AT(0.38f, s * 0.09f * w, -0.17f), Vector3Scale(f, 0.055f), Vector3Scale(up, 0.22f), Vector3Scale(r, 0.055f), leg);
    }
    // 胴：腰から肩へ（シャツ）。肩
    MBCyl(m, AT(-0.02f, 0, 0.02f), AT(0.0f, 0, 0.46f), 0.13f * w, 0.16f * w, 8, fn->shirt);
    MBBox(m, AT(0.0f, 0, 0.47f), Vector3Scale(f, 0.08f), Vector3Scale(up, 0.045f), Vector3Scale(r, 0.20f * w), fn->shirt);
    // 腕：肩から膝の上へ（袖と手）
    for (int s = -1; s <= 1; s += 2) {
        Vector3 sh = AT(0.0f, s * 0.19f * w, 0.44f), el = AT(0.08f, s * 0.21f * w, 0.20f), hd = AT(0.26f, s * 0.12f * w, 0.14f);
        MBCyl(m, sh, el, 0.055f, 0.048f, 6, fn->shirt);
        MBCyl(m, el, hd, 0.042f, 0.036f, 6, fn->skin);
    }
    // 首と頭。髪（後ろと上）、帽子の人も。顔：目・口・眉
    MBCyl(m, AT(0, 0, 0.48f), AT(0.01f, 0, 0.58f), 0.045f, 0.045f, 6, fn->skin);
    Vector3 hc = AT(0.02f, 0, 0.68f);
    MBSphere(m, hc, 0.105f, 6, 8, fn->skin);
    float hat = RND();
    if (hat < 0.15f) {   // キャップ
        Color cap = fn->shirt.r > 200 && fn->shirt.g > 200 ? (Color){ 30, 50, 110, 255 } : (Color){ 245, 245, 245, 255 };
        MBCyl(m, AT(0.0f, 0, 0.71f), AT(0.0f, 0, 0.79f), 0.112f, 0.10f, 8, cap);
        MBBox(m, AT(0.13f, 0, 0.725f), Vector3Scale(f, 0.07f), Vector3Scale(up, 0.008f), Vector3Scale(r, 0.08f), cap);
    } else if (hat < 0.22f) {   // つば広の帽子
        Color sh = { 235, 225, 200, 255 };
        MBCyl(m, AT(0.0f, 0, 0.72f), AT(0.0f, 0, 0.81f), 0.11f, 0.10f, 8, sh);
        MBCyl(m, AT(0.0f, 0, 0.72f), AT(0.0f, 0, 0.73f), 0.19f, 0.19f, 10, sh);
    } else if (RND() > 0.08f) {   // 髪（たまに坊主・はげ）
        MBSphere(m, AT(-0.025f, 0, 0.70f), 0.108f, 6, 8, hair);
        if (RND() < 0.35f) MBSphere(m, AT(-0.07f, 0, 0.60f), 0.09f, 5, 7, hair);   // 長い髪
    }
    Vector3 fc = AT(0.02f + 0.098f, 0, 0.68f);
    if (RND() < 0.12f) {   // サングラス
        MBBox(m, Vector3Add(fc, Vector3Scale(up, 0.02f)), Vector3Scale(f, 0.012f), Vector3Scale(up, 0.018f), Vector3Scale(r, 0.065f), dark);
    } else {
        for (int s = -1; s <= 1; s += 2) {
            MBBox(m, Vector3Add(fc, Vector3Add(Vector3Scale(r, s * 0.036f), Vector3Scale(up, 0.018f))), Vector3Scale(f, 0.012f), Vector3Scale(up, 0.012f), Vector3Scale(r, 0.011f), dark);       // 目
            MBBox(m, Vector3Add(Vector3Add(fc, Vector3Scale(f, -0.004f)), Vector3Add(Vector3Scale(r, s * 0.036f), Vector3Scale(up, 0.043f))), Vector3Scale(f, 0.01f), Vector3Scale(up, 0.006f), Vector3Scale(r, 0.02f), hair);   // 眉
        }
    }
    MBBox(m, Vector3Add(Vector3Add(fc, Vector3Scale(f, -0.012f)), Vector3Scale(up, -0.045f)), Vector3Scale(f, 0.01f), Vector3Scale(up, 0.008f), Vector3Scale(r, 0.028f), (Color){ 150, 70, 70, 255 });   // 口
    MBBox(m, Vector3Add(fc, Vector3Add(Vector3Scale(f, 0.012f), Vector3Scale(up, -0.008f))), Vector3Scale(f, 0.02f), Vector3Scale(up, 0.022f), Vector3Scale(r, 0.012f), fn->skin);   // 鼻
    #undef AT
    #undef RND
}

static void BuildFans(void)
{
    if (gFanCount) return;
    unsigned int r = 12345;
    #define RND() ((r = r * 1103515245u + 12345u) >> 8 & 0xffff) / 65535.0f
    gStandX0 = DOUBLES_W * 0.5f + 5.5f;
    gStandZ0 = HALF_LEN + 6.4f;
    const Color shirts[] = { { 230, 230, 235, 255 }, { 40, 60, 120, 255 }, { 200, 50, 50, 255 }, { 250, 210, 60, 255 }, { 60, 140, 80, 255 },
                             { 30, 30, 35, 255 }, { 120, 170, 220, 255 }, { 240, 150, 170, 255 }, { 150, 110, 70, 255 }, { 245, 245, 245, 255 } };
    const Color skins[] = { { 240, 205, 170, 255 }, { 225, 180, 140, 255 }, { 190, 140, 100, 255 }, { 140, 95, 65, 255 }, { 250, 220, 190, 255 } };
    MeshB mb[3] = { 0 };
    for (int side = 0; side < 3; side++) {      // 0 = 左, 1 = 右, 2 = 奥
        Vector3 f = side == 0 ? (Vector3){ 1, 0, 0 } : side == 1 ? (Vector3){ -1, 0, 0 } : (Vector3){ 0, 0, 1 };
        Vector3 rt = Vector3CrossProduct(f, (Vector3){ 0, 1, 0 });   // 顔の向きから見た右
        for (int row = 0; row < STAND_ROWS; row++) {
            float len = side < 2 ? 36.0f : 2.0f * (gStandX0 - 0.5f);
            int n = (int)(len / 0.62f);
            for (int i = 0; i < n && gFanCount < FAN_MAX; i++) {
                if (RND() < 0.12f) continue;   // 空席
                float u = -len * 0.5f + (i + 0.5f) * len / n;
                float out = 0.65f + row * STAND_STEP, y = 0.75f + row * STAND_RISE;   // 段の後ろ寄りに座る
                Vector3 p = side == 0 ? (Vector3){ -(gStandX0 + out), y, u } : side == 1 ? (Vector3){ gStandX0 + out, y, u }
                                      : (Vector3){ u, y, -(gStandZ0 + out) };
                Fan fn = { p, f, rt, shirts[(int)(RND() * 9.99f)], skins[(int)(RND() * 4.99f)], RND() * 6.28f };
                gFans[gFanCount++] = fn;
                BuildFan(&mb[side], &fn, &r);
            }
        }
    }
    #undef RND
    for (int i = 0; i < 3; i++) {
        Mesh mesh = { 0 };
        mesh.vertexCount = mb[i].count; mesh.triangleCount = mb[i].count / 3;
        mesh.vertices = mb[i].v; mesh.normals = mb[i].n; mesh.colors = mb[i].c;
        UploadMesh(&mesh, false);
        gCrowd[i] = LoadModelFromMesh(mesh);
    }
    gCrowdOk = true;
}

// 簡単な人（審判・線審・ボールキッズ）。base：足元（座っているなら椅子の座面の下の床）、f：顔の向き
//   pose 0 = 立つ、1 = 椅子に座る、2 = しゃがむ。armOut：右腕を横へ伸ばす（アウトのコール）
static void DrawPerson(Vector3 base, Vector3 f, int pose, Color shirt, Color pants, Color cap, Color skin, float armOut, Vector3 lookAt)
{
    Vector3 up = { 0, 1, 0 }, r = Vector3CrossProduct(f, up);
    #define AT(fw, rt, y) Vector3Add(base, Vector3Add(Vector3Scale(f, (fw)), Vector3Add(Vector3Scale(r, (rt)), Vector3Scale(up, (y)))))
    float hipY = pose == 0 ? 0.90f : pose == 1 ? 0.48f : 0.42f;
    for (int s = -1; s <= 1; s += 2) {
        Vector3 hip = AT(0, s * 0.10f, hipY), kn, ft;
        if (pose == 0)      { kn = AT(0.02f, s * 0.11f, 0.47f); ft = AT(0, s * 0.12f, 0.06f); }
        else if (pose == 1) { kn = AT(0.42f, s * 0.12f, 0.50f); ft = AT(0.46f, s * 0.12f, 0.06f); }
        else                { kn = AT(0.35f, s * 0.14f, s < 0 ? 0.50f : 0.12f); ft = AT(s < 0 ? 0.30f : -0.15f, s * 0.14f, 0.06f); }   // 片膝をつく
        DrawCylinderEx(hip, kn, 0.075f, 0.062f, 8, pants);
        DrawCylinderEx(kn, ft, 0.060f, 0.050f, 8, pants);
        DrawSphere(kn, 0.062f, pants);
        DrawCube(Vector3Add(ft, Vector3Scale(f, 0.06f)), fabsf(f.x) * 0.22f + fabsf(r.x) * 0.10f, 0.08f, fabsf(f.z) * 0.22f + fabsf(r.z) * 0.10f, (Color){ 30, 30, 30, 255 });
    }
    Vector3 hip = AT(0, 0, hipY), chest = AT(pose == 2 ? 0.10f : 0.02f, 0, hipY + 0.48f);
    DrawCylinderEx(hip, chest, 0.16f, 0.19f, 10, shirt);
    DrawCylinderEx(Vector3Add(chest, Vector3Scale(r, -0.21f)), Vector3Add(chest, Vector3Scale(r, 0.21f)), 0.075f, 0.075f, 8, shirt);
    for (int s = -1; s <= 1; s += 2) {
        Vector3 sh = Vector3Add(chest, Vector3Scale(r, s * 0.21f)), el, hd;
        if (s > 0 && armOut > 0.0f) {   // 右腕を横へまっすぐ伸ばす（アウト！）
            el = Vector3Add(sh, Vector3Add(Vector3Scale(r, 0.30f * armOut), Vector3Scale(up, -0.28f * (1.0f - armOut))));
            hd = Vector3Add(sh, Vector3Add(Vector3Scale(r, 0.60f * armOut), Vector3Scale(up, -0.55f * (1.0f - armOut) + 0.05f * armOut)));
        } else if (pose == 1) { el = Vector3Add(sh, (Vector3){ 0, -0.28f, 0 }); el = Vector3Add(el, Vector3Scale(f, 0.08f)); hd = AT(0.36f, s * 0.13f, hipY + 0.10f); }
        else                  { el = Vector3Add(sh, (Vector3){ 0, -0.28f, 0 }); hd = Vector3Add(el, Vector3Add((Vector3){ 0, -0.24f, 0 }, Vector3Scale(f, pose == 2 ? 0.20f : 0.04f))); }
        DrawCylinderEx(sh, el, 0.05f, 0.045f, 6, shirt);
        DrawCylinderEx(el, hd, 0.042f, 0.038f, 6, armOut > 0.0f && s > 0 ? shirt : skin);
        DrawSphereEx(hd, 0.045f, 4, 6, skin);
    }
    Vector3 head = AT(pose == 2 ? 0.12f : 0.03f, 0, hipY + 0.73f);
    DrawCylinderEx(chest, head, 0.05f, 0.05f, 6, skin);
    DrawSphereEx(head, 0.11f, 6, 8, skin);
    // 顔の向き：球を見る
    Vector3 lk = Vector3Subtract(lookAt, head); lk.y = 0;
    Vector3 ff = Vector3Length(lk) > 0.01f ? Vector3Normalize(lk) : f;
    if (Vector3DotProduct(ff, f) < 0.0f) ff = Vector3Normalize(Vector3Add(ff, Vector3Scale(f, 1.2f)));   // 真後ろは向かない
    DrawSphereEx(Vector3Add(head, Vector3Add(Vector3Scale(ff, -0.03f), (Vector3){ 0, 0.03f, 0 })), 0.105f, 6, 8, (Color){ 70, 55, 45, 255 });   // 髪
    if (cap.a) {
        DrawCylinderEx(Vector3Add(head, (Vector3){ 0, 0.03f, 0 }), Vector3Add(head, (Vector3){ 0, 0.12f, 0 }), 0.118f, 0.105f, 10, cap);
        DrawCube(Vector3Add(head, Vector3Add(Vector3Scale(ff, 0.15f), (Vector3){ 0, 0.045f, 0 })), 0.12f, 0.015f, 0.12f, cap);   // つば
    }
    Vector3 fc = Vector3Add(head, Vector3Scale(ff, 0.10f));
    Vector3 rr = Vector3CrossProduct(ff, up);
    for (int s = -1; s <= 1; s += 2) DrawSphereEx(Vector3Add(fc, Vector3Add(Vector3Scale(rr, s * 0.035f), (Vector3){ 0, 0.02f, 0 })), 0.014f, 3, 4, (Color){ 30, 25, 25, 255 });
    DrawSphereEx(Vector3Add(fc, Vector3Scale(ff, 0.01f)), 0.022f, 3, 4, skin);
    #undef AT
}

void DrawStadium(Vector3 camPos, Vector3 ballPos, float excite, float time, Vector3 callPos, float callT)
{
    BuildFans();
    Color wall = gSurface == SURF_HARD ? (Color){ 30, 60, 120, 255 } : (Color){ 25, 70, 45, 255 };
    Color step = { 150, 150, 155, 255 }, step2 = { 135, 135, 140, 255 };
    // カメラがその席より外にいたら、その席は描かない
    bool showL = camPos.x > -gStandX0, showR = camPos.x < gStandX0, showB = camPos.z > -gStandZ0;
    float H = STAND_ROWS * STAND_RISE, D = STAND_ROWS * STAND_STEP;
    for (int s = -1; s <= 1; s += 2) {
        if ((s < 0 && !showL) || (s > 0 && !showR)) continue;
        DrawCube((Vector3){ s * (gStandX0 - 0.1f), 0.6f, 0 }, 0.2f, 1.2f, 36.0f, wall);
        for (int row = 0; row < STAND_ROWS; row++)
            DrawCube((Vector3){ s * (gStandX0 + 0.5f + row * STAND_STEP), (0.75f + row * STAND_RISE) * 0.5f, 0 },
                     STAND_STEP, 0.75f + row * STAND_RISE, 36.0f, row % 2 ? step : step2);
        DrawCube((Vector3){ s * (gStandX0 + 0.5f + D), H + 1.0f, 0 }, 0.3f, 2.0f, 36.0f, wall);   // 一番上の壁
    }
    if (showB) {
        float w = 2.0f * gStandX0;
        DrawCube((Vector3){ 0, 0.6f, -(gStandZ0 - 0.1f) }, w, 1.2f, 0.2f, wall);
        for (int row = 0; row < STAND_ROWS; row++)
            DrawCube((Vector3){ 0, (0.75f + row * STAND_RISE) * 0.5f, -(gStandZ0 + 0.5f + row * STAND_STEP) },
                     w, 0.75f + row * STAND_RISE, STAND_STEP, row % 2 ? step : step2);
        DrawCube((Vector3){ 0, H + 1.0f, -(gStandZ0 + 0.5f + D) }, w, 2.0f, 0.3f, wall);
    }
    // 観客（席ごとのモデル）。盛り上がると少し弾む
    if (gCrowdOk) {
        bool show[3] = { showL, showR, showB };
        for (int i = 0; i < 3; i++)
            if (show[i]) DrawModel(gCrowd[i], (Vector3){ 0, excite * 0.04f * fabsf(sinf(time * 8.0f + i)), 0 }, 1.0f, WHITE);
    }
    // 盛り上がったら：半分くらいの人が両手を上げて拍手する
    if (excite > 0.05f) {
        for (int i = 0; i < gFanCount; i++) {
            const Fan *f = &gFans[i];
            if ((f->p.x < -gStandX0 && !showL) || (f->p.x > gStandX0 && !showR) || (f->p.z < -gStandZ0 && !showB)) continue;
            float up = excite * (0.5f + 0.5f * sinf(f->seed * 3.0f));
            if (up < 0.45f) continue;
            float cl = 0.06f * sinf(time * 14.0f + f->seed * 4.0f);
            for (int s = -1; s <= 1; s += 2) {
                Vector3 sh = Vector3Add(f->p, Vector3Add(Vector3Scale(f->r, s * 0.19f), (Vector3){ 0, 0.47f, 0 }));
                Vector3 hd = Vector3Add(f->p, Vector3Add(Vector3Add(Vector3Scale(f->r, s * (0.08f + cl)), Vector3Scale(f->f, 0.12f)), (Vector3){ 0, 1.02f, 0 }));
                DrawCylinderEx(sh, hd, 0.05f, 0.04f, 5, f->shirt);
                DrawSphereEx(hd, 0.045f, 3, 5, f->skin);
            }
        }
    }

    // 審判台：ネットの横（左）。審判は座って、頭で球を追う
    Color uniform = { 235, 230, 215, 255 }, uniPants = { 40, 45, 70, 255 }, uniCap = { 40, 45, 70, 255 }, skin = { 230, 195, 160, 255 };
    {
        float ux = -(NET_POST_X + 1.0f);
        Color metal = { 70, 75, 80, 255 };
        for (int i = 0; i < 4; i++) {
            float lx = ux + ((i & 1) ? 0.40f : -0.40f), lz = (i & 2) ? 0.40f : -0.40f;
            DrawCylinderEx((Vector3){ lx, 0, lz }, (Vector3){ lx, 2.0f, lz * 0.9f }, 0.04f, 0.035f, 6, metal);
        }
        for (int k = 1; k <= 4; k++) DrawCube((Vector3){ ux - 0.41f, k * 0.42f, 0 }, 0.05f, 0.04f, 0.7f, metal);   // はしご
        DrawCube((Vector3){ ux, 2.02f, 0 }, 0.90f, 0.06f, 0.90f, metal);                                    // 台
        DrawCube((Vector3){ ux + 0.44f, 2.30f, 0 }, 0.05f, 0.50f, 0.90f, wall);                              // 前の囲い
        DrawCube((Vector3){ ux - 0.30f, 2.45f, 0 }, 0.08f, 0.85f, 0.60f, metal);                             // 背もたれ
        DrawPerson((Vector3){ ux - 0.12f, 2.05f, 0 }, (Vector3){ 1, 0, 0 }, 1, uniform, uniPants, uniCap, skin, 0.0f, ballPos);
    }
    // 線審：コートのまわりの椅子に座る。アウトのときは、球に一番近い線審が立って右腕を横へ伸ばす
    {
        Vector3 judges[12];
        Vector3 facing[12];
        int nj = 0;
        float bz = HALF_LEN + 5.4f, sxw = gStandX0 - 0.7f, sx = SINGLES_W * 0.5f;
        for (int e = -1; e <= 1; e += 2) {   // 両方のベースラインの後ろ（サイドラインの延長）
            for (int s = -1; s <= 1; s += 2) { judges[nj] = (Vector3){ s * sx, 0, e * bz }; facing[nj++] = (Vector3){ 0, 0, (float)-e }; }
        }
        for (int s = -1; s <= 1; s += 2)     // 横の壁際（ベースラインとサービスラインの延長）
            for (int e = -1; e <= 1; e += 2) {
                judges[nj] = (Vector3){ s * sxw, 0, e * HALF_LEN }; facing[nj++] = (Vector3){ (float)-s, 0, 0 };
                judges[nj] = (Vector3){ s * sxw, 0, e * SERVICE_Z }; facing[nj++] = (Vector3){ (float)-s, 0, 0 };
            }
        int caller = -1;
        if (callT > 0.0f) {
            float best = 1e9f;
            for (int i = 0; i < nj; i++) { float d = Vector3Distance(judges[i], (Vector3){ callPos.x, 0, callPos.z }); if (d < best) { best = d; caller = i; } }
        }
        for (int i = 0; i < nj; i++) {
            if (judges[i].z > camPos.z - 2.5f) continue;   // カメラの手前（すぐ前）は描かない
            if ((judges[i].x < -gStandX0 + 1.0f && !showL) || (judges[i].x > gStandX0 - 1.0f && !showR)) continue;
            bool call = i == caller;
            float arm = call ? Clamp01((1.6f - callT) / 0.15f) * Clamp01(callT / 0.3f) : 0.0f;
            if (!call) {   // 椅子
                Vector3 c = Vector3Add(judges[i], Vector3Scale(facing[i], -0.05f));
                DrawCube((Vector3){ c.x, 0.45f, c.z }, 0.45f, 0.06f, 0.45f, (Color){ 60, 60, 65, 255 });
                DrawCube(Vector3Add((Vector3){ c.x, 0.75f, c.z }, Vector3Scale(facing[i], -0.22f)), fabsf(facing[i].z) * 0.45f + 0.05f, 0.6f, fabsf(facing[i].x) * 0.45f + 0.05f, (Color){ 60, 60, 65, 255 });
            }
            DrawPerson(judges[i], facing[i], call ? 0 : 1, uniform, uniPants, uniCap, skin, arm, ballPos);
        }
    }
    // ボールキッズ：ネットの両端にしゃがむ
    {
        Color kid = { 110, 60, 150, 255 }, kidPants = { 40, 40, 50, 255 };
        DrawPerson((Vector3){ -(NET_POST_X + 0.3f), 0, 0.9f }, (Vector3){ 1, 0, 0 }, 2, kid, kidPants, (Color){ 0 }, (Color){ 225, 185, 150, 255 }, 0.0f, ballPos);
        DrawPerson((Vector3){ NET_POST_X + 0.6f, 0, -0.6f }, (Vector3){ -1, 0, 0 }, 2, kid, kidPants, (Color){ 0 }, (Color){ 200, 150, 115, 255 }, 0.0f, ballPos);
    }
}

// ================= 体とラケットの描画 =================
// 体から見た座標（x=右、y=高さ、z=前）をワールド座標に直す。facingNegZ: -z 向き（自分）か +z 向き（AI）か
Vector3 LocalToWorld(Vector3 base, bool facingNegZ, Vector3 l)
{
    float sx = facingNegZ ? 1.0f : -1.0f;
    return (Vector3){ base.x + sx * l.x, base.y + l.y, base.z - sx * l.z };
}

float EaseOut(float u) { u = Clamp01(u); return 1.0f - (1.0f - u) * (1.0f - u); }

// ---- ラケットの動き ----
// 手（グリップ）の位置と、ラケットの向き（手から先端へ）を別々に動かす。
// 腕とラケットが一本の棒にならず、手首が返る・ラケットが遅れて出てくる・頭の後ろに巻きつく、が出る。
// 体から見た座標：x=右, y=高さ, z=前（ネットの方）。右利きで計算する
typedef struct { float t; Vector3 h, d, f; } RKey;   // 時刻、手、ラケットの向き、面の向き（だいたい）

static Vector3 V3(float x, float y, float z) { return (Vector3){ x, y, z }; }
static Vector3 CatRom(Vector3 p0, Vector3 p1, Vector3 p2, Vector3 p3, float s)
{
    float s2 = s * s, s3 = s2 * s;
    Vector3 r;
    r.x = 0.5f * (2 * p1.x + (-p0.x + p2.x) * s + (2 * p0.x - 5 * p1.x + 4 * p2.x - p3.x) * s2 + (-p0.x + 3 * p1.x - 3 * p2.x + p3.x) * s3);
    r.y = 0.5f * (2 * p1.y + (-p0.y + p2.y) * s + (2 * p0.y - 5 * p1.y + 4 * p2.y - p3.y) * s2 + (-p0.y + 3 * p1.y - 3 * p2.y + p3.y) * s3);
    r.z = 0.5f * (2 * p1.z + (-p0.z + p2.z) * s + (2 * p0.z - 5 * p1.z + 4 * p2.z - p3.z) * s2 + (-p0.z + 3 * p1.z - 3 * p2.z + p3.z) * s3);
    return r;
}
// キーの間をなめらかにつなぐ（キャットマル–ロム曲線）。向きは正規化する
static void SampleKeys(const RKey *k, int n, float t, Vector3 *h, Vector3 *d, Vector3 *f)
{
    if (t <= k[0].t) { *h = k[0].h; *d = Vector3Normalize(k[0].d); *f = k[0].f; return; }
    if (t >= k[n - 1].t) { *h = k[n - 1].h; *d = Vector3Normalize(k[n - 1].d); *f = k[n - 1].f; return; }
    int i = 0;
    while (i < n - 2 && t > k[i + 1].t) i++;
    float s = (t - k[i].t) / fmaxf(k[i + 1].t - k[i].t, 1e-4f);
    int i0 = i > 0 ? i - 1 : i, i3 = i + 2 < n ? i + 2 : i + 1;
    *h = CatRom(k[i0].h, k[i].h, k[i + 1].h, k[i3].h, s);
    *d = Vector3Normalize(CatRom(Vector3Normalize(k[i0].d), Vector3Normalize(k[i].d), Vector3Normalize(k[i + 1].d), Vector3Normalize(k[i3].d), s));
    *f = CatRom(k[i0].f, k[i].f, k[i + 1].f, k[i3].f, s);
}
void DrawRacketBent(Vector3 hand, Vector3 dir, Vector3 normal, Color frame, float bend);
static Vector3 MirrorX(Vector3 v) { return (Vector3){ -v.x, v.y, v.z }; }

#define RACKET_REACH 0.50f   // 手から面の中心まで（m）
#define ARM_REACH    0.60f   // 肩から手まで届く長さ

// 構え（何もしていないとき）：ラケットを体の前で立てて、左手を首に添える
static const RKey READY = { 0, { 0.20f, 1.02f, 0.36f }, { 0.05f, 0.50f, 0.86f }, { 1.0f, 0.0f, 0.0f } };

// 当たる瞬間：球の位置に面の中心が来るように、手の位置とラケットの角度を決める（肩から届く範囲で）
static RKey ContactKey(Vector3 cl, float sgn, bool oneArm, bool slice)
{
    float rc = sqrtf(cl.x * cl.x + cl.z * cl.z), k = rc > 1.6f ? 1.6f / rc : (rc < 0.5f ? 0.5f / fmaxf(rc, 0.01f) : 1.0f);
    Vector3 p = { cl.x * k, Clamp(cl.y, 0.15f, 1.9f), cl.z * k };
    float down = Clamp((p.y - 1.05f) * 0.55f, -0.55f, 0.20f);   // 低い球はラケットの頭を下げて拾う
    Vector3 d = Vector3Normalize(V3(sgn * 0.93f, down, slice ? 0.30f : 0.18f));
    Vector3 h = Vector3Subtract(p, Vector3Scale(d, RACKET_REACH));
    // 肩から届かないなら、手を肩の方へ寄せて、ラケットを伸ばして当てる
    Vector3 sh = oneArm && sgn < 0.0f ? V3(0.05f, 1.42f, 0.18f) : V3(sgn * 0.18f, 1.42f, 0.05f);
    Vector3 sv = Vector3Subtract(h, sh);
    float sl = Vector3Length(sv);
    if (sl > ARM_REACH) { h = Vector3Add(sh, Vector3Scale(sv, ARM_REACH / sl)); d = Vector3Normalize(Vector3Subtract(p, h)); }
    Vector3 f = slice ? V3(0.0f, 0.45f, 1.0f) : V3(0.0f, -0.12f, 1.0f);   // スライスは面が上、トップは少し伏せる
    return (RKey){ SWING_LEAD, h, d, f };
}

// ストロークのキー（時刻は振り出しからの秒。SWING_LEAD で当たる）
//   grace：優雅さ（フェデラー）。フォロースルーが高く大きく、最後まで振り切る
static int StrokeKeys(const SwingAnim *a, RKey *k, int *strokeOut)
{
    const Contact *c = &a->lastContact;
    bool fore = c->forehand;
    float sgn = fore ? 1.0f : -1.0f;
    float g = a->grace;
    *strokeOut = fore ? 0 : 1;
    RKey ck = ContactKey(a->contactLocal, sgn, a->oneHanded, a->slice);
    int n = 0;
    if (a->slice) {
        // スライス：ラケットを肩より高く引き、面を開いて、上から斜め下へ運ぶ。フォロースルーは前へ長く
        k[n++] = (RKey){ 0.00f, { 0.42f, 1.48f, -0.12f }, { 0.30f, 0.72f, -0.62f }, { 0.4f, 0.6f, 0.6f } };
        k[n++] = (RKey){ 0.04f, { 0.52f, 1.28f, 0.08f }, { 0.65f, 0.35f, -0.45f }, { 0.2f, 0.6f, 0.8f } };
        k[n++] = ck;
        k[n++] = (RKey){ 0.15f, { 0.10f, 1.02f, 0.72f }, { -0.15f, 0.25f, 0.95f }, { 0.0f, 0.8f, 0.4f } };
        k[n++] = (RKey){ 0.30f, { -0.12f + 0.05f * g, 1.08f + 0.08f * g, 0.70f }, { -0.30f, 0.45f, 0.85f }, { -0.2f, 0.9f, 0.2f } };
        if (!fore) for (int i = 0; i < n; i++) if (i != 2) { k[i].h = MirrorX(k[i].h); k[i].d = MirrorX(k[i].d); k[i].f = MirrorX(k[i].f); }
        return n;
    }
    if (fore && a->fhStyle == 1) {
        // ナダルのフォア（投げ縄）：ラケットを高く引き → 膝の下まで深く落とし → 下から真上へ一気に持ち上げ →
        //   肘を高く上げ、打った側の頭の上で振り終える（反対の肩に巻きつけない）。強い順回転のための極端な下から上
        k[n++] = (RKey){ 0.000f, { 0.55f, 1.42f, -0.22f }, { 0.05f, 0.90f, -0.42f }, { 1.0f, -0.3f, 0.2f } };
        k[n++] = (RKey){ 0.045f, { 0.58f, 0.60f, -0.25f }, { 0.20f, -0.82f, -0.50f }, { 0.4f, -0.4f, 1.0f } };
        {   // 球の下から入る：ラケットの頭を少し下げる（面の中心は球の位置のまま）
            Vector3 p = Vector3Add(ck.h, Vector3Scale(ck.d, RACKET_REACH));
            ck.d = Vector3Normalize(Vector3Add(ck.d, V3(0.0f, -0.15f, 0.0f)));
            ck.h = Vector3Subtract(p, Vector3Scale(ck.d, RACKET_REACH));
        }
        k[n++] = ck;
        k[n++] = (RKey){ 0.12f, { 0.40f, 1.38f, 0.42f }, { 0.12f, 0.96f, 0.22f }, { 0.0f, -0.5f, 0.85f } };
        k[n++] = (RKey){ 0.19f, { 0.22f, 1.88f, 0.12f }, { -0.42f, 0.40f, -0.80f }, { -0.3f, -0.9f, 0.2f } };
        k[n++] = (RKey){ 0.30f, { 0.12f, 1.95f, 0.00f }, { -0.55f, 0.22f, -0.80f }, { -0.2f, -0.95f, 0.1f } };
        return n;
    }
    if (fore) {
        // フォア：肩を入れてラケットを立てて引き（ユニットターン）→ ラケットの頭が背中側に落ちる →
        //   下から前へ振り抜いて → 左肩の上に巻きつける（ワイパー）
        k[n++] = (RKey){ 0.000f, { 0.50f, 1.28f, -0.25f }, { 0.10f, 0.80f, -0.60f }, { 1.0f, -0.3f, 0.3f } };
        k[n++] = (RKey){ 0.045f, { 0.55f, 0.82f, -0.28f }, { 0.25f, -0.65f, -0.70f }, { 0.4f, -0.3f, 1.0f } };
        k[n++] = ck;
        k[n++] = (RKey){ 0.13f, { 0.15f, 1.20f + 0.05f * g, 0.72f }, { 0.30f, 0.70f, 0.65f }, { -0.3f, -0.6f, 0.7f } };
        k[n++] = (RKey){ 0.20f, { -0.18f, 1.40f + 0.10f * g, 0.40f }, { -0.30f, 0.55f + 0.15f * g, -0.75f }, { -0.8f, -0.3f, 0.4f } };
        k[n++] = (RKey){ 0.30f, { -0.30f, 1.38f + 0.14f * g, 0.18f + 0.06f * g }, { -0.35f, 0.15f + 0.30f * g, -0.92f }, { -0.6f, -0.7f, 0.2f } };
        return n;
    }
    if (a->oneHanded) {
        // 片手バック：左手でラケットの首を持って大きく肩を入れ → 振り出しで左手を離す →
        //   腕を伸ばしたまま高く振り抜き、左手は後ろへ（両腕を広げて終わる）
        k[n++] = (RKey){ 0.000f, { -0.40f, 1.24f, -0.15f }, { -0.25f, 0.80f, -0.55f }, { -1.0f, -0.3f, 0.3f } };
        k[n++] = (RKey){ 0.045f, { -0.36f, 0.92f, -0.04f }, { -0.35f, -0.50f, -0.78f }, { -0.3f, -0.2f, 1.0f } };
        k[n++] = ck;
        k[n++] = (RKey){ 0.13f, { -0.02f, 1.32f, 0.78f }, { 0.10f, 0.80f, 0.55f }, { 0.3f, -0.5f, 0.8f } };
        k[n++] = (RKey){ 0.20f, { 0.32f, 1.66f + 0.06f * g, 0.48f }, { 0.30f, 0.90f, -0.25f }, { 0.9f, 0.0f, 0.3f } };
        k[n++] = (RKey){ 0.30f, { 0.40f, 1.70f + 0.08f * g, 0.36f }, { 0.45f, 0.82f, -0.35f }, { 0.9f, 0.0f, 0.3f } };
        return n;
    }
    // 両手バック：左手が主役のフォアのような形。右肩の上で終わる
    k[n++] = (RKey){ 0.000f, { -0.42f, 1.18f, -0.25f }, { -0.20f, 0.80f, -0.55f }, { -1.0f, -0.3f, 0.3f } };
    k[n++] = (RKey){ 0.045f, { -0.50f, 0.84f, -0.25f }, { -0.30f, -0.60f, -0.70f }, { -0.4f, -0.3f, 1.0f } };
    k[n++] = ck;
    k[n++] = (RKey){ 0.13f, { -0.05f, 1.15f, 0.70f }, { -0.10f, 0.70f, 0.70f }, { 0.3f, -0.6f, 0.7f } };
    k[n++] = (RKey){ 0.20f, { 0.22f, 1.38f, 0.36f }, { 0.30f, 0.50f, -0.80f }, { 0.8f, -0.3f, 0.4f } };
    k[n++] = (RKey){ 0.30f, { 0.30f, 1.38f, 0.14f }, { 0.35f, 0.20f, -0.90f }, { 0.6f, -0.7f, 0.2f } };
    return n;
}

// 構え（溜め）の終わりの形
static RKey WindupKey(int stroke, bool slice, bool oneHanded, int fhStyle)
{
    RKey k[8]; int s;
    SwingAnim t = { 0 };
    t.lastContact.forehand = (stroke == 0); t.slice = slice; t.oneHanded = oneHanded; t.fhStyle = fhStyle;
    t.contactLocal = V3(stroke == 0 ? 0.85f : -0.85f, 1.0f, 0.4f);
    StrokeKeys(&t, k, &s);
    return k[0];
}

// ---- サーブ（とスマッシュ）：構え → トス → トロフィー → ラケットダウン → 打点 → プロネーション → 左の腰へ ----
// 構え：横向きに立ち、ラケットの先をサービスボックスへ向け、ボールを持った左手をラケットののどに添える
static const RKey SERVE_STANCE = { 0.00f, { 0.10f, 1.05f, 0.35f }, { 0.00f, 0.25f, 0.97f }, { 1.0f, 0.0f, 0.0f } };
// 溜め（windup 0〜1）：振り子のように下から後ろへ回し、肘を肩の高さに上げてラケットを立てる（トロフィー）
static const RKey SERVE_WIND[] = {
    { 0.00f, { 0.10f, 1.05f, 0.35f }, { 0.00f, 0.25f, 0.97f }, { 1.0f, 0.0f, 0.0f } },
    { 0.30f, { 0.12f, 0.98f, 0.28f }, { 0.05f, 0.10f, 0.99f }, { 1.0f, 0.0f, 0.0f } },
    { 0.60f, { 0.40f, 0.85f, -0.05f }, { 0.25f, -0.55f, -0.80f }, { 1.0f, 0.0f, 0.2f } },
    { 0.85f, { 0.42f, 1.30f, -0.25f }, { 0.30f, 0.40f, -0.85f }, { 1.0f, 0.0f, 0.0f } },
    { 1.00f, { 0.34f, 1.58f, -0.16f }, { 0.10f, 0.85f, -0.50f }, { 1.0f, -0.2f, 0.0f } },
};
static int ServeSwingKeys(Vector3 cl, RKey *k)
{
    int n = 0;
    k[n] = SERVE_WIND[4]; k[n].t = 0.0f; n++;   // トロフィーから
    k[n++] = (RKey){ 0.045f, { 0.30f, 1.66f, -0.08f }, { 0.05f, -0.92f, -0.38f }, { 1.0f, 0.0f, -0.2f } };   // ラケットの頭が背中に落ちる
    // 打点：腕を伸ばしきって、ほぼ真上で当てる
    Vector3 p = { Clamp(cl.x, -0.4f, 0.8f), Clamp(cl.y, 1.9f, 3.0f), Clamp(cl.z, -0.2f, 1.0f) };
    Vector3 d = Vector3Normalize(V3(0.08f, 0.95f, 0.30f));
    Vector3 h = Vector3Subtract(p, Vector3Scale(d, RACKET_REACH));
    Vector3 sh = { 0.20f, 1.62f, 0.05f };   // 跳んで伸び上がった肩
    Vector3 sv = Vector3Subtract(h, sh);
    float sl = Vector3Length(sv);
    if (sl > 0.66f) { h = Vector3Add(sh, Vector3Scale(sv, 0.66f / sl)); d = Vector3Normalize(Vector3Subtract(p, h)); }
    k[n++] = (RKey){ SWING_LEAD, h, d, { 0.0f, 0.1f, 1.0f } };
    k[n++] = (RKey){ 0.13f, { 0.22f, 1.80f, 0.62f }, { 0.40f, 0.05f, 0.92f }, { 0.9f, -0.3f, 0.2f } };   // 手首を内側にひねる（プロネーション）
    k[n++] = (RKey){ 0.20f, { -0.05f, 1.25f, 0.58f }, { -0.25f, -0.75f, 0.60f }, { 0.3f, -0.5f, -0.8f } };
    k[n++] = (RKey){ 0.30f, { -0.35f, 0.95f, 0.22f }, { -0.40f, -0.85f, -0.15f }, { 0.0f, -0.2f, -1.0f } };  // 左の腰の横で終わる
    return n;
}

// スマッシュ：振り子はなし。横を向いてラケットを真上へ担ぎ上げ（トロフィー）、左手で球を指す。
//   振るとラケットの頭が背中に落ち、前で高く当てて、上から叩き下ろす
static const RKey SMASH_WIND[] = {
    { 0.00f, { 0.20f, 1.02f, 0.36f }, { 0.05f, 0.50f, 0.86f }, { 1.0f, 0.0f, 0.0f } },
    { 0.50f, { 0.30f, 1.45f, 0.00f }, { 0.10f, 0.80f, -0.50f }, { 1.0f, 0.0f, 0.0f } },
    { 1.00f, { 0.34f, 1.60f, -0.18f }, { 0.10f, 0.85f, -0.50f }, { 1.0f, -0.2f, 0.0f } },
};
static int SmashSwingKeys(Vector3 cl, RKey *k)
{
    int n = 0;
    k[n] = SMASH_WIND[2]; k[n].t = 0.0f; n++;
    k[n++] = (RKey){ 0.04f, { 0.30f, 1.62f, -0.06f }, { 0.05f, -0.85f, -0.50f }, { 1.0f, 0.0f, -0.2f } };
    Vector3 p = { Clamp(cl.x, -0.4f, 0.8f), Clamp(cl.y, 1.9f, 3.0f), Clamp(cl.z, 0.30f, 1.0f) };   // 体の前で当てる
    Vector3 d = Vector3Normalize(V3(0.05f, 0.92f, 0.38f));
    Vector3 h = Vector3Subtract(p, Vector3Scale(d, RACKET_REACH));
    Vector3 sh = { 0.20f, 1.62f, 0.05f };
    Vector3 sv = Vector3Subtract(h, sh);
    float sl = Vector3Length(sv);
    if (sl > 0.66f) { h = Vector3Add(sh, Vector3Scale(sv, 0.66f / sl)); d = Vector3Normalize(Vector3Subtract(p, h)); }
    k[n++] = (RKey){ SWING_LEAD, h, d, { 0.0f, -0.1f, 1.0f } };
    k[n++] = (RKey){ 0.13f, { 0.20f, 1.70f, 0.65f }, { 0.30f, -0.10f, 0.95f }, { 0.4f, -0.8f, 0.4f } };   // 上から叩き下ろす
    k[n++] = (RKey){ 0.20f, { -0.05f, 1.15f, 0.55f }, { -0.20f, -0.85f, 0.50f }, { 0.3f, -0.5f, -0.8f } };
    k[n++] = (RKey){ 0.30f, { -0.32f, 0.95f, 0.20f }, { -0.35f, -0.90f, -0.10f }, { 0.0f, -0.2f, -1.0f } };
    return n;
}

// ---- ボレー ----
static int VolleyKeys(Vector3 cl, float sgn, RKey *k)
{
    float rcl = sqrtf(cl.x * cl.x + cl.z * cl.z), kk = rcl > 1.2f ? 1.2f / rcl : 1.0f;
    Vector3 p = { cl.x * kk, Clamp(cl.y, 0.2f, 2.0f), fmaxf(cl.z * kk, 0.35f) };   // 当てるのは体の前
    float up = Clamp((p.y - 0.85f) * 0.9f, -0.25f, 0.65f);                         // 低い球ほどラケットを寝かせる（頭はなるべく落とさない）
    Vector3 d = Vector3Normalize(V3(sgn * 0.62f, up + 0.15f, 0.55f));
    Vector3 h = Vector3Subtract(p, Vector3Scale(d, RACKET_REACH));
    Vector3 sh = { sgn * 0.18f, 1.42f - Clamp((0.95f - p.y) * 0.6f, 0.0f, 0.32f), 0.10f };   // 低い球は膝を曲げて肩も下がる
    Vector3 sv = Vector3Subtract(h, sh);
    float sl = Vector3Length(sv);
    if (sl > ARM_REACH) { h = Vector3Add(sh, Vector3Scale(sv, ARM_REACH / sl)); d = Vector3Normalize(Vector3Subtract(p, h)); }
    float open = Clamp((1.0f - p.y) * 0.6f, 0.0f, 0.5f);                           // 低い球は面を上に開く
    Vector3 f = { 0.0f, 0.20f + open, 1.0f };
    // 構え：手を少し後ろ・上へ、ラケットの頭を後ろへ倒す（手首を返す）
    Vector3 hp = { h.x - sgn * 0.04f, h.y + 0.08f, h.z - 0.30f };
    Vector3 dp = Vector3Normalize(Vector3Add(d, V3(0.0f, 0.20f, -0.55f)));
    int n = 0;
    k[n++] = (RKey){ 0.00f, hp, dp, f };
    k[n++] = (RKey){ SWING_LEAD, h, d, f };
    k[n++] = (RKey){ 0.16f, { h.x, h.y - 0.06f, h.z + 0.22f }, d, f };                // 短く押し出して止める
    k[n++] = (RKey){ 0.30f, { h.x * 0.9f, h.y - 0.04f, h.z + 0.18f }, d, f };
    return n;
}

// ---- ポイント後のしぐさ ----
// 両手（ラケットを持つ手とその向き、もう片方の手）と体（沈み込み・前傾・跳ぶ・膝上げ・膝つき・顔の向き）を時間で決める。
// 手の高さは立っているとき（腰 0.88m）の値。体が沈んだ分は描くときに下げる
typedef struct { Vector3 rh, rd, lh; float hipDrop, lean, jump, liftL, liftR, kneel, headUp, headShake, k, e; } CelebOut;
static float Pulse(float e, float c, float w) { float x = (e - c) / w; return (x > -1.0f && x < 1.0f) ? 0.5f + 0.5f * cosf(x * PI) : 0.0f; }
float Smooth01(float x) { x = Clamp01(x); return x * x * (3.0f - 2.0f * x); }
bool CelebPose(const SwingAnim *a, CelebOut *o)
{
    if (a->celebT <= 0.0f || a->celebKind == CELEB_NONE || a->smashT > 0.0f) return false;
    float dur = a->celebDur > 0.0f ? a->celebDur : CELEB_TIME;
    float e = dur - a->celebT, k = Clamp01(e / 0.18f) * Clamp01(a->celebT / 0.30f);
    Vector3 rh0 = { 0.32f, 0.88f, 0.12f }, rd0 = { 0.15f, -0.55f, 0.82f }, lh0 = { -0.28f, 1.18f, 0.15f };
    Vector3 rh = rh0, rd = rd0, lh = lh0;
    CelebOut t = { 0 };
    switch (a->celebKind) {
        case CELEB_FIST: {   // 前にかがんで、拳を胸の前から腰の後ろへ強く 2 回引き下ろす「カモン！」
            float s = Pulse(e, 0.32f, 0.16f) + Pulse(e, 0.66f, 0.16f);
            //   拳は肩の高さから腰の後ろまで大きく。膝を曲げて体ごと沈む
            lh = Vector3Lerp(V3(-0.08f, 1.55f, 0.42f), V3(-0.36f, 0.92f, -0.08f), s);
            rh = V3(0.45f, 0.98f, 0.00f); rd = V3(0.30f, -0.45f, 0.85f);
            t.hipDrop = 0.08f + 0.10f * s; t.lean = 0.18f + 0.10f * s; t.headUp = 0.3f;
        } break;
        case CELEB_VAMOS: {  // 拳を下から突き上げ、膝を上げる「バモス！」
            float up = 0.5f + 0.5f * sinf(e * 2.0f * PI * 1.6f - PI * 0.5f);
            lh = V3(-0.24f, 1.06f + 0.60f * up, 0.22f + 0.10f * up);
            t.liftL = 0.32f * up; t.hipDrop = 0.08f; t.lean = 0.10f;
        } break;
        case CELEB_ROAR:     // 両腕を広げて天を仰ぎ、吠える
            rh = V3(0.62f, 1.55f, 0.05f); rd = V3(0.55f, 0.75f, -0.25f); lh = V3(-0.62f, 1.56f, 0.05f);
            t.lean = -0.10f; t.headUp = 1.0f;
            break;
        case CELEB_COMEON: { // 深くかがんで、両方の拳を体の前で握りしめて震わせ、ボックスを見上げて吠える
            float sh = 0.025f * sinf(e * 25.0f), p = Pulse(e, 0.22f, 0.14f);
            //   肘を張って拳を腰の前へ引きつける（力こぶ）。ラケットは下を向けて持つ
            rh = V3(0.44f, 1.02f + sh + 0.14f * p, 0.30f); rd = V3(0.35f, -0.55f, 0.75f);
            lh = V3(-0.44f, 1.00f + sh + 0.14f * p, 0.30f);
            t.hipDrop = 0.20f; t.lean = 0.30f; t.headUp = 0.8f;
        } break;
        case CELEB_RANT: {   // 両手を肩の高さに広げて（手のひらを上に）文句 → 最後に太ももを叩く
            float bob = 0.07f * sinf(e * 9.0f), b = Smooth01((e - 1.0f) / 0.15f);
            rh = Vector3Lerp(V3(0.60f, 1.38f + bob, 0.32f), V3(0.30f, 0.92f, 0.08f), b); rd = Vector3Lerp(V3(0.55f, 0.55f, 0.62f), rd0, b);
            lh = Vector3Lerp(V3(-0.60f, 1.38f + bob, 0.32f), V3(-0.30f, 0.92f, 0.08f), b);
            t.lean = 0.12f + 0.08f * b; t.headShake = 1.0f - b;
        } break;
        case CELEB_HANDS_HEAD:   // 頭を抱える（信じられない）
            rh = V3(0.13f, 1.76f, -0.04f); rd = V3(0.35f, 0.55f, -0.75f); lh = V3(-0.13f, 1.76f, -0.04f);
            t.lean = -0.06f; t.headUp = 0.6f;
            break;
        case CELEB_RACKET_UP:    // ラケットを真上に掲げる。小さく跳ぶ
            rh = V3(0.22f, 2.02f, 0.05f); rd = V3(0.05f, 0.98f, 0.12f); lh = V3(-0.24f, 1.92f, 0.10f);
            t.jump = 0.10f * Pulse(e, 0.25f, 0.20f); t.headUp = 1.0f;
            break;
        case CELEB_RACKET_POINT: // 観客（ボックス）にラケットを向けて掲げる
            rh = V3(0.38f, 1.80f, 0.50f); rd = V3(0.12f, 0.80f, 0.58f); lh = V3(-0.25f, 1.20f, 0.20f);
            t.lean = -0.04f; t.headUp = 0.5f;
            break;
        case CELEB_JUMP_FIST: {  // かがんでから跳び、空中で拳を突き上げる
            float j = Clamp01((e - 0.10f) / 0.50f), air = (j > 0.0f && j < 1.0f) ? sinf(PI * j) : 0.0f;
            t.jump = 0.38f * air; t.liftL = t.liftR = 0.22f * air;
            t.hipDrop = 0.14f * Pulse(e, 0.05f, 0.10f);
            lh = V3(-0.22f, 1.15f + 0.85f * Clamp01(air * 1.3f), 0.20f);
            rh = V3(0.40f, 1.05f, -0.05f); rd = V3(0.30f, -0.30f, -0.90f);
            t.headUp = 0.6f;
        } break;
        case CELEB_ARMS_UP:      // 両手を高く上げる（V 字）
            rh = V3(0.48f, 1.95f, 0.02f); rd = V3(0.35f, 0.88f, -0.15f); lh = V3(-0.48f, 1.95f, 0.02f);
            t.lean = -0.06f; t.headUp = 1.0f;
            break;
        case CELEB_KNEES: {      // 試合に勝って、膝から崩れ落ちて両手を上げる
            float kn = Smooth01((e - 0.15f) / 0.45f), au = Smooth01((e - 0.35f) / 0.40f);
            t.kneel = kn;
            rh = Vector3Lerp(V3(0.35f, 1.00f, 0.20f), V3(0.45f, 1.92f, 0.05f), au); rd = Vector3Lerp(rd0, V3(0.30f, 0.90f, -0.10f), au);
            lh = Vector3Lerp(V3(-0.35f, 1.00f, 0.20f), V3(-0.45f, 1.92f, 0.05f), au);
            t.headUp = au; t.lean = 0.05f - 0.12f * au;
        } break;
        case CELEB_HEART: {      // 胸の前で両手のハート → 観客へ投げる（ジョコビッチ）
            float th = Smooth01((e - 1.05f) / 0.35f);
            rh = Vector3Lerp(V3(0.06f, 1.36f, 0.30f), V3(0.40f, 1.60f, 0.65f), th); rd = Vector3Lerp(V3(0.10f, 0.95f, 0.20f), V3(0.30f, 0.70f, 0.65f), th);
            lh = Vector3Lerp(V3(-0.06f, 1.36f, 0.30f), V3(-0.40f, 1.60f, 0.65f), th);
            t.headUp = 0.3f; t.lean = 0.05f * th;
        } break;
        default: return false;
    }
    o->rh = Vector3Lerp(rh0, rh, k); o->rd = Vector3Normalize(Vector3Lerp(rd0, rd, k)); o->lh = Vector3Lerp(lh0, lh, k);
    o->hipDrop = t.hipDrop * k; o->lean = t.lean * k; o->jump = t.jump * k; o->liftL = t.liftL * k; o->liftR = t.liftR * k;
    o->kneel = t.kneel * k; o->headUp = t.headUp * k; o->headShake = t.headShake * k; o->k = k; o->e = e;
    return true;
}

// ラケットの姿勢。戻り値：面の中心。hand / dir は手の位置とラケットの向き（hasHand=false ならサーブ・スマッシュで、肩から伸ばして描く）
Vector3 RacketPose(const SwingAnim *a, int *strokeOut, Vector3 *normal, Vector3 *hand, Vector3 *dir, bool *hasHand)
{
    *hasHand = true;
    Vector3 h, d, f;
    if (a->smashT > 0.0f) {
        // ラケットを叩きつける：頭の上まで振りかぶって地面へ 3 回。最後は曲がったラケットを見下ろす
        const RKey UP   = { 0, { 0.20f, 1.95f, 0.05f }, { 0.05f, 0.55f, -0.83f }, { 1, 0, 0 } };
        const RKey DOWN = { 0, { 0.18f, 0.62f, 0.55f }, { 0.00f, -0.85f, 0.52f }, { 1, 0, 0 } };
        const RKey LOOK = { 0, { 0.25f, 0.95f, 0.32f }, { 0.10f, -0.60f, 0.80f }, { 1, 0, 0 } };
        const float T[] = { 0.0f, 0.38f, 0.52f, 0.78f, 0.90f, 1.14f, 1.26f, 1.70f };
        const RKey *K[] = { &READY, &UP, &DOWN, &UP, &DOWN, &UP, &DOWN, &LOOK };
        RKey k[8];
        for (int i = 0; i < 8; i++) { k[i] = *K[i]; k[i].t = T[i]; }
        SampleKeys(k, 8, SMASH_TIME - a->smashT, &h, &d, &f);
        *strokeOut = 0;
    } else if (a->celebT > 0.0f && a->celebKind != CELEB_NONE && a->smashT <= 0.0f) {
        // ポイント後のしぐさ：ラケットを持つ手（体が沈んだ分だけ下げる）
        CelebOut co;
        if (CelebPose(a, &co)) { h = co.rh; h.y -= co.hipDrop + 0.42f * co.kneel; d = co.rd; }
        else { h = READY.h; d = Vector3Normalize(READY.d); }
        f = V3(1, 0, 0);
        *strokeOut = 0;
    } else if (a->swingT >= 0.0f && a->swingT < SWING_TIME) {
        const Contact *c = &a->lastContact;
        Vector3 cl = a->contactLocal;
        float u = EaseOut(a->swingT / SWING_TIME);
        (void)u;
        if (c->overhead) {
            *strokeOut = 2;
            RKey k[8];
            int n = a->smash ? SmashSwingKeys(cl, k) : ServeSwingKeys(cl, k);
            SampleKeys(k, n, a->swingT, &h, &d, &f);
            goto finish;
        }
        float sgn = c->forehand ? 1.0f : -1.0f;
        *strokeOut = c->forehand ? 0 : 1;
        if (c->volley) {
            // ボレー：振らない。構えでラケットの頭を立てて肩の横に小さく引き（手首を後ろへ返す）→ 前へ短く押し出して当て →
            //   面を相手に向けたまま止める（パンチ）。低い球は面を開いて、ラケットの頭をなるべく落とさない
            RKey k[4];
            int n = VolleyKeys(cl, sgn, k);
            SampleKeys(k, n, a->swingT, &h, &d, &f);
            goto finish;
        }
        RKey k[8];
        int n = StrokeKeys(a, k, strokeOut);
        SampleKeys(k, n, a->swingT, &h, &d, &f);
    } else if (a->windup > 0.0f) {
        float w = EaseOut(a->windup * 1.6f);
        *strokeOut = a->windStroke;
        if (a->windStroke == 2) {
            if (a->smash) SampleKeys(SMASH_WIND, 3, Clamp01(a->windup), &h, &d, &f);
            else          SampleKeys(SERVE_WIND, 5, Clamp01(a->windup), &h, &d, &f);
            goto finish;
        }
        float sgn = a->windStroke == 0 ? 1.0f : -1.0f;
        if (a->atNet) {
            // ボレーの構え：構えから、ラケットの頭を立てたまま肩の横へ小さく引くだけ（大きく引かない）
            RKey k[4];
            VolleyKeys(V3(sgn * 0.65f, 1.15f, 0.50f), sgn, k);
            h = Vector3Lerp(READY.h, k[0].h, w);
            d = Vector3Normalize(Vector3Lerp(READY.d, k[0].d, w));
            f = Vector3Lerp(READY.f, k[0].f, w);
        } else {
            RKey e = WindupKey(a->windStroke, a->slice, a->oneHanded, a->fhStyle);
            h = Vector3Lerp(READY.h, e.h, w);
            d = Vector3Normalize(Vector3Lerp(READY.d, e.d, w));
            f = Vector3Lerp(READY.f, e.f, w);
        }
    } else if (a->serveStance) {
        *strokeOut = 2;
        h = SERVE_STANCE.h; d = Vector3Normalize(SERVE_STANCE.d); f = SERVE_STANCE.f;
    } else {
        *strokeOut = 0;
        h = READY.h; d = Vector3Normalize(READY.d); f = READY.f;
    }
finish:;
    // 面の向き：ラケットの向きに直交させる
    Vector3 nf = Vector3Subtract(f, Vector3Scale(d, Vector3DotProduct(f, d)));
    if (Vector3Length(nf) < 1e-3f) nf = Vector3CrossProduct(d, V3(0, 1, 0));
    if (Vector3Length(nf) < 1e-3f) nf = V3(1, 0, 0);
    *normal = Vector3Normalize(nf);
    *hand = h; *dir = d;
    return Vector3Add(h, Vector3Scale(d, RACKET_REACH));
}

void DrawRacket(Vector3 hand, Vector3 dir, Vector3 normal, Color frame) { DrawRacketBent(hand, dir, normal, frame, 0.0f); }

Vector3 RacketLocal(const SwingAnim *a, int *strokeOut, Vector3 *tangent)
{
    Vector3 h, d; bool hh;
    return RacketPose(a, strokeOut, tangent, &h, &d, &hh);
}

// ラケット：グリップ、のど（V 字）、楕円の枠、ガット。hand から dir の向き、normal が面の向き
void DrawRacketBent(Vector3 hand, Vector3 dir, Vector3 normal, Color frame, float bend)
{
    dir = Vector3Normalize(dir);
    Vector3 side = Vector3Normalize(Vector3CrossProduct(normal, dir));
    Vector3 butt = Vector3Subtract(hand, Vector3Scale(dir, 0.07f));
    Vector3 throat = Vector3Add(hand, Vector3Scale(dir, 0.20f));
    // 曲がったラケット：のどから先が面の裏側へ折れ、枠がつぶれる
    if (bend > 0.0f) {
        Vector3 nd = Vector3Normalize(Vector3CrossProduct(dir, side));
        dir = Vector3Normalize(Vector3RotateByAxisAngle(dir, side, 0.75f * bend));
        normal = Vector3Normalize(Vector3RotateByAxisAngle(nd, side, 0.75f * bend));
        side = Vector3Normalize(Vector3CrossProduct(normal, dir));
    }
    Vector3 center = Vector3Add(throat, Vector3Scale(dir, RACKET_REACH - 0.20f));
    const float A = 0.165f * (1.0f - 0.15f * bend), B = 0.125f * (1.0f + 0.10f * bend);   // 楕円の半径（長さ方向、幅方向）
    DrawCylinderEx(butt, hand, 0.017f, 0.016f, 6, frame);                   // グリップ
    DrawCylinderEx(hand, throat, 0.014f, 0.011f, 6, frame);                 // シャフト
    Vector3 bottom = Vector3Subtract(center, Vector3Scale(dir, A));
    for (int s = -1; s <= 1; s += 2)                                       // のど（V 字）
        DrawCylinderEx(throat, Vector3Add(Vector3Add(bottom, Vector3Scale(side, s * 0.045f)), Vector3Scale(dir, 0.02f)), 0.008f, 0.008f, 4, frame);
    Vector3 prev = { 0 };
    Vector3 pts[24];
    for (int i = 0; i <= 24; i++) {
        float t = (float)i / 24.0f * 2.0f * PI;
        float dent = 1.0f - bend * 0.30f * fmaxf(cosf(t), 0.0f) * (0.6f + 0.4f * sinf(5.0f * t));   // 先がへこむ
        Vector3 p = Vector3Add(center, Vector3Add(Vector3Scale(dir, A * cosf(t) * dent), Vector3Scale(side, B * sinf(t))));
        if (i < 24) pts[i] = p;
        if (i > 0) DrawCylinderEx(prev, p, 0.009f, 0.009f, 4, frame);
        prev = p;
    }
    Color str = Fade(RAYWHITE, 0.30f * (1.0f - 0.7f * bend));   // 折れたらガットも緩む
    for (int i = 0; i < 24; i++) {
        Vector3 p0 = pts[i], p1 = pts[(i + 1) % 24];
        DrawTriangle3D(center, p0, p1, str);
        DrawTriangle3D(center, p1, p0, str);
    }
}

// ラケットの面（円）を描く。normal: 面の向き
void DrawRacketHead(Vector3 center, Vector3 normal, Color frame)
{
    normal = Vector3Normalize(normal);
    Vector3 z = { 0, 0, 1 };
    Vector3 axis = Vector3CrossProduct(z, normal);
    float ang = acosf(Clamp(Vector3DotProduct(z, normal), -1.0f, 1.0f)) * RAD2DEG;
    if (Vector3Length(axis) < 1e-4f) axis = (Vector3){ 1, 0, 0 };
    DrawCircle3D(center, 0.14f, Vector3Normalize(axis), ang, frame);
    DrawCircle3D(center, 0.135f, Vector3Normalize(axis), ang, frame);
    DrawCylinderEx(Vector3Subtract(center, Vector3Scale(normal, 0.004f)), Vector3Add(center, Vector3Scale(normal, 0.004f)),
                   0.13f, 0.13f, 14, Fade(RAYWHITE, 0.35f));
}

// 2 本の骨（太もも・すね、上腕・前腕）の関節の位置。届かないときは骨を伸ばして届かせる
//   hint：関節が曲がる向き
Vector3 TwoBone(Vector3 root, Vector3 end, float l1, float l2, Vector3 hint)
{
    Vector3 d = Vector3Subtract(end, root);
    float L = Vector3Length(d);
    if (L < 1e-4f) return root;
    if (L >= l1 + l2) { float k = L / (l1 + l2) * 1.0001f; l1 *= k; l2 *= k; }
    Vector3 u = Vector3Scale(d, 1.0f / L);
    float a = (l1 * l1 - l2 * l2 + L * L) / (2.0f * L);
    float h = sqrtf(fmaxf(l1 * l1 - a * a, 0.0f));
    Vector3 perp = Vector3Subtract(hint, Vector3Scale(u, Vector3DotProduct(hint, u)));
    float pl = Vector3Length(perp);
    perp = pl > 1e-4f ? Vector3Scale(perp, 1.0f / pl) : (Vector3){ 0, 0, 1 };
    return Vector3Add(Vector3Add(root, Vector3Scale(u, a)), Vector3Scale(perp, h));
}

// 太さの変わる棒（体の部品）
static void Limb(Vector3 a, Vector3 b, float ra, float rb, Color c)
{
    DrawCylinderEx(a, b, ra, rb, 8, c);
    DrawSphere(a, ra, c);
    DrawSphere(b, rb, c);
}

void DrawFigure(Vector3 pos, bool facingNegZ, Color shirt, const SwingAnim *a, float speed)
{
    Color skin = { 236, 196, 158, 255 }, frame = { 35, 35, 40, 255 };
    Color shorts = a->shorts.a ? a->shorts : (Color){ 245, 245, 248, 255 };
    Color shoe = { 250, 250, 252, 255 }, sole = { 120, 120, 130, 255 }, sock = { 250, 250, 250, 255 };
    Color hair = a->hair.a ? a->hair : (Color){ 70, 50, 35, 255 };
    // 足元の影（光と反対の向きへ少しずらす）。地面に立っている感じを出す
    {
        rlDisableDepthMask();
        for (int k = 0; k < 3; k++)
            DrawCylinder((Vector3){ pos.x - 0.10f - 0.06f * k, 0.006f, pos.z - 0.12f - 0.07f * k }, 0.34f - 0.06f * k, 0.34f - 0.06f * k, 0.001f, 18, Fade(BLACK, 0.12f));
        rlEnableDepthMask();
    }
    // 左利き：右利きの動きで計算して、最後に左右を反転して描く（体の左右の値も反転しておく）
    SwingAnim mir;
    if (a->lefty) {
        mir = *a;
        mir.contactLocal.x = -a->contactLocal.x;
        mir.slideSide = -a->slideSide; mir.lungeSide = -a->lungeSide; mir.velX = -a->velX;
        a = &mir;
    }
    float MX = a->lefty ? -1.0f : 1.0f;
    #define W(x, y, z) LocalToWorld(pos, facingNegZ, (Vector3){ MX * (x), (y), (z) })
    #define WV(v) W((v).x, (v).y, (v).z)

    if (a->downT > 0.0f) {
        // 飛びついて倒れている
        float s = a->lastContact.forehand ? 1.0f : -1.0f;
        Limb(W(s * 0.15f, 0.18f, 0.0f), W(s * 0.80f, 0.22f, 0.15f), 0.15f, 0.16f, shirt);
        DrawSphere(W(s * 1.12f, 0.22f, 0.2f), 0.11f, skin);
        Limb(W(s * 0.1f, 0.14f, 0.0f), W(-s * 0.25f, 0.12f, -0.08f), 0.10f, 0.09f, shorts);
        Limb(W(-s * 0.25f, 0.12f, -0.08f), W(-s * 0.7f, 0.08f, -0.15f), 0.06f, 0.05f, skin);
        Limb(W(-s * 0.25f, 0.12f, 0.12f), W(-s * 0.65f, 0.08f, 0.2f), 0.06f, 0.05f, skin);
        Limb(W(s * 0.9f, 0.25f, 0.2f), W(s * 1.40f, 0.08f, 0.35f), 0.045f, 0.04f, skin);
        DrawRacket(W(s * 1.45f, 0.06f, 0.36f), Vector3Subtract(W(s * 1.0f, 0.0f, 0.3f), W(0, 0, 0)), (Vector3){ 0, 1, 0 }, frame);
        return;
    }

    (void)speed;
    // ---- 動きの量：走る向き（体から見て）と速さ ----
    float vx = a->velX, vz = a->velZ;
    float spd = sqrtf(vx * vx + vz * vz);
    float move = Clamp(spd / 6.0f, 0.0f, 1.0f);
    float mdx = spd > 0.05f ? vx / spd : 0.0f, mdz = spd > 0.05f ? vz / spd : 0.0f;
    float ph = a->runPhase;

    // ---- 体のひねり：構えで横を向き、振りながら前へ戻る ----
    int stroke; Vector3 nrm, hand, rdir; bool hasHand;
    Vector3 rl = RacketPose(a, &stroke, &nrm, &hand, &rdir, &hasHand);
    float sgnK = (stroke == 1) ? -1.0f : 1.0f;
    float yaw = 0.0f;
    bool swinging = a->swingT >= 0.0f && a->swingT < SWING_TIME;
    // 構えでは横を向く（ストロークは 75°、片手バックは 95°、サーブ・スマッシュはほぼ真横の 90°）。腰は肩の 6 割回る
    bool volleyPose = swinging ? (a->lastContact.volley && !a->lastContact.overhead) : a->atNet;
    bool oneHandBack = stroke == 1 && (a->oneHanded || a->slice);
    float turn = (stroke == 2) ? 90.0f : (volleyPose ? (stroke == 1 ? 45.0f : 30.0f) : (oneHandBack ? 95.0f : 75.0f));   // ボレーは肩をあまり回さない
    float endTurn = volleyPose ? 10.0f : (oneHandBack ? 20.0f : -35.0f);   // 片手バックは肩を開ききらない（横向きのまま振り抜く）
    if (swinging)            yaw = sgnK * Lerp(turn, endTurn, EaseOut(a->swingT / SWING_TIME));
    else if (a->windup > 0)  yaw = sgnK * turn * Clamp01(a->windup * 1.4f);
    else if (a->serveStance) yaw = 80.0f;                     // サーブの構え：横を向く
    yaw *= DEG2RAD;
    float hipYaw = yaw * 0.6f;
    // 走り方：横へゆっくりなら、足を交差させずに横へ跳ねる（サイドステップ。体はネットを向いたまま）。
    //   横へ速く長く走るときは、体を走る向きへ回して走る。打つ準備（溜め）に入ったら正面へ戻す
    bool freeBody = !swinging && a->smashT <= 0.0f && a->celebT <= 0.0f && !a->serveStance;
    float lat = fabsf(mdx);
    float shuffle = freeBody ? Smooth01((lat - 0.70f) / 0.15f) * (1.0f - Smooth01((spd - 3.6f) / 0.8f)) * Smooth01(spd / 0.8f) : 0.0f;
    float runTurn = freeBody ? Smooth01((spd - 3.5f) / 1.5f) * (1.0f - Clamp01(a->windup * 2.0f)) : 0.0f;
    float runYaw = (spd > 0.05f && mdz > -0.3f) ? Clamp(atan2f(mdx, fmaxf(mdz, 0.0f)), -1.2f, 1.2f) * runTurn : 0.0f;
    yaw += runYaw; hipYaw += runYaw;
    float stanceK = swinging ? 1.0f - Clamp01(a->swingT / SWING_TIME) : Clamp01(a->windup * 1.4f);   // 足を前後に開く量
    // ボレー：当てるのに合わせて、球と反対の足（フォアなら左）を球の方へ踏み込む。低い球は膝を曲げて沈む
    bool volleySwing = swinging && a->lastContact.volley && !a->lastContact.overhead;
    float volStep = volleySwing ? Smooth01(a->swingT / SWING_LEAD) : 0.0f;
    float volLow = volleySwing ? Clamp((0.95f - a->contactLocal.y) * 0.6f, 0.0f, 0.32f) * Smooth01(a->swingT / 0.06f) : 0.0f;

    // ---- 腰の高さ：構えでは膝を曲げて低く、走ると上下に弾む ----
    // 膝はいつも曲げておく（構え・走り）。引くときにさらに沈み（ため）、振りながら脚で押し上げる
    float load = 0.0f;
    if (swinging && !a->lastContact.volley && !a->lastContact.overhead)
        load = KNEE_LOAD * (1.0f - Smooth01((a->swingT - 0.03f) / 0.14f));
    else if (!swinging) load = KNEE_LOAD * Clamp01(a->windup);
    float hipY = 0.88f - KNEE_BEND * (1.0f - 0.3f * move) - load - 0.03f * move * fabsf(sinf(ph));
    Vector3 leanV = { mdx * 0.14f * move, 0.0f, 0.05f + mdz * 0.14f * move };   // 前傾（走る向きへ）
    hipY -= 0.30f * a->crouch;                 // しゃがむ（Q）：腰を落として前に屈む
    hipY -= 0.05f * shuffle;                   // サイドステップは低い姿勢で
    hipY -= volLow; leanV.z += 0.5f * volLow + 0.06f * volStep;
    leanV.z += 0.10f * a->crouch;
    // ナダルのフォア：オープンスタンスで沈み込み、振り上げと一緒に体ごと跳び上がる
    bool lasso = a->fhStyle == 1 && stroke == 0 && !volleyPose && !a->slice && (swinging || a->windup > 0.0f);
    if (lasso) {
        if (swinging) {
            float u2 = Clamp01((a->swingT - 0.03f) / 0.22f);
            pos.y += 0.12f * sinf(PI * u2);
            hipY += 0.04f * sinf(PI * u2) - 0.06f * (1.0f - Clamp01(a->swingT / 0.06f));
        } else hipY -= 0.06f * Clamp01(a->windup);
        stanceK *= 0.25f;   // 足は前後に開かない（オープンスタンス）
    }
    // スプリットステップ：相手が打つのに合わせて小さく跳び、足を広げて膝を曲げて着地する
    float splitWide = 0.0f;
    if (a->splitT > 0.0f && !swinging && stroke != 2 && !a->serveStance && a->downT <= 0.0f) {
        float u = 1.0f - a->splitT / SPLIT_TIME, kk = 1.0f - 0.6f * move;
        if (u < 0.5f) pos.y += 0.09f * sinf(PI * u / 0.5f) * kk;
        else          hipY -= 0.08f * sinf(PI * (u - 0.5f) / 0.5f) * kk;
        splitWide = Clamp01(u / 0.4f) * Clamp01((1.0f - u) / 0.25f) * kk;
    }
    // ポイント後のしぐさ
    CelebOut co = { 0 };
    bool celeb = CelebPose(a, &co);
    if (celeb) { hipY += KNEE_BEND * co.k; hipY -= co.hipDrop + 0.42f * co.kneel; leanV.z += co.lean; pos.y += co.jump; }   // しぐさの間は膝を伸ばす
    leanV.z += 0.12f * a->slouch;              // 背中を丸める
    // よろめき：打った方へ体が流れて傾き、腰が落ちる（立て直すにつれて戻る）
    float stg = (!swinging || a->swingT > 0.12f) ? a->staggerK : 0.0f;
    if (stg > 0.0f) { leanV.x += a->staggerSide * 0.18f * stg; leanV.z -= 0.05f * stg; hipY -= 0.05f * stg; }
    if (a->smashT > 0.0f) {                    // 叩きつけるたびに前へかがむ
        float down = Clamp01((1.9f - hand.y) / 1.3f);
        leanV.z += 0.28f * down; hipY -= 0.10f * down;
    }

    // ---- サーブ・スマッシュ：トスで膝を深く曲げて反り、振るときに伸び上がって跳ぶ ----
    bool overheadPose = (stroke == 2) && (swinging || a->windup > 0.0f || a->serveStance) && move < 0.5f;
    float jump = 0.0f, ohU = 0.0f;
    float ohAir = 0.0f;
    if (overheadPose && a->smash) {
        // スマッシュ：後ろ足で踏み切って跳び、空中で脚を入れ替える（シザースキック）。高い球ほど高く跳ぶ
        if (swinging) {
            ohU = Clamp01(a->swingT / SWING_TIME);
            ohAir = sinf(PI * Clamp01(ohU * 1.6f));
            jump = (a->contactLocal.y > 2.55f ? 0.30f : 0.14f) * ohAir;
            hipY = 0.90f;
            leanV = (Vector3){ 0.0f, 0.0f, -0.08f + 0.25f * ohU };
        } else {
            float w = Clamp01(a->windup);
            hipY = 0.88f - 0.07f * w;                                    // 膝は軽く曲げるだけ
            leanV = (Vector3){ 0.03f * w, 0.0f, -0.06f * w };            // 後ろ足に体重を乗せる
        }
        pos.y += jump;
    } else if (overheadPose) {
        if (swinging) {
            ohU = Clamp01(a->swingT / SWING_TIME);
            jump = 0.20f * sinf(PI * Clamp01(ohU * 1.5f));
            hipY = 0.93f;
            leanV = (Vector3){ 0.0f, 0.0f, -0.04f + 0.22f * ohU };     // 打ちながら前へ倒れ込む
        } else {
            float w = Clamp01(a->windup);
            hipY = 0.88f - 0.17f * w;                                    // トロフィー：膝を深く曲げる
            leanV = (Vector3){ 0.06f * w, 0.0f, -0.10f * w };            // 後ろに反る
        }
        pos.y += jump;
    }

    // ---- 跳んで伸びて取る（リターンなどで遠い球）：球の方へ跳び、体を倒して足を大きく開く ----
    bool lunging = a->lungeT > 0.0f && !overheadPose;
    if (lunging) {
        float pr = 1.0f - a->lungeT / LUNGE_TIME;
        pos.y += 0.22f * sinf(PI * Clamp01(pr * 1.3f));
        leanV = (Vector3){ a->lungeSide * 0.28f, 0.0f, 0.10f };
        hipY = 0.76f;
        hipYaw = 0.0f;
    }

    // ---- 脚：ショーツ（太ももの半分まで）、脚、白いソックス、シューズ ----
    Vector3 hips[2], feet[2], knees[2]; float toes[2];
    if (a->sliding || lunging) {
        // スライド・飛びつき：腰を落として足を大きく開く。スライドは足元に滑った跡
        float sd = lunging ? a->lungeSide : (a->slideSide >= 0.0f ? 1.0f : -1.0f);
        if (!lunging) hipY = 0.70f;
        float lift = lunging ? 0.10f : 0.0f;
        hips[0] = (Vector3){ sd * 0.10f + leanV.x * 0.3f, hipY, 0.0f };  feet[0] = (Vector3){ sd * 0.85f, 0.07f, 0.10f };
        hips[1] = (Vector3){ -sd * 0.10f + leanV.x * 0.3f, hipY, 0.0f }; feet[1] = (Vector3){ -sd * 0.40f, 0.07f + lift, -0.20f };
        knees[0] = TwoBone(hips[0], feet[0], 0.45f, 0.45f, (Vector3){ sd * 0.3f, 0.6f, 0.4f });
        knees[1] = TwoBone(hips[1], feet[1], 0.45f, 0.45f, (Vector3){ 0.0f, 0.2f, 1.0f });
        toes[0] = sd * 1.0f; toes[1] = sd * 0.6f;
        if (!lunging) DrawCylinderEx(W(sd * 0.80f, 0.01f, 0.05f), W(sd * 1.40f, 0.01f, 0.05f), 0.05f, 0.02f, 6, Fade(RAYWHITE, 0.5f));
    } else {
        for (int i = 0; i < 2; i++) {
            float sd = i == 0 ? 1.0f : -1.0f;
            float p = ph + (i == 0 ? 0.0f : PI);
            float width = 0.14f + 0.10f * (1.0f - move) + 0.10f * splitWide;
            float stride = 0.52f * move * (1.0f - shuffle);
            // 構え・振り：前足（フォアなら左）を踏み出し、後ろ足を引く。足と腰は横を向く
            float hc = cosf(hipYaw), hs = sinf(hipYaw);
            float bx = sd * width, bz = (sd == -sgnK ? 0.20f : -0.10f) * stanceK;
            if (volleySwing) {   // 踏み込む足は前へ大きく、少し球の側へ
                bz = (sd == -sgnK ? 0.36f : -0.06f) * volStep;
                if (sd == -sgnK) bx += sgnK * 0.10f * volStep;
            }
            Vector3 hip  = { sd * 0.10f * hc + leanV.x * 0.3f, hipY, -sd * 0.10f * hs + leanV.z * 0.3f };
            // 走り：振り出す足（cos>0）は膝を上げ、後ろへ蹴った足はかかとを上げる
            float sw = fmaxf(cosf(p), 0.0f);
            float lift = (0.20f * powf(sw, 0.7f) + 0.10f * fmaxf(-sinf(p), 0.0f) * sw) * move * (1.0f - shuffle);
            Vector3 foot = { bx * hc + bz * hs + mdx * sinf(p) * stride, 0.07f + lift,
                             -bx * hs + bz * hc + mdz * sinf(p) * stride };
            if (shuffle > 0.0f) {
                // サイドステップ：両足の間が開いて閉じるのを繰り返す（足は交差しない）。小さく跳ねる
                float open = 0.5f + 0.5f * sinf(ph);
                foot.x += sd * (0.08f + 0.14f * open) * shuffle;
                foot.y += 0.05f * fabsf(cosf(ph)) * shuffle;
            }
            float toe = hipYaw + sd * 0.30f;                             // つま先の向き（少し外向き）
            if (overheadPose) {
                // 横向きに構える：左足を前（つま先は右前のポストの方）、右足は後ろでベースラインと平行。
                //   跳んだら右足を後ろに蹴り上げる
                if (a->smash && swinging) {
                    // シザースキック：後ろ（右）足が前へ、前（左）足が後ろへ蹴り上がって入れ替わる
                    float sc = Smooth01(ohU * 2.2f), base = 0.07f - jump + jump * 0.85f;
                    foot = (sd < 0) ? Vector3Lerp((Vector3){ -0.10f, base, 0.26f }, (Vector3){ -0.18f, base + 0.25f * ohAir, -0.35f }, sc)
                                    : Vector3Lerp((Vector3){ 0.26f, base, -0.24f }, (Vector3){ 0.12f, base + 0.08f * ohAir, 0.30f }, sc);
                } else {
                    foot = (sd < 0) ? (Vector3){ -0.10f, 0.07f - jump, 0.26f }
                                    : (Vector3){ 0.26f, 0.07f - jump + 0.25f * ohU, -0.24f - 0.20f * ohU };
                    if (jump > 0.0f) foot.y += jump * 0.6f;   // 跳んでいる間は足も浮く
                }
                toe = (sd < 0) ? 0.75f : 1.50f;
                hip.x = sd * 0.10f * cosf(0.9f); hip.z = -sd * 0.10f * sinf(0.9f) + leanV.z * 0.3f;
            }
            if (celeb) {   // 膝を上げる（バモス・ジャンプ）、膝をつく（足を後ろへ）
                float lift = sd < 0 ? co.liftL : co.liftR;
                foot.y += lift; foot.z += 0.55f * lift;
                foot.z -= 0.50f * co.kneel; foot.y = fmaxf(foot.y, 0.07f);
            }
            Vector3 hint = { sd * 0.25f * cosf(hipYaw) + sinf(hipYaw), 0.0f, -sd * 0.25f * sinf(hipYaw) + cosf(hipYaw) };
            hips[i] = hip; feet[i] = foot; toes[i] = toe;
            knees[i] = TwoBone(hip, foot, 0.44f, 0.44f, hint);
        }
    }
    for (int i = 0; i < 2; i++) {
        Vector3 hp = hips[i], kn = knees[i], ft = feet[i];
        Vector3 mid = Vector3Lerp(hp, kn, 0.55f);                                // ショーツの裾
        Vector3 sockTop = Vector3Lerp(kn, ft, 0.78f);
        Limb(WV(kn), WV(ft), 0.050f, 0.040f, skin);                                // すね
        Limb(WV(hp), WV(kn), 0.068f, 0.052f, skin);                                // 太もも
        if (!a->female) DrawCylinderEx(WV(hp), WV(mid), 0.092f, 0.088f, 10, shorts);   // ショーツ（脚の部分は少しゆったり）
        DrawCylinderEx(WV(sockTop), WV(ft), 0.046f, 0.044f, 8, sock);              // ソックス
        float tx = sinf(toes[i]), tz = cosf(toes[i]);
        Vector3 heel = { ft.x - tx * 0.06f, ft.y - 0.035f, ft.z - tz * 0.06f };
        Vector3 toeP = { ft.x + tx * 0.16f, ft.y - 0.045f, ft.z + tz * 0.16f };
        Limb(WV(heel), WV(toeP), 0.047f, 0.040f, shoe);                            // シューズ
        DrawCylinderEx(W(heel.x, heel.y - 0.035f, heel.z), W(toeP.x, toeP.y - 0.03f, toeP.z), 0.035f, 0.030f, 6, sole);
    }

    // ---- 胴：腰（ショーツ）→ ウエスト → 胸。胸は横に広く、肩の線はひねりに合わせて回る ----
    float cy = cosf(yaw), sy = sinf(yaw), hcy = cosf(hipYaw), hsy = sinf(hipYaw);
    Vector3 pelvis = { leanV.x * 0.3f, hipY + 0.02f, leanV.z * 0.3f };
    Vector3 waist  = { leanV.x * 0.55f, hipY + 0.20f, leanV.z * 0.55f };
    Vector3 chest  = { leanV.x, hipY + 0.42f, leanV.z };
    float shw = a->female ? 0.18f : 0.20f;   // 肩幅
    Vector3 rShoulder = { chest.x + shw * cy, hipY + 0.55f, chest.z - shw * sy };
    Vector3 lShoulder = { chest.x - shw * cy, hipY + 0.55f, chest.z + shw * sy };
    // ショーツの腰：左右の股関節を包む（丸くしない）
    {
        Vector3 hl = { pelvis.x - 0.085f * hcy, pelvis.y, pelvis.z + 0.085f * hsy };
        Vector3 hr = { pelvis.x + 0.085f * hcy, pelvis.y, pelvis.z - 0.085f * hsy };
        if (a->female) {
            // スカート：腰から広がる（走ると裾が揺れる）
            float fl = 0.27f + 0.03f * move * fabsf(sinf(ph * 2.0f));
            DrawCylinderEx(W(pelvis.x, pelvis.y + 0.13f, pelvis.z), W(pelvis.x * 1.2f, pelvis.y - 0.20f, pelvis.z * 1.2f), 0.15f, fl, 14, shorts);
        } else {
            DrawCylinderEx(W(hl.x, hl.y - 0.05f, hl.z), W(hl.x, hl.y + 0.13f, hl.z), 0.10f, 0.105f, 10, shorts);
            DrawCylinderEx(W(hr.x, hr.y - 0.05f, hr.z), W(hr.x, hr.y + 0.13f, hr.z), 0.10f, 0.105f, 10, shorts);
        }
    }
    // シャツ：ウエストから胸へ広がる。胸の左右に 2 本重ねて、横に広く前後に薄くする
    for (int sd = -1; sd <= 1; sd += 2) {
        Vector3 w0 = { waist.x + sd * 0.06f * hcy, waist.y - 0.10f, waist.z - sd * 0.06f * hsy };
        Vector3 c1 = { chest.x + sd * 0.09f * cy, chest.y + 0.08f, chest.z - sd * 0.09f * sy };
        DrawCylinderEx(WV(w0), WV(c1), 0.105f, 0.125f, 10, shirt);
        DrawSphere(WV(c1), a->female ? 0.112f : 0.125f, shirt);
    }
    Limb(WV(lShoulder), WV(rShoulder), 0.070f, 0.070f, shirt);
    // 首と頭。髪（後ろと上を覆う）、選手によってはヘッドバンド
    Vector3 neckB = { chest.x * 1.05f, hipY + 0.58f, chest.z * 1.05f };
    Vector3 head  = { chest.x * 1.12f, hipY + 0.79f, chest.z * 1.12f + 0.01f };
    head.y -= 0.04f * a->slouch; head.z += 0.05f * a->slouch;                   // うつむく
    if (celeb) {
        head.x += 0.03f * sinf(co.e * 12.0f) * co.headShake;                   // 首を振る
        head.z -= 0.03f * co.headUp; head.y += 0.02f * co.headUp;              // 天を仰ぐ
    }
    DrawCylinderEx(WV(neckB), WV(head), 0.050f, 0.045f, 8, skin);
    DrawSphere(WV(head), 0.105f, skin);
    DrawSphere(W(head.x, head.y + 0.025f, head.z - 0.022f), 0.103f, hair);
    if (a->headband.a) DrawCylinderEx(W(head.x, head.y + 0.025f, head.z), W(head.x, head.y + 0.060f, head.z), 0.110f, 0.108f, 14, a->headband);
    if (a->female) {
        // ポニーテール：頭の後ろから下へ。走ると左右に揺れ、振るとなびく
        float sway = 0.07f * sinf(ph) * move + (swinging ? 0.06f * sinf(a->swingT * 20.0f) : 0.0f);
        Vector3 pb = { head.x, head.y + 0.05f, head.z - 0.10f };
        Vector3 pt = { head.x + sway, head.y - 0.20f, head.z - 0.17f - 0.05f * move };
        DrawSphere(WV(pb), 0.04f, hair);
        DrawCylinderEx(WV(pb), WV(pt), 0.04f, 0.015f, 8, hair);
    }

    // ---- ラケットと腕 ----
    // 右利き：ラケットはいつも右手。両手バックは左手を添える。片手バック・スライスは左手を離す
    bool twoHands = (stroke == 1) && !a->slice && !a->oneHanded;
    if (!hasHand) {
        // サーブ・スマッシュ：肩から伸ばした腕の先にラケット
        Vector3 dd = Vector3Normalize(Vector3Subtract(rl, rShoulder));
        hand = Vector3Subtract(rl, Vector3Scale(dd, 0.55f));
        if (swinging) {
            SwingAnim pa = *a; pa.swingT = fmaxf(a->swingT - 0.01f, 0.0f);
            int sk2; Vector3 tg2;
            Vector3 rp = RacketLocal(&pa, &sk2, &tg2);
            Vector3 v = Vector3Subtract(rl, rp);
            if (Vector3Length(v) > 1e-4f) {
                float lag = a->swingT < SWING_LEAD ? 0.18f * (1.0f - a->swingT / SWING_LEAD) : -0.08f * Clamp01((a->swingT - SWING_LEAD) / 0.08f);
                hand = Vector3Add(hand, Vector3Scale(Vector3Normalize(v), lag));
            }
        }
        rdir = Vector3Normalize(Vector3Subtract(rl, hand));
    }
    // 走るとき：ラケットを立てて持ち、腕を振る（右腕は左足と一緒に前へ）。サイドステップでは体の前で構えたまま
    if (freeBody && a->windup <= 0.0f && hasHand) {
        float runA = Smooth01((move - 0.25f) / 0.30f) * (1.0f - shuffle);
        if (runA > 0.0f) {
            Vector3 hr = { 0.26f, hipY + 0.38f + 0.05f * cosf(ph), 0.10f - 0.24f * sinf(ph) * move };
            hand = Vector3Lerp(hand, hr, runA);
            rdir = Vector3Normalize(Vector3Lerp(rdir, (Vector3){ 0.15f, 0.75f, 0.60f }, runA));
            Vector3 nf = Vector3CrossProduct(rdir, (Vector3){ 0, 1, 0 });
            if (Vector3Length(nf) > 1e-3f) nrm = Vector3Normalize(nf);
        }
    }
    Vector3 neck = Vector3Add(hand, Vector3Scale(rdir, 0.28f));       // ラケットの首（のど）
    Vector3 shoulder = rShoulder, other = lShoulder;
    Vector3 otherHand;
    bool idle = !swinging && a->windup <= 0.0f;
    float su = swinging ? a->swingT : 0.0f;
    if (a->smashT > 0.0f)            otherHand = (Vector3){ -0.32f, hipY + 0.32f, 0.12f };            // 叩きつけ：片手で
    else if (stg > 0.25f && !swinging && a->windup <= 0.0f) otherHand = Vector3Lerp(neck, (Vector3){ -0.58f, hipY + 0.42f, -0.05f }, Clamp01(stg * 1.5f));   // よろめき：左手を広げてバランスを取る
    else if (celeb) { otherHand = co.lh; otherHand.y -= co.hipDrop + 0.42f * co.kneel; }
    else if (swinging && volleyPose) {
        // ボレー：構えでは左手でラケットを支え、当てるときに離してバランスを取る（フォアは前へ、バックは後ろへ）
        float r = Smooth01((a->swingT - 0.02f) / 0.06f);
        Vector3 bal = stroke == 1 ? (Vector3){ -0.45f, hipY + 0.42f, -0.22f } : (Vector3){ -0.38f, hipY + 0.46f, 0.32f };
        otherHand = Vector3Lerp(neck, bal, r);
    }
    else if (!swinging && a->windup > 0.0f && a->atNet) otherHand = neck;                           // ボレーの構え：両手で
    else if (twoHands)               otherHand = Vector3Add(hand, Vector3Scale(rdir, 0.10f));          // グリップの少し上
    else if (stroke == 1 && !swinging && a->windup > 0.0f && !a->atNet) otherHand = neck;          // 片手バックの構え：左手で首を支える
    else if (stroke == 1 && swinging && !volleyPose) {
        // 片手バック・スライス：振り出しで左手を離し、後ろへ伸ばしてバランスを取る（両腕を広げる）
        float r = Clamp01((su - 0.03f) / 0.12f);
        Vector3 back = { -0.55f, hipY + 0.38f + 0.06f * a->grace, -0.42f };
        otherHand = Vector3Lerp(su < 0.03f ? neck : Vector3Add(hand, Vector3Scale(rdir, 0.28f)), back, EaseOut(r));
    }
    else if (idle && (move < 0.3f || shuffle > 0.5f)) otherHand = neck;                          // 構え・サイドステップ：左手をラケットの首に                                                  // 構え：左手をラケットの首に
    else if (stroke == 2 && !swinging) {
        // サーブ：ボールを持った左手をラケットののどに添え → トスで腕を伸ばしたまま真上へ上げ、ボールを指したまま待つ
        Vector3 hold = a->serveStance && a->windup <= 0.0f ? neck : (Vector3){ -0.02f, hipY + 0.20f, 0.45f };
        float up = Smooth01((a->windup - 0.30f) / 0.40f);
        otherHand = Vector3Lerp(hold, (Vector3){ -0.10f, hipY + 1.17f, 0.28f }, up);
        if (a->smash) otherHand = Vector3Lerp(neck, (Vector3){ -0.12f, hipY + 1.12f, 0.42f }, Smooth01(a->windup / 0.4f));   // スマッシュ：左手で球を指す
    }
    else if (stroke == 2 && swinging) {
        // 打つときは左手を胸に引きつける
        float r = Smooth01(a->swingT / 0.10f);
        otherHand = Vector3Lerp((Vector3){ -0.10f, hipY + 1.17f, 0.28f }, (Vector3){ -0.22f, hipY + 0.40f, 0.18f }, r);
    }
    else if (!swinging && a->windup > 0.0f) otherHand = (Vector3){ 0.32f * sgnK, hipY + 0.45f, 0.50f }; // 構え：左手を伸ばして球を指す
    else if (swinging) {
        // フォア：伸ばしていた左手を、振るのに合わせて胸に引きつける（体の回転を止める）
        float r = EaseOut(Clamp01(su / 0.14f));
        otherHand = Vector3Lerp((Vector3){ 0.30f, hipY + 0.45f, 0.48f }, (Vector3){ -0.22f, hipY + 0.40f, 0.18f }, r);
    }
    else otherHand = (Vector3){ lShoulder.x + 0.04f, hipY + 0.32f + 0.05f * cosf(ph), lShoulder.z + 0.10f + sinf(ph) * 0.26f * move };   // 走り：肘を曲げて、右足と一緒に前へ振る

    // スイングの軌跡（振った直後だけ）
    if (a->swingT >= 0.0f && a->swingT < SWING_TIME * 0.8f) {
        SwingAnim tmp = *a;
        Vector3 prev = { 0 };
        for (int i = 0; i <= 14; i++) {
            float t = -0.12f + (a->swingT + 0.12f) * (float)i / 14.0f;   // 少し前から今まで
            Vector3 p, tg; int sk;
            if (t < 0.0f) {
                tmp.swingT = -1.0f; tmp.windup = 1.0f + t / 0.12f;
                tmp.windStroke = a->lastContact.overhead ? 2 : (a->lastContact.forehand ? 0 : 1);
            } else {
                tmp.swingT = t; tmp.windup = 0.0f;
            }
            p = RacketLocal(&tmp, &sk, &tg);
            if (i > 0) DrawLine3D(WV(prev), WV(p), Fade(RAYWHITE, 0.25f + 0.05f * i));
            prev = p;
        }
    }

    // 腕：肩・肘・手首。半袖。肘は下外側に曲がる
    for (int arm = 0; arm < 2; arm++) {
        Vector3 s0 = arm == 0 ? shoulder : other, hd = arm == 0 ? hand : otherHand;
        Vector3 hint = arm == 0 ? (Vector3){ 0.6f, -1.0f, -0.2f } : (Vector3){ -0.6f, -1.0f, -0.2f };
        if (idle) { hint.x *= 0.35f; hint.z = 0.15f; }
        if (idle && move >= 0.3f && arm == 1) hint = (Vector3){ -0.15f, -0.5f, -1.0f };   // 走り：肘は後ろへ   // 構え：肘は体の前に軽く曲げる（横に張ると腰に手を当てたように見える）
        Vector3 el = TwoBone(s0, hd, 0.31f, 0.30f, hint);
        Vector3 sleeve = Vector3Lerp(s0, el, 0.45f);
        Limb(WV(s0), WV(el), 0.050f, 0.043f, skin);
        Limb(WV(el), WV(hd), 0.042f, 0.034f, skin);
        DrawCylinderEx(WV(s0), WV(sleeve), 0.072f, 0.066f, 8, shirt);   // 袖
        DrawSphere(WV(hd), 0.042f, skin);
        if (a->headband.a && arm == 0)   // ヘッドバンドの選手はリストバンドも
            DrawCylinderEx(WV(Vector3Lerp(el, hd, 0.78f)), WV(Vector3Lerp(el, hd, 0.92f)), 0.046f, 0.044f, 8, a->headband);
    }
    Vector3 nW = Vector3Subtract(W(nrm.x, nrm.y, nrm.z), W(0, 0, 0));
    Vector3 dW = Vector3Subtract(WV(rdir), W(0, 0, 0));
    DrawRacketBent(WV(hand), dW, nW, frame, a->broken);
    if (a->holdBall) DrawSphere(W(otherHand.x, otherHand.y + 0.02f, otherHand.z + 0.03f), BALL_R * 1.2f, (Color){ 220, 255, 60, 255 });   // サーブ前：左手にボール
    #undef W
    #undef WV
}

const char *ResultText(ShotResult r)
{
    switch (r) {
        case RESULT_IN:  return "IN";
        case RESULT_OUT: return "OUT";
        case RESULT_NET: return "NET";
        default:         return "-";
    }
}

const char *MissText(MissReason m)
{
    switch (m) {
        case MISS_EARLY:   return "WHIFF: too early";
        case MISS_LATE:    return "WHIFF: too late";
        case MISS_FAR:     return "WHIFF: too far";
        case MISS_HIGH:    return "WHIFF: too high";
        case MISS_LOW:     return "WHIFF: too low";
        case MISS_NO_BALL: return "no ball to hit";
        default:           return "";
    }
}

Color QualityColor(float q)
{
    if (q >= 0.75f) return GREEN;
    if (q >= 0.45f) return YELLOW;
    return RED;
}
