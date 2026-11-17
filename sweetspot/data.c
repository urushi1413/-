// SWEET SPOT — コート・球種・難易度の表と、グローバルな設定
#include "game.h"

SurfaceType gSurface = SURF_HARD;

bool gCrouch = false;
bool  gLefty = false;
float gSpinMul = 1.0f;
float gSliceEdge = 0.0f;
float gErrMul = 1.0f;
float gRiseSkill = 0.0f;
bool  gWomen = false;
float gTimingTolMul = HUMAN_TIMING_TOL;
float gReachAdd = 0.0f;
// 選手の身長（cm、だいたい）
static const float PERSONA_HEIGHT[PERSONA_COUNT] = {
    185,                                       // 癖なしの AI（女子の試合では 175）
    185, 188, 185, 191,                        // フェデラー、ジョコビッチ、ナダル、マレー
    183, 191, 198, 193,                        // アルカラス、シナー、メドベデフ、キルギオス
    175, 176, 182, 175, 188, 166,              // ウィリアムズ、シフィオンテク、サバレンカ、ガウフ、シャラポワ、バーティ
    178, 198,                                  // 錦織、ズベレフ
};
float gHeightDelta = 0.0f;
bool gAllWhite = false;
int gLang = LANG_JA;   // 表示の言語（初めは日本語）
int   gYouPersona = 0;
char  gPlayerName[16] = "PLAYER";
char  gOppName[24] = "AI";
const char *YouName(void) { return gYouPersona ? PERSONAS[gYouPersona].shortName : (gPlayerName[0] ? gPlayerName : "PLAYER"); }
float gYouRage = 0.0f;
float gServeAdd = 0.0f, gServeErrMul = 1.0f, gServeSpeedK = 1.0f;
void RestoreHumanContact(void)
{
    gTimingTolMul = HUMAN_TIMING_TOL;
    gReachAdd = HeightDelta(gYouPersona) * REACH_PER_CM;
    gHeightDelta = HeightDelta(gYouPersona);
}
void ApplyHumanTraits(void)
{
    const Persona *pp = &PERSONAS[gYouPersona % PERSONA_COUNT];
    gLefty = pp->lefty; gSpinMul = pp->spinMul; gSliceEdge = pp->sliceEdge;
    gRiseSkill = fmaxf(pp->risingPref, 0.0f);
    const Mental *mm = &MENTAL[gYouPersona % PERSONA_COUNT];
    float ck = pp->clutch * gPointImportance + (gDecidingSet ? mm->decider * (0.5f + 0.5f * gPointImportance) : 0.0f);
    gErrMul = Clamp(pp->errMul * (1.0f - 0.40f * ck)                               // 勝負強い選手は大事なポイントでミスが減る（弱い選手は増える）
                    * Clamp(1.0f - 1.2f * pp->surfSpeed[gSurface], 0.5f, 1.6f),   // 得意なコートほどミスが減る
                    0.6f, 1.4f)                                                  // 自分が使うときは 0.6〜1.4 倍に収める
             * (1.0f + YOU_RAGE_ERR * gYouRage);                                 // ラケットを叩きつけて燃えている：ミスが増える
    gServeAdd = pp->serveAdd + YOU_RAGE_SERVE * gYouRage;
    gServeErrMul = Clamp(1.0f - 2.0f * pp->serveSkillAdd, 0.6f, 1.4f) * (1.0f + 0.8f * mm->dfNerves * gPointImportance);   // 緊張でサーブがぶれる
    RestoreHumanContact();
}
float PlayerRunSpeed(void)
{
    const Persona *pp = &PERSONAS[gYouPersona % PERSONA_COUNT];
    return (PLAYER_SPEED + pp->speedAdd + pp->surfSpeed[gSurface] + RAGE_SPEED * gYouRage) * (1.0f - HEIGHT_SPEED_PER_CM * HeightDelta(gYouPersona));   // 燃えていると少し速い
}
float HumanServeHeightK(void) { return 1.0f + HeightDelta(gYouPersona) / 185.0f; }
// ツアーごとの選手の並び
static const int MEN_IDS[] = { 1, 2, 3, 4, 5, 6, 7, 8, 15, 16 };
static const int WOMEN_IDS[] = { 9, 10, 11, 12, 13, 14 };
int TourPersonaCount(bool women) { return women ? (int)(sizeof(WOMEN_IDS) / sizeof(int)) : (int)(sizeof(MEN_IDS) / sizeof(int)); }
int TourPersonaAt(bool women, int i) { int n = TourPersonaCount(women); i = ((i % n) + n) % n; return women ? WOMEN_IDS[i] : MEN_IDS[i]; }
int TourPersonaIndex(bool women, int p) { for (int i = 0; i < TourPersonaCount(women); i++) if (TourPersonaAt(women, i) == p) return i; return 0; }
float HeightDelta(int persona) { return persona < 0 ? 0.0f : PersonaHeight(persona) - (gWomen ? 175.0f : 185.0f); }
float PersonaHeight(int persona)
{
    if (persona <= 0) return gWomen ? 175.0f : 185.0f;
    return PERSONA_HEIGHT[persona % PERSONA_COUNT];
}   // 上がり際の球の跳ね上がりを面で抑える上手さ（0〜1）
bool  gRuleNetTouch = true;   // ルール（設定 → ルール）
bool  gRuleBodyShot = true;
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
            { {  50,  35,  25, 255 }, { 245, 245, 248, 255 }, { 0 },                    0.2f, 0 },   // アルカラス
        { { 200,  90,  40, 255 }, {  30,  30,  35, 255 }, { 0 },                    0.2f, 0 },   // シナー：赤毛
        { { 110,  80,  55, 255 }, {  30,  40,  70, 255 }, { 0 },                    0.0f, 0 },   // メドベデフ
        { {  25,  20,  20, 255 }, {  30,  30,  35, 255 }, { 250, 250, 250, 255 },   0.3f, 0 },   // キルギオス：ヘッドバンド
        { {  25,  20,  20, 255 }, {  30,  30,  35, 255 }, { 0 },                    0.2f, 0 },   // ウィリアムズ
        { {  90,  60,  40, 255 }, { 245, 245, 248, 255 }, { 250, 250, 250, 255 },   0.2f, 0 },   // シフィオンテク：キャップ代わりのバンド
        { {  90,  60,  40, 255 }, {  30,  30,  35, 255 }, { 0 },                    0.1f, 0 },   // サバレンカ
        { {  30,  25,  20, 255 }, { 245, 245, 248, 255 }, { 0 },                    0.3f, 0 },   // ガウフ
        { { 220, 190, 120, 255 }, { 245, 245, 248, 255 }, { 0 },                    0.4f, 0 },   // シャラポワ：金髪
        { {  70,  50,  35, 255 }, {  40,  60, 100, 255 }, { 250, 250, 250, 255 },   0.5f, 0 },   // バーティ
        { {  20,  18,  18, 255 }, {  30,  30,  35, 255 }, { 0 },                    0.3f, 0 },   // 錦織
        { { 205, 175, 110, 255 }, { 245, 245, 248, 255 }, { 0 },                    0.2f, 0 },   // ズベレフ：金髪
};
    // 肌の色（だいたい）
    static const Color SKIN[PERSONA_COUNT] = {
        { 236, 196, 158, 255 },
        { 238, 200, 165, 255 }, { 232, 192, 155, 255 }, { 214, 165, 120, 255 }, { 240, 205, 175, 255 },   // フェデラー、ジョコビッチ、ナダル、マレー
        { 215, 168, 125, 255 }, { 242, 208, 180, 255 }, { 240, 205, 175, 255 }, { 200, 145, 105, 255 },   // アルカラス、シナー、メドベデフ、キルギオス
        { 110,  70,  50, 255 }, { 240, 205, 175, 255 }, { 238, 200, 165, 255 }, { 120,  78,  55, 255 },   // ウィリアムズ、シフィオンテク、サバレンカ、ガウフ
        { 242, 210, 185, 255 }, { 210, 160, 120, 255 },                                                   // シャラポワ、バーティ
        { 226, 190, 150, 255 }, { 242, 210, 185, 255 },                                                   // 錦織、ズベレフ
    };
    a->scale = PersonaHeight(persona) / 185.0f;
    if (persona < 0) {   // 人間：紺のショーツ
        a->hair = (Color){ 40, 30, 25, 255 }; a->shorts = (Color){ 35, 45, 90, 255 }; a->headband = (Color){ 0 }; a->grace = 0.3f; a->fhStyle = 0; a->female = gWomen; a->skin = (Color){ 0 };
        if (gAllWhite) a->shorts = (Color){ 245, 245, 248, 255 };
        a->hairStyle = gWomen ? HS_PONY : HS_SHORT; a->beard = 0.0f; a->hat = 0;
        a->eyeCol = (Color){ 95, 62, 38, 255 }; a->faceH = a->faceW = a->jaw = a->noseK = 1.0f;
        a->bShoulder = a->bBulk = a->bArm = a->bLeg = a->bWaist = 1.0f;
        return;
    }
    persona %= PERSONA_COUNT;
    a->female = PERSONAS[persona].female || (persona == 0 && gWomen);
    a->hair = LOOK[persona].hair; a->shorts = LOOK[persona].shorts; a->headband = LOOK[persona].band; a->grace = LOOK[persona].grace; a->fhStyle = LOOK[persona].fhStyle; a->skin = SKIN[persona];
    {   // 髪型・ひげ・かぶりもの（選手ごと）
        static const struct { int hs; float beard; int hat; } HAIR[PERSONA_COUNT] = {
            { HS_SHORT, 0.0f, 0 },        // 癖なし
            { HS_SWEPT, 0.0f, 0 },        // フェデラー：横に流した髪＋ヘッドバンド
            { HS_SHORT, 0.25f, 0 },       // ジョコビッチ：短髪、うっすら無精ひげ
            { HS_NADAL, 0.0f, 0 },        // ナダル：バンダナからはみ出す髪（ひげなし）
            { HS_CURLY_SHORT, 0.45f, 0 }, // マレー：短い巻き毛、無精ひげ
            { HS_CURLY_SHORT, 0.0f, 0 },  // アルカラス：前が高い巻き毛
            { HS_CURLY, 0.0f, 0 },        // シナー：もじゃもじゃの赤毛
            { HS_SHORT, 0.0f, 0 },        // メドベデフ：短髪
            { HS_FADE, 0.60f, 0 },        // キルギオス：横を刈り上げ、ひげ
            { HS_BUN, 0.0f, 0 },          // ウィリアムズ：まとめ髪
            { HS_PONY, 0.0f, 1 },         // シフィオンテク：キャップとポニーテール
            { HS_PONY, 0.0f, 0 },         // サバレンカ：ポニーテール
            { HS_BUN, 0.0f, 0 },          // ガウフ：編み込みをまとめる
            { HS_PONY, 0.0f, 2 },         // シャラポワ：サンバイザーとポニーテール
            { HS_PONY, 0.0f, 0 },         // バーティ：ポニーテール
            { HS_SPIKY, 0.0f, 0 },        // 錦織：立てた短髪
            { HS_SWEPT, 0.0f, 0 },        // ズベレフ：金髪を横に流す
        };
        a->hairStyle = HAIR[persona].hs; a->beard = HAIR[persona].beard; a->hat = HAIR[persona].hat;
        if (persona == 0) a->hairStyle = gWomen ? HS_PONY : HS_SHORT;
        a->hatCol = (Color){ 248, 248, 250, 255 };
    }
    {   // 顔の形と目の色（写真から見た目で。縦長さ・横幅・あごの張り・鼻の長さ）
        const Color BRN = { 95, 62, 38, 255 }, DBR = { 60, 38, 25, 255 }, HAZ = { 120, 95, 50, 255 },
                    GRN = { 85, 120, 75, 255 }, BLU = { 70, 115, 165, 255 }, GRY = { 105, 125, 140, 255 };
        static const struct { float h, w, jaw, nose; int eye; } FACE[PERSONA_COUNT] = {   // 男子（0〜8）
            { 1.00f, 1.00f, 1.00f, 1.00f, 0 },   // 癖なし
            { 1.08f, 0.96f, 0.98f, 1.25f, 0 },   // フェデラー：面長、鼻が高い、茶
            { 1.08f, 0.95f, 0.95f, 1.20f, 3 },   // ジョコビッチ：面長、細いあご、緑がかったヘーゼル
            { 1.03f, 1.02f, 1.08f, 1.10f, 1 },   // ナダル：四角く張ったあご、こげ茶
            { 1.09f, 0.95f, 0.95f, 1.05f, 4 },   // マレー：面長、青
            { 1.04f, 0.99f, 1.05f, 1.00f, 0 },   // アルカラス：しっかりしたあご、茶
            { 1.07f, 0.93f, 0.92f, 1.10f, 4 },   // シナー：細面、青
            { 1.12f, 0.92f, 0.92f, 1.20f, 5 },   // メドベデフ：とても面長で細い、灰色がかった青
            { 1.03f, 1.03f, 1.06f, 1.05f, 0 },   // キルギオス：横幅があり、あごが張る、茶
        };
        static const struct { float h, w, jaw, nose; int eye; } F2[PERSONA_COUNT] = {   // 女子（9〜14）と錦織・ズベレフ
            { 0 }, { 0 }, { 0 }, { 0 }, { 0 }, { 0 }, { 0 }, { 0 }, { 0 },
            { 1.00f, 1.04f, 1.06f, 1.00f, 1 },   // ウィリアムズ：横幅があり、あごが張る、こげ茶
            { 1.02f, 0.97f, 0.95f, 0.95f, 2 },   // シフィオンテク：卵形、ヘーゼル
            { 1.03f, 1.00f, 1.04f, 0.95f, 3 },   // サバレンカ：しっかりしたあご、緑がかったヘーゼル
            { 1.03f, 0.97f, 0.96f, 0.95f, 1 },   // ガウフ：卵形、こげ茶
            { 1.08f, 0.95f, 0.97f, 1.00f, 3 },   // シャラポワ：面長、緑
            { 0.99f, 1.00f, 1.00f, 0.90f, 2 },   // バーティ：丸め、ヘーゼル
            { 1.00f, 1.00f, 0.98f, 0.85f, 1 },   // 錦織：丸め、鼻は小さめ、こげ茶
            { 1.10f, 0.94f, 0.98f, 1.10f, 4 },   // ズベレフ：面長、青
        };
        float h, w, jw, ns; int e;
        if (persona >= 9) { h = F2[persona].h; w = F2[persona].w; jw = F2[persona].jaw; ns = F2[persona].nose; e = F2[persona].eye; }
        else { h = FACE[persona].h; w = FACE[persona].w; jw = FACE[persona].jaw; ns = FACE[persona].nose; e = FACE[persona].eye; }
        const Color EC[6] = { BRN, DBR, HAZ, GRN, BLU, GRY };
        a->faceH = h; a->faceW = w; a->jaw = jw; a->noseK = ns; a->eyeCol = EC[e];
        if (persona == 13) a->eyeCol = (Color){ 80, 125, 85, 255 };   // シャラポワは緑がはっきり
    }
    {   // 体つき（肩幅・胴の厚み・腕・脚・腰回り）。身長は別（scale）
        static const float BODY[PERSONA_COUNT][5] = {
            { 1.00f, 1.00f, 1.00f, 1.00f, 1.00f },   // 癖なし
            { 1.00f, 1.00f, 1.00f, 1.00f, 1.00f },   // フェデラー：標準的なアスリート体型
            { 0.97f, 0.90f, 0.90f, 0.92f, 0.92f },   // ジョコビッチ：細身でしなやか
            { 1.08f, 1.12f, 1.24f, 1.10f, 1.00f },   // ナダル：筋肉質、太い腕
            { 0.98f, 0.94f, 0.95f, 0.96f, 0.95f },   // マレー：やや細身
            { 1.05f, 1.08f, 1.10f, 1.12f, 1.00f },   // アルカラス：がっしり、太い脚
            { 0.92f, 0.86f, 0.85f, 0.88f, 0.88f },   // シナー：とても細い
            { 0.94f, 0.86f, 0.86f, 0.88f, 0.90f },   // メドベデフ：ひょろ長い
            { 1.08f, 1.12f, 1.10f, 1.06f, 1.08f },   // キルギオス：肩幅が広く、厚みがある
            { 1.08f, 1.15f, 1.15f, 1.15f, 1.08f },   // ウィリアムズ：力強い体
            { 1.00f, 1.00f, 1.00f, 1.04f, 0.96f },   // シフィオンテク：引き締まった体、強い脚
            { 1.06f, 1.08f, 1.10f, 1.08f, 1.00f },   // サバレンカ：肩幅が広く、力強い
            { 0.97f, 0.92f, 0.92f, 0.95f, 0.92f },   // ガウフ：細身で手足が長い
            { 0.96f, 0.92f, 0.92f, 0.94f, 0.92f },   // シャラポワ：細身
            { 1.00f, 1.03f, 1.02f, 1.06f, 0.98f },   // バーティ：小柄でがっしり
            { 0.97f, 0.96f, 0.96f, 1.00f, 0.96f },   // 錦織：小柄で引き締まった体
            { 0.98f, 0.92f, 0.92f, 0.94f, 0.94f },   // ズベレフ：細身で長身
        };
        #define BX(v) (1.0f + 1.6f * ((v) - 1.0f))   // 差を 1.6 倍に（遠くから見ても分かるように）
        a->bShoulder = BX(BODY[persona][0]); a->bBulk = BX(BODY[persona][1]); a->bArm = BX(BODY[persona][2]); a->bLeg = BX(BODY[persona][3]); a->bWaist = BX(BODY[persona][4]);
        #undef BX
    }
    if (gAllWhite) {   // ウィンブルドン：服は白だけ
        a->shorts = (Color){ 245, 245, 248, 255 };
        if (a->headband.a) a->headband = (Color){ 250, 250, 250, 255 };
    }
}
Color PersonaShirt(int persona) { return gAllWhite ? (Color){ 248, 248, 250, 255 } : PERSONAS[persona].shirt; }
