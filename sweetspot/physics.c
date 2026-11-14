// SWEET SPOT — ボールの物理（重力・空気抵抗・回転・バウンド・ネット）と、狙った所に落とす角度の計算
#include "game.h"

// ネットは中央 0.914m、ポスト 1.07m。その間は直線でたるむとみなす
float NetHeightAt(float x)
{
    float t = fminf(fabsf(x) / NET_POST_X, 1.0f);
    return NET_H_CENTER + (NET_H_POST - NET_H_CENTER) * t;
}

float Clamp01(float v) { return fminf(fmaxf(v, 0.0f), 1.0f); }

// 平均 0・標準偏差 1 の乱数（Box-Muller）
float RandNormal(void)
{
    float u1 = ((float)rand() + 1.0f) / ((float)RAND_MAX + 2.0f);
    float u2 = ((float)rand() + 1.0f) / ((float)RAND_MAX + 2.0f);
    return sqrtf(-2.0f * logf(u1)) * cosf(2.0f * PI * u2);
}

float RandRange(float a, float b) { return a + (b - a) * ((float)rand() / (float)RAND_MAX); }

void JudgeFirstBounce(Ball *b)
{
    // ネットに当たった、またはネットに届く前に自分のコートに落ちた
    bool ownSide = (b->side < 0) ? (b->pos.z > 0.0f) : (b->pos.z < 0.0f);
    if (b->hitNet || ownSide) { b->result = RESULT_NET; return; }
    // ラインに少しでも触れたらイン
    bool inX = fabsf(b->pos.x) <= SINGLES_W * 0.5f + BALL_R;
    float depth = b->isServe ? SERVICE_Z : HALF_LEN;
    bool inZ = (b->side < 0) ? (b->pos.z < 0.0f && b->pos.z >= -depth - BALL_R)
                             : (b->pos.z > 0.0f && b->pos.z <=  depth + BALL_R);
    if (b->isServe) inX = b->pos.x >= b->boxX0 - BALL_R && b->pos.x <= b->boxX1 + BALL_R;
    b->result = (inX && inZ) ? RESULT_IN : RESULT_OUT;
    // 判定に使った点と、どれだけ外れたか（表示用）
    b->firstBounce = b->pos;
    float ox = b->isServe ? fmaxf(b->boxX0 - BALL_R - b->pos.x, b->pos.x - (b->boxX1 + BALL_R))
                          : fabsf(b->pos.x) - (SINGLES_W * 0.5f + BALL_R);
    float oz = fabsf(b->pos.z) - (depth + BALL_R);
    b->outBy = fmaxf(fmaxf(ox, oz), 0.0f);
}

// 空中でボールにかかる加速度
Vector3 AirAccel(Vector3 vel, Vector3 spin)
{
    Vector3 acc = { 0.0f, -GRAVITY, 0.0f };
    float v = Vector3Length(vel);
    if (v < 0.001f) return acc;

    // 空気抵抗：進行方向と逆向き、速さの2乗に比例
    acc = Vector3Add(acc, Vector3Scale(vel, -K_AIR * DRAG_CD * v));

    // マグヌス力：回転軸 × 速度 の向きに働く。
    // 揚力係数は「表面の速さ / 球の速さ」で決まる（よく使われる近似式）
    float w = Vector3Length(spin);
    if (w > 0.1f) {
        float cl = 1.0f / (2.0f + v / (BALL_R * w));
        Vector3 dir = Vector3Normalize(Vector3CrossProduct(spin, vel));
        acc = Vector3Add(acc, Vector3Scale(dir, K_AIR * cl * v * v));
    }
    return acc;
}

// 地面でのバウンド。接地点の「滑り」を摩擦が打ち消そうとする
//   トップスピン → 接地点の滑りが小さい → 減速が少なく、高く跳ねる
//   スライス     → 接地点の滑りが大きい → 低く滑る
void Bounce(Ball *b)
{
    float vyIn = -b->vel.y;                       // 落ちてくる速さ（正）
    b->vel.y = vyIn * BOUNCE_E;

    Vector3 r = { 0.0f, -BALL_R, 0.0f };          // 中心から接地点
    Vector3 slip = Vector3Add(b->vel, Vector3CrossProduct(b->spin, r));
    slip.y = 0.0f;
    float slipLen = Vector3Length(slip);
    if (slipLen > 1e-4f) {
        Vector3 u = Vector3Scale(slip, 1.0f / slipLen);
        // 中空球（I = 2/3 m R^2）は、摩擦で速度が dv 変わると滑りが 2.5*dv 変わる
        float dv = fminf(COURT_MU * (1.0f + BOUNCE_E) * vyIn, slipLen / 2.5f);
        b->vel = Vector3Subtract(b->vel, Vector3Scale(u, dv));
        Vector3 dw = Vector3Scale(Vector3CrossProduct(r, u), -1.5f * dv / (BALL_R * BALL_R));
        b->spin = Vector3Add(b->spin, dw);
    }
    // 柔らかいコート（クレー）はボールがめり込むので、回転に関係なく横の勢いが落ちる
    float keep = SURFACES[gSurface].keep;
    b->vel.x *= keep;
    b->vel.z *= keep;
}

// 1ステップ分ボールを進める。描画にも予測にも同じ関数を使う
void StepBall(Ball *b, float dt)
{
    if (!b->active) return;
    b->age += dt;

    bool inAir = b->pos.y > BALL_R + 1e-4f || b->vel.y > 0.0f;
    if (inAir) {
        b->vel = Vector3Add(b->vel, Vector3Scale(AirAccel(b->vel, b->spin), dt));
        b->spin = Vector3Scale(b->spin, expf(-dt / SPIN_DECAY_TAU));
    }

    Vector3 prev = b->pos;
    b->pos = Vector3Add(b->pos, Vector3Scale(b->vel, dt));

    // ネット（z=0 の面）を横切ったか
    bool crossed = (prev.z > 0.0f) != (b->pos.z > 0.0f);
    if (crossed && fabsf(b->pos.x) < NET_POST_X) {
        float clearance = (b->pos.y - BALL_R) - NetHeightAt(b->pos.x);
        if (b->bounces == 0) b->netClearance = clearance;
        if (clearance < 0.0f) {
            b->hitNet = true;
            b->pos.z = prev.z;
            b->vel.z *= -0.15f;   // ネットに吸われてほぼ止まる
            b->vel.x *= 0.3f;
            b->vel.y *= 0.3f;
            b->spin = Vector3Scale(b->spin, 0.3f);
        }
    }

    // 地面
    if (b->pos.y < BALL_R) {
        // 地面に触れた点：このステップの途中で高さ BALL_R を横切った所（ステップの終わりの点だと、
        //   速い球はステップの間に 10cm ほど先へ進んでいて、判定が外側にずれる）
        if (prev.y > BALL_R && prev.y > b->pos.y) {
            float f = (prev.y - BALL_R) / (prev.y - b->pos.y);
            b->pos.x = prev.x + (b->pos.x - prev.x) * f;
            b->pos.z = prev.z + (b->pos.z - prev.z) * f;
        }
        b->pos.y = BALL_R;
        if (b->vel.y < -0.3f) {
            Bounce(b);
            b->bounces++;
            if (b->bounces == 1) JudgeFirstBounce(b);
        } else {
            // 跳ねる力が残ってない → 転がる（転がり始めたら 2 バウンドしたのと同じ扱い）
            if (b->bounces == 1) b->bounces = 2;
            b->vel.y = 0.0f;
            float k = fmaxf(0.0f, 1.0f - ROLL_FRICTION * dt);
            b->vel.x *= k;
            b->vel.z *= k;
            if (Vector3Length(b->vel) < 0.05f) b->active = false;
        }
    }

    if (b->bounces == 1 && b->pos.y > b->peakAfterFirstBounce)
        b->peakAfterFirstBounce = b->pos.y;
}

// 打ち出し方向と回転を決めてボールを作る
//   rpm > 0 : トップスピン（ボールの上側が前に回る）
//   rpm < 0 : バックスピン
Ball MakeShot(Vector3 from, Vector3 dirFlat, float speed, float angle, float rpm)
{
    Ball b = { 0 };
    b.pos = from;
    b.vel = Vector3Add(Vector3Scale(dirFlat, speed * cosf(angle)),
                       (Vector3){ 0.0f, speed * sinf(angle), 0.0f });
    // 上向き × 進行方向 = トップスピンの回転軸（マグヌス力が下向きになる）
    Vector3 axis = Vector3Normalize(Vector3CrossProduct((Vector3){ 0, 1, 0 }, dirFlat));
    b.spin = Vector3Scale(axis, rpm * RPM_TO_RADS);
    b.active = true;
    b.side = (dirFlat.z < 0.0f) ? -1 : 1;
    b.netClearance = -99.0f;
    return b;
}

// 試し打ちして、最初のバウンドまでに進んだ距離（打つ方向に沿って）を返す
float PredictLandingDist(Vector3 from, Vector3 dirFlat, float speed, float angle, float rpm)
{
    Ball b = MakeShot(from, dirFlat, speed, angle, rpm);
    for (int i = 0; i < 240 * 6 && b.active; i++) {
        StepBall(&b, PHYS_DT);
        if (b.hitNet) return 0.0f;   // ネット = 「短すぎ」扱い
        if (b.bounces > 0) return Vector3DotProduct(Vector3Subtract(b.pos, from), dirFlat);
    }
    return 1000.0f;
}

// 試し打ちして、ネット上の余裕を返す（ネットに届く前に落ちたら大きなマイナス）
float PredictNetClearance(Vector3 from, Vector3 dirFlat, float speed, float angle, float rpm)
{
    Ball b = MakeShot(from, dirFlat, speed, angle, rpm);
    for (int i = 0; i < 240 * 6 && b.active && b.bounces == 0; i++) {
        StepBall(&b, PHYS_DT);
        if (b.netClearance > -50.0f) return b.netClearance;
    }
    return -10.0f;
}

// 狙った距離に落ちる打ち出し角を求める
// 飛距離は角度に対して「上がって下がる」山なので、同じ距離に落ちる角度は2つある。
//   high=false : 低い方（普通のショット）
//   high=true  : 高い方（守りのロブ）
// 無理な場合:
//   どの角度でも届かない → 一番遠くまで飛ぶ角度（それでも短い）
//   ネットを越えてその距離に落とせない → ネットを少し余裕を持って越える角度（＝アウト）
float SolveLaunchAngle(Vector3 from, Vector3 dirFlat, float dist, float speed, float rpm, bool high)
{
    // 一番遠くまで飛ぶ角度を探す（粗く探してから三分探索で詰める）
    float best = MIN_ANGLE, bestDist = -1.0f;
    for (float a = MIN_ANGLE; a <= MAX_ANGLE + 1e-4f; a += 5.0f * DEG2RAD) {
        float d = PredictLandingDist(from, dirFlat, speed, a, rpm);
        if (d > bestDist) { bestDist = d; best = a; }
    }
    float lo = fmaxf(best - 5.0f * DEG2RAD, MIN_ANGLE), hi = fminf(best + 5.0f * DEG2RAD, MAX_ANGLE);
    for (int i = 0; i < 16; i++) {
        float m1 = lo + (hi - lo) / 3.0f, m2 = hi - (hi - lo) / 3.0f;
        if (PredictLandingDist(from, dirFlat, speed, m1, rpm) < PredictLandingDist(from, dirFlat, speed, m2, rpm)) lo = m1;
        else hi = m2;
    }
    float peak = 0.5f * (lo + hi);
    if (PredictLandingDist(from, dirFlat, speed, peak, rpm) < dist) return peak;

    if (high) {
        lo = peak; hi = MAX_ANGLE;
        for (int i = 0; i < 30; i++) {
            float mid = 0.5f * (lo + hi);
            if (PredictLandingDist(from, dirFlat, speed, mid, rpm) > dist) lo = mid;   // まだ遠い → もっと上げる
            else                                                          hi = mid;
        }
        return 0.5f * (lo + hi);
    }

    lo = MIN_ANGLE; hi = peak;
    for (int i = 0; i < 30; i++) {
        float mid = 0.5f * (lo + hi);
        if (PredictLandingDist(from, dirFlat, speed, mid, rpm) < dist) lo = mid;   // 手前 → 角度を上げる
        else                                                          hi = mid;
    }
    float ang = 0.5f * (lo + hi);

    const float minMargin = 0.10f;
    if (PredictNetClearance(from, dirFlat, speed, ang, rpm) < minMargin) {
        lo = ang; hi = peak;
        for (int i = 0; i < 30; i++) {
            float mid = 0.5f * (lo + hi);
            if (PredictNetClearance(from, dirFlat, speed, mid, rpm) < minMargin) lo = mid;
            else                                                                hi = mid;
        }
        ang = hi;
    }
    return ang;
}

// 打ち出し角を固定して、狙った距離に届く速さ（m/s）を求める
float SolveSpeed(Vector3 from, Vector3 dirFlat, float dist, float angle, float rpm)
{
    float lo = 2.0f, hi = 40.0f;
    for (int i = 0; i < 30; i++) {
        float mid = 0.5f * (lo + hi);
        if (PredictLandingDist(from, dirFlat, mid, angle, rpm) < dist) lo = mid;
        else                                                          hi = mid;
    }
    return 0.5f * (lo + hi);
}

// from から target に向けて、指定の球速・回転で打つ（角度は自動で決める）
Ball AimedShot(Vector3 from, Vector3 target, float kmh, float rpm, bool high, float *outAngle)
{
    Vector3 d = { target.x - from.x, 0.0f, target.z - from.z };
    Vector3 dirFlat = Vector3Normalize(d);
    float ang = SolveLaunchAngle(from, dirFlat, Vector3Length(d), kmh / 3.6f, rpm, high);
    if (outAngle) *outAngle = ang;
    return MakeShot(from, dirFlat, kmh / 3.6f, ang, rpm);
}
