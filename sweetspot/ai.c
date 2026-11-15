// SWEET SPOT — 相手（AI）と、走る計算（加速・減速）
#include "game.h"

// ================= 相手（AI） =================
// AI は自分（プレイヤー）とまったく同じ物理・同じ打点の評価・同じショットの関数を使う。
// コートを縦軸まわりに 180° 回した「AI から見た座標」に変換して、プレイヤー用の関数をそのまま呼ぶ。
// 180° 回転なので、位置・速度・回転（角速度）すべて (x, y, z) → (-x, y, -z) で変換できる
Vector3 Rot180(Vector3 v) { return (Vector3){ -v.x, v.y, -v.z }; }

Ball RotBall(Ball b)
{
    b.pos = Rot180(b.pos);
    b.vel = Rot180(b.vel);
    b.spin = Rot180(b.spin);
    b.side = -b.side;
    float x0 = b.boxX0, x1 = b.boxX1;
    b.boxX0 = -x1; b.boxX1 = -x0;
    return b;
}

const AiLevel AI_LEVELS[3] = {
    //          走る  反応   タイミング 位置   強気   見極め  1st     2nd     サーブの安定感
    //                                                                                コースの厳しさ 深さ
    // EASY は初心者の友達くらい：足が遅く、反応が遅れ、当たりがぶれ、サーブは遅くて真ん中へ
    { "EASY",   4.3f, 0.45f, 34.0f, 0.55f, 0.12f, 0.60f, 130.0f,  95.0f, 0.45f, 0.95f, 5.2f },
    { "NORMAL", 5.6f, 0.28f, 12.0f, 0.20f, 0.50f, 0.20f, 175.0f, 120.0f, 0.70f, 0.42f, 5.8f },
    { "PRO",    6.4f, 0.20f,  6.0f, 0.08f, 0.80f, 0.06f, 205.0f, 135.0f, 0.86f, 0.28f, 6.0f },
};

// 追い込まれたとき（ブレークされて 2 ゲーム差・セットを落とした）にラケットを叩きつける確率。叩きつけると、しばらく強くなる
const float RAGE_SMASH[PERSONA_COUNT] = { 0.0f, 0.0f, 0.75f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.55f, 0, 0, 0, 0, 0, 0, 0.0f, 0.40f };   // ジョコビッチ、キルギオス、ズベレフ

// ポイント後のしぐさ。大事なポイント（と長いラリー、少しだけ普通のウィナー）は winChance、セットと試合は必ず。マレーは落とすと怒る
const CelebStyle CELEB_STYLE[PERSONA_COUNT] = {
    // big                                                      set                                   match                              lose                                  win    lose
    { { CELEB_FIST, CELEB_RACKET_UP, 0 },                       { CELEB_ARMS_UP, CELEB_RACKET_UP },   { CELEB_ARMS_UP, CELEB_RACKET_UP }, { 0, 0 },                             0.30f, 0.00f },   // AI
    { { CELEB_FIST, CELEB_FIST, CELEB_RACKET_POINT },           { CELEB_RACKET_UP, CELEB_ARMS_UP },   { CELEB_KNEES, CELEB_ARMS_UP },     { 0, 0 },                             0.55f, 0.00f },   // フェデラー
    { { CELEB_ROAR, CELEB_EAR, CELEB_RACKET_UP },               { CELEB_ROAR, CELEB_ARMS_UP },        { CELEB_HEART, CELEB_ROAR },        { 0, 0 },                             0.75f, 0.00f },   // ジョコビッチ
    { { CELEB_VAMOS, CELEB_VAMOS, CELEB_JUMP_FIST },            { CELEB_JUMP_FIST, CELEB_VAMOS },     { CELEB_KNEES, CELEB_JUMP_FIST },   { 0, 0 },                             0.90f, 0.00f },   // ナダル
    { { CELEB_COMEON, CELEB_COMEON, CELEB_JUMP_FIST },          { CELEB_COMEON, CELEB_ARMS_UP },      { CELEB_ARMS_UP, CELEB_KNEES },     { CELEB_RANT, CELEB_HANDS_HEAD },     0.85f, 0.55f },   // マレー
    { { CELEB_JUMP_FIST, CELEB_EAR, CELEB_RACKET_POINT },       { CELEB_JUMP_FIST, CELEB_ARMS_UP },   { CELEB_KNEES, CELEB_ARMS_UP },     { 0, 0 },                             0.85f, 0.00f },   // アルカラス：跳んで拳、観客をあおる
    { { CELEB_FIST, CELEB_FIST, 0 },                           { CELEB_FIST, CELEB_RACKET_UP },      { CELEB_ARMS_UP, CELEB_RACKET_UP }, { 0, 0 },                             0.40f, 0.00f },   // シナー：静か
    { { CELEB_SHRUG, CELEB_SHRUG, CELEB_FIST },                 { CELEB_SHRUG, CELEB_RACKET_UP },     { CELEB_SHRUG, CELEB_SHRUG },       { CELEB_HANDS_HEAD, CELEB_RANT },     0.25f, 0.35f },   // メドベデフ：喜ばない（勝っても手を少し広げるだけ）
    { { CELEB_RACKET_POINT, CELEB_ROAR, CELEB_JUMP_FIST },      { CELEB_ROAR, CELEB_RACKET_POINT },   { CELEB_ROAR, CELEB_ARMS_UP },      { CELEB_RANT, CELEB_HANDS_HEAD },     0.70f, 0.60f },   // キルギオス：観客をあおる、すぐ怒る
    { { CELEB_COMEON, CELEB_ROAR, CELEB_FIST },                { CELEB_ROAR, CELEB_ARMS_UP },        { CELEB_ARMS_UP, CELEB_KNEES },     { 0, 0 },                             0.85f, 0.00f },   // ウィリアムズ：カモン！と吠える
    { { CELEB_FIST, CELEB_FIST, CELEB_JUMP_FIST },             { CELEB_JUMP_FIST, CELEB_ARMS_UP },   { CELEB_KNEES, CELEB_ARMS_UP },     { 0, 0 },                             0.60f, 0.00f },   // シフィオンテク
    { { CELEB_ROAR, CELEB_COMEON, 0 },                         { CELEB_ROAR, CELEB_ARMS_UP },        { CELEB_ARMS_UP, CELEB_ROAR },      { CELEB_HANDS_HEAD, 0 },              0.75f, 0.25f },   // サバレンカ：吠える
    { { CELEB_FIST, CELEB_JUMP_FIST, 0 },                      { CELEB_JUMP_FIST, CELEB_ARMS_UP },   { CELEB_KNEES, CELEB_ARMS_UP },     { 0, 0 },                             0.60f, 0.00f },   // ガウフ
    { { CELEB_FIST, CELEB_COMEON, 0 },                         { CELEB_FIST, CELEB_ARMS_UP },        { CELEB_KNEES, CELEB_ARMS_UP },     { 0, 0 },                             0.75f, 0.00f },   // シャラポワ：拳を握って背を向ける
    { { CELEB_FIST, CELEB_RACKET_POINT, 0 },                   { CELEB_RACKET_UP, CELEB_ARMS_UP },   { CELEB_HANDS_HEAD, CELEB_ARMS_UP },{ 0, 0 },                             0.45f, 0.00f },   // バーティ：控えめ
    { { CELEB_FIST, CELEB_FIST, CELEB_RACKET_POINT },           { CELEB_FIST, CELEB_ARMS_UP },        { CELEB_ARMS_UP, CELEB_KNEES },     { 0, 0 },                             0.55f, 0.00f },   // 錦織：小さく「カモン」
    { { CELEB_ROAR, CELEB_FIST, CELEB_RACKET_UP },              { CELEB_ROAR, CELEB_ARMS_UP },        { CELEB_KNEES, CELEB_ARMS_UP },     { CELEB_RANT, 0 },                    0.65f, 0.25f },   // ズベレフ：吠える、ときどき怒る
};

// しぐさの長さ
// サーブ前の儀式・ポイント間のしぐさ・プレッシャー（リサーチより）
//   ジョコビッチ：ボールを何度もつく（ふつう 10 回前後、大事なポイントで 20 回近く）。勝負強さは一番
//   ナダル：パンツを引っぱり、肩・鼻・耳を触る儀式。勝負強い
//   フェデラー：髪を耳にかけ、2〜3 回つくだけで速い
//   シャラポワ：ポイントの間は後ろを向いてガットを直す
//   ズベレフ：長くプレッシャーに弱いと言われた（グランドスラム決勝 3 連敗、大事なポイントのダブルフォールト）が、
//             2026 年に全仏と全米を取って克服（その年のグランドスラムは 25 勝 2 敗）。ダブルフォールトの癖と、締めくくりの緊張は少し残る
//   錦織：最終セットの勝率が歴代 3 位（72.5%）
//   キルギオス：アンダーサーブ、先にセットを取られると気持ちが切れる
//   サバレンカ・ガウフ：プレッシャーでダブルフォールトが増える時期があった
//   メドベデフ：あまり喜ばない、判定や観客にいら立って崩れる
const Mental MENTAL[PERSONA_COUNT] = {
    //  サーブ前        つく回数    間のしぐさ       DF緊張 最終set 崩れ  切れる アンダー
    { RIT_BOUNCE,  3,  4, 1,  0,           0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f },   // 癖なし
    { RIT_HAIR,    2,  3, 0,  RIT_HAIR,    0.35f, 0.00f, 0.10f, 0.00f, 0.00f, 0.00f },   // フェデラー
    { RIT_BOUNCE,  8, 13, 6,  0,           0.00f, 0.00f, 0.60f, 0.00f, 0.00f, 0.00f },   // ジョコビッチ
    { RIT_NADAL,   4,  7, 2,  RIT_NADAL,   0.60f, 0.00f, 0.30f, 0.00f, 0.00f, 0.00f },   // ナダル
    { RIT_BOUNCE,  3,  5, 2,  0,           0.00f, 0.00f, 0.20f, 0.25f, 0.00f, 0.00f },   // マレー
    { RIT_BOUNCE,  2,  4, 1,  0,           0.00f, 0.00f, 0.30f, 0.10f, 0.00f, 0.00f },   // アルカラス
    { RIT_BOUNCE,  2,  3, 0,  0,           0.00f, 0.00f, 0.30f, 0.00f, 0.00f, 0.00f },   // シナー
    { RIT_BOUNCE,  3,  6, 2,  0,           0.00f, 0.10f, 0.10f, 0.50f, 0.00f, 0.00f },   // メドベデフ
    { RIT_BOUNCE,  1,  2, 0,  0,           0.00f, 0.00f, 0.00f, 0.50f, 0.40f, 0.06f },   // キルギオス
    { RIT_BOUNCE,  4,  5, 1,  0,           0.00f, 0.00f, 0.30f, 0.10f, 0.00f, 0.00f },   // ウィリアムズ
    { RIT_HAIR,    3,  4, 1,  RIT_HAIR,    0.50f, 0.10f, 0.20f, 0.20f, 0.00f, 0.00f },   // シフィオンテク
    { RIT_BOUNCE,  3,  5, 2,  0,           0.00f, 0.70f, 0.00f, 0.35f, 0.00f, 0.00f },   // サバレンカ
    { RIT_BOUNCE,  3,  4, 1,  0,           0.00f, 0.60f, 0.10f, 0.30f, 0.00f, 0.00f },   // ガウフ
    { RIT_STRINGS, 2,  2, 0,  RIT_STRINGS, 0.70f, 0.10f, 0.30f, 0.00f, 0.00f, 0.00f },   // シャラポワ
    { RIT_BOUNCE,  2,  3, 0,  0,           0.00f, 0.00f, 0.20f, 0.00f, 0.00f, 0.00f },   // バーティ
    { RIT_BOUNCE,  3,  5, 1,  0,           0.00f, 0.10f, 0.90f, 0.10f, 0.00f, 0.00f },   // 錦織
    { RIT_BOUNCE,  4,  6, 3,  0,           0.00f, 0.30f, 0.20f, 0.15f, 0.00f, 0.00f },   // ズベレフ（2026 年にグランドスラム 2 勝。緊張は少しだけ残る）
};
bool gDecidingSet = false;

float RitualStart(int kind)
{
    switch (kind) {
        case RIT_NADAL:   return 1.95f;
        case RIT_HAIR:    return 0.80f;
        case RIT_STRINGS: return 2.10f;
        default:          return 0.20f;
    }
}
float RitualDuration(int kind, int bounces) { return RitualStart(kind) + bounces * RIT_BOUNCE_T + (bounces > 0 ? 0.25f : 0.10f); }

float CelebDuration(int kind)
{
    switch (kind) {
        case CELEB_KNEES: return 3.2f;
        case CELEB_HEART: return 2.6f;
        case CELEB_RANT:  return 1.9f;
        case CELEB_EAR:   return 1.8f;
        case CELEB_ARMS_UP: case CELEB_RACKET_UP: case CELEB_RACKET_POINT: case CELEB_HANDS_HEAD: return 1.9f;
        default: return CELEB_TIME;
    }
}

// 選手（強さ EASY/NORMAL/PRO に上乗せする癖）。0 は癖なし
const Persona PERSONAS[PERSONA_COUNT] = {
    //  name               short       左利き 片手  速さ   反応    ﾀｲﾐﾝｸﾞ 位置  強気   ｻｰﾌﾞ  安定   ｺｰｽ    回転  ﾊﾞｯｸｽﾗｲｽ ﾛﾌﾞ ﾄﾞﾛｯﾌﾟ ﾈｯﾄ  S&V   ﾊﾞｯｸ狙い ﾘﾀｰﾝ  ﾎﾞﾚｰ  ﾗｲｼﾞﾝｸﾞ ｽﾗｲｽ ﾌﾞﾚ   勝負強さ ｺｰﾄ(ﾊｰﾄﾞ,ｸﾚｰ,芝)
    { "AI",              "AI",       false, false, 0.00f,  0.00f, 1.00f, 1.00f, 0.00f,  0.0f, 0.00f,  0.00f, 1.00f, 0.00f, 0.35f, 0.00f, 1.0f, -1.0f, 0.00f, 0.0f, 0.00f, 0.00f, 0.00f, 1.00f, 0.00f, { 0.00f,  0.00f,  0.00f }, { 70, 90, 200, 255 } },
    { "Rogerr Federerr", "FEDERERR", false, true,  0.10f, -0.01f, 0.59f, 0.63f, 0.05f, 8.0f, 0.09f, -0.06f, 0.90f, 0.25f, 0.30f, 0.08f, 1.6f,  0.20f, 0.20f, 0.3f, 0.60f, 0.80f, 1.00f, 0.82f, 0.30f, { -0.06f, -0.26f, 0.10f }, { 200, 40, 50, 255 } },
    { "Novac Djokovik",  "DJOKOVIK", false, false, 0.35f, -0.05f, 0.62f, 0.62f, 0.00f, 4.0f, 0.10f, -0.03f, 1.05f, 0.08f, 0.30f, 0.04f, 0.7f,  0.03f, 0.25f, 0.9f, 0.30f, 0.40f, 0.20f, 0.70f, 1.00f, { -0.10f, -0.14f, -0.22f }, { 40, 60, 140, 255 } },
    { "Rafa Nadar",      "NADAR",    true,  false, 0.40f, -0.03f, 0.42f, 0.45f, 0.05f, -8.0f, 0.08f,  0.00f, 1.60f, 0.05f, 0.30f, 0.03f, 0.8f,  0.03f, 0.40f, 0.4f, 0.20f, 0.00f, 0.00f, 0.85f, 0.85f, { -0.22f, 0.40f, -0.15f }, { 240, 140, 40, 255 } },
    { "Andie Murraye",   "MURRAYE",  false, false, 0.30f, -0.04f, 0.45f, 0.48f, 0.00f, 2.0f, 0.07f, -0.02f, 1.00f, 0.25f, 0.40f, 0.06f, 0.6f,  0.05f, 0.20f, 0.7f, 0.35f, 0.20f, 0.50f, 0.85f, 0.40f, { -0.16f, -0.30f, -0.24f }, { 30, 150, 140, 255 } },
    // ---- 男子（追加）----
    { "Carlos Alcarazz", "ALCARAZZ", false, false, 0.45f, -0.05f, 0.56f, 0.56f, 0.15f, 6.0f, 0.07f, -0.03f, 1.25f, 0.08f, 0.30f, 0.12f, 1.2f,  0.05f, 0.25f, 0.6f, 0.45f, 0.30f, 0.30f, 0.90f, 0.70f, { -0.06f, -0.04f, -0.08f }, { 60, 170, 200, 255 } },
    { "Jannik Sinnerr",  "SINNERR",  false, false, 0.30f, -0.04f, 0.55f, 0.55f, 0.12f, 6.0f, 0.08f, -0.03f, 0.95f, 0.03f, 0.25f, 0.02f, 0.6f,  0.02f, 0.20f, 0.9f, 0.25f, 0.50f, 0.00f, 0.80f, 0.65f, { 0.00f, -0.16f, -0.14f }, { 30, 90, 70, 255 } },
    { "Daniil Medvedeb", "MEDVEDEB", false, false, 0.38f, -0.03f, 0.55f, 0.56f, 0.05f, 6.0f, 0.08f, -0.02f, 0.85f, 0.02f, 0.40f, 0.02f, 0.5f,  0.00f, 0.20f, 0.5f, 0.20f, -0.40f, 0.00f, 0.70f, 0.30f, { 0.14f, -0.16f, -0.06f }, { 90, 140, 200, 255 } },
    { "Nick Kirgios",    "KIRGIOS",  false, false, 0.10f, -0.02f, 0.75f, 0.80f, 0.30f, 18.0f, 0.12f, -0.08f, 1.00f, 0.15f, 0.35f, 0.10f, 1.0f,  0.10f, 0.20f, 0.2f, 0.45f, 0.40f, 0.30f, 1.05f, -0.20f, { 0.02f, -0.18f, 0.12f }, { 25, 25, 30, 255 } },
    // ---- 女子 ----
    { "Serenah Williamz", "WILLIAMZ", false, false, 0.15f, -0.02f, 0.60f, 0.62f, 0.25f, 20.0f, 0.10f, -0.06f, 0.95f, 0.05f, 0.25f, 0.03f, 0.8f, 0.05f, 0.25f, 0.8f, 0.35f, 0.30f, 0.00f, 0.85f, 0.90f, { 0.00f, -0.20f, 0.04f }, { 230, 60, 140, 255 }, true },
    { "Iga Swiontek",     "SWIONTEK", false, false, 0.35f, -0.04f, 0.55f, 0.56f, 0.12f, 4.0f, 0.08f, -0.03f, 1.45f, 0.03f, 0.25f, 0.03f, 0.6f, 0.02f, 0.25f, 0.8f, 0.25f, 0.40f, 0.00f, 0.80f, 0.50f, { -0.24f, 0.30f, -0.30f }, { 90, 110, 160, 255 }, true },
    { "Aryna Sabalenco",  "SABALENCO", false, false, 0.15f, -0.02f, 0.60f, 0.62f, 0.30f, 12.0f, 0.05f, -0.05f, 1.00f, 0.03f, 0.20f, 0.04f, 0.7f, 0.02f, 0.20f, 0.7f, 0.30f, 0.30f, 0.00f, 1.00f, 0.00f, { 0.02f, -0.15f, -0.10f }, { 240, 240, 240, 255 }, true },
    { "Coco Gauph",       "GAUPH",    false, false, 0.45f, -0.06f, 0.60f, 0.58f, 0.00f, 8.0f, 0.06f, -0.03f, 1.05f, 0.05f, 0.35f, 0.03f, 0.7f, 0.02f, 0.20f, 0.6f, 0.30f, 0.10f, 0.00f, 0.88f, 0.20f, { -0.06f, -0.10f, -0.12f }, { 250, 180, 40, 255 }, true },
    { "Maria Sharapowa",  "SHARAPOWA", false, false, -0.05f, -0.02f, 0.58f, 0.60f, 0.25f, 6.0f, 0.06f, -0.04f, 0.90f, 0.02f, 0.20f, 0.03f, 0.6f, 0.02f, 0.20f, 0.8f, 0.25f, 0.40f, 0.00f, 0.95f, 0.80f, { -0.08f, 0.10f, -0.15f }, { 255, 190, 210, 255 }, true },
    { "Ash Bartie",       "BARTIE",   false, false, 0.25f, -0.04f, 0.58f, 0.58f, 0.05f, 6.0f, 0.08f, -0.04f, 1.05f, 0.60f, 0.30f, 0.08f, 1.3f, 0.10f, 0.25f, 0.5f, 0.60f, 0.40f, 0.90f, 0.85f, 0.40f, { -0.06f, -0.15f, 0.10f }, { 40, 120, 200, 255 }, true },
    // ---- 男子（追加 2）----
    //   錦織：足が速く反応も速い。上がり際で早く打つ。リターンとバックハンドがうまい。サーブは遅め。ハードが得意
    { "Kei Nishikoli",   "NISHIKOLI", false, false, 0.30f, -0.04f, 0.60f, 0.58f, 0.10f, -6.0f, 0.03f, -0.02f, 1.10f, 0.05f, 0.30f, 0.06f, 0.8f,  0.02f, 0.20f, 0.9f, 0.30f, 0.70f, 0.00f, 0.92f, 0.20f, { -0.02f, -0.15f, -0.12f }, { 60, 60, 70, 255 } },
    //   ズベレフ：サーブがとても速い。両手バックが強い。ベースラインの後ろで粘る。クレーとハードが得意
    { "Sascha Zverew",   "ZVEREW",    false, false, 0.25f, -0.03f, 0.57f, 0.57f, 0.10f, 15.0f, 0.08f, -0.05f, 1.05f, 0.03f, 0.25f, 0.02f, 0.6f,  0.02f, 0.25f, 0.6f, 0.20f, 0.10f, 0.00f, 0.85f, 0.35f, { 0.04f, 0.10f, -0.06f }, { 120, 180, 230, 255 } },
};

// 強さに選手の癖を足した、実際の能力
AiLevel EffLevel(const Opponent *ai)
{
    AiLevel L = AI_LEVELS[ai->level];
    const Persona *p = &PERSONAS[ai->persona % PERSONA_COUNT];
    // 選手の上乗せは難易度で変える（EASY は 4 割、NORMAL と PRO は全部）
    static const float LEVEL_BOOST[3] = { 0.25f, 1.0f, 1.0f };
    float f = LEVEL_BOOST[ai->level % 3];
    #define BOOSTMUL(m) (1.0f - (1.0f - (m)) * f)
    // コートの得意・不得意：走る速さに加えて、タイミングの安定にも効く（ミスの少なさは AiSwing で）
    float form = p->surfSpeed[gSurface];
    L.speed        += (p->speedAdd + form) * f;
    if (gWomen) L.speed *= WOMEN_RUN_K;
    {   // 身長のトレードオフ：背が高いほど足と反応が少し遅い（低いと速い）
        float hd = HeightDelta(ai->persona);
        L.speed *= 1.0f - HEIGHT_SPEED_PER_CM * hd;
        L.reaction = fmaxf(0.10f, L.reaction + HEIGHT_REACT_PER_CM * hd);
    }
    L.timingSigmaMs *= Clamp(1.0f - 1.5f * form * f, 0.4f, 1.8f);
    L.posErr        *= Clamp(1.0f - 1.5f * form * f, 0.4f, 1.8f);
    L.reaction      = fmaxf(0.12f, L.reaction + p->reactionAdd * f);
    L.timingSigmaMs *= BOOSTMUL(p->timingMul);
    L.posErr       *= BOOSTMUL(p->posErrMul);
    L.aggression    = Clamp01(L.aggression + p->aggrAdd);
    L.serve1Kmh    += p->serveAdd;
    L.serve2Kmh    += p->serveAdd * 0.5f;
    L.serveSkill    = Clamp(L.serveSkill + p->serveSkillAdd * f, 0.3f, 0.95f);
    #undef BOOSTMUL
    L.serveMargin   = fmaxf(0.15f, L.serveMargin + p->serveMarginAdd);
    // 勝負強さ：大事なポイントほど、タイミングが安定し、反応が速く、サーブがぶれない
    const Mental *mm = &MENTAL[ai->persona % PERSONA_COUNT];
    float k = p->clutch * gPointImportance + (gDecidingSet ? mm->decider * (0.5f + 0.5f * gPointImportance) : 0.0f);
    k = Clamp(k, -1.0f, 1.2f);
    L.timingSigmaMs *= 1.0f - 0.50f * k;
    L.reaction      = fmaxf(0.10f, L.reaction - 0.05f * k);
    L.serveSkill    = Clamp(L.serveSkill + 0.06f * k, 0.3f, 0.97f);
    // 気持ちが崩れている：タイミングと立ち位置がぶれ、足が止まる
    float tl = Clamp01(ai->tilt);
    L.timingSigmaMs *= 1.0f + 0.45f * tl;
    L.posErr        *= 1.0f + 0.35f * tl;
    L.speed         -= 0.35f * tl;
    L.serveSkill     = Clamp(L.serveSkill - 0.05f * tl, 0.3f, 0.97f);
    // ラケットを叩きつけた後：吹っ切れて集中する
    float r = Clamp01(ai->rage);
    L.timingSigmaMs *= 1.0f - RAGE_TIMING * r;
    L.posErr        *= 1.0f - RAGE_POS * r;
    L.speed         += RAGE_SPEED * r;
    L.reaction      = fmaxf(0.10f, L.reaction - 0.03f * r);
    L.aggression    = Clamp01(L.aggression + 0.10f * r);
    L.serveSkill    = Clamp(L.serveSkill + 0.04f * r, 0.3f, 0.97f);
    return L;
}


// 速さ v0（目的地方向の成分）から距離 d を走って止まるまでの時間
// 加速 a で最高速 vmax まで上げ、最後は減速 b で止まる（台形の速度変化）
float TravelTime(float d, float v0, float vmax, float a, float b)
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
Vector3 Accelerate(Vector3 vel, Vector3 want, float a, float b, float dt)
{
    Vector3 dv = Vector3Subtract(want, vel);
    float len = Vector3Length(dv);
    float lim = (Vector3Length(want) < Vector3Length(vel) - 0.01f) ? b : a;
    float maxDv = lim * dt;
    if (len > maxDv) dv = Vector3Scale(dv, maxDv / len);
    return Vector3Add(vel, dv);
}

// 目的地に向かって走る（止まれる速さに落としながら）
Vector3 MoveToward(Vector3 *pos, Vector3 vel, Vector3 target, float vmax, float dt)
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
Ball FlipSides(Ball b)
{
    b = RotBall(b);
    if (b.lastHitter == HITTER_PLAYER) b.lastHitter = HITTER_OPP;
    else if (b.lastHitter == HITTER_OPP) b.lastHitter = HITTER_PLAYER;
    return b;
}

// AI のホームポジション（ワールド座標）
// ネットでの構え（ワールド座標）：自分が打ったコースの側に寄って、ネットから 2.6m
Vector3 AiNetPos(const Opponent *ai)
{
    return Rot180((Vector3){ Clamp(ai->lastTargetX * 0.45f, -2.2f, 2.2f), 0.0f, 2.6f });
}

Vector3 AiHome(void) { return (Vector3){ 0.0f, 0.0f, -(HALF_LEN + 0.8f) }; }
// 選手ごとの構える位置：ライジング好みほどベースラインに近い（最大 0.6m 前）
static Vector3 AiHomeFor(const Opponent *ai)
{
    Vector3 h = AiHome();
    h.z += 0.6f * PERSONAS[ai->persona % PERSONA_COUNT].risingPref;
    return h;
}

// リターンの構え位置（自分側の座標。AI は Rot180 して使う）。ボックスの外寄り、ベースラインの 1.3m 後ろ
// ファーストは下がって時間を作り（ベースラインの 1.8m 後ろ）、セカンドは前で待つ（0.3m 後ろ）
Vector3 ReturnPos(bool deuce, bool second)
{
    return (Vector3){ deuce ? 2.3f : -2.3f, 0.0f, HALF_LEN + (second ? 0.3f : 1.8f) };
}

// サーバーの立ち位置に合わせたレシーブの位置（レシーバーの座標）。serverX はサーバーの世界の x（サーバーは反対側のコート）。
//   打てるコースの両端（センターの T とワイドの角）を結ぶ角の二等分線の上に立つ。サーバーが横へ寄るほど、ワイドへ切れる角度が大きいので外へ動く。
//   真ん中寄りから打つときの位置（ReturnPos）を基準に、二等分線のずれの分だけ動かす。ワイドは弾んでから外へ逃げるので少し外寄り
static float BisectX(float sx, float side, float zr)
{
    Vector2 S = { sx, -(HALF_LEN + 0.15f) };
    Vector2 T = { 0.0f, SERVICE_Z }, Wd = { side * SINGLES_W * 0.5f, SERVICE_Z };
    Vector2 dT = Vector2Normalize(Vector2Subtract(T, S)), dW = Vector2Normalize(Vector2Subtract(Wd, S));
    Vector2 b = Vector2Normalize(Vector2Add(dT, dW));
    return S.x + b.x * (zr - S.y) / b.y;
}
Vector3 ReturnPosVs(bool deuce, bool second, float serverX)
{
    Vector3 p = ReturnPos(deuce, second);
    float side = deuce ? 1.0f : -1.0f;
    float sx = -serverX;                             // サーバーの位置をレシーバーの座標へ（180° 回す）
    float base = BisectX(-side * 0.6f, side, p.z);   // ふだんの位置（センター寄り 0.6m から打つとき）
    float now  = BisectX(sx, side, p.z);
    p.x += (now - base) * 1.1f;
    p.x = Clamp(p.x, -SINGLES_W * 0.5f - 2.0f, SINGLES_W * 0.5f + 2.0f);
    return p;
}

// 飛んでくる球を予測して、どこで打つかを決める（AI 座標で考える）
void AiPlan(Opponent *ai, const Ball *worldBall)
{
    AiLevel lvE = EffLevel(ai);
    const AiLevel *lv = &lvE;
    const Persona *pp = &PERSONAS[ai->persona % PERSONA_COUNT];
    (void)pp;
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
            ai->standPos = ai->netRush ? ai->pos : AiHomeFor(ai);   // ネットにいれば、そのまま見送る
            return;
        }
    }

    // 2) 軌道の中から、間に合って、打ちやすい点を選ぶ
    //    ベースライン：バウンド後の、高さ 1m 前後
    //    ネット（サービスラインより前）：バウンド前のボレー（早く触るほどいい）、高ければスマッシュ
    bool atNet = me.z < SERVICE_Z + 1.5f;
    Ball q = b;
    float t = 0.0f;
    float bestCost = 1e9f, bestT = -1.0f, bestH = 1.0f;
    Vector3 bestStand = me;
    float bestBallX = 0.0f, lateBallX = 0.0f;
    float lateCost = 1e9f, lateT = -1.0f;
    Vector3 lateStand = me;
    for (int i = 0; i < 240 * 4 && q.active && q.bounces < 2; i++) {
        StepBall(&q, PHYS_DT);
        t += PHYS_DT;
        if (i % 2 != 0 || q.pos.z <= 0.0f) continue;
        bool volley = q.bounces == 0;
        if (volley && !(atNet && q.pos.z > 0.3f && q.pos.y > 0.3f && q.pos.y < JUMP_MAX_H)) continue;
        if (!volley && q.bounces != 1) continue;
        Vector3 stand;
        float cost;
        if (volley && q.pos.y > OVERHEAD_MIN_H) {
            // スマッシュ：球の少し後ろ左に入る
            stand = (Vector3){ q.pos.x - OVERHEAD_SIDE, 0.0f, q.pos.z + OVERHEAD_FWD };
            cost = 0.6f * fabsf(q.pos.y - OVERHEAD_IDEAL_H) + 0.25f * t;
        } else if (volley) {
            // ボレー：体の横やや前（40°）で触る。早いほどいい（相手に時間を与えない）
            Vector3 fore = { q.pos.x - 0.72f, 0.0f, q.pos.z + 0.60f };
            Vector3 back = { q.pos.x + 0.72f, 0.0f, q.pos.z + 0.60f };
            stand = (Vector3Distance(fore, me) <= Vector3Distance(back, me)) ? fore : back;
            cost = 0.3f * t + 0.3f * fabsf(q.pos.y - 1.2f);
        } else {
            // フォアかバックか、近い方で構える
            Vector3 fore = { q.pos.x - IDEAL_SIDE, 0.0f, q.pos.z + IDEAL_FWD };
            Vector3 back = { q.pos.x + IDEAL_SIDE, 0.0f, q.pos.z + IDEAL_FWD };
            stand = (Vector3Distance(fore, me) <= Vector3Distance(back, me)) ? fore : back;
            cost = fabsf(q.pos.y - 1.0f) + 0.4f * fmaxf(q.pos.z - (HALF_LEN + 2.0f), 0.0f);
            // ライジング好み：下がらずに、上がり際で打ちにいく
            cost += pp->risingPref * (0.5f * fmaxf(q.pos.z - HALF_LEN, 0.0f) - (q.vel.y > 0.3f ? 0.25f : 0.0f));
            if (atNet) cost += 0.6f;   // ネットにいるなら、バウンドさせるよりボレーで取りたい
        }
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
                if (cost + extra < bestCost) { bestCost = cost + extra; bestT = t; bestStand = st; bestBallX = q.pos.x; bestH = q.pos.y; }
            } else if (need - t < lateCost) {
                lateCost = need - t; lateT = t; lateStand = st; lateBallX = q.pos.x;   // 間に合わない：一番惜しいところを目指す
            }
        }
    }
    if (bestT < 0.0f) { bestT = lateT; bestStand = lateStand; bestBallX = lateBallX; }
    ai->planFore = pp->lefty ? (bestBallX <= bestStand.x) : (bestBallX >= bestStand.x);   // 左利きは体の左がフォア
    if (bestT < 0.0f) { ai->planned = false; ai->standPos = AiHomeFor(ai); return; }

    bestStand.x += RandNormal() * lv->posErr;
    bestStand.z += RandNormal() * lv->posErr;
    ai->standPos = Rot180(bestStand);
    ai->swingAge = worldBall->age + bestT + RandNormal() * lv->timingSigmaMs / 1000.0f;
    ai->planned = true;
    ai->planH = bestH;
    ai->leaving = false;
}

static bool sAttack;   // 直前に選んだショットが「決めにいく」球か

// AI のショット選び（AI 座標）
ShotType AiChooseShot(const Opponent *ai, const Contact *c, Vector3 playerAi, Vector3 meAi, float *charge,
                             Vector3 *target)
{
    AiLevel lvE = EffLevel(ai);
    const AiLevel *lv = &lvE;
    const Persona *pp = &PERSONAS[ai->persona % PERSONA_COUNT];
    (void)pp;
    ShotType t = SHOT_TOP;
    float r = RandRange(0.0f, 1.0f);
    bool stretched = c->side > IDEAL_R + 0.45f || c->dive;
    bool playerAtNet = playerAi.z > -7.0f;        // プレイヤーが前に出てきている（AI 座標で相手は -z 側）
    bool playerDeep  = playerAi.z < -(HALF_LEN + 2.0f);

    // 低い・遠い → つなぐ。ただ膝くらい（40〜55cm）なら、強気な選手は膝を曲げてトップスピンで持ち上げる
    //   ライジングの名手は、上がり際の低い球もそのまま振り抜く
    float rise = (c->vyBall > 0.0f) ? pp->risingPref : 0.0f;
    bool low = c->height < 0.40f - 0.12f * rise ||
               (c->height < 0.55f && RandRange(0.0f, 1.0f) > 0.7f * lv->aggression + 0.6f * rise);
    if (low || (stretched && r < 0.7f)) t = SHOT_SLICE;
    else if (c->height > 1.05f && c->quality > 0.55f && r < lv->aggression) t = SHOT_FLAT;   // 高くていい打点 → 叩く
    // バックハンドのスライス（片手バックの選手ほど多い）
    if (!c->forehand && !c->volley && !c->overhead && t == SHOT_TOP && RandRange(0.0f, 1.0f) < pp->backSlice) t = SHOT_SLICE;
    if (playerAtNet && (stretched || r < pp->lobPref)) t = SHOT_TOP_LOB;      // 前に来たらロブ
    if (c->overhead) t = SHOT_FLAT;                                             // 頭上はスマッシュ

    // ネットでのボレー・スマッシュ：相手のいない方へ。相手が下がっていれば短いアングルかドロップボレー
    if (c->volley) {
        bool oppBack = playerAi.z < -(HALF_LEN - 1.0f);   // 相手がベースライン付近まで下がっている
        float side = (playerAi.x > 0.3f) ? -1.0f : (playerAi.x < -0.3f ? 1.0f : ((rand() % 2) ? 1.0f : -1.0f));
        if (c->overhead) {
            t = SHOT_FLAT;
            *charge = 0.7f + 0.3f * lv->aggression;
            *target = (Vector3){ side * (2.6f + 0.8f * lv->aggression), 0.0f, -8.5f };
        } else if (oppBack && r < 0.25f * lv->aggression) {
            t = SHOT_DROP;
            *charge = 0.6f;
            *target = (Vector3){ side * 2.5f, 0.0f, -2.5f };
        } else {
            t = (c->height > 1.2f && c->quality > 0.5f) ? SHOT_FLAT : SHOT_SLICE;   // 高ければ叩く、低ければスライスで運ぶ
            *charge = RandRange(0.35f, 0.6f);
            bool angle = oppBack && r < 0.5f;
            *target = (Vector3){ side * (angle ? 3.4f : 2.4f + 1.0f * lv->aggression), 0.0f, angle ? -5.5f : -9.5f };
        }
        return t;
    }
    if (playerDeep && meAi.z < HALF_LEN - 1.0f && r < 0.15f * lv->aggression + pp->dropAdd) t = SHOT_DROP;

    // 球速の目安（打ち出し）。プロのラリーはフラット 130〜150、トップ 110〜135、スライス 80〜100km/h くらい
    float wantKmh;
    switch (t) {
        // 当たりの質で実際の速さは 1〜2 割落ちるので、そのぶん高めに狙う
        case SHOT_FLAT:  wantKmh = 145.0f + 40.0f * lv->aggression; break;
        case SHOT_TOP:   wantKmh = 132.0f + 30.0f * lv->aggression; break;
        case SHOT_SLICE: wantKmh =  95.0f + 20.0f * lv->aggression; break;
        default:         wantKmh = Lerp(SHOTS[t].minKmh, SHOTS[t].maxKmh, 0.5f); break;
    }
    wantKmh += RandNormal() * 8.0f - (stretched ? 20.0f : 0.0f);
    if (c->isReturn) wantKmh += 6.0f * pp->returnAggro;    // リターンのうまい選手は少し強く返す
    const ShotSpec *ss = &SHOTS[t];
    *charge = (ss->maxKmh > ss->minKmh) ? Clamp01((wantKmh - ss->minKmh) / (ss->maxKmh - ss->minKmh)) : 0.5f;

    // 甘い球（いい打点で、ベースラインより内側）は決めにいく：速く、厳しく
    bool attack = c->quality > 0.5f && meAi.z < HALF_LEN - 0.3f && (t == SHOT_TOP || t == SHOT_FLAT) &&
                  RandRange(0, 1) < 0.22f + 0.55f * lv->aggression;
    sAttack = attack;
    if (attack) {
        // 甘い球はフラットドライブで叩く（今のプロの打ち方）
        if (t == SHOT_TOP && c->height > 0.7f) t = SHOT_FLAT;
        const ShotSpec *as = &SHOTS[t];
        float want = (t == SHOT_FLAT ? 160.0f : 145.0f) + 25.0f * lv->aggression;
        *charge = Clamp01((want - as->minKmh) / (as->maxKmh - as->minKmh));
    }

    // コース：プレイヤーのいない方へ。強気なほどサイドラインに近い
    const ShotSpec *sp = &SHOTS[t];
    float wide = attack ? 3.2f : 2.2f + 1.0f * lv->aggression;   // サイドラインまで 4.1m。普段は 1.1〜1.9m の余裕、攻めるときは 0.9m
    float x;
    if (playerAi.x > 0.6f)       x = -wide;
    else if (playerAi.x < -0.6f) x =  wide;
    else                         x = (rand() % 2) ? wide : -wide;
    if (r > 0.85f) x = -x * 0.5f;   // たまに逆を突く
    // 相手（右利き）のバック側を狙う：AI 座標では相手から見て左＝ +x
    if (!attack && RandRange(0.0f, 1.0f) < pp->toBackhand) x = wide;
    float depth = sp->depth;
    if (t <= SHOT_SLICE && RandRange(0, 1) < 0.15f * lv->aggression) depth = sp->shortDepth;   // 短いアングル
    *target = (Vector3){ x, 0.0f, depth };
    return t;
}

// AI が振る。当たったら true（ball は AI の打球に置き換わる）
bool AiSwing(Opponent *ai, Ball *ball, Vector3 player)
{
    AiLevel lvE = EffLevel(ai);
    const AiLevel *lv = &lvE;
    const Persona *pp = &PERSONAS[ai->persona % PERSONA_COUNT];
    (void)pp;
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
    gLefty = pp->lefty; gSpinMul = pp->spinMul; gSliceEdge = pp->sliceEdge;
    static const float LEVEL_ERR[3] = { 2.4f, 1.0f, 0.72f };   // 難易度でミスの量が変わる（EASY はかなりぶれる）
    gRiseSkill = pp->risingPref;   // ライジングが得意な選手は、跳ね上がりの分を面で抑えられる
    gErrMul = LEVEL_ERR[ai->level % 3] * pp->errMul * (1.0f - 0.40f * pp->clutch * gPointImportance)   // ミスの少なさ（大事なポイントほど減る）
            * Clamp(1.0f - 1.2f * pp->surfSpeed[gSurface], 0.5f, 1.6f)          // 得意なコートほどミスが減る
            * (1.0f - RAGE_ERR * Clamp01(ai->rage));                            // ラケットを叩きつけた後はミスが減る
    gTimingTolMul = 1.0f;   // AI のタイミングの許容幅はそのまま（人間だけ広い）
    gReachAdd = HeightDelta(ai->persona) * REACH_PER_CM;   // 身長で届く範囲が変わる
    gHeightDelta = HeightDelta(ai->persona);              // 背が高いほど低い球がつらい
    Contact c = EvaluateContact(me, &b, SHOT_TOP, 0.5f, moveToward, bonus);
    sAttack = false;
    bool sLapse = false;
    ShotType t = AiChooseShot(ai, &c, Rot180(player), me, &charge, &target);
    // 凡ミス：楽な体勢でも、たまに打ち損じる（力み・集中切れ）。決めにいく球ほど起きやすい。
    //   これがないと AI はハイライトのようなプレーしかしない。現実のプロでも 100 ポイントに 10〜15 本は凡ミスする
    if (!c.isReturn && !c.volley && !c.dive && !c.stretch) {
        static const float LEVEL_LAPSE[3] = { AI_LAPSE_EASY, AI_LAPSE_NORMAL, AI_LAPSE_PRO };
        float lapse = LEVEL_LAPSE[ai->level % 3] * (sAttack ? AI_LAPSE_ATTACK : 1.0f)
                    * pp->errMul * (1.0f - 0.40f * pp->clutch * gPointImportance)
                    * Clamp(1.0f - 1.2f * pp->surfSpeed[gSurface], 0.5f, 1.6f)
                    * (1.0f - RAGE_LAPSE * Clamp01(ai->rage))
                    * (1.0f + 1.2f * Clamp01(ai->tilt));   // 気持ちが崩れていると凡ミスが増える
        if (sAttack) lapse = fmaxf(lapse, AI_LAPSE_ATTACK_MIN);   // 決めにいく球は、ミスの少ない選手でも外すことがある
        if (RandRange(0.0f, 1.0f) < lapse) { sLapse = true; gErrMul = fmaxf(gErrMul, 1.0f) * AI_LAPSE_ERR; }   // 打ち損じは、うまい選手でも普通の選手と同じくらいぶれる
        else if (sAttack) gErrMul *= AI_ATTACK_ERR;   // 決めにいく球は、少しだけ力む
    }
    ai->anim.slice = IsSliceShot(t);
    c = EvaluateContact(me, &b, t, charge, moveToward, bonus);
    if (sLapse) c.quality *= AI_LAPSE_Q;   // 打ち損じ：芯を外す
    c.stepIn = -meVel.z;
    if (c.miss == MISS_NONE && c.volley && !c.overhead)   // ボレーのうまい選手は、当たりの悪さを取り戻す
        c.quality += (1.0f - c.quality) * 0.5f * pp->volleySkill;
    ai->anim.lastContact = c;
    if (c.miss == MISS_NONE && c.lunge && !c.dive) { ai->anim.lungeT = LUNGE_TIME; ai->anim.lungeSide = c.forehand ? 1.0f : -1.0f; }
    ai->anim.contactLocal = (Vector3){ b.pos.x - me.x, b.pos.y, me.z - b.pos.z };
    ai->anim.swingT = 0.0f;
    ai->anim.smash = c.overhead;
    RestoreHumanContact();   // 当たりの評価の値は自分の値に戻す（回転やミスは打ち終わってから）
    if (c.miss != MISS_NONE) { ApplyHumanTraits(); return false; }
    {   // よろめき
        float sg = StaggerAmount(&c, fabsf(ai->vel.x));
        ai->staggerTotal = ai->staggerT = sg > 0.0f ? STAGGER_T0 + STAGGER_T1 * sg : 0.0f;
        ai->anim.staggerSide = c.forehand ? 1.0f : -1.0f;
        ai->staggerS = sg;
    }
    if (c.dive) { ai->downTimer = DIVE_DOWN_TIME; ai->anim.downT = DIVE_DOWN_TIME; }
    else if (c.stretch) ai->downTimer = STRETCH_RECOVER;   // 体勢を立て直すまで動けない

    float kmh;
    Ball out = PlayerShot(&c, b.pos, target, t, charge, &kmh, NULL, NULL);
    ApplyHumanTraits();   // 自分の値に戻す
    ai->lastTargetX = target.x;
    // 前に出るか：甘い球をベースラインの内側で攻めたら、強気なほど前に出る。ネットにいれば残る。
    // ロブで下がらされたら（サービスラインより 3m 以上後ろ）、ネットはあきらめる
    if (me.z > SERVICE_Z + 3.0f) ai->netRush = false;
    if (!ai->netRush && !c.dive && me.z < HALF_LEN - 2.0f && t <= SHOT_SLICE &&
        RandRange(0.0f, 1.0f) < (0.25f + 0.6f * lv->aggression) * pp->netRushMul)
        ai->netRush = true;
    if (me.z < SERVICE_Z + 1.5f) ai->netRush = true;
    out.lastHitter = HITTER_OPP;
    *ball = RotBall(out);
    ai->lastType = t;
    ai->lastQ = c.quality;
    ai->lastKmh = kmh;
    ai->lastServe = false;
    ai->lastAttack = sAttack; ai->lastLapse = sLapse;
    return true;
}

// アンダーサーブ：下から短く、逆回転と横回転をかけてネットのすぐ先に落とす（相手がベースラインの後ろにいるとき）
Ball AiServeUnderarm(Opponent *ai, bool deuce)
{
    const Persona *pp = &PERSONAS[ai->persona % PERSONA_COUNT];
    Vector3 me = { deuce ? 0.6f : -0.6f, 0.0f, HALF_LEN + 0.15f };
    ai->pos = Rot180(me);
    Vector3 contact = { me.x + (pp->lefty ? -0.45f : 0.45f), 0.45f, me.z - 0.30f };
    float x0, x1;
    ServiceBox(me.x, -1, &x0, &x1);
    Vector3 target = { 0.5f * (x0 + x1) + RandRange(-0.6f, 0.6f), 0.0f, -RandRange(1.6f, 2.6f) };
    float kmh = RandRange(55.0f, 70.0f);
    Vector3 spin = { -900.0f, pp->lefty ? -1200.0f : 1200.0f, 0.0f };   // 逆回転と横回転：弾んでから止まって横へ逃げる
    Vector3 dir; float pitch;
    SolveServe(contact, target, kmh / 3.6f, spin, &dir, &pitch);
    dir = Vector3RotateByAxisAngle(dir, (Vector3){ 0, 1, 0 }, RandNormal() * 1.0f * DEG2RAD);
    Ball b = MakeServeBall(contact, dir, kmh / 3.6f, pitch + RandNormal() * 0.4f * DEG2RAD, spin);
    b.boxX0 = x0; b.boxX1 = x1;
    b.lastHitter = HITTER_OPP;
    // 見た目：下からすくい上げる（低いフォア）
    ai->anim.lastContact = (Contact){ .overhead = false, .forehand = true, .quality = 0.9f, .height = 0.45f };
    ai->anim.smash = false; ai->anim.slice = true;
    ai->anim.contactLocal = (Vector3){ contact.x - me.x, contact.y, me.z - contact.z };
    ai->anim.swingT = 0.0f;
    ai->lastKmh = kmh;
    ai->lastServe = true;
    ai->planned = false;
    ai->lastTargetX = target.x;
    ai->netRush = false;
    return RotBall(b);
}

// AI のサーブ。deuce: デュースサイドから打つ。first: ファーストサーブ
Ball AiServe(Opponent *ai, bool deuce, bool first)
{
    AiLevel lvE = EffLevel(ai);
    const AiLevel *lv = &lvE;
    const Persona *pp = &PERSONAS[ai->persona % PERSONA_COUNT];
    (void)pp;
    // AI 座標では自分と同じ：デュースサイドは x>0、対角のボックスは x<0
    Vector3 me = { deuce ? 0.6f : -0.6f, 0.0f, HALF_LEN + 0.15f };
    ai->pos = Rot180(me);
    // トスのぶれとタイミングのずれから、打点の高さと質を決める
    Vector2 off = { RandNormal() * TOSS_SPREAD, RandNormal() * TOSS_SPREAD };
    float hErr = fabsf(RandNormal() * lv->timingSigmaMs / 1000.0f * 2.4f);   // 落ちてくる速さ 2.4m/s 前後
    float q = Clamp01(1.0f - hErr / SERVE_TOL_H) * Clamp01(1.0f - Vector2Length(off) / TOSS_TOL);
    // 強さごとの安定感（低いほどぶれて、フォルトが増える）。セカンドは安全に入れにいくので、ぶれは小さい
    q *= first ? lv->serveSkill : lv->serveSkill + (1.0f - lv->serveSkill) * 0.65f;
    if (!first) q *= 1.0f - 0.45f * MENTAL[ai->persona % PERSONA_COUNT].dfNerves * gPointImportance;   // 大事なポイントのセカンドで腕が縮む
    Vector3 contact = { me.x + (pp->lefty ? -0.25f : 0.25f) + off.x, SERVE_IDEAL_H * (1.0f + HeightDelta(ai->persona) / 185.0f) - hErr, me.z - 0.35f + off.y };   // 背の高い選手ほど高い打点から打つ   // 左利きは左でトス

    ServeType t;
    float charge;
    // 速さは強さごとの目安から逆算する（打点の質で落ちる分も見込む）
    float wantKmh;
    if (first) { t = (rand() % 2) ? SERVE_FLAT : SERVE_SLICE; wantKmh = lv->serve1Kmh + RandNormal() * 6.0f; }
    else       { t = SERVE_KICK;                              wantKmh = lv->serve2Kmh + RandNormal() * 5.0f; }
    const ServeSpec *ssp = &SERVES[t];
    charge = (wantKmh / (0.80f + 0.20f * q) - ssp->minKmh) / (ssp->maxKmh - ssp->minKmh);
    float x0, x1;
    ServiceBox(me.x, -1, &x0, &x1);
    // コース：強い AI ほどライン際の深いところへ（返されないのはスピードよりコース）。セカンドは安全に内側・浅め
    float m = lv->serveMargin + (first ? 0.0f : 0.30f);
    float depth = lv->serveDepth - (first ? 0.0f : 0.45f);
    const float course[3] = { x1 - m, x0 + m, 0.5f * (x0 + x1) };   // T / ワイド / ボディ（デュース側）
    int ci = (RandRange(0.0f, 1.0f) < 0.5f + 0.4f * lv->aggression) ? rand() % 2 : 2;   // 強い AI ほどコーナーへ（PRO で 8 割）
    float tx = course[ci];
    if (!deuce) tx = (ci == 0) ? x0 + m : (ci == 1 ? x1 - m : 0.5f * (x0 + x1));
    Vector3 target = { tx, 0.0f, -depth };
    float kmh;
    gLefty = pp->lefty;   // 左利きのサーブは横回転が逆
    gServeAdd = 0.0f; gServeErrMul = 1.0f;   // AI のサーブの速さとぶれは、もう目標に入っている
    Ball b = HitServe(contact, target, t, Clamp01(charge), q, x0, x1, &kmh);
    ApplyHumanTraits();
    b.lastHitter = HITTER_OPP;
    // 見た目：頭上で打つ
    ai->anim.lastContact = (Contact){ .overhead = true, .forehand = true, .quality = q };
    ai->anim.smash = false;
    ai->anim.contactLocal = (Vector3){ contact.x - me.x, contact.y, me.z - contact.z };
    ai->anim.swingT = 0.0f;
    ai->lastKmh = kmh;
    ai->lastServe = true;
    ai->planned = false;
    ai->lastTargetX = target.x;
    // サーブ＆ボレー：強気な AI がたまに。芝では多め
    float sv = pp->serveVolley >= 0.0f ? pp->serveVolley * (gSurface == SURF_GRASS ? 1.5f : 1.0f)
                                       : 0.15f * lv->aggression + (gSurface == SURF_GRASS ? 0.25f * lv->aggression : 0.0f);
    ai->netRush = first && RandRange(0.0f, 1.0f) < sv;
    return RotBall(b);
}

// 毎フレーム：反応して、計画を立て、走る
void AiUpdate(Opponent *ai, const Ball *ball, float dt)
{
    AiLevel lvE = EffLevel(ai);
    const AiLevel *lv = &lvE;
    const Persona *pp = &PERSONAS[ai->persona % PERSONA_COUNT];
    (void)pp;
    if (ball->lastHitter != ai->watching) {
        ai->watching = ball->lastHitter;
        ai->planned = false;
        ai->leaving = false;
        ai->swung = false;
        // サーブはトスとフォームで読める（スプリットステップ）ので、反応は半分
        if (ball->lastHitter == HITTER_PLAYER) ai->seeTimer = ball->isServe ? lv->reaction * 0.5f : lv->reaction;
        if (ball->lastHitter == HITTER_PLAYER && ball->touch) {   // ロブ・ドロップ：打つ形からは読めないので、気づくのが遅れる
            static const float READ[3] = { 1.3f, 1.0f, 0.95f };
            ai->seeTimer += TOUCH_READ_DELAY * READ[ai->level % 3];
        }
        if (ball->lastHitter == HITTER_PLAYER && ai->staggerT > 0.0f) ai->seeTimer += ai->staggerT * STAGGER_REACT;   // よろめいていると一歩目が遅れる
        else ai->standPos = ai->netRush ? AiNetPos(ai) : AiHomeFor(ai);   // 自分が打った後：ネットに詰めるか、戻る
    }
    // 見た目のタイマー
    if (ai->anim.swingT >= 0.0f) ai->anim.swingT = fminf(ai->anim.swingT + dt, 5.0f);
    if (ai->anim.lungeT > 0.0f) ai->anim.lungeT -= dt;
    if (ai->anim.downT > 0.0f) ai->anim.downT -= dt;
    ai->anim.runPhase += Vector3Length(ai->vel) * dt * 2.2f;
    ai->anim.lefty = pp->lefty; ai->anim.oneHanded = pp->oneHanded;
    ApplyLook(&ai->anim, ai->persona);
    ai->anim.atNet = ai->pos.z > -VOICE_NET_Z;   // AI はネットが +z 側
    ai->anim.velX = -ai->vel.x; ai->anim.velZ = ai->vel.z;   // AI は +z 向き
    // 飛びついて倒れている間は動けない
    if (ai->downTimer > 0.0f) {
        ai->downTimer -= dt;
        ai->vel = (Vector3){ 0 };
        ai->planned = false;
        return;
    }
    float smul = StaggerSpeedMul(ai->staggerT, ai->staggerTotal);   // よろめいている間は遅い
    if (ai->staggerT > 0.0f) ai->staggerT -= dt;
    ai->anim.staggerK = ai->staggerTotal > 0.0f ? fmaxf(ai->staggerT, 0.0f) / ai->staggerTotal * ai->staggerS : 0.0f;
    lvE.speed *= smul;
    {   // 後ろへ下がる（ロブを追う）ときは、体の向きを変えて走るので遅い
        Vector3 d0 = Vector3Subtract(ai->standPos, ai->pos);
        float l0 = sqrtf(d0.x * d0.x + d0.z * d0.z);
        if (l0 > 0.3f && d0.z < 0.0f) lvE.speed *= 1.0f - BACKPEDAL_SLOW * (-d0.z / l0);   // AI のコートは z<0、後ろは -z
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
        bool oh = ai->planH > OVERHEAD_MIN_H;   // スマッシュ：頭上で構える
        ai->anim.windStroke = oh ? 2 : (ai->planFore ? 0 : 1);
        ai->anim.smash = oh;
        ai->anim.slice = false;
    } else {
        ai->anim.windup = 0.0f;
    }
}
