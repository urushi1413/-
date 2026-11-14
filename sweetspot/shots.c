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
    c.forehand = gLefty ? (dx <= 0.0f) : (dx >= 0.0f);   // 左利きは体の左がフォア
    c.side     = r;
    c.angleDeg = ang;
    c.height   = bp.y;
    c.fwdErr   = dz - IDEAL_FWD;         // プラス＝ボールがまだ前にある＝振るのが早い
    c.volley   = ball->bounces == 0;
    c.inKmh    = Vector3Length(ball->vel) * 3.6f;
    c.easeKmh  = ball->isServe ? 999.0f : c.inKmh;
    c.vyBall   = ball->vel.y;
    // 上から叩ける球：1.1m から上で効き始め、1.7m で最大。来た球が 50km/h 以下で全部、90km/h 以上で効かない
    c.sit      = Clamp01((bp.y - SIT_H0) / (SIT_H1 - SIT_H0)) * Clamp01((90.0f - c.easeKmh) / 40.0f);
    c.timeErrMs = c.fwdErr / fmaxf(Vector3Length(ball->vel), 1.0f) * 1000.0f;
    c.overhead = bp.y > OVERHEAD_MIN_H;

    // 届くか
    if (bp.y > OVERHEAD_MAX_H) { c.miss = MISS_HIGH; return c; }
    if (bp.y < 0.06f)          { c.miss = MISS_LOW;  return c; }
    if (gCasual) reachBonus += CASUAL_REACH;
    c.isReturn = ball->isServe;
    float qualityReach = reachBonus + (ball->isServe ? 0.5f * RETURN_REACH : 0.0f);   // 当たりの質（距離）に効く分：伸びた分の半分は合わせられる
    if (c.isReturn) reachBonus += RETURN_REACH;   // リターンは跳んで伸びて取る（遠いサーブでも届く）。届くだけで、伸びた分だけ当たりは悪い
    reachBonus += SHOTS[type].reach;   // 球種ごとのリーチ（スライスは腕を伸ばして届く）
    if (!c.overhead && ang < (gCasual ? -80.0f : BEHIND_LIMIT)) { c.miss = MISS_LATE; return c; }
    float reach = c.overhead ? 1.6f + (gCasual ? CASUAL_REACH : 0.0f) : REACH_SIDE + reachBonus;
    if (r > reach) {
        bool canDive = !c.overhead && bp.y < 1.6f && r <= REACH_DIVE + reachBonus && moveToward >= DIVE_MIN_SPEED;
        // 踏み込み（ランジ）：走っていなくても、球の方へ体ごと倒れ込めばもう少し届く
        float stretchMax = reach + (c.volley ? STRETCH_VOLLEY : STRETCH_GROUND);
        bool canStretch = !c.overhead && bp.y > 0.15f && bp.y < 2.0f && r <= stretchMax;
        if (canDive)         c.dive = true;
        else if (canStretch) c.stretch = true;
        else { c.miss = (ang > 55.0f) ? MISS_EARLY : (ang < -20.0f ? MISS_LATE : MISS_FAR); return c; }
    }

    c.lunge = !c.overhead && r > LUNGE_R;
    const ShotSpec *sp = &SHOTS[type];
    float tk = gCasual ? CASUAL_TOL : 1.0f, hk = gCasual ? CASUAL_TOL_H : 1.0f;
    tk *= 1.0f + SIT_POS_BONUS * c.sit;   // チャンスボールは前後左右に多少ずれても打てる
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
        c.qDist   = Clamp(1.0f - fabsf(fminf(r, REACH_SIDE) - IDEAL_R) / ((sp->tolSide + qualityReach + sp->reach) * tk), 0.10f, 1.0f);   // 届けば最低限は当たる
        c.qHeight = Clamp(1.0f - HeightErr(sp, bp.y + (gCrouch ? CROUCH_LIFT : 0.0f), c.easeKmh) / (sp->tolHeight * hk * HeightTolScale(c.easeKmh)), 0.10f, 1.0f);
    }
    // 3 つの掛け算をそのまま使うと、どれも 8 割の「悪くない当たり」が 5 割になってしまうので、0.6 乗して緩める
    //   リターンは反応で打つので、緩めない
    c.quality = powf(c.qTiming * c.qDist * c.qHeight, c.isReturn ? 1.0f : QUALITY_EXP);
    if (c.dive) c.quality *= DIVE_QUALITY;
    if (c.stretch) c.quality *= STRETCH_QUALITY;
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
    float q = c->quality + (1.0f - c->quality) * SIT_BONUS * c->sit;

    // 球速 = 来た球の跳ね返り + 自分のスイング
    //   芯を外すと（質が低いと）どちらも落ちる。ボレーは自分のスイングがほとんど乗らない
    float specKmh   = Lerp(sp->minKmh, sp->maxKmh, charge);
    float racketKmh = fmaxf(specKmh - RACKET_EA * REF_IN_KMH, 0.0f) / (1.0f + RACKET_EA);
    if (c->volley && !c->overhead) racketKmh *= VOLLEY_POWER;   // スマッシュはノーバウンドでもフルスイング
    if (c->isReturn) racketKmh *= RETURN_SWING;                 // リターンは合わせるだけ：遅くなる分、弾道が高くなって入るが浮く
    float step = Clamp(c->stepIn / STEP_FULL, -1.0f, 1.0f);
    racketKmh += fmaxf(step, 0.0f) * STEP_POWER_KMH;
    // 芯を外すと遅くなる。ただしプロの球は多少外しても速い（長さのぶれは、速い球ほど角度のぶれを小さくして抑えている）
    float paceKmh  = RACKET_EA * c->inKmh * (0.70f + 0.30f * q);
    float swingKmh = (1.0f + RACKET_EA) * racketKmh * (0.75f + 0.25f * q);
    float kmh = paceKmh + swingKmh;
    float errMul = (2.0f * paceKmh + swingKmh) / fmaxf(kmh, 1.0f)
                 * (1.0f + (1.0f - q) * fmaxf(c->inKmh - TWIST_FREE_KMH, 0.0f) / TWIST_KMH)
                 * (1.0f - STEP_STABILITY * step)
                 * (gCasual ? CASUAL_ERR : 1.0f) * gErrMul;
    float rpm = Lerp(sp->minRpm, sp->maxRpm, charge) * (0.60f + 0.40f * q);
    if (rpm > 0.0f) rpm *= gSpinMul;   // 選手ごとのトップスピンの強さ
    if (gWomen) { kmh *= WOMEN_SHOT_K; rpm *= WOMEN_SPIN_K; }   // 女子は少し遅く、回転も少ない
    // 前（ベースラインより内側）で打つストローク：速さはそのままで、ベースラインと同じくらい入るようにする。
    //   前ほどトップスピンを強くかけて（コートに落とす）、上下のぶれも小さくする。前に出た球がチャンスになるように
    float fwd = (!c->volley && !c->overhead && !c->isReturn && sp->touchAngle <= 0.0f)
              ? Clamp01((FWD_ASSIST_Z0 - fabsf(ballPos.z)) / (FWD_ASSIST_Z0 - FWD_ASSIST_Z1)) : 0.0f;
    if (rpm > 0.0f) rpm *= 1.0f + (type == SHOT_FLAT ? FWD_SPIN_ADD_FLAT : FWD_SPIN_ADD) * fwd;   // フラットはもともと回転が少ないので多めに
    if (type == SHOT_SLICE && gSliceEdge > 0.0f) {   // キレのあるスライス：速く、逆回転が強い（低く滑る）
        kmh *= 1.0f + 0.15f * gSliceEdge;
        rpm *= 1.0f + 0.30f * gSliceEdge;
    }

    // 狙いをずらす：横方向（タイミング＋ばらつき）
    float hand = (c->forehand ? 1.0f : -1.0f) * (gLefty ? -1.0f : 1.0f);   // 引っ張る向き・スライスの曲がる向き（左利きは逆）
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
        float paceDifficulty = 1.0f + c->inKmh / 150.0f;
        if (type == SHOT_DROP) dist *= 1.0f + RandNormal() * (0.03f + (1.0f - q) * 0.12f) * paceDifficulty;
        else                   dist += RandNormal() * (0.30f + (1.0f - q) * 1.6f) * paceDifficulty;   // ロブ：深さのぶれは 1〜2m
    }
    float ang, angErr = 0.0f;
    if (sp->touchAngle > 0.0f) {
        // ロブ・ドロップ：決まった角度で、狙った深さに届く速さで運ぶ（タッチ。速さで距離を合わせる）
        ang = sp->touchAngle * DEG2RAD;
        kmh = SolveSpeed(ballPos, dirFlat, dist, ang, rpm) * 3.6f;
        ang += RandNormal() * (0.7f + (1.0f - q) * 3.0f) * errMul * DEG2RAD;
        if (type == SHOT_DROP && q > 0.4f) {
            // まあまあの当たりなら、ネットにかけずに少し浮いて深くなる（ネットより、甘くなるミスの方が多い）
            for (int it = 0; it < 8 && PredictNetClearance(ballPos, dirFlat, kmh / 3.6f, ang, rpm) < 0.08f; it++) kmh *= 1.06f;
        }
    } else {
        if (c->volley && !c->overhead) {
            // ボレー：速すぎて狙った所に落とせない（ネットを越えてから落ちきらない）なら、面で勢いを殺して遅くする
            for (int it = 0; it < 10 && kmh > VOLLEY_MIN_KMH; it++) {
                float a0 = SolveLaunchAngle(ballPos, dirFlat, dist, kmh / 3.6f, rpm, sp->highArc);
                if (PredictLandingDist(ballPos, dirFlat, kmh / 3.6f, a0, rpm) <= dist + 0.5f &&
                    PredictNetClearance(ballPos, dirFlat, kmh / 3.6f, a0, rpm) >= 0.10f) break;
                kmh = fmaxf(kmh * 0.88f, VOLLEY_MIN_KMH);
            }
        }
        if (IsSliceShot(type) && !c->volley) {
            // 逆回転は浮くので、速すぎると下に向けて打っても落ちきらない。そういう球は打たない（少し遅くして沈める）
            for (int it = 0; it < 8; it++) {
                float a0 = SolveLaunchAngle(ballPos, dirFlat, dist, kmh / 3.6f, rpm, sp->highArc);
                if (PredictLandingDist(ballPos, dirFlat, kmh / 3.6f, a0, rpm) <= dist + 0.5f) break;
                kmh *= 0.92f;
            }
        }
        ang = SolveLaunchAngle(ballPos, dirFlat, dist, kmh / 3.6f, rpm, sp->highArc);
        // 縦方向のばらつき（ネットやアウトの原因）。完璧な打点でもゼロにはならない
        // ボレーは面を固めて当てるだけなので、上下のぶれは小さい
        // 速い球ほど、角度が少しずれただけで遠くまで外れる。外れる距離がそろうように、速い球ほど角度のぶれを小さく
        float speedScale = Clamp(PITCH_ERR_REF_MS / fmaxf(kmh / 3.6f, 1.0f), 0.35f, 1.5f);
        angErr = RandNormal() * (ERR_PITCH_BASE + (1.0f - q) * (1.0f - q) * ERR_PITCH_Q) * errMul * speedScale
               * (c->volley && !c->overhead ? VOLLEY_PITCH_ERR : 1.0f) * (1.0f - FWD_PITCH_CUT * fwd) * DEG2RAD;
        angErr = Clamp(angErr, -PITCH_ERR_MAX * speedScale * DEG2RAD, PITCH_ERR_MAX * speedScale * DEG2RAD);   // とんでもなく長い・短い球にはしない（速い球ほど上限も小さく）
        // 跳ね返りの分は、来た球の上下の動きを持ったまま出ていく：
        //   跳ね上がってくる速い球（サーブ）を返すと上に行きやすく、沈んでくる球は下（ネット）へ行きやすい。
        //   うまく当てるほど面で抑えられるが、半分は残る
        float vin = fmaxf(c->inKmh / 3.6f, 1.0f);
        //   リターンは遅く合わせる分もともと高い弾道になるので、上へのずれは半分（入るが浮く）
        angErr += (paceKmh / fmaxf(kmh, 1.0f)) * atan2f(c->vyBall * REBOUND_VY_KEEP, vin) * (1.0f - 0.5f * q)
                * (c->isReturn ? 0.5f : 1.0f) * (c->volley ? 0.3f : 1.0f)
                * (1.0f - 0.65f * (c->vyBall > 0.0f ? gRiseSkill : 0.0f))   // ライジングの名手は、上がり際の跳ね上がりを抑えられる   // ボレーは面で押さえるので小さい
                * (1.0f - FWD_REBOUND_CUT * fwd);   // 前の球は面で押さえやすい
        ang += angErr;
    }

    if (outKmh) *outKmh = kmh;
    if (outRpm) *outRpm = rpm;
    if (outAngle) *outAngle = ang;
    Ball b = MakeShot(ballPos, dirFlat, kmh / 3.6f, ang, rpm);
    if (IsSliceShot(type)) {
        // スライスは横回転も乗る：フォアは右、バックは左へ曲がる（弾んだ後も同じ向きに滑る）
        float ratio = (type == SHOT_SLICE) ? SLICE_SIDE_RATIO : SLICE_SIDE_RATIO * 0.6f;
        float wy = -hand * ratio * fabsf(rpm) * RPM_TO_RADS;   // 縦軸まわり。マイナスで右に曲がる
        // 曲がる分を見込んで打ち出す（狙った所に落ちる。途中で曲がって見える）
        Ball s0 = b, s1 = b;
        s1.spin.y += wy;
        for (int i = 0; i < 2400 && s0.bounces < 1 && s0.active; i++) StepBall(&s0, PHYS_DT);
        for (int i = 0; i < 2400 && s1.bounces < 1 && s1.active; i++) StepBall(&s1, PHYS_DT);
        Vector3 right = { -dirFlat.z, 0.0f, dirFlat.x };
        float lateral = Vector3DotProduct(Vector3Subtract(s1.pos, s0.pos), right);   // 右に曲がった量
        float run  = fmaxf(Vector3Length((Vector3){ s0.pos.x - ballPos.x, 0.0f, s0.pos.z - ballPos.z }), 1.0f);
        float run1 = Vector3Length((Vector3){ s1.pos.x - ballPos.x, 0.0f, s1.pos.z - ballPos.z });
        dirFlat = Vector3RotateByAxisAngle(dirFlat, (Vector3){ 0, 1, 0 }, atan2f(lateral, run));    // 右に曲がる分だけ左へ
        // 曲がって進む分だけ距離が縮むので、その分を見込んで打ち出し角を決め直す（ネットにかかりやすくならないように）
        (void)run1;
        if (sp->touchAngle <= 0.0f) {
            // 横回転があると飛ぶ距離が変わるので、実際に飛ばして狙った距離に合うまで打ち出し角を詰める
            float want = dist, a0 = ang - angErr;
            for (int it = 0; it < 4; it++) {
                a0 = SolveLaunchAngle(ballPos, dirFlat, want, kmh / 3.6f, rpm, sp->highArc);
                Ball t = MakeShot(ballPos, dirFlat, kmh / 3.6f, a0, rpm);
                t.spin.y += wy;
                for (int i = 0; i < 2400 && t.bounces < 1 && t.active; i++) StepBall(&t, PHYS_DT);
                float got = Vector3Length((Vector3){ t.pos.x - ballPos.x, 0.0f, t.pos.z - ballPos.z });
                if (fabsf(got - dist) < 0.1f) break;
                want += dist - got;
            }
            // ネットの上の余裕（10cm）も、横回転込みの弾道で確かめる。足りなければ少しずつ上げる
            for (int k = 0; k < 40; k++) {
                Ball t = MakeShot(ballPos, dirFlat, kmh / 3.6f, a0, rpm);
                t.spin.y += wy;
                for (int i = 0; i < 2400 && t.bounces < 1 && t.active && t.netClearance <= -98.0f; i++) StepBall(&t, PHYS_DT);
                if (t.netClearance >= 0.10f) break;
                a0 += 0.25f * DEG2RAD;
            }
            ang = a0 + angErr;
        }
        b = MakeShot(ballPos, dirFlat, kmh / 3.6f, ang, rpm);
        b.spin.y += wy;
    }
    // 人間の打球の横アウトの補正：実際に落ちる所を計算して、横に出るなら向きを直す
    //   カジュアルは必ず（25cm 内側）。リアルは当たりが良いほど（質 35% から効き始め、75% で完全に。5cm 内側）
    float sideK = !gHumanShot ? 0.0f : gCasual ? 1.0f : Clamp01((q - SIDE_ASSIST_Q0) / (SIDE_ASSIST_Q1 - SIDE_ASSIST_Q0));
    if (sideK > 0.0f) {
        float lim = SINGLES_W * 0.5f - (gCasual ? CASUAL_SIDE_MARGIN : REAL_SIDE_MARGIN);
        for (int it = 0; it < 2; it++) {
            Ball t = b;
            for (int k = 0; k < 2400 && t.active && t.bounces < 1; k++) StepBall(&t, PHYS_DT);
            if (fabsf(t.pos.x) <= lim) break;
            float want = (t.pos.x > 0.0f) ? lim : -lim;
            float runZ = t.pos.z - ballPos.z, a0 = atan2f(t.pos.x - ballPos.x, -runZ), a1 = atan2f(want - ballPos.x, -runZ);
            Vector3 sp0 = b.spin;
            Vector3 v = b.vel;
            Vector3 vh = { v.x, 0.0f, v.z };
            float rot = (a0 - a1) * sideK;   // 右に外れていたら左へ回す（y 軸まわり）。リアルは当たりの良さに応じて一部だけ
            vh = Vector3RotateByAxisAngle(vh, (Vector3){ 0, 1, 0 }, rot);
            b.vel = (Vector3){ vh.x, v.y, vh.z };
            b.spin = Vector3RotateByAxisAngle(sp0, (Vector3){ 0, 1, 0 }, rot);
        }
    }
    return b;
}

// 実際に狙う場所：球種と立ち位置で、狙える深さを制限する
//   普通のショットはネットから 6m より短くは狙えない（ネットに近いほど短く：ネット前なら 2.5m）
//   タッチショットは自由。簡単モードは左右を少し内側に
Vector3 EffectiveAim(ShotType t, float playerZ, float aimX, float aimDepth, PlayStyle style)
{
    float lim = style == STYLE_CASUAL ? AIM_SIDE_CASUAL : AIM_SIDE_REAL;   // ライン際まで狙える（うっかり行かないよう、印の動きは重くしてある）
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

bool IsSliceShot(ShotType t) { return t == SHOT_SLICE || t == SHOT_SLICE_LOB || t == SHOT_DROP; }
