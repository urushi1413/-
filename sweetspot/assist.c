// SWEET SPOT — 手がかり表示（立つ場所・タイミングの輪）の計算
#include "game.h"

// ================= 手がかり表示（画面に奥行きがないのを補う） =================
// 物理や判定は変えない。人間が目で読めない「いつ」「どこ」を画面に出すだけ

Assist ComputeAssist(Vector3 player, Vector3 playerVel, const Ball *ball, ShotType type, float charge)
{
    Assist a = { 0 };
    if (!ball->active || ball->lastHitter != HITTER_OPP) return a;
    Ball b = *ball;
    float t = 0.0f, bestCost = 1e9f;
    a.qBest = 0.0f;
    for (int i = 0; i < 360 && b.active && b.bounces < 2; i++) {
        StepBall(&b, PHYS_DT);
        t += PHYS_DT;
        bool hittable = b.bounces <= 1 && b.pos.z > 0.0f && !b.hitNet && (!b.isServe || b.bounces == 1);
        if (!hittable) continue;
        // タイミング：今の位置のまま、いつ振るのが一番いいか
        Contact c = EvaluateContact(player, &b, type, charge, 0.0f, 0.0f);
        if (c.miss == MISS_NONE && c.quality > a.qBest) {
            a.qBest = c.quality; a.tBest = t; a.fore = c.forehand; a.overhead = c.overhead; a.timingValid = true;
        }
        // 立つ場所：間に合って、打ちやすい高さになる点（AI と同じ考え方）
        if (i % 2 != 0) continue;
        bool high = b.pos.y > OVERHEAD_MIN_H;
        if (b.bounces != 1 && !high) continue;
        float idealH = high ? OVERHEAD_IDEAL_H : SHOTS[type].idealHeight;
        Vector3 cand[2];
        int nc;
        if (high) { cand[0] = (Vector3){ b.pos.x - OVERHEAD_SIDE, 0.0f, b.pos.z + OVERHEAD_FWD }; nc = 1; }
        else {
            cand[0] = (Vector3){ b.pos.x - IDEAL_SIDE, 0.0f, b.pos.z + IDEAL_FWD };
            cand[1] = (Vector3){ b.pos.x + IDEAL_SIDE, 0.0f, b.pos.z + IDEAL_FWD };
            nc = 2;
        }
        for (int k = 0; k < nc; k++) {
            Vector3 to = Vector3Subtract(cand[k], player);
            float d = Vector3Length(to);
            float v0 = (d > 0.01f) ? Vector3DotProduct(playerVel, Vector3Scale(to, 1.0f / d)) : 0.0f;
            if (TravelTime(d, v0, PLAYER_SPEED, PLAYER_ACCEL, NO_BRAKE) > t) continue;   // 滑り込めば間に合う
            float cost = fabsf(b.pos.y - idealH) + 0.4f * fmaxf(b.pos.z - (HALF_LEN + 2.0f), 0.0f) + 0.05f * d;
            if (cost < bestCost) { bestCost = cost; a.stand = cand[k]; a.ballAt = b.pos; a.standValid = true; }
        }
    }
    return a;
}
