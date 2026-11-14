// SWEET SPOT — コート・球種・難易度の表と、グローバルな設定
#include "game.h"

SurfaceType gSurface = SURF_HARD;

bool gCrouch = false;
bool  gLefty = false;
float gSpinMul = 1.0f;
float gSliceEdge = 0.0f;
float gErrMul = 1.0f;
float gRiseSkill = 0.0f;   // 上がり際の球の跳ね上がりを面で抑える上手さ（0〜1）
bool  gHumanShot = false;
float gPointImportance = 0.0f;     // 自分の打球を評価している間だけ、しゃがんでいれば true
bool gCasual = false;     // 自分の打球を評価している間だけ、簡単モードなら true

const Surface SURFACES[SURF_COUNT] = {
    //                       mu     e   めり込み 滑りの減速 滑れる速さ
    [SURF_HARD]  = { "HARD",  0.60f, 0.77f, 1.00f, 7.0f, 3.0f, { 40, 90, 160, 255 },  { 40, 110, 80, 255 },  { 245, 245, 245, 255 } },
    [SURF_CLAY]  = { "CLAY",  0.80f, 0.80f, 0.88f, 4.5f, 2.0f, { 196, 98, 52, 255 },  { 170, 85, 45, 255 },   { 245, 240, 230, 255 } },
    [SURF_GRASS] = { "GRASS", 0.38f, 0.70f, 1.00f, 9.0f, 3.5f, { 70, 140, 60, 255 },  { 55, 115, 50, 255 },   { 250, 250, 250, 255 } },
};

const char *STYLE_NAMES[STYLE_COUNT] = { "REAL", "CASUAL" };

// 最後の 2 つ：タイミングの許容幅の倍率、リーチの上乗せ（m）
const ShotSpec SHOTS[SHOT_COUNT] = {
    //                     name         km/h            rpm               高さ(理想,許容) 距離許容 深さ(普通, S, W)    高い弾道 固定角  （末尾：タイミング許容, リーチ, 上の幅）
    [SHOT_FLAT]      = { "FLAT DRIVE", 130.0f, 175.0f, 2300.0f, 2900.0f, 1.20f, 0.60f, 0.60f,  -9.0f, -6.0f, -10.3f, false, 0.0f, RAYWHITE, 0.80f, 0.00f , 0.50f },
    [SHOT_TOP]       = { "TOPSPIN",  105.0f, 155.0f,  2600.0f,  3600.0f, 1.00f, 0.80f, 0.80f,  -9.0f, -6.0f, -10.3f, false, 0.0f, (Color){ 255, 200, 40, 255 }, 1.00f, 0.00f , 0.25f },
    [SHOT_SLICE]     = { "SLICE",     75.0f, 120.0f, -1200.0f, -2200.0f, 0.75f, 1.00f, 1.10f,  -9.0f, -6.0f, -10.3f, false, 0.0f, (Color){ 80, 220, 255, 255 }, 1.30f, 0.25f , 0.30f },
    [SHOT_SLICE_LOB] = { "SLICE LOB", 50.0f,  65.0f,  -800.0f, -1500.0f, 0.80f, 1.00f, 1.10f, -9.5f, -7.5f, -10.0f, true, 50.0f, (Color){ 150, 160, 255, 255 }, 1.40f, 0.30f , 0.30f },
    [SHOT_TOP_LOB]   = { "TOP LOB",   60.0f,  90.0f,  2000.0f,  3500.0f, 1.00f, 0.80f, 0.80f, -9.5f, -7.5f, -10.0f, false, 40.0f, (Color){ 255, 130, 40, 255 }, 1.00f, 0.00f , 0.25f },
    [SHOT_DROP]      = { "DROP",       0.0f,   0.0f, -1500.0f, -2500.0f, 0.90f, 0.90f, 0.90f,  -2.5f, -1.8f,  -4.0f, false, 28.0f, (Color){ 200, 120, 255, 255 }, 1.10f, 0.10f , 0.30f },
};

// 選手の見た目：髪、ショーツ、ヘッドバンド（なしは a=0）、優雅さ
void ApplyLook(SwingAnim *a, int persona)
{
    static const struct { Color hair, shorts, band; float grace; int fhStyle; } LOOK[PERSONA_COUNT] = {
        { {  70,  50,  35, 255 }, {  40,  50,  90, 255 }, { 0 },                    0.0f, 0 },   // AI（癖なし）
        { {  60,  42,  30, 255 }, { 245, 245, 248, 255 }, { 250, 250, 250, 255 },   1.0f, 0 },   // フェデラー：白いヘッドバンド、大きく優雅なフォロースルー
        { {  28,  24,  24, 255 }, {  30,  40,  80, 255 }, { 0 },                    0.3f, 0 },   // ジョコビッチ
        { {  45,  32,  24, 255 }, {  45,  45,  55, 255 }, { 245, 245, 245, 255 },   0.0f, 1 },   // ナダル：バンダナ、頭の上へ振り上げるフォア
        { { 105,  72,  45, 255 }, {  30,  30,  35, 255 }, { 0 },                    0.2f, 0 },   // マレー
    };
    if (persona < 0) {   // 人間：紺のショーツ
        a->hair = (Color){ 40, 30, 25, 255 }; a->shorts = (Color){ 35, 45, 90, 255 }; a->headband = (Color){ 0 }; a->grace = 0.3f; a->fhStyle = 0;
        return;
    }
    persona %= PERSONA_COUNT;
    a->hair = LOOK[persona].hair; a->shorts = LOOK[persona].shorts; a->headband = LOOK[persona].band; a->grace = LOOK[persona].grace; a->fhStyle = LOOK[persona].fhStyle;
}
