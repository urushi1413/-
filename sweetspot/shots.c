// SWEET SPOT — 打点の評価と打球の作り方、狙いの制限
#include "game.h"

// ---- 打点の評価 ----
// player はプレイヤーの足元の位置。相手コート（-z）を向いて立っている
// charge が大きい（大きく振る）ほど、タイミングの許容幅が狭くなる
// moveToward: ボールの方向へ走っている速さ（m/s）。速ければ飛びつける
// reachBonus: スライド中などで伸びるリーチ（m）
// 打ちやすい高さからどれだけ外れているか。理想より下はそのまま、上は highBand の幅までは 0。
// 上から叩けるのは、浮いた遅い球だけ。速くて重い高い球（キックサーブ、深いトップスピン）は肩の上だと押される
//   来た球 50km/h 以下で幅いっぱい、90km/h 以上で幅なし
float HeightErr(const ShotSpec *sp, float y, float inKmh)
{
    if (y < sp->idealHeight) return sp->idealHeight - y;
    float band = sp->highBand * Clamp01((90.0f - inKmh) / 40.0f);
    return fmaxf(y - sp->idealHeight - band, 0.0f);
}

// 遅い球は膝を曲げて合わせる時間があるので、高さの許容幅が広い（50km/h 以下で 1.6 倍、90km/h 以上で 1 倍）
float HeightTolScale(float inKmh)
{
    return 1.0f + 0.6f * Clamp01((90.0f - inKmh) / 40.0f);
}

Contact EvaluateContact(Vector3 player, const Ball *ball, ShotType type, float charge, float moveToward, float reachBonus)
{
    Contact c = { 0 };
    Vector3 bp = ball->pos;
    float dx = bp.x - player.x;          // 右（フォア側）がプラス
    float dz = player.z - bp.z;          // 前がプラス
    float r  = sqrtf(dx * dx + dz * dz);
    float ang = atan2f(dz, fabsf(dx)) * RAD2DEG;
    c.forehand = dx >= 0.0f;
    c.side     = r;
    c.angleDeg = ang;
    c.height   = bp.y;
    c.fwdErr   = dz - IDEAL_FWD;         // プラス＝ボールがまだ前にある＝振るのが早い
    c.volley   = ball->bounces == 0;
    c.inKmh    = Vector3Length(ball->vel) * 3.6f;
    c.easeKmh  = ball->isServe ? 999.0f : c.inKmh;
    c.vyBall   = ball->vel.y;
    c.timeErrMs = c.fwdErr / fmaxf(Vector3Length(ball->vel), 1.0f) * 1000.0f;
    c.overhead = bp.y > OVERHEAD_MIN_H;

    // 届くか
    if (bp.y > OVERHEAD_MAX_H) { c.miss = MISS_HIGH; return c; }
    if (bp.y < 0.06f)          { c.miss = MISS_LOW;  return c; }
    if (gCasual) reachBonus += CASUAL_REACH;
    reachBonus += SHOTS[type].reach;   // 球種ごとのリーチ（スライスは腕を伸ばして届く）
    if (!c.overhead && ang < (gCasual ? -80.0f : BEHIND_LIMIT)) { c.miss = MISS_LATE; return c; }
    float reach = c.overhead ? 1.6f + (gCasual ? CASUAL_REACH : 0.0f) : REACH_SIDE + reachBonus;
    if (r > reach) {
        bool canDive = !c.overhead && bp.y < 1.6f && r <= REACH_DIVE + reachBonus && moveToward >= DIVE_MIN_SPEED;
        if (!canDive) { c.miss = (ang > 55.0f) ? MISS_EARLY : (ang < -20.0f ? MISS_LATE : MISS_FAR); return c; }
        c.dive = true;
    }

    const ShotSpec *sp = &SHOTS[type];
    float tk = gCasual ? CASUAL_TOL : 1.0f, hk = gCasual ? CASUAL_TOL_H : 1.0f;
    if (c.overhead) {
        // 頭上：高さがタイミング（落ちてくる球をどこで叩くか）、体の前右の点からの距離が位置
        float ox = fabsf(dx) - OVERHEAD_SIDE, oz = dz - OVERHEAD_FWD;
        float otol = bp.y < OVERHEAD_IDEAL_H ? OVERHEAD_TOL_H_LOW : OVERHEAD_TOL_H;
        c.qTiming = Clamp(1.0f - fabsf(bp.y - OVERHEAD_IDEAL_H) / (otol * hk), 0.10f, 1.0f);
        c.qDist   = Clamp(1.0f - sqrtf(ox * ox + oz * oz) / (OVERHEAD_TOL_R * tk), 0.10f, 1.0f);
        c.qHeight = 1.0f;
    } else {
        float idealAng = c.volley ? VOLLEY_ANGLE : IDEAL_ANGLE;
        float tol = Lerp(TOL_ANG_COMPACT, TOL_ANG_FULL, charge) * (c.volley ? VOLLEY_TOL_BONUS : 1.0f) * tk * sp->tolTiming;
        c.qTiming = Clamp(1.0f - fabsf(ang - idealAng) / tol, 0.10f, 1.0f);
        c.qDist   = Clamp(1.0f - fabsf(fminf(r, REACH_SIDE) - IDEAL_R) / ((sp->tolSide + reachBonus) * tk), 0.10f, 1.0f);   // 届けば最低限は当たる
        c.qHeight = Clamp(1.0f - HeightErr(sp, bp.y, c.easeKmh) / (sp->tolHeight * hk * HeightTolScale(c.easeKmh)), 0.10f, 1.0f);
    }
    c.quality = c.qTiming * c.qDist * c.qHeight;
    if (c.dive) c.quality *= DIVE_QUALITY;
    if (gCasual) c.quality = CASUAL_QFLOOR + (1.0f - CASUAL_QFLOOR) * c.quality;
    return c;
}

// 打点の質から実際の打球を作る
//   質が悪い → 球速と回転が落ちる、狙いが散らばる
//   タイミングのずれ → 早いと引っ張り、遅いと流れる（フォアとバックで逆）
Ball PlayerShot(const Contact *c, Vector3 ballPos, Vector3 target, ShotType type, float charge,
                       float *outKmh, float *outRpm, float *outAngle)
{
    // 頭上の球は J/L ならスマッシュ（フラット）、K ならスライスで合わせる
    if (c->overhead && type < SHOT_NORMAL_COUNT && type != SHOT_SLICE) type = SHOT_FLAT;
    const ShotSpec *sp = &SHOTS[type];
    // 上から叩ける球（自分より高い、遅い球）は、多少当たりが悪くても威力が出て、ぶれにくい
    //   1.1m から上で効き始め、1.7m で最大。来た球が 50km/h 以下で全部、90km/h 以上で効かない
    float sit = Clamp01((c->height - SIT_H0) / (SIT_H1 - SIT_H0)) * Clamp01((90.0f - c->easeKmh) / 40.0f);
    float q = c->quality + (1.0f - c->quality) * SIT_BONUS * sit;

    // 球速 = 来た球の跳ね返り + 自分のスイング
    //   芯を外すと（質が低いと）どちらも落ちる。ボレーは自分のスイングがほとんど乗らない
    float specKmh   = Lerp(sp->minKmh, sp->maxKmh, charge);
    float racketKmh = fmaxf(specKmh - RACKET_EA * REF_IN_KMH, 0.0f) / (1.0f + RACKET_EA);
    if (c->volley && !c->overhead) racketKmh *= VOLLEY_POWER;   // スマッシュはノーバウンドでもフルスイング
    float step = Clamp(c->stepIn / STEP_FULL, -1.0f, 1.0f);
    racketKmh += fmaxf(step, 0.0f) * STEP_POWER_KMH;
    float paceKmh  = RACKET_EA * c->inKmh * (0.60f + 0.40f * q);
    float swingKmh = (1.0f + RACKET_EA) * racketKmh * (0.65f + 0.35f * q);
    float kmh = paceKmh + swingKmh;
    float errMul = (2.0f * paceKmh + swingKmh) / fmaxf(kmh, 1.0f)
                 * (1.0f + (1.0f - q) * fmaxf(c->inKmh - TWIST_FREE_KMH, 0.0f) / TWIST_KMH)
                 * (1.0f - STEP_STABILITY * step)
                 * (gCasual ? CASUAL_ERR : 1.0f);
    float rpm = Lerp(sp->minRpm, sp->maxRpm, charge) * (0.60f + 0.40f * q);

    // 狙いをずらす：横方向（タイミング＋ばらつき）
    float hand = c->forehand ? 1.0f : -1.0f;
    float over = fmaxf(fabsf(c->timeErrMs) - TIMING_PULL_DEAD_MS, 0.0f) * (c->timeErrMs >= 0.0f ? 1.0f : -1.0f);
    float yaw = hand * over * TIMING_PULL_DEG_MS * DEG2RAD * (gCasual ? 0.5f : 1.0f)
              + RandNormal() * (ERR_YAW_BASE + (1.0f - q) * (1.0f - q) * ERR_YAW_Q) * errMul * DEG2RAD;
    yaw = Clamp(yaw, -30.0f * DEG2RAD, 30.0f * DEG2RAD);

    Vector3 d = { target.x - ballPos.x, 0.0f, target.z - ballPos.z };
    Vector3 dirFlat = Vector3RotateByAxisAngle(Vector3Normalize(d), (Vector3){ 0, 1, 0 }, yaw);
    float dist = Vector3Length(d);
    // タッチショットは遅いので角度のずれが効きにくい。代わりに距離感をずらす（ドロップが浮く・ロブが浅い）
    // 跳ね返りの成分は距離感をコントロールしにくい（ブロックリターンやボレーは深さがばらつく）
    dist *= 1.0f + RandNormal() * (paceKmh / fmaxf(kmh, 1.0f)) * (0.05f + (1.0f - q) * 0.25f);
    // 速い球ほど勢いを殺すのが難しい（100km/h の球だと、ずれが約 2 倍）
    if (type >= SHOT_NORMAL_COUNT) {
        float paceDifficulty = 1.0f + c->inKmh / 100.0f;
        dist *= 1.0f + RandNormal() * (0.04f + (1.0f - q) * 0.20f) * paceDifficulty;
    }
    float ang;
    if (sp->touchAngle > 0.0f) {
        // ドロップ：決まった角度で、届くぎりぎりの速さで運ぶ
        ang = sp->touchAngle * DEG2RAD;
        kmh = SolveSpeed(ballPos, dirFlat, dist, ang, rpm) * 3.6f;
        ang += RandNormal() * (1.0f + (1.0f - q) * 5.0f) * errMul * DEG2RAD;
    } else {
        ang = SolveLaunchAngle(ballPos, dirFlat, dist, kmh / 3.6f, rpm, sp->highArc);
        // 縦方向のばらつき（ネットやアウトの原因）。完璧な打点でもゼロにはならない
        ang += RandNormal() * (ERR_PITCH_BASE + (1.0f - q) * (1.0f - q) * ERR_PITCH_Q) * errMul * DEG2RAD;
    }

    if (outKmh) *outKmh = kmh;
    if (outRpm) *outRpm = rpm;
    if (outAngle) *outAngle = ang;
    return MakeShot(ballPos, dirFlat, kmh / 3.6f, ang, rpm);
}

// 実際に狙う場所：球種と立ち位置で、狙える深さを制限する
//   普通のショットはネットから 6m より短くは狙えない（ネットに近いほど短く：ネット前なら 2.5m）
//   タッチショットは自由。簡単モードは左右を少し内側に
Vector3 EffectiveAim(ShotType t, float playerZ, float aimX, float aimDepth, PlayStyle style)
{
    float lim = style == STYLE_CASUAL ? 2.8f : 3.7f;
    float x = Clamp(aimX, -lim, lim);
    float z = aimDepth;
    if (t < SHOT_NORMAL_COUNT) {
        float shrt = SHOTS[t].shortDepth;
        if (playerZ < SERVICE_Z + 1.0f) shrt = fmaxf(shrt, -Lerp(2.5f, 4.0f, Clamp01((playerZ - 1.0f) / SERVICE_Z)));
        z = fminf(z, shrt);
        z = fmaxf(z, -(HALF_LEN - 0.6f));
    }
    return (Vector3){ x, 0.0f, z };
}

// 押す長さで深さを決める：0 で球種の一番浅いところ、1 で一番深いところ、それより長いとさらに深く（アウト）
//   左右はマウスの印。ネットの近くから打つときは、もっと浅く（角度をつけて）打てる
Vector3 AimFromHold(ShotType t, float playerZ, float aimX, float hold, PlayStyle style)
{
    float shrt = SHOTS[t].shortDepth;
    if (t < SHOT_NORMAL_COUNT) shrt = EffectiveAim(t, playerZ, aimX, 0.0f, style).z;   // 一番浅いところ
    float z = shrt + (SHOTS[t].deepDepth + HOLD_DEEP_MARGIN - shrt) * Clamp01(hold);
    Vector3 a = EffectiveAim(t, playerZ, aimX, -1.0f, style);                           // 左右の制限だけ使う
    return (Vector3){ a.x, 0.0f, z };
}

// サーブの深さ：トスを上げてから離すまでの時間。早いと浅く、遅いと深く（遅すぎるとロング）
float ServeDepthFromTime(float t)
{
    float u = (t - SERVE_DEPTH_T0) / (SERVE_DEPTH_T1 - SERVE_DEPTH_T0);
    return SERVE_DEPTH_SHORT + (SERVE_DEPTH_LONG - SERVE_DEPTH_SHORT) * Clamp(u, 0.0f, 1.2f);
}
