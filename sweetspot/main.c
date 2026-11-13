// SWEET SPOT — 打点と球種がちゃんと結果に効く、球足の速いリアル系テニス
// AI と試合をする（練習モード：ラリー・リターン・サーブもある）。
// 打点（タイミング・体からの距離・高さ）の良し悪しで、球速・回転・コースのずれが決まる。
// 単位はすべてメートル・秒・ラジアン。座標系: x=横, y=上, z=縦（自分側が +z、相手側が -z）
#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"
#include <math.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

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
#define PLAYER_ACCEL     9.0f    // 加速（m/s^2）。最高速まで 0.67 秒
#define PLAYER_BRAKE     20.0f   // 減速（m/s^2）。最高速から 0.3 秒・0.9m で止まる
#define CHARGE_MOVE      0.85f   // 溜めている（テイクバックしている）間の走る速さの割合
// スライド：溜めながら速く走っていて減速・切り返しをすると、足を滑らせながら打てる（止まる必要がない）
#define SLIDE_MIN_SPEED  3.0f    // これより速く走っているとスライドになる
#define SLIDE_DECEL      7.0f    // 滑っている間の減速（m/s^2）。ハードコートで滑るときの摩擦くらい。6m/s から 2.6m 滑る
#define SLIDE_END_SPEED  0.8f
#define SLIDE_REACH      0.30f   // スライド中は低く広い構えになり、リーチがこれだけ伸びる
#define NO_BRAKE         1e6f    // TravelTime に渡すと「止まらずに滑り込む」計算になる
// 打点は「体のまわりの立体的な範囲」で考える。上から見て、体の中心からボールまでの
//   距離 r と、向き θ（真横が 0°、真正面が 90°、後ろがマイナス）で評価する
#define IDEAL_FWD        0.40f   // 理想の打点：体より 40cm 前
#define IDEAL_SIDE       0.85f   // 理想の打点：体から横に 85cm
#define IDEAL_R          0.94f   //   → 体からの距離 94cm
#define IDEAL_ANGLE      25.0f   //   → 向き 25°（横やや前）
#define VOLLEY_ANGLE     40.0f   // ボレーはもっと前で触る
#define TOL_ANG_COMPACT  55.0f   // タイミング（向き）の許容幅。溜めなしのコンパクトなスイング
#define TOL_ANG_FULL     40.0f   //   〃 フルスイング。大きく振るほどシビア
#define VOLLEY_TOL_BONUS 1.30f   // ボレーはテイクバックが小さいので許容幅が広い
#define BEHIND_LIMIT    -55.0f   // これより後ろに入られたら振り遅れ（空振り）
#define REACH_SIDE       2.10f   // これ以上遠いと届かない。腕＋ラケット 1.6m ＋最後の踏み込み 0.5m
#define REACH_DIVE       2.80f   // 走っている勢いで飛びつけば、ここまで届く（打った後は倒れる）
#define DIVE_MIN_SPEED   2.5f    // 飛びつくには、ボールの方向にこの速さ（m/s）以上で走っている必要がある
#define DIVE_QUALITY     0.35f   // 飛びついた球の質は 35% に落ちる
#define DIVE_DOWN_TIME   1.10f   // 飛びついた後、起き上がるまでの時間（秒）
#define OVERHEAD_MIN_H   1.85f   // これより高い球は頭上で打つ（スマッシュ・ハイボレー）
#define OVERHEAD_IDEAL_H 2.55f   // 頭上の理想の打点
#define OVERHEAD_TOL_H   0.70f
#define OVERHEAD_MAX_H   3.05f   // ジャンプして届く高さ
#define OVERHEAD_FWD     0.45f   // 頭上の理想の打点：体より少し前、少し右
#define OVERHEAD_SIDE    0.30f
#define OVERHEAD_TOL_R   0.90f
#define TIMING_PULL      0.15f   // タイミングのずれ 1m あたりの方向ずれ（rad）。早いと引っ張り、遅いと流れる

// ---- 来た球の勢い ----
// ラケットに当たったボールの速さ ≒ EA × 来た球の速さ ＋ (1 + EA) × ラケットの速さ
// （EA はラケットとボールの「見かけの反発係数」。実測で 0.4 前後）
// 球種表の km/h は「REF_IN_KMH の球を打ち返したとき」の値として、そこからラケットの速さを逆算する
#define RACKET_EA        0.40f
#define REF_IN_KMH       60.0f
#define VOLLEY_POWER     0.40f   // ボレーは振らないので、自分の力は 4 割しか乗らない

// ---- 打球のばらつき（標準偏差、度）----
// 実際のばらつき = (BASE + (1 - 質)^2 * Q) × 勢いの倍率
// 2 乗にしているのは、普通にいい当たり（質 60% 前後）ではぶれが小さく、悪い当たりで急に大きくなるように
// 勢いの倍率：ラケット面が δ ずれると、跳ね返る成分は 2δ、自分で振った成分は δ ずれる。
//            なので来た球が速いほど、同じ面のずれでも狙いが大きく狂う（1〜2 倍）
#define ERR_YAW_BASE     0.5f
#define ERR_YAW_Q        5.0f
#define ERR_PITCH_BASE   0.2f
#define ERR_PITCH_Q      3.5f
// 芯を外したときのラケットのねじれ。来た球が重い（速い）ほど大きい
// 倍率 = 1 + (1 - 質) × (来た球の速さ - TWIST_FREE_KMH) / TWIST_KMH
// ラリーの普通の球（45km/h 以下）では効かず、速いサーブや強打で効く
#define MISHIT_PLAN      0.35f   // 芯を外して遅くなった分を、角度でどれだけ補うか（1=補わない＝短く落ちる、0=全部補う＝打ち上がる）
#define TWIST_FREE_KMH   45.0f
#define TWIST_KMH        18.0f

// ---- 踏み込み ----
// 前に踏み込みながら打つと、体重が乗って球が重くなり、ラケットもぶれにくい。下がりながらだと逆
#define STEP_FULL        3.0f    // この速さ（m/s）で踏み込むと効果が最大
#define STEP_POWER_KMH   12.0f   // 最大の踏み込みでラケットの速さに上乗せされる量
#define STEP_STABILITY   0.35f   // 最大の踏み込みで、ねじれ・ばらつきが 35% 減る（下がると 35% 増える）

// ---- サーブ ----
#define SERVE_REACH      2.80f   // 腕とラケットを伸ばした打点の高さ
#define SERVE_TOL_H      0.30f   // 打点の高さがこれだけずれると質 0
#define SERVE_OVERREACH  0.25f   // 理想の打点よりこれだけ高い球まで、背伸びして届く。トスの最高点（2.95m）は必ず届く
#define TOSS_SPEED       4.55f   // トスの初速（上向き、m/s）。手から 1.9m で離すと最高点 2.95m（届く高さの中）
#define TOSS_SPREAD      0.06f   // トスのぶれ（標準偏差、m）。悪いトスは見送れる（押しっぱなしで落とす）
#define TOSS_TOL         0.50f   // トスがこれだけずれると質 0

typedef enum { RESULT_NONE, RESULT_IN, RESULT_OUT, RESULT_NET } ShotResult;
// 前の3つが通常ショット、後ろの3つが Shift を押しながら打つタッチショット
typedef enum {
    SHOT_FLAT, SHOT_TOP, SHOT_SLICE,             // L / J / K
    SHOT_SLICE_LOB, SHOT_TOP_LOB, SHOT_DROP,     // Shift + L / J / K
    SHOT_COUNT
} ShotType;
#define SHOT_NORMAL_COUNT 3
typedef enum { HITTER_NONE, HITTER_OPP, HITTER_PLAYER } Hitter;   // OPP = 相手（球出しマシンか AI）

// 球種ごとの性格。溜め 0→1 で min→max に変わる
// rpm はプラスがトップスピン、マイナスがバックスピン（スライス）
// idealHeight / tolHeight: その球種が一番打ちやすい打点の高さと、許容幅
// depth / shortDepth / deepDepth: 狙う深さ（何も押さない / S / W）
// highArc: 同じ着地点に落ちる2つの弾道のうち、高い方を選ぶ（守りのロブ）
// touchAngle: 0 以外なら、打ち出し角をこの角度に固定して「届く速さ」を逆算する（ドロップ）。
//             このとき min/maxKmh は使わず、溜めは回転量だけに効く
typedef struct {
    const char *name;
    float minKmh, maxKmh;
    float minRpm, maxRpm;
    float idealHeight, tolHeight;
    float tolSide;          // 体からの距離の許容幅。スライスは腕を伸ばしても打てるので広い
    float depth, shortDepth, deepDepth;
    bool  highArc;
    float touchAngle;
    Color color;
} ShotSpec;

static const ShotSpec SHOTS[SHOT_COUNT] = {
    //                     name         km/h            rpm               高さ(理想,許容) 距離許容 深さ(普通, S, W)    高い弾道 固定角
    [SHOT_FLAT]      = { "FLAT",      90.0f, 190.0f,   500.0f,   900.0f, 1.20f, 0.60f, 0.60f,  -9.0f, -6.0f, -10.3f, false, 0.0f, RAYWHITE },
    [SHOT_TOP]       = { "TOPSPIN",   70.0f, 150.0f,  1800.0f,  3200.0f, 1.00f, 0.80f, 0.80f,  -9.0f, -6.0f, -10.3f, false, 0.0f, (Color){ 255, 200, 40, 255 } },
    [SHOT_SLICE]     = { "SLICE",     60.0f, 120.0f, -1200.0f, -2200.0f, 0.75f, 1.00f, 1.10f,  -9.0f, -6.0f, -10.3f, false, 0.0f, (Color){ 80, 220, 255, 255 } },
    [SHOT_SLICE_LOB] = { "SLICE LOB", 50.0f,  65.0f,  -800.0f, -1500.0f, 0.80f, 1.00f, 1.10f, -10.0f, -8.0f, -11.0f, true, 0.0f, (Color){ 150, 160, 255, 255 } },
    [SHOT_TOP_LOB]   = { "TOP LOB",   60.0f,  90.0f,  2000.0f,  3500.0f, 1.00f, 0.80f, 0.80f, -10.0f, -8.0f, -11.0f, false, 0.0f, (Color){ 255, 130, 40, 255 } },
    [SHOT_DROP]      = { "DROP",       0.0f,   0.0f, -1500.0f, -2500.0f, 0.90f, 0.90f, 0.90f,  -2.5f, -1.8f,  -4.0f, false, 28.0f, (Color){ 200, 120, 255, 255 } },
};

typedef struct {
    Vector3 pos;
    Vector3 vel;
    Vector3 spin;          // 角速度ベクトル（rad/s）。向き＝回転軸、長さ＝回転の速さ
    bool    active;
    bool    hitNet;
    int     side;          // 狙った側：-1=相手コート(z<0)、+1=自分のコート(z>0)
    int     shotId;        // 自分の打球の通し番号（プレイ記録で結果と結び付ける）
    int     bounces;
    float   netClearance;  // ネット上を通過したときの余裕（m）。マイナスならネットに当たった
    float   peakAfterFirstBounce;
    ShotResult result;
    Hitter  lastHitter;
    float   age;           // 打たれてからの時間（秒）
    bool    isServe;       // サーブならサービスボックスで判定する
    float   boxX0, boxX1;  // サービスボックスの横の範囲
} Ball;

// 打点の評価結果
typedef enum { MISS_NONE, MISS_EARLY, MISS_LATE, MISS_FAR, MISS_HIGH, MISS_LOW, MISS_NO_BALL } MissReason;
typedef struct {
    MissReason miss;
    bool  forehand;
    bool  overhead;    // 頭上で打った（スマッシュ・ハイボレー）
    bool  dive;        // 飛びついて打った
    float angleDeg;    // 上から見たボールの向き（真横 0°、正面 90°）
    float fwdErr;      // プラス＝早い（ボールがまだ前）、マイナス＝遅い（食い込まれた）
    float timeErrMs;   // fwdErr を時間に直したもの（参考表示用）
    float inKmh;       // 来た球の速さ
    bool  volley;      // ノーバウンドで打った
    float stepIn;      // 打つ瞬間に前（ネット方向）へ踏み込んでいた速さ（m/s）。マイナスは下がりながら
    float side;        // 体からの横距離
    float height;      // 打点の高さ
    float qTiming, qDist, qHeight;
    float quality;     // 0〜1。3つの積
} Contact;

// 見た目用：体とラケットの動き
typedef struct {
    float   swingT;        // 振ってからの時間（秒）。マイナスなら振っていない
    Contact lastContact;   // 最後のスイングの評価
    Vector3 contactLocal;  // 振った瞬間のボールの位置（体から見た座標：x=右, y=高さ, z=前）
    float   downT;         // 飛びついて倒れている残り時間
    float   windup;        // 溜め（0〜1）
    int     windStroke;    // 構え：0=フォア 1=バック 2=頭上
    float   runPhase;      // 走りの足の動き
    bool    sliding;       // 滑っている（低く広い構え）
    float   slideSide;     // 滑っている向き（体から見て右 +1、左 -1）
} SwingAnim;

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

// 試し打ちして、ネット上の余裕を返す（ネットに届く前に落ちたら大きなマイナス）
static float PredictNetClearance(Vector3 from, Vector3 dirFlat, float speed, float angle, float rpm)
{
    Ball b = MakeShot(from, dirFlat, speed, angle, rpm);
    for (int i = 0; i < 240 * 6 && b.active && b.bounces == 0; i++) {
        StepBall(&b, PHYS_DT);
        if (b.netClearance > -50.0f) return b.netClearance;
    }
    return -10.0f;
}

#define MIN_ANGLE  (-15.0f * DEG2RAD)
#define MAX_ANGLE  ( 75.0f * DEG2RAD)

// 狙った距離に落ちる打ち出し角を求める
// 飛距離は角度に対して「上がって下がる」山なので、同じ距離に落ちる角度は2つある。
//   high=false : 低い方（普通のショット）
//   high=true  : 高い方（守りのロブ）
// 無理な場合:
//   どの角度でも届かない → 一番遠くまで飛ぶ角度（それでも短い）
//   ネットを越えてその距離に落とせない → ネットを少し余裕を持って越える角度（＝アウト）
static float SolveLaunchAngle(Vector3 from, Vector3 dirFlat, float dist, float speed, float rpm, bool high)
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
static float SolveSpeed(Vector3 from, Vector3 dirFlat, float dist, float angle, float rpm)
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
static Ball AimedShot(Vector3 from, Vector3 target, float kmh, float rpm, bool high, float *outAngle)
{
    Vector3 d = { target.x - from.x, 0.0f, target.z - from.z };
    Vector3 dirFlat = Vector3Normalize(d);
    float ang = SolveLaunchAngle(from, dirFlat, Vector3Length(d), kmh / 3.6f, rpm, high);
    if (outAngle) *outAngle = ang;
    return MakeShot(from, dirFlat, kmh / 3.6f, ang, rpm);
}

// ---- 打点の評価 ----
// player はプレイヤーの足元の位置。相手コート（-z）を向いて立っている
// charge が大きい（大きく振る）ほど、タイミングの許容幅が狭くなる
// moveToward: ボールの方向へ走っている速さ（m/s）。速ければ飛びつける
// reachBonus: スライド中などで伸びるリーチ（m）
static Contact EvaluateContact(Vector3 player, const Ball *ball, ShotType type, float charge, float moveToward, float reachBonus)
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
    c.timeErrMs = c.fwdErr / fmaxf(Vector3Length(ball->vel), 1.0f) * 1000.0f;
    c.overhead = bp.y > OVERHEAD_MIN_H;

    // 届くか
    if (bp.y > OVERHEAD_MAX_H) { c.miss = MISS_HIGH; return c; }
    if (bp.y < 0.06f)          { c.miss = MISS_LOW;  return c; }
    if (!c.overhead && ang < BEHIND_LIMIT) { c.miss = MISS_LATE; return c; }
    float reach = c.overhead ? 1.6f : REACH_SIDE + reachBonus;
    if (r > reach) {
        bool canDive = !c.overhead && bp.y < 1.6f && r <= REACH_DIVE + reachBonus && moveToward >= DIVE_MIN_SPEED;
        if (!canDive) { c.miss = (ang > 55.0f) ? MISS_EARLY : (ang < -20.0f ? MISS_LATE : MISS_FAR); return c; }
        c.dive = true;
    }

    const ShotSpec *sp = &SHOTS[type];
    if (c.overhead) {
        // 頭上：高さがタイミング（落ちてくる球をどこで叩くか）、体の前右の点からの距離が位置
        float ox = fabsf(dx) - OVERHEAD_SIDE, oz = dz - OVERHEAD_FWD;
        c.qTiming = Clamp(1.0f - fabsf(bp.y - OVERHEAD_IDEAL_H) / OVERHEAD_TOL_H, 0.10f, 1.0f);
        c.qDist   = Clamp(1.0f - sqrtf(ox * ox + oz * oz) / OVERHEAD_TOL_R, 0.10f, 1.0f);
        c.qHeight = 1.0f;
    } else {
        float idealAng = c.volley ? VOLLEY_ANGLE : IDEAL_ANGLE;
        float tol = Lerp(TOL_ANG_COMPACT, TOL_ANG_FULL, charge) * (c.volley ? VOLLEY_TOL_BONUS : 1.0f);
        c.qTiming = Clamp(1.0f - fabsf(ang - idealAng) / tol, 0.10f, 1.0f);
        c.qDist   = Clamp(1.0f - fabsf(fminf(r, REACH_SIDE) - IDEAL_R) / (sp->tolSide + reachBonus), 0.10f, 1.0f);   // 届けば最低限は当たる
        c.qHeight = Clamp(1.0f - fabsf(bp.y - sp->idealHeight) / sp->tolHeight, 0.10f, 1.0f);
    }
    c.quality = c.qTiming * c.qDist * c.qHeight;
    if (c.dive) c.quality *= DIVE_QUALITY;
    return c;
}

// 打点の質から実際の打球を作る
//   質が悪い → 球速と回転が落ちる、狙いが散らばる
//   タイミングのずれ → 早いと引っ張り、遅いと流れる（フォアとバックで逆）
static Ball PlayerShot(const Contact *c, Vector3 ballPos, Vector3 target, ShotType type, float charge,
                       float *outKmh, float *outRpm, float *outAngle)
{
    // 頭上の球は J/L ならスマッシュ（フラット）、K ならスライスで合わせる
    if (c->overhead && type < SHOT_NORMAL_COUNT && type != SHOT_SLICE) type = SHOT_FLAT;
    const ShotSpec *sp = &SHOTS[type];
    float q = c->quality;

    // 球速 = 来た球の跳ね返り + 自分のスイング
    //   芯を外すと（質が低いと）どちらも落ちる。ボレーは自分のスイングがほとんど乗らない
    float specKmh   = Lerp(sp->minKmh, sp->maxKmh, charge);
    float racketKmh = fmaxf(specKmh - RACKET_EA * REF_IN_KMH, 0.0f) / (1.0f + RACKET_EA);
    if (c->volley) racketKmh *= VOLLEY_POWER;
    float step = Clamp(c->stepIn / STEP_FULL, -1.0f, 1.0f);
    racketKmh += fmaxf(step, 0.0f) * STEP_POWER_KMH;
    // 芯を外したときの減速：芯から外れるとラケットの反発が落ちる（実測で 10〜20% くらい）
    float paceKmh  = RACKET_EA * c->inKmh * (0.75f + 0.25f * q);
    float swingKmh = (1.0f + RACKET_EA) * racketKmh * (0.80f + 0.20f * q);
    float kmh = paceKmh + swingKmh;
    float errMul = (2.0f * paceKmh + swingKmh) / fmaxf(kmh, 1.0f)
                 * (1.0f + (1.0f - q) * fmaxf(c->inKmh - TWIST_FREE_KMH, 0.0f) / TWIST_KMH)
                 * (1.0f - STEP_STABILITY * step);
    float rpm = Lerp(sp->minRpm, sp->maxRpm, charge) * (0.60f + 0.40f * q);
    // 狙った弾道は「芯で捉えたときの速さと回転」で決まる。芯を外すと遅く・回転も少なくなるので、
    // 同じ角度で飛び出しても短く・低く落ちる（ネットや浅い球になる）。
    // ※ 遅くなった分を角度で補うと、ミスショットほど高く打ち上がってしまう
    float planKmh = RACKET_EA * c->inKmh + (1.0f + RACKET_EA) * racketKmh;
    float planRpm = Lerp(sp->minRpm, sp->maxRpm, charge);

    // 狙いをずらす：横方向（タイミング＋ばらつき）
    float hand = c->forehand ? 1.0f : -1.0f;
    float yaw = hand * c->fwdErr * TIMING_PULL
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
        // 打った瞬間の感触で少しだけ補正する（遅くなった分の半分）
        ang = SolveLaunchAngle(ballPos, dirFlat, dist, Lerp(kmh, planKmh, MISHIT_PLAN) / 3.6f,
                               Lerp(rpm, planRpm, MISHIT_PLAN), sp->highArc);
        // 縦方向のばらつき（ネットやアウトの原因）。完璧な打点でもゼロにはならない
        ang += RandNormal() * (ERR_PITCH_BASE + (1.0f - q) * (1.0f - q) * ERR_PITCH_Q) * errMul * DEG2RAD;
    }

    if (outKmh) *outKmh = kmh;
    if (outRpm) *outRpm = rpm;
    if (outAngle) *outAngle = ang;
    return MakeShot(ballPos, dirFlat, kmh / 3.6f, ang, rpm);
}

// ================= サーブ =================
// 回転は3成分：
//   top  : トップスピン（進行方向に対して前回り）。下に落ちる
//   side : 縦軸まわりの横回転。プラスで打った人から見て左に曲がる（右利きのスライス）
//   gyro : 進行方向まわりの回転。空中では曲がらないが、バウンドで横に跳ねる（キックが跳ねる理由）
typedef enum { SERVE_FLAT, SERVE_SLICE, SERVE_KICK, SERVE_COUNT } ServeType;
typedef struct {
    const char *name;
    float minKmh, maxKmh;
    float top0, top1, side0, side1, gyro0, gyro1;   // 溜め 0 → 1
    Color color;
} ServeSpec;

static const ServeSpec SERVES[SERVE_COUNT] = {
    [SERVE_FLAT]  = { "FLAT SERVE",  150.0f, 215.0f,  600.0f, 1200.0f,     0.0f,     0.0f,    0.0f,    0.0f, RAYWHITE },
    [SERVE_SLICE] = { "SLICE SERVE", 140.0f, 190.0f,  300.0f,  600.0f,  1800.0f,  3000.0f,    0.0f,    0.0f, (Color){ 80, 220, 255, 255 } },
    [SERVE_KICK]  = { "KICK SERVE",  120.0f, 170.0f, 2500.0f, 3800.0f,  -400.0f,  -800.0f, 1200.0f, 2200.0f, (Color){ 255, 160, 40, 255 } },
};

static Vector3 SpinVector(Vector3 dirFlat, float top, float side, float gyro)
{
    Vector3 axTop = Vector3Normalize(Vector3CrossProduct((Vector3){ 0, 1, 0 }, dirFlat));
    Vector3 w = Vector3Scale(axTop, top);
    w = Vector3Add(w, (Vector3){ 0.0f, side, 0.0f });
    w = Vector3Add(w, Vector3Scale(dirFlat, gyro));
    return Vector3Scale(w, RPM_TO_RADS);
}

static Ball MakeServeBall(Vector3 from, Vector3 dirFlat, float speed, float pitch, Vector3 spinRpm)
{
    Ball b = MakeShot(from, dirFlat, speed, pitch, 0.0f);
    b.spin = SpinVector(dirFlat, spinRpm.x, spinRpm.y, spinRpm.z);
    b.isServe = true;
    return b;
}

static Ball FlyToFirstBounce(Ball b)
{
    for (int i = 0; i < 240 * 4 && b.active && b.bounces == 0; i++) StepBall(&b, PHYS_DT);
    return b;
}

// 横回転があると曲がるので、左右の向きと打ち出し角を交互に合わせて狙いを決める
// 無理な場合はネットを 5cm 越える角度にする（＝ロング）
static void SolveServe(Vector3 from, Vector3 target, float speed, Vector3 spinRpm, Vector3 *outDir, float *outPitch)
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
static void ServiceBox(float serverX, int side, float *x0, float *x1)
{
    // サーバーが x>0 にいたら、対角のボックスは x<0 側
    if (serverX > 0.0f) { *x0 = -SINGLES_W * 0.5f; *x1 = 0.0f; }
    else                { *x0 = 0.0f;              *x1 = SINGLES_W * 0.5f; }
    (void)side;
}

// サーブを打つ。charge: 溜め、q: 打点の質（1 で完璧）
//   速く打つほど（溜めるほど）ばらつきが大きい。打点が低いと物理的に入りにくい
static Ball HitServe(Vector3 contact, Vector3 target, ServeType type, float charge, float q,
                     float x0, float x1, float *outKmh)
{
    const ServeSpec *sp = &SERVES[type];
    float kmh = Lerp(sp->minKmh, sp->maxKmh, charge) * (0.80f + 0.20f * q);
    float sk = 0.70f + 0.30f * q;
    Vector3 spin = { Lerp(sp->top0, sp->top1, charge) * sk, Lerp(sp->side0, sp->side1, charge) * sk,
                     Lerp(sp->gyro0, sp->gyro1, charge) * sk };
    Vector3 dir; float pitch;
    SolveServe(contact, target, kmh / 3.6f, spin, &dir, &pitch);
    float effort = 0.5f + 0.8f * charge;   // 振り切るほどぶれる
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
static Ball MachineServe(void)
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
// pro=false: 打ち出し 80〜110km/h くらい（スクール）、pro=true: 105〜140km/h（プロのラリー）
static Ball MachineFeed(bool pro)
{
    Vector3 from   = { RandRange(-1.0f, 1.0f), 1.0f, -HALF_LEN - 0.5f };
    Vector3 target = { RandRange(-3.2f, 3.2f), 0.0f, RandRange(5.5f, 10.5f) };
    ShotType t = (ShotType)(rand() % SHOT_NORMAL_COUNT);
    float charge = pro ? RandRange(0.5f, 0.8f) : RandRange(0.1f, 0.5f);
    float kmh = Lerp(SHOTS[t].minKmh, SHOTS[t].maxKmh, charge);
    if (pro) {
        const float lo[SHOT_NORMAL_COUNT] = { 115.0f, 105.0f,  90.0f };   // フラット / トップ / スライス
        const float hi[SHOT_NORMAL_COUNT] = { 140.0f, 135.0f, 115.0f };
        kmh = RandRange(lo[t], hi[t]);
    }
    float rpm = Lerp(SHOTS[t].minRpm, SHOTS[t].maxRpm, charge);
    Ball b = AimedShot(from, target, kmh, rpm, false, NULL);
    b.lastHitter = HITTER_OPP;
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


// ================= 体とラケットの描画 =================
// 体から見た座標（x=右、y=高さ、z=前）をワールド座標に直す。facingNegZ: -z 向き（自分）か +z 向き（AI）か
static Vector3 LocalToWorld(Vector3 base, bool facingNegZ, Vector3 l)
{
    float sx = facingNegZ ? 1.0f : -1.0f;
    return (Vector3){ base.x + sx * l.x, base.y + l.y, base.z - sx * l.z };
}

static float EaseOut(float u) { u = Clamp01(u); return 1.0f - (1.0f - u) * (1.0f - u); }

#define SWING_TIME 0.30f   // フォロースルーまでの時間（見た目）

// ラケットの面の中心（ローカル座標）と、ラケットが動いている向き
static Vector3 RacketLocal(const SwingAnim *a, int *strokeOut, Vector3 *tangent)
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
        float phi = Lerp(60.0f, BACK_FH, w) * DEG2RAD;
        *tangent = (Vector3){ -sgn * sinf(phi), 0.0f, cosf(phi) };
        return (Vector3){ sgn * cosf(phi) * Lerp(0.6f, 0.85f, w), Lerp(1.0f, 1.25f, w), sinf(phi) * Lerp(0.6f, 0.85f, w) };
    }
    *strokeOut = 0;
    *tangent = (Vector3){ 0.0f, 0.0f, 1.0f };
    return (Vector3){ 0.25f, 1.0f, 0.55f };   // 構え：ラケットを前に
}

// ラケットの面（円）を描く。normal: 面の向き
static void DrawRacketHead(Vector3 center, Vector3 normal, Color frame)
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

static void DrawFigure(Vector3 pos, bool facingNegZ, Color shirt, const SwingAnim *a, float speed)
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
    Vector3 shoulder = (stroke == 1) ? (Vector3){ -0.22f, 1.42f, 0.0f } : (Vector3){ 0.22f, 1.42f, 0.0f };
    Vector3 dir = Vector3Normalize(Vector3Subtract(rl, shoulder));
    Vector3 hand = Vector3Subtract(rl, Vector3Scale(dir, 0.55f));
    float armLen = Vector3Length(Vector3Subtract(hand, shoulder));
    if (armLen > 0.75f) hand = Vector3Add(shoulder, Vector3Scale(Vector3Normalize(Vector3Subtract(hand, shoulder)), 0.75f));
    Vector3 neck = Vector3Subtract(rl, Vector3Scale(dir, 0.14f));
    Vector3 other = (stroke == 1) ? (Vector3){ 0.22f, 1.42f, 0.0f } : (Vector3){ -0.22f, 1.42f, 0.0f };
    Vector3 otherHand = { other.x * 1.4f, 1.0f, 0.2f };

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
        case MISS_LOW:     return "WHIFF: too low";
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


// ================= 相手（AI） =================
// AI は自分（プレイヤー）とまったく同じ物理・同じ打点の評価・同じショットの関数を使う。
// コートを縦軸まわりに 180° 回した「AI から見た座標」に変換して、プレイヤー用の関数をそのまま呼ぶ。
// 180° 回転なので、位置・速度・回転（角速度）すべて (x, y, z) → (-x, y, -z) で変換できる
static Vector3 Rot180(Vector3 v) { return (Vector3){ -v.x, v.y, -v.z }; }

static Ball RotBall(Ball b)
{
    b.pos = Rot180(b.pos);
    b.vel = Rot180(b.vel);
    b.spin = Rot180(b.spin);
    b.side = -b.side;
    float x0 = b.boxX0, x1 = b.boxX1;
    b.boxX0 = -x1; b.boxX1 = -x0;
    return b;
}

typedef struct {
    const char *name;
    float speed;          // 走る速さ（m/s）
    float reaction;       // 相手が打ってから動き出すまで（秒）
    float timingSigmaMs;  // スイングのタイミングのばらつき
    float posErr;         // 立ち位置のずれ（m）
    float aggression;     // 0〜1。強打・コースの厳しさ
    float lineJudge;      // ライン際の球の見極めのあまさ（m）。これより際どいアウトは打ってしまうことがある
} AiLevel;

static const AiLevel AI_LEVELS[3] = {
    { "EASY",   5.0f, 0.35f, 30.0f, 0.35f, 0.25f, 0.40f },
    { "NORMAL", 5.6f, 0.28f, 20.0f, 0.20f, 0.50f, 0.20f },
    { "PRO",    6.2f, 0.22f, 12.0f, 0.10f, 0.75f, 0.08f },
};

typedef struct {
    Vector3 pos;          // 足元（ワールド座標）
    Vector3 vel;
    Vector3 standPos;     // 向かっている位置（ワールド座標）
    bool    planned;      // 打つ計画がある
    bool    leaving;      // アウトと判断して見送る
    bool    swung;        // この球にはもう振った（空振りでも振り直さない）
    float   swingAge;     // ボールの age がこの値になったら振る
    float   seeTimer;     // 相手が打ってから反応するまでの残り時間
    Hitter  watching;     // 最後に見た打者（打者が変わったら反応を始める）
    int     level;
    float   downTimer;    // 飛びついて倒れている間は動けない
    bool    sliding;      // 滑り込んでいる
    bool    planFore;     // フォアで打つ予定
    SwingAnim anim;
    // 最後のショット（HUD 用）
    ShotType lastType;
    float   lastQ, lastKmh;
} Opponent;

// 速さ v0（目的地方向の成分）から距離 d を走って止まるまでの時間
// 加速 a で最高速 vmax まで上げ、最後は減速 b で止まる（台形の速度変化）
static float TravelTime(float d, float v0, float vmax, float a, float b)
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
static Vector3 Accelerate(Vector3 vel, Vector3 want, float a, float b, float dt)
{
    Vector3 dv = Vector3Subtract(want, vel);
    float len = Vector3Length(dv);
    float lim = (Vector3Length(want) < Vector3Length(vel) - 0.01f) ? b : a;
    float maxDv = lim * dt;
    if (len > maxDv) dv = Vector3Scale(dv, maxDv / len);
    return Vector3Add(vel, dv);
}

// 目的地に向かって走る（止まれる速さに落としながら）
static Vector3 MoveToward(Vector3 *pos, Vector3 vel, Vector3 target, float vmax, float dt)
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
static Ball FlipSides(Ball b)
{
    b = RotBall(b);
    if (b.lastHitter == HITTER_PLAYER) b.lastHitter = HITTER_OPP;
    else if (b.lastHitter == HITTER_OPP) b.lastHitter = HITTER_PLAYER;
    return b;
}

// AI のホームポジション（ワールド座標）
static Vector3 AiHome(void) { return (Vector3){ 0.0f, 0.0f, -(HALF_LEN + 0.8f) }; }

// リターンの構え位置（自分側の座標。AI は Rot180 して使う）。ボックスの外寄り、ベースラインの 1.3m 後ろ
static Vector3 ReturnPos(bool deuce) { return (Vector3){ deuce ? 2.3f : -2.3f, 0.0f, HALF_LEN + 1.3f }; }

// 飛んでくる球を予測して、どこで打つかを決める（AI 座標で考える）
static void AiPlan(Opponent *ai, const Ball *worldBall)
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
            ai->standPos = AiHome();
            return;
        }
    }

    // 2) バウンド後の軌道の中から、間に合って、打ちやすい高さの点を選ぶ
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
        if (i % 2 != 0 || q.bounces != 1 || q.pos.z <= 0.0f) continue;
        // フォアかバックか、近い方で構える
        Vector3 fore = { q.pos.x - IDEAL_SIDE, 0.0f, q.pos.z + IDEAL_FWD };
        Vector3 back = { q.pos.x + IDEAL_SIDE, 0.0f, q.pos.z + IDEAL_FWD };
        Vector3 stand = (Vector3Distance(fore, me) <= Vector3Distance(back, me)) ? fore : back;
        float cost = fabsf(q.pos.y - 1.0f) + 0.4f * fmaxf(q.pos.z - (HALF_LEN + 2.0f), 0.0f);
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
static ShotType AiChooseShot(const Opponent *ai, const Contact *c, Vector3 playerAi, Vector3 meAi, float *charge,
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
static bool AiSwing(Opponent *ai, Ball *ball, Vector3 player)
{
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
    c = EvaluateContact(me, &b, t, charge, moveToward, bonus);
    c.stepIn = -meVel.z;
    ai->anim.lastContact = c;
    ai->anim.contactLocal = (Vector3){ b.pos.x - me.x, b.pos.y, me.z - b.pos.z };
    ai->anim.swingT = 0.0f;
    if (c.miss != MISS_NONE) return false;
    if (c.dive) { ai->downTimer = DIVE_DOWN_TIME; ai->anim.downT = DIVE_DOWN_TIME; }

    float kmh;
    Ball out = PlayerShot(&c, b.pos, target, t, charge, &kmh, NULL, NULL);
    out.lastHitter = HITTER_OPP;
    *ball = RotBall(out);
    ai->lastType = t;
    ai->lastQ = c.quality;
    ai->lastKmh = kmh;
    return true;
}

// AI のサーブ。deuce: デュースサイドから打つ。first: ファーストサーブ
static Ball AiServe(Opponent *ai, bool deuce, bool first)
{
    const AiLevel *lv = &AI_LEVELS[ai->level];
    // AI 座標では自分と同じ：デュースサイドは x>0、対角のボックスは x<0
    Vector3 me = { deuce ? 0.6f : -0.6f, 0.0f, HALF_LEN + 0.15f };
    ai->pos = Rot180(me);
    // トスのぶれとタイミングのずれから、打点の高さと質を決める
    Vector2 off = { RandNormal() * TOSS_SPREAD, RandNormal() * TOSS_SPREAD };
    float hErr = fabsf(RandNormal() * lv->timingSigmaMs / 1000.0f * 2.4f);   // 落ちてくる速さ 2.4m/s 前後
    float q = Clamp01(1.0f - hErr / SERVE_TOL_H) * Clamp01(1.0f - Vector2Length(off) / TOSS_TOL);
    Vector3 contact = { me.x + 0.25f + off.x, SERVE_REACH - hErr, me.z - 0.35f + off.y };

    ServeType t;
    float charge;
    if (first) { t = (rand() % 2) ? SERVE_FLAT : SERVE_SLICE; charge = 0.55f + 0.40f * lv->aggression + RandRange(-0.05f, 0.05f); }
    else       { t = SERVE_KICK; charge = RandRange(0.45f, 0.65f); }
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
    ai->planned = false;
    return RotBall(b);
}

// 毎フレーム：反応して、計画を立て、走る
static void AiUpdate(Opponent *ai, const Ball *ball, float dt)
{
    const AiLevel *lv = &AI_LEVELS[ai->level];
    if (ball->lastHitter != ai->watching) {
        ai->watching = ball->lastHitter;
        ai->planned = false;
        ai->leaving = false;
        ai->swung = false;
        // サーブはトスとフォームで読める（スプリットステップ）ので、反応は半分
        if (ball->lastHitter == HITTER_PLAYER) ai->seeTimer = ball->isServe ? lv->reaction * 0.5f : lv->reaction;
        else ai->standPos = AiHome();   // 自分が打った後：戻る
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
    } else {
        ai->anim.windup = 0.0f;
    }
}

// ================= 試合のスコア =================
typedef struct {
    int points[2];   // 0=自分, 1=AI
    int games[2];
    int sets[2];
    bool tiebreak;
    int server;      // 0=自分, 1=AI（ゲームの最初のサーバー）
    bool over;
} Score;

static const char *PointName(int p) { static const char *n[] = { "0", "15", "30", "40" }; return p < 4 ? n[p] : "?"; }

// このポイントのサーバー（タイブレークは 1, 2, 2, 2... で交代）
static int CurrentServer(const Score *s)
{
    if (!s->tiebreak) return s->server;
    int n = s->points[0] + s->points[1];
    if (n == 0) return s->server;
    return (((n - 1) / 2) % 2 == 0) ? 1 - s->server : s->server;
}

// デュースサイドか（ゲーム内のポイント数が偶数ならデュース）
static bool DeuceCourt(const Score *s) { return (s->points[0] + s->points[1]) % 2 == 0; }

static void AwardPoint(Score *s, int w)
{
    int l = 1 - w;
    s->points[w]++;
    bool gameWon;
    if (s->tiebreak) gameWon = s->points[w] >= 7 && s->points[w] - s->points[l] >= 2;
    else             gameWon = s->points[w] >= 4 && s->points[w] - s->points[l] >= 2;
    if (!gameWon) {
        // デュース後は 3-3 に戻して表示を簡単にする（アドバンテージは 4-3）
        if (!s->tiebreak && s->points[0] >= 4 && s->points[1] >= 4) { s->points[0]--; s->points[1]--; }
        return;
    }
    s->games[w]++;
    int firstServer = s->server;
    s->points[0] = s->points[1] = 0;
    bool setWon = (s->games[w] >= 6 && s->games[w] - s->games[l] >= 2) || (s->tiebreak);
    s->tiebreak = false;
    s->server = 1 - firstServer;
    if (setWon) {
        s->sets[w]++;
        s->games[0] = s->games[1] = 0;
        s->over = true;   // 今は 1 セットマッチ
    } else if (s->games[0] == 6 && s->games[1] == 6) {
        s->tiebreak = true;
    }
}

static const char *ScoreText(const Score *s)
{
    if (s->tiebreak) return TextFormat("Tiebreak %d - %d", s->points[0], s->points[1]);
    int a = s->points[0], b = s->points[1];
    if (a >= 3 && b >= 3) {
        if (a == b) return "Deuce";
        return (a > b) ? "Advantage You" : "Advantage AI";
    }
    return TextFormat("%s - %s", PointName(a), PointName(b));
}



// ================= 手がかり表示（画面に奥行きがないのを補う） =================
// 物理や判定は変えない。人間が目で読めない「いつ」「どこ」を画面に出すだけ
typedef struct {
    bool    timingValid;  // タイミングの輪を出せる
    float   tBest;        // 今の立ち位置で一番いい瞬間まで（秒）
    float   qBest;        // そのときの質
    bool    fore, overhead;
    bool    standValid;   // 立つ場所の目印を出せる
    Vector3 stand;        // 立つ場所（地面）
    Vector3 ballAt;       // そのときのボール
} Assist;

static Assist ComputeAssist(Vector3 player, Vector3 playerVel, const Ball *ball, ShotType type, float charge)
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

// ================= プレイ記録（playlog.csv） =================
// 自分のプレイを 1 行ずつ CSV に書き出す。どこで失敗しているかを数字で調べるため。
//   event = swing    : 振った（当たった・空振り）
//           no_swing : 来た球に振らなかった（届いたのか、届かなかったのか）
//           serve    : サーブ
// timing_offset_ms と best_quality_here が一番大事：
//   「その立ち位置のまま、一番いいタイミングで振っていたら質は何%だったか」と「そこから何 ms ずれていたか」
//   best_quality_here が低い → 立ち位置（足）の問題。高いのに quality が低い → タイミング（手）の問題
#define LOG_RING 256
typedef struct {
    FILE  *f;
    int    shotSerial;
    bool   pending;          // 結果待ちの行（打球がバウンドしたら書き出す）
    int    pendingId;
    bool   pendingServe;
    char   row[1024];
    // 来た球の追跡
    bool   track, swung;
    float  prevAge;
    Hitter prevHitter;
    float  minR, angAtMin, hAtMin, ageAtMin, kmhAtMin;
    float  firstMove, chargeStart;
    ShotResult bounceResult;
    Ball   ring[LOG_RING];   // 来た球の最近の状態（タイミング分析用）
    int    ringN, ringHead;
    // このセッションの集計（Tab で表示）
    int    swings, hits, whiffs, miss[8], shotIn, shotOut, shotNet;
    int    early, late, onTime, posProblem, timingProblem;
    float  sumAbsOffset; int nOffset;
    float  sumQ, sumBestQ;
    int    noSwingIn, noSwingReachable, noSwingOut;
    float  sumFirstMove; int nFirstMove;
    int    serves, servesIn, serveWhiffs;
    int    assist;           // 手がかり表示のレベル（記録の最後の列）
} PlayLog;

#define LOG_HEADER "time_s,mode,ai_level,event,shot,charge,stroke,volley,dive,miss,quality,q_timing,q_dist,q_height," \
                   "angle_deg,dist_m,height_m,timing_offset_ms,best_quality_here,in_kmh,out_kmh,step_in," \
                   "player_x,player_z,player_speed,ball_x,ball_y,ball_z,ball_age_s,first_move_s,charge_start_s,result,assist"

static void LogOpen(PlayLog *L)
{
    // 列が違う古い記録は、別の名前にして残す（混ざると集計できないので）
    FILE *old = fopen("playlog.csv", "r");
    if (old) {
        char line[1024] = { 0 };
        if (fgets(line, sizeof(line), old)) line[strcspn(line, "\r\n")] = 0;
        fclose(old);
        if (line[0] && strcmp(line, LOG_HEADER) != 0) rename("playlog.csv", TextFormat("playlog_old_%ld.csv", (long)time(NULL)));
    }
    L->f = fopen("playlog.csv", "a");
    if (!L->f) return;
    fseek(L->f, 0, SEEK_END);
    if (ftell(L->f) == 0)
        fprintf(L->f, LOG_HEADER "\n");
    fflush(L->f);
}

static void LogFlush(PlayLog *L, const char *result)
{
    if (!L->pending) return;
    L->pending = false;
    if (L->pendingServe) { if (strcmp(result, "IN") == 0) L->servesIn++; }
    else {
        if (strcmp(result, "IN") == 0) L->shotIn++;
        else if (strcmp(result, "OUT") == 0) L->shotOut++;
        else if (strcmp(result, "NET") == 0) L->shotNet++;
    }
    if (L->f) { fprintf(L->f, "%s,%s,%d\n", L->row, result, L->assist); fflush(L->f); }
}

// 「この立ち位置のまま、いつ振るのが一番よかったか」を、来た球の過去の軌道と、この先の軌道から探す
static void LogTimingAnalysis(const PlayLog *L, Vector3 player, const Ball *pre, ShotType type, float charge,
                              float *bestQ, float *offsetMs)
{
    *bestQ = 0.0f; *offsetMs = 0.0f;
    float bestAge = -1.0f;
    for (int i = 0; i < L->ringN + 240; i++) {
        Ball b;
        static Ball fwd;
        if (i < L->ringN) b = L->ring[(L->ringHead - L->ringN + i + LOG_RING) % LOG_RING];
        else {
            if (i == L->ringN) fwd = *pre;
            StepBall(&fwd, PHYS_DT);
            b = fwd;
        }
        bool hittable = b.active && b.bounces <= 1 && b.pos.z > 0.0f && !b.hitNet && (!b.isServe || b.bounces == 1);
        if (!hittable) continue;
        Contact c = EvaluateContact(player, &b, type, charge, 0.0f, 0.0f);
        if (c.miss == MISS_NONE && c.quality > *bestQ) { *bestQ = c.quality; bestAge = b.age; }
    }
    if (bestAge >= 0.0f) *offsetMs = (pre->age - bestAge) * 1000.0f;   // プラス＝遅い、マイナス＝早い
}

static const char *StrokeName(const Contact *c) { return c->overhead ? "overhead" : (c->forehand ? "forehand" : "backhand"); }

static const char *MissName(MissReason m)
{
    static const char *n[] = { "", "early", "late", "far", "high", "low", "no_ball" };
    return n[m];
}

// 来た球を毎フレーム追う：振らなかった球の記録、反応の速さ
static void LogTrackIncoming(PlayLog *L, const Ball *ball, Vector3 player, Vector3 playerVel, bool charging,
                             const char *mode, const char *level)
{
    bool opp = ball->active && ball->lastHitter == HITTER_OPP;
    bool newBall = opp && (L->prevHitter != HITTER_OPP || ball->age < L->prevAge);
    bool ended = L->track && (newBall || !opp || ball->bounces >= 2);
    if (ended) {
        if (!L->swung && L->minR < 1e8f) {
            bool reachable = L->minR <= REACH_SIDE && L->hAtMin <= OVERHEAD_MAX_H;
            const char *res = ResultText(L->bounceResult);
            if (L->bounceResult == RESULT_IN || L->bounceResult == RESULT_NONE) {
                L->noSwingIn++;
                if (reachable) L->noSwingReachable++;
            } else {
                L->noSwingOut++;
            }
            if (L->f) {
                fprintf(L->f, "%.2f,%s,%s,no_swing,,,,,,%s,,,,,%.1f,%.2f,%.2f,,,%.0f,,,%.2f,%.2f,,,,,%.2f,%.2f,%.2f,%s,%d\n",
                        GetTime(), mode, level, reachable ? "reachable" : "unreachable",
                        L->angAtMin, L->minR, L->hAtMin, L->kmhAtMin, player.x, player.z,
                        L->ageAtMin, L->firstMove, L->chargeStart, res, L->assist);
                fflush(L->f);
            }
        }
        L->track = false;
    }
    if (newBall) {
        L->track = true; L->swung = false;
        L->minR = 1e9f; L->firstMove = -1.0f; L->chargeStart = -1.0f;
        L->bounceResult = RESULT_NONE;
        L->ringN = 0;
    }
    if (L->track && opp) {
        if (L->firstMove < 0.0f && Vector3Length(playerVel) > 0.5f) {
            L->firstMove = ball->age;
            L->sumFirstMove += ball->age; L->nFirstMove++;
        }
        if (L->chargeStart < 0.0f && charging) L->chargeStart = ball->age;
        if (ball->bounces >= 1 && L->bounceResult == RESULT_NONE) L->bounceResult = ball->result;
        bool hittable = ball->bounces <= 1 && ball->pos.z > 0.0f && !ball->hitNet && (!ball->isServe || ball->bounces == 1);
        if (hittable) {
            float dx = ball->pos.x - player.x, dz = player.z - ball->pos.z;
            float r = sqrtf(dx * dx + dz * dz);
            if (r < L->minR) {
                L->minR = r; L->angAtMin = atan2f(dz, fabsf(dx)) * RAD2DEG; L->hAtMin = ball->pos.y;
                L->ageAtMin = ball->age; L->kmhAtMin = Vector3Length(ball->vel) * 3.6f;
            }
        }
    }
    L->prevHitter = ball->active ? ball->lastHitter : HITTER_NONE;
    L->prevAge = ball->age;
}

// 来た球の状態を物理ステップごとに覚えておく
static void LogRecordBall(PlayLog *L, const Ball *ball)
{
    if (!L->track || ball->lastHitter != HITTER_OPP) return;
    L->ring[L->ringHead] = *ball;
    L->ringHead = (L->ringHead + 1) % LOG_RING;
    if (L->ringN < LOG_RING) L->ringN++;
}

// Windows で日本語入力（IME）を切る（platform_ime.c。Windows 以外では何もしない）
void DisableIme(void *hwnd);

typedef enum { MODE_MATCH, MODE_RALLY, MODE_RETURN, MODE_SERVE, MODE_COUNT } PracticeMode;
static const char *MODE_NAMES[MODE_COUNT] = { "MATCH", "RALLY", "RETURN", "SERVE" };

typedef enum { SV_READY, SV_TOSS, SV_FLIGHT, SV_WAIT } ServeState;
typedef enum { PH_SERVE, PH_PLAY, PH_BETWEEN, PH_POINT_OVER } MatchPhase;

int main(void)
{
    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_WINDOW_RESIZABLE);
    InitWindow(1280, 720, "SWEET SPOT");
    SetTargetFPS(60);
    srand((unsigned)GetTime() ^ 0x5eed);
    DisableIme(GetWindowHandle());   // 日本語入力が ON だとキーを離したことが届かず、押しっぱなしになることがある

    static PlayLog plog = { 0 };   // プレイ記録（playlog.csv）
    LogOpen(&plog);
    bool showStats = false;        // Tab：このセッションの統計
    int  assistLevel = 2;          // H：手がかり表示 2=立つ場所＋タイミング 1=タイミングだけ 0=なし

    Camera3D cam = { 0 };
    cam.up = (Vector3){ 0, 1, 0 };
    cam.fovy = 45.0f;
    cam.projection = CAMERA_PERSPECTIVE;
    int camMode = 0;

    Vector3 player = { 0.0f, 0.0f, HALF_LEN + 0.8f };   // 足元の位置
    Vector3 playerVel = { 0 };
    SwingAnim playerAnim = { .swingT = -1.0f };
    float playerDownT = 0.0f;           // 飛びついて倒れている残り時間
    bool  playerSliding = false;        // 滑り込んでいる
    Vector3 lastIdealPos = { 0 };       // 最後に振ったときの理想の打点（ワールド座標）

    Ball ball = { 0 };
    float feedTimer = 1.0f;          // 0 になったら次の球を出す
    Vector3 trail[TRAIL_N];
    int trailCount = 0;
    float trailTimer = 0.0f;
    Color trailColor = LIGHTGRAY;

    // 溜め打ち：J/K/L を押している間に溜まり、離した瞬間にスイング
    // L=フラット J=トップスピン K=スライス。Shift を押しながらだと スライスロブ / トップスピンロブ / ドロップ
    // サーブのときは L=フラット K=スライス J=キック
    const int shotKeys[SHOT_COUNT] = { KEY_L, KEY_J, KEY_K, KEY_L, KEY_J, KEY_K };
    const int serveKeys[SERVE_COUNT] = { KEY_L, KEY_K, KEY_J };
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
    PracticeMode mode = MODE_MATCH;
    bool proFeed = true;

    // サーブ（練習・試合共通）
    ServeState sv = SV_READY;
    int   serveNo = 1;                  // 1st / 2nd
    bool  deuceSide = true;
    int   chargingServe = -1;
    Vector2 tossOffset = { 0 };
    float svTimer = 0.0f;
    ServeType lastServeType = SERVE_FLAT;
    float lastServeKmh = 0.0f, lastServeH = 0.0f, lastServeQ = 0.0f;
    const char *serveMsg = "";
    bool  serveAce = false;
    int   svStats[2][2] = { 0 };        // [1st/2nd][0=打った数, 1=イン]
    int   aces = 0, doubleFaults = 0;

    // 試合
    Opponent ai = { 0 };
    ai.level = 1;
    Score score = { 0 };
    MatchPhase phase = PH_SERVE;
    bool  serveJudged = false;
    float phaseTimer = 0.0f;
    const char *pointMsg = "";
    char  scoreCall[64] = "";          // ポイントの後に大きく出すコール（DEUCE、GAME など）
    bool  needSetup = true;            // 次のポイント（またはセカンドサーブ）の準備をする
    bool  autopilot = false;           // B：自分側も AI が操作する（観戦モード）
    Opponent bot = { 0 };              // 自分側を操作する AI（座標は 180° 回した世界）

    // 直前のフレームの物理ステップごとのボールの状態。
    // キー入力はフレーム単位（1/60 秒）でしか取れないので、「離したのはこのフレームの間のどこか」として
    // その間で一番いいタイミングの瞬間を採用する（入力の粗さで損をしないように）
    Ball history[64];
    int historyCount = 0;

    // バウンドの目印
    Vector3 bouncePos = { 0 };
    float bounceT = 10.0f;
    int prevBounces = 0;

    while (!WindowShouldClose()) {
        float frameDt = fminf(GetFrameTime(), 0.1f);
        float simDt = frameDt * (slowMo ? 0.25f : 1.0f);

        if (IsKeyPressed(KEY_C)) camMode = (camMode + 1) % 3;
        if (IsKeyPressed(KEY_T)) slowMo = !slowMo;
        if (IsKeyPressed(KEY_V)) proFeed = !proFeed;
        if (IsKeyPressed(KEY_F)) feedTimer = 0.0f;
        bool restart = false;
        if (IsKeyPressed(KEY_G)) { mode = (PracticeMode)((mode + 1) % MODE_COUNT); restart = true; }
        for (int k = 0; k < 3; k++) if (mode == MODE_MATCH && IsKeyPressed(KEY_ONE + k)) { ai.level = k; restart = true; }
        if (mode == MODE_MATCH && score.over && IsKeyPressed(KEY_ENTER)) restart = true;
        if (IsKeyPressed(KEY_B)) autopilot = !autopilot;
        if (IsKeyPressed(KEY_TAB)) showStats = !showStats;
        if (IsKeyPressed(KEY_H)) assistLevel = (assistLevel + 2) % 3;
        plog.assist = assistLevel;
        bot.level = ai.level;
        if (restart) {
            feedTimer = 0.5f;
            ball = (Ball){ 0 };
            chargingShot = -1; chargingServe = -1;
            sv = SV_READY; serveNo = 1; svTimer = 0.0f; serveMsg = "";
            player = (Vector3){ 0.0f, 0.0f, HALF_LEN + 0.8f };
            playerVel = (Vector3){ 0 };
            hasLast = false;
            int lvl = ai.level;
            ai = (Opponent){ 0 };
            ai.level = lvl;
            ai.pos = AiHome();
            ai.anim.swingT = -1.0f;
            playerAnim = (SwingAnim){ .swingT = -1.0f };
            playerDownT = 0.0f;
            playerSliding = false;
            score = (Score){ 0 };
            score.server = rand() % 2;
            needSetup = true;
            pointMsg = "";
            trailCount = 0;
        }

        // ---- 試合：ポイントの準備 ----
        int server = CurrentServer(&score);
        if (mode == MODE_MATCH && needSetup && !score.over) {
            needSetup = false;
            bool deuce = DeuceCourt(&score);
            deuceSide = deuce;
            ball = (Ball){ 0 };
            trailCount = 0;
            ai.planned = false; ai.leaving = false; ai.swung = false; ai.watching = HITTER_NONE;
            ai.vel = (Vector3){ 0 };
            playerVel = (Vector3){ 0 };
            bot.planned = false; bot.leaving = false; bot.swung = false; bot.watching = HITTER_NONE;
            bot.vel = (Vector3){ 0 };
            bot.downTimer = 0.0f; bot.anim = (SwingAnim){ .swingT = -1.0f };
            ai.downTimer = 0.0f; ai.anim = (SwingAnim){ .swingT = -1.0f };
            playerAnim = (SwingAnim){ .swingT = -1.0f };
            playerDownT = 0.0f;
            playerSliding = false;
            ai.sliding = false; bot.sliding = false;
            chargingShot = -1; chargingServe = -1;
            if (server == 0) {
                player = (Vector3){ deuce ? 0.6f : -0.6f, 0.0f, HALF_LEN + 0.15f };
                ai.pos = Rot180(ReturnPos(deuce));
                ai.standPos = ai.pos;
                sv = SV_READY;
            } else {
                player = ReturnPos(deuce);
                ai.pos = Rot180((Vector3){ deuce ? 0.6f : -0.6f, 0.0f, HALF_LEN + 0.15f });
                ai.standPos = ai.pos;
                phaseTimer = 1.5f;
            }
            phase = PH_SERVE;
            serveJudged = false;
            bot.pos = Rot180(player);
            bot.standPos = bot.pos;
            if (autopilot && server == 0) phaseTimer = 1.5f;
            TraceLog(LOG_INFO, "POINT start: server=%s %s court, %s serve, games %d-%d, %s",
                     server == 0 ? "YOU" : "AI", deuce ? "deuce" : "ad", serveNo == 1 ? "1st" : "2nd",
                     score.games[0], score.games[1], ScoreText(&score));
        }
        bool playerServing = (mode == MODE_SERVE) || (mode == MODE_MATCH && phase == PH_SERVE && server == 0 && !score.over);
        bool botControls = autopilot && mode == MODE_MATCH && !score.over;

        // ---- オートパイロット：自分側のサーブ ----
        if (botControls && playerServing) {
            phaseTimer -= simDt;
            if (phaseTimer <= 0.0f) {
                ball = FlipSides(AiServe(&bot, DeuceCourt(&score), serveNo == 1));
                player = Rot180(bot.pos);
                trailColor = RAYWHITE;
                trailCount = 0;
                phase = PH_PLAY;
                serveJudged = false;
                sv = SV_FLIGHT;
            }
            playerServing = false;
        }

        // ---- 移動（WASD）。加速・減速あり ----
        Vector3 move = { 0 };
        if (IsKeyDown(KEY_A)) move.x -= 1.0f;
        if (IsKeyDown(KEY_D)) move.x += 1.0f;
        if (IsKeyDown(KEY_W)) move.z -= 1.0f;
        if (IsKeyDown(KEY_S)) move.z += 1.0f;
        if (botControls) {
            // オートパイロット：AI と同じ関数で走る
            if (phase == PH_PLAY || phase == PH_POINT_OVER) {
                Ball fb = FlipSides(ball);
                AiUpdate(&bot, &fb, simDt);
                player = Rot180(bot.pos);
                playerVel = Rot180(bot.vel);
            }
        } else if (playerServing) {
            // サーブ前はベースラインに沿って左右だけ動ける（センターマークを越えない）
            playerVel = (Vector3){ 0 };
            if (sv == SV_READY && move.x != 0.0f) {
                player.x += move.x * 2.0f * frameDt;
                player.x = deuceSide ? Clamp(player.x, 0.2f, SINGLES_W * 0.5f) : Clamp(player.x, -SINGLES_W * 0.5f, -0.2f);
            }
            player.z = HALF_LEN + 0.15f;
        } else if (playerDownT > 0.0f) {
            // 飛びついて倒れている：起き上がるまで動けない
            playerDownT -= simDt;
            playerVel = (Vector3){ 0 };
        } else {
            // 溜めている間は少し遅くなる（構えながら動く）
            float spd = PLAYER_SPEED * (chargingShot >= 0 ? CHARGE_MOVE : 1.0f);
            Vector3 want = (Vector3Length(move) > 0.0f) ? Vector3Scale(Vector3Normalize(move), spd) : (Vector3){ 0 };
            float v = Vector3Length(playerVel);
            // 溜めながら速く走っていて、減速・切り返しをしたら滑り込む
            if (!playerSliding && chargingShot >= 0 && v > SLIDE_MIN_SPEED &&
                Vector3DotProduct(want, Vector3Scale(playerVel, 1.0f / v)) < v * 0.5f)
                playerSliding = true;
            if (playerSliding) {
                float nv = v - SLIDE_DECEL * simDt;
                if (nv < SLIDE_END_SPEED) { playerSliding = false; playerVel = Vector3Scale(playerVel, fmaxf(nv, 0.0f) / fmaxf(v, 1e-4f)); }
                else playerVel = Vector3Scale(playerVel, nv / v);
            } else {
                playerVel = Accelerate(playerVel, want, PLAYER_ACCEL, PLAYER_BRAKE, simDt);
            }
            player = Vector3Add(player, Vector3Scale(playerVel, simDt));
            player.x = Clamp(player.x, -DOUBLES_W * 0.5f - 3.0f, DOUBLES_W * 0.5f + 3.0f);
            player.z = Clamp(player.z, 0.5f, HALF_LEN + 5.0f);
        }

        // 見た目のタイマー
        if (playerAnim.swingT >= 0.0f) playerAnim.swingT = fminf(playerAnim.swingT + simDt, 5.0f);
        playerAnim.downT = playerDownT;
        playerAnim.sliding = playerSliding;
        if (Vector3Length(playerVel) > 0.3f) playerAnim.slideSide = playerVel.x;   // 自分は -z 向きなので右＝+x
        playerAnim.runPhase += Vector3Length(playerVel) * simDt * 2.2f;

        // ---- サーブ（自分）----
        Vector3 tossIdeal = { player.x + 0.25f, SERVE_REACH, player.z - 0.35f };   // 理想の打点
        if (playerServing) {
            if (sv == SV_READY) {
                for (int s = 0; s < SERVE_COUNT; s++) {
                    if (!IsKeyPressed(serveKeys[s])) continue;
                    chargingServe = s;
                    charge = 0.0f;
                    tossOffset = (Vector2){ RandNormal() * TOSS_SPREAD, RandNormal() * TOSS_SPREAD };
                    ball = (Ball){ 0 };
                    ball.pos = (Vector3){ tossIdeal.x + tossOffset.x, 1.9f, tossIdeal.z + tossOffset.y };
                    ball.vel = (Vector3){ 0.0f, TOSS_SPEED, 0.0f };
                    ball.active = true;
                    ball.netClearance = -99.0f;
                    trailCount = 0;
                    sv = SV_TOSS;
                    serveMsg = "";
                    break;
                }
            } else if (sv == SV_TOSS) {
                charge = fminf(charge + frameDt / CHARGE_TIME, 1.0f);
                playerAnim.windup = 0.3f + 0.7f * charge;
                playerAnim.windStroke = 2;
                if (IsKeyReleased(serveKeys[chargingServe])) {
                    playerAnim.windup = 0.0f;
                    ServeType type = (ServeType)chargingServe;
                    chargingServe = -1;
                    // 直前のフレームの間で、理想の打点の高さに一番近かった瞬間を使う（高すぎる球は打てない）
                    int best = -1;
                    for (int h = 0; h < historyCount; h++) {
                        const Ball *hb = &history[h];
                        if (!hb->active || hb->bounces > 0 || hb->pos.y > SERVE_REACH + SERVE_OVERREACH) continue;
                        if (best < 0 || fabsf(hb->pos.y - SERVE_REACH) < fabsf(history[best].pos.y - SERVE_REACH)) best = h;
                    }
                    lastServeType = type;
                    if (mode == MODE_SERVE) svStats[serveNo - 1][0]++;
                    if (best < 0) {
                        serveMsg = "WHIFF (ball above reach)";   // 空振りはフォルト
                        lastServeKmh = 0.0f;
                        plog.serves++; plog.serveWhiffs++;
                        if (plog.f) {
                            fprintf(plog.f, "%.2f,%s,%s,serve,%s,%.2f,serve,0,0,high,,,,,,,%.2f,,,,,,%.2f,%.2f,,,,,,,,WHIFF,%d\n",
                                    GetTime(), MODE_NAMES[mode], AI_LEVELS[ai.level].name, SERVES[type].name, charge,
                                    ball.pos.y, player.x, player.z, plog.assist);
                            fflush(plog.f);
                        }
                        // フォルトとして判定させる
                        ball = (Ball){ 0 };
                        ball.isServe = true; ball.bounces = 1; ball.result = RESULT_OUT;
                        ball.lastHitter = HITTER_PLAYER;
                        ball.netClearance = -99.0f;
                        sv = SV_FLIGHT;
                        if (mode == MODE_MATCH) { phase = PH_PLAY; serveJudged = false; }
                    } else {
                        Ball hb = history[best];
                        float qH = Clamp01(1.0f - fabsf(hb.pos.y - SERVE_REACH) / SERVE_TOL_H);
                        float qT = Clamp01(1.0f - Vector2Length(tossOffset) / TOSS_TOL);
                        float q = qH * qT;
                        // コース：A=左、D=右、なし=ボックスの真ん中（ボディ）。W/S で深さ
                        float x0, x1;
                        ServiceBox(player.x, -1, &x0, &x1);
                        Vector3 target = { 0.5f * (x0 + x1), 0.0f, -5.7f };
                        if (IsKeyDown(KEY_A)) target.x = x0 + 0.35f;
                        if (IsKeyDown(KEY_D)) target.x = x1 - 0.35f;
                        if (IsKeyDown(KEY_W)) target.z = -6.1f;
                        if (IsKeyDown(KEY_S)) target.z = -4.8f;
                        lastServeH = hb.pos.y;
                        lastServeQ = q;
                        lastContactPos = hb.pos;
                        playerAnim.lastContact = (Contact){ .overhead = true, .forehand = true, .quality = q };
                        playerAnim.contactLocal = (Vector3){ hb.pos.x - player.x, hb.pos.y, player.z - hb.pos.z };
                        playerAnim.swingT = 0.0f;
                        ball = HitServe(hb.pos, target, type, charge, q, x0, x1, &lastServeKmh);
                        ball.lastHitter = HITTER_PLAYER;
                        ball.shotId = ++plog.shotSerial;
                        plog.serves++;
                        LogFlush(&plog, "?");
                        snprintf(plog.row, sizeof(plog.row),
                                 "%.2f,%s,%s,serve,%s,%.2f,serve,0,0,,%.3f,%.3f,%.3f,,,,%.2f,,,,%.0f,,%.2f,%.2f,,%.2f,%.2f,%.2f,,,",
                                 GetTime(), MODE_NAMES[mode], AI_LEVELS[ai.level].name, SERVES[type].name, charge,
                                 q, qH, qT, hb.pos.y, lastServeKmh, player.x, player.z, hb.pos.x, hb.pos.y, hb.pos.z);
                        plog.pending = true; plog.pendingId = ball.shotId; plog.pendingServe = true;
                        trailColor = SERVES[type].color;
                        trailCount = 0;
                        serveAce = false;
                        sv = SV_FLIGHT;
                        if (mode == MODE_MATCH) { phase = PH_PLAY; serveJudged = false; }
                    }
                } else if (ball.pos.y < 1.2f && ball.vel.y < 0.0f) {
                    // 打たずに落とした：トスのやり直し（ペナルティなし）
                    chargingServe = -1;
                    ball = (Ball){ 0 };
                    serveMsg = "Let the toss drop - toss again";
                    sv = SV_READY;
                }
            }
        }
        // サーブ練習：結果の集計と次のサーブ
        if (mode == MODE_SERVE) {
            if (sv == SV_FLIGHT && ball.result != RESULT_NONE) {
                bool in = ball.result == RESULT_IN;
                if (in) {
                    svStats[serveNo - 1][1]++;
                    serveMsg = "IN";
                } else if (serveNo == 1) {
                    serveMsg = (ball.result == RESULT_NET) ? "FAULT (net)" : (serveMsg[0] ? serveMsg : "FAULT (out)");
                } else {
                    serveMsg = (ball.result == RESULT_NET) ? "DOUBLE FAULT (net)" : "DOUBLE FAULT";
                    doubleFaults++;
                }
                if (!in && serveNo == 1) serveNo = 2;
                else { serveNo = 1; deuceSide = !deuceSide; }
                svTimer = 2.0f;
                sv = SV_WAIT;
            } else if (sv == SV_WAIT) {
                svTimer -= frameDt;
                if (svTimer <= 0.0f) {
                    ball = (Ball){ 0 };
                    player = (Vector3){ deuceSide ? 0.6f : -0.6f, 0.0f, HALF_LEN + 0.15f };
                    sv = SV_READY;
                }
            }
        }

        // ---- 試合：AI のサーブ ----
        if (mode == MODE_MATCH && phase == PH_SERVE && server == 1 && !score.over) {
            phaseTimer -= simDt;
            if (phaseTimer <= 0.0f) {
                ball = AiServe(&ai, DeuceCourt(&score), serveNo == 1);
                trailColor = SERVES[serveNo == 1 ? SERVE_FLAT : SERVE_KICK].color;
                trailCount = 0;
                phase = PH_PLAY;
                serveJudged = false;
            }
        }

        // ---- スイング（ラリー・リターン・試合）----
        if (!playerServing && mode != MODE_SERVE && !botControls && playerDownT <= 0.0f) {
            if (chargingShot < 0) {
                bool shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
                for (int s = 0; s < SHOT_NORMAL_COUNT; s++) {
                    if (IsKeyPressed(shotKeys[s])) {
                        chargingShot = shift ? s + SHOT_NORMAL_COUNT : s;
                        charge = 0.0f;
                    }
                }
            } else {
                charge = fminf(charge + frameDt / CHARGE_TIME, 1.0f);
                // 構えの見た目：ボールのある側で構える（高い球なら頭上）
                playerAnim.windup = charge;
                if (ball.active && ball.lastHitter == HITTER_OPP)
                    playerAnim.windStroke = ball.pos.y > OVERHEAD_MIN_H && Vector3Distance(ball.pos, player) < 6.0f ? 2
                                          : (ball.pos.x >= player.x ? 0 : 1);
                if (IsKeyReleased(shotKeys[chargingShot])) {
                    ShotType type = (ShotType)chargingShot;
                    chargingShot = -1;
                    playerAnim.windup = 0.0f;

                    // コースは「離した瞬間に押している方向」で決まる
                    const ShotSpec *sp = &SHOTS[type];
                    Vector3 target = { 0.0f, 0.0f, sp->depth };
                    if (IsKeyDown(KEY_A)) target.x = -3.0f;
                    if (IsKeyDown(KEY_D)) target.x =  3.0f;
                    if (IsKeyDown(KEY_W)) target.z = sp->deepDepth;
                    if (IsKeyDown(KEY_S)) target.z = sp->shortDepth;

                    // 直前のフレームの間で、一番いい当たりになる瞬間のボールを使う
                    Contact c = { .miss = MISS_NO_BALL };
                    int bestIdx = -1;
                    for (int h = 0; h < historyCount; h++) {
                        const Ball *hb = &history[h];
                        bool hittable = hb->active && hb->lastHitter == HITTER_OPP &&
                                        hb->bounces <= 1 && hb->pos.z > 0.0f && !hb->hitNet &&
                                        (!hb->isServe || hb->bounces == 1);   // サーブはバウンドしてから
                        if (!hittable) continue;
                        Vector3 toBall = { hb->pos.x - player.x, 0.0f, hb->pos.z - player.z };
                        float moveToward = (Vector3Length(toBall) > 0.01f) ? Vector3DotProduct(playerVel, Vector3Normalize(toBall)) : 0.0f;
                        Contact hc = EvaluateContact(player, hb, type, charge, moveToward, playerSliding ? SLIDE_REACH : 0.0f);
                        bool better = (bestIdx < 0) ||
                                      (hc.miss == MISS_NONE && (c.miss != MISS_NONE || hc.quality > c.quality)) ||
                                      (hc.miss != MISS_NONE && c.miss != MISS_NONE);
                        if (better) { c = hc; bestIdx = h; }
                    }
                    if (bestIdx >= 0 && c.miss == MISS_NONE) ball = history[bestIdx];
                    c.stepIn = -playerVel.z;

                    // スイングの見た目と、理想の打点との比較用
                    Vector3 sb = (bestIdx >= 0) ? history[bestIdx].pos : ball.pos;
                    playerAnim.lastContact = c;
                    playerAnim.contactLocal = (Vector3){ sb.x - player.x, sb.y, player.z - sb.z };
                    playerAnim.swingT = 0.0f;
                    if (c.overhead) lastIdealPos = (Vector3){ player.x + OVERHEAD_SIDE, OVERHEAD_IDEAL_H, player.z - OVERHEAD_FWD };
                    else lastIdealPos = (Vector3){ player.x + (c.forehand ? IDEAL_SIDE : -IDEAL_SIDE),
                                                   SHOTS[type].idealHeight, player.z - IDEAL_FWD };
                    lastContactPos = sb;
                    if (c.miss == MISS_NONE && c.dive) { playerDownT = DIVE_DOWN_TIME; playerVel = (Vector3){ 0 }; }

                    hasLast = true;
                    lastContact = c;
                    lastType = type;

                    // ---- プレイ記録 ----
                    if (c.miss != MISS_NO_BALL) {
                        Ball pre = (bestIdx >= 0) ? history[bestIdx] : ball;
                        float bestQ, offMs;
                        LogTimingAnalysis(&plog, player, &pre, type, charge, &bestQ, &offMs);
                        plog.swung = true;
                        plog.swings++;
                        if (c.miss != MISS_NONE) { plog.whiffs++; plog.miss[c.miss]++; }
                        else { plog.hits++; plog.sumQ += c.quality; }
                        if (bestQ > 0.0f) {
                            plog.sumBestQ += bestQ;
                            plog.sumAbsOffset += fabsf(offMs); plog.nOffset++;
                            if (offMs < -25.0f) plog.early++; else if (offMs > 25.0f) plog.late++; else plog.onTime++;
                            // 原因の切り分け：その位置で一番よくても 40% 未満なら足（位置）、そうでなく質が半分以下ならタイミング
                            if (bestQ < 0.40f) plog.posProblem++;
                            else if (c.miss != MISS_NONE || c.quality < bestQ * 0.5f) plog.timingProblem++;
                        } else {
                            plog.posProblem++;
                        }
                        snprintf(plog.row, sizeof(plog.row),
                                 "%.2f,%s,%s,swing,%s,%.2f,%s,%d,%d,%s,%.3f,%.3f,%.3f,%.3f,%.1f,%.2f,%.2f,%s,%.3f,%.0f,%s,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.3f,%.2f,%.2f",
                                 GetTime(), MODE_NAMES[mode], AI_LEVELS[ai.level].name, SHOTS[type].name, charge,
                                 StrokeName(&c), c.volley, c.dive, MissName(c.miss),
                                 c.quality, c.qTiming, c.qDist, c.qHeight, c.angleDeg, c.side, c.height,
                                 bestQ > 0.0f ? TextFormat("%.0f", offMs) : "", bestQ, c.inKmh, "%OUT%", c.stepIn,
                                 player.x, player.z, Vector3Length(playerVel), pre.pos.x, pre.pos.y, pre.pos.z, pre.age,
                                 plog.firstMove, plog.chargeStart);
                        if (c.miss != MISS_NONE) {
                            // 空振り：結果は WHIFF。打球速度は空
                            char *o = strstr(plog.row, "%OUT%");
                            if (o) memmove(o, o + 5, strlen(o + 5) + 1);
                            plog.pending = true; plog.pendingServe = false;
                            LogFlush(&plog, "WHIFF");
                        }
                    }

                    if (c.miss == MISS_NONE) {
                        ball = PlayerShot(&c, ball.pos, target, type, charge, &lastKmh, &lastRpm, &lastAngle);
                        ball.lastHitter = HITTER_PLAYER;
                        ball.shotId = ++plog.shotSerial;
                        {
                            // 打球速度を埋めて、結果待ちにする
                            char *o = strstr(plog.row, "%OUT%");
                            if (o) {
                                char tail[1024];
                                snprintf(tail, sizeof(tail), "%s", o + 5);
                                snprintf(o, sizeof(plog.row) - (size_t)(o - plog.row), "%.0f%s", lastKmh, tail);
                            }
                            plog.pending = true; plog.pendingId = ball.shotId; plog.pendingServe = false;
                        }
                        trailColor = SHOTS[type].color;
                        trailCount = 0;
                        stats[type][0]++;
                        scoredLast = false;
                    }
                }
            }
        }
        if (IsKeyPressed(KEY_R) && mode != MODE_MATCH) {
            // ITF のバウンドテスト: 2.54m から落とす
            ball = (Ball){ 0 };
            ball.pos = (Vector3){ 0.0f, 2.54f, -3.0f };
            ball.active = true;
            ball.netClearance = -99.0f;
            trailCount = 0;
            feedTimer = 4.0f;
        }

        // ---- AI：反応して走る ----
        if (mode == MODE_MATCH && phase == PH_PLAY) AiUpdate(&ai, &ball, simDt);

        // ---- 物理（固定ステップ）----
        accumulator += simDt;
        historyCount = 0;
        history[historyCount++] = ball;
        while (accumulator >= PHYS_DT) {
            StepBall(&ball, PHYS_DT);
            if (historyCount < 64) history[historyCount++] = ball;
            accumulator -= PHYS_DT;
            if (!botControls) LogRecordBall(&plog, &ball);

            // AI のスイング（物理と同じ細かさで）
            if (mode == MODE_MATCH && phase == PH_PLAY && ai.planned && ball.age >= ai.swingAge) {
                if (AiSwing(&ai, &ball, player)) {
                    trailColor = Fade(SHOTS[ai.lastType].color, 0.7f);
                    trailCount = 0;
                }
            }
            if (botControls && phase == PH_PLAY && bot.planned && ball.age >= bot.swingAge) {
                Ball fb = FlipSides(ball);
                if (AiSwing(&bot, &fb, Rot180(ai.pos))) {
                    ball = FlipSides(fb);
                    trailColor = SHOTS[bot.lastType].color;
                    trailCount = 0;
                }
            }

            trailTimer += PHYS_DT;
            if (ball.active && trailTimer >= 1.0f / 120.0f) {
                trailTimer = 0.0f;
                if (trailCount < TRAIL_N) trail[trailCount++] = ball.pos;
            }

            // サーブ練習：エース級か（レシーバーが反応 0.3 秒・6m/s で、踏み込んで届くか）
            if (mode == MODE_SERVE && ball.isServe && ball.result == RESULT_IN && ball.bounces == 1 &&
                !serveAce && ball.pos.z <= -HALF_LEN - 0.8f) {
                float retX = 0.5f * (ball.boxX0 + ball.boxX1);
                float canMove = fmaxf(ball.age - 0.3f, 0.0f) * PLAYER_SPEED;
                serveAce = true;
                if (fabsf(ball.pos.x - retX) - REACH_SIDE > canMove) { aces++; serveMsg = "ACE!"; }
                else serveMsg = "IN (returnable)";
            }
        }

        // バウンドを見つけて目印を出す
        bounceT += simDt;
        if (ball.bounces > prevBounces && ball.bounces <= 2) { bouncePos = ball.pos; bounceT = 0.0f; }
        prevBounces = ball.bounces;

        // ---- プレイ記録：来た球の追跡と、打球の結果 ----
        if (!botControls) {
            LogTrackIncoming(&plog, &ball, player, playerVel, chargingShot >= 0, MODE_NAMES[mode], AI_LEVELS[ai.level].name);
            if (plog.pending) {
                if (ball.shotId != plog.pendingId) LogFlush(&plog, "?");
                else if (ball.result != RESULT_NONE) LogFlush(&plog, ResultText(ball.result));
            }
        }

        // ---- 試合：ポイントの判定 ----
        if (mode == MODE_MATCH && phase == PH_PLAY) {
            int hitter = (ball.lastHitter == HITTER_PLAYER) ? 0 : 1;
            int winner = -1;
            if (ball.isServe && !serveJudged && ball.bounces >= 1) {
                if (ball.result == RESULT_IN) {
                    serveJudged = true;
                } else if (serveNo == 1) {
                    serveNo = 2;
                    pointMsg = (ball.result == RESULT_NET) ? "Fault (net)" : "Fault";
                    TraceLog(LOG_INFO, "FAULT by %s", hitter == 0 ? "YOU" : "AI");
                    phase = PH_BETWEEN;
                    phaseTimer = 1.5f;
                } else {
                    winner = 1 - hitter;
                    pointMsg = "Double fault";
                }
            } else if (!ball.isServe || serveJudged) {
                if (ball.bounces >= 1 && ball.result != RESULT_IN && ball.result != RESULT_NONE) {
                    winner = 1 - hitter;
                    pointMsg = (ball.result == RESULT_NET) ? (hitter == 0 ? "Your error (net)" : "AI error (net)")
                                                           : (hitter == 0 ? "Your error (out)" : "AI error (out)");
                } else if (ball.bounces >= 2) {
                    winner = hitter;
                    if (ball.isServe) pointMsg = (hitter == 0) ? "Your ace / unreturned serve" : "AI ace / unreturned serve";
                    else              pointMsg = (hitter == 0) ? "Your winner" : "AI winner";
                }
            }
            if (winner >= 0) {
                TraceLog(LOG_INFO, "POINT end: %s -> %s", pointMsg, winner == 0 ? "YOU" : "AI");
                int gamesBefore = score.games[0] + score.games[1] + 6 * (score.sets[0] + score.sets[1]);
                AwardPoint(&score, winner);
                int gamesAfter = score.games[0] + score.games[1] + 6 * (score.sets[0] + score.sets[1]);
                if (score.over)                   snprintf(scoreCall, sizeof(scoreCall), "GAME, SET  %s", winner == 0 ? "YOU" : "AI");
                else if (gamesAfter != gamesBefore || (score.points[0] == 0 && score.points[1] == 0))
                    snprintf(scoreCall, sizeof(scoreCall), "GAME %s   (%d - %d)", winner == 0 ? "YOU" : "AI", score.games[0], score.games[1]);
                else                              snprintf(scoreCall, sizeof(scoreCall), "%s", ScoreText(&score));
                phase = PH_POINT_OVER;
                phaseTimer = 2.0f;
            }
        } else if (mode == MODE_MATCH && (phase == PH_BETWEEN || phase == PH_POINT_OVER)) {
            phaseTimer -= simDt;
            if (phase == PH_POINT_OVER) AiUpdate(&ai, &ball, simDt);   // 打ち終わったら戻る
            if (phaseTimer <= 0.0f) {
                if (phase == PH_POINT_OVER) serveNo = 1;
                needSetup = true;
                pointMsg = "";
                scoreCall[0] = 0;
            }
        }

        // 自分の打球の結果を集計
        if (mode != MODE_SERVE && ball.lastHitter == HITTER_PLAYER && !scoredLast && ball.result != RESULT_NONE) {
            if (ball.result == RESULT_IN) stats[lastType][1]++;
            scoredLast = true;
        }

        // 次の球出し（練習）：打球が落ちた / 打ち損ねて2バウンド / 止まった
        if (mode == MODE_RALLY || mode == MODE_RETURN) {
            bool rallyOver = !ball.active || ball.hitNet ||
                             (ball.lastHitter == HITTER_PLAYER  && ball.bounces >= 1) ||
                             (ball.lastHitter == HITTER_OPP && ball.bounces >= 2);
            if (rallyOver) {
                feedTimer -= frameDt;
                if (feedTimer <= 0.0f) {
                    if (mode == MODE_RETURN) {
                        ball = MachineServe();
                        player = ReturnPos(true);   // リターンの構え位置へ
                        playerVel = (Vector3){ 0 };
                    } else {
                        ball = MachineFeed(proFeed);
                    }
                    trailColor = Fade(LIGHTGRAY, 0.6f);
                    trailCount = 0;
                    feedTimer = 1.2f;
                }
            }
        }

        // ---- カメラ ----
        if (camMode == 0) {        // プレイヤーの後ろ
            // 少し高く後ろから：上から見る角度があると、奥行き（ボールまでの距離）が読みやすい
            cam.position = (Vector3){ player.x * 0.6f, 4.6f, player.z + 7.5f };
            cam.target   = (Vector3){ player.x * 0.3f, 0.3f, player.z - 7.0f };
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

            // プレイヤー（-z 向き）と AI（+z 向き）
            DrawFigure(player, true, (Color){ 230, 120, 60, 255 }, botControls ? &bot.anim : &playerAnim,
                       Vector3Length(playerVel));
            if (mode == MODE_MATCH)
                DrawFigure(ai.pos, false, (Color){ 70, 90, 200, 255 }, &ai.anim, Vector3Length(ai.vel));

            if (playerServing) {
                // 狙うサービスボックスを薄く塗る
                float x0, x1;
                ServiceBox(player.x, -1, &x0, &x1);
                DrawCube((Vector3){ 0.5f * (x0 + x1), 0.003f, -SERVICE_Z * 0.5f }, x1 - x0, 0.002f, SERVICE_Z,
                         Fade(YELLOW, 0.15f));
                // 理想の打点
                DrawSphereWires(tossIdeal, 0.12f, 6, 8, Fade(RAYWHITE, 0.8f));
                // 構えているときはボールを手に持つ
                if (sv == SV_READY)
                    DrawSphere((Vector3){ player.x + 0.25f, 1.0f, player.z - 0.3f }, BALL_R, (Color){ 210, 240, 40, 255 });
            } else if (mode != MODE_SERVE && !botControls) {
                // 届く範囲（地面の輪）：内側の輪が理想の距離、外側が届く限界
                DrawCircle3D((Vector3){ player.x, 0.01f, player.z }, IDEAL_R, (Vector3){ 1, 0, 0 }, 90.0f, Fade(RAYWHITE, 0.35f));
                DrawCircle3D((Vector3){ player.x, 0.01f, player.z }, REACH_SIDE, (Vector3){ 1, 0, 0 }, 90.0f, Fade(RAYWHITE, 0.18f));
                // 理想の打点（フォア・バック）。溜めている球種の理想の高さに出す。頭上で構えていれば頭上
                ShotType t = (chargingShot >= 0) ? (ShotType)chargingShot : SHOT_TOP;
                Color zc = Fade(SHOTS[t].color, 0.8f);
                if (chargingShot >= 0 && playerAnim.windStroke == 2) {
                    DrawSphereWires((Vector3){ player.x + OVERHEAD_SIDE, OVERHEAD_IDEAL_H, player.z - OVERHEAD_FWD }, 0.15f, 6, 8, zc);
                } else {
                    float h = SHOTS[t].idealHeight;
                    DrawSphereWires((Vector3){ player.x + IDEAL_SIDE, h, player.z - IDEAL_FWD }, 0.15f, 6, 8, zc);
                    DrawSphereWires((Vector3){ player.x - IDEAL_SIDE, h, player.z - IDEAL_FWD }, 0.15f, 6, 8, zc);
                }
                // 最後に振ったとき：理想の打点（白い網）と、実際のボール（色つき）を線で結ぶ。ずれが一目で分かる
                if (hasLast && playerAnim.swingT >= 0.0f && playerAnim.swingT < 2.5f && lastContact.miss != MISS_NO_BALL) {
                    Color qc = lastContact.miss == MISS_NONE ? QualityColor(lastContact.quality) : RED;
                    DrawSphereWires(lastIdealPos, 0.15f, 6, 8, RAYWHITE);
                    DrawSphere(lastContactPos, 0.06f, qc);
                    DrawLine3D(lastIdealPos, lastContactPos, qc);
                    DrawLine3D((Vector3){ lastContactPos.x, 0.01f, lastContactPos.z }, lastContactPos, Fade(qc, 0.5f));
                }
            }

            // 手がかり：立つ場所（緑の輪）と、タイミングの輪（一番いい瞬間に向かって縮む）
            if (assistLevel > 0 && !botControls && !playerServing && mode != MODE_SERVE && playerDownT <= 0.0f) {
                ShotType at = (chargingShot >= 0) ? (ShotType)chargingShot : SHOT_TOP;
                Assist as = ComputeAssist(player, playerVel, &ball, at, chargingShot >= 0 ? charge : 0.5f);
                if (assistLevel >= 2 && as.standValid) {
                    Vector3 st = { as.stand.x, 0.012f, as.stand.z };
                    DrawCircle3D(st, 0.30f, (Vector3){ 1, 0, 0 }, 90.0f, GREEN);
                    DrawCircle3D(st, 0.26f, (Vector3){ 1, 0, 0 }, 90.0f, GREEN);
                    DrawLine3D((Vector3){ player.x, 0.012f, player.z }, st, Fade(GREEN, 0.5f));
                    DrawSphere(as.ballAt, 0.045f, Fade(YELLOW, 0.5f));
                    DrawLine3D((Vector3){ as.ballAt.x, 0.012f, as.ballAt.z }, as.ballAt, Fade(YELLOW, 0.4f));
                }
                if (as.timingValid && as.tBest < 1.2f) {
                    Vector3 ip = as.overhead ? (Vector3){ player.x + OVERHEAD_SIDE, OVERHEAD_IDEAL_H, player.z - OVERHEAD_FWD }
                                             : (Vector3){ player.x + (as.fore ? IDEAL_SIDE : -IDEAL_SIDE), SHOTS[at].idealHeight,
                                                          player.z - IDEAL_FWD };
                    float rr = 0.16f + 1.4f * as.tBest;
                    Color rc = QualityColor(as.qBest);
                    DrawCircle3D(ip, rr, (Vector3){ 1, 0, 0 }, 90.0f, rc);
                    DrawCircle3D(ip, rr + 0.02f, (Vector3){ 1, 0, 0 }, 90.0f, rc);
                }
            }

            // 軌跡：新しいところほど濃く。相手の球は明るい色で
            {
                bool incoming = ball.lastHitter == HITTER_OPP;
                Color tc = incoming ? (Color){ 255, 255, 170, 255 } : trailColor;
                for (int i = 1; i < trailCount; i++) {
                    float age = (float)(trailCount - i) / 60.0f;          // 0 = 今、1 = 0.5 秒前
                    float alpha = Clamp(1.0f - age, 0.15f, 1.0f);
                    DrawLine3D(trail[i - 1], trail[i], Fade(tc, alpha));
                }
            }

            // バウンドした場所に輪を一瞬出す（深さが読みやすい）
            if (bounceT < 0.8f) {
                float k = bounceT / 0.8f;
                DrawCircle3D((Vector3){ bouncePos.x, 0.012f, bouncePos.z }, 0.12f + 0.35f * k, (Vector3){ 1, 0, 0 }, 90.0f,
                             Fade(RAYWHITE, 1.0f - k));
            }

            // ボール：実寸（直径 6.7cm）だと遠くで数ピクセルになるので、遠いほど少し大きく描く。
            // 影と、ボールから地面への縦の線で、高さと位置が分かるようにする
            if (ball.active || ball.bounces > 0) {
                float camDist = Vector3Distance(cam.position, ball.pos);
                float vr = fmaxf(BALL_R * 1.4f, camDist * 0.0050f);
                Vector3 g = { ball.pos.x, 0.004f, ball.pos.z };
                DrawCylinder(g, vr * 1.1f, vr * 1.1f, 0.002f, 14, (Color){ 0, 0, 0, 150 });   // 影
                if (ball.pos.y > 0.15f) DrawLine3D(g, ball.pos, Fade(RAYWHITE, 0.45f));       // 高さの線
                DrawSphere(ball.pos, vr, (Color){ 235, 255, 40, 255 });
                // 光のふち（ボールの後に描く。先に描くと、ふちの奥行きでボールが隠れる）
                rlDisableDepthMask();
                DrawSphere(ball.pos, vr * 1.7f, Fade((Color){ 255, 255, 140, 255 }, 0.22f));
                rlEnableDepthMask();
            }
        EndMode3D();

        // ---- HUD ----
        DrawRectangle(10, 10, 500, 290, Fade(BLACK, 0.5f));
        int y = 20;
        bool showServeInfo = (mode == MODE_SERVE) || (playerServing && mode == MODE_MATCH);
        if (showServeInfo) {
            DrawText(TextFormat("%s serve  -  %s court", serveNo == 1 ? "1st" : "2nd", deuceSide ? "deuce" : "ad"),
                     20, y, 22, RAYWHITE); y += 30;
            if (lastServeKmh > 0.0f || serveMsg[0]) {
                if (lastServeKmh > 0.0f) {
                    DrawText(TextFormat("%s  %.0f km/h", SERVES[lastServeType].name, lastServeKmh), 20, y, 18, SERVES[lastServeType].color); y += 22;
                    DrawText(TextFormat("Contact height %.2f m (reach %.2f)  quality %.0f%%", lastServeH, SERVE_REACH, lastServeQ * 100.0f),
                             20, y, 18, QualityColor(lastServeQ)); y += 22;
                } else {
                    DrawText(SERVES[lastServeType].name, 20, y, 18, SERVES[lastServeType].color); y += 44;
                }
                if (ball.netClearance > -50.0f)
                    DrawText(TextFormat("Net clearance %+.2f m", ball.netClearance), 20, y, 18, RAYWHITE);
                y += 22;
                Color mc = (serveMsg[0] == 'I' || serveMsg[0] == 'A') ? GREEN : (serveMsg[0] == 'L' ? YELLOW : RED);
                DrawText(serveMsg, 20, y, 22, mc); y += 30;
            } else {
                DrawText("A/D: position.  Hold L flat / K slice / J kick to toss,", 20, y, 18, RAYWHITE); y += 22;
                DrawText("release to hit. A/D/W/S at release = course.", 20, y, 18, RAYWHITE); y += 22;
                DrawText("Keep holding to let a bad toss drop.", 20, y, 18, RAYWHITE); y += 22;
            }
        } else if (hasLast) {
            const Contact *c = &lastContact;
            if (c->miss != MISS_NONE) {
                DrawText(MissText(c->miss), 20, y, 22, RED); y += 30;
                if (c->miss != MISS_NO_BALL) {
                    DrawText(TextFormat("Ball was %.2f m from you, %+.0f deg (side=0, front=90), %.2f m high",
                                        c->side, c->angleDeg, c->height), 20, y, 16, RAYWHITE); y += 20;
                    DrawText("White ring on the ground = ideal distance, faint ring = max reach", 20, y, 16, LIGHTGRAY); y += 20;
                }
            } else {
                const char *stroke = c->overhead ? "Overhead" : (c->forehand ? "Forehand" : "Backhand");
                const char *tag = c->dive ? " DIVE!" : (c->volley ? " volley" : "");
                DrawText(TextFormat("%s%s %s  quality %.0f%%", stroke, tag,
                                    c->overhead && lastType != SHOT_SLICE && lastType < SHOT_NORMAL_COUNT ? "SMASH" : SHOTS[lastType].name,
                                    c->quality * 100.0f), 20, y, 22, QualityColor(c->quality));
                y += 30;
                if (c->overhead) {
                    const char *tStr = c->height > OVERHEAD_IDEAL_H + 0.15f ? "early" : (c->height < OVERHEAD_IDEAL_H - 0.15f ? "late" : "good");
                    DrawText(TextFormat("Timing  height %.2f m (ideal %.2f) %-5s %3.0f%%", c->height, OVERHEAD_IDEAL_H, tStr, c->qTiming * 100.0f), 20, y, 18, QualityColor(c->qTiming)); y += 22;
                    DrawText(TextFormat("Position under the ball         %3.0f%%", c->qDist * 100.0f), 20, y, 18, QualityColor(c->qDist)); y += 22;
                    y += 22;
                } else {
                    float idealAng = c->volley ? VOLLEY_ANGLE : IDEAL_ANGLE;
                    const char *tStr = c->angleDeg > idealAng + 8.0f ? "early" : (c->angleDeg < idealAng - 8.0f ? "late" : "good");
                    const char *dStr = c->side < IDEAL_R - 0.15f ? "jammed" : (c->side > IDEAL_R + 0.15f ? "stretched" : "good");
                    const char *hStr = c->height < SHOTS[lastType].idealHeight - 0.2f ? "low"
                                     : (c->height > SHOTS[lastType].idealHeight + 0.2f ? "high" : "good");
                    DrawText(TextFormat("Timing   %+3.0f deg (%+.0f ms) %-5s %3.0f%%", c->angleDeg - idealAng, c->timeErrMs, tStr, c->qTiming * 100.0f), 20, y, 18, QualityColor(c->qTiming)); y += 22;
                    DrawText(TextFormat("Distance %.2f m  %-9s %3.0f%%", c->side, dStr, c->qDist * 100.0f), 20, y, 18, QualityColor(c->qDist)); y += 22;
                    DrawText(TextFormat("Height   %.2f m  %-9s %3.0f%%", c->height, hStr, c->qHeight * 100.0f), 20, y, 18, QualityColor(c->qHeight)); y += 22;
                }
                DrawText(TextFormat("%.0f km/h in -> %.0f km/h out  step %+.1f m/s", c->inKmh, lastKmh, c->stepIn), 20, y, 18, RAYWHITE); y += 22;
                if (ball.lastHitter == HITTER_PLAYER) {
                    if (ball.netClearance > -50.0f)
                        DrawText(TextFormat("Net clearance %+.2f m", ball.netClearance), 20, y, 18, RAYWHITE);
                    DrawText(ResultText(ball.result), 300, y, 22, ball.result == RESULT_IN ? GREEN : RED);
                }
                y += 24;
            }
        } else {
            DrawText("Move to the ball, hold J/K/L, release to swing", 20, y, 18, RAYWHITE);
        }
        y = 176;
        if (mode == MODE_SERVE) {
            DrawText(TextFormat("1st in %d/%d   2nd in %d/%d", svStats[0][1], svStats[0][0], svStats[1][1], svStats[1][0]), 20, y, 18, RAYWHITE); y += 20;
            DrawText(TextFormat("Aces %d   Double faults %d", aces, doubleFaults), 20, y, 18, RAYWHITE); y += 24;
        } else {
            DrawText(TextFormat("IN  flat %d/%d   top %d/%d   slice %d/%d",
                                stats[SHOT_FLAT][1], stats[SHOT_FLAT][0], stats[SHOT_TOP][1], stats[SHOT_TOP][0],
                                stats[SHOT_SLICE][1], stats[SHOT_SLICE][0]), 20, y, 18, RAYWHITE); y += 20;
            DrawText(TextFormat("    lob %d/%d   top lob %d/%d   drop %d/%d",
                                stats[SHOT_SLICE_LOB][1], stats[SHOT_SLICE_LOB][0], stats[SHOT_TOP_LOB][1], stats[SHOT_TOP_LOB][0],
                                stats[SHOT_DROP][1], stats[SHOT_DROP][0]), 20, y, 18, RAYWHITE); y += 24;
        }
        DrawText("WASD move/aim  J top K slice L flat  +Shift: lob/drop", 20, y, 16, LIGHTGRAY); y += 18;
        if (mode == MODE_MATCH)
            DrawText(TextFormat("G mode: MATCH   1/2/3 AI level: %s   C cam  T slow", AI_LEVELS[ai.level].name), 20, y, 16, LIGHTGRAY);
        else
            DrawText(TextFormat("G mode: %s   V feed: %s   F feed  C cam  T slow", MODE_NAMES[mode], proFeed ? "PRO" : "CLUB"),
                     20, y, 16, LIGHTGRAY);

        // スコア（試合）
        if (mode == MODE_MATCH) {
            int sx = GetScreenWidth() - 330;
            DrawRectangle(sx, 10, 320, 120, Fade(BLACK, 0.55f));
            DrawText(TextFormat("YOU %d   AI %d   (games)", score.games[0], score.games[1]), sx + 12, 20, 22, RAYWHITE);
            DrawText(ScoreText(&score), sx + 12, 50, 26, YELLOW);
            DrawText(TextFormat("Serving: %s  %s", server == 0 ? "YOU" : "AI", serveNo == 2 ? "(2nd)" : ""), sx + 12, 84, 18, LIGHTGRAY);
            if (ai.lastKmh > 0.0f)
                DrawText(TextFormat("AI last: %s %.0f km/h", ball.isServe && ball.lastHitter == HITTER_OPP ? "SERVE" : SHOTS[ai.lastType].name, ai.lastKmh),
                         sx + 12, 106, 16, LIGHTGRAY);
            if (pointMsg[0]) {
                int w = MeasureText(pointMsg, 34);
                DrawRectangle(GetScreenWidth() / 2 - w / 2 - 16, 150, w + 32, 50, Fade(BLACK, 0.6f));
                DrawText(pointMsg, GetScreenWidth() / 2 - w / 2, 158, 34, YELLOW);
            }
            if (scoreCall[0] && !score.over) {
                // スコアのコール（40-15、DEUCE、ADVANTAGE、GAME）を大きく
                const char *call = TextToUpper(scoreCall);
                int w = MeasureText(call, 48);
                DrawRectangle(GetScreenWidth() / 2 - w / 2 - 20, 210, w + 40, 64, Fade(BLACK, 0.7f));
                DrawText(call, GetScreenWidth() / 2 - w / 2, 218, 48, RAYWHITE);
            }
            if (score.over) {
                const char *m = score.sets[0] > score.sets[1] ? "YOU WIN THE SET!  (Enter: new match)" : "AI WINS THE SET  (Enter: new match)";
                int w = MeasureText(m, 30);
                DrawRectangle(GetScreenWidth() / 2 - w / 2 - 16, 220, w + 32, 46, Fade(BLACK, 0.7f));
                DrawText(m, GetScreenWidth() / 2 - w / 2, 228, 30, RAYWHITE);
            }
        }

        // 統計（Tab）
        if (showStats) {
            int bx = 260, by = 120, bw = 760, bh = 470;
            DrawRectangle(bx, by, bw, bh, Fade(BLACK, 0.85f));
            int ty = by + 16;
            #define LINE(...) do { DrawText(TextFormat(__VA_ARGS__), bx + 20, ty, 20, RAYWHITE); ty += 26; } while (0)
            DrawText("SESSION STATS  (Tab to close)   saved to playlog.csv", bx + 20, ty, 22, YELLOW); ty += 36;
            int sw = plog.swings > 0 ? plog.swings : 1;
            LINE("Swings %d:  hit %d (%d%%)   whiff %d (%d%%)", plog.swings, plog.hits, plog.hits * 100 / sw, plog.whiffs, plog.whiffs * 100 / sw);
            LINE("  whiffs: early %d  late %d  far %d  high %d  low %d",
                 plog.miss[MISS_EARLY], plog.miss[MISS_LATE], plog.miss[MISS_FAR], plog.miss[MISS_HIGH], plog.miss[MISS_LOW]);
            int res = plog.shotIn + plog.shotOut + plog.shotNet; if (res < 1) res = 1;
            LINE("Your shots that landed: IN %d%%   OUT %d%%   NET %d%%  (of %d)",
                 plog.shotIn * 100 / res, plog.shotOut * 100 / res, plog.shotNet * 100 / res, plog.shotIn + plog.shotOut + plog.shotNet);
            ty += 8;
            int no = plog.nOffset > 0 ? plog.nOffset : 1;
            LINE("Timing vs best moment: avg %.0f ms off   early %d / on time %d / late %d",
                 plog.sumAbsOffset / no, plog.early, plog.onTime, plog.late);
            LINE("Contact quality: avg %.0f%%   best possible from where you stood: %.0f%%",
                 plog.hits ? plog.sumQ / plog.hits * 100.0f : 0.0f, plog.sumBestQ / no * 100.0f);
            LINE("Main cause:  POSITION (feet) %d   TIMING (hands) %d", plog.posProblem, plog.timingProblem);
            ty += 8;
            LINE("Balls you didn't swing at: %d landing IN (%d were reachable), %d going OUT",
                 plog.noSwingIn, plog.noSwingReachable, plog.noSwingOut);
            LINE("Reaction (first move after opponent hit): avg %.2f s",
                 plog.nFirstMove ? plog.sumFirstMove / plog.nFirstMove : 0.0f);
            ty += 8;
            LINE("Serves %d:  in %d   whiff %d", plog.serves, plog.servesIn, plog.serveWhiffs);
            #undef LINE
        }

        // 溜めゲージ
        int gw = 300, gx = GetScreenWidth() / 2 - gw / 2, gy = GetScreenHeight() - 60;
        if (chargingShot >= 0) {
            const ShotSpec *sp = &SHOTS[chargingShot];
            DrawRectangle(gx, gy, gw, 20, Fade(BLACK, 0.6f));
            DrawRectangle(gx, gy, (int)(gw * charge), 20, sp->color);
            if (sp->touchAngle > 0.0f)
                DrawText(TextFormat("%s  %.0f rpm", sp->name, Lerp(sp->minRpm, sp->maxRpm, charge)), gx, gy - 24, 20, RAYWHITE);
            else
                DrawText(TextFormat("%s  %.0f km/h", sp->name, Lerp(sp->minKmh, sp->maxKmh, charge)), gx, gy - 24, 20, RAYWHITE);
        }
        if (chargingServe >= 0) {
            const ServeSpec *sp = &SERVES[chargingServe];
            DrawRectangle(gx, gy, gw, 20, Fade(BLACK, 0.6f));
            DrawRectangle(gx, gy, (int)(gw * charge), 20, sp->color);
            DrawText(TextFormat("%s  %.0f km/h", sp->name, Lerp(sp->minKmh, sp->maxKmh, charge)), gx, gy - 24, 20, RAYWHITE);
        }
        if (slowMo) DrawText("SLOW x0.25", GetScreenWidth() - 140, GetScreenHeight() - 40, 20, RED);
        {
            static const char *camNames[3] = { "BEHIND", "SIDE VIEW", "FOLLOW BALL" };
            DrawText(TextFormat("Camera: %s (C)", camNames[camMode]), 20, GetScreenHeight() - 30, 18, camMode == 0 ? LIGHTGRAY : ORANGE);
        }
        if (botControls) DrawText("AUTOPILOT (B)", GetScreenWidth() - 180, GetScreenHeight() - 70, 20, ORANGE);
        DrawText("Tab: stats", GetScreenWidth() - 120, GetScreenHeight() - 30, 16, LIGHTGRAY);
        {
            static const char *an[3] = { "OFF", "TIMING", "TIMING+SPOT" };
            DrawText(TextFormat("H assist: %s", an[assistLevel]), 260, GetScreenHeight() - 30, 18, assistLevel ? GREEN : LIGHTGRAY);
        }
        EndDrawing();
    }

    LogFlush(&plog, "?");
    if (plog.f) fclose(plog.f);
    CloseWindow();
    return 0;
}
