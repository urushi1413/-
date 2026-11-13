// SWEET SPOT — サーブ（回転 3 成分、狙いの計算）と、練習用のマシン
#include "game.h"

// ================= サーブ =================
// 回転は3成分：
//   top  : トップスピン（進行方向に対して前回り）。下に落ちる
//   side : 縦軸まわりの横回転。プラスで打った人から見て左に曲がる（右利きのスライス）
//   gyro : 進行方向まわりの回転。空中では曲がらないが、バウンドで横に跳ねる（キックが跳ねる理由）

const ServeSpec SERVES[SERVE_COUNT] = {
    [SERVE_FLAT]  = { "FLAT SERVE",  150.0f, 215.0f,  600.0f, 1200.0f,     0.0f,     0.0f,    0.0f,    0.0f, RAYWHITE },
    [SERVE_SLICE] = { "SLICE SERVE", 140.0f, 190.0f,  300.0f,  600.0f,  1800.0f,  3000.0f,    0.0f,    0.0f, (Color){ 80, 220, 255, 255 } },
    [SERVE_KICK]  = { "KICK SERVE",  120.0f, 170.0f, 2500.0f, 3800.0f,  -400.0f,  -800.0f, 1200.0f, 2200.0f, (Color){ 255, 160, 40, 255 } },
};

Vector3 SpinVector(Vector3 dirFlat, float top, float side, float gyro)
{
    Vector3 axTop = Vector3Normalize(Vector3CrossProduct((Vector3){ 0, 1, 0 }, dirFlat));
    Vector3 w = Vector3Scale(axTop, top);
    w = Vector3Add(w, (Vector3){ 0.0f, side, 0.0f });
    w = Vector3Add(w, Vector3Scale(dirFlat, gyro));
    return Vector3Scale(w, RPM_TO_RADS);
}

Ball MakeServeBall(Vector3 from, Vector3 dirFlat, float speed, float pitch, Vector3 spinRpm)
{
    Ball b = MakeShot(from, dirFlat, speed, pitch, 0.0f);
    b.spin = SpinVector(dirFlat, spinRpm.x, spinRpm.y, spinRpm.z);
    b.isServe = true;
    return b;
}

Ball FlyToFirstBounce(Ball b)
{
    for (int i = 0; i < 240 * 4 && b.active && b.bounces == 0; i++) StepBall(&b, PHYS_DT);
    return b;
}

// 横回転があると曲がるので、左右の向きと打ち出し角を交互に合わせて狙いを決める
// 無理な場合はネットを 5cm 越える角度にする（＝ロング）
void SolveServe(Vector3 from, Vector3 target, float speed, Vector3 spinRpm, Vector3 *outDir, float *outPitch)
{
    Vector3 toT = { target.x - from.x, 0.0f, target.z - from.z };
    float want = Vector3Length(toT);
    Vector3 dir0 = Vector3Normalize(toT);
    float yaw = 0.0f, pitch = 0.0f;
    for (int it = 0; it < 6; it++) {
        Vector3 dir = Vector3RotateByAxisAngle(dir0, (Vector3){ 0, 1, 0 }, yaw);
        float lo = -20.0f * DEG2RAD, hi = 20.0f * DEG2RAD;
        for (int k = 0; k < 26; k++) {
            float mid = 0.5f * (lo + hi);
            Ball b = FlyToFirstBounce(MakeServeBall(from, dir, speed, mid, spinRpm));
            float d = b.hitNet ? 0.0f : Vector3Length((Vector3){ b.pos.x - from.x, 0, b.pos.z - from.z });
            if (d < want) lo = mid; else hi = mid;
        }
        pitch = 0.5f * (lo + hi);
        Ball b = FlyToFirstBounce(MakeServeBall(from, dir, speed, pitch, spinRpm));
        if (b.hitNet) break;
        Vector3 toLand = Vector3Normalize((Vector3){ b.pos.x - from.x, 0, b.pos.z - from.z });
        // 着地点の向きと狙いの向きのずれ（縦軸まわりの角度）だけ回す
        float err = atan2f(Vector3CrossProduct(toLand, dir0).y, Vector3DotProduct(toLand, dir0));
        yaw += err;
    }
    Vector3 dir = Vector3RotateByAxisAngle(dir0, (Vector3){ 0, 1, 0 }, yaw);
    Ball b = FlyToFirstBounce(MakeServeBall(from, dir, speed, pitch, spinRpm));
    if (b.netClearance < 0.05f) {
        float lo = pitch, hi = 20.0f * DEG2RAD;
        for (int k = 0; k < 26; k++) {
            float mid = 0.5f * (lo + hi);
            Ball t = FlyToFirstBounce(MakeServeBall(from, dir, speed, mid, spinRpm));
            if (t.netClearance < 0.05f) lo = mid; else hi = mid;
        }
        pitch = hi;
    }
    *outDir = dir;
    *outPitch = pitch;
}

// サーブのコースのサービスボックス（打つ側から見て対角）。side: -1=相手側(z<0) に打つ
void ServiceBox(float serverX, int side, float *x0, float *x1)
{
    // サーバーが x>0 にいたら、対角のボックスは x<0 側
    if (serverX > 0.0f) { *x0 = -SINGLES_W * 0.5f; *x1 = 0.0f; }
    else                { *x0 = 0.0f;              *x1 = SINGLES_W * 0.5f; }
    (void)side;
}

// サーブを打つ。charge: 溜め、q: 打点の質（1 で完璧）
//   速く打つほど（溜めるほど）ばらつきが大きい。打点が低いと物理的に入りにくい
Ball HitServe(Vector3 contact, Vector3 target, ServeType type, float charge, float q,
                     float x0, float x1, float *outKmh)
{
    const ServeSpec *sp = &SERVES[type];
    float kmh = Lerp(sp->minKmh, sp->maxKmh, charge) * (0.80f + 0.20f * q);
    float sk = 0.70f + 0.30f * q;
    Vector3 spin = { Lerp(sp->top0, sp->top1, charge) * sk, Lerp(sp->side0, sp->side1, charge) * sk,
                     Lerp(sp->gyro0, sp->gyro1, charge) * sk };
    Vector3 dir; float pitch;
    SolveServe(contact, target, kmh / 3.6f, spin, &dir, &pitch);
    float effort = (0.5f + 0.8f * charge) * (gCasual ? 0.55f : 1.0f);   // 振り切るほどぶれる（簡単モードは小さく）
    float yawErr   = RandNormal() * (0.50f + (1.0f - q) * 2.5f) * effort * DEG2RAD;
    float pitchErr = RandNormal() * (0.20f + (1.0f - q) * 1.0f) * effort * DEG2RAD;
    dir = Vector3RotateByAxisAngle(dir, (Vector3){ 0, 1, 0 }, yawErr);
    Ball b = MakeServeBall(contact, dir, kmh / 3.6f, pitch + pitchErr, spin);
    b.boxX0 = x0; b.boxX1 = x1;
    if (outKmh) *outKmh = kmh;
    return b;
}

// サーブマシン：相手のベースラインの高さ 2.8m から、自分のデュースコートのサービスボックスへ
// 7 割がファースト（フラットかスライスで T かワイド）、3 割がセカンド（キックでどこでも）
Ball MachineServe(void)
{
    Vector3 from = { -0.6f, 2.8f, -HALF_LEN + 0.3f };
    const float courseX[3] = { 0.35f, 3.75f, 2.0f };   // センター / ワイド / ボディ
    bool second = rand() % 10 < 3;
    ServeType t = second ? SERVE_KICK : (rand() % 2 ? SERVE_FLAT : SERVE_SLICE);
    Vector3 target = { courseX[rand() % (second ? 3 : 2)], 0.0f, RandRange(5.3f, 6.0f) };
    float charge = second ? RandRange(0.4f, 0.8f) : RandRange(0.7f, 1.0f);
    const ServeSpec *sp = &SERVES[t];
    float kmh = Lerp(sp->minKmh, sp->maxKmh, charge);
    Vector3 spin = { Lerp(sp->top0, sp->top1, charge), Lerp(sp->side0, sp->side1, charge), Lerp(sp->gyro0, sp->gyro1, charge) };
    // マシンは相手側（+z 向き）に打つので、スライスの曲がる向きもマシンから見て左
    Vector3 dir; float pitch;
    SolveServe(from, target, kmh / 3.6f, spin, &dir, &pitch);
    Ball b = MakeServeBall(from, dir, kmh / 3.6f, pitch, spin);
    b.boxX0 = 0.0f; b.boxX1 = SINGLES_W * 0.5f;
    b.lastHitter = HITTER_OPP;
    return b;
}

// 球出しマシン：相手のベースラインから、ランダムな場所・球種で出す
// pro=false: 打ち出し 70〜105km/h くらい（スクール）、pro=true: 105〜140km/h（プロのラリー）
Ball MachineFeed(bool pro)
{
    Vector3 from   = { RandRange(-1.0f, 1.0f), 1.0f, -HALF_LEN - 0.5f };
    Vector3 target = { RandRange(-3.2f, 3.2f), 0.0f, RandRange(5.5f, 10.5f) };
    ShotType t = (ShotType)(rand() % SHOT_NORMAL_COUNT);
    float charge = pro ? RandRange(0.5f, 0.8f) : RandRange(0.1f, 0.5f);
    float kmh = Lerp(SHOTS[t].minKmh, SHOTS[t].maxKmh, charge);
    {
        // フラット / トップ / スライス
        const float proLo[SHOT_NORMAL_COUNT]  = { 115.0f, 105.0f,  90.0f };
        const float proHi[SHOT_NORMAL_COUNT]  = { 140.0f, 135.0f, 115.0f };
        const float clubLo[SHOT_NORMAL_COUNT] = {  85.0f,  75.0f,  65.0f };
        const float clubHi[SHOT_NORMAL_COUNT] = { 105.0f, 100.0f,  85.0f };
        kmh = pro ? RandRange(proLo[t], proHi[t]) : RandRange(clubLo[t], clubHi[t]);
    }
    float rpm = Lerp(SHOTS[t].minRpm, SHOTS[t].maxRpm, charge);
    Ball b = AimedShot(from, target, kmh, rpm, false, NULL);
    b.lastHitter = HITTER_OPP;
    return b;
}
