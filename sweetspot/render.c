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
    const float BACK_FH = -110.0f, BACK_OH = 160.0f;
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

    float swingAmt = Clamp(speed / 6.0f, 0.0f, 1.0f) * 0.32f;
    float ph = a->runPhase;
    if (a->sliding) {
        // スライド：腰を落として足を大きく開く。足元に滑った跡
        float sd = a->slideSide >= 0.0f ? 1.0f : -1.0f;
        DrawCapsule(W( sd * 0.12f, 0.72f, 0.0f), W( sd * 0.75f, 0.05f, 0.05f), 0.075f, 6, 3, pants);
        DrawCapsule(W(-sd * 0.12f, 0.72f, 0.0f), W(-sd * 0.55f, 0.05f, -0.1f), 0.075f, 6, 3, pants);
        DrawCylinderEx(W(sd * 0.75f, 0.01f, 0.05f), W(sd * 1.35f, 0.01f, 0.05f), 0.05f, 0.02f, 6, Fade(RAYWHITE, 0.5f));
    } else {
        // 足（走っていれば交互に振る）
        DrawCapsule(W( 0.11f, 0.9f, 0.0f), W( 0.13f, 0.06f,  sinf(ph) * swingAmt), 0.075f, 6, 3, pants);
        DrawCapsule(W(-0.11f, 0.9f, 0.0f), W(-0.13f, 0.06f, -sinf(ph) * swingAmt), 0.075f, 6, 3, pants);
    }
    // 胴と頭
    DrawCapsule(W(0.0f, 0.95f, 0.0f), W(0.0f, 1.42f, 0.0f), 0.17f, 8, 4, shirt);
    DrawSphere(W(0.0f, 1.68f, 0.0f), 0.12f, skin);

    // ラケットと腕
    int stroke; Vector3 tan;
    Vector3 rl = RacketLocal(a, &stroke, &tan);
    // 右利き：ラケットはいつも右手。バックハンドは両手打ち（右手でグリップ、左手をその上に添える）
    //   ラケットの向きは、バックなら左肩を支点に考える（両手打ちは左手が主導して振る）
    Vector3 rShoulder = { 0.22f, 1.42f, 0.0f }, lShoulder = { -0.22f, 1.42f, 0.0f };
    bool twoHands = (stroke == 1) && !a->slice;   // スライスのバックは片手
    Vector3 pivot = twoHands ? lShoulder : rShoulder;
    Vector3 dir = Vector3Normalize(Vector3Subtract(rl, pivot));
    Vector3 hand = Vector3Subtract(rl, Vector3Scale(dir, 0.55f));
    float armLen = Vector3Length(Vector3Subtract(hand, pivot));
    if (armLen > 0.75f) hand = Vector3Add(pivot, Vector3Scale(Vector3Normalize(Vector3Subtract(hand, pivot)), 0.75f));
    Vector3 neck = Vector3Subtract(rl, Vector3Scale(dir, 0.14f));
    if (stroke == 1 && !twoHands) {
        // 片手のバック：右肩から腕を伸ばす
        dir = Vector3Normalize(Vector3Subtract(rl, rShoulder));
        hand = Vector3Subtract(rl, Vector3Scale(dir, 0.55f));
        if (Vector3Length(Vector3Subtract(hand, rShoulder)) > 0.75f)
            hand = Vector3Add(rShoulder, Vector3Scale(Vector3Normalize(Vector3Subtract(hand, rShoulder)), 0.75f));
        neck = Vector3Subtract(rl, Vector3Scale(dir, 0.14f));
    }
    Vector3 shoulder = rShoulder;                                    // ラケットを持つ腕（右）
    Vector3 other = lShoulder;                                       // 左腕
    Vector3 otherHand = twoHands ? Vector3Add(hand, Vector3Scale(dir, 0.09f))   // グリップの少し上
                                 : (Vector3){ -0.31f, 1.0f, 0.2f };             // フォア・頭上：左手は体の前

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

    DrawCapsule(W(shoulder.x, shoulder.y, shoulder.z), W(hand.x, hand.y, hand.z), 0.05f, 6, 3, skin);
    DrawCapsule(W(other.x, other.y, other.z), W(otherHand.x, otherHand.y, otherHand.z), 0.05f, 6, 3, skin);
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
