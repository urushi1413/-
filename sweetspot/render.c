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

    float dx = DOUBLES_W * 0.5f, sx = SINGLES_W * 0.5f;
    LineAlongX( HALF_LEN, -dx, dx);
    LineAlongX(-HALF_LEN, -dx, dx);
    LineAlongZ(-dx, -HALF_LEN, HALF_LEN);
    LineAlongZ( dx, -HALF_LEN, HALF_LEN);
    LineAlongZ(-sx, -HALF_LEN, HALF_LEN);
    LineAlongZ( sx, -HALF_LEN, HALF_LEN);
    LineAlongX( SERVICE_Z, -sx, sx);
    LineAlongX(-SERVICE_Z, -sx, sx);
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
        float phi = (phiC + (190.0f - phiC) * u) * DEG2RAD;
        float rr = Lerp(rc, 0.55f, u);
        *tangent = (Vector3){ -sgn * sinf(phi), 0.0f, cosf(phi) };
        return (Vector3){ sgn * cosf(phi) * rr, Lerp(hc, 1.5f, u), sinf(phi) * rr };
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
    #define W(x, y, z) LocalToWorld(pos, facingNegZ, (Vector3){ (x), (y), (z) })

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
    if (swinging)            yaw = sgnK * Lerp(50.0f, -35.0f, EaseOut(a->swingT / SWING_TIME));
    else if (a->windup > 0)  yaw = sgnK * 55.0f * Clamp01(a->windup * 1.4f);
    yaw *= DEG2RAD;

    // ---- 腰の高さ：構えでは膝を曲げて低く、走ると上下に弾む ----
    float hipY = 0.86f - 0.04f * Clamp01(a->windup) - 0.03f * move * fabsf(sinf(ph));
    float lean = 0.06f + 0.10f * move;   // 前傾（走る向きへ）
    Vector3 leanV = { mdx * 0.10f * move, 0.0f, 0.06f + mdz * 0.10f * move };
    (void)lean;

    if (a->sliding) {
        // スライド：腰を落として足を大きく開く。足元に滑った跡
        float sd = a->slideSide >= 0.0f ? 1.0f : -1.0f;
        hipY = 0.70f;
        Vector3 h1 = { sd * 0.10f, hipY, 0.0f }, f1 = { sd * 0.80f, 0.06f, 0.05f };
        Vector3 h2 = { -sd * 0.10f, hipY, 0.0f }, f2 = { -sd * 0.35f, 0.06f, -0.15f };
        Vector3 k1 = TwoBone(h1, f1, 0.45f, 0.45f, (Vector3){ sd * 0.3f, 0.6f, 0.4f });
        Vector3 k2 = TwoBone(h2, f2, 0.45f, 0.45f, (Vector3){ 0.0f, 0.2f, 1.0f });
        DrawCapsule(W(h1.x, h1.y, h1.z), W(k1.x, k1.y, k1.z), 0.075f, 6, 3, pants);
        DrawCapsule(W(k1.x, k1.y, k1.z), W(f1.x, f1.y, f1.z), 0.065f, 6, 3, pants);
        DrawCapsule(W(h2.x, h2.y, h2.z), W(k2.x, k2.y, k2.z), 0.075f, 6, 3, pants);
        DrawCapsule(W(k2.x, k2.y, k2.z), W(f2.x, f2.y, f2.z), 0.065f, 6, 3, pants);
        DrawCylinderEx(W(sd * 0.80f, 0.01f, 0.05f), W(sd * 1.40f, 0.01f, 0.05f), 0.05f, 0.02f, 6, Fade(RAYWHITE, 0.5f));
    } else {
        // 足：股関節・膝・足首。構えでは足を広げて膝を曲げ、走ると交互に踏み出して足を上げる
        for (int i = 0; i < 2; i++) {
            float sd = i == 0 ? 1.0f : -1.0f;
            float p = ph + (i == 0 ? 0.0f : PI);
            float width = 0.14f + 0.10f * (1.0f - move);
            float stride = 0.42f * move;
            Vector3 hip  = { sd * 0.10f + leanV.x * 0.3f, hipY, leanV.z * 0.3f };
            Vector3 foot = { sd * width + mdx * sinf(p) * stride, 0.06f + fmaxf(cosf(p), 0.0f) * 0.16f * move,
                             mdz * sinf(p) * stride };
            Vector3 knee = TwoBone(hip, foot, 0.44f, 0.44f, (Vector3){ sd * 0.25f, 0.0f, 1.0f });
            DrawCapsule(W(hip.x, hip.y, hip.z), W(knee.x, knee.y, knee.z), 0.075f, 6, 3, pants);
            DrawCapsule(W(knee.x, knee.y, knee.z), W(foot.x, foot.y, foot.z), 0.062f, 6, 3, pants);
            DrawCapsule(W(foot.x, foot.y - 0.02f, foot.z - 0.04f), W(foot.x, foot.y - 0.03f, foot.z + 0.13f), 0.045f, 6, 3, RAYWHITE);
        }
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
    bool twoHands = (stroke == 1) && !a->slice;
    Vector3 pivot = twoHands ? lShoulder : rShoulder;
    Vector3 dir = Vector3Normalize(Vector3Subtract(rl, pivot));
    Vector3 hand = Vector3Subtract(rl, Vector3Scale(dir, 0.55f));       // グリップ
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
