// 3D テニス：ステップ1「ボールが重力で落ちてバウンドする」
// 単位はすべてメートル・秒。座標系: x=横, y=上, z=縦（自分側が +z、相手側が -z）
#include "raylib.h"
#include "raymath.h"
#include <math.h>
#include <stdbool.h>

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
#define GRAVITY          9.81f
#define BOUNCE_E         0.75f   // 垂直方向の反発係数。2.54m から落として 1.43m 跳ねる（規格は 1.35〜1.47m）
#define BOUNCE_FRICTION  0.80f   // バウンド時に水平速度が残る割合（後で回転に応じて変える）
#define ROLL_FRICTION    1.5f    // 転がり中の減速（1/秒）

#define PHYS_DT          (1.0f / 240.0f)   // 物理は固定ステップで回す（速い球でもすり抜けない）
#define TRAIL_N          600

typedef enum { RESULT_NONE, RESULT_IN, RESULT_OUT, RESULT_NET } ShotResult;

typedef struct {
    Vector3 pos;
    Vector3 vel;
    bool    active;
    bool    hitNet;
    int     bounces;
    float   peakAfterFirstBounce;
    ShotResult result;
} Ball;

// ネットは中央 0.914m、ポスト 1.07m。その間は直線でたるむとみなす
static float NetHeightAt(float x)
{
    float t = fminf(fabsf(x) / NET_POST_X, 1.0f);
    return NET_H_CENTER + (NET_H_POST - NET_H_CENTER) * t;
}

static void JudgeFirstBounce(Ball *b)
{
    if (b->hitNet) { b->result = RESULT_NET; return; }
    // ラインに少しでも触れたらイン
    bool inX = fabsf(b->pos.x) <= SINGLES_W * 0.5f + BALL_R;
    bool inZ = b->pos.z < 0.0f && b->pos.z >= -HALF_LEN - BALL_R;
    b->result = (inX && inZ) ? RESULT_IN : RESULT_OUT;
}

// 1ステップ分ボールを進める。描画にも予測にも同じ関数を使う
static void StepBall(Ball *b, float dt)
{
    if (!b->active) return;

    // 力: 今は重力だけ。次のステップで空気抵抗とマグヌス力をここに足す
    Vector3 acc = { 0.0f, -GRAVITY, 0.0f };

    b->vel = Vector3Add(b->vel, Vector3Scale(acc, dt));
    Vector3 prev = b->pos;
    b->pos = Vector3Add(b->pos, Vector3Scale(b->vel, dt));

    // ネット（z=0 の面）を横切ったか
    bool crossed = (prev.z > 0.0f) != (b->pos.z > 0.0f);
    if (crossed && fabsf(b->pos.x) < NET_POST_X) {
        if (b->pos.y - BALL_R < NetHeightAt(b->pos.x)) {
            b->hitNet = true;
            b->pos.z = prev.z;
            b->vel.z *= -0.15f;   // ネットに吸われてほぼ止まる
            b->vel.x *= 0.3f;
            b->vel.y *= 0.3f;
        }
    }

    // 地面
    if (b->pos.y < BALL_R) {
        b->pos.y = BALL_R;
        if (b->vel.y < -0.3f) {
            b->vel.y = -b->vel.y * BOUNCE_E;
            b->vel.x *= BOUNCE_FRICTION;
            b->vel.z *= BOUNCE_FRICTION;
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

// 打ち出し角を変えて試し打ちし、最初のバウンド地点の z を返す
// （物理をシミュレーションで予測するので、空気抵抗や回転を足してもそのまま使える）
static float PredictLandingZ(Vector3 from, Vector3 dirFlat, float speed, float angle)
{
    Ball b = { 0 };
    b.pos = from;
    b.vel = Vector3Add(Vector3Scale(dirFlat, speed * cosf(angle)),
                       (Vector3){ 0.0f, speed * sinf(angle), 0.0f });
    b.active = true;
    for (int i = 0; i < 240 * 5 && b.active; i++) {
        StepBall(&b, PHYS_DT);
        if (b.hitNet) return HALF_LEN;   // ネット = 「短すぎ」扱い
        if (b.bounces > 0) return b.pos.z;
    }
    return -100.0f;
}

// 狙った地点に落ちる打ち出し角を二分探索で求める
static float SolveLaunchAngle(Vector3 from, Vector3 target, float speed)
{
    Vector3 d = { target.x - from.x, 0.0f, target.z - from.z };
    Vector3 dirFlat = Vector3Normalize(d);
    float lo = -15.0f * DEG2RAD, hi = 40.0f * DEG2RAD;
    for (int i = 0; i < 30; i++) {
        float mid = 0.5f * (lo + hi);
        float z = PredictLandingZ(from, dirFlat, speed, mid);
        if (z > target.z) lo = mid;   // まだ手前に落ちる → 角度を上げる
        else              hi = mid;
    }
    return 0.5f * (lo + hi);
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

int main(void)
{
    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_WINDOW_RESIZABLE);
    InitWindow(1280, 720, "Tennis - step1: gravity & bounce");
    SetTargetFPS(60);

    Camera3D cam = { 0 };
    cam.up = (Vector3){ 0, 1, 0 };
    cam.fovy = 45.0f;
    cam.projection = CAMERA_PERSPECTIVE;
    int camMode = 0;

    Ball ball = { 0 };
    Vector3 trail[TRAIL_N];
    int trailCount = 0;
    float trailTimer = 0.0f;

    Vector3 hitPoint = { 0.0f, 1.0f, HALF_LEN + 0.3f };   // 打点（ベースライン少し後ろ、高さ1m）
    Vector3 target   = { 0.0f, 0.0f, -9.0f };              // 狙う着地点
    float speedKmh = 90.0f;
    float lastAngleDeg = 0.0f;
    float lastSpeedKmh = 0.0f;
    bool slowMo = false;
    float accumulator = 0.0f;
    ball.pos = hitPoint;   // 最初は打点に置いておく

    while (!WindowShouldClose()) {
        float frameDt = fminf(GetFrameTime(), 0.1f);

        // ---- 入力 ----
        if (IsKeyDown(KEY_UP))    speedKmh = fminf(speedKmh + 60.0f * frameDt, 220.0f);
        if (IsKeyDown(KEY_DOWN))  speedKmh = fmaxf(speedKmh - 60.0f * frameDt, 30.0f);
        if (IsKeyDown(KEY_LEFT))  target.x = fmaxf(target.x - 4.0f * frameDt, -SINGLES_W * 0.5f);
        if (IsKeyDown(KEY_RIGHT)) target.x = fminf(target.x + 4.0f * frameDt,  SINGLES_W * 0.5f);
        if (IsKeyDown(KEY_W))     target.z = fmaxf(target.z - 4.0f * frameDt, -HALF_LEN);
        if (IsKeyDown(KEY_S))     target.z = fminf(target.z + 4.0f * frameDt, -1.0f);
        if (IsKeyPressed(KEY_C))  camMode = (camMode + 1) % 3;
        if (IsKeyPressed(KEY_T))  slowMo = !slowMo;

        if (IsKeyPressed(KEY_SPACE)) {
            float v = speedKmh / 3.6f;
            float ang = SolveLaunchAngle(hitPoint, target, v);
            Vector3 dirFlat = Vector3Normalize((Vector3){ target.x - hitPoint.x, 0, target.z - hitPoint.z });
            ball = (Ball){ 0 };
            ball.pos = hitPoint;
            ball.vel = Vector3Add(Vector3Scale(dirFlat, v * cosf(ang)), (Vector3){ 0, v * sinf(ang), 0 });
            ball.active = true;
            lastAngleDeg = ang * RAD2DEG;
            lastSpeedKmh = speedKmh;
            trailCount = 0;
        }
        if (IsKeyPressed(KEY_R)) {
            // ITF のバウンドテスト: 2.54m から落とす
            ball = (Ball){ 0 };
            ball.pos = (Vector3){ 0.0f, 2.54f, -3.0f };
            ball.active = true;
            trailCount = 0;
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

        // ---- カメラ ----
        if (camMode == 0) {        // ベースライン後方（プレイヤー視点）
            cam.position = (Vector3){ 0.0f, 4.0f, HALF_LEN + 7.0f };
            cam.target   = (Vector3){ 0.0f, 0.5f, -3.0f };
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

            // 狙い
            DrawCircle3D((Vector3){ target.x, 0.01f, target.z }, 0.3f, (Vector3){ 1, 0, 0 }, 90.0f, ORANGE);

            // プレイヤーの仮置き（スケール感用）
            DrawCapsule((Vector3){ hitPoint.x + 0.5f, 0.3f, hitPoint.z + 0.2f },
                        (Vector3){ hitPoint.x + 0.5f, 1.5f, hitPoint.z + 0.2f }, 0.25f, 8, 4, (Color){ 230, 120, 60, 255 });

            // 軌跡
            for (int i = 1; i < trailCount; i++) DrawLine3D(trail[i - 1], trail[i], YELLOW);

            // 影（高さが分かるようにするのはテニスゲームでは超重要）
            DrawCircle3D((Vector3){ ball.pos.x, 0.006f, ball.pos.z }, BALL_R * 1.5f, (Vector3){ 1, 0, 0 }, 90.0f,
                         (Color){ 0, 0, 0, 120 });
            DrawSphere(ball.pos, BALL_R, (Color){ 210, 240, 40, 255 });
        EndMode3D();

        float spd = Vector3Length(ball.vel) * 3.6f;
        DrawRectangle(10, 10, 360, 190, Fade(BLACK, 0.5f));
        DrawText(TextFormat("Shot speed : %.0f km/h  (UP/DOWN)", speedKmh), 20, 20, 18, RAYWHITE);
        DrawText(TextFormat("Target     : x=%.1f z=%.1f (arrows, W/S)", target.x, target.z), 20, 42, 18, RAYWHITE);
        DrawText(TextFormat("Last shot  : %.0f km/h @ %.1f deg", lastSpeedKmh, lastAngleDeg), 20, 64, 18, RAYWHITE);
        DrawText(TextFormat("Ball       : %.0f km/h  h=%.2f m", spd, ball.pos.y), 20, 86, 18, RAYWHITE);
        DrawText(TextFormat("Bounces    : %d   Result: %s", ball.bounces, ResultText(ball.result)), 20, 108, 18, RAYWHITE);
        DrawText(TextFormat("1st bounce peak: %.2f m", ball.peakAfterFirstBounce), 20, 130, 18, RAYWHITE);
        DrawText("SPACE hit  R drop test  C camera  T slow-mo", 20, 160, 16, LIGHTGRAY);
        if (slowMo) DrawText("SLOW x0.25", GetScreenWidth() - 140, 20, 20, RED);
        EndDrawing();
    }

    CloseWindow();
    return 0;
}
