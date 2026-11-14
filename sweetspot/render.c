// SWEET SPOT — コートと人とラケットの描画、表示用の文字
#include "game.h"

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
    DrawCylinder((Vector3){ -NET_POST_X, 0, 0 }, 0.05f, 0.05f, NET_H_POST, 8, DARKGRAY);
    DrawCylinder((Vector3){  NET_POST_X, 0, 0 }, 0.05f, 0.05f, NET_H_POST, 8, DARKGRAY);
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

// ラケットの姿勢。戻り値：面の中心。hand / dir は手の位置とラケットの向き（hasHand=false ならサーブ・スマッシュで、肩から伸ばして描く）
Vector3 RacketPose(const SwingAnim *a, int *strokeOut, Vector3 *normal, Vector3 *hand, Vector3 *dir, bool *hasHand)
{
    const float BACK_OH = 160.0f;
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
    } else if (a->swingT >= 0.0f && a->swingT < SWING_TIME) {
        const Contact *c = &a->lastContact;
        Vector3 cl = a->contactLocal;
        float u = EaseOut(a->swingT / SWING_TIME);
        if (c->overhead) {
            *strokeOut = 2; *hasHand = false;
            float ry = cl.y - 1.75f, rz = cl.z;
            float psiC = atan2f(ry, rz) * RAD2DEG;
            float rc = Clamp(sqrtf(ry * ry + rz * rz), 0.5f, 1.2f);
            float psi = (psiC + (-70.0f - psiC) * u) * DEG2RAD;
            float rr = Lerp(rc, 0.7f, u);
            *normal = (Vector3){ 0.0f, -cosf(psi), sinf(psi) };
            return (Vector3){ 0.3f - 0.6f * u, 1.75f + rr * sinf(psi), rr * cosf(psi) };
        }
        float sgn = c->forehand ? 1.0f : -1.0f;
        *strokeOut = c->forehand ? 0 : 1;
        if (c->volley) {
            // ボレー：振らない。ラケットの頭を立てて面を前に向けたまま、球へ 30cm ほど押し出して止める（パンチ）
            float rcl = sqrtf(cl.x * cl.x + cl.z * cl.z), k = rcl > 1.2f ? 1.2f / rcl : 1.0f;
            Vector3 P1 = { cl.x * k, Clamp(cl.y, 0.2f, 2.0f), cl.z * k };
            Vector3 P0 = { P1.x * 0.85f, P1.y + 0.05f, P1.z - 0.30f };
            Vector3 P2 = { P1.x * 0.95f, P1.y - 0.08f, P1.z + 0.28f };
            Vector3 p = a->swingT < SWING_LEAD ? Vector3Lerp(P0, P1, a->swingT / SWING_LEAD)
                                               : Vector3Lerp(P1, P2, Clamp01((a->swingT - SWING_LEAD) / 0.10f));
            d = Vector3Normalize(V3(sgn * 0.70f, Clamp((P1.y - 0.95f) * 0.9f + 0.25f, -0.60f, 0.65f), 0.35f));
            *hand = Vector3Subtract(p, Vector3Scale(d, RACKET_REACH)); *dir = d;
            *normal = Vector3Normalize(V3(0.0f, 0.25f, 1.0f));
            return p;
        }
        RKey k[8];
        int n = StrokeKeys(a, k, strokeOut);
        SampleKeys(k, n, a->swingT, &h, &d, &f);
    } else if (a->windup > 0.0f) {
        float w = EaseOut(a->windup * 1.6f);
        *strokeOut = a->windStroke;
        if (a->windStroke == 2) {
            *hasHand = false;
            float psi = Lerp(60.0f, BACK_OH, w) * DEG2RAD;
            *normal = (Vector3){ 0.0f, cosf(psi), -sinf(psi) };
            return (Vector3){ 0.3f, 1.75f + 0.8f * sinf(psi), 0.8f * cosf(psi) };
        }
        float sgn = a->windStroke == 0 ? 1.0f : -1.0f;
        if (a->atNet) {
            // ボレーの構え：ラケットを体の前で立てて、横に少し引くだけ
            h = V3(sgn * (0.22f + 0.12f * w), 1.10f, 0.42f - 0.12f * w);
            d = Vector3Normalize(V3(sgn * 0.45f, 0.70f, 0.40f));
            f = V3(0.0f, 0.2f, 1.0f);
        } else {
            RKey e = WindupKey(a->windStroke, a->slice, a->oneHanded, a->fhStyle);
            h = Vector3Lerp(READY.h, e.h, w);
            d = Vector3Normalize(Vector3Lerp(READY.d, e.d, w));
            f = Vector3Lerp(READY.f, e.f, w);
        }
    } else {
        *strokeOut = 0;
        h = READY.h; d = Vector3Normalize(READY.d); f = READY.f;
    }
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
    float turn = (stroke == 2) ? 90.0f : (volleyPose ? 25.0f : (oneHandBack ? 95.0f : 75.0f));   // ボレーは肩をあまり回さない
    float endTurn = volleyPose ? 10.0f : (oneHandBack ? 20.0f : -35.0f);   // 片手バックは肩を開ききらない（横向きのまま振り抜く）
    if (swinging)            yaw = sgnK * Lerp(turn, endTurn, EaseOut(a->swingT / SWING_TIME));
    else if (a->windup > 0)  yaw = sgnK * turn * Clamp01(a->windup * 1.4f);
    yaw *= DEG2RAD;
    float hipYaw = yaw * 0.6f;
    float stanceK = swinging ? 1.0f - Clamp01(a->swingT / SWING_TIME) : Clamp01(a->windup * 1.4f);   // 足を前後に開く量

    // ---- 腰の高さ：構えでは膝を曲げて低く、走ると上下に弾む ----
    float hipY = 0.88f - 0.05f * Clamp01(a->windup) - 0.03f * move * fabsf(sinf(ph));
    Vector3 leanV = { mdx * 0.10f * move, 0.0f, 0.05f + mdz * 0.10f * move };   // 前傾（走る向きへ）
    hipY -= 0.30f * a->crouch;                 // しゃがむ（Q）：腰を落として前に屈む
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
    if (a->smashT > 0.0f) {                    // 叩きつけるたびに前へかがむ
        float down = Clamp01((1.9f - hand.y) / 1.3f);
        leanV.z += 0.28f * down; hipY -= 0.10f * down;
    }

    // ---- サーブ・スマッシュ：トスで膝を深く曲げて反り、振るときに伸び上がって跳ぶ ----
    bool overheadPose = (stroke == 2) && (swinging || a->windup > 0.0f) && move < 0.5f;
    float jump = 0.0f, ohU = 0.0f;
    if (overheadPose) {
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
            float width = 0.14f + 0.10f * (1.0f - move);
            float stride = 0.42f * move;
            // 構え・振り：前足（フォアなら左）を踏み出し、後ろ足を引く。足と腰は横を向く
            float hc = cosf(hipYaw), hs = sinf(hipYaw);
            float bx = sd * width, bz = (sd == -sgnK ? 0.20f : -0.10f) * stanceK;
            Vector3 hip  = { sd * 0.10f * hc + leanV.x * 0.3f, hipY, -sd * 0.10f * hs + leanV.z * 0.3f };
            Vector3 foot = { bx * hc + bz * hs + mdx * sinf(p) * stride, 0.07f + fmaxf(cosf(p), 0.0f) * 0.16f * move,
                             -bx * hs + bz * hc + mdz * sinf(p) * stride };
            float toe = hipYaw + sd * 0.30f;                             // つま先の向き（少し外向き）
            if (overheadPose) {
                // 横向きに構える：左足を前（つま先は右前のポストの方）、右足は後ろでベースラインと平行。
                //   跳んだら右足を後ろに蹴り上げる
                foot = (sd < 0) ? (Vector3){ -0.10f, 0.07f - jump, 0.26f }
                                : (Vector3){ 0.26f, 0.07f - jump + 0.25f * ohU, -0.24f - 0.20f * ohU };
                if (jump > 0.0f) foot.y += jump * 0.6f;   // 跳んでいる間は足も浮く
                toe = (sd < 0) ? 0.75f : 1.50f;
                hip.x = sd * 0.10f * cosf(0.9f); hip.z = -sd * 0.10f * sinf(0.9f) + leanV.z * 0.3f;
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
        DrawCylinderEx(WV(hp), WV(mid), 0.092f, 0.088f, 10, shorts);               // ショーツ（脚の部分は少しゆったり）
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
    Vector3 rShoulder = { chest.x + 0.20f * cy, hipY + 0.55f, chest.z - 0.20f * sy };
    Vector3 lShoulder = { chest.x - 0.20f * cy, hipY + 0.55f, chest.z + 0.20f * sy };
    // ショーツの腰：左右の股関節を包む（丸くしない）
    {
        Vector3 hl = { pelvis.x - 0.085f * hcy, pelvis.y, pelvis.z + 0.085f * hsy };
        Vector3 hr = { pelvis.x + 0.085f * hcy, pelvis.y, pelvis.z - 0.085f * hsy };
        DrawCylinderEx(W(hl.x, hl.y - 0.05f, hl.z), W(hl.x, hl.y + 0.13f, hl.z), 0.10f, 0.105f, 10, shorts);
        DrawCylinderEx(W(hr.x, hr.y - 0.05f, hr.z), W(hr.x, hr.y + 0.13f, hr.z), 0.10f, 0.105f, 10, shorts);
    }
    // シャツ：ウエストから胸へ広がる。胸の左右に 2 本重ねて、横に広く前後に薄くする
    for (int sd = -1; sd <= 1; sd += 2) {
        Vector3 w0 = { waist.x + sd * 0.06f * hcy, waist.y - 0.10f, waist.z - sd * 0.06f * hsy };
        Vector3 c1 = { chest.x + sd * 0.09f * cy, chest.y + 0.08f, chest.z - sd * 0.09f * sy };
        DrawCylinderEx(WV(w0), WV(c1), 0.105f, 0.125f, 10, shirt);
        DrawSphere(WV(c1), 0.125f, shirt);
    }
    Limb(WV(lShoulder), WV(rShoulder), 0.070f, 0.070f, shirt);
    // 首と頭。髪（後ろと上を覆う）、選手によってはヘッドバンド
    Vector3 neckB = { chest.x * 1.05f, hipY + 0.58f, chest.z * 1.05f };
    Vector3 head  = { chest.x * 1.12f, hipY + 0.79f, chest.z * 1.12f + 0.01f };
    DrawCylinderEx(WV(neckB), WV(head), 0.050f, 0.045f, 8, skin);
    DrawSphere(WV(head), 0.105f, skin);
    DrawSphere(W(head.x, head.y + 0.025f, head.z - 0.022f), 0.103f, hair);
    if (a->headband.a) DrawCylinderEx(W(head.x, head.y + 0.025f, head.z), W(head.x, head.y + 0.060f, head.z), 0.110f, 0.108f, 14, a->headband);

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
    Vector3 neck = Vector3Add(hand, Vector3Scale(rdir, 0.28f));       // ラケットの首（のど）
    Vector3 shoulder = rShoulder, other = lShoulder;
    Vector3 otherHand;
    bool idle = !swinging && a->windup <= 0.0f;
    float su = swinging ? a->swingT : 0.0f;
    if (a->smashT > 0.0f)            otherHand = (Vector3){ -0.32f, hipY + 0.32f, 0.12f };            // 叩きつけ：片手で
    else if (twoHands)               otherHand = Vector3Add(hand, Vector3Scale(rdir, 0.10f));          // グリップの少し上
    else if (stroke == 1 && !swinging && a->windup > 0.0f && !a->atNet) otherHand = neck;          // 片手バックの構え：左手で首を支える
    else if (stroke == 1 && swinging && !volleyPose) {
        // 片手バック・スライス：振り出しで左手を離し、後ろへ伸ばしてバランスを取る（両腕を広げる）
        float r = Clamp01((su - 0.03f) / 0.12f);
        Vector3 back = { -0.55f, hipY + 0.38f + 0.06f * a->grace, -0.42f };
        otherHand = Vector3Lerp(su < 0.03f ? neck : Vector3Add(hand, Vector3Scale(rdir, 0.28f)), back, EaseOut(r));
    }
    else if (idle && move < 0.3f)    otherHand = neck;                                                  // 構え：左手をラケットの首に
    else if (stroke == 2 && !swinging) otherHand = (Vector3){ -0.05f, hipY + 1.15f, 0.35f };            // サーブ・スマッシュの構え：左手を上に
    else if (!swinging && a->windup > 0.0f) otherHand = (Vector3){ 0.32f * sgnK, hipY + 0.45f, 0.50f }; // 構え：左手を伸ばして球を指す
    else if (swinging) {
        // フォア：伸ばしていた左手を、振るのに合わせて胸に引きつける（体の回転を止める）
        float r = EaseOut(Clamp01(su / 0.14f));
        otherHand = Vector3Lerp((Vector3){ 0.30f, hipY + 0.45f, 0.48f }, (Vector3){ -0.22f, hipY + 0.40f, 0.18f }, r);
    }
    else otherHand = (Vector3){ lShoulder.x + 0.04f, hipY + 0.30f + 0.06f * cosf(ph), lShoulder.z + 0.12f - sinf(ph) * 0.26f * move };   // 走り：肘を曲げて足と逆に振る

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
