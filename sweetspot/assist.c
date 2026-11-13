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

// ================= 球ごとの予定（どこで弾み、どこに立てば打ちやすいか） =================
// 相手が打った瞬間（か、溜める球種を変えたとき）に一度だけ計算する。
// 今の位置から間に合うかで候補を消さず、間に合わなさは「遅れ」として点を引くだけにする。
// そうすれば、出遅れても目印が消えず、行くべき場所が出続ける。
AssistPlan PlanAssist(Vector3 player, Vector3 playerVel, const Ball *ball, ShotType type)
{
    AssistPlan p = { 0 };
    if (!ball->active || ball->lastHitter != HITTER_OPP) return p;
    p.valid = true;
    Ball b = *ball;
    float t = 0.0f, bestCost = 1e9f;
    float idealLow = SHOTS[type].idealHeight, tol = SHOTS[type].tolHeight;
    int prevBounces = b.bounces;
    for (int i = 0; i < 960 && b.active && b.bounces < 2; i++) {
        StepBall(&b, PHYS_DT);
        t += PHYS_DT;
        if (b.bounces == 1 && prevBounces == 0) {
            p.bounce = (Vector3){ b.pos.x, 0.0f, b.pos.z };
            p.bounceValid = b.pos.z > 0.0f;
        }
        prevBounces = b.bounces;
        if (b.pos.z > HALF_LEN + 7.0f) break;
        bool hittable = b.pos.z > 0.0f && !b.hitNet && (!b.isServe || b.bounces == 1);
        if (!hittable) continue;
        if (b.bounces == 1 && i % 8 == 0 && p.nPath < ASSIST_PATH_MAX) {
            p.path[p.nPath] = b.pos;
            p.good[p.nPath] = fabsf(b.pos.y - idealLow) <= tol * 0.6f;
            p.nPath++;
        }
        if (i % 2 != 0) continue;
        bool high = b.pos.y > OVERHEAD_MIN_H;
        if (b.bounces != 1 && !high) continue;
        float idealH = high ? OVERHEAD_IDEAL_H : idealLow;
        Vector3 cand[2];
        bool fore[2] = { true, false };
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
            float late = fmaxf(TravelTime(d, v0, PLAYER_SPEED, PLAYER_ACCEL, NO_BRAKE) - t, 0.0f);
            float cost = fabsf(b.pos.y - idealH) + 0.4f * fmaxf(b.pos.z - (HALF_LEN + 2.0f), 0.0f) + 0.05f * d
                       + 3.0f * late + (fore[k] ? 0.0f : 0.08f);
            if (cost < bestCost) {
                bestCost = cost; p.stand = cand[k]; p.ballAt = b.pos; p.standValid = true;
                p.fore = fore[k]; p.overhead = high; p.tContact = t;
            }
        }
    }
    return p;
}
