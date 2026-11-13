// SWEET SPOT — 相手（AI）と、走る計算（加速・減速）
#include "game.h"

// ================= 相手（AI） =================
// AI は自分（プレイヤー）とまったく同じ物理・同じ打点の評価・同じショットの関数を使う。
// コートを縦軸まわりに 180° 回した「AI から見た座標」に変換して、プレイヤー用の関数をそのまま呼ぶ。
// 180° 回転なので、位置・速度・回転（角速度）すべて (x, y, z) → (-x, y, -z) で変換できる
Vector3 Rot180(Vector3 v) { return (Vector3){ -v.x, v.y, -v.z }; }

Ball RotBall(Ball b)
{
    b.pos = Rot180(b.pos);
    b.vel = Rot180(b.vel);
    b.spin = Rot180(b.spin);
    b.side = -b.side;
    float x0 = b.boxX0, x1 = b.boxX1;
    b.boxX0 = -x1; b.boxX1 = -x0;
    return b;
}

const AiLevel AI_LEVELS[3] = {
    //          走る  反応   タイミング 位置   強気   見極め  1st     2nd     サーブの安定感
    { "EASY",   5.0f, 0.35f, 30.0f, 0.35f, 0.25f, 0.40f, 155.0f, 105.0f, 0.56f },
    { "NORMAL", 5.6f, 0.28f, 20.0f, 0.20f, 0.50f, 0.20f, 175.0f, 120.0f, 0.70f },
    { "PRO",    6.2f, 0.22f, 12.0f, 0.10f, 0.75f, 0.08f, 200.0f, 130.0f, 0.84f },
};

// 速さ v0（目的地方向の成分）から距離 d を走って止まるまでの時間
// 加速 a で最高速 vmax まで上げ、最後は減速 b で止まる（台形の速度変化）
float TravelTime(float d, float v0, float vmax, float a, float b)
{
    v0 = Clamp(v0, 0.0f, vmax);
    float dAcc = (vmax * vmax - v0 * v0) / (2.0f * a);
    float dBrk = vmax * vmax / (2.0f * b);
    if (d >= dAcc + dBrk) return (vmax - v0) / a + (d - dAcc - dBrk) / vmax + vmax / b;
    // 最高速まで上がらない：ピークの速さ vp を求める
    float vp = sqrtf((d + v0 * v0 / (2.0f * a)) / (1.0f / (2.0f * a) + 1.0f / (2.0f * b)));
    if (vp < v0) return 2.0f * d / fmaxf(v0, 0.1f);   // すでに速すぎる：すぐ減速
    return (vp - v0) / a + vp / b;
}

// 速度を「行きたい速度」に近づける。速くするときは a、遅くするときは b の加速度で
Vector3 Accelerate(Vector3 vel, Vector3 want, float a, float b, float dt)
{
    Vector3 dv = Vector3Subtract(want, vel);
    float len = Vector3Length(dv);
    float lim = (Vector3Length(want) < Vector3Length(vel) - 0.01f) ? b : a;
    float maxDv = lim * dt;
    if (len > maxDv) dv = Vector3Scale(dv, maxDv / len);
    return Vector3Add(vel, dv);
}

// 目的地に向かって走る（止まれる速さに落としながら）
Vector3 MoveToward(Vector3 *pos, Vector3 vel, Vector3 target, float vmax, float dt)
{
    Vector3 d = Vector3Subtract(target, *pos);
    d.y = 0.0f;
    float dist = Vector3Length(d);
    Vector3 want = { 0 };
    if (dist > 0.02f) {
        float spd = fminf(vmax, sqrtf(2.0f * PLAYER_BRAKE * dist));
        want = Vector3Scale(d, spd / dist);
    }
    vel = Accelerate(vel, want, PLAYER_ACCEL, PLAYER_BRAKE, dt);
    *pos = Vector3Add(*pos, Vector3Scale(vel, dt));
    return vel;
}

// 自分側を AI に任せる（オートパイロット）ための変換：コートを 180° 回して、打者の名前を入れ替える
Ball FlipSides(Ball b)
{
    b = RotBall(b);
    if (b.lastHitter == HITTER_PLAYER) b.lastHitter = HITTER_OPP;
    else if (b.lastHitter == HITTER_OPP) b.lastHitter = HITTER_PLAYER;
    return b;
}

// AI のホームポジション（ワールド座標）
// ネットでの構え（ワールド座標）：自分が打ったコースの側に寄って、ネットから 2.6m
Vector3 AiNetPos(const Opponent *ai)
{
    return Rot180((Vector3){ Clamp(ai->lastTargetX * 0.45f, -2.2f, 2.2f), 0.0f, 2.6f });
}

Vector3 AiHome(void) { return (Vector3){ 0.0f, 0.0f, -(HALF_LEN + 0.8f) }; }

// リターンの構え位置（自分側の座標。AI は Rot180 して使う）。ボックスの外寄り、ベースラインの 1.3m 後ろ
// ファーストは下がって時間を作り（ベースラインの 1.8m 後ろ）、セカンドは前で待つ（0.3m 後ろ）
Vector3 ReturnPos(bool deuce, bool second)
{
    return (Vector3){ deuce ? 2.3f : -2.3f, 0.0f, HALF_LEN + (second ? 0.3f : 1.8f) };
}

// 飛んでくる球を予測して、どこで打つかを決める（AI 座標で考える）
void AiPlan(Opponent *ai, const Ball *worldBall)
{
    const AiLevel *lv = &AI_LEVELS[ai->level];
    Ball b = RotBall(*worldBall);
    Vector3 me = Rot180(ai->pos);

    // 1) 1バウンド目の判定を予測：アウトなら見送る（際どい球は見極めを間違えることがある）
    Ball p = b;
    for (int i = 0; i < 240 * 4 && p.active && p.bounces == 0; i++) StepBall(&p, PHYS_DT);
    if (p.result == RESULT_OUT || p.result == RESULT_NET) {
        float overX = fabsf(p.pos.x) - SINGLES_W * 0.5f;
        float overZ = fabsf(p.pos.z) - (p.isServe ? SERVICE_Z : HALF_LEN);
        float over = fmaxf(overX, overZ);
        bool misjudge = p.result == RESULT_OUT && over < lv->lineJudge && RandRange(0, 1) < 0.5f;
        if (!misjudge) {
            ai->leaving = true;
            ai->planned = false;
            ai->standPos = ai->netRush ? ai->pos : AiHome();   // ネットにいれば、そのまま見送る
            return;
        }
    }

    // 2) 軌道の中から、間に合って、打ちやすい点を選ぶ
    //    ベースライン：バウンド後の、高さ 1m 前後
    //    ネット（サービスラインより前）：バウンド前のボレー（早く触るほどいい）、高ければスマッシュ
    bool atNet = me.z < SERVICE_Z + 1.5f;
    Ball q = b;
    float t = 0.0f;
    float bestCost = 1e9f, bestT = -1.0f;
    Vector3 bestStand = me;
    float bestBallX = 0.0f, lateBallX = 0.0f;
    float lateCost = 1e9f, lateT = -1.0f;
    Vector3 lateStand = me;
    for (int i = 0; i < 240 * 4 && q.active && q.bounces < 2; i++) {
        StepBall(&q, PHYS_DT);
        t += PHYS_DT;
        if (i % 2 != 0 || q.pos.z <= 0.0f) continue;
        bool volley = q.bounces == 0;
        if (volley && !(atNet && q.pos.z > 0.3f && q.pos.y > 0.3f && q.pos.y < OVERHEAD_MAX_H)) continue;
        if (!volley && q.bounces != 1) continue;
        Vector3 stand;
        float cost;
        if (volley && q.pos.y > OVERHEAD_MIN_H) {
            // スマッシュ：球の少し後ろ左に入る
            stand = (Vector3){ q.pos.x - OVERHEAD_SIDE, 0.0f, q.pos.z + OVERHEAD_FWD };
            cost = 0.6f * fabsf(q.pos.y - OVERHEAD_IDEAL_H) + 0.25f * t;
        } else if (volley) {
            // ボレー：体の横やや前（40°）で触る。早いほどいい（相手に時間を与えない）
            Vector3 fore = { q.pos.x - 0.72f, 0.0f, q.pos.z + 0.60f };
            Vector3 back = { q.pos.x + 0.72f, 0.0f, q.pos.z + 0.60f };
            stand = (Vector3Distance(fore, me) <= Vector3Distance(back, me)) ? fore : back;
            cost = 0.3f * t + 0.3f * fabsf(q.pos.y - 1.2f);
        } else {
            // フォアかバックか、近い方で構える
            Vector3 fore = { q.pos.x - IDEAL_SIDE, 0.0f, q.pos.z + IDEAL_FWD };
            Vector3 back = { q.pos.x + IDEAL_SIDE, 0.0f, q.pos.z + IDEAL_FWD };
            stand = (Vector3Distance(fore, me) <= Vector3Distance(back, me)) ? fore : back;
            cost = fabsf(q.pos.y - 1.0f) + 0.4f * fmaxf(q.pos.z - (HALF_LEN + 2.0f), 0.0f);
            if (atNet) cost += 0.6f;   // ネットにいるなら、バウンドさせるよりボレーで取りたい
        }
        // 理想の距離で構えるのが無理なら、踏み込んで腕を伸ばせば届く位置（体から 1.75m）でもいい（ただし打点は悪くなる）
        for (int option = 0; option < 2; option++) {
            Vector3 st = stand;
            float extra = 0.0f;
            if (option == 1) {
                float sgn = (q.pos.x - stand.x) >= 0.0f ? 1.0f : -1.0f;   // ボールは体のどちら側か
                Vector3 toMe = Vector3Subtract(me, (Vector3){ q.pos.x, 0.0f, stand.z });
                float side = (fabsf(toMe.x) > 0.01f) ? (toMe.x > 0 ? 1.0f : -1.0f) : -sgn;
                st.x = q.pos.x + side * 1.75f;                       // 自分に近い側で、1.75m 離れて立つ（踏み込んで届く）
                extra = 0.8f;
            }
            Vector3 toStand = Vector3Subtract(st, me);
            float dStand = Vector3Length(toStand);
            float v0 = (dStand > 0.01f) ? Vector3DotProduct(Rot180(ai->vel), Vector3Scale(toStand, 1.0f / dStand)) : 0.0f;
            float need = TravelTime(dStand, v0, lv->speed, PLAYER_ACCEL, NO_BRAKE);   // 滑り込めば止まらなくていい
            if (need <= t) {
                if (cost + extra < bestCost) { bestCost = cost + extra; bestT = t; bestStand = st; bestBallX = q.pos.x; }
            } else if (need - t < lateCost) {
                lateCost = need - t; lateT = t; lateStand = st; lateBallX = q.pos.x;   // 間に合わない：一番惜しいところを目指す
            }
        }
    }
    if (bestT < 0.0f) { bestT = lateT; bestStand = lateStand; bestBallX = lateBallX; }
    ai->planFore = bestBallX >= bestStand.x;
    if (bestT < 0.0f) { ai->planned = false; ai->standPos = AiHome(); return; }

    bestStand.x += RandNormal() * lv->posErr;
    bestStand.z += RandNormal() * lv->posErr;
    ai->standPos = Rot180(bestStand);
    ai->swingAge = worldBall->age + bestT + RandNormal() * lv->timingSigmaMs / 1000.0f;
    ai->planned = true;
    ai->leaving = false;
}

// AI のショット選び（AI 座標）
ShotType AiChooseShot(const Opponent *ai, const Contact *c, Vector3 playerAi, Vector3 meAi, float *charge,
                             Vector3 *target)
{
    const AiLevel *lv = &AI_LEVELS[ai->level];
    ShotType t = SHOT_TOP;
    float r = RandRange(0.0f, 1.0f);
    bool stretched = c->side > IDEAL_R + 0.45f || c->dive;
    bool playerAtNet = playerAi.z > -7.0f;        // プレイヤーが前に出てきている（AI 座標で相手は -z 側）
    bool playerDeep  = playerAi.z < -(HALF_LEN + 2.0f);

    if (c->height < 0.55f || (stretched && r < 0.7f)) t = SHOT_SLICE;          // 低い・遠い → つなぐ
    else if (c->height > 1.05f && c->quality > 0.55f && r < lv->aggression) t = SHOT_FLAT;   // 高くていい打点 → 叩く
    if (playerAtNet && (stretched || r < 0.35f)) t = SHOT_TOP_LOB;              // 前に来たらロブ
    if (c->overhead) t = SHOT_FLAT;                                             // 頭上はスマッシュ

    // ネットでのボレー・スマッシュ：相手のいない方へ。相手が下がっていれば短いアングルかドロップボレー
    if (c->volley) {
        bool oppBack = playerAi.z < -(HALF_LEN - 1.0f);   // 相手がベースライン付近まで下がっている
        float side = (playerAi.x > 0.3f) ? -1.0f : (playerAi.x < -0.3f ? 1.0f : ((rand() % 2) ? 1.0f : -1.0f));
        if (c->overhead) {
            t = SHOT_FLAT;
            *charge = 0.7f + 0.3f * lv->aggression;
            *target = (Vector3){ side * (2.6f + 0.8f * lv->aggression), 0.0f, -8.5f };
        } else if (oppBack && r < 0.25f * lv->aggression) {
            t = SHOT_DROP;
            *charge = 0.6f;
            *target = (Vector3){ side * 2.5f, 0.0f, -2.5f };
        } else {
            t = (c->height > 1.2f && c->quality > 0.5f) ? SHOT_FLAT : SHOT_SLICE;   // 高ければ叩く、低ければスライスで運ぶ
            *charge = RandRange(0.35f, 0.6f);
            bool angle = oppBack && r < 0.5f;
            *target = (Vector3){ side * (angle ? 3.4f : 2.4f + 1.0f * lv->aggression), 0.0f, angle ? -5.5f : -9.5f };
        }
        return t;
    }
    if (playerDeep && meAi.z < HALF_LEN - 1.0f && r < 0.15f * lv->aggression) t = SHOT_DROP;

    // 球速の目安（打ち出し）。プロのラリーはフラット 130〜150、トップ 110〜135、スライス 80〜100km/h くらい
    float wantKmh;
    switch (t) {
        case SHOT_FLAT:  wantKmh = 115.0f + 35.0f * lv->aggression; break;
        case SHOT_TOP:   wantKmh = 100.0f + 35.0f * lv->aggression; break;
        case SHOT_SLICE: wantKmh =  75.0f + 20.0f * lv->aggression; break;
        default:         wantKmh = Lerp(SHOTS[t].minKmh, SHOTS[t].maxKmh, 0.5f); break;
    }
    wantKmh += RandNormal() * 8.0f - (stretched ? 20.0f : 0.0f);
    const ShotSpec *ss = &SHOTS[t];
    *charge = (ss->maxKmh > ss->minKmh) ? Clamp01((wantKmh - ss->minKmh) / (ss->maxKmh - ss->minKmh)) : 0.5f;

    // 甘い球（いい打点で、ベースラインより内側）は決めにいく：速く、厳しく
    bool attack = c->quality > 0.6f && meAi.z < HALF_LEN - 0.3f && (t == SHOT_TOP || t == SHOT_FLAT) &&
                  RandRange(0, 1) < 0.3f + 0.6f * lv->aggression;
    if (attack) {
        const ShotSpec *as = &SHOTS[t];
        *charge = Clamp01(*charge + 15.0f / (as->maxKmh - as->minKmh));
    }

    // コース：プレイヤーのいない方へ。強気なほどサイドラインに近い
    const ShotSpec *sp = &SHOTS[t];
    float wide = attack ? 3.2f : 1.5f + 1.3f * lv->aggression;   // サイドラインまで 4.1m。普段は 1.5m 以上余裕を残す
    float x;
    if (playerAi.x > 0.6f)       x = -wide;
    else if (playerAi.x < -0.6f) x =  wide;
    else                         x = (rand() % 2) ? wide : -wide;
    if (r > 0.85f) x = -x * 0.5f;   // たまに逆を突く
    float depth = sp->depth;
    if (t <= SHOT_SLICE && RandRange(0, 1) < 0.15f * lv->aggression) depth = sp->shortDepth;   // 短いアングル
    *target = (Vector3){ x, 0.0f, depth };
    return t;
}

// AI が振る。当たったら true（ball は AI の打球に置き換わる）
bool AiSwing(Opponent *ai, Ball *ball, Vector3 player)
{
    const AiLevel *lv = &AI_LEVELS[ai->level];
    ai->swung = true;
    ai->planned = false;
    Ball b = RotBall(*ball);
    Vector3 me = Rot180(ai->pos);
    bool hittable = b.active && b.lastHitter == HITTER_PLAYER && b.pos.z > 0.0f && !b.hitNet &&
                    b.bounces <= 1 && (!b.isServe || b.bounces == 1);
    if (!hittable) return false;

    float charge; Vector3 target;
    Vector3 meVel = Rot180(ai->vel);
    Vector3 toBall = { b.pos.x - me.x, 0.0f, b.pos.z - me.z };
    float moveToward = (Vector3Length(toBall) > 0.01f) ? Vector3DotProduct(meVel, Vector3Normalize(toBall)) : 0.0f;
    float bonus = ai->sliding ? SLIDE_REACH : 0.0f;
    Contact c = EvaluateContact(me, &b, SHOT_TOP, 0.5f, moveToward, bonus);
    ShotType t = AiChooseShot(ai, &c, Rot180(player), me, &charge, &target);
    ai->anim.slice = IsSliceShot(t);
    c = EvaluateContact(me, &b, t, charge, moveToward, bonus);
    c.stepIn = -meVel.z;
    ai->anim.lastContact = c;
    ai->anim.contactLocal = (Vector3){ b.pos.x - me.x, b.pos.y, me.z - b.pos.z };
    ai->anim.swingT = 0.0f;
    if (c.miss != MISS_NONE) return false;
    if (c.dive) { ai->downTimer = DIVE_DOWN_TIME; ai->anim.downT = DIVE_DOWN_TIME; }

    float kmh;
    Ball out = PlayerShot(&c, b.pos, target, t, charge, &kmh, NULL, NULL);
    ai->lastTargetX = target.x;
    // 前に出るか：甘い球をベースラインの内側で攻めたら、強気なほど前に出る。ネットにいれば残る。
    // ロブで下がらされたら（サービスラインより 3m 以上後ろ）、ネットはあきらめる
    if (me.z > SERVICE_Z + 3.0f) ai->netRush = false;
    if (!ai->netRush && !c.dive && me.z < HALF_LEN - 2.0f && t <= SHOT_SLICE &&
        RandRange(0.0f, 1.0f) < 0.25f + 0.6f * lv->aggression)
        ai->netRush = true;
    if (me.z < SERVICE_Z + 1.5f) ai->netRush = true;
    out.lastHitter = HITTER_OPP;
    *ball = RotBall(out);
    ai->lastType = t;
    ai->lastQ = c.quality;
    ai->lastKmh = kmh;
    ai->lastServe = false;
    return true;
}

// AI のサーブ。deuce: デュースサイドから打つ。first: ファーストサーブ
Ball AiServe(Opponent *ai, bool deuce, bool first)
{
    const AiLevel *lv = &AI_LEVELS[ai->level];
    // AI 座標では自分と同じ：デュースサイドは x>0、対角のボックスは x<0
    Vector3 me = { deuce ? 0.6f : -0.6f, 0.0f, HALF_LEN + 0.15f };
    ai->pos = Rot180(me);
    // トスのぶれとタイミングのずれから、打点の高さと質を決める
    Vector2 off = { RandNormal() * TOSS_SPREAD, RandNormal() * TOSS_SPREAD };
    float hErr = fabsf(RandNormal() * lv->timingSigmaMs / 1000.0f * 2.4f);   // 落ちてくる速さ 2.4m/s 前後
    float q = Clamp01(1.0f - hErr / SERVE_TOL_H) * Clamp01(1.0f - Vector2Length(off) / TOSS_TOL);
    // 強さごとの安定感（低いほどぶれて、フォルトが増える）。セカンドは安全に入れにいくので、ぶれは小さい
    q *= first ? lv->serveSkill : lv->serveSkill + (1.0f - lv->serveSkill) * 0.65f;
    Vector3 contact = { me.x + 0.25f + off.x, SERVE_IDEAL_H - hErr, me.z - 0.35f + off.y };

    ServeType t;
    float charge;
    // 速さは強さごとの目安から逆算する（打点の質で落ちる分も見込む）
    float wantKmh;
    if (first) { t = (rand() % 2) ? SERVE_FLAT : SERVE_SLICE; wantKmh = lv->serve1Kmh + RandNormal() * 6.0f; }
    else       { t = SERVE_KICK;                              wantKmh = lv->serve2Kmh + RandNormal() * 5.0f; }
    const ServeSpec *ssp = &SERVES[t];
    charge = (wantKmh / (0.80f + 0.20f * q) - ssp->minKmh) / (ssp->maxKmh - ssp->minKmh);
    float x0, x1;
    ServiceBox(me.x, -1, &x0, &x1);
    const float course[3] = { x1 - 0.35f, x0 + 0.35f, 0.5f * (x0 + x1) };   // T / ワイド / ボディ（デュース側）
    int ci = rand() % 3;
    float tx = course[ci];
    if (!deuce) tx = (ci == 0) ? x0 + 0.35f : (ci == 1 ? x1 - 0.35f : 0.5f * (x0 + x1));
    Vector3 target = { tx, 0.0f, -5.7f };
    float kmh;
    Ball b = HitServe(contact, target, t, Clamp01(charge), q, x0, x1, &kmh);
    b.lastHitter = HITTER_OPP;
    // 見た目：頭上で打つ
    ai->anim.lastContact = (Contact){ .overhead = true, .forehand = true, .quality = q };
    ai->anim.contactLocal = (Vector3){ contact.x - me.x, contact.y, me.z - contact.z };
    ai->anim.swingT = 0.0f;
    ai->lastKmh = kmh;
    ai->lastServe = true;
    ai->planned = false;
    ai->lastTargetX = target.x;
    // サーブ＆ボレー：強気な AI がたまに。芝では多め
    ai->netRush = first && RandRange(0.0f, 1.0f) < 0.15f * lv->aggression + (gSurface == SURF_GRASS ? 0.25f * lv->aggression : 0.0f);
    return RotBall(b);
}

// 毎フレーム：反応して、計画を立て、走る
void AiUpdate(Opponent *ai, const Ball *ball, float dt)
{
    const AiLevel *lv = &AI_LEVELS[ai->level];
    if (ball->lastHitter != ai->watching) {
        ai->watching = ball->lastHitter;
        ai->planned = false;
        ai->leaving = false;
        ai->swung = false;
        // サーブはトスとフォームで読める（スプリットステップ）ので、反応は半分
        if (ball->lastHitter == HITTER_PLAYER) ai->seeTimer = ball->isServe ? lv->reaction * 0.5f : lv->reaction;
        else ai->standPos = ai->netRush ? AiNetPos(ai) : AiHome();   // 自分が打った後：ネットに詰めるか、戻る
    }
    // 見た目のタイマー
    if (ai->anim.swingT >= 0.0f) ai->anim.swingT = fminf(ai->anim.swingT + dt, 5.0f);
    if (ai->anim.downT > 0.0f) ai->anim.downT -= dt;
    ai->anim.runPhase += Vector3Length(ai->vel) * dt * 2.2f;
    // 飛びついて倒れている間は動けない
    if (ai->downTimer > 0.0f) {
        ai->downTimer -= dt;
        ai->vel = (Vector3){ 0 };
        ai->planned = false;
        return;
    }
    if (ball->lastHitter == HITTER_PLAYER && !ai->planned && !ai->leaving && !ai->swung && ball->active) {
        ai->seeTimer -= dt;
        if (ai->seeTimer <= 0.0f) AiPlan(ai, ball);
    }
    // 走る（加速・減速あり）。止まっていては間に合わないときは、全速で走って滑り込む
    if (ai->sliding) {
        float v = Vector3Length(ai->vel);
        float nv = v - SLIDE_DECEL * dt;
        if (nv < SLIDE_END_SPEED) { ai->sliding = false; ai->vel = Vector3Scale(ai->vel, fmaxf(nv, 0.0f) / fmaxf(v, 1e-4f)); }
        else ai->vel = Vector3Scale(ai->vel, nv / v);
        ai->pos = Vector3Add(ai->pos, Vector3Scale(ai->vel, dt));
    } else {
        bool slideIn = false;
        Vector3 d = Vector3Subtract(ai->standPos, ai->pos); d.y = 0.0f;
        float dist = Vector3Length(d);
        if (ai->planned && dist > 0.05f) {
            float v0 = Vector3DotProduct(ai->vel, Vector3Scale(d, 1.0f / dist));
            float tLeft = ai->swingAge - ball->age;
            slideIn = TravelTime(dist, v0, lv->speed, PLAYER_ACCEL, PLAYER_BRAKE) > tLeft;
        }
        if (slideIn) {
            // 全速で目的地へ。着いたら滑り始める
            Vector3 want = Vector3Scale(d, lv->speed / dist);
            ai->vel = Accelerate(ai->vel, want, PLAYER_ACCEL, PLAYER_BRAKE, dt);
            Vector3 step = Vector3Scale(ai->vel, dt);
            if (Vector3Length(step) >= dist && Vector3Length(ai->vel) > SLIDE_MIN_SPEED) ai->sliding = true;
            ai->pos = Vector3Add(ai->pos, step);
        } else {
            ai->vel = MoveToward(&ai->pos, ai->vel, ai->standPos, lv->speed, dt);
        }
    }
    ai->anim.sliding = ai->sliding;
    if (Vector3Length(ai->vel) > 0.3f) ai->anim.slideSide = -ai->vel.x;   // AI は +z 向きなので右＝-x
    // 打つ 0.6 秒前から構える（見た目）
    if (ai->planned) {
        ai->anim.windup = Clamp01(1.0f - (ai->swingAge - ball->age) / 0.6f);
        ai->anim.windStroke = ai->planFore ? 0 : 1;
        ai->anim.slice = false;
    } else {
        ai->anim.windup = 0.0f;
    }
}
