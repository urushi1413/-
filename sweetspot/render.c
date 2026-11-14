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

// ラケットの面の中心（ローカル座標）と、ラケットが動いている向き
// 2 次ベジェ曲線（p0 から c の方へ引っ張られて p1 へ）と、その向き
static Vector3 Bezier2(Vector3 p0, Vector3 c, Vector3 p1, float s)
{
    float a = (1 - s) * (1 - s), b = 2 * (1 - s) * s, d = s * s;
    return (Vector3){ a * p0.x + b * c.x + d * p1.x, a * p0.y + b * c.y + d * p1.y, a * p0.z + b * c.z + d * p1.z };
}
static Vector3 Bezier2D(Vector3 p0, Vector3 c, Vector3 p1, float s)
{
    return Vector3Add(Vector3Scale(Vector3Subtract(c, p0), 2 * (1 - s)), Vector3Scale(Vector3Subtract(p1, c), 2 * s));
}

Vector3 RacketLocal(const SwingAnim *a, int *strokeOut, Vector3 *tangent)
{
    const float BACK_FH = -75.0f, BACK_OH = 160.0f;
    if (a->swingT >= 0.0f && a->swingT < SWING_TIME) {
        const Contact *c = &a->lastContact;
        Vector3 cl = a->contactLocal;
        float u = EaseOut(a->swingT / SWING_TIME);
        if (c->overhead) {
            *strokeOut = 2;
            float ry = cl.y - 1.75f, rz = cl.z;
            float psiC = atan2f(ry, rz) * RAD2DEG;
            float rc = Clamp(sqrtf(ry * ry + rz * rz), 0.5f, 1.2f);
            float psi = (psiC + (-70.0f - psiC) * u) * DEG2RAD;
            float rr = Lerp(rc, 0.7f, u);
            *tangent = (Vector3){ 0.0f, -cosf(psi), sinf(psi) };
            return (Vector3){ 0.3f - 0.6f * u, 1.75f + rr * sinf(psi), rr * cosf(psi) };
        }
        float sgn = c->forehand ? 1.0f : -1.0f;
        *strokeOut = c->forehand ? 0 : 1;
        if (c->volley) {
            // ボレー：振らない。面を前に向けたまま、球へ 30cm ほど押し出して止める（パンチ）
            float rcl = sqrtf(cl.x * cl.x + cl.z * cl.z), k = rcl > 1.2f ? 1.2f / rcl : 1.0f;
            Vector3 P1 = { cl.x * k, Clamp(cl.y, 0.2f, 2.0f), cl.z * k };
            Vector3 P0 = { P1.x * 0.85f, P1.y + 0.05f, P1.z - 0.30f };
            Vector3 P2 = { P1.x * 0.95f, P1.y - 0.08f, P1.z + 0.28f };
            *tangent = Vector3Normalize((Vector3){ 0.0f, 0.25f, 1.0f });   // 面はネットの方、少し上向き
            if (a->swingT < SWING_LEAD) return Vector3Lerp(P0, P1, a->swingT / SWING_LEAD);
            return Vector3Lerp(P1, P2, Clamp01((a->swingT - SWING_LEAD) / 0.10f));
        }
        float phiC = atan2f(cl.z, sgn * cl.x) * RAD2DEG;
        float rc = Clamp(sqrtf(cl.x * cl.x + cl.z * cl.z), 0.5f, 1.6f);
        float hc = Clamp(cl.y, 0.15f, 1.9f);
        if (a->slice) {
            // スライス：上から下へ切る。フォロースルーは短く前に押し出し、面は上を向いたまま
            float phi = (phiC + (140.0f - phiC) * u) * DEG2RAD;
            float rr = Lerp(rc, 0.8f, u);
            *tangent = Vector3Normalize((Vector3){ -sgn * sinf(phi), 0.45f, cosf(phi) });
            return (Vector3){ sgn * cosf(phi) * rr, Lerp(hc, fmaxf(hc - 0.25f, 0.8f), u), sinf(phi) * rr };
        }
        // トップスピン・フラットドライブ：構えから一度ラケットを球の下へ落とし（ラケットダウン）、
        //   下から上へ振り抜いて、反対の肩の上で終わる（ワイパー）。面は振り抜くほど伏せる
        (void)phiC;
        float k = rc / fmaxf(sqrtf(cl.x * cl.x + cl.z * cl.z), 0.01f);
        Vector3 P0 = { sgn * cosf(BACK_FH * DEG2RAD) * 0.85f, 1.25f, sinf(BACK_FH * DEG2RAD) * 0.85f };   // 構えの終わり
        Vector3 C0 = { sgn * 0.80f, fmaxf(hc - 0.40f, 0.15f), -0.40f };                                     // ラケットダウン
        Vector3 P1 = { cl.x * k, hc, cl.z * k };                                                            // 当たる点
        Vector3 C1 = { sgn * 0.20f, hc + 0.55f, 0.95f };                                                    // 前に振り抜く
        Vector3 P2 = { -sgn * 0.35f, 1.62f, -0.10f };                                                       // 反対の肩の上
        Vector3 p, d;
        if (a->swingT < SWING_LEAD) {
            float s = a->swingT / SWING_LEAD;
            p = Bezier2(P0, C0, P1, s); d = Bezier2D(P0, C0, P1, s);
        } else {
            float s = EaseOut((a->swingT - SWING_LEAD) / (SWING_TIME - SWING_LEAD));
            p = Bezier2(P1, C1, P2, s); d = Bezier2D(P1, C1, P2, s);
            u = s;
        }
        Vector3 dh = { d.x, 0.0f, d.z };
        if (Vector3Length(dh) < 1e-3f) dh = (Vector3){ 0, 0, 1 };
        dh = Vector3Normalize(dh);
        *tangent = Vector3Normalize((Vector3){ dh.x, -0.15f - 0.6f * (a->swingT < SWING_LEAD ? 0.0f : u), dh.z });
        return p;
    }
    if (a->windup > 0.0f) {
        float w = EaseOut(a->windup * 1.6f);
        *strokeOut = a->windStroke;
        if (a->windStroke == 2) {
            float psi = Lerp(60.0f, BACK_OH, w) * DEG2RAD;
            *tangent = (Vector3){ 0.0f, cosf(psi), -sinf(psi) };
            return (Vector3){ 0.3f, 1.75f + 0.8f * sinf(psi), 0.8f * cosf(psi) };
        }
        float sgn = a->windStroke == 0 ? 1.0f : -1.0f;
        if (a->atNet) {
            // ボレーの構え：ラケットを体の前に出して、横に少し引くだけ
            *tangent = Vector3Normalize((Vector3){ 0.0f, 0.2f, 1.0f });
            return (Vector3){ sgn * (0.40f + 0.20f * w), 1.25f, 0.45f - 0.20f * w };
        }
        if (a->slice) {
            // スライスの構え：ラケットを肩より高く引き、面を上に開く
            float phi = Lerp(60.0f, -85.0f, w) * DEG2RAD;
            *tangent = Vector3Normalize((Vector3){ -sgn * sinf(phi), 0.45f, cosf(phi) });
            return (Vector3){ sgn * cosf(phi) * Lerp(0.6f, 0.75f, w), Lerp(1.0f, 1.65f, w), sinf(phi) * Lerp(0.6f, 0.75f, w) };
        }
        float phi = Lerp(60.0f, BACK_FH, w) * DEG2RAD;
        *tangent = (Vector3){ -sgn * sinf(phi), 0.0f, cosf(phi) };
        return (Vector3){ sgn * cosf(phi) * Lerp(0.6f, 0.85f, w), Lerp(1.0f, 1.25f, w), sinf(phi) * Lerp(0.6f, 0.85f, w) };
    }
    *strokeOut = 0;
    *tangent = (Vector3){ 0.0f, 0.0f, 1.0f };
    return (Vector3){ 0.25f, 1.0f, 0.55f };   // 構え：ラケットを前に
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

void DrawFigure(Vector3 pos, bool facingNegZ, Color shirt, const SwingAnim *a, float speed)
{
    Color skin = { 240, 200, 160, 255 }, pants = { 40, 40, 60, 255 }, frame = { 30, 30, 30, 255 };
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

    if (a->downT > 0.0f) {
        // 飛びついて倒れている
        float s = a->lastContact.forehand ? 1.0f : -1.0f;
        DrawCapsule(W(s * 0.15f, 0.18f, 0.0f), W(s * 0.95f, 0.22f, 0.15f), 0.17f, 8, 4, shirt);
        DrawSphere(W(s * 1.2f, 0.22f, 0.2f), 0.12f, skin);
        DrawCapsule(W(s * 0.1f, 0.12f, 0.0f), W(-s * 0.7f, 0.08f, -0.15f), 0.08f, 6, 3, pants);
        DrawCapsule(W(s * 0.1f, 0.12f, 0.05f), W(-s * 0.65f, 0.08f, 0.2f), 0.08f, 6, 3, pants);
        DrawCapsule(W(s * 0.9f, 0.25f, 0.2f), W(s * 1.45f, 0.08f, 0.35f), 0.05f, 6, 3, skin);
        DrawRacketHead(W(s * 1.95f, 0.06f, 0.45f), (Vector3){ 0, 1, 0 }, frame);
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
    int strokeK; Vector3 tanK;
    RacketLocal(a, &strokeK, &tanK);
    float sgnK = (strokeK == 1) ? -1.0f : 1.0f;
    float yaw = 0.0f;
    bool swinging = a->swingT >= 0.0f && a->swingT < SWING_TIME;
    // 構えでは横を向く（ストロークは 75°、サーブ・スマッシュはほぼ真横の 90°）。腰は肩の 6 割回る
    bool volleyPose = swinging ? (a->lastContact.volley && !a->lastContact.overhead) : a->atNet;
    float turn = (strokeK == 2) ? 90.0f : (volleyPose ? 25.0f : 75.0f);   // ボレーは肩をあまり回さない
    if (swinging)            yaw = sgnK * Lerp(turn, volleyPose ? 10.0f : -35.0f, EaseOut(a->swingT / SWING_TIME));
    else if (a->windup > 0)  yaw = sgnK * turn * Clamp01(a->windup * 1.4f);
    yaw *= DEG2RAD;
    float hipYaw = yaw * 0.6f;
    float stanceK = swinging ? 1.0f - Clamp01(a->swingT / SWING_TIME) : Clamp01(a->windup * 1.4f);   // 足を前後に開く量

    // ---- 腰の高さ：構えでは膝を曲げて低く、走ると上下に弾む ----
    float hipY = 0.86f - 0.04f * Clamp01(a->windup) - 0.03f * move * fabsf(sinf(ph));
    Vector3 leanV = { mdx * 0.10f * move, 0.0f, 0.06f + mdz * 0.10f * move };   // 前傾（走る向きへ）
    // しゃがむ（Q）：腰を落として前に屈む
    hipY -= 0.30f * a->crouch;
    leanV.z += 0.10f * a->crouch;

    // ---- サーブ・スマッシュ：トスで膝を深く曲げて反り、振るときに伸び上がって跳ぶ ----
    bool overheadPose = (strokeK == 2) && (swinging || a->windup > 0.0f) && move < 0.5f;
    float jump = 0.0f, ohU = 0.0f;
    if (overheadPose) {
        if (swinging) {
            ohU = Clamp01(a->swingT / SWING_TIME);
            jump = 0.20f * sinf(PI * Clamp01(ohU * 1.5f));
            hipY = 0.93f;
            leanV = (Vector3){ 0.0f, 0.0f, -0.04f + 0.22f * ohU };     // 打ちながら前へ倒れ込む
        } else {
            float w = Clamp01(a->windup);
            hipY = 0.86f - 0.17f * w;                                    // トロフィー：膝を深く曲げる
            leanV = (Vector3){ 0.06f * w, 0.0f, -0.10f * w };            // 後ろに反る
        }
        pos.y += jump;
    }

    // ---- 跳んで伸びて取る（リターンなどで遠い球）：球の方へ跳び、体を倒して足を大きく開く ----
    bool lunging = a->lungeT > 0.0f && !overheadPose;
    if (lunging) {
        float pr = 1.0f - a->lungeT / LUNGE_TIME;
        float jl = 0.22f * sinf(PI * Clamp01(pr * 1.3f));
        pos.y += jl;
        float sd = a->lungeSide;
        leanV = (Vector3){ sd * 0.28f, 0.0f, 0.10f };
        hipY = 0.76f;
        hipYaw = 0.0f;
    }

    if (a->sliding || lunging) {
        // スライド・飛びつき：腰を落として足を大きく開く。スライドは足元に滑った跡
        float sd = lunging ? a->lungeSide : (a->slideSide >= 0.0f ? 1.0f : -1.0f);
        if (!lunging) hipY = 0.70f;
        float lift = lunging ? 0.10f : 0.0f;
        Vector3 h1 = { sd * 0.10f + leanV.x * 0.3f, hipY, 0.0f }, f1 = { sd * 0.85f, 0.06f, 0.10f };
        Vector3 h2 = { -sd * 0.10f + leanV.x * 0.3f, hipY, 0.0f }, f2 = { -sd * 0.40f, 0.06f + lift, -0.20f };
        Vector3 k1 = TwoBone(h1, f1, 0.45f, 0.45f, (Vector3){ sd * 0.3f, 0.6f, 0.4f });
        Vector3 k2 = TwoBone(h2, f2, 0.45f, 0.45f, (Vector3){ 0.0f, 0.2f, 1.0f });
        DrawCapsule(W(h1.x, h1.y, h1.z), W(k1.x, k1.y, k1.z), 0.075f, 6, 3, pants);
        DrawCapsule(W(k1.x, k1.y, k1.z), W(f1.x, f1.y, f1.z), 0.065f, 6, 3, pants);
        DrawCapsule(W(h2.x, h2.y, h2.z), W(k2.x, k2.y, k2.z), 0.075f, 6, 3, pants);
        DrawCapsule(W(k2.x, k2.y, k2.z), W(f2.x, f2.y, f2.z), 0.065f, 6, 3, pants);
        DrawCapsule(W(f1.x, f1.y - 0.02f, f1.z), W(f1.x + sd * 0.10f, f1.y - 0.03f, f1.z + 0.08f), 0.045f, 6, 3, RAYWHITE);
        DrawCapsule(W(f2.x, f2.y - 0.02f, f2.z), W(f2.x + sd * 0.12f, f2.y - 0.03f, f2.z + 0.04f), 0.045f, 6, 3, RAYWHITE);
        if (!lunging) DrawCylinderEx(W(sd * 0.80f, 0.01f, 0.05f), W(sd * 1.40f, 0.01f, 0.05f), 0.05f, 0.02f, 6, Fade(RAYWHITE, 0.5f));
    } else {
        // 足：股関節・膝・足首。構えでは足を広げて膝を曲げ、走ると交互に踏み出して足を上げる
        Vector3 hips[2];
        for (int i = 0; i < 2; i++) {
            float sd = i == 0 ? 1.0f : -1.0f;
            float p = ph + (i == 0 ? 0.0f : PI);
            float width = 0.14f + 0.10f * (1.0f - move);
            float stride = 0.42f * move;
            // 構え・振り：前足（フォアなら左）を踏み出し、後ろ足を引く。足と腰は横を向く
            float hc = cosf(hipYaw), hs = sinf(hipYaw);
            float bx = sd * width, bz = (sd == -sgnK ? 0.20f : -0.10f) * stanceK;
            Vector3 hip  = { sd * 0.10f * hc + leanV.x * 0.3f, hipY, -sd * 0.10f * hs + leanV.z * 0.3f };
            Vector3 foot = { bx * hc + bz * hs + mdx * sinf(p) * stride, 0.06f + fmaxf(cosf(p), 0.0f) * 0.16f * move,
                             -bx * hs + bz * hc + mdz * sinf(p) * stride };
            float toe = hipYaw + sd * 0.30f;                             // つま先の向き（少し外向き）
            if (overheadPose) {
                // 横向きに構える：左足を前（つま先は右前のポストの方）、右足は後ろでベースラインと平行。
                //   跳んだら右足を後ろに蹴り上げる
                foot = (sd < 0) ? (Vector3){ -0.10f, 0.06f - jump, 0.26f }
                                : (Vector3){ 0.26f, 0.06f - jump + 0.25f * ohU, -0.24f - 0.20f * ohU };
                if (jump > 0.0f) foot.y += jump * 0.6f;   // 跳んでいる間は足も浮く
                toe = (sd < 0) ? 0.75f : 1.50f;
                hip.x = sd * 0.10f * cosf(0.9f); hip.z = -sd * 0.10f * sinf(0.9f) + leanV.z * 0.3f;
            }
            Vector3 hint = { sd * 0.25f * cosf(hipYaw) + sinf(hipYaw), 0.0f, -sd * 0.25f * sinf(hipYaw) + cosf(hipYaw) };
            Vector3 knee = TwoBone(hip, foot, 0.44f, 0.44f, hint);
            DrawCapsule(W(hip.x, hip.y, hip.z), W(knee.x, knee.y, knee.z), 0.075f, 6, 3, pants);
            DrawCapsule(W(knee.x, knee.y, knee.z), W(foot.x, foot.y, foot.z), 0.062f, 6, 3, pants);
            float tx = sinf(toe), tz = cosf(toe);
            DrawCapsule(W(foot.x - tx * 0.04f, foot.y - 0.02f, foot.z - tz * 0.04f),
                        W(foot.x + tx * 0.13f, foot.y - 0.03f, foot.z + tz * 0.13f), 0.045f, 6, 3, RAYWHITE);
            hips[i] = hip;
        }
        // 腰の線（腰が回っているのが見えるように）
        DrawCapsule(W(hips[0].x, hips[0].y + 0.03f, hips[0].z), W(hips[1].x, hips[1].y + 0.03f, hips[1].z), 0.085f, 6, 3, pants);
    }

    // ---- 胴：骨盤から胸まで。肩の線はひねりに合わせて回る ----
    Vector3 pelvis = { leanV.x * 0.3f, hipY + 0.06f, leanV.z * 0.3f };
    Vector3 chest  = { leanV.x, hipY + 0.50f, leanV.z };
    float cy = cosf(yaw), sy = sinf(yaw);
    Vector3 rShoulder = { chest.x + 0.21f * cy, hipY + 0.56f, chest.z - 0.21f * sy };
    Vector3 lShoulder = { chest.x - 0.21f * cy, hipY + 0.56f, chest.z + 0.21f * sy };
    DrawCapsule(W(pelvis.x, pelvis.y, pelvis.z), W(chest.x, chest.y, chest.z), 0.16f, 8, 4, shirt);
    DrawCapsule(W(lShoulder.x, lShoulder.y, lShoulder.z), W(rShoulder.x, rShoulder.y, rShoulder.z), 0.075f, 6, 3, shirt);
    DrawSphere(W(chest.x * 1.1f, hipY + 0.80f, chest.z * 1.1f), 0.12f, skin);

    // ---- ラケットと腕 ----
    int stroke; Vector3 tan;
    Vector3 rl = RacketLocal(a, &stroke, &tan);
    // 右利き：ラケットはいつも右手。バックハンドは両手打ち（右手でグリップ、左手をその上に添える）。スライスのバックは片手
    bool twoHands = (stroke == 1) && !a->slice && !a->oneHanded;   // 片手バックの選手もいる
    Vector3 pivot = twoHands ? lShoulder : rShoulder;
    Vector3 dir = Vector3Normalize(Vector3Subtract(rl, pivot));
    Vector3 hand = Vector3Subtract(rl, Vector3Scale(dir, 0.55f));       // グリップ
    // 手首：振り出しでは手が先に出てラケットの頭が遅れ（ラグ）、当たる瞬間にそろい、振り抜くと頭が追い越す
    if (swinging && stroke != 2 && !(a->lastContact.volley && !a->lastContact.overhead)) {   // ボレーは手首を固める
        SwingAnim pa = *a; pa.swingT = fmaxf(a->swingT - 0.01f, 0.0f);
        int sk2; Vector3 tg2;
        Vector3 rp = RacketLocal(&pa, &sk2, &tg2);
        Vector3 v = Vector3Subtract(rl, rp);
        if (Vector3Length(v) > 1e-4f) {
            v = Vector3Normalize(v);
            float lag = a->swingT < SWING_LEAD ? 0.18f * (1.0f - a->swingT / SWING_LEAD)
                                               : -0.08f * Clamp01((a->swingT - SWING_LEAD) / 0.08f);
            hand = Vector3Add(hand, Vector3Scale(v, lag));
        }
    } else if (!swinging && a->windup > 0.0f && stroke != 2 && !a->atNet) {
        // 構え：ラケットの頭を立てて手首を返す
        hand = Vector3Add(hand, (Vector3){ 0.0f, -0.12f * a->windup, 0.10f * a->windup });
    }
    dir = Vector3Normalize(Vector3Subtract(rl, hand));
    Vector3 neck = Vector3Subtract(rl, Vector3Scale(dir, 0.14f));       // ラケットの首
    Vector3 shoulder = rShoulder, other = lShoulder;
    Vector3 otherHand;
    bool idle = !swinging && a->windup <= 0.0f;
    if (twoHands)          otherHand = Vector3Add(hand, Vector3Scale(dir, 0.09f));         // グリップの少し上
    else if (idle && move < 0.3f) otherHand = neck;                                        // 構え：左手をラケットの首に
    else if (stroke == 2 && !swinging) otherHand = (Vector3){ -0.05f, hipY + 1.15f, 0.35f }; // サーブ・スマッシュの構え：左手を上に
    else if (!swinging && a->windup > 0.0f) otherHand = (Vector3){ 0.25f * sgnK, hipY + 0.42f, 0.45f }; // 構え：左手で球を指す
    else if (swinging)     otherHand = (Vector3){ -0.28f, hipY + 0.25f, 0.10f };          // 振っている間は体に引きつける
    else                   otherHand = (Vector3){ lShoulder.x - 0.05f, hipY + 0.12f, lShoulder.z - sinf(ph) * 0.28f * move };   // 走り：足と逆に振る

    // スイングの軌跡（振った直後だけ）
    if (a->swingT >= 0.0f && a->swingT < SWING_TIME * 0.8f) {
        SwingAnim tmp = *a;
        Vector3 prev = { 0 };
        for (int i = 0; i <= 14; i++) {
            float t = -0.12f + (a->swingT + 0.12f) * (float)i / 14.0f;   // 少し前から今まで
            Vector3 p, tg; int sk;
            if (t < 0.0f) {
                // 振り始め：テイクバックから当たる点まで
                tmp.swingT = -1.0f; tmp.windup = 1.0f + t / 0.12f;
                tmp.windStroke = a->lastContact.overhead ? 2 : (a->lastContact.forehand ? 0 : 1);
            } else {
                tmp.swingT = t; tmp.windup = 0.0f;
            }
            p = RacketLocal(&tmp, &sk, &tg);
            if (i > 0) DrawLine3D(W(prev.x, prev.y, prev.z), W(p.x, p.y, p.z), Fade(RAYWHITE, 0.25f + 0.05f * i));
            prev = p;
        }
    }

    // 腕：肩・肘・手首。肘は下外側に曲がる
    {
        Vector3 e1 = TwoBone(shoulder, hand, 0.31f, 0.30f, (Vector3){ 0.6f, -1.0f, -0.2f });
        Vector3 e2 = TwoBone(other, otherHand, 0.31f, 0.30f, (Vector3){ -0.6f, -1.0f, -0.2f });
        DrawCapsule(W(shoulder.x, shoulder.y, shoulder.z), W(e1.x, e1.y, e1.z), 0.052f, 6, 3, skin);
        DrawCapsule(W(e1.x, e1.y, e1.z), W(hand.x, hand.y, hand.z), 0.045f, 6, 3, skin);
        DrawCapsule(W(other.x, other.y, other.z), W(e2.x, e2.y, e2.z), 0.052f, 6, 3, skin);
        DrawCapsule(W(e2.x, e2.y, e2.z), W(otherHand.x, otherHand.y, otherHand.z), 0.045f, 6, 3, skin);
    }
    DrawCylinderEx(W(hand.x, hand.y, hand.z), W(neck.x, neck.y, neck.z), 0.015f, 0.015f, 6, frame);
    Vector3 nW = Vector3Subtract(W(tan.x, tan.y, tan.z), W(0, 0, 0));
    DrawRacketHead(W(rl.x, rl.y, rl.z), nW, frame);
    #undef W
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
