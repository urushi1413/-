// 3D テニス：ステップ3「打点システム」
// 球出しマシンの球を、動いて・溜めて・タイミングよく離して打ち返す。
// 打点（タイミング・体からの距離・高さ）の良し悪しで、球速・回転・コースのずれが決まる。
// 単位はすべてメートル・秒・ラジアン。座標系: x=横, y=上, z=縦（自分側が +z、相手側が -z）
#include "raylib.h"
#include "raymath.h"
#include <math.h>
#include <stdbool.h>
#include <stdlib.h>

// ---- コート寸法（ITF 規格） ----
#define COURT_LEN        23.77f
#define HALF_LEN         (COURT_LEN * 0.5f)
#define SINGLES_W        8.23f
#define DOUBLES_W        10.97f
#define SERVICE_Z        6.40f              // ネットからサービスラインまで
#define NET_H_CENTER     0.914f
#define NET_H_POST       1.07f
#define NET_POST_X       (DOUBLES_W * 0.5f + 0.914f)
#define LINE_W           0.05f

// ---- ボールの物理 ----
#define BALL_R           0.0335f
#define BALL_MASS        0.057f
#define GRAVITY          9.81f
#define AIR_RHO          1.21f
#define DRAG_CD          0.55f
#define BALL_AREA        (PI * BALL_R * BALL_R)
#define K_AIR            (0.5f * AIR_RHO * BALL_AREA / BALL_MASS)   // 空気の力 = K_AIR * 係数 * 速さ^2
#define SPIN_DECAY_TAU   4.0f    // 回転が空気で弱まる時定数（秒）

#define BOUNCE_E         0.77f   // 垂直方向の反発係数。空気抵抗込みで 2.54m から落として 1.40m 跳ねる（規格は 1.35〜1.47m）
#define COURT_MU         0.60f   // コート表面の摩擦係数（ハードコートくらい）
#define ROLL_FRICTION    1.5f    // 転がり中の減速（1/秒）

#define RPM_TO_RADS      (2.0f * PI / 60.0f)
#define PHYS_DT          (1.0f / 240.0f)   // 物理は固定ステップで回す（速い球でもすり抜けない）
#define TRAIL_N          900
#define CHARGE_TIME      0.8f              // 最大まで溜めるのにかかる秒数

// ---- 打点（右利き。体の右がフォア、左がバック） ----
#define PLAYER_SPEED     6.0f    // 走る速さ（m/s）
#define IDEAL_FWD        0.40f   // 理想の打点：体より 40cm 前
#define IDEAL_SIDE       0.85f   // 理想の打点：体から横に 85cm
#define TOL_FWD          0.90f   // タイミングの許容幅（前後にこれだけずれると質 0）
#define TOL_SIDE         0.80f   // 距離の許容幅
#define REACH_FWD        1.00f   // これ以上前後にずれたら空振り
#define REACH_SIDE       1.70f   // これ以上遠いと空振り（ラケットが届かない）
#define REACH_HEIGHT     2.30f
#define TIMING_PULL      0.30f   // タイミングのずれ 1m あたりの方向ずれ（rad）。早いと引っ張り、遅いと流れる

typedef enum { RESULT_NONE, RESULT_IN, RESULT_OUT, RESULT_NET } ShotResult;
typedef enum { SHOT_FLAT, SHOT_TOP, SHOT_SLICE, SHOT_COUNT } ShotType;
typedef enum { HITTER_NONE, HITTER_MACHINE, HITTER_PLAYER } Hitter;

// 球種ごとの性格。溜め 0→1 で min→max に変わる
// rpm はプラスがトップスピン、マイナスがバックスピン（スライス）
// idealHeight / tolHeight: その球種が一番打ちやすい打点の高さと、許容幅
typedef struct {
    const char *name;
    float minKmh, maxKmh;
    float minRpm, maxRpm;
    float idealHeight, tolHeight;
    Color color;
} ShotSpec;

static const ShotSpec SHOTS[SHOT_COUNT] = {
    //                 name       km/h            rpm               高さ(理想, 許容)
    [SHOT_FLAT]  = { "FLAT",    90.0f, 190.0f,   500.0f,   900.0f, 1.20f, 0.60f, RAYWHITE },
    [SHOT_TOP]   = { "TOPSPIN", 70.0f, 150.0f,  1800.0f,  3200.0f, 1.00f, 0.80f, (Color){ 255, 200, 40, 255 } },
    [SHOT_SLICE] = { "SLICE",   60.0f, 105.0f, -1200.0f, -2200.0f, 0.75f, 1.00f, (Color){ 80, 220, 255, 255 } },
};

typedef struct {
    Vector3 pos;
    Vector3 vel;
    Vector3 spin;          // 角速度ベクトル（rad/s）。向き＝回転軸、長さ＝回転の速さ
    bool    active;
    bool    hitNet;
    int     side;          // 狙った側：-1=相手コート(z<0)、+1=自分のコート(z>0)
    int     bounces;
    float   netClearance;  // ネット上を通過したときの余裕（m）。マイナスならネットに当たった
    float   peakAfterFirstBounce;
    ShotResult result;
    Hitter  lastHitter;
} Ball;

// 打点の評価結果
typedef enum { MISS_NONE, MISS_EARLY, MISS_LATE, MISS_FAR, MISS_HIGH, MISS_NO_BALL } MissReason;
typedef struct {
    MissReason miss;
    bool  forehand;
    float fwdErr;      // プラス＝早い（ボールがまだ前）、マイナス＝遅い（食い込まれた）
    float side;        // 体からの横距離
    float height;      // 打点の高さ
    float qTiming, qDist, qHeight;
    float quality;     // 0〜1。3つの積
} Contact;

// ネットは中央 0.914m、ポスト 1.07m。その間は直線でたるむとみなす
static float NetHeightAt(float x)
{
    float t = fminf(fabsf(x) / NET_POST_X, 1.0f);
    return NET_H_CENTER + (NET_H_POST - NET_H_CENTER) * t;
}

static float Clamp01(float v) { return fminf(fmaxf(v, 0.0f), 1.0f); }

// 平均 0・標準偏差 1 の乱数（Box-Muller）
static float RandNormal(void)
{
    float u1 = ((float)rand() + 1.0f) / ((float)RAND_MAX + 2.0f);
    float u2 = ((float)rand() + 1.0f) / ((float)RAND_MAX + 2.0f);
    return sqrtf(-2.0f * logf(u1)) * cosf(2.0f * PI * u2);
}

static float RandRange(float a, float b) { return a + (b - a) * ((float)rand() / (float)RAND_MAX); }

static void JudgeFirstBounce(Ball *b)
{
    if (b->hitNet) { b->result = RESULT_NET; return; }
    // ラインに少しでも触れたらイン
    bool inX = fabsf(b->pos.x) <= SINGLES_W * 0.5f + BALL_R;
    bool inZ = (b->side < 0) ? (b->pos.z < 0.0f && b->pos.z >= -HALF_LEN - BALL_R)
                             : (b->pos.z > 0.0f && b->pos.z <=  HALF_LEN + BALL_R);
    b->result = (inX && inZ) ? RESULT_IN : RESULT_OUT;
}

// 空中でボールにかかる加速度
static Vector3 AirAccel(Vector3 vel, Vector3 spin)
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
static void Bounce(Ball *b)
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
}

// 1ステップ分ボールを進める。描画にも予測にも同じ関数を使う
static void StepBall(Ball *b, float dt)
{
    if (!b->active) return;

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
        b->pos.y = BALL_R;
        if (b->vel.y < -0.3f) {
            Bounce(b);
            b->bounces++;
            if (b->bounces == 1) JudgeFirstBounce(b);
        } else {
            // 跳ねる力が残ってない → 転がる
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
static Ball MakeShot(Vector3 from, Vector3 dirFlat, float speed, float angle, float rpm)
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
static float PredictLandingDist(Vector3 from, Vector3 dirFlat, float speed, float angle, float rpm)
{
    Ball b = MakeShot(from, dirFlat, speed, angle, rpm);
    for (int i = 0; i < 240 * 6 && b.active; i++) {
        StepBall(&b, PHYS_DT);
        if (b.hitNet) return 0.0f;   // ネット = 「短すぎ」扱い
        if (b.bounces > 0) return Vector3DotProduct(Vector3Subtract(b.pos, from), dirFlat);
    }
    return 1000.0f;
}

// 狙った距離に落ちる打ち出し角を二分探索で求める
static float SolveLaunchAngle(Vector3 from, Vector3 dirFlat, float dist, float speed, float rpm)
{
    float lo = -15.0f * DEG2RAD, hi = 40.0f * DEG2RAD;
    for (int i = 0; i < 30; i++) {
        float mid = 0.5f * (lo + hi);
        if (PredictLandingDist(from, dirFlat, speed, mid, rpm) < dist) lo = mid;   // 手前 → 角度を上げる
        else                                                          hi = mid;
    }
    return 0.5f * (lo + hi);
}

// from から target に向けて、指定の球速・回転で打つ（角度は自動で決める）
static Ball AimedShot(Vector3 from, Vector3 target, float kmh, float rpm, float *outAngle)
{
    Vector3 d = { target.x - from.x, 0.0f, target.z - from.z };
    Vector3 dirFlat = Vector3Normalize(d);
    float ang = SolveLaunchAngle(from, dirFlat, Vector3Length(d), kmh / 3.6f, rpm);
    if (outAngle) *outAngle = ang;
    return MakeShot(from, dirFlat, kmh / 3.6f, ang, rpm);
}

// ---- 打点の評価 ----
// player はプレイヤーの足元の位置。相手コート（-z）を向いて立っている
static Contact EvaluateContact(Vector3 player, Vector3 ballPos, ShotType type)
{
    Contact c = { 0 };
    float lateral = ballPos.x - player.x;
    c.forehand = lateral >= 0.0f;
    c.side     = fabsf(lateral);
    c.height   = ballPos.y;
    c.fwdErr   = (player.z - IDEAL_FWD) - ballPos.z;   // プラス＝ボールがまだ前にある＝振るのが早い

    if (c.fwdErr >  REACH_FWD)   { c.miss = MISS_EARLY; return c; }
    if (c.fwdErr < -REACH_FWD)   { c.miss = MISS_LATE;  return c; }
    if (c.side   >  REACH_SIDE)  { c.miss = MISS_FAR;   return c; }
    if (c.height >  REACH_HEIGHT){ c.miss = MISS_HIGH;  return c; }

    const ShotSpec *sp = &SHOTS[type];
    c.qTiming = Clamp01(1.0f - fabsf(c.fwdErr) / TOL_FWD);
    c.qDist   = Clamp01(1.0f - fabsf(c.side - IDEAL_SIDE) / TOL_SIDE);
    c.qHeight = Clamp01(1.0f - fabsf(c.height - sp->idealHeight) / sp->tolHeight);
    c.quality = c.qTiming * c.qDist * c.qHeight;
    return c;
}

// 打点の質から実際の打球を作る
//   質が悪い → 球速と回転が落ちる、狙いが散らばる
//   タイミングのずれ → 早いと引っ張り、遅いと流れる（フォアとバックで逆）
static Ball PlayerShot(const Contact *c, Vector3 ballPos, Vector3 target, ShotType type, float charge,
                       float *outKmh, float *outRpm, float *outAngle)
{
    const ShotSpec *sp = &SHOTS[type];
    float q = c->quality;
    float kmh = Lerp(sp->minKmh, sp->maxKmh, charge) * (0.65f + 0.35f * q);
    float rpm = Lerp(sp->minRpm, sp->maxRpm, charge) * (0.60f + 0.40f * q);

    // 狙いをずらす：横方向（タイミング＋ばらつき）
    float hand = c->forehand ? 1.0f : -1.0f;
    float yaw = hand * c->fwdErr * TIMING_PULL
              + RandNormal() * (0.5f + (1.0f - q) * 6.0f) * DEG2RAD;
    yaw = Clamp(yaw, -30.0f * DEG2RAD, 30.0f * DEG2RAD);

    Vector3 d = { target.x - ballPos.x, 0.0f, target.z - ballPos.z };
    Vector3 dirFlat = Vector3RotateByAxisAngle(Vector3Normalize(d), (Vector3){ 0, 1, 0 }, yaw);
    float ang = SolveLaunchAngle(ballPos, dirFlat, Vector3Length(d), kmh / 3.6f, rpm);

    // 縦方向のばらつき（ネットやアウトの原因）。完璧な打点でもゼロにはならない
    ang += RandNormal() * (0.15f + (1.0f - q) * 2.5f) * DEG2RAD;

    if (outKmh) *outKmh = kmh;
    if (outRpm) *outRpm = rpm;
    if (outAngle) *outAngle = ang;
    return MakeShot(ballPos, dirFlat, kmh / 3.6f, ang, rpm);
}

// 球出しマシン：相手のベースラインから、ランダムな場所・球種で出す
static Ball MachineFeed(void)
{
    Vector3 from   = { RandRange(-1.0f, 1.0f), 1.0f, -HALF_LEN - 0.5f };
    Vector3 target = { RandRange(-3.2f, 3.2f), 0.0f, RandRange(5.5f, 10.5f) };
    ShotType t = (ShotType)(rand() % SHOT_COUNT);
    float charge = RandRange(0.1f, 0.5f);
    float kmh = Lerp(SHOTS[t].minKmh, SHOTS[t].maxKmh, charge);
    float rpm = Lerp(SHOTS[t].minRpm, SHOTS[t].maxRpm, charge);
    Ball b = AimedShot(from, target, kmh, rpm, NULL);
    b.lastHitter = HITTER_MACHINE;
    return b;
}

// ---- 描画まわり ----
static void LineAlongX(float z, float x0, float x1)
{
    DrawCube((Vector3){ (x0 + x1) * 0.5f, 0.002f, z }, fabsf(x1 - x0), 0.004f, LINE_W, RAYWHITE);
}

static void LineAlongZ(float x, float z0, float z1)
{
    DrawCube((Vector3){ x, 0.002f, (z0 + z1) * 0.5f }, LINE_W, 0.004f, fabsf(z1 - z0), RAYWHITE);
}

static void DrawCourt(void)
{
    DrawPlane((Vector3){ 0, -0.001f, 0 }, (Vector2){ 40.0f, 60.0f }, (Color){ 40, 110, 80, 255 });
    DrawPlane((Vector3){ 0, 0.0f, 0 }, (Vector2){ DOUBLES_W, COURT_LEN }, (Color){ 40, 90, 160, 255 });

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

static const char *ResultText(ShotResult r)
{
    switch (r) {
        case RESULT_IN:  return "IN";
        case RESULT_OUT: return "OUT";
        case RESULT_NET: return "NET";
        default:         return "-";
    }
}

static const char *MissText(MissReason m)
{
    switch (m) {
        case MISS_EARLY:   return "WHIFF: too early";
        case MISS_LATE:    return "WHIFF: too late";
        case MISS_FAR:     return "WHIFF: too far";
        case MISS_HIGH:    return "WHIFF: too high";
        case MISS_NO_BALL: return "no ball to hit";
        default:           return "";
    }
}

static Color QualityColor(float q)
{
    if (q >= 0.75f) return GREEN;
    if (q >= 0.45f) return YELLOW;
    return RED;
}

int main(void)
{
    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_WINDOW_RESIZABLE);
    InitWindow(1280, 720, "Tennis - step3: contact point");
    SetTargetFPS(60);
    srand((unsigned)GetTime() ^ 0x5eed);

    Camera3D cam = { 0 };
    cam.up = (Vector3){ 0, 1, 0 };
    cam.fovy = 45.0f;
    cam.projection = CAMERA_PERSPECTIVE;
    int camMode = 0;

    Vector3 player = { 0.0f, 0.0f, HALF_LEN + 0.8f };   // 足元の位置

    Ball ball = { 0 };
    float feedTimer = 1.0f;          // 0 になったら次の球を出す
    Vector3 trail[TRAIL_N];
    int trailCount = 0;
    float trailTimer = 0.0f;
    Color trailColor = LIGHTGRAY;

    // 溜め打ち：J/K/L を押している間に溜まり、離した瞬間にスイング
    const int shotKeys[SHOT_COUNT] = { KEY_L, KEY_J, KEY_K };   // L=フラット J=トップスピン K=スライス
    int   chargingShot = -1;
    float charge = 0.0f;

    // 最後のショットの記録
    bool hasLast = false;
    Contact lastContact = { 0 };
    ShotType lastType = SHOT_TOP;
    float lastKmh = 0.0f, lastRpm = 0.0f, lastAngle = 0.0f;
    Vector3 lastContactPos = { 0 };
    bool scoredLast = true;

    int stats[SHOT_COUNT][2] = { 0 };   // [球種][0=打った数, 1=イン]
    bool slowMo = false;
    float accumulator = 0.0f;

    while (!WindowShouldClose()) {
        float frameDt = fminf(GetFrameTime(), 0.1f);

        // ---- 移動（WASD）----
        Vector3 move = { 0 };
        if (IsKeyDown(KEY_A)) move.x -= 1.0f;
        if (IsKeyDown(KEY_D)) move.x += 1.0f;
        if (IsKeyDown(KEY_W)) move.z -= 1.0f;
        if (IsKeyDown(KEY_S)) move.z += 1.0f;
        if (Vector3Length(move) > 0.0f) {
            // 溜めている間は少し遅くなる（構えながら動く）
            float spd = PLAYER_SPEED * (chargingShot >= 0 ? 0.6f : 1.0f);
            player = Vector3Add(player, Vector3Scale(Vector3Normalize(move), spd * frameDt));
            player.x = Clamp(player.x, -DOUBLES_W * 0.5f - 3.0f, DOUBLES_W * 0.5f + 3.0f);
            player.z = Clamp(player.z, 1.0f, HALF_LEN + 5.0f);
        }

        if (IsKeyPressed(KEY_C)) camMode = (camMode + 1) % 3;
        if (IsKeyPressed(KEY_T)) slowMo = !slowMo;
        if (IsKeyPressed(KEY_F)) feedTimer = 0.0f;

        // ---- スイング ----
        if (chargingShot < 0) {
            for (int s = 0; s < SHOT_COUNT; s++) {
                if (IsKeyPressed(shotKeys[s])) { chargingShot = s; charge = 0.0f; }
            }
        } else {
            charge = fminf(charge + frameDt / CHARGE_TIME, 1.0f);
            if (IsKeyReleased(shotKeys[chargingShot])) {
                ShotType type = (ShotType)chargingShot;
                chargingShot = -1;

                // コースは「離した瞬間に押している方向」で決まる
                Vector3 target = { 0.0f, 0.0f, -9.0f };
                if (IsKeyDown(KEY_A)) target.x = -3.0f;
                if (IsKeyDown(KEY_D)) target.x =  3.0f;
                if (IsKeyDown(KEY_W)) target.z = -10.3f;
                if (IsKeyDown(KEY_S)) target.z = -6.0f;

                bool hittable = ball.active && ball.lastHitter == HITTER_MACHINE &&
                                ball.bounces <= 1 && ball.pos.z > 0.0f && !ball.hitNet;
                Contact c = EvaluateContact(player, ball.pos, type);
                if (!hittable) c.miss = MISS_NO_BALL;

                hasLast = true;
                lastContact = c;
                lastType = type;
                if (c.miss == MISS_NONE) {
                    lastContactPos = ball.pos;
                    ball = PlayerShot(&c, ball.pos, target, type, charge, &lastKmh, &lastRpm, &lastAngle);
                    ball.lastHitter = HITTER_PLAYER;
                    trailColor = SHOTS[type].color;
                    trailCount = 0;
                    stats[type][0]++;
                    scoredLast = false;
                }
            }
        }
        if (IsKeyPressed(KEY_R)) {
            // ITF のバウンドテスト: 2.54m から落とす
            ball = (Ball){ 0 };
            ball.pos = (Vector3){ 0.0f, 2.54f, -3.0f };
            ball.active = true;
            ball.netClearance = -99.0f;
            trailCount = 0;
            feedTimer = 4.0f;
        }

        // ---- 物理（固定ステップ）----
        accumulator += frameDt * (slowMo ? 0.25f : 1.0f);
        while (accumulator >= PHYS_DT) {
            StepBall(&ball, PHYS_DT);
            accumulator -= PHYS_DT;

            trailTimer += PHYS_DT;
            if (ball.active && trailTimer >= 1.0f / 120.0f) {
                trailTimer = 0.0f;
                if (trailCount < TRAIL_N) trail[trailCount++] = ball.pos;
            }
        }

        // 自分の打球の結果を集計
        if (ball.lastHitter == HITTER_PLAYER && !scoredLast && ball.result != RESULT_NONE) {
            if (ball.result == RESULT_IN) stats[lastType][1]++;
            scoredLast = true;
        }

        // 次の球出し：打球が落ちた / 打ち損ねて2バウンド / 止まった
        bool rallyOver = !ball.active || ball.hitNet ||
                         (ball.lastHitter == HITTER_PLAYER  && ball.bounces >= 1) ||
                         (ball.lastHitter == HITTER_MACHINE && ball.bounces >= 2);
        if (rallyOver) {
            feedTimer -= frameDt;
            if (feedTimer <= 0.0f) {
                ball = MachineFeed();
                trailColor = Fade(LIGHTGRAY, 0.6f);
                trailCount = 0;
                feedTimer = 1.2f;
            }
        }

        // ---- カメラ ----
        if (camMode == 0) {        // プレイヤーの後ろ
            cam.position = (Vector3){ player.x * 0.6f, 3.2f, player.z + 6.0f };
            cam.target   = (Vector3){ player.x * 0.3f, 0.6f, player.z - 8.0f };
        } else if (camMode == 1) { // 真横（弾道が一番わかる）
            cam.position = (Vector3){ 22.0f, 2.5f, 0.0f };
            cam.target   = (Vector3){ 0.0f, 1.0f, 0.0f };
        } else {                   // ボール追従
            cam.target   = ball.pos;
            cam.position = Vector3Add(ball.pos, (Vector3){ 4.0f, 2.0f, 6.0f });
        }

        // ---- 描画 ----
        BeginDrawing();
        ClearBackground((Color){ 150, 190, 230, 255 });
        BeginMode3D(cam);
            DrawCourt();

            // プレイヤー（仮）。正面が -z
            DrawCapsule((Vector3){ player.x, 0.3f, player.z }, (Vector3){ player.x, 1.5f, player.z },
                        0.25f, 8, 4, (Color){ 230, 120, 60, 255 });

            // 理想の打点（フォア・バック）。溜めている球種の理想の高さに出す
            {
                ShotType t = (chargingShot >= 0) ? (ShotType)chargingShot : SHOT_TOP;
                float h = SHOTS[t].idealHeight;
                Color zc = Fade(SHOTS[t].color, 0.8f);
                DrawSphereWires((Vector3){ player.x + IDEAL_SIDE, h, player.z - IDEAL_FWD }, 0.15f, 6, 8, zc);
                DrawSphereWires((Vector3){ player.x - IDEAL_SIDE, h, player.z - IDEAL_FWD }, 0.15f, 6, 8, zc);
            }

            // 最後に打った打点
            if (hasLast && lastContact.miss == MISS_NONE)
                DrawSphere(lastContactPos, 0.05f, QualityColor(lastContact.quality));

            // 軌跡
            for (int i = 1; i < trailCount; i++) DrawLine3D(trail[i - 1], trail[i], trailColor);

            // 影（高さが分かるようにするのはテニスゲームでは超重要）
            if (ball.active || ball.bounces > 0) {
                DrawCircle3D((Vector3){ ball.pos.x, 0.006f, ball.pos.z }, BALL_R * 1.5f, (Vector3){ 1, 0, 0 }, 90.0f,
                             (Color){ 0, 0, 0, 120 });
                DrawSphere(ball.pos, BALL_R, (Color){ 210, 240, 40, 255 });
            }
        EndMode3D();

        // ---- HUD ----
        DrawRectangle(10, 10, 470, 250, Fade(BLACK, 0.5f));
        int y = 20;
        if (hasLast) {
            const Contact *c = &lastContact;
            if (c->miss != MISS_NONE) {
                DrawText(MissText(c->miss), 20, y, 22, RED); y += 30;
            } else {
                DrawText(TextFormat("%s %s  quality %.0f%%", c->forehand ? "Forehand" : "Backhand",
                                    SHOTS[lastType].name, c->quality * 100.0f), 20, y, 22, QualityColor(c->quality));
                y += 30;
                const char *tStr = c->fwdErr > 0.08f ? "early" : (c->fwdErr < -0.08f ? "late" : "good");
                const char *dStr = c->side < IDEAL_SIDE - 0.15f ? "jammed" : (c->side > IDEAL_SIDE + 0.15f ? "stretched" : "good");
                const char *hStr = c->height < SHOTS[lastType].idealHeight - 0.2f ? "low"
                                 : (c->height > SHOTS[lastType].idealHeight + 0.2f ? "high" : "good");
                DrawText(TextFormat("Timing  %+.2f m  %-9s %3.0f%%", c->fwdErr, tStr, c->qTiming * 100.0f), 20, y, 18, QualityColor(c->qTiming)); y += 22;
                DrawText(TextFormat("Distance %.2f m  %-9s %3.0f%%", c->side, dStr, c->qDist * 100.0f), 20, y, 18, QualityColor(c->qDist)); y += 22;
                DrawText(TextFormat("Height   %.2f m  %-9s %3.0f%%", c->height, hStr, c->qHeight * 100.0f), 20, y, 18, QualityColor(c->qHeight)); y += 22;
                DrawText(TextFormat("-> %.0f km/h  %.0f rpm  %.1f deg", lastKmh, lastRpm, lastAngle * RAD2DEG), 20, y, 18, RAYWHITE); y += 22;
                if (ball.lastHitter == HITTER_PLAYER) {
                    if (ball.netClearance > -50.0f)
                        DrawText(TextFormat("Net clearance %+.2f m", ball.netClearance), 20, y, 18, RAYWHITE);
                    DrawText(ResultText(ball.result), 300, y, 22, ball.result == RESULT_IN ? GREEN : RED);
                }
                y += 24;
            }
        } else {
            DrawText("Move to the ball, hold J/K/L, release to swing", 20, y, 18, RAYWHITE); y += 24;
        }
        y = 190;
        DrawText(TextFormat("IN  flat %d/%d   top %d/%d   slice %d/%d",
                            stats[SHOT_FLAT][1], stats[SHOT_FLAT][0], stats[SHOT_TOP][1], stats[SHOT_TOP][0],
                            stats[SHOT_SLICE][1], stats[SHOT_SLICE][0]), 20, y, 18, RAYWHITE); y += 26;
        DrawText("WASD move/aim  J top K slice L flat  F feed  C cam  T slow", 20, y, 16, LIGHTGRAY);

        // 溜めゲージ
        if (chargingShot >= 0) {
            const ShotSpec *sp = &SHOTS[chargingShot];
            int w = 300, x = GetScreenWidth() / 2 - w / 2, gy = GetScreenHeight() - 60;
            DrawRectangle(x, gy, w, 20, Fade(BLACK, 0.6f));
            DrawRectangle(x, gy, (int)(w * charge), 20, sp->color);
            DrawText(TextFormat("%s  %.0f km/h", sp->name, Lerp(sp->minKmh, sp->maxKmh, charge)), x, gy - 24, 20, RAYWHITE);
        }
        if (slowMo) DrawText("SLOW x0.25", GetScreenWidth() - 140, 20, 20, RED);
        EndDrawing();
    }

    CloseWindow();
    return 0;
}
