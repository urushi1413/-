// SWEET SPOT — リプレイとハイライト
//   試合中は毎フレーム、球と両選手の位置・動きを記録しておく（最後の 25 秒）。
//   ポイントが終わったら R で今のポイントをスローで見直せる。見応えのあるポイントはハイライトとして取っておき、試合の後に続けて見られる
#include "game.h"

#define REC_MAX 1500
static RFrame gRec[REC_MAX];
static long   gRecTotal = 0;                 // これまでに記録したフレームの数（通し番号）

#define HL_MAX    6
#define CLIP_MAX  900
static RFrame gHL[HL_MAX][CLIP_MAX];
static int    gHLn[HL_MAX];
static float  gHLscore[HL_MAX];
static long   gHLorder[HL_MAX];             // 試合の中での順番
static char   gHLcap[HL_MAX][80];
static int    gHLcount = 0;

static RFrame gClip[CLIP_MAX];              // 再生中のクリップ（今のポイント）
static int    gClipN = 0;

void RecReset(void) { gRecTotal = 0; gHLcount = 0; gClipN = 0; }
void RecPush(const RFrame *f) { gRec[gRecTotal % REC_MAX] = *f; gRecTotal++; }
long RecTotal(void) { return gRecTotal; }

// start から今までのフレームを dst にまとめる（長すぎるときは最後の max フレーム）
static int CopySince(long start, RFrame *dst, int max)
{
    if (start < gRecTotal - REC_MAX) start = gRecTotal - REC_MAX;
    if (start < 0) start = 0;
    long n = gRecTotal - start;
    if (n > max) { start = gRecTotal - max; n = max; }
    for (long i = 0; i < n; i++) dst[i] = gRec[(start + i) % REC_MAX];
    // 頭の待ち時間（サーブの前）は削る：球が動き出す 0.4 秒前から
    int first = 0;
    while (first < n && !dst[first].ballOn) first++;
    first = first > 24 ? first - 24 : 0;
    if (first > 0 && first < n) { for (long i = first; i < n; i++) dst[i - first] = dst[i]; n -= first; }
    return (int)n;
}

// 今のポイントを再生用に用意する
int RecPrepareLast(long start) { gClipN = CopySince(start, gClip, CLIP_MAX); return gClipN; }
const RFrame *RecClip(int *n) { *n = gClipN; return gClip; }

// ポイントをハイライトに取っておく（点数が高いものを HL_MAX 個まで）
void RecSaveHighlight(long start, const char *caption, float score)
{
    int slot = -1;
    if (gHLcount < HL_MAX) slot = gHLcount++;
    else {
        int lo = 0;
        for (int i = 1; i < HL_MAX; i++) if (gHLscore[i] < gHLscore[lo]) lo = i;
        if (score <= gHLscore[lo]) return;
        slot = lo;
    }
    gHLn[slot] = CopySince(start, gHL[slot], CLIP_MAX);
    gHLscore[slot] = score;
    gHLorder[slot] = start;
    snprintf(gHLcap[slot], sizeof(gHLcap[slot]), "%s", caption);
}
int HighlightCount(void) { return gHLcount; }
// 試合の順番で k 番目のハイライト
const RFrame *Highlight(int k, int *n, const char **caption)
{
    int idx[HL_MAX];
    for (int i = 0; i < gHLcount; i++) idx[i] = i;
    for (int i = 0; i < gHLcount; i++)
        for (int j = i + 1; j < gHLcount; j++)
            if (gHLorder[idx[j]] < gHLorder[idx[i]]) { int t = idx[i]; idx[i] = idx[j]; idx[j] = t; }
    int s = idx[k % gHLcount];
    *n = gHLn[s]; *caption = gHLcap[s];
    return gHL[s];
}

// テレビ中継のような横からのカメラ。球を追う
Camera3D ReplayCamera(int view, Vector3 ball, Camera3D prev, bool first)
{
    Camera3D c = { 0 };
    c.up = (Vector3){ 0, 1, 0 };
    c.fovy = 34.0f;
    c.projection = CAMERA_PERSPECTIVE;
    Vector3 pos, tgt;
    switch (view) {
        case 1:   // 自分の後ろの高いところから（中継のメインカメラ）
            pos = (Vector3){ ball.x * 0.25f, 7.0f, HALF_LEN + 12.0f };
            tgt = (Vector3){ ball.x * 0.35f, 0.5f, -2.0f };
            c.fovy = 40.0f;
            break;
        case 2:   // ボールを追う（近く）
            pos = Vector3Add(ball, (Vector3){ 3.5f, 1.6f, 5.0f });
            tgt = ball;
            c.fovy = 45.0f;
            break;
        case 3:   // 相手の後ろの高いところから
            pos = (Vector3){ -ball.x * 0.25f, 7.0f, -(HALF_LEN + 12.0f) };
            tgt = (Vector3){ ball.x * 0.35f, 0.5f, 2.0f };
            c.fovy = 40.0f;
            break;
        default:  // 横の観客席の外から、コート全体が入るように（球の方へ少しずつ振る）
            pos = (Vector3){ 24.0f, 7.5f, Clamp(ball.z * 0.45f, -6.0f, 6.0f) };
            tgt = (Vector3){ 0.0f, 0.8f, Clamp(ball.z * 0.75f, -9.0f, 9.0f) };
            break;
    }
    if (first) { c.position = pos; c.target = tgt; return c; }
    c.position = Vector3Lerp(prev.position, pos, 0.08f);
    c.target = Vector3Lerp(prev.target, tgt, 0.12f);
    return c;
}

// 記録した 1 フレームを描く（コート・観客席・両選手・球）
void DrawReplayFrame(const RFrame *f, Camera3D cam)
{
    DrawCourt();
    DrawStadium(cam.position, f->ball, 0.0f, (float)GetTime(), (Vector3){ 0 }, 0.0f);
    DrawFigure(f->pPos, true, f->pShirt, &f->pA, 0.0f);
    DrawFigure(f->aPos, false, f->aShirt, &f->aA, 0.0f);
    if (f->ballOn) {
        float d = Vector3Distance(cam.position, f->ball);
        float vr = Lerp(BALL_R, fmaxf(BALL_R * 1.4f, d * 0.004f), Smooth01((f->ball.y - BALL_R - 0.05f) / 0.6f));
        DrawBallShadow(f->ball, vr);
        DrawSphere(f->ball, vr, (Color){ 235, 255, 40, 255 });
    }
}
// リプレイのボールの残像：記録したフレーム（1/60 秒ごと）の、今のフレームまでの FX_TRAIL_TIME 秒ぶん。DrawReplayFrame の後に呼ぶ
void DrawReplayAfterimage(const RFrame *fr, int idx, Camera3D cam)
{
    if (!FX_BALL_TRAIL || idx < 1 || !fr[idx].ballOn) return;
    Vector3 pts[FX_TRAIL_N + 1];
    int m = 0, want = (int)(FX_TRAIL_TIME * 60.0f) + 1;
    if (want > FX_TRAIL_N) want = FX_TRAIL_N;
    int first = idx;
    while (first > 0 && idx - first < want && fr[first - 1].ballOn && Vector3Distance(fr[first - 1].ball, fr[first].ball) < 2.5f) first--;   // 球が置き直された所（次のサーブなど）はつながない
    for (int i = first; i <= idx; i++) pts[m++] = fr[i].ball;
    if (m < 2) return;
    float speed = Vector3Distance(fr[idx].ball, fr[idx - 1].ball) / fmaxf(fr[idx].dt, 1e-3f);
    float d = Vector3Distance(cam.position, fr[idx].ball);
    float vr = Lerp(BALL_R, fmaxf(BALL_R * 1.4f, d * 0.004f), Smooth01((fr[idx].ball.y - BALL_R - 0.05f) / 0.6f));
    DrawBallAfterimage(pts, m, cam.position, vr, Smooth01((speed - FX_TRAIL_V0) / (FX_TRAIL_V1 - FX_TRAIL_V0)), (Color){ 248, 255, 170, 255 });
}
