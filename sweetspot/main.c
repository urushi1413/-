// SWEET SPOT — 打点と球種がちゃんと結果に効く、球足の速いリアル系テニス
// ゲームの流れ（メニュー・入力・試合の進行・画面）
#include "game.h"
#include <ctype.h>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>   // ブラウザ版：窓の大きさ、マウスの固定
#endif

static const char *MODE_NAMES[MODE_COUNT] = { "MATCH", "RALLY", "RETURN", "SERVE" };   // 記録用（英語のまま）
static const char *ModeName(int m)   // 表示用
{
    static const char *JA[MODE_COUNT] = { "試合", "ラリー練習", "リターン練習", "サーブ練習" };
    return gLang == LANG_JA ? JA[m % MODE_COUNT] : MODE_NAMES[m % MODE_COUNT];
}

// 試合の統計（中継のマッチサマリーの形）。mst はサーブの入り [0=1st 打数 1=1st 入 2=2nd 打数 3=2nd 入 4=返されなかった 5=DF]
// 試合の統計の表の大きさ（画面の真ん中に出す）
#define SUMMARY_W     700
#define SUMMARY_ROW_H 32
#define SUMMARY_ROWS  13
#define SUMMARY_H     (132 + SUMMARY_ROWS * SUMMARY_ROW_H + 44)   // 置く場所を決める高さ
#define SUMMARY_DRAWN (118 + SUMMARY_ROWS * SUMMARY_ROW_H + 40)   // 実際に描く高さ（見出し 70 ＋ 名前 48 ＋ 行 ＋ 下の案内 40）
static int SummaryTop(void)    { return GetScreenHeight() / 2 - SUMMARY_H / 2; }
static int SummaryBottom(void) { return SummaryTop() + SUMMARY_DRAWN; }
// rightLimit：表の右端をこれより左に収める（右上のスコア表を出したまま表を出すとき。かからないなら真ん中のまま）
static void DrawMatchSummary(const char *title, const char *footer, const Score *sc, const MatchStats ms[2], const int mst[2][6], const char *opp, int rightLimit)
{
    int W = GetScreenWidth();
    int pw = SUMMARY_W, rowH = SUMMARY_ROW_H, nRows = SUMMARY_ROWS;
    int px = W / 2 - pw / 2, py = SummaryTop();
    if (px + pw > rightLimit - 10) px = rightLimit - 10 - pw;
    if (px < 4) px = 4;
    Color head = { 70, 40, 110, 240 }, rowA = { 20, 80, 50, 235 }, rowB = { 25, 95, 60, 235 };
    Color you = RAYWHITE, aiC = (Color){ 255, 215, 90, 255 };
    DrawRectangle(px, py, pw, SUMMARY_DRAWN, (Color){ 10, 26, 20, 255 });   // 下地：後ろの絵や文字が、行のすき間や半透明の行から透けて文字に重ならないように
    DrawRectangle(px, py, pw, 70, head);
    // セットのスコア（右）と見出し（左）：見出しはセットのスコアにかからない大きさに
    char sets[64] = { 0 };
    for (int i = 0; i < sc->nSetsDone; i++)
        snprintf(sets + strlen(sets), sizeof(sets) - strlen(sets), "%s%d-%d", i ? "  " : "", sc->setScore[i][0], sc->setScore[i][1]);
    int setsW = MeasureText(sets, 26);
    DrawText(sets, px + pw - 24 - setsW, py + 24, 26, RAYWHITE);
    { int tfs = 36; while (tfs > 18 && MeasureText(title, tfs) > pw - 48 - setsW - 24) tfs--;
      DrawText(title, px + 24, py + 18 + (36 - tfs) / 2, tfs, RAYWHITE); }
    int y = py + 74;
    DrawRectangle(px, y, pw, 40, rowB);
    {   // 名前：長い名前でも表の外にはみ出さない・真ん中にかからない大きさに（左右それぞれ幅 240 まで）
        const char *nm[2] = { YouName(), opp };
        for (int k = 0; k < 2; k++) {
            int nfs = 24; while (nfs > 14 && MeasureText(nm[k], nfs) > 240) nfs--;
            int nw = MeasureText(nm[k], nfs), cx = k == 0 ? px + 70 : px + pw - 70;
            int nx = Clamp(cx - nw / 2, px + 10, px + pw - 10 - nw);
            DrawText(nm[k], nx, y + 8 + (24 - nfs) / 2, nfs, k == 0 ? you : aiC);
        }
    }
    y += 44;
    for (int r = 0; r < nRows; r++) {
        const char *label = "";
        char v[2][24];
        for (int k = 0; k < 2; k++) {
            const MatchStats *m = &ms[k];
            int o = 1 - k;
            int retFaced = mst[o][1] + mst[o][3], retIn = retFaced - mst[o][4];
            #define PCT(a, b) snprintf(v[k], sizeof(v[k]), "%d%%", (b) > 0 ? (a) * 100 / (b) : 0)
            #define NUM(a)    snprintf(v[k], sizeof(v[k]), "%d", (a))
            #define FRAC(a, b) snprintf(v[k], sizeof(v[k]), "%d / %d", (a), (b))
            switch (r) {
                case 0:  label = TR("Aces", "サービスエース");                    NUM(m->aces); break;
                case 1:  label = TR("Double Faults", "ダブルフォールト");           NUM(mst[k][5]); break;
                case 2:  label = TR("1st Serves IN", "ファーストサーブの確率");           PCT(mst[k][1], mst[k][0]); break;
                case 3:  label = TR("1st Serve Points Won", "ファーストサーブでの得点率");    PCT(m->s1Won, m->s1Pts); break;
                case 4:  label = TR("2nd Serve Points Won", "セカンドサーブでの得点率");    PCT(m->s2Won, m->s2Pts); break;
                case 5:  label = TR("Total Serve Returns IN", "リターンが入った率");  PCT(retIn, retFaced); break;
                case 6:  label = TR("Break Points Won", "ブレークポイント");        FRAC(m->bpWon, m->bpTotal); break;
                case 7:  label = TR("Net Approach Points Won", "ネットに出たポイント"); FRAC(m->netWon, m->netPts); break;
                case 8:  label = TR("Total Unforced Errors", "アンフォーストエラー");   NUM(m->unforced); break;
                case 9:  label = TR("Total Winners", "ウィナー");           NUM(m->winners); break;
                case 10: label = TR("Total Points Won", "取ったポイント");        NUM(m->points); break;
                case 11: label = TR("Average Shot Speed", "平均の球速");      snprintf(v[k], sizeof(v[k]), "%.0f km/h", m->shots ? m->speedSum / m->shots : 0.0f); break;
                default: label = TR("Average Rally (shots)", "平均のラリー（打数）");   snprintf(v[k], sizeof(v[k]), "%.1f", ms[0].rallyPoints ? (float)ms[0].rallyTotal / ms[0].rallyPoints : 0.0f); break;
            }
            #undef PCT
            #undef NUM
            #undef FRAC
        }
        DrawRectangle(px, y, pw, rowH - 2, (r % 2) ? rowB : rowA);
        DrawText(v[0], px + 70 - MeasureText(v[0], 22) / 2, y + 6, 22, you);
        DrawText(label, px + pw / 2 - MeasureText(label, 22) / 2, y + 6, 22, RAYWHITE);
        DrawText(v[1], px + pw - 70 - MeasureText(v[1], 22) / 2, y + 6, 22, aiC);
        y += rowH;
    }
    DrawRectangle(px, y, pw, 40, Fade(BLACK, 0.75f));
    { int ff = 20; while (ff > 12 && MeasureText(footer, ff) > pw - 20) ff--;   // 長い案内は小さくして表の幅に収める
      DrawText(footer, px + pw / 2 - MeasureText(footer, ff) / 2, y + 10, ff, LIGHTGRAY); }
}

// 体勢が崩れて取った球か（ラケットでぎりぎり取って地面に当たる音を鳴らす）：飛びついた、または手が届くぎりぎり。
//   スライス・ボレー・ネット前（distFromNet が VOICE_NET_Z より近い）は除く
// 打った直後の球が、もう決まったか（相手がどうやっても届かない）：球の飛び方を先まで計算する。
//   相手は今の位置から、反応 0.25 秒のあと毎秒 5.7m で走り、2.0m まで手が届くとする。入らない球は決まりではない
//   （AI どうしの試し：NORMAL は決まりと見た球の 85% が本当に決まった。決まった球のうち 13〜17% を拾う）
static bool ClearWinner(const Ball *ball, Vector3 opp)
{
    Ball b = *ball;
    float t = 0.0f;
    for (int i = 0; i < 240 * 6 && b.active && b.bounces < 2; i++) {
        StepBall(&b, PHYS_DT);
        t += PHYS_DT;
        if (b.bounces >= 1 && b.result != RESULT_IN) return false;
        if (b.pos.y > 2.6f) continue;   // 高すぎる球は打てない（スマッシュは考えない）
        float d = sqrtf((b.pos.x - opp.x) * (b.pos.x - opp.x) + (b.pos.z - opp.z) * (b.pos.z - opp.z));
        if (d - 2.0f <= 5.7f * fmaxf(t - 0.25f, 0.0f)) return false;   // 間に合う瞬間がある
    }
    return b.bounces >= 1;
}

static bool DefensiveVoice(const Contact *c, ShotType t, float distFromNet, float hold)
{
    (void)hold;
    if (IsSliceShot(t) || (c->volley && !c->overhead) || distFromNet < VOICE_NET_Z) return false;
    return c->dive || c->side > OFFBALANCE_R;
}

// 設定の保存（sweetspot_settings.txt）：メニューで変えた設定を次に起動したときも使う
typedef struct { int sets, women, you, opp, level, style, court, assist, feed, lanes, cam, hud, guides; float sens, vol; int lang, tod, wx, look, slide, bounce; } Settings;
static const char *SETTINGS_FILE = "sweetspot_settings.txt";
static void SaveSettingsFile(const Settings *s)
{
    FILE *f = fopen(SETTINGS_FILE, "w");
    if (!f) return;
    fprintf(f, "look=%d\nslide=%d\nbounce_ritual=%d\n", s->look, s->slide, s->bounce);
    fprintf(f, "sets=%d\nwomen=%d\nplay_as=%d\nopponent=%d\nai_level=%d\nstyle=%d\ncourt=%d\nassist=%d\npro_feed=%d\naim_lanes=%d\ncamera=%d\nscreen_info=%d\nguides=%d\nsensitivity=%.2f\nvolume=%.2f\nlanguage=%d\ntime_of_day=%d\nweather=%d\nname=%s\n",
            s->sets, s->women, s->you, s->opp, s->level, s->style, s->court, s->assist, s->feed, s->lanes, s->cam, s->hud, s->guides, s->sens, s->vol, s->lang, s->tod, s->wx, gPlayerName);
    for (int a = 0; a < ACT_COUNT; a++) fprintf(f, "key_%s=%d\n", ActionId(a), gKeys[a]);   // キーの割り当て（raylib のキーの番号）
    fclose(f);
}
static bool LoadSettingsFile(Settings *s)
{
    FILE *f = fopen(SETTINGS_FILE, "r");
    if (!f) return false;
    char line[128];
    while (fgets(line, sizeof(line), f)) {
        char key[64]; float v;
        if (strncmp(line, "name=", 5) == 0) {   // 名前（文字）
            char nm[64] = { 0 }; sscanf(line + 5, "%63[^\r\n]", nm);
            if (nm[0]) snprintf(gPlayerName, sizeof(gPlayerName), "%.12s", nm);
            continue;
        }
        if (sscanf(line, "%63[^=]=%f", key, &v) != 2) continue;
        int iv = (int)v;
        if (!strncmp(key, "key_", 4)) {   // キーの割り当て
            for (int a = 0; a < ACT_COUNT; a++) if (!strcmp(key + 4, ActionId(a)) && iv > 0 && !KeyReserved(iv)) KeyAssign(a, iv);
            continue;
        }
        if (!strcmp(key, "sets")) s->sets = iv;           else if (!strcmp(key, "women")) s->women = iv;
        else if (!strcmp(key, "play_as")) s->you = iv;    else if (!strcmp(key, "opponent")) s->opp = iv;
        else if (!strcmp(key, "ai_level")) s->level = iv; else if (!strcmp(key, "style")) s->style = iv;
        else if (!strcmp(key, "court")) s->court = iv;    else if (!strcmp(key, "assist")) s->assist = iv;
        else if (!strcmp(key, "pro_feed")) s->feed = iv; else if (!strcmp(key, "aim_lanes")) s->lanes = iv;
        else if (!strcmp(key, "camera")) s->cam = iv;     else if (!strcmp(key, "screen_info")) s->hud = iv;
        else if (!strcmp(key, "guides")) s->guides = iv;  else if (!strcmp(key, "sensitivity")) s->sens = v;
        else if (!strcmp(key, "volume")) s->vol = v;
        else if (!strcmp(key, "language")) s->lang = iv;
        else if (!strcmp(key, "time_of_day")) s->tod = iv; else if (!strcmp(key, "weather")) s->wx = iv;
        else if (!strcmp(key, "look")) s->look = iv;
        else if (!strcmp(key, "slide")) s->slide = iv;
        else if (!strcmp(key, "bounce_ritual")) s->bounce = iv;
    }
    fclose(f);
    return true;
}

// 操作の案内（今のキーの割り当てで）
static const char *ControlsMove(void)
{
    static char mv[48];
    if (gKeys[ACT_UP] == KEY_W && gKeys[ACT_LEFT] == KEY_A && gKeys[ACT_DOWN] == KEY_S && gKeys[ACT_RIGHT] == KEY_D) snprintf(mv, sizeof(mv), "WASD");
    else if (gKeys[ACT_UP] == KEY_UP && gKeys[ACT_LEFT] == KEY_LEFT && gKeys[ACT_DOWN] == KEY_DOWN && gKeys[ACT_RIGHT] == KEY_RIGHT) snprintf(mv, sizeof(mv), "%s", TR("Arrows", "矢印"));
    else { snprintf(mv, sizeof(mv), "%s", KeyName(gKeys[ACT_UP])); for (int a = ACT_DOWN; a <= ACT_RIGHT; a++) { size_t L = strlen(mv); snprintf(mv + L, sizeof(mv) - L, "/%s", KeyName(gKeys[a])); } }
    return mv;
}
static const char *ControlsLine(void)
{
    static char buf[2][640];
    static int k = 0; k ^= 1;
    char mv[48]; snprintf(mv, sizeof(mv), "%s", ControlsMove());
    char lob[16], drop[16], cf[16], sh[3][16];
    snprintf(lob, sizeof(lob), "%s", KeyName(gKeys[ACT_LOB])); snprintf(drop, sizeof(drop), "%s", KeyName(gKeys[ACT_DROP]));
    snprintf(cf, sizeof(cf), "%s", KeyName(gKeys[ACT_CLICK_FLAT]));
    snprintf(sh[0], 16, "%s", KeyName(gKeys[ACT_TOP])); snprintf(sh[1], 16, "%s", KeyName(gKeys[ACT_SLICE])); snprintf(sh[2], 16, "%s", KeyName(gKeys[ACT_FLAT]));
    char cr[16], sm[16], mu[16];
    snprintf(cr, sizeof(cr), "%s", KeyName(gKeys[ACT_CROUCH])); snprintf(sm, sizeof(sm), "%s", KeyName(gKeys[ACT_SMASH])); snprintf(mu, sizeof(mu), "%s", KeyName(gKeys[ACT_MUTE]));
    if (gLang == LANG_JA)
        snprintf(buf[k], sizeof(buf[k]), "%s 移動  マウス 狙い  左クリック トップ  右クリック スライス  %s＋クリック フラット  %s＋クリック ロブ  %s＋クリック ドロップ  キーだけ：%s/%s/%s  %s かがむ  %s ラケット破壊  %s 消音  Esc メニュー",
                 mv, cf, lob, drop, sh[0], sh[1], sh[2], cr, sm, mu);
    else
        snprintf(buf[k], sizeof(buf[k]), "%s move  Mouse aim  L-click top  R-click slice  %s+click flat  %s+click lob  %s+click drop  Keys only: %s/%s/%s  %s crouch  %s smash racket  %s mute  Esc menu",
                 mv, cf, lob, drop, sh[0], sh[1], sh[2], cr, sm, mu);
    return buf[k];
}

// 選手の紹介（選手選択の画面に出す）
static const char *PersonaBlurb(int p)
{
    static const char *EN[PERSONA_COUNT] = {
        "No special traits. A solid all-round player.",
        PN("Elegant all-court genius. One-handed backhand, great slice and volleys. Loves grass.", "All-court player. One-handed backhand, good slice and volleys. Likes fast courts."),
        PN("The best defender and returner. Rubber-band sliding, ice cold on big points.", "Strong defender and returner. Slides well and stays calm on big points."),
        PN("Left-handed. Huge topspin forehand. King of clay, never gives up. Total emotional control: never smashes a racket.", "Left-handed. Heavy topspin forehand. Strong on clay and never gives up. Keeps his emotions in check: never smashes a racket."),
        PN("Tireless counter-puncher. Great lobs and returns. Talks to himself a lot.", "Tireless counter-puncher. Good lobs and returns."),
        PN("Explosive speed and power. Drop shots, net play, loves the big stage.", "Fast and powerful. Uses drop shots and comes to the net."),
        PN("Hits early and flat. Fewest errors, the best returner of the new generation.", "Hits early and flat. Few errors and a good returner."),
        PN("Plays far behind the baseline. Flat, low balls and almost no errors on hard courts.", "Stands far behind the baseline. Flat, low balls and few errors. Likes hard courts."),
        PN("Huge serve and wild shot-making. Underarm serves, easily frustrated.", "Big serve and creative shots. Moody and easily frustrated."),
        PN("Fast feet, takes the ball very early. Great returner, strong in deciding sets.", "Quick feet and takes the ball early. Good returner, strong in deciding sets."),
        PN("Big serve and solid two-handed backhand. 198cm. Clay and hard.", "Big serve and a solid two-handed backhand. Tall."),
        PN("The most powerful serve in the women's game. Fierce and clutch.", "Very powerful serve. Fierce and clutch."),
        PN("Heavy topspin and fast feet. Dominant on clay.", "Heavy topspin and fast feet. Strong on clay."),
        PN("Big hitter with a fast serve. Very aggressive.", "Big hitter with a fast serve. Very aggressive."),
        PN("Fastest legs on tour. Great defence.", "Very fast legs. Great defence."),
        PN("Flat and aggressive. Turns away to fix her strings. Strong on clay.", "Flat and aggressive. Loud on every shot."),
        PN("Slice backhand and volleys. Varied and clever. Loves grass.", "Slice backhand and volleys. Varied and clever. Likes fast courts."),
    };
    static const char *JA[PERSONA_COUNT] = {
        "癖のない、バランスのいい選手。",
        PN("優雅なオールラウンダー。片手バック、キレのあるスライスとボレー。芝が得意。", "オールラウンダー。片手バック、スライスとボレーがうまい。速いコートが得意。"),
        PN("守備とリターンが一番。よく滑って拾い、大事なポイントに一番強い。", "守備とリターンがうまい。よく滑って拾い、大事なポイントで落ち着いている。"),
        PN("左利き。強烈なトップスピンのフォア。クレーの王者で、最後まであきらめない。感情をコントロールでき、ラケットを壊さない。", "左利き。回転の強いフォア。クレーに強く、最後まであきらめない。感情をコントロールでき、ラケットを壊さない。"),
        PN("粘り強い守備の名手。ロブとリターンがうまい。独り言が多い。", "粘り強い守備型。ロブとリターンがうまい。"),
        PN("爆発的な足と強打。ドロップとネットプレー。大舞台に強い。", "速い足と強打。ドロップとネットプレーも使う。"),
        PN("早いタイミングでフラットに打つ。ミスが少なく、リターンがうまい。", "早いタイミングでフラットに打つ。ミスが少なく、リターンがうまい。"),
        PN("ベースラインのずっと後ろで構え、低いフラットでミスをしない。ハードが得意。", "ベースラインのずっと後ろで構え、低いフラットでミスをしない。ハードが得意。"),
        PN("とても速いサーブと気まぐれな天才肌。アンダーサーブも。すぐ怒る。", "速いサーブと型にはまらないショット。気分屋で、すぐ怒る。"),
        PN("足が速く、上がり際を早く打つ。リターンがうまく、最終セットに強い。", "足が速く、上がり際を早く打つ。リターンがうまく、最終セットに強い。"),
        PN("速いサーブと堅い両手バック。198cm。クレーとハードが得意。", "速いサーブと堅い両手バック。背が高い。"),
        PN("女子で一番速いサーブ。強気で、勝負どころに強い。", "とても速いサーブ。強気で、勝負どころに強い。"),
        PN("回転の強いトップスピンと速い足。クレーで圧倒的。", "回転の強いトップスピンと速い足。クレーに強い。"),
        PN("強打と速いサーブ。とても攻撃的。", "強打と速いサーブ。とても攻撃的。"),
        PN("ツアーで一番の俊足。守備がうまい。", "とても足が速い。守備がうまい。"),
        PN("フラットで強気。後ろを向いてガットを直す。クレーにも強い。", "フラットで強気。毎球、大きな声を出す。"),
        PN("スライスとボレー。多彩で頭がいい。芝が得意。", "スライスとボレー。多彩で頭がいい。速いコートが得意。"),
    };
    // 表は 0〜8 が男子（癖なし＋8 人）、9〜14 が女子、15・16 が錦織・ズベレフ。上の並びに合わせる
    static const int ORDER[PERSONA_COUNT] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 11, 12, 13, 14, 15, 16, 9, 10 };
    int i = ORDER[((p % PERSONA_COUNT) + PERSONA_COUNT) % PERSONA_COUNT];
    return gLang == LANG_JA ? JA[i] : EN[i];
}

// タイトル・選手選択の背景：コートの上に自分と相手が並んで立つ（ボールをつく）。カメラはゆっくり揺れる
bool gSlideOn = false;
static bool oppRandom = false, courtRandom = false;   // 選手と試合：相手・コートをランダムに（試合ごとに選び直す）
bool gBounceRit = true;   // サーブ前のボールつき（設定）：自分も相手もつく。なしにすると、すぐサーブ   // 滑り込み（設定）：はじめは「なし」（走ると止まれないと不評だったため）

// 能力のグラフ（六角形）：サーブ・強打・足と守備・安定・ネット・勝負強さ。全選手の中で比べて 3〜10 に
#define RADAR_N 6
static void PersonaRadar(int p, float out[RADAR_N])
{
    static float lo[RADAR_N], hi[RADAR_N]; static bool ready = false;
    #define RAW(q, v) do { const Persona *e = &PERSONAS[q]; \
        v[0] = e->serveAdd / 18.0f + 3.0f * e->serveSkillAdd - 2.0f * e->serveMarginAdd; \
        v[1] = 2.0f * e->aggrAdd + 1.5f * (e->spinMul - 1.0f); \
        v[2] = e->speedAdd - 5.0f * e->reactionAdd; \
        v[3] = -2.0f * (e->errMul - 1.0f) - (e->timingMul - 1.0f); \
        v[4] = e->volleySkill + 0.5f * (e->netRushMul - 1.0f) + e->serveVolley; \
        v[5] = e->clutch; } while (0)
    if (!ready) {
        for (int a = 0; a < RADAR_N; a++) { lo[a] = 1e9f; hi[a] = -1e9f; }
        for (int q = 1; q < PERSONA_COUNT; q++) { float v[RADAR_N]; RAW(q, v); for (int a = 0; a < RADAR_N; a++) { lo[a] = fminf(lo[a], v[a]); hi[a] = fmaxf(hi[a], v[a]); } }
        ready = true;
    }
    float v[RADAR_N]; RAW(p, v);
    for (int a = 0; a < RADAR_N; a++) out[a] = p == 0 ? 6.0f : 3.0f + 7.0f * (hi[a] > lo[a] ? (v[a] - lo[a]) / (hi[a] - lo[a]) : 0.5f);   // 癖なしは真ん中
    #undef RAW
}
static void DrawRadar(float cx, float cy, float r, const float val[RADAR_N], Color col)
{
    static const char *EN[RADAR_N] = { "SERVE", "POWER", "SPEED", "CONSISTENCY", "NET", "CLUTCH" };
    static const char *JA[RADAR_N] = { "サーブ", "強打", "足と守備", "安定", "ネット", "勝負強さ" };
    Vector2 ax[RADAR_N];
    for (int a = 0; a < RADAR_N; a++) { float an = -PI / 2 + a * 2 * PI / RADAR_N; ax[a] = (Vector2){ cosf(an), sinf(an) }; }
    for (int ring = 1; ring <= 5; ring++) {   // 目盛り（2 ずつ）
        float rr = r * ring / 5.0f;
        for (int a = 0; a < RADAR_N; a++) DrawLineV((Vector2){ cx + ax[a].x * rr, cy + ax[a].y * rr }, (Vector2){ cx + ax[(a + 1) % RADAR_N].x * rr, cy + ax[(a + 1) % RADAR_N].y * rr }, Fade(RAYWHITE, ring == 5 ? 0.45f : 0.15f));
    }
    for (int a = 0; a < RADAR_N; a++) DrawLineV((Vector2){ cx, cy }, (Vector2){ cx + ax[a].x * r, cy + ax[a].y * r }, Fade(RAYWHITE, 0.15f));
    Vector2 pt[RADAR_N];
    for (int a = 0; a < RADAR_N; a++) { float k = val[a] / 10.0f; pt[a] = (Vector2){ cx + ax[a].x * r * k, cy + ax[a].y * r * k }; }
    for (int a = 0; a < RADAR_N; a++) {   // 中を塗る（真ん中から三角を並べる。どちら回りでも見えるように両方の向きで）
        Vector2 c0 = { cx, cy }, p1 = pt[a], p2 = pt[(a + 1) % RADAR_N];
        DrawTriangle(c0, p1, p2, Fade(col, 0.35f)); DrawTriangle(c0, p2, p1, Fade(col, 0.35f));
    }
    for (int a = 0; a < RADAR_N; a++) { DrawLineEx(pt[a], pt[(a + 1) % RADAR_N], 2.0f, col); DrawCircleV(pt[a], 3.0f, col); }
    for (int a = 0; a < RADAR_N; a++) {   // 名前と数
        const char *l = TextFormat("%s %d", gLang == LANG_JA ? JA[a] : EN[a], (int)roundf(val[a]));
        int fs = 14, w = MeasureText(l, fs);
        float lx = cx + ax[a].x * (r + 12), ly = cy + ax[a].y * (r + 12);
        int x = (int)(ax[a].x > 0.3f ? lx : ax[a].x < -0.3f ? lx - w : lx - w / 2), y = (int)(ax[a].y > 0.3f ? ly : ax[a].y < -0.3f ? ly - fs : ly - fs / 2);
        DrawText(l, x, y, fs, RAYWHITE);
    }
}

// lift：選手と試合の画面では、2 人を少し小さく上に（下に能力のグラフを出すため）
static void DrawShowcase(int youP, int oppP, Color youShirt, float t, float lookX, Vector2 *youScr, Vector2 *oppScr, float lift)
{
    Camera3D cam = { 0 };
    cam.up = (Vector3){ 0, 1, 0 }; cam.fovy = 38.0f; cam.projection = CAMERA_PERSPECTIVE;
    cam.position = (Vector3){ lookX + 0.5f * sinf(t * 0.25f), 1.75f + 0.20f * lift, -HALF_LEN + 3.4f - 1.4f * lift };
    cam.target   = (Vector3){ lookX, 1.05f - 0.75f * lift, -HALF_LEN + 9.0f };
    PixelBegin(); DrawSky(cam);   // 空（時間帯と天気）。奥行きの情報も消す（消さないと前のフレームと重なって乱れる）
    BeginMode3D(cam);
    DrawCourt();
    for (int k = 0; k < 2; k++) {
        if (k == 1 && oppP < 0) continue;   // 相手なし（グランドスラムの準備：相手は組み合わせで決まる）
        SwingAnim a = { 0 }; a.swingT = -1.0f;
        int p = k == 0 ? youP : oppP;
        ApplyLook(&a, k == 0 ? (p ? p : -1) : p);
        a.lefty = PERSONAS[p].lefty; a.oneHanded = PERSONAS[p].oneHanded;
        a.celebKind = RIT_BOUNCE; a.ritBounces = 4; a.celebDur = RitualDuration(RIT_BOUNCE, 4) + 0.8f;
        a.celebT = a.celebDur - fmodf(t + k * 1.3f, a.celebDur);
        Vector3 pos = { k == 0 ? 0.85f : -0.85f, 0.0f, -HALF_LEN + 9.0f };   // カメラから見て左が自分、右が相手
        DrawFigure(pos, true, k == 0 ? youShirt : PersonaShirt(p), &a, 0.0f);
        Vector2 s = GetWorldToScreen((Vector3){ pos.x, 2.15f, pos.z }, cam);
        if (k == 0 && youScr) *youScr = s;
        if (k == 1 && oppScr) *oppScr = s;
    }
    EndMode3D();
    AtmosOverlay(cam); PixelEnd();
}

// 優勝の表彰：コートの真ん中で、自分の選手がその大会のトロフィーを頭の上に掲げる。カメラはまわりをゆっくり回る
static void DrawCeremony(const Tournament *t, int youP, Color youShirt, float tm)
{
    int W = GetScreenWidth(), H = GetScreenHeight();
    Vector3 pos = { 0.0f, 0.0f, -SERVICE_Z - 1.0f };
    Camera3D cam = { 0 };
    cam.up = (Vector3){ 0, 1, 0 }; cam.fovy = 40.0f; cam.projection = CAMERA_PERSPECTIVE;
    float a = 0.55f * sinf(tm * 0.22f);   // 正面から左右へゆっくり回り込む
    cam.position = (Vector3){ pos.x + sinf(a) * 5.6f, 1.35f, pos.z - cosf(a) * 5.6f };
    cam.target = (Vector3){ pos.x, 1.45f, pos.z };
    PixelBegin(); DrawSky(cam);
    BeginMode3D(cam);
    DrawCourt();
    DrawStadium(cam.position, (Vector3){ 0, -10, 0 }, 1.0f, tm, (Vector3){ 0 }, 0.0f);
    SwingAnim an = { 0 }; an.swingT = -1.0f;
    ApplyLook(&an, youP ? youP : -1);
    an.lefty = PERSONAS[youP].lefty; an.oneHanded = PERSONAS[youP].oneHanded;
    an.celebKind = CELEB_TROPHY; an.celebDur = 10000.0f; an.celebT = an.celebDur - (tm + 2.0f);
    DrawFigure(pos, true, youShirt, &an, 0.0f);
    // トロフィー：両手の間（手は頭の上、少しだけ前）。手の上げ下げに合わせる
    float sc = an.scale > 0.0f ? an.scale : 1.0f;   // 背の高さで手の高さも変わる
    float e = tm + 2.0f, hy = (2.02f + 0.06f * sinf(e * 1.6f)) * sc;
    float th = TrophyHeight(t->slam, t->women);
    Vector3 base = { pos.x, hy + 0.03f - 0.30f * th, pos.z - 0.06f * sc };
    if (TrophyIsDish(t->slam, t->women)) base.y = hy - 0.5f * th;   // 大皿は真ん中を持つ
    DrawTrophy3D(t->slam, t->women, base, tm * 0.6f);
    EndMode3D();
    AtmosOverlay(cam); PixelEnd();
    // 紙吹雪
    for (int i = 0; i < 90; i++) {
        float x = fmodf(i * 97.3f + sinf(tm * 0.7f + i) * 40.0f, (float)W);
        float y = fmodf(i * 53.0f + tm * (70.0f + (i % 9) * 18.0f), (float)H + 40.0f) - 20.0f;
        Color c = (Color[]){ RED, YELLOW, SKYBLUE, GREEN, PINK, ORANGE, RAYWHITE }[i % 7];
        float w = 6.0f + 4.0f * fabsf(sinf(tm * 3.0f + i));
        DrawRectanglePro((Rectangle){ x, y, w, 5 }, (Vector2){ w / 2, 2.5f }, tm * 120.0f + i * 40.0f, c);
    }
    // 文字
    const char *ch = TR("CHAMPION", "優勝");
    DrawText(ch, W / 2 - MeasureText(ch, 72) / 2 + 4, 34 + 4, 72, Fade(BLACK, 0.5f));
    DrawText(ch, W / 2 - MeasureText(ch, 72) / 2, 34, 72, (Color){ 255, 215, 70, 255 });
    const char *nm = TextFormat("%s  -  %s  -  %s", TourName(t->slam), t->women ? TR("WOMEN'S SINGLES", "女子シングルス") : TR("MEN'S SINGLES", "男子シングルス"),
                                youP ? PERSONAS[youP].name : YouName());
    DrawText(nm, W / 2 - MeasureText(nm, 24) / 2, 118, 24, RAYWHITE);
    const char *msg = TourEndMessage(t);
    DrawRectangle(0, H - 74, W, 74, Fade(BLACK, 0.45f));
    DrawText(msg, W / 2 - MeasureText(msg, 24) / 2, H - 62, 24, YELLOW);
    const char *tab = TR("Tab: draw", "Tab：組み合わせ表");
    DrawText(tab, W / 2 - MeasureText(tab, 16) / 2, H - 28, 16, LIGHTGRAY);
}

// ポイントの大事さ（0 / 0.5 / 1）：ブレーク・セット・マッチポイントは 1、30-30・デュース・タイブレークは 0.5。試合とリターン練習で同じ決め方
static float PointImportance(const Score *sc)
{
    int sv = CurrentServer(sc), rv = 1 - sv;
    Score t1 = *sc, t2 = *sc;
    AwardPoint(&t1, rv); AwardPoint(&t2, sv);
    bool rvGame = t1.games[rv] != sc->games[rv] || t1.sets[rv] != sc->sets[rv];
    bool setPt  = t1.sets[rv] != sc->sets[rv] || t2.sets[sv] != sc->sets[sv];
    if (rvGame || setPt) return 1.0f;                                                              // ブレーク／セット／マッチポイント
    if (sc->tiebreak || (sc->points[0] >= 2 && sc->points[1] >= 2)) return 0.5f;                   // 30-30、デュース、タイブレーク
    return 0.0f;
}
// AI がサーブの構えに入る：サーブ前の儀式（選手ごと。大事なポイントほど長い）を始めて、構えてからトスまでの時間を返す。
//   試合とリターン練習の両方がこれを使う（サーブまでの間が同じになるように）
static float AiServePrep(Opponent *ai, int serveNo)
{
    float wait = RandRange(1.2f, 2.2f);   // 構えてからトスまで（毎回同じリズムだと読めてしまう）
    if (gBounceRit) {   // サーブ前の儀式：選手ごと（ジョコビッチは何度もつく、ナダルは肩・鼻・耳を触る…）。大事なポイントほど長い
        const Mental *mm = &MENTAL[ai->persona % PERSONA_COUNT];
        int nb = GetRandomValue(mm->bMin, mm->bMax) + (gPointImportance >= 0.5f ? mm->bBig : 0);
        if (serveNo == 2 && mm->serveRit != RIT_BOUNCE) nb = GetRandomValue(1, 3);   // セカンドは短く
        int kind = serveNo == 2 && mm->serveRit == RIT_STRINGS ? RIT_BOUNCE : mm->serveRit;
#ifdef SALE_BUILD
        kind = RIT_BOUNCE;   // 売る版：決まったルーティン（儀式・髪・ガット）はなく、ボールをつくだけ
#endif
        ai->anim.celebKind = kind; ai->anim.ritBounces = nb;
        ai->anim.celebDur = RitualDuration(kind, nb); ai->anim.celebT = ai->anim.celebDur;
        ai->anim.swingT = -1.0f; ai->anim.windup = 0.0f;
        wait = ai->anim.celebDur + RandRange(0.15f, 0.45f);
    }
    return wait;
}

int main(void)
{
    SetConfigFlags(FLAG_MSAA_4X_HINT | (MOBILE_ONLY ? 0 : FLAG_WINDOW_RESIZABLE));   // スマホ版は 1280×720 のまま（ページの方で縮めて出す）
    InitWindow(1280, 720, "SWEET SPOT");
    SfxInit();
    UiFontInit();   // 日本語フォント
    InitBoards();   // 奥の壁の看板とスピードメーター
#ifndef __EMSCRIPTEN__
    SetTargetFPS(60);   // ブラウザでは、ブラウザが画面を描く間隔に合わせる（raylib が毎フレーム待つ）
#endif
    srand((unsigned)GetTime() ^ 0x5eed);
    DisableIme(GetWindowHandle());   // 日本語入力が ON だとキーを離したことが届かず、押しっぱなしになることがある
    // マウス：マイクラと同じようにカーソルをウィンドウに固定して隠す。Esc で外す／戻す（Esc で終了しない）
    SetExitKey(KEY_NULL);
    bool mouseLocked = false;      // タイトルメニューではカーソルを出す

    static PlayLog plog = { 0 };   // プレイ記録（playlog.csv）
    LogOpen(&plog);
    MLogOpen();   // 試合の記録（matchlog.csv）
    int mlPoint = 0, mlShot = 0;         // 記録用：ポイントと打った球の通し番号
    Score mlBefore = { 0 };              // ポイントが始まったときのスコア
    float youMinDist = 99.0f;            // AI の球に一番近づいた距離（ウィナーを取られたとき、どれだけ遠かったか）
    bool showStats = false;        // Tab：このセッションの統計
    int  assistLevel = 2;          // H：手がかり表示 2=立つ場所＋タイミング 1=タイミングだけ 0=なし
    PlayStyle style = STYLE_REAL;  // N：難易度
    AssistPlan aplan = { 0 };      // 今来ている球の予定（相手が打った瞬間に一度だけ作る）
    float aplanAge = 1e9f;         // 予定を作ったときの球の経過時間（減ったら新しい球）
    int   aplanType = -1;          // 予定を作ったときの球種

    // 自分（プレイヤー）。2 人目を足すときは、これを配列にする
    Player P = NewPlayer((Vector3){ 0.0f, 0.0f, HALF_LEN + 0.8f });

    Camera3D cam = { 0 };
    cam.up = (Vector3){ 0, 1, 0 };
    cam.fovy = 45.0f;
    cam.projection = CAMERA_PERSPECTIVE;
    int camMode = 0;


    Ball ball = { 0 };
    float feedTimer = 1.0f;          // 0 になったら次の球を出す
    Vector3 feederPos = { 0.0f, 0.0f, -HALF_LEN - 0.6f };   // 練習で球を出す相手の立ち位置
    bool  feedSwing = false; Ball feedNext = { 0 };          // 球出しの振り：振り始めたら、当たる瞬間に球を出す
    // リターン練習の中のスコア（相手がずっとサーブするゲーム）。試合と同じように、ポイントのあとに主審がスコアをコールする
    //   サーブは試合の AI のサーブと同じ処理（AiServePrep → トス → AiServe）。RET_WAIT：次のサーブを待つ（feedTimer）、RET_SERVE：構え・儀式・トス、RET_LIVE：球が生きている
    Score retScore = { .server = 1 };
    enum { RET_WAIT, RET_SERVE, RET_LIVE };
    int   retPhase = RET_WAIT;
    float winFx = -1.0f;
    int   youSmashes = 0; float youSmashPrevE = 0.0f;
    int   speedBoardKmh = 0;             // 奥の壁のスピードメーター（最後のサーブ）
    Vector3 protestTo = { 0 };           // 抗議で歩いていく先（審判台の前）
#define PROTEST_WALK 2.0f                // 審判の方へ歩く速さ（m/s）                // 自分がラケットを叩きつけた回数（2 回目からポイントペナルティ）
    float sweetT = 9.0f; Vector3 sweetPos = { 0 }; float sweetQ = 0.0f;   // いい当たりの演出（経過時間・位置・強さ）
#define SWEET_Q 0.80f   // これ以上の当たりで演出を出す             // 試合に勝った演出の経過時間（-1 = なし）
    Vector3 trail[TRAIL_N];
    int trailCount = 0;
    float trailTimer = 0.0f;
    Color trailColor = LIGHTGRAY;

    // 溜め打ち：押している間に溜まり、離した瞬間にスイング（入力の読み方は input.c）

    // 最後のショットの記録
    bool scoredLast = true;

    int stats[SHOT_COUNT][2] = { 0 };   // [球種][0=打った数, 1=イン]
    bool slowMo = false;
    float accumulator = 0.0f;
    PracticeMode mode = MODE_MATCH;
    bool proFeed = true;

    // サーブ（練習・試合共通）
    int   serveNo = 1;                  // 1st / 2nd
    bool  deuceSide = true;
    float svTimer = 0.0f;
    const char *serveMsg = "";
    bool  serveAce = false;
    int   svStats[2][2] = { 0 };        // [1st/2nd][0=打った数, 1=イン]
    int   aces = 0, doubleFaults = 0;
    // 試合のサーブの成績 [0=自分 1=AI][0=1st 打数 1=1st 入 2=2nd 打数 3=2nd 入 4=返されなかった 5=ダブルフォルト]
    int   mst[2][6] = { { 0 } };
    bool  retPending = false;          // 入ったサーブの、リターンの結果待ち
    int   retServer = 0;
    MatchStats ms[2] = { 0 };          // 試合の統計（マッチサマリー）
    bool  netFlag[2] = { false };      // このポイントでネットに出たか
    int   setsToWin = 0;               // 試合の長さ：0=タイブレークだけ（7 点先取） 1=1 セット 2=3 セットマッチ 3=5 セットマッチ
    bool  setBreak = false;
    float crowdExcite = 0.0f;            // 観客の盛り上がり（0〜1）
    float stagS = 0.0f;                  // 自分のよろめきの強さ（見た目）
    Vector3 lineCallPos = { 0 };         // 線審がアウトをコールした球の位置
    float lineCallT = 0.0f;
    char  rageMsg[160] = { 0 };          // ラケットを叩きつけたときの表示（審判のコール）
    float rageMsgT = 0.0f, smashPrevE = 0.0f;            // セット間：統計を見せて、Enter / クリックで次のセットへ
    float summaryT = 0.0f;
    // 体験版：この試合がずっと「AI ふつう以上・リアル」だったか（途中で変えたら証明コードは出さない）と、勝って出たコード
    bool demoClean = true;
    bool matchAssisted = false;   // この試合で自動操作・スロー・難しさの切り替えを使った（記録に数えない）
    char demoCode[40] = "";
    bool demoTried = false;   // 勝ったが条件に足りなかった
    bool demoCounted = false; // この試合を、遊んだ試合の数に入れた
    // テニスの外の楽しみ：観客をあおる（E）、球を観客席へ（Z）、ハプニング（ハト・「お静かに」）
    int lastPtWinner = -1;        // 直前のポイントを取った方
    int tauntIdx = 0, taunts = 0, tauntGame = -1;   // あおった回数（同じゲームの中で）
    float quietT = 0.0f;          // 「お静かに」の残り時間（サーブの前：ざわつく → 主審が注意 → 静まる → サーブ）
    bool  quietPending = false;
    float inputLockT = 0.0f;      // 試合・練習に入った直後：この間は打つ・サーブの入力を受けない   // 次のサーブの前に「お静かに」が起きる
    float youSwatPrevE = 0.0f;
    float swatCatchT = 0.0f;      // かっ飛ばした球が観客席に届くまで（届いたら観客が捕って沸く）
    // バトルモード（試合に体力をつける）：ポイントを取られる・体にボールを当てられると体力が減り、0 になったら K.O. 負け
    bool  battle = false;
    float hp[2] = { BATTLE_HP, BATTLE_HP }, hpShown[2] = { BATTLE_HP, BATTLE_HP };   // 体力（見せる値は少し遅れて減る）
    float koT = -1.0f;            // K.O. の文字を出している時間
    // 撮影モード（F3）：画面の文字や印を消して、カメラを映画のような位置に（宣伝の画像・動画用）。0 = なし
    int   cineMode = 0; float cineLabelT = 0.0f; Vector3 cinePos = { 0 }, cineTgt = { 0 }; bool cineFirst = true;
    // はじめての人への練習：0 動く → 1 打つ → 2 狙う → 3 サーブ → 4 終わり。-1 = していない
    int   tut = -1, tutBase = 0, tutLastShot = -1; bool tutL = false, tutR = false; float tutT = 0.0f;
    float hitFlashT[2] = { 0 };   // 体に当たった：画面の赤い点滅
    bool  showSummary = false;
    int   rallyHits = 0;
    static char outMsg[64];            // 「OUT 12cm」の表示           // このポイントで打った回数（拍手の大きさ）
    bool  prevHitNet = false, prevAiSliding = false;
    float soundVolBeforeMute = 0.8f;
    bool  aimLanes = false;            // 狙い方：false＝曲線（どこでも狙える、外には出ない） true＝5 か所から選ぶ
    char  toast[96] = { 0 };           // 画面の真ん中下に少しの間だけ出すお知らせ
    float toastT = 0.0f;
    bool  playerGrunt = false, aiGrunt = false, botGrunt = false;   // この球で、打つ前の声を鳴らし始めたか
    bool  playerGruntTried = false, aiGruntTried = false, botGruntTried = false;   // この球で、打つ前の声をもう鳴らしたか
    bool  playerFoot = false, aiFoot = false;   // この球で、フットワークの音をもう鳴らしたか
    bool  aiTossing = false;           // AI がトスを上げている
    float aiTossT = 0.0f;
    bool  aiUnderarm = false;          // アンダーサーブ         // Tab の 2 回目：試合の統計

    // 試合
    Opponent ai = { 0 };
    ai.level = 0;   // はじめは AI の強さ「やさしい」、試合の長さ「タイブレーク」（設定ファイルがあればそちら）
    ai.persona = 1;   // 相手の選手（1〜4）
    Score score = { 0 };
    MatchPhase phase = PH_SERVE;
    bool  serveJudged = false;
    float phaseTimer = 0.0f;
    const char *pointMsg = "";
    char  scoreCall[64] = "";          // ポイントの後に大きく出すコール（DEUCE、GAME など）
    bool  needSetup = true;            // 次のポイント（またはセカンドサーブ）の準備をする
    // コートチェンジ：奇数ゲームの後（セットの終わりも、そのセットのゲーム数が奇数なら）。両選手がベンチへ歩き、
    //   （1 ゲーム目の後以外は）休み、画面が一瞬暗くなる間に審判台とベンチが反対側になって、自分の位置へ戻る
    bool  changePending = false, changeSit = false, changeActive = false, changeFlipped = false;
    float changeT = 0.0f, changeDur = 0.0f;
    Vector3 coFromP = { 0 }, coFromA = { 0 };
    bool  autopilot = false;           // B：自分側も AI が操作する（観戦モード）
    Opponent bot = { 0 };              // 自分側を操作する AI（座標は 180° 回した世界）

    // メニュー
    AppState app = APP_TITLE;
    float bracketWait = 0.0f;          // 組み合わせ表に移ってすぐは入力を受け付けない（移るときの Enter が残っていて、表が一瞬で消えないように）
    static Tournament tour = { 0 };      // 大会
    static char oppShort[24] = "";       // 大会の予選上がりの名前（試合中の表示用）
#define OPP_SHORT ((ai.persona == 0 && tour.active && oppShort[0]) ? oppShort : PERSONAS[ai.persona].shortName)
    int slamSel = 0;                     // メニューで選んだグランドスラム（4 = シーズン）
    // 画面の表示：0 FULL（全部）1 CLEAN（操作説明と状態の行を消す）2 MINIMAL（スコアと溜めのゲージだけ）3 OFF（中継のようにスコアだけ）
    int hudLevel = 0;
    // ガイド（コートの上の印）：0 ALL 1 AIM ONLY（狙いの印だけ）2 OFF
    int guideLevel = 0;
    static const char *HUD_NAMES_EN[4] = { "FULL", "CLEAN (no help lines)", "MINIMAL (score + gauge)", "OFF (TV style)" };
    static const char *HUD_NAMES_JA[4] = { "全部", "すっきり（説明の行なし）", "最小（スコアとゲージ）", "なし（中継のように）" };
    static const char *GUIDE_NAMES_EN[3] = { "ALL", "AIM ONLY", "OFF" };
    static const char *GUIDE_NAMES_JA[3] = { "全部", "狙いの印だけ", "なし" };
#define HUD_NAMES (gLang == LANG_JA ? HUD_NAMES_JA : HUD_NAMES_EN)
#define GUIDE_NAMES (gLang == LANG_JA ? GUIDE_NAMES_JA : GUIDE_NAMES_EN)
    // リプレイ・ハイライト
    long pointStart = 0;                 // 今のポイントが始まったフレーム（記録の通し番号）
    bool hlPending = false;              // 前のポイントをハイライトに取っておく
    char hlCap[80] = { 0 };
    float hlScore = 0.0f;
    struct { bool on; int mode, clip, idx; float t, acc; Camera3D cam; bool first; int view; } rp = { 0 };   // mode 0 = 今のポイント、1 = ハイライト
    int   menuSel = 0;
    float aimSens = 1.0f;          // マウス感度（倍率）
    bool  forceRestart = false;    // メニューでモードや AI を変えたら、戻ったときにやり直す
    bool  quitGame = false;
    bool  skipMouseDelta = false;
    int   menuFrames = 0;              // メニューを開いてからのフレーム数
    // 設定：前回の設定を読む（なければ今の初期値）
    KeysReset();
#define SaveSettings() do { Settings s_ = { setsToWin, gWomen, gYouPersona, ai.persona, ai.level, (int)style, (int)gSurface, assistLevel, proFeed, aimLanes, \
                                            camMode, hudLevel, guideLevel, aimSens, SfxGetVolume(), gLang, gTimeSetting, gWeatherSetting, gLook, gSlideOn, gBounceRit }; SaveSettingsFile(&s_); } while (0)
    {
        Settings s = { setsToWin, gWomen, gYouPersona, ai.persona, ai.level, (int)style, (int)gSurface, assistLevel, proFeed, aimLanes,
                       camMode, hudLevel, guideLevel, aimSens, SfxGetVolume(), gLang, gTimeSetting, gWeatherSetting, gLook, gSlideOn, gBounceRit };
        if (LoadSettingsFile(&s)) {
            setsToWin = DEMO_ONLY ? 1 : Clamp(s.sets, 0, 3); gWomen = s.women != 0;   // 体験版は 1 セットだけ
            gYouPersona = (s.you > 0 && s.you < PERSONA_COUNT && PERSONAS[s.you].female == gWomen) ? s.you : 0;
            ai.persona = (s.opp > 0 && s.opp < PERSONA_COUNT && PERSONAS[s.opp].female == gWomen) ? s.opp : TourPersonaAt(gWomen, 0);
            ai.level = Clamp(s.level, 0, 2); style = (PlayStyle)Clamp(s.style, 0, STYLE_COUNT - 1); gSurface = (SurfaceType)Clamp(s.court, 0, SURF_COUNT - 1);
            assistLevel = Clamp(s.assist, 0, 2); proFeed = s.feed != 0; aimLanes = s.lanes != 0; camMode = Clamp(s.cam, 0, 2);
            hudLevel = Clamp(s.hud, 0, 3); guideLevel = Clamp(s.guides, 0, 2);
            aimSens = Clamp(s.sens, 0.25f, 3.0f); SfxSetVolume(Clamp(s.vol, 0.0f, 1.0f));
            gLang = s.lang == LANG_EN ? LANG_EN : LANG_JA;
            gTimeSetting = Clamp(s.tod, 0, TOD_COUNT); gWeatherSetting = Clamp(s.wx, 0, WX_COUNT);
            gLook = Clamp(s.look, 0, 2);
            gSlideOn = s.slide != 0;
            gBounceRit = s.bounce != 0;
        }
        AtmosNewMatch();
    }
    if (MOBILE_ONLY) { setsToWin = 0; mode = MODE_MATCH; style = STYLE_CASUAL; hudLevel = 1; }   // スマホ版：タイブレークの試合、かんたん、説明の行なし

    // 直前のフレームの物理ステップごとのボールの状態。
    // キー入力はフレーム単位（1/60 秒）でしか取れないので、「離したのはこのフレームの間のどこか」として
    // その間で一番いいタイミングの瞬間を採用する（入力の粗さで損をしないように）
    Ball history[64];
    int historyCount = 0;

    // バウンドの目印
    Vector3 bouncePos = { 0 };
    float bounceT = 10.0f;
    Vector3 markPos = { 0 };             // 最初に弾んだ跡（実寸）
    float markT = 10.0f;
    Ball hawk = { 0 };                   // 際どい判定の拡大図に使う球
    float hawkT = 10.0f;
    bool  hawkPending = false;           // 際どい球があった：ポイント（ラリー）が終わってから拡大図を出す
    bool  bounceByPlayer = false;      // 自分の打球が落ちた（狙いとのずれを表示する）
    int prevBounces = 0;

    while (!WindowShouldClose() && !quitGame) {
#ifdef __EMSCRIPTEN__
        {   // ブラウザ版：ページの大きさが変わったら、ゲームの画面も合わせる
            int iw = EM_ASM_INT({ return window.innerWidth; }), ih = EM_ASM_INT({ return window.innerHeight; });
            if (!MOBILE_ONLY && iw > 0 && ih > 0 && (iw != GetScreenWidth() || ih != GetScreenHeight())) SetWindowSize(iw, ih);
        }
#endif
        float frameDt = fminf(GetFrameTime(), 0.1f);
        float simDt = frameDt * (slowMo ? 0.25f : 1.0f);
        crowdExcite = fmaxf(crowdExcite - frameDt * 0.35f, 0.0f);
        gBattle = battle && mode == MODE_MATCH;
        gRallyPartner = mode == MODE_RALLY;
        if (app == APP_PLAY && mode == MODE_MATCH) {
            int pe = PigeonUpdate(frameDt);
            if (pe == 1) { SfxOoh(0.4f); snprintf(toast, sizeof(toast), "%s", TL("A pigeon has landed on court...", "コートにハトが…")); toastT = 2.5f; }
            if (pe == 2) { crowdExcite = fmaxf(crowdExcite, 0.6f); SfxPointEnd(6, false); }   // 追い払うと、観客が笑って拍手
            if (gPigeon.on) phaseTimer = fmaxf(phaseTimer, 0.8f);   // いなくなるまで次のポイントを始めない
            if (swatCatchT > 0.0f) {   // かっ飛ばした球が奥の観客席に届いた：観客が捕って沸く
                swatCatchT -= frameDt;
                if (swatCatchT <= 0.0f) { crowdExcite = fmaxf(crowdExcite, 0.7f); SfxPointEnd(5, false); snprintf(toast, sizeof(toast), "%s", TL("A fan catches it!", "観客がキャッチ！")); toastT = 2.0f; }
            }
            if (quietT > 0.0f) {
                float q0 = quietT; quietT -= frameDt;
                if (q0 >= QUIET_SAY && quietT < QUIET_SAY)     { snprintf(toast, sizeof(toast), "%s", TL("UMPIRE: Quiet, please.", "主審：お静かに願います")); toastT = 1.8f; }
                if (q0 >= QUIET_THANKS && quietT < QUIET_THANKS) { snprintf(toast, sizeof(toast), "%s", TL("UMPIRE: Thank you.", "主審：ありがとうございます")); toastT = 1.4f; }
            }
        }
        lineCallT = fmaxf(lineCallT - frameDt, 0.0f);
        // スプリットステップの見た目の時間
        if (P.anim.splitT > 0.0f)   P.anim.splitT   -= simDt;
        if (bot.anim.splitT > 0.0f) bot.anim.splitT -= simDt;
        if (ai.anim.splitT > 0.0f)  ai.anim.splitT  -= simDt;

        gAllWhite = tour.active && tour.slam == 2;   // ウィンブルドンは白い服
        snprintf(gOppName, sizeof(gOppName), "%s", OPP_SHORT);   // 相手の名前（テロップ用）
        ApplyHumanTraits();                          // 自分が使う選手の癖（足、回転、ミス、サーブ、身長、利き手）
        // ================= 大会の組み合わせ表 =================
        if (app == APP_BRACKET) {
            SfxCrowd(false, 0.0f);
            bool goKey = IsKeyPressed(KEY_ENTER) || IsMouseButtonPressed(MOUSE_BUTTON_LEFT) || (autopilot && IsKeyPressed(KEY_SPACE));
            if (bracketWait > 0.0f) { bracketWait -= GetFrameTime(); goKey = false; }
            if (goKey && TourRevealActive()) { TourRevealSkip(); goKey = false; }   // 結果を見せている途中：飛ばすだけ
            if (goKey) {
                int op = TourOpponent(&tour);
                if (op < 0 && TourNextSlam(&tour)) {
                    // シーズン：次の大会の組み合わせ表へ
                } else if (op >= 0) {
                    {   // 予選上がりは名字を大文字で
                        const char *nm = TourOpponentName(&tour), *sp = strrchr(nm, ' ');
                        snprintf(oppShort, sizeof(oppShort), "%s", sp ? sp + 1 : nm);
                        for (char *c = oppShort; *c; c++) *c = (char)toupper((unsigned char)*c);
                    }
                    ai.persona = op; gWomen = tour.women; gSurface = (SurfaceType)tour.surface; setsToWin = tour.setsToWin;
                    mode = MODE_MATCH; forceRestart = true; app = APP_PLAY; inputLockT = INPUT_LOCK;
                    mouseLocked = true; DisableCursor(); skipMouseDelta = true;
                } else {
                    tour.active = false; app = APP_TITLE; menuSel = 0;
                }
            }
            // 優勝したら、結果を見せ終わってから表彰の場面（Tab で組み合わせ表と切り替え）
            static bool showDraw = false;
            if (IsKeyPressed(KEY_TAB)) showDraw = !showDraw;
            if (!tour.champion) showDraw = false;
            BeginDrawing();
            if (tour.champion && !TourRevealActive() && !showDraw)
                DrawCeremony(&tour, gYouPersona, gYouPersona ? PersonaShirt(gYouPersona) : (Color){ 240, 240, 245, 255 }, (float)GetTime());
            else
                TourDraw(&tour, GetScreenWidth(), GetScreenHeight(), (float)GetTime());
            EndDrawing();
            continue;
        }

        // ================= メニュー（タイトル / 一時停止） =================
        if (app != APP_PLAY) menuFrames++; else menuFrames = 0;
        if (app != APP_PLAY) {
            SfxCrowd(false, 0.0f);   // メニューの間は観客の音を止める
            enum { MI_PLAY, MI_TOURNAMENT, MI_MODE, MI_SETS, MI_TOUR, MI_PLAYER, MI_OPP, MI_AI, MI_STYLE, MI_COURT, MI_ASSIST, MI_FEED, MI_SENS, MI_AIM, MI_SOUND, MI_CAMERA, MI_HUD, MI_GUIDES, MI_LANG, MI_RESTART, MI_TITLE, MI_QUIT,
                   MI_PG_MATCH, MI_PG_SETTINGS, MI_BACK, MI_NAME, MI_TIME, MI_WEATHER, MI_SLAM_GO, MI_CREDITS, MI_KEYS, MI_KEYRESET, MI_LOOK, MI_TUTORIAL, MI_SLIDE, MI_START, MI_BOUNCE, MI_KEY0 };
            static int keyCap = -1;   // キー設定：押されたキーを待っている操作（-1 = 待っていない）
            static bool quitAsk = false;   // 「ゲームを終了しますか？」を聞いている（タイトルに戻るつもりで押す人が多いので確認する）
            static char keyMsg[96] = ""; static float keyMsgT = 0.0f;
            static bool nameEdit = false;   // 名前を入力している
            // ページ：0 = タイトル / 一時停止、1 = 選手と試合、2 = 設定、3 = グランドスラムの準備（大会・選手・強さ・試合の長さを決めて始める）、4 = クレジット、5 = キー設定
            static int menuPage = 0; static AppState lastMenuApp = APP_PLAY;
            if (app != lastMenuApp) { menuPage = 0; lastMenuApp = app; keyCap = -1; }
            int items[32], n = 0;
            if (menuPage == 0 && MOBILE_ONLY) {   // スマホ版：タイブレークの試合だけ。キーボードを使う項目はなし
                items[n++] = MI_PLAY; if (app == APP_PAUSE) items[n++] = MI_PG_MATCH; items[n++] = MI_PG_SETTINGS;
                if (app == APP_PAUSE) { items[n++] = MI_RESTART; items[n++] = MI_TITLE; }
                if (app == APP_TITLE) items[n++] = MI_CREDITS;
            } else if (menuPage == 1 && MOBILE_ONLY) {
                items[n++] = MI_TOUR; items[n++] = MI_PLAYER; items[n++] = MI_OPP; items[n++] = MI_AI;
                items[n++] = MI_COURT; items[n++] = MI_TIME; items[n++] = MI_WEATHER; items[n++] = MI_STYLE; items[n++] = MI_START; items[n++] = MI_BACK;
            } else if (menuPage == 2 && MOBILE_ONLY) {
                items[n++] = MI_ASSIST; items[n++] = MI_SENS; items[n++] = MI_SOUND; items[n++] = MI_LOOK; items[n++] = MI_HUD; items[n++] = MI_LANG; items[n++] = MI_BACK;
            } else if (menuPage == 0) {
                items[n++] = MI_PLAY;
                if (app == APP_TITLE) items[n++] = MI_TUTORIAL;
                if (app == APP_TITLE && !DEMO_ONLY) items[n++] = MI_TOURNAMENT;   // 体験版はグランドスラムなし
                if (app == APP_PAUSE) items[n++] = MI_PG_MATCH;   // タイトルでは「プレイ」から選手と試合へ
                items[n++] = MI_PG_SETTINGS;
                if (app == APP_PAUSE) { items[n++] = MI_RESTART; items[n++] = MI_TITLE; }
                if (app == APP_TITLE) items[n++] = MI_CREDITS;
#ifndef __EMSCRIPTEN__
                items[n++] = MI_QUIT;   // ブラウザ版は終わる項目なし（ページを閉じる）
#endif
            } else if (menuPage == 1) {
                items[n++] = MI_MODE; items[n++] = MI_TOUR; items[n++] = MI_PLAYER; items[n++] = MI_OPP; items[n++] = MI_AI;
                items[n++] = MI_COURT; items[n++] = MI_TIME; items[n++] = MI_WEATHER; if (mode == MODE_MATCH) items[n++] = MI_SETS; items[n++] = MI_STYLE; items[n++] = MI_START; items[n++] = MI_BACK;   // 練習では試合の長さは出さない
            } else if (menuPage == 4) {
                items[n++] = MI_BACK;
            } else if (menuPage == 5) {
                for (int a = 0; a < ACT_COUNT; a++) items[n++] = MI_KEY0 + a;
                items[n++] = MI_KEYRESET; items[n++] = MI_BACK;
            } else if (menuPage == 3) {
                items[n++] = MI_TOURNAMENT; items[n++] = MI_TOUR; items[n++] = MI_PLAYER; items[n++] = MI_AI; items[n++] = MI_SETS;
                items[n++] = MI_STYLE; items[n++] = MI_TIME; items[n++] = MI_WEATHER; items[n++] = MI_SLAM_GO; items[n++] = MI_BACK;
            } else {
                items[n++] = MI_NAME; items[n++] = MI_ASSIST; items[n++] = MI_AIM; items[n++] = MI_SENS; items[n++] = MI_KEYS; items[n++] = MI_SLIDE; items[n++] = MI_BOUNCE; items[n++] = MI_FEED; items[n++] = MI_SOUND;
                items[n++] = MI_LOOK; items[n++] = MI_HUD; items[n++] = MI_GUIDES; items[n++] = MI_LANG; items[n++] = MI_BACK;
            }
            if (menuSel >= n) menuSel = 0;
            const char *assistNames[3] = { TR("OFF", "なし"), TR("TIMING", "タイミング"), TR("TIMING + SPOT", "タイミング＋立つ場所") };
            const char *camNames[3] = { TR("BEHIND", "後ろから"), TR("SIDE VIEW", "横から"), TR("FOLLOW BALL", "ボールを追う") };
            (void)camNames;

            int W = GetScreenWidth(), H = GetScreenHeight();
            bool titleMain = app == APP_TITLE && menuPage == 0;
            // 並べ方：タイトルは左に大きく、選手と試合は左のパネル（右に選手）、それ以外は真ん中のパネル
            int pw, rowH, fs, px, py, ph, rowY0;
            if (titleMain) { pw = 520; rowH = n >= 7 ? 46 : 54; fs = n >= 7 ? 28 : 30; px = 70; rowY0 = n >= 7 ? H / 2 - 50 : H / 2 - 30; py = rowY0 - 20; ph = n * rowH + 40; }   // 項目が多いときは詰める
            else if (menuPage == 4) { pw = Clamp(W - 120, 600, 1100); rowH = 40; fs = 22; px = W / 2 - pw / 2; py = 30; ph = H - 60; rowY0 = py + ph - 60; }
            else if (menuPage == 5) { pw = 760; rowH = Clamp((H - 190) / n, 24, 40); fs = rowH >= 32 ? 20 : 18; ph = 96 + n * rowH + 44; px = W / 2 - pw / 2; py = H / 2 - ph / 2; rowY0 = py + 90; }
            else if (menuPage == 1 || menuPage == 3) { pw = 600; rowH = Clamp((H - 260) / n, 28, 44); fs = rowH >= 36 ? 22 : 20; px = 40; py = 70; rowY0 = py + 90; ph = 90 + n * rowH + 30; }
            else { pw = 680; rowH = Clamp((H - 260) / n, 28, 46); fs = rowH >= 36 ? 24 : 20; px = W / 2 - pw / 2; ph = 110 + n * rowH + 60; py = H / 2 - ph / 2; rowY0 = py + 100; }

            // 名前の入力中：英数字（大文字）を 12 文字まで。Enter / Esc で終わる
            if (nameEdit) {
                int ch;
                while ((ch = GetCharPressed()) > 0) {
                    size_t L = strlen(gPlayerName);
                    if (ch >= 'a' && ch <= 'z') ch -= 32;
                    if (L < 12 && ((ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == ' ' || ch == '-' || ch == '.')) { gPlayerName[L] = (char)ch; gPlayerName[L + 1] = 0; }
                }
                if (IsKeyPressed(KEY_BACKSPACE) && gPlayerName[0]) gPlayerName[strlen(gPlayerName) - 1] = 0;
                if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_ESCAPE) || IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                    nameEdit = false;
                    if (!gPlayerName[0]) snprintf(gPlayerName, sizeof(gPlayerName), "PLAYER");
                    SaveSettings();
                }
            }
            // キー設定：押されたキーをその操作に割り当てる（Esc でやめる）。マウスのクリックは割り当てない
            bool keyWaiting = keyCap >= 0;
            if (keyCap >= 0) {
                int k;
                while ((k = GetKeyPressed()) > 0) {
                    if (k == KEY_ESCAPE) { keyCap = -1; break; }
                    if (k == KEY_RIGHT_SHIFT) k = KEY_LEFT_SHIFT;   // 左右は同じに扱う
                    if (k == KEY_RIGHT_CONTROL) k = KEY_LEFT_CONTROL;
                    if (k == KEY_RIGHT_ALT) k = KEY_LEFT_ALT;
                    if (KeyReserved(k)) {
                        snprintf(keyMsg, sizeof(keyMsg), TR("%s is used for something else. Try another key", "%s はほかに使っている。別のキーで"), KeyName(k));
                        keyMsgT = 2.5f; continue;
                    }
                    int was = -1; for (int b = 0; b < ACT_COUNT; b++) if (b != keyCap && gKeys[b] == k) was = b;
                    if (was >= 0) { snprintf(keyMsg, sizeof(keyMsg), TR("Swapped with \"%s\" (now %s)", "「%s」と入れ替えた（そちらは %s）"), ActionName(was), KeyName(gKeys[keyCap])); keyMsgT = 3.0f; }
                    KeyAssign(keyCap, k);
                    keyCap = -1;
                    SaveSettings();
                    break;
                }
            }
            if (keyMsgT > 0.0f) keyMsgT -= GetFrameTime();
            // 入力：W/S（↑↓）で選ぶ、A/D（←→）で変える、Enter で決定、Esc で戻る。マウス：左クリックで次、右クリックで前
            int change = 0; bool activate = false;
            if (quitAsk) {   // 終了の確認：Enter / クリック / Y で終了、Esc / N / 右クリックでやめる
                if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_Y) || IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) quitGame = true;
                else if (IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_N) || IsKeyPressed(KEY_BACKSPACE) || IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) quitAsk = false;
                keyWaiting = true;   // 確認の間は、ほかのメニューの操作をしない
            }
            bool nameEditing = nameEdit || keyWaiting;   // 文字やキーを入力している間は、メニューの操作に使わない
            if (!nameEditing && (IsKeyPressed(KEY_W) || IsKeyPressed(KEY_UP)))   menuSel = (menuSel + n - 1) % n;   // 名前の入力中は文字として使う
            if (!nameEditing && (IsKeyPressed(KEY_S) || IsKeyPressed(KEY_DOWN))) menuSel = (menuSel + 1) % n;
            if (!nameEditing && (IsKeyPressed(KEY_A) || IsKeyPressed(KEY_LEFT)))  change = -1;
            if (!nameEditing && (IsKeyPressed(KEY_D) || IsKeyPressed(KEY_RIGHT))) change = +1;
            if (!nameEditing && (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_SPACE))) activate = true;
            Vector2 mp = GetMousePosition();
            for (int i = 0; i < n && !keyWaiting; i++) {
                Rectangle rr = { (float)px + 20, (float)(rowY0 + i * rowH), (float)pw - 40, (float)rowH - 4 };
                if (CheckCollisionPointRec(mp, rr)) {
                    if (menuFrames > 10 && Vector2Length(GetMouseDelta()) > 0.0f) menuSel = i;   // 開いた直後のマウスの跳びは無視
                    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT))  { menuSel = i; activate = true; }
                    if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) { menuSel = i; change = -1; }
                }
            }
            if (IsKeyPressed(KEY_ESCAPE) && !nameEditing && menuPage == 5) {   // キー設定から設定へ戻る
                menuPage = 2; menuSel = 4; n = 0; activate = false; change = 0;
            } else if (IsKeyPressed(KEY_ESCAPE) && !nameEditing && !keyWaiting) {
                if (menuPage != 0) { int back = menuPage == 1 ? MI_PG_MATCH : menuPage == 3 ? MI_TOURNAMENT : menuPage == 4 ? MI_CREDITS : MI_PG_SETTINGS; menuPage = 0; menuSel = 0;   // 前のページへ
                    int m0[8], k0 = 0; m0[k0++] = MI_PLAY; if (app == APP_TITLE) m0[k0++] = MI_TUTORIAL; if (app == APP_TITLE && !DEMO_ONLY) m0[k0++] = MI_TOURNAMENT; if (app == APP_PAUSE) m0[k0++] = MI_PG_MATCH; m0[k0++] = MI_PG_SETTINGS; if (app == APP_TITLE) m0[k0++] = MI_CREDITS;
                    for (int q = 0; q < k0; q++) if (m0[q] == back) menuSel = q;
                    n = 0; activate = false; change = 0; }
                else if (app == APP_PAUSE) { activate = true; menuSel = 0; }   // Esc でそのまま戻る
            }
            if (nameEditing) { change = 0; activate = false; }
            int it = n > 0 ? items[menuSel] : MI_BACK;
            bool action = it == MI_PLAY || (it == MI_TOURNAMENT && menuPage == 0) || it == MI_SLAM_GO || it == MI_CREDITS || it == MI_RESTART || it == MI_TITLE || it == MI_QUIT || it == MI_PG_MATCH || it == MI_PG_SETTINGS || it == MI_BACK || it == MI_NAME || it == MI_KEYS || it == MI_KEYRESET || it == MI_TUTORIAL || it == MI_START || it >= MI_KEY0;
            if (activate && !action) { change = +1; activate = false; }
            if (change != 0 && n > 0) {
                switch (it) {
                    case MI_PLAY:   if (app != APP_TITLE || MOBILE_ONLY) break;   // タイトル：←→でモード（試合・バトル・練習）。Enter で選手と試合へ   // タイトル：←→でモード（試合・バトル・練習）。スマホ版はタイブレークの試合だけ
                    /* fall through */
                    case MI_MODE: {   // 並び：試合 → バトル → ラリー練習 → …（バトルは「試合＋体力」）
                        int idx = battle ? 1 : (mode == MODE_MATCH ? 0 : (int)mode + 1);
                        idx = (idx + MODE_COUNT + 1 + change) % (MODE_COUNT + 1);
                        battle = idx == 1; mode = idx <= 1 ? MODE_MATCH : (PracticeMode)(idx - 1);
                        forceRestart = true;
                    } break;
                    case MI_AI:     ai.level = (ai.level + 3 + change) % 3; forceRestart = true; break;
                    case MI_TOUR:   gWomen = !gWomen; ai.persona = TourPersonaAt(gWomen, 0); gYouPersona = 0; forceRestart = true; break;
                    case MI_PLAYER: {   // 自分が使う選手：癖なし（0）→ そのツアーの選手
                        int cnt = TourPersonaCount(gWomen) + 1;
                        int k = gYouPersona ? TourPersonaIndex(gWomen, gYouPersona) + 1 : 0;
                        k = (k + cnt + change) % cnt;
                        gYouPersona = k ? TourPersonaAt(gWomen, k - 1) : 0;
                        forceRestart = true;
                    } break;
                    case MI_HUD:    hudLevel = (hudLevel + 4 + change) % 4; break;
                    case MI_LOOK:   gLook = (gLook + 3 + change) % 3; break;
                    case MI_SLIDE:  gSlideOn = !gSlideOn; break;
                    case MI_BOUNCE: gBounceRit = !gBounceRit; break;
                    case MI_GUIDES: guideLevel = (guideLevel + 3 + change) % 3; break;
                    case MI_LANG:   gLang = gLang == LANG_JA ? LANG_EN : LANG_JA; break;
                    case MI_OPP: {   // 相手：ツアーの選手 →（最後の次）ランダム
                        int cnt = TourPersonaCount(gWomen), k = oppRandom ? cnt : TourPersonaIndex(gWomen, ai.persona);
                        k = (k + cnt + 1 + change) % (cnt + 1);
                        oppRandom = k == cnt; if (!oppRandom) ai.persona = TourPersonaAt(gWomen, k);
                        forceRestart = true;
                    } break;
                    case MI_SETS:   if (!DEMO_ONLY) { setsToWin = (setsToWin + 4 + change) % 4; forceRestart = true; } break;   // タイブレーク → 1 セット → 3 → 5。体験版は 1 セットだけ
                    case MI_STYLE:  style = (PlayStyle)((style + STYLE_COUNT + change) % STYLE_COUNT);
                                    if (app == APP_PAUSE && mode == MODE_MATCH && !score.over) matchAssisted = true;   // 試合の途中で難しさを変えた
                                    break;
                    case MI_COURT: {   // コート：ハード → クレー → 芝 → ランダム
                        int k = courtRandom ? SURF_COUNT : (int)gSurface;
                        k = (k + SURF_COUNT + 1 + change) % (SURF_COUNT + 1);
                        courtRandom = k == SURF_COUNT; if (!courtRandom) gSurface = (SurfaceType)k;
                    } break;
                    case MI_TIME:    gTimeSetting = (gTimeSetting + TOD_COUNT + 1 + change) % (TOD_COUNT + 1); AtmosNewMatch(); break;   // 後ろの画面にすぐ出る
                    case MI_WEATHER: gWeatherSetting = (gWeatherSetting + WX_COUNT + 1 + change) % (WX_COUNT + 1); AtmosNewMatch(); break;
                    case MI_ASSIST: assistLevel = (assistLevel + 3 + change) % 3; break;
                    case MI_FEED:   proFeed = !proFeed; break;
                    case MI_SENS:   aimSens = Clamp(aimSens + 0.25f * change, 0.25f, 3.0f); break;
                    case MI_AIM:    aimLanes = !aimLanes; break;
                    case MI_TOURNAMENT: slamSel = (slamSel + SLAM_COUNT + 1 + change) % (SLAM_COUNT + 1); break;
                    case MI_SOUND:  SfxSetVolume(roundf(Clamp(SfxGetVolume() + 0.1f * change, 0.0f, 1.0f) * 10.0f) / 10.0f); break;
                    default: break;
                }
                if (it != MI_TOURNAMENT) SaveSettings();
            }
            if (activate && n > 0) {
                if (it == MI_PLAY && app == APP_TITLE) {   // タイトルの「プレイ」：選手と試合を決めて、「試合を始める」で始める
                    menuPage = 1; menuSel = MOBILE_ONLY ? 8 : (mode == MODE_MATCH ? 10 : 9);   // 「試合を始める」を選んだ状態で開く
                } else if (it == MI_TUTORIAL) {   // はじめての人への練習：ラリー練習で動く・打つ・狙う、サーブ練習でサーブ
                    tut = 0; tutT = 0.0f; tutL = tutR = false; tutLastShot = -1;
                    mode = MODE_RALLY; battle = false; assistLevel = 2;
                    forceRestart = true; app = APP_PLAY; inputLockT = INPUT_LOCK;
                    mouseLocked = true; DisableCursor(); skipMouseDelta = true;
                } else if (it == MI_START) {   // 選手と試合：この設定で始める（タイトルからでも、試合の途中からでも新しく）
                    forceRestart = true; app = APP_PLAY; inputLockT = INPUT_LOCK;
                    if (!MOBILE_ONLY) { mouseLocked = true; DisableCursor(); skipMouseDelta = true; }
                } else if (it == MI_PLAY || it == MI_RESTART) {
                    if (app == APP_TITLE || it == MI_RESTART) forceRestart = true;
                    app = APP_PLAY; inputLockT = INPUT_LOCK;
                    mouseLocked = true; DisableCursor(); skipMouseDelta = true;
                } else if (it == MI_TOURNAMENT) {
                    menuPage = 3; menuSel = 8;   // グランドスラムの準備のページへ。「大会を始める」を選んだ状態で開く（すぐ始められるように）
                } else if (it == MI_SLAM_GO) {
                    // 大会を始める：今のツアー（男子・女子）、試合の長さで
                    SaveSettings();
                    if (slamSel == SLAM_COUNT) TourStartSeason(&tour, gWomen, setsToWin);
                    else TourStart(&tour, gWomen, slamSel, setsToWin);
                    app = APP_BRACKET; bracketWait = 0.4f;
                } else if (it == MI_NAME) { nameEdit = true; while (GetCharPressed() > 0) { } }
                else if (it == MI_KEYS) { menuPage = 5; menuSel = 0; keyCap = -1; }
                else if (it == MI_KEYRESET) { KeysReset(); SaveSettings(); snprintf(keyMsg, sizeof(keyMsg), "%s", TR("Keys reset to defaults", "はじめの割り当てに戻した")); keyMsgT = 2.5f; }
                else if (it >= MI_KEY0) { keyCap = it - MI_KEY0; while (GetKeyPressed() > 0) { } }   // 次に押されたキーを割り当てる
                else if (it == MI_BACK && menuPage == 5) { menuPage = 2; menuSel = 4; }
                else if (it == MI_PG_MATCH) { menuPage = 1; menuSel = 0; }
                else if (it == MI_PG_SETTINGS) { menuPage = 2; menuSel = 0; }
                else if (it == MI_CREDITS) { menuPage = 4; menuSel = 0; }
                else if (it == MI_BACK) {
                    int back = menuPage == 1 ? MI_PG_MATCH : menuPage == 3 ? MI_TOURNAMENT : menuPage == 4 ? MI_CREDITS : MI_PG_SETTINGS; menuPage = 0; menuSel = 0;
                    int m0[8], k0 = 0; m0[k0++] = MI_PLAY; if (app == APP_TITLE) m0[k0++] = MI_TUTORIAL; if (app == APP_TITLE && !DEMO_ONLY) m0[k0++] = MI_TOURNAMENT; if (app == APP_PAUSE) m0[k0++] = MI_PG_MATCH; m0[k0++] = MI_PG_SETTINGS; if (app == APP_TITLE) m0[k0++] = MI_CREDITS;
                    for (int q = 0; q < k0; q++) if (m0[q] == back) menuSel = q;
                } else if (it == MI_TITLE) {
                    tour.active = false;   // 大会の途中なら、やめる
                    tut = -1;              // 練習の途中なら、やめる
                    app = APP_TITLE; menuSel = 0;
                } else if (it == MI_QUIT) {
                    quitAsk = true;   // すぐには終わらず、確認する
                }
            }
            if (n == 0 || app == APP_PLAY || app == APP_BRACKET) { continue; }

            BeginDrawing();
            // ---- 背景 ----
            Color youShirtM = gYouPersona ? PersonaShirt(gYouPersona) : (Color){ 240, 240, 245, 255 };
            Vector2 youScr = { 0 }, oppScr = { 0 };
            if (titleMain || menuPage == 1 || menuPage == 3) {
                SurfaceType keepSurf = gSurface;
                if (menuPage == 3) gSurface = (SurfaceType)TourSlamSurface(slamSel == SLAM_COUNT ? 0 : slamSel);   // その大会のコートを見せる
                DrawShowcase(gYouPersona, menuPage == 3 ? -1 : ai.persona, youShirtM, (float)GetTime(), titleMain ? 1.55f : 1.35f, &youScr, &oppScr, titleMain ? 0.0f : 1.0f);   // カメラを左へずらして、2 人を画面の右に
                gSurface = keepSurf;
                // 左側を暗くして文字を読みやすく
                DrawRectangleGradientH(0, 0, W * 6 / 10, H, Fade(BLACK, 0.80f), Fade(BLACK, 0.0f));
            } else {
                ClearBackground((Color){ 30, 70, 60, 255 });
                for (int i = 0; i < 9; i++) DrawLine(W / 2 - 300 + i * 75, H, W / 2 - 120 + i * 30, 0, Fade(RAYWHITE, 0.06f));
            }
            // ---- 見出し ----
            if (titleMain) {
                DrawText("SWEET SPOT", px + 4, 90 + 4, 96, Fade(BLACK, 0.5f));
                DrawText("SWEET SPOT", px, 90, 96, (Color){ 255, 215, 70, 255 });
                DrawText(TR("real tennis where contact point and spin matter", "打点と回転がものをいう本物のテニス"), px + 4, 196, 22, RAYWHITE);
                if (DEMO_ONLY) {
                    int left = DEMO_MATCHES - DemoMatchesUsed(); if (left < 0) left = 0;
                    DrawText(left > 0 ? TextFormat(TR("DEMO  -  1-set matches: %d left. Practice is unlimited.", "体験版  -  1 セットの試合はあと %d 回。練習は何度でも"), left)
                                      : TR("DEMO  -  No matches left. Practice is unlimited. Get the full game for more!", "体験版  -  試合はここまで。練習は何度でも。続きは製品版で"),
                             px + 4, 232, 20, (Color){ 120, 220, 255, 255 });
                    DrawText(TR("CHALLENGE: beat AI NORMAL or PRO on REAL and get the full game free", "チャレンジ：AI「ふつう」か「プロ」に「リアル」で勝つと、製品版が無料"), px + 4, 258, 20, (Color){ 255, 215, 70, 255 });
                }
            } else {
                bool leftPanel = menuPage == 1 || menuPage == 3;
                if (!leftPanel) { DrawRectangle(px, py, pw, ph, Fade(BLACK, 0.75f)); DrawRectangleLines(px, py, pw, ph, Fade(RAYWHITE, 0.4f)); }
                else DrawRectangle(px, py, pw, ph, Fade(BLACK, 0.55f));
                const char *title = menuPage == 1 ? TR("PLAYERS & MATCH", "選手と試合") : menuPage == 2 ? TR("SETTINGS", "設定") : menuPage == 3 ? TR("GRAND SLAM", "グランドスラム")
                                  : menuPage == 4 ? TR("CREDITS", "クレジット") : menuPage == 5 ? TR("KEYS", "キー設定") : TR("PAUSED", "一時停止");
                int tx = leftPanel ? px + 24 : W / 2 - MeasureText(title, 46) / 2;
                DrawText(title, tx, py + 22, 46, YELLOW);
                const char *sub = menuPage == 0 ? TR("Esc: resume", "Esc：戻る") : TR("Esc: back", "Esc：戻る");
                if (menuPage == 5) sub = keyCap >= 0 ? TR("Press a key  (Esc: cancel)", "割り当てるキーを押す（Esc：やめる）") : TR("Enter / click: change key   Esc: back", "Enter / クリック：キーを変える   Esc：戻る");
                DrawText(sub, leftPanel ? px + 26 : W / 2 - MeasureText(sub, 18) / 2, py + 72, 18, LIGHTGRAY);
            }
            // ---- クレジット ----
            if (menuPage == 4) {
                static const char *LINES_JA[] = {
                    "#制作", "SWEET SPOT  -  URUSHI",
                    "#録音", "打球音・靴の音・滑る音・拍手・主審の「フォルト」：作者が試合会場で録音",
                    "選手の声・主審と線審のコール・雄叫び：作者の声",
                    "#効果音素材", "Gregor Quendel「Free Crowd Cheering Sounds」（CC BY 4.0）  -  会場のざわめき・大きな拍手・勝利の歓声",
                    "  https://www.gregorquendel.com  /  https://opengameart.org/content/free-crowd-cheering-sounds",
                    "Kenney「Impact Sounds」（CC0）  -  バウンド・ネット    https://kenney.nl",
                    "OpenGameArt（CC0）  -  ホールの拍手、竹の素振り（空振り）",
                    "ブーイング・「おお〜」・ラケットを叩きつける音：合成（作者のツールで作成）",
                    "#フォント・ライブラリ", "IPA フォント（IPAP ゴシック）  -  IPA フォントライセンス v1.0（fonts/ に同梱）",
                    "raylib  -  zlib/libpng ライセンス    https://www.raylib.com",
                    "#おことわり", "登場する選手・大会・トロフィーは実在のものを参考にした架空のもので、実在の人物・団体とは関係ありません。",
                };
                static const char *LINES_EN[] = {
                    "#MADE BY", "SWEET SPOT  -  URUSHI",
                    "#RECORDINGS", "Ball hits, footwork, slides, applause, umpire's \"fault\": recorded at tournaments by the author",
                    "Player voices, umpire and line calls, roar: the author's voice",
                    "#SOUND ASSETS", "Gregor Quendel - Free Crowd Cheering Sounds (CC BY 4.0) - crowd ambience, big applause, match win cheers",
                    "  https://www.gregorquendel.com  /  https://opengameart.org/content/free-crowd-cheering-sounds",
                    "Kenney - Impact Sounds (CC0) - bounces, net    https://kenney.nl",
                    "OpenGameArt (CC0) - hall applause, bamboo swish (whiff)",
                    "Boos, crowd \"ooh\", racket smash: synthesized with the author's tools",
                    "#FONT & LIBRARY", "IPA Font (IPAPGothic) - IPA Font License v1.0 (included in fonts/)",
                    "raylib - zlib/libpng license    https://www.raylib.com",
                    "#NOTE", "Players, tournaments and trophies are fictional, inspired by real ones, and not affiliated with any real person or organization.",
                };
                const char **L = gLang == LANG_JA ? LINES_JA : LINES_EN;
                int nl = (int)(sizeof(LINES_JA) / sizeof(LINES_JA[0]));
                int y = py + 100, x0 = px + 40, maxw = pw - 80;
                for (int i = 0; i < nl; i++) {
                    const char *ln = L[i];
                    if (ln[0] == '#') { y += 8; DrawText(ln + 1, x0, y, 20, YELLOW); y += 28; continue; }
                    int f2 = 18; while (f2 > 12 && MeasureText(ln, f2) > maxw) f2--;   // 枠に収まるように
                    DrawText(ln, x0 + 12, y, f2, ln[0] == ' ' ? LIGHTGRAY : RAYWHITE);
                    y += f2 + 8;
                }
            }
            // ---- 項目 ----
            for (int i = 0; i < n; i++) {
                int ry = rowY0 + i * rowH;
                bool sel = i == menuSel;
                if (sel) {
                    if (titleMain) { DrawRectangle(px - 10, ry, pw, rowH - 6, Fade(YELLOW, 0.20f)); DrawRectangle(px - 10, ry, 6, rowH - 6, YELLOW); }
                    else DrawRectangle(px + 20, ry, pw - 40, rowH - 4, Fade(YELLOW, 0.18f));
                }
                const char *label = "", *value = "";
                if (items[i] >= MI_KEY0) {   // キー設定の 1 行：操作の名前と、今のキー
                    int a = items[i] - MI_KEY0;
                    Color lc2 = sel ? YELLOW : RAYWHITE;
                    DrawText(ActionName(a), px + 40, ry + (rowH - 4 - fs) / 2, fs, lc2);
                    const char *v = keyCap == a ? (fmodf((float)GetTime(), 0.8f) < 0.5f ? TR("press a key...", "キーを押す…") : "") : TextFormat("[ %s ]", KeyName(gKeys[a]));
                    DrawText(v, px + pw - 40 - MeasureText(v, fs), ry + (rowH - 4 - fs) / 2, fs, keyCap == a ? ORANGE : lc2);
                    continue;
                }
                switch (items[i]) {
                    case MI_PLAY:    label = app == APP_TITLE ? TR("PLAY", "プレイ") : TR("RESUME", "続ける");
                                     if (app == APP_TITLE && !MOBILE_ONLY) value = (battle && mode == MODE_MATCH) ? TR("BATTLE", "バトル") : ModeName(mode);

                                     break;
                    case MI_TOURNAMENT: label = menuPage == 3 ? TR("Tournament", "大会") : TR("GRAND SLAM", "グランドスラム"); value = slamSel == SLAM_COUNT ? TR("SEASON: ALL 4", "シーズン：4 大会") : TourName(slamSel); break;
                    case MI_SLAM_GO:    label = TR("START TOURNAMENT", "大会を始める"); break;
                    case MI_CREDITS:    label = TR("CREDITS", "クレジット"); break;
                    case MI_PG_MATCH:    label = TR("PLAYERS & MATCH", "選手と試合"); break;
                    case MI_PG_SETTINGS: label = TR("SETTINGS", "設定"); break;
                    case MI_BACK:    label = TR("BACK", "戻る"); break;
                    case MI_NAME:    label = TR("Your name (Enter to edit)", "名前（Enter で入力）");
                                     value = nameEdit ? TextFormat("%s%s", gPlayerName, fmodf((float)GetTime(), 1.0f) < 0.5f ? "_" : " ") : gPlayerName; break;
                    case MI_MODE:    label = TR("Mode", "モード");            value = (battle && mode == MODE_MATCH) ? TR("BATTLE", "バトル") : ModeName(mode); break;
                    case MI_SETS:    label = TR("Match length", "試合の長さ");    value = setsToWin == 0 ? TR("TIEBREAK (first to 7)", "タイブレーク（7 点先取）") : setsToWin == 1 ? TR("1 SET", "1 セット") : (setsToWin == 2 ? TR("BEST OF 3", "3 セットマッチ") : TR("BEST OF 5", "5 セットマッチ")); break;
                    case MI_TOUR:    label = TR("Tour", "ツアー");            value = gWomen ? TR("WOMEN", "女子") : TR("MEN", "男子"); break;
                    case MI_PLAYER:  label = TR("Play as", "使う選手");         value = gYouPersona ? PERSONAS[gYouPersona].name : TextFormat(TR("%s (no traits)", "%s（癖なし）"), gPlayerName); break;
                    case MI_OPP:     label = TR("Opponent", "相手");        value = oppRandom ? TR("RANDOM", "ランダム") : PERSONAS[ai.persona].name; break;
                    case MI_HUD:     label = TR("Screen info (F1)", "画面の表示（F1）"); value = HUD_NAMES[hudLevel]; break;
                    case MI_GUIDES:  label = TR("Guides (F2)", "ガイド（F2）");     value = GUIDE_NAMES[guideLevel]; break;
                    case MI_LANG:    label = gLang == LANG_JA ? "Language / 言語" : "Language";  value = gLang == LANG_JA ? (UiJapaneseOk() ? "日本語" : "JAPANESE (font not found)") : "ENGLISH"; break;   // 英語表示は日本語の字を持たないフォントなので「言語」は出さない
                    case MI_AI:      label = TR("AI level", "AI の強さ");        value = LevelNameUI(ai.level); break;
                    case MI_STYLE:   label = TR("Style", "難しさ");           value = style == STYLE_CASUAL ? TR("CASUAL (easy)", "カジュアル（かんたん）") : TR("REAL", "リアル"); break;
                    case MI_COURT:   label = TR("Court", "コート");           value = courtRandom ? TR("RANDOM", "ランダム") : gSurface == SURF_CLAY ? TR("CLAY (slow, high)", "クレー（遅く、高く弾む）")
                                                                       : gSurface == SURF_GRASS ? TR("GRASS (fast, low)", "芝（速く、低く滑る）") : TR("HARD", "ハード"); break;
                    case MI_TIME:    { static const char *TN[2][TOD_COUNT + 1] = { { "AUTO", "DAY", "EVENING", "NIGHT" }, { "おまかせ", "昼", "夕方", "ナイター" } };
                                       label = TR("Time of day", "時間帯"); value = TN[gLang == LANG_JA][gTimeSetting]; } break;
                    case MI_WEATHER: { static const char *WN[2][WX_COUNT + 1] = { { "AUTO", "SUNNY", "CLOUDY" }, { "おまかせ", "晴れ", "くもり" } };
                                       label = TR("Weather", "天気"); value = WN[gLang == LANG_JA][gWeatherSetting]; } break;
                    case MI_ASSIST:  label = TR("Assist", "手がかり");          value = assistNames[assistLevel]; break;
                    case MI_FEED:    label = TR("Practice feed", "練習の球出し");   value = proFeed ? TR("PRO", "プロ") : TR("CLUB", "クラブ"); break;
                    case MI_SENS:    label = TR("Mouse sensitivity", "マウスの感度"); value = TextFormat("%.2f", aimSens); break;
                    case MI_SOUND:   label = !SfxDeviceOk() ? TR("Sound (no audio device)", "音（音の出る機器がない）")
                                           : SfxLoadedCount() == 0 ? TR("Sound (no files in sounds/)", "音（sounds/ にファイルがない）")
                                           : TextFormat(TR("Sound (%d files)", "音（%d ファイル）"), SfxLoadedCount());
                                     value = SfxGetVolume() > 0.0f ? TextFormat("%.0f%%", SfxGetVolume() * 100.0f) : TR("OFF", "なし"); break;
                    case MI_AIM:     label = TR("Aim", "狙い方");             value = aimLanes ? TR("LANES (5 spots)", "5 か所から選ぶ") : TR("CURVE (free, never wide)", "曲線（自由、サイドアウトしない）"); break;
                    case MI_KEYS:    label = TR("Keys", "キー設定"); value = TR("CHANGE", "変える"); break;
                    case MI_START:   label = mode == MODE_MATCH ? TR("START MATCH", "試合を始める") : TR("START PRACTICE", "練習を始める"); break;
                    case MI_BOUNCE:  label = TR("Bounce before serving", "サーブ前のボールつき"); value = gBounceRit ? TR("ON (press serve to skip)", "あり（サーブを押すと飛ばせる）") : TR("OFF", "なし"); break;
                    case MI_SLIDE:   label = TR("Slide into shots", "滑り込み"); value = gSlideOn ? TR("ON (slide when stopping while charging)", "あり（溜めながら止まると滑る）") : TR("OFF (always stop sharply)", "なし（いつもピタッと止まる）"); break;
                    case MI_TUTORIAL: label = TR("HOW TO PLAY", "はじめての人へ"); value = TR("4 short steps", "4 つの短い練習"); break;
                    case MI_LOOK:    label = TR("Look", "見た目"); value = gLook == 2 ? TR("PIXEL", "ドット風") : gLook ? TR("TOON (outlines)", "アニメ調（輪郭線）") : TR("STANDARD", "ふつう"); break;
                    case MI_KEYRESET: label = TR("Reset to defaults", "はじめの割り当てに戻す"); break;
                    case MI_RESTART: label = TR("RESTART", "やり直す"); break;
                    case MI_TITLE:   label = TR("BACK TO TITLE SCREEN", "タイトル画面に戻る"); break;
                    case MI_QUIT:    label = TR("QUIT GAME", "ゲームを終了する"); break;
                }
                Color lc = sel ? YELLOW : RAYWHITE;
                if (titleMain) {
                    DrawText(label, px + 8, ry + (rowH - 6 - fs) / 2, fs, lc);
                    if (value[0]) {   // 長い値（大会の名前など）は、項目の名前にかぶらないように字を小さく
                        const char *v = TextFormat("<  %s  >", value);
                        int vf = 22, room = pw - 20 - (8 + MeasureText(label, fs) + 24);
                        while (vf > 13 && MeasureText(v, vf) > room) vf--;
                        DrawText(v, px + pw - 20 - MeasureText(v, vf), ry + (rowH - 6 - vf) / 2, vf, lc);
                    }
                } else {
                    DrawText(label, px + 40, ry + (rowH - 4 - fs) / 2, fs, lc);
                    if (value[0]) {
                        const char *v = TextFormat("<  %s  >", value);
                        int vf = fs, room = pw - 40 - (40 + MeasureText(label, fs) + 24);
                        while (vf > 13 && MeasureText(v, vf) > room) vf--;
                        DrawText(v, px + pw - 40 - MeasureText(v, vf), ry + (rowH - 4 - vf) / 2, vf, lc);
                    }
                }
            }
            // ---- 選手と試合：右に 2 人の名前・身長・紹介 ----
            if (menuPage == 1 || menuPage == 3) {
                int prevRight = 0;
                for (int k = 0; k < (menuPage == 3 ? 1 : 2); k++) {   // 大会の準備では相手は組み合わせで決まるので、自分だけ
                    int pp = k == 0 ? gYouPersona : ai.persona;
                    Vector2 sp = k == 0 ? youScr : oppScr;
                    const char *nm = k == 0 ? (gYouPersona ? PERSONAS[pp].name : YouName()) : PERSONAS[pp].name;
                    const char *tag = k == 0 ? TR("PLAYER", "プレイヤー") : TR("OPPONENT", "相手");
                    int cx = (int)sp.x, cy = (int)sp.y - 70;
                    int bw = 230, bx = Clamp(cx - bw / 2, pw + 50, W - bw - 10);
                    if (bx < prevRight + 10) bx = prevRight + 10;   // 2 つの名札が重ならないように
                    prevRight = bx + bw;
                    DrawRectangle(bx, cy - 6, bw, 62, Fade(BLACK, 0.55f));
                    DrawText(tag, bx + 10, cy, 16, k == 0 ? SKYBLUE : ORANGE);
                    DrawText(nm, bx + 10, cy + 20, 22, RAYWHITE);
#ifndef SALE_BUILD   // 売る版は身長を出さない（実在の選手と結びつかないように）
                    DrawText(TextFormat("%.0f cm", PersonaHeight(pp)), bx + bw - 10 - MeasureText(TextFormat("%.0f cm", PersonaHeight(pp)), 16), cy, 16, LIGHTGRAY);
#endif
                }
                // 紹介文（下に 2 人分）
                int by = H - 150, bx0 = pw + 60, bw0 = W - bx0 - 20;
                {   // 能力のグラフ（六角形）：紹介文の枠の上に、2 人分（大会の準備では自分だけ）
                    int gy = by - 196, gh = 188, nk = menuPage == 3 ? 1 : 2;
                    DrawRectangle(bx0, gy, bw0, gh, Fade(BLACK, 0.55f));
                    for (int k = 0; k < nk; k++) {
                        float v[RADAR_N]; PersonaRadar(k == 0 ? gYouPersona : ai.persona, v);
                        float cx = bx0 + bw0 * (nk == 1 ? 0.5f : (k == 0 ? 0.25f : 0.75f));
                        DrawRadar(cx, gy + gh * 0.5f + 4, 60.0f, v, k == 0 ? SKYBLUE : ORANGE);
                    }
                }
                DrawRectangle(bx0, by, bw0, 130, Fade(BLACK, 0.60f));
                for (int k = 0; k < (menuPage == 3 ? 1 : 2); k++) {   // 枠に収まるように字を小さくする
                    const char *bl = k == 0 ? TextFormat(TR("%s: %s", "%s：%s"), gYouPersona ? PERSONAS[gYouPersona].shortName : YouName(), PersonaBlurb(gYouPersona))
                                            : TextFormat(TR("%s: %s", "%s：%s"), PERSONAS[ai.persona].shortName, PersonaBlurb(ai.persona));
                    char buf[256]; snprintf(buf, sizeof(buf), "%s", bl);
                    int bfs = 18; while (bfs > 14 && MeasureText(buf, bfs) > bw0 - 28) bfs--;
                    Color bc = k == 0 ? SKYBLUE : ORANGE;
                    if (MeasureText(buf, bfs) <= bw0 - 28) DrawText(buf, bx0 + 14, by + 14 + k * 36, bfs, bc);
                    else {   // 長い紹介文は 2 行に（字の切れ目で、枠に収まるところまで 1 行目に）
                        int cut = 0, len = (int)strlen(buf);
                        for (int i = 1; i <= len; i++) {
                            if (i < len && (buf[i] & 0xC0) == 0x80) continue;   // UTF-8 の字の途中では切らない
                            char t = buf[i]; buf[i] = 0; int w = MeasureText(buf, 14); buf[i] = t;
                            if (w > bw0 - 28) break;
                            cut = i;
                        }
                        char l1[256]; snprintf(l1, sizeof(l1), "%.*s", cut, buf);
                        DrawText(l1, bx0 + 14, by + 8 + k * 38, 14, bc);
                        DrawText(buf + cut, bx0 + 14, by + 25 + k * 38, 14, bc);
                    }
                }
                if (menuPage == 3) {
                    const char *ev = slamSel == SLAM_COUNT ? PN(TR("4 slams in a row: Australian (hard), French (clay), Wimbledon (grass), US (hard)", "4 大会を続けて：全豪（ハード）・全仏（クレー）・ウィンブルドン（芝）・全米（ハード）"),
                                                              TR("4 slams in a row: hard, clay, grass, hard", "4 大会を続けて：ハード・クレー・芝・ハード"))
                                                           : TextFormat(TR("%s  -  %s court  -  16 players, 4 rounds", "%s  -  %s コート  -  16 人、4 回勝てば優勝"), TourName(slamSel), SurfNameUI(TourSlamSurface(slamSel)));
                    { int ef = 18; while (ef > 12 && MeasureText(ev, ef) > bw0 - 28) ef--; DrawText(ev, bx0 + 14, by + 50, ef, RAYWHITE); }   // 長い大会名でも枠に収める
                    DrawText(TextFormat(TR("Opponents: %s  -  %s", "相手：%s  -  %s"), LevelNameUI(ai.level), setsToWin == 0 ? TR("tiebreak", "タイブレーク") : setsToWin == 1 ? TR("1 set", "1 セット") : setsToWin == 2 ? TR("best of 3", "3 セットマッチ") : TR("best of 5", "5 セットマッチ")), bx0 + 14, by + 90, 18, LIGHTGRAY);
                } else
                DrawText(TextFormat(TR("%s  -  %s court  -  %s", "%s  -  %s コート  -  %s"), LevelNameUI(ai.level), SurfNameUI(gSurface), ModeName(mode)), bx0 + 14, by + 90, 18, LIGHTGRAY);
            }
            // ---- 下の案内 ----
            const char *hint = MOBILE_ONLY ? TR("Tap: choose / change", "タップ：選ぶ・変える") : TR("W/S select   A/D change   Enter OK   Esc back   (mouse: L-click next, R-click back)", "W/S：選ぶ   A/D：変える   Enter：決定   Esc：戻る（マウス：左クリックで次、右クリックで前）");
            DrawText(hint, titleMain ? px : ((menuPage == 1 || menuPage == 3) ? px + 20 : px + 30), H - 34, 16, LIGHTGRAY);
            if (menuPage == 5 && keyMsgT > 0.0f && keyMsg[0]) {   // キー設定の知らせ（枠の下の方）
                int mw = MeasureText(keyMsg, 20);
                DrawText(keyMsg, W / 2 - mw / 2, py + ph - 32, 20, ORANGE);
            }
            if ((titleMain || menuPage == 0) && MOBILE_ONLY) {
                DrawText(TR("Tap to choose. In a match you run to the ball automatically: press a button to swing, slide your finger to aim.", "タップで選ぶ。試合では球の所へ自動で走る。ボタンを押して離すと打つ、押したまま指を動かすと狙い"),
                         titleMain ? px : px + 30, H - 58, 16, Fade(LIGHTGRAY, 0.85f));
            } else if (titleMain || menuPage == 0) {
                DrawText(TextFormat(TR("In game: %s move, mouse aim + click to hit (keys only: %s/%s/%s)   Change keys: SETTINGS > Keys", "試合中：%s 移動、マウスで狙ってクリックで打つ（キーだけなら %s/%s/%s）   キーは 設定 → キー設定 で変えられる"),
                                    ControlsMove(), KeyName(gKeys[ACT_TOP]), KeyName(gKeys[ACT_SLICE]), KeyName(gKeys[ACT_FLAT])),
                         titleMain ? px : px + 30, H - 58, 16, Fade(LIGHTGRAY, 0.8f));
            }
            if (quitAsk) {   // 終了の確認
                DrawRectangle(0, 0, W, H, Fade(BLACK, 0.6f));
                int bw = 560, bh = 170, bx = W / 2 - bw / 2, by = H / 2 - bh / 2;
                DrawRectangle(bx, by, bw, bh, (Color){ 20, 30, 40, 245 });
                DrawRectangleLines(bx, by, bw, bh, (Color){ 255, 215, 70, 255 });
                const char *q1 = TR("Quit the game?", "ゲームを終了しますか？");
                const char *q2 = TR("Enter / click: quit     Esc / right click: cancel", "Enter / クリック：終了する     Esc / 右クリック：やめる");
                const char *q3 = TR("(To go back to the title, choose BACK TO TITLE SCREEN)", "（タイトルに戻るなら「タイトル画面に戻る」を選ぶ）");
                DrawText(q1, W / 2 - MeasureText(q1, 30) / 2, by + 24, 30, RAYWHITE);
                DrawText(q2, W / 2 - MeasureText(q2, 20) / 2, by + 82, 20, (Color){ 255, 215, 70, 255 });
                if (app == APP_PAUSE) DrawText(q3, W / 2 - MeasureText(q3, 16) / 2, by + 124, 16, LIGHTGRAY);   // 試合中のメニューだけ
            }
            EndDrawing();
            continue;
        }

        camMode = 0;   // プレイ中は後ろからのカメラだけ（カメラの切り替えはリプレイのときに C）
        if (IsKeyPressed(KEY_F1)) { hudLevel = (hudLevel + 1) % 4; snprintf(toast, sizeof(toast), TR("SCREEN INFO: %s  (F1)", "画面の表示：%s（F1）"), HUD_NAMES[hudLevel]); toastT = 2.0f; SaveSettings(); }
        if (IsKeyPressed(KEY_F2)) { guideLevel = (guideLevel + 1) % 3; snprintf(toast, sizeof(toast), TR("GUIDES: %s  (F2)", "ガイド：%s（F2）"), GUIDE_NAMES[guideLevel]); toastT = 2.0f; SaveSettings(); }
        if (ActPressed(ACT_MUTE)) {   // M（変えられる）：消音 / 元に戻す
            if (SfxGetVolume() > 0.0f) { soundVolBeforeMute = SfxGetVolume(); SfxSetVolume(0.0f); snprintf(toast, sizeof(toast), "%s", TR("SOUND OFF  (M)", "音を消した（M）")); }
            else {
                SfxSetVolume(soundVolBeforeMute > 0.0f ? soundVolBeforeMute : 0.8f);
                if (!SfxDeviceOk())             snprintf(toast, sizeof(toast), "%s", TR("SOUND ON - but no audio device", "音を出す - でも音の出る機器がない"));
                else if (SfxLoadedCount() == 0) snprintf(toast, sizeof(toast), "%s", TR("SOUND ON - beep test. No files in sounds/", "音を出す - テストの音。sounds/ にファイルがない"));
                else                            snprintf(toast, sizeof(toast), TR("SOUND ON %.0f%% - %d files (M)", "音を出す %.0f%% - %d ファイル（M）"), SfxGetVolume() * 100.0f, SfxLoadedCount());
                SfxTest();   // 鳴るかどうか、その場でわかるように
            }
            toastT = 3.0f;
        }
        // 試合の途中は、1 キーでモード・強さ・難しさを変えない（G で試合が消える事故があった）。変えるのは Esc のメニューから
        bool matchLive = mode == MODE_MATCH && !score.over && tut < 0;
        #define MATCH_LOCKED_TOAST() do { snprintf(toast, sizeof(toast), "%s", TR("Can't change during a match (Esc > Players & match)", "試合中は変えられません（Esc → 選手と試合）")); toastT = 2.0f; } while (0)
        if (IsKeyPressed(KEY_T) && !DEMO_ONLY && !(SALE_ONLY && matchLive)) {   // 体験版はスローなし（チャレンジのため）。売る版は試合中もなし
            slowMo = !slowMo;
            if (slowMo && matchLive) matchAssisted = true;
        }
        if (DEMO_ONLY && mode == MODE_MATCH && !score.over && (style != STYLE_REAL || ai.level < 1)) demoClean = false;
        if (IsKeyPressed(KEY_V) && !matchLive) proFeed = !proFeed;   // 球出しの設定：試合では意味がない
        if (IsKeyPressed(KEY_F) && !matchLive) feedTimer = 0.0f;
        // ================= リプレイ（スロー、横からのカメラ） =================
        if (rp.on) {
            int n = 0; const char *cap = "REPLAY";
            const RFrame *fr = rp.mode == 0 ? RecClip(&n) : Highlight(rp.clip, &n, &cap);
            rp.t += frameDt * (rp.mode == 0 ? 0.5f : 0.7f);
            while (rp.idx < n - 1 && rp.acc + fr[rp.idx].dt <= rp.t) { rp.acc += fr[rp.idx].dt; rp.idx++; }
            bool skip = IsKeyPressed(KEY_ENTER) || ActPressed(ACT_REPLAY) || IsKeyPressed(KEY_ESCAPE) || IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
            if (rp.idx >= n - 1 || skip) {
                if (rp.mode == 1 && !IsKeyPressed(KEY_ESCAPE) && rp.clip + 1 < HighlightCount()) {   // 次のハイライトへ
                    rp.clip++; rp.idx = 0; rp.t = 0.0f; rp.acc = 0.0f; rp.first = true;
                } else rp.on = false;
            }
            if (IsKeyPressed(KEY_C)) { rp.view = (rp.view + 1) % REPLAY_VIEWS; rp.first = true; }   // C：リプレイのカメラを切り替える
            if (n > 0) {
                const RFrame *f = &fr[rp.idx < n ? rp.idx : n - 1];
                rp.cam = ReplayCamera(rp.view, f->ball, rp.cam, rp.first);
                rp.first = false;
                BeginDrawing();
                PixelBegin(); DrawSky(rp.cam);
                BeginMode3D(rp.cam);
                DrawReplayFrame(f, rp.cam);
                DrawReplayAfterimage(fr, rp.idx < n ? rp.idx : n - 1, rp.cam);   // ボールの残像
                EndMode3D();
                AtmosOverlay(rp.cam); PixelEnd();
                int W = GetScreenWidth(), H = GetScreenHeight();
                if (fmodf((float)GetTime(), 1.0f) < 0.7f) { DrawRectangle(20, 20, 150, 40, (Color){ 200, 30, 30, 230 }); DrawText(TR("REPLAY", "リプレイ"), 34, 28, 26, RAYWHITE); }
                if (rp.mode == 1) {
                    const char *hc = TextFormat(TR("HIGHLIGHTS  %d / %d", "ハイライト  %d / %d"), rp.clip + 1, HighlightCount());
                    DrawText(hc, 190, 30, 22, YELLOW);
                }
                int cw = MeasureText(cap, 30);
                DrawRectangle(W / 2 - cw / 2 - 20, H - 120, cw + 40, 50, Fade(BLACK, 0.65f));
                DrawText(cap, W / 2 - cw / 2, H - 110, 30, RAYWHITE);
                const char *vn[REPLAY_VIEWS] = { TR("SIDE (TV)", "横から（中継）"), TR("BEHIND PLAYER", "プレイヤーの後ろから"), TR("FOLLOW BALL", "ボールを追う"), TR("BEHIND OPPONENT", "相手の後ろから") };
                const char *hint = rp.mode == 1 ? TextFormat(TR("Enter: next   Esc: stop   C: camera (%s)", "Enter：次   Esc：やめる   C：カメラ（%s）"), vn[rp.view])
                                                : TextFormat(TR("Enter / R: back   C: camera (%s)", "Enter / R：戻る   C：カメラ（%s）"), vn[rp.view]);
                DrawText(hint, W / 2 - MeasureText(hint, 18) / 2, H - 50, 18, LIGHTGRAY);
                EndDrawing();
            }
            continue;
        }

        bool restart = forceRestart;
        forceRestart = false;
        if (IsKeyPressed(KEY_G)) {
            if (matchLive) MATCH_LOCKED_TOAST();
            else { mode = (PracticeMode)((mode + 1) % MODE_COUNT); restart = true; }
        }
        for (int k = 0; k < 3; k++) if ((mode == MODE_MATCH || mode == MODE_RETURN) && IsKeyPressed(KEY_ONE + k)) {   // リターン練習のサーブも AI の強さで変わる
            if (matchLive) MATCH_LOCKED_TOAST();
            else { ai.level = k; restart = true; }
        }
        if (mode == MODE_MATCH && score.over && IsKeyPressed(KEY_ENTER)) {
            if (tour.active) {   // 大会：結果を記録して組み合わせ表へ
                { int r0 = tour.round; TourRecordPlayerMatch(&tour, score.sets[0] > score.sets[1], &score); TourBeginReveal(&tour, r0); }   // 結果を 1 試合ずつ見せる
                app = APP_BRACKET; bracketWait = 0.4f; mouseLocked = false; EnableCursor();
                continue;
            }
            restart = true;
        }
        if (IsKeyPressed(KEY_B) && !DEMO_ONLY && !(SALE_ONLY && tour.active && matchLive)) {   // 体験版は自動操作なし。売る版の大会の試合中もなし
            autopilot = !autopilot;
            if (autopilot && matchLive) matchAssisted = true;
        }
        if (IsKeyPressed(KEY_TAB)) {
            // Tab：セッションの統計 → （試合中なら）試合の統計 → 閉じる
            if (!showStats && !showSummary) showStats = true;
            else if (showStats && mode == MODE_MATCH) { showStats = false; showSummary = true; }
            else { showStats = false; showSummary = false; }
        }
        if (IsKeyPressed(KEY_H)) assistLevel = (assistLevel + 2) % 3;
        if (IsKeyPressed(KEY_N)) {
            if (matchLive) MATCH_LOCKED_TOAST();
            else style = (PlayStyle)((style + 1) % STYLE_COUNT);
        }
        #undef MATCH_LOCKED_TOAST
        plog.assist = assistLevel;
        plog.style = style;
        bot.level = ai.level;
        if (restart && DEMO_ONLY) {
            demoCounted = false;
            if (mode == MODE_MATCH && DemoMatchesUsed() >= DEMO_MATCHES) {   // 体験版の試合はここまで。練習はできる
                mode = MODE_RALLY; battle = false; app = APP_TITLE; menuSel = 0;
                mouseLocked = false; EnableCursor();
            }
        }
        if (restart) {
            pointMsg = ""; scoreCall[0] = 0; serveMsg = ""; rageMsgT = 0.0f; toastT = 0.0f; hawkT = 10.0f; hawkPending = false;   // 前の試合のテロップを消す
            feedTimer = 0.5f;
            retScore = (Score){ .server = 1 }; retPhase = RET_WAIT;
            aiTossing = false; aiTossT = 0.0f; aiUnderarm = false;
            ball = (Ball){ 0 };
            P.chargingShot = -1; P.chargingServe = -1;
            P.sv = SV_READY; serveNo = 1; svTimer = 0.0f; serveMsg = "";
            P.pos = (Vector3){ 0.0f, 0.0f, HALF_LEN + 0.8f };
            P.vel = (Vector3){ 0 };
            P.hasLast = false;
            RecReset(); pointStart = 0; hlPending = false;
            MLogNewMatch(); mlPoint = 0; mlShot = 0;
            int lvl = ai.level, per = ai.persona;
            ai = (Opponent){ 0 };
            ai.level = lvl; ai.persona = per;
            ai.pos = AiHome();
            ai.anim.swingT = -1.0f;
            P.anim = (SwingAnim){ .swingT = -1.0f };
            P.downT = 0.0f; P.recoverT = 0.0f;
            P.sliding = false;
            if (!tour.active && oppRandom) ai.persona = TourPersonaAt(gWomen, GetRandomValue(0, TourPersonaCount(gWomen) - 1));   // 相手：ランダム
            if (!tour.active && courtRandom) gSurface = (SurfaceType)GetRandomValue(0, SURF_COUNT - 1);                          // コート：ランダム
            score = (Score){ 0 };
            score.server = rand() % 2;
            AtmosNewMatch();   // 時間帯と天気（おまかせなら試合ごとに変わる）
            gUmpSide = -1; changePending = changeActive = false;   // 審判台は自分から見て左から
            SfxSetLineJudges(false);   // アウトのコールはどのコートでも同じ声に（クレーだけ線審の叫び声だったのをそろえた）
            if (MOBILE_ONLY) setsToWin = 0;   // スマホ版はいつもタイブレーク
            score.setsToWin = setsToWin > 0 ? setsToWin : 1;
            if (setsToWin == 0) { score.tbOnly = true; score.tiebreak = true; }   // タイブレークだけの試合：最初からタイブレーク
            ai.tilt = 0.0f;
            winFx = -1.0f;
            youSmashes = 0; gYouRage = 0.0f;
            demoClean = true; demoCode[0] = 0; demoTried = false;
            matchAssisted = false;
            hp[0] = hp[1] = hpShown[0] = hpShown[1] = BATTLE_HP; koT = -1.0f;
            if (battle && mode == MODE_MATCH) demoClean = false;   // 体験版のチャレンジはふつうの試合だけ
            memset(mst, 0, sizeof(mst)); retPending = false;
            memset(ms, 0, sizeof(ms)); setBreak = false;
            needSetup = true;
            pointMsg = "";
            trailCount = 0;
        }

        // ---- 試合：ポイントの準備 ----
        int server = CurrentServer(&score);
        if (mode == MODE_MATCH && needSetup && !score.over) {
            needSetup = false;
            if (hlPending) { RecSaveHighlight(pointStart, hlCap, hlScore); hlPending = false; }   // 前のポイント（喜ぶところまで）
            mlPoint++; mlShot = 0; mlBefore = score; youMinDist = 99.0f;
            pointStart = RecTotal();
            ai.anim.smashT = 0.0f; ai.anim.broken = 0.0f;   // 新しいラケットに替える
            ai.anim.celebT = 0.0f;
            ai.staggerT = 0.0f; bot.staggerT = 0.0f; P.staggerT = 0.0f;   // ポイントの間に体勢は戻る
            netFlag[0] = netFlag[1] = false;
            // このポイントの大事さ（勝負強い選手が強くなる）
            {
                gPointImportance = PointImportance(&score);
                gDecidingSet = score.setsToWin > 1 && score.sets[0] == score.setsToWin - 1 && score.sets[1] == score.setsToWin - 1;   // 最終セット
            }
            bool deuce = DeuceCourt(&score);
            deuceSide = deuce;
            ball = (Ball){ 0 };
            trailCount = 0;
            ai.planned = false; ai.leaving = false; ai.swung = false; ai.watching = HITTER_NONE; ai.netRush = false;
            ai.vel = (Vector3){ 0 };
            P.vel = (Vector3){ 0 };
            bot.planned = false; bot.leaving = false; bot.swung = false; bot.watching = HITTER_NONE; bot.netRush = false;
            bot.vel = (Vector3){ 0 };
            bot.downTimer = 0.0f; bot.anim = (SwingAnim){ .swingT = -1.0f };
            ai.downTimer = 0.0f; ai.anim = (SwingAnim){ .swingT = -1.0f };
            P.anim = (SwingAnim){ .swingT = -1.0f };
            P.downT = 0.0f; P.recoverT = 0.0f;
            P.sliding = false;
            ai.sliding = false; bot.sliding = false;
            P.chargingShot = -1; P.chargingServe = -1;
            if (server == 0) {
                P.pos = (Vector3){ deuce ? 0.6f : -0.6f, 0.0f, HALF_LEN + 0.15f };
                ai.pos = Rot180(ReturnPosVs(deuce, serveNo == 2, P.pos.x));
                ai.standPos = ai.pos;
                P.sv = SV_READY;
                P.aimSlide = 0.0f;   // サーブの狙いはボックスの真ん中（ボディ）から
                if (gBounceRit && !autopilot && mode == MODE_MATCH) {   // 自分もサーブの前にボールをつく（設定でなしにできる。サーブのボタンを押すと飛ばせる）
                    int nb = serveNo == 2 ? GetRandomValue(1, 2) : GetRandomValue(2, 4);
                    P.anim.celebKind = RIT_BOUNCE; P.anim.ritBounces = nb;
                    P.anim.celebDur = RitualDuration(RIT_BOUNCE, nb); P.anim.celebT = P.anim.celebDur;
                }
            } else {
                P.pos = HumanReturnPos(deuce, serveNo == 2);
                ai.pos = Rot180((Vector3){ deuce ? 0.6f : -0.6f, 0.0f, HALF_LEN + 0.15f });
                ai.standPos = ai.pos;
                phaseTimer = AiServePrep(&ai, serveNo);   // サーブ前の儀式と、構えてからトスまでの間（リターン練習も同じ）
                aiTossing = false;
            }
            phase = PH_SERVE;
            serveJudged = false;
            bot.pos = Rot180(P.pos);
            bot.standPos = bot.pos;
            if (autopilot && server == 0) phaseTimer = 1.5f;
            if (quietPending && mode == MODE_MATCH) {   // サーブに入ろうとしても観客が静まらない：主審が注意して、静まるまで待つ
                quietPending = false; quietT = QUIET_TIME;
                phaseTimer = fmaxf(phaseTimer, QUIET_TIME + 0.3f);
            }
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
                SfxServe(bot.anim.lastContact.quality, bot.lastKmh, false, Vector3Distance(cam.position, ball.pos));
                ai.anim.splitT = SPLIT_TIME; ai.anim.celebT = 0.0f;
                MLogShot(mlPoint, ++mlShot, false, "SERVE", bot.lastKmh, bot.anim.lastContact.quality, false, false, false, false, P.pos, ball.pos.y, ai.pos, Vector3Length(ai.vel));
                rallyHits = 1;
                P.pos = Rot180(bot.pos);
                trailColor = RAYWHITE;
                trailCount = 0;
                phase = PH_PLAY;
                serveJudged = false;
                P.sv = SV_FLIGHT;
            }
            playerServing = false;
        }

        // ---- 入力（キーボード＋マウス）。ゲームパッドや 2 人目も、同じ PlayerInput の形で読む ----
        bool clickConsumed = false;
        bool webEsc = false;
#ifdef __EMSCRIPTEN__
        {   // ブラウザでは Esc はブラウザがマウスの固定を外すのに使い、ゲームには届かない。固定が外れたら Esc と同じに扱う
            static bool hadLock = false;
            bool lockedNow = EM_ASM_INT({ return document.pointerLockElement ? 1 : 0; }) != 0;
            if (!mouseLocked) hadLock = false;
            else if (lockedNow) hadLock = true;
            else if (hadLock) { webEsc = true; hadLock = false; }
        }
#endif
        if (IsKeyPressed(KEY_ESCAPE) || webEsc) {   // Esc：一時停止して設定メニュー
            app = APP_PAUSE; menuSel = 0;
            mouseLocked = false; EnableCursor();
            P.chargingShot = -1; P.chargingServe = -1;
        }
        if (!MOBILE_ONLY && !mouseLocked && (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) || IsMouseButtonPressed(MOUSE_BUTTON_RIGHT))) {
            mouseLocked = true; DisableCursor(); clickConsumed = true;   // クリックで戻す（このクリックでは打たない）
        }
        PlayerInput in;
        ReadKeyboardMouse(&in, !MOBILE_ONLY && mouseLocked && !clickConsumed, skipMouseDelta, aimSens, frameDt);
        if (MOBILE_ONLY) {
            ReadTouch(&in, aimSens);   // 右手のボタン・狙い、左手のスティック
            if (TouchPauseTapped()) { app = APP_PAUSE; menuSel = 0; P.chargingShot = -1; P.chargingServe = -1; }
            // 自動で走る：スティックを使っていなければ、来る球を打つ場所へ（球がなければ真ん中の後ろへ戻る）
            if (!botControls && Vector2Length(in.move) < 0.01f && P.downT <= 0.0f && (phase == PH_PLAY || mode != MODE_MATCH)) {
                static AssistPlan mp; static float mpAge = 1e9f;
                Vector3 tgt = { 0.0f, 0.0f, HALF_LEN + 0.6f };
                bool incoming = ball.active && ball.lastHitter == HITTER_OPP && ball.pos.z < HALF_LEN + 3.0f;
                if (incoming) {
                    if (ball.age < mpAge || !mp.valid) mp = PlanAssist(P.pos, P.vel, &ball, SHOT_TOP);   // 新しい球なら作り直す
                    mpAge = ball.age;
                    if (mp.valid && mp.standValid) tgt = mp.stand;
                } else { mp.valid = false; mpAge = 1e9f; tgt.x = Clamp(P.pos.x * 0.3f, -1.0f, 1.0f); }
                Vector3 d = { tgt.x - P.pos.x, 0.0f, tgt.z - P.pos.z };
                float l = Vector3Length(d);
                if (l > 0.12f) { float k = fminf(1.0f, l / 0.6f) / l; in.move = (Vector2){ d.x * k, -d.z * k }; }
            }
        }
        skipMouseDelta = false;   // メニューから戻った直後のマウスの跳びは無視した

        if (inputLockT > 0.0f) {   // メニューで押したボタン・クリックで、サーブや打球が始まらないように
            inputLockT -= frameDt;
            in.shot = -1; in.serve = -1;
            for (int q = 0; q < SRC_COUNT; q++) in.released[q] = false;
        }
        if (phase == PH_SERVE && P.anim.celebKind == RIT_BOUNCE && P.anim.celebT > 0.0f) P.anim.celebT = fmaxf(P.anim.celebT - simDt, 0.0f);   // 自分のボールつき
        // ---- 移動。加速・減速あり ----
        Vector3 move = { in.move.x, 0.0f, -in.move.y };   // 自分は -z を向いているので、前は -z
        if (botControls) {
            // オートパイロット：AI と同じ関数で走る
            if (phase == PH_PLAY || phase == PH_POINT_OVER) {
                Ball fb = FlipSides(ball);
                AiUpdate(&bot, &fb, simDt);
                P.pos = Rot180(bot.pos);
                P.vel = Rot180(bot.vel);
            }
        } else if (playerServing) {
            // サーブ前はベースラインに沿って左右だけ動ける（センターマークを越えない）
            P.vel = (Vector3){ 0 };
            if (P.sv == SV_READY && move.x != 0.0f) {
                P.pos.x += move.x * 2.0f * frameDt;
                P.pos.x = deuceSide ? Clamp(P.pos.x, 0.2f, SINGLES_W * 0.5f) : Clamp(P.pos.x, -SINGLES_W * 0.5f, -0.2f);
            }
            P.pos.z = HALF_LEN + 0.15f;
            // 相手（AI）はこちらの立ち位置に合わせてレシーブの位置を変える（横へ寄ってワイドを狙うと、外へ動いて待つ）
            if (mode == MODE_MATCH && phase == PH_SERVE && P.sv == SV_READY) {
                Vector3 want = Rot180(ReturnPosVs(deuceSide, serveNo == 2, P.pos.x));
                Vector3 d = Vector3Subtract(want, ai.pos);
                float dl = Vector3Length(d), step = 3.0f * frameDt;
                if (dl > 0.01f) { Vector3 mv = Vector3Scale(d, fminf(step, dl) / dl); ai.pos = Vector3Add(ai.pos, mv); ai.vel = Vector3Scale(mv, 1.0f / fmaxf(frameDt, 1e-4f)); }
                else ai.vel = (Vector3){ 0 };
                ai.standPos = ai.pos;
            } else if (mode == MODE_MATCH && phase == PH_SERVE) ai.vel = (Vector3){ 0 };   // トスを上げたら止まって構える
        } else if (P.downT > 0.0f) {
            // 飛びついて倒れている：起き上がるまで動けない
            P.downT -= simDt;
            P.vel = (Vector3){ 0 };
        } else {
            // 溜めている間は少し遅くなる（構えながら動く）
            float spd = PlayerRunSpeed() * (P.chargingShot >= 0 ? CHARGE_MOVE : 1.0f) * (in.crouch ? CROUCH_SPEED : 1.0f)
                      * fminf(P.recoverT > 0.0f ? STRETCH_MOVE : 1.0f, StaggerSpeedMul(P.staggerT, P.staggerTotal));   // 体勢が崩れている間は遅い
            Vector3 want = (Vector3Length(move) > 0.0f) ? Vector3Scale(Vector3Normalize(move), spd) : (Vector3){ 0 };
            // 足の自動調整：溜めている間、球がこの先 0.45 秒で一番近くを通る点を見て、その点を打ちやすい位置（体の横 0.94m）へ小刻みに寄せる。
            //   自分のタイミング（上がり際で打つなど）に合わせて動く
            if (ball.active && ball.lastHitter == HITTER_OPP && P.chargingShot >= 0 && mode != MODE_SERVE) {
                Ball fb = ball;
                float bestD = 1e9f; Vector3 bestB = { 0 }; bool found = false;
                for (int i = 0; i < 108 && fb.active && fb.bounces < 2; i++) {   // 0.45 秒先まで
                    StepBall(&fb, PHYS_DT);
                    bool hittable = fb.pos.z > 0.0f && !fb.hitNet && (fb.bounces == 1 || (fb.bounces == 0 && P.pos.z < SERVICE_Z + 1.0f)) &&
                                    fb.pos.y > 0.25f && fb.pos.y < OVERHEAD_MIN_H && (!fb.isServe || fb.bounces == 1);
                    if (!hittable) continue;
                    float dx = fb.pos.x - P.pos.x, dz = fb.pos.z - P.pos.z, d = sqrtf(dx * dx + dz * dz);
                    if (d < bestD) { bestD = d; bestB = fb.pos; found = true; }
                }
                if (found && bestD < IDEAL_R + FOOT_ASSIST_R) {
                    // フォアかバックか：球が体の右を通るならフォア（体の左に立つ）
                    float side = bestB.x >= P.pos.x ? -1.0f : 1.0f;
                    Vector3 stand = { bestB.x + side * IDEAL_SIDE, 0.0f, bestB.z + IDEAL_FWD };
                    Vector3 to = { stand.x - P.pos.x, 0.0f, stand.z - P.pos.z };
                    float d = Vector3Length(to);
                    if (d > 0.04f && d < FOOT_ASSIST_R) {
                        want = Vector3Add(want, Vector3Scale(to, fminf(d * 6.0f, FOOT_ASSIST_SPEED) / d));
                        float wl = Vector3Length(want);
                        if (wl > PlayerRunSpeed()) want = Vector3Scale(want, PlayerRunSpeed() / wl);
                    }
                }
            }
            float v = Vector3Length(P.vel);
            // 溜めながら速く走っていて、減速・切り返しをしたら滑り込む
            if (gSlideOn && !P.sliding && P.chargingShot >= 0 && v > SLIDE_MIN_SPEED &&   // 設定で「滑り込み：あり」のときだけ
                Vector3DotProduct(want, Vector3Scale(P.vel, 1.0f / v)) < v * 0.5f)
            { P.sliding = true; SfxSlide(Vector3Distance(cam.position, P.pos)); }
            if (P.sliding && Vector3Length(want) > 0.5f && Vector3DotProduct(want, P.vel) < -0.3f * v * Vector3Length(want)) P.sliding = false;   // 反対向きに入れたらすぐ止まる
            if (P.sliding) {
                float nv = v - SLIDE_DECEL * 1.6f * simDt;   // 滑る距離は短め（止まれない感じがしないように）
                if (nv < SLIDE_END_SPEED) { P.sliding = false; P.vel = Vector3Scale(P.vel, fmaxf(nv, 0.0f) / fmaxf(v, 1e-4f)); }
                else P.vel = Vector3Scale(P.vel, nv / v);
            } else {
                P.vel = Accelerate(P.vel, want, PLAYER_ACCEL, PLAYER_BRAKE, simDt);
            }
            P.pos = Vector3Add(P.pos, Vector3Scale(P.vel, simDt));
            P.pos.x = Clamp(P.pos.x, -DOUBLES_W * 0.5f - 3.0f, DOUBLES_W * 0.5f + 3.0f);
            P.pos.z = Clamp(P.pos.z, 0.5f, HALF_LEN + 5.0f);
        }

        // 見た目のタイマー
        if (P.anim.swingT >= 0.0f) P.anim.swingT = fminf(P.anim.swingT + simDt, 5.0f);
        if (P.anim.lungeT > 0.0f) P.anim.lungeT -= simDt;
        if (P.recoverT > 0.0f) P.recoverT -= simDt;
        if (P.staggerT > 0.0f) P.staggerT -= simDt;
        P.anim.staggerK = P.staggerTotal > 0.0f ? fmaxf(P.staggerT, 0.0f) / P.staggerTotal * stagS : 0.0f;
        {   // しゃがみの見た目（なめらかに）
            float ct = (in.crouch && !botControls) ? 1.0f : 0.0f;
            P.anim.crouch += (ct - P.anim.crouch) * fminf(simDt * 12.0f, 1.0f);
        }
        P.anim.downT = P.downT;
        P.anim.sliding = P.sliding;
        if (Vector3Length(P.vel) > 0.3f) P.anim.slideSide = P.vel.x;   // 自分は -z 向きなので右＝+x
        P.anim.runPhase += Vector3Length(P.vel) * simDt * 2.2f;
        P.anim.velX = P.vel.x; P.anim.velZ = -P.vel.z;   // 自分は -z 向き
        P.anim.atNet = P.pos.z < VOICE_NET_Z;              // サービスラインの 1.5m 後ろより前ならボレーの構え

        // ---- 狙い（ずっと残る）。自分は -z を向いているので、深く＝ -z ----
        // 左右：決まった位置（ラリーは 5 か所、サーブは 3 か所）から選ぶ。マウスを振った量で隣へ移る。
        //   振りすぎても一番外（ライン際の内側）で止まるので、外には出ない。打つたびに真ん中に戻る
        {
            int half = playerServing ? 1 : 2;                         // 真ん中から片側にいくつあるか
            float lim = aimLanes ? (half + 0.45f) * AIM_LANE_STEP : 4.0f * AIM_CURVE_K;
            P.aimSlide = Clamp(P.aimSlide + in.aim.x, -lim, lim);
            int lane = (int)roundf(Clamp(P.aimSlide / AIM_LANE_STEP, (float)-half, (float)half));
            if (!aimLanes) {
                // 曲線：少し振れば少し、大きく振るほどライン際に近づくが、ライン際（の内側）より外には出ない
                float wide = style == STYLE_CASUAL ? AIM_LANE_WIDE_CASUAL : AIM_LANE_WIDE;
                if (playerServing) {
                    float x0, x1;
                    ServiceBox(P.pos.x, -1, &x0, &x1);
                    float mid = 0.5f * (x0 + x1), hw = 0.5f * (x1 - x0) - 0.30f;
                    P.aimX = mid + hw * tanhf(P.aimSlide / (AIM_CURVE_K * 0.6f));
                } else {
                    P.aimX = wide * tanhf(P.aimSlide / AIM_CURVE_K);
                }
            } else if (playerServing) {
                float x0, x1;
                ServiceBox(P.pos.x, -1, &x0, &x1);
                float xs[3] = { x0 + 0.30f, 0.5f * (x0 + x1), x1 - 0.30f };   // ボックスの左端・真ん中・右端（ワイド／ボディ／T）
                P.aimX = xs[lane + 1];
            } else {
                float wide = style == STYLE_CASUAL ? AIM_LANE_WIDE_CASUAL : AIM_LANE_WIDE;
                float xs[5] = { -wide, -AIM_LANE_MID, 0.0f, AIM_LANE_MID, wide };
                P.aimX = xs[lane + 2];
            }
        }
        P.aimDepth -= in.aim.y;
        P.aimDepth = Clamp(P.aimDepth, -(HALF_LEN - 0.3f), -1.2f);

        // ---- サーブ（自分）----
        Vector3 tossIdeal = { P.pos.x + (gLefty ? -0.25f : 0.25f), SERVE_IDEAL_H * HumanServeHeightK(), P.pos.z - 0.35f };   // 理想の打点（これ以上の高さ）。背が高いほど高い。左利きは左
        float handS = gLefty ? -1.0f : 1.0f;   // 利き手の側（右利き +1）
        if (playerServing) {
            if (P.sv == SV_READY) {
                // サーブの狙いはサービスボックスの中
                {
                    float x0, x1;
                    ServiceBox(P.pos.x, -1, &x0, &x1);
                    P.aimX = Clamp(P.aimX, x0 + 0.2f, x1 - 0.2f);
                    P.aimDepth = Clamp(P.aimDepth, -SERVICE_Z + 0.2f, -3.0f);
                }
                for (int s = 0; s < SERVE_COUNT; s++) {
                    if (in.serve != s || quietT > 0.0f) continue;
                    if (P.anim.celebKind == RIT_BOUNCE) P.anim.celebT = 0.0f;   // ボールつきの途中でも、押したらトスへ   // 主審が観客を静かにさせている間はトスを上げない
                    P.chargeSrc = in.source;
                    P.chargingServe = s;
                    P.charge = 0.0f;
                    P.serveT = 0.0f;
                    SfxStopApplause();
                    playerGrunt = false; playerGruntTried = false;
                    P.tossOffset = (Vector2){ RandNormal() * TOSS_SPREAD, RandNormal() * TOSS_SPREAD };
                    ball = (Ball){ 0 };
                    ball.pos = (Vector3){ tossIdeal.x + P.tossOffset.x, 1.9f * HumanServeHeightK(), tossIdeal.z + P.tossOffset.y };
                    ball.vel = (Vector3){ 0.0f, TOSS_SPEED, 0.0f };
                    ball.active = true;
                    ball.netClearance = -99.0f;
                    trailCount = 0;
                    P.sv = SV_TOSS;
                    serveMsg = "";
                    break;
                }
            } else if (P.sv == SV_TOSS) {
                P.charge = fminf(P.charge + simDt / SERVE_CHARGE_TIME, 1.0f);   // トスと同じ時間の進みで溜める（スローでも同じ）
                P.serveT += simDt;
                if (!playerGruntTried && P.serveT >= 0.50f - SfxServeLead(gYouPersona ? gYouPersona : -1)) {
                    // 選手を使っているときは、その選手の声を出す割合で（癖なしは今までどおり毎回）
                    if (!gYouPersona || RandRange(0.0f, 1.0f) < SfxServeChance(gYouPersona)) playerGrunt = SfxServePreBy(gYouPersona ? gYouPersona : -1, Vector3Distance(cam.position, P.pos));
                    playerGruntTried = true;
                }
                P.anim.windup = 0.3f + 0.7f * P.charge;
                P.anim.windStroke = 2;
                bool serveReleased = in.released[P.chargeSrc];
                if (serveReleased) {
                    P.anim.windup = 0.0f;
                    ServeType type = (ServeType)P.chargingServe;
                    P.chargingServe = -1;
                    // 直前のフレームの間で、理想の打点の高さに一番近かった瞬間を使う（高すぎる球は打てない）
                    int best = -1;
                    for (int h = 0; h < historyCount; h++) {
                        const Ball *hb = &history[h];
                        // 届く高さも背の高さで変わる（トスも背が高いほど高い。同じ倍率にしないと、背の高い選手はトスの一番上が届かずに空振りになっていた）
                        if (!hb->active || hb->bounces > 0 || hb->pos.y > SERVE_REACH * HumanServeHeightK() + SERVE_OVERREACH) continue;
                        if (best < 0 || hb->pos.y > history[best].pos.y) best = h;   // 届く範囲で一番高いところ
                    }
                    P.lastServeType = type;
                    if (mode == MODE_SERVE) svStats[serveNo - 1][0]++;
                    if (best < 0) {
                        serveMsg = TR("WHIFF (ball above reach)", "空振り（届かない高さ）");   // 空振りはフォルト
                        SfxWhiff();
                        P.lastServeKmh = 0.0f;
                        plog.serves++; plog.serveWhiffs++;
                        if (plog.f) {
                            fprintf(plog.f, "%.2f,%s,%s,serve,%s,%.2f,serve,0,0,high,,,,,,,%.2f,,,,,,%.2f,%.2f,,,,,,,,WHIFF,%d,,,%d,%s,,%s\n",
                                    GetTime(), MODE_NAMES[mode], AI_LEVELS[ai.level].name, SERVES[type].name, P.charge,
                                    ball.pos.y, P.pos.x, P.pos.z, plog.assist, plog.style, SURFACES[gSurface].name, LogStamp());
                            fflush(plog.f);
                        }
                        // フォルトとして判定させる
                        ball = (Ball){ 0 };
                        ball.isServe = true; ball.bounces = 1; ball.result = RESULT_OUT;
                        ball.lastHitter = HITTER_PLAYER;
                        ball.netClearance = -99.0f;
                        P.sv = SV_FLIGHT;
                        if (mode == MODE_MATCH) { phase = PH_PLAY; serveJudged = false; }
                    } else {
                        Ball hb = history[best];
                        float qH = Clamp01(1.0f - fmaxf(tossIdeal.y - hb.pos.y, 0.0f) / SERVE_TOL_H);   // 高い分には減点しない
                        float qT = Clamp01(1.0f - Vector2Length(P.tossOffset) / TOSS_TOL);
                        float q = qH * qT;
                        // コース：左右はマウスの印（サービスボックスの中）、深さはトスから離すまでの時間
                        float x0, x1;
                        ServiceBox(P.pos.x, -1, &x0, &x1);
                        Vector3 target = { Clamp(P.aimX, x0 + 0.2f, x1 - 0.2f), 0.0f, ServeDepthFromTime(P.serveT) };
                        P.lastServeH = hb.pos.y;
                        P.lastServeQ = q;
                        P.lastContactPos = hb.pos;
                        P.anim.lastContact = (Contact){ .overhead = true, .forehand = true, .quality = q };
                        P.anim.smash = false;
                        P.anim.contactLocal = (Vector3){ hb.pos.x - P.pos.x, hb.pos.y, P.pos.z - hb.pos.z };
                        P.anim.swingT = 0.0f;
                        gCasual = (style == STYLE_CASUAL);
                        ball = HitServe(hb.pos, target, type, P.charge, q, x0, x1, &P.lastServeKmh);
                        speedBoardKmh = (int)roundf(P.lastServeKmh);
                        SfxServe(q, P.lastServeKmh, playerGrunt, Vector3Distance(cam.position, ball.pos));
                        ai.anim.splitT = SPLIT_TIME; ai.anim.celebT = 0.0f;
                        if (mode == MODE_MATCH) MLogShot(mlPoint, ++mlShot, false, "SERVE", P.lastServeKmh, q, false, false, false, false, P.pos, hb.pos.y, ai.pos, Vector3Length(ai.vel));
                        playerGrunt = false;
                        rallyHits = 1;
                        P.lastAimTarget = target;
                        P.aimX = 0.0f; P.aimSlide = 0.0f;   // 打つたびに狙いを真ん中に戻す
                        gCasual = false; gCrouch = false;
                        ball.lastHitter = HITTER_PLAYER;
                        ball.shotId = ++plog.shotSerial;
                        plog.serves++;
                        LogFlush(&plog, "?");
                        snprintf(plog.row, sizeof(plog.row),
                                 "%.2f,%s,%s,serve,%s,%.2f,serve,0,0,,%.3f,%.3f,%.3f,,,,%.2f,,,,%.0f,,%.2f,%.2f,,%.2f,%.2f,%.2f,,,",
                                 GetTime(), MODE_NAMES[mode], AI_LEVELS[ai.level].name, SERVES[type].name, P.charge,
                                 q, qH, qT, hb.pos.y, P.lastServeKmh, P.pos.x, P.pos.z, hb.pos.x, hb.pos.y, hb.pos.z);
                        plog.pending = true; plog.pendingId = ball.shotId; plog.pendingServe = true;
                        plog.pendingLaunch = atan2f(ball.vel.y, sqrtf(ball.vel.x * ball.vel.x + ball.vel.z * ball.vel.z)) * RAD2DEG;
                        plog.pendingApex = ball.pos.y;
                        trailColor = SERVES[type].color;
                        if (q >= SWEET_Q + 0.08f) { sweetT = 0.0f; sweetPos = hb.pos; sweetQ = q; trailColor = (Color){ 255, 210, 60, 255 }; }   // いい当たりのサーブ
                        trailCount = 0;
                        serveAce = false;
                        P.sv = SV_FLIGHT;
                        if (mode == MODE_MATCH) { phase = PH_PLAY; serveJudged = false; }
                    }
                } else if (ball.pos.y < 1.2f && ball.vel.y < 0.0f) {
                    // 打たずに落とした：トスのやり直し（ペナルティなし）
                    P.chargingServe = -1;
                    ball = (Ball){ 0 };
                    serveMsg = TR("Let the toss drop - toss again", "トスを見送った - もう一度");
                    P.sv = SV_READY;
                }
            }
        }
        // サーブ練習：結果の集計と次のサーブ
        if (mode == MODE_SERVE) {
            if (P.sv == SV_FLIGHT && ball.result != RESULT_NONE) {
                bool in = ball.result == RESULT_IN;
                if (in) {
                    svStats[serveNo - 1][1]++;
                    serveMsg = "IN";
                } else if (serveNo == 1) {
                    serveMsg = (ball.result == RESULT_NET) ? TR("FAULT (net)", "フォルト（ネット）") : (serveMsg[0] ? serveMsg : TR("FAULT (out)", "フォルト（アウト）"));
                } else {
                    serveMsg = (ball.result == RESULT_NET) ? TR("DOUBLE FAULT (net)", "ダブルフォールト（ネット）") : TR("DOUBLE FAULT", "ダブルフォールト");
                    doubleFaults++;
                }
                if (!in && serveNo == 1) serveNo = 2;
                else { serveNo = 1; deuceSide = !deuceSide; }
                svTimer = 2.0f;
                P.sv = SV_WAIT;
            } else if (P.sv == SV_WAIT) {
                svTimer -= frameDt;
                if (svTimer <= 0.0f) {
                    ball = (Ball){ 0 };
                    P.pos = (Vector3){ deuceSide ? 0.6f : -0.6f, 0.0f, HALF_LEN + 0.15f };
                    P.sv = SV_READY;
                }
            }
        }

        // ---- AI のサーブ（試合と、リターン練習。同じ処理：間・トス・声・球速・回転・コース・フォルト） ----
        bool retServing = mode == MODE_RETURN && retPhase == RET_SERVE;
        if ((mode == MODE_MATCH && phase == PH_SERVE && server == 1 && !score.over) || retServing) {
            bool svDeuce = DeuceCourt(retServing ? &retScore : &score);
            // サーブのモーション：構え → トスを上げる（ラケットを担ぐ）→ 一番上を少し過ぎたところで打つ
            if (!aiTossing) {
                phaseTimer -= simDt;
                aiUnderarm = false;
                if (phaseTimer <= 0.0f && serveNo == 1 && P.pos.z > HALF_LEN + 1.2f &&
                    RandRange(0.0f, 1.0f) < MENTAL[ai.persona % PERSONA_COUNT].underarm) {
                    // アンダーサーブ（キルギオス）：相手がベースラインのずっと後ろにいるときに、トスを上げずに下から
                    aiUnderarm = true; aiTossing = true; aiTossT = AI_TOSS_HIT_T;
                    ai.anim.celebT = 0.0f;
                    snprintf(toast, sizeof(toast), "%s", TL("UNDERARM SERVE!", "アンダーサーブ！")); toastT = 2.0f;
                } else if (phaseTimer <= 0.0f) {
                    aiTossing = true; aiTossT = 0.0f;
                    ai.anim.celebT = 0.0f;
                    SfxStopApplause();
                    bool dc = svDeuce;
                    Vector3 me = { dc ? 0.6f : -0.6f, 0.0f, HALF_LEN + 0.15f };
                    ball = (Ball){ 0 };
                    ball.pos = Rot180((Vector3){ me.x + (PERSONAS[ai.persona].lefty ? -0.25f : 0.25f), 1.9f, me.z - 0.35f });
                    ball.vel = (Vector3){ 0.0f, TOSS_SPEED, 0.0f };
                    ball.active = true;
                    ball.netClearance = -99.0f;
                    trailCount = 0;
                }
            } else {
                aiTossT += simDt;
                float lead = SfxServeLead(ai.persona);
                if (aiTossT - simDt < AI_TOSS_HIT_T - lead && aiTossT >= AI_TOSS_HIT_T - lead &&
                    RandRange(0.0f, 1.0f) < SfxServeChance(ai.persona))   // サーブは声が出やすい
                    aiGrunt = SfxServePreBy(ai.persona, Vector3Distance(cam.position, ai.pos));
                ai.anim.windStroke = 2;
                ai.anim.windup = 0.3f + 0.7f * Clamp01(aiTossT / SERVE_CHARGE_TIME);
            }
            if (aiTossing && aiTossT >= AI_TOSS_HIT_T) {
                aiTossing = false;
                ai.anim.windup = 0.0f;
                ball = aiUnderarm ? AiServeUnderarm(&ai, svDeuce) : AiServe(&ai, svDeuce, serveNo == 1);
                if (retServing) ai.netRush = false;   // リターン練習：打ったら動かない（リターンの練習なので、ラリーにはしない）
                speedBoardKmh = (int)roundf(ai.lastKmh);
                SfxServe(ai.anim.lastContact.quality, ai.lastKmh, aiGrunt, Vector3Distance(cam.position, ball.pos));
                P.anim.splitT = bot.anim.splitT = SPLIT_TIME;   // レシーバーのスプリットステップ
                if (mode == MODE_MATCH) MLogShot(mlPoint, ++mlShot, true, "SERVE", ai.lastKmh, ai.anim.lastContact.quality, false, false, false, false, ai.pos, ball.pos.y, P.pos, Vector3Length(P.vel));
                youMinDist = 99.0f;
                aiGrunt = false;
                rallyHits = 1;
                trailColor = SERVES[serveNo == 1 ? SERVE_FLAT : SERVE_KICK].color;
                trailCount = 0;
                if (retServing) retPhase = RET_LIVE; else phase = PH_PLAY;
                serveJudged = false;
            }
        }

        // ---- スイング（ラリー・リターン・試合）----
        if (!playerServing && mode != MODE_SERVE && !botControls && P.downT <= 0.0f) {
            if (P.chargingShot < 0) {
                if (in.shot >= 0) {
                    P.chargingShot = in.shot;
                    P.chargeSrc = in.source;
                    P.charge = 0.0f;
                    P.hold = 0.0f;
                }
            } else {
                P.hold = fminf(P.hold + frameDt / CHARGE_TIME, HOLD_MAX);
                // 打つ前の声：溜めている間、一番いい瞬間まで GRUNT_LEAD 秒を切ったら鳴らし始める（スライス・ボレーは声を出さない）
                if (!playerGrunt && !playerGruntTried && !IsSliceShot((ShotType)P.chargingShot) && ball.active && ball.lastHitter == HITTER_OPP &&
                    ball.bounces >= 1 && P.pos.z > VOICE_NET_Z && P.hold >= 0.5f) {   // 強く打つときだけ。ネット前は声なし
                    gCasual = (style == STYLE_CASUAL);
                    Assist ga = ComputeAssist(P.pos, P.vel, &ball, (ShotType)P.chargingShot, P.charge);
                    gCasual = false;
                    if (ga.timingValid && !ga.overhead && ga.tBest <= SfxGruntLead(gYouPersona ? gYouPersona : -1) && ga.qBest > 0.2f) {
                        if (!gYouPersona || RandRange(0.0f, 1.0f) < SfxGruntChance(gYouPersona))   // 選手を使っているときは、その選手の割合で
                            playerGrunt = SfxGruntPreBy(gYouPersona ? gYouPersona : -1, Vector3Distance(cam.position, P.pos));
                        playerGruntTried = true;
                    }
                }
                P.charge = fminf(P.hold, 1.0f);
                // 構えの見た目：ボールのある側で構える（高い球なら頭上）
                P.anim.windup = P.charge;
                P.anim.slice = IsSliceShot((ShotType)P.chargingShot);
                if (ball.active && ball.lastHitter == HITTER_OPP)
                    P.anim.windStroke = ball.pos.y > OVERHEAD_MIN_H && Vector3Distance(ball.pos, P.pos) < 6.0f ? 2
                                          : ((gLefty ? ball.pos.x <= P.pos.x : ball.pos.x >= P.pos.x) ? 0 : 1);   // 左利きは体の左がフォア
                P.anim.smash = P.anim.windStroke == 2;
                bool released = in.released[P.chargeSrc];
                if (released) {
                    ShotType rt = (ShotType)P.chargingShot;
                    float rcharge = P.charge;
                    P.chargingShot = -1;
                    P.anim.windup = 0.0f;
                    P.swingQ.active = true; P.swingQ.type = rt; P.swingQ.charge = rcharge;
                    P.swingQ.target = AimFromHold(rt, P.pos.z, P.aimX, P.hold, style);   // 深さは押した長さ、左右はマウス
                    P.swingQ.age = -1.0f;   // すぐ振る
                    P.swingQ.releaseAge = ball.age;
                    P.swingQ.penalty = 1.0f;
                    if (ball.active && ball.lastHitter == HITTER_OPP) {
                        // 早く振ったとき、この先の一番いい瞬間が今より良ければ、ラケットがそこに合わせにいく
                        //   本格：0.2 秒先まで。早かった分だけ質が落ちる
                        //   簡単：1 秒先まで。質は落ちない
                        bool cas = style == STYLE_CASUAL;
                        float window = cas ? CASUAL_WAIT : ADAPT_EARLY;
                        gCasual = cas; gCrouch = in.crouch;
                        float nowQ = 0.0f, bestQ = 0.0f, bestAge = -1.0f;
                        for (int h = 0; h < historyCount; h++) {
                            const Ball *hb = &history[h];
                            if (!(hb->active && hb->lastHitter == HITTER_OPP && hb->bounces <= 1 && hb->pos.z > 0.0f &&
                                  !hb->hitNet && (!hb->isServe || hb->bounces == 1))) continue;
                            Contact hc = EvaluateContact(P.pos, hb, rt, rcharge, 0.0f, 0.0f);
                            if (hc.miss == MISS_NONE && hc.quality > nowQ) nowQ = hc.quality;
                        }
                        Ball fb = ball;
                        for (int i = 0; i < (int)(window / PHYS_DT) && fb.active && fb.bounces < 2; i++) {
                            StepBall(&fb, PHYS_DT);
                            if (!(fb.bounces <= 1 && fb.pos.z > 0.0f && !fb.hitNet && (!fb.isServe || fb.bounces == 1))) continue;
                            Contact fc = EvaluateContact(P.pos, &fb, rt, rcharge, 0.0f, 0.0f);
                            if (fc.miss == MISS_NONE && fc.quality > bestQ) { bestQ = fc.quality; bestAge = fb.age; }
                        }
                        gCasual = false; gCrouch = false;
                        if (bestAge > 0.0f && bestQ > nowQ + 0.03f) {
                            P.swingQ.age = bestAge;
                            if (!cas) P.swingQ.penalty = Clamp(1.0f - (bestAge - ball.age) / ADAPT_PENALTY_T, 0.10f, 1.0f);
                            P.anim.windup = rcharge;   // 構えたまま待つ
                        }
                    }
                }
            }
        }
        // ---- スイングの実行（すぐ、または簡単モードで待っていた瞬間）----
        if (P.swingQ.active && (P.swingQ.age < 0.0f || ball.age >= P.swingQ.age - 0.5f / 60.0f ||
                              !ball.active || ball.lastHitter != HITTER_OPP)) {
            P.swingQ.active = false;
            ShotType type = P.swingQ.type;
            Vector3 target = P.swingQ.target;
            float swingCharge = P.swingQ.charge;
            P.anim.windup = 0.0f;
            gCasual = (style == STYLE_CASUAL); gCrouch = in.crouch;
            {
                    // 直前のフレームの間で、一番いい当たりになる瞬間のボールを使う
                    Contact c = { .miss = MISS_NO_BALL };
                    int bestIdx = -1;
                    for (int h = 0; h < historyCount; h++) {
                        const Ball *hb = &history[h];
                        bool hittable = hb->active && hb->lastHitter == HITTER_OPP &&
                                        hb->bounces <= 1 && hb->pos.z > 0.0f && !hb->hitNet &&
                                        (!hb->isServe || hb->bounces == 1);   // サーブはバウンドしてから
                        if (!hittable) continue;
                        Vector3 toBall = { hb->pos.x - P.pos.x, 0.0f, hb->pos.z - P.pos.z };
                        float moveToward = (Vector3Length(toBall) > 0.01f) ? Vector3DotProduct(P.vel, Vector3Normalize(toBall)) : 0.0f;
                        Contact hc = EvaluateContact(P.pos, hb, type, swingCharge, moveToward, P.sliding ? SLIDE_REACH : 0.0f);
                        bool better = (bestIdx < 0) ||
                                      (hc.miss == MISS_NONE && (c.miss != MISS_NONE || hc.quality > c.quality)) ||
                                      (hc.miss != MISS_NONE && c.miss != MISS_NONE);
                        if (better) { c = hc; bestIdx = h; }
                    }
                    if (bestIdx >= 0 && c.miss == MISS_NONE) ball = history[bestIdx];
                    c.stepIn = -P.vel.z;
                    // 早く振ってラケットが合わせにいった分：タイミングの質を落とし、引っ張り気味にする
                    if (c.miss == MISS_NONE && P.swingQ.age >= 0.0f) {
                        float early = fmaxf(P.swingQ.age - P.swingQ.releaseAge, 0.0f);
                        c.qTiming *= P.swingQ.penalty;
                        c.quality *= P.swingQ.penalty;
                        c.fwdErr += early * Vector3Length(ball.vel);
                        c.timeErrMs = early * 1000.0f;   // プラス＝早い
                    }

                    // スイングの見た目と、理想の打点との比較用
                    Vector3 sb = (bestIdx >= 0) ? history[bestIdx].pos : ball.pos;
                    P.anim.lastContact = c;
                    if (c.miss == MISS_NONE && c.lunge && !c.dive) { P.anim.lungeDur = c.stretch ? STRETCH_TIME : LUNGE_TIME; P.anim.lungeT = P.anim.lungeDur; P.anim.lungeSide = c.forehand ? 1.0f : -1.0f; }
                    P.anim.slice = IsSliceShot(type);
                    P.anim.contactLocal = (Vector3){ sb.x - P.pos.x, sb.y, P.pos.z - sb.z };
                    P.anim.swingT = 0.0f;
                    P.anim.smash = c.overhead;
                    if (c.overhead) P.lastIdealPos = (Vector3){ P.pos.x + OVERHEAD_SIDE * handS, OVERHEAD_IDEAL_H, P.pos.z - OVERHEAD_FWD };
                    else P.lastIdealPos = (Vector3){ P.pos.x + (c.forehand ? IDEAL_SIDE : -IDEAL_SIDE) * handS,
                                                   SHOTS[type].idealHeight, P.pos.z - IDEAL_FWD };
                    P.lastContactPos = sb;
                    if (c.miss == MISS_NONE && c.dive) { P.downT = DIVE_DOWN_TIME; P.vel = (Vector3){ 0 }; }
                    if (c.miss == MISS_NONE && c.stretch) { P.recoverT = STRETCH_RECOVER; P.vel = Vector3Scale(P.vel, 0.3f); }
                    {   // よろめき：体勢の崩れ具合で、立て直すまで遅くなる
                        stagS = StaggerAmount(&c, fabsf(P.vel.x)) * STAGGER_HUMAN;
                        P.staggerTotal = P.staggerT = stagS > 0.0f ? STAGGER_T0 + STAGGER_T1 * stagS : 0.0f;
                        P.anim.staggerSide = c.forehand ? 1.0f : -1.0f;
                    }

                    P.hasLast = true;
                    P.lastContact = c;
                    P.lastType = type;

                    // ---- プレイ記録 ----
                    if (c.miss != MISS_NO_BALL) {
                        Ball pre = (bestIdx >= 0) ? history[bestIdx] : ball;
                        float bestQ, offMs;
                        LogTimingAnalysis(&plog, P.pos, &pre, type, swingCharge, &bestQ, &offMs);
                        if (P.swingQ.age >= 0.0f) offMs += (P.swingQ.releaseAge - pre.age) * 1000.0f;   // 実際に離した瞬間で記録
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
                                 GetTime(), MODE_NAMES[mode], AI_LEVELS[ai.level].name, SHOTS[type].name, swingCharge,
                                 StrokeName(&c), c.volley, c.dive, MissName(c.miss),
                                 c.quality, c.qTiming, c.qDist, c.qHeight, c.angleDeg, c.side, c.height,
                                 bestQ > 0.0f ? TextFormat("%.0f", offMs) : "", bestQ, c.inKmh, "%OUT%", c.stepIn,
                                 P.pos.x, P.pos.z, Vector3Length(P.vel), pre.pos.x, pre.pos.y, pre.pos.z, pre.age,
                                 plog.firstMove, plog.chargeStart);
                        if (c.miss != MISS_NONE) {
                            // 空振り：結果は WHIFF。打球速度は空
                            char *o = strstr(plog.row, "%OUT%");
                            if (o) memmove(o, o + 5, strlen(o + 5) + 1);
                            plog.pending = true; plog.pendingServe = false;
                            LogFlush(&plog, "WHIFF");
                        }
                    }

                    if (c.miss != MISS_NONE && c.miss != MISS_NO_BALL) SfxWhiff();
                    if (c.miss == MISS_NONE) {
                        if (c.volley) c.quality += (1.0f - c.quality) * 0.25f * PERSONAS[gYouPersona].volleySkill;   // ボレーのうまい選手は当たりの悪さを少し取り戻す
                        gHumanShot = true;
                        ball = PlayerShot(&c, ball.pos, target, type, (HOLD_POWER_BASE + (HOLD_POWER_MAX - HOLD_POWER_BASE) * swingCharge) * (1.0f + YOU_RAGE_POWER * gYouRage),   // 燃えていると少し強い
                                          &P.lastKmh, &P.lastRpm, &P.lastAngle);
                        gHumanShot = false;
                        ms[0].shots++; ms[0].speedSum += P.lastKmh;
                        ai.anim.splitT = SPLIT_TIME; ai.anim.celebT = 0.0f;
                        if (mode == MODE_MATCH) MLogShot(mlPoint, ++mlShot, false, SHOTS[type].name, P.lastKmh, c.quality, false, false, c.volley, c.isReturn, P.pos, c.height, ai.pos, Vector3Length(ai.vel));
                        if (mode == MODE_MATCH && gYouPersona && SfxHasRoar(gYouPersona) && ClearWinner(&ball, ai.pos) && RandRange(0.0f, 1.0f) < 0.5f + 0.5f * gPointImportance)
                            SfxHitRoar(gYouPersona, Vector3Distance(cam.position, P.pos));   // 自分がジョコビッチを使っているときも
                        else
                        SfxHit(c.quality, P.lastKmh, IsSliceShot(type), playerGrunt,
                               DefensiveVoice(&c, type, P.pos.z, swingCharge), Vector3Distance(cam.position, ball.pos));
                        playerGrunt = false;
                        rallyHits++;
                        P.lastAimTarget = target;
                        P.aimX = 0.0f; P.aimSlide = 0.0f;   // 打つたびに狙いを真ん中に戻す
                        ball.lastHitter = HITTER_PLAYER;
                        ball.shotId = ++plog.shotSerial;
                        {
                            // 打球速度を埋めて、結果待ちにする
                            char *o = strstr(plog.row, "%OUT%");
                            if (o) {
                                char tail[1024];
                                snprintf(tail, sizeof(tail), "%s", o + 5);
                                snprintf(o, sizeof(plog.row) - (size_t)(o - plog.row), "%.0f%s", P.lastKmh, tail);
                            }
                            plog.pending = true; plog.pendingId = ball.shotId; plog.pendingServe = false;
                            plog.pendingLaunch = P.lastAngle * RAD2DEG; plog.pendingApex = ball.pos.y;
                            plog.pendingVy = c.vyBall; plog.hasVy = !c.volley;
                        }
                        trailColor = SHOTS[type].color;
                        if (c.quality >= SWEET_Q) { sweetT = 0.0f; sweetPos = ball.pos; sweetQ = c.quality; trailColor = (Color){ 255, 210, 60, 255 }; }   // いい当たり：金色の軌跡と光
                        trailCount = 0;
                        stats[type][0]++;
                        scoredLast = false;
                    }
            }
            gCasual = false; gCrouch = false;
        }
        // ---- ヒンドランス（妨害）：相手がサーブのトスを上げている間・相手の打った球が飛んでいる間に、カモン（Y）と叫ぶと相手のポイント ----
        //      あおる（E）は移動のキー（W・D）の隣で、うっかり押しやすいので対象にしない（相手のプレー中に押しても何も起きない）
        if (mode == MODE_MATCH && !score.over && !botControls && ActPressed(ACT_COMEON) &&
            phase == PH_PLAY && ball.active && ball.lastHitter == HITTER_OPP && ball.bounces == 0 && rallyHits >= 2) {   // 相手の打った球（サーブ以外）が飛んでいる間だけ
            aiTossing = false; ai.anim.windup = 0.0f;
            ball = (Ball){ 0 }; trailCount = 0;
            P.anim.celebKind = CELEB_COMEON; P.anim.celebDur = CelebDuration(CELEB_COMEON); P.anim.celebT = P.anim.celebDur;
            P.anim.swingT = -1.0f; P.anim.windup = 0.0f; P.chargingShot = -1; P.chargingServe = -1;
            SfxCelebrate(gYouPersona ? gYouPersona : -1, Vector3Distance(cam.position, P.pos));
            SfxBoo(0.4f, 0.3f);
            int gb = score.games[0] + score.games[1], sb = score.nSetsDone;
            AwardPoint(&score, 1);
            ms[1].points++; lastPtWinner = 1;
            if (!score.over && score.nSetsDone == sb && score.games[0] + score.games[1] != gb && (score.games[0] + score.games[1]) % 2 == 1) { changePending = true; changeSit = true; }
            snprintf(rageMsg, sizeof(rageMsg), TL("HINDRANCE!\nYOU shouted \"C'mon!\" during the opponent's %s.\nPoint to %s.", "ヒンドランス（妨害）！\nあなたが、相手の%s中に「カモン」と叫んだ\n%s のポイント"),
                     phase == PH_SERVE ? TL("serve", "サーブ") : TL("shot", "打球"), OPP_SHORT);
            rageMsgT = 3.5f;
            snprintf(scoreCall, sizeof(scoreCall), "%s", ScoreText(&score));
            SfxScoreCall(&score, server, 1, 0, 1.4f);
            TraceLog(LOG_INFO, "POINT end: HINDRANCE -> AI");
            phase = PH_POINT_OVER; phaseTimer = 2.8f;
        }
        if (ActPressed(ACT_REPLAY) && mode != MODE_MATCH) {
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
        else if (mode == MODE_RALLY && !feedSwing && ball.active) AiUpdate(&ai, &ball, simDt);   // ラリー練習：相手も走って打ち返す

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
            // AI のフットワークの音：打つ 0.6 秒前に、まだ動いていたら
            if (mode == MODE_MATCH && phase == PH_PLAY && ai.planned && !aiFoot && ball.age >= ai.swingAge - 0.6f &&
                Vector3Length(ai.vel) > 1.0f && !ai.sliding) {
                SfxFootwork(2 + GetRandomValue(0, 1), Vector3Distance(cam.position, ai.pos));
                aiFoot = true;
            }
            // AI の打つ前の声：選手ごとの確率で（フェデラーはほとんど出さない、ナダルはほぼ毎回）。ネット前は声なし
            if (mode == MODE_MATCH && phase == PH_PLAY && ai.planned && !aiGruntTried && ball.bounces >= 1 &&
                ball.age >= ai.swingAge - SfxGruntLead(ai.persona)) {
                aiGruntTried = true;
                if (ai.pos.z < -VOICE_NET_Z && RandRange(0.0f, 1.0f) < SfxGruntChance(ai.persona))
                    aiGrunt = SfxGruntPreBy(ai.persona, Vector3Distance(cam.position, ai.pos));
            }
            if (botControls && phase == PH_PLAY && bot.planned && !botGruntTried && ball.lastHitter == HITTER_OPP && ball.bounces >= 1 &&
                ball.age >= bot.swingAge - GRUNT_LEAD) {
                botGruntTried = true;
                if (P.pos.z > VOICE_NET_Z && GetRandomValue(0, 99) < 60) botGrunt = SfxGruntPre(Vector3Distance(cam.position, P.pos));
            }
            if (ball.lastHitter != HITTER_OPP) { botGrunt = false; botGruntTried = false; }
            if (((mode == MODE_MATCH && phase == PH_PLAY) || mode == MODE_RALLY) && ai.planned && ball.age >= ai.swingAge) {
                if (AiSwing(&ai, &ball, P.pos)) {
                    if (mode == MODE_MATCH) ms[1].shots++, ms[1].speedSum += ai.lastKmh;
                    if (mode == MODE_MATCH) MLogShot(mlPoint, ++mlShot, true, SHOTS[ai.lastType].name, ai.lastKmh, ai.lastQ, ai.lastAttack, ai.lastLapse, ai.anim.lastContact.volley, ai.anim.lastContact.isReturn, ai.pos, ai.anim.lastContact.height, P.pos, Vector3Length(P.vel));
                    youMinDist = 99.0f;
                    P.anim.splitT = bot.anim.splitT = SPLIT_TIME;   // 相手が打った：スプリットステップ
                    // 決まったと確信したショット：雄叫び（ジョコビッチ）。いつもではなく、大事なポイントほど
                    if (mode == MODE_MATCH && SfxHasRoar(ai.persona) && ClearWinner(&ball, P.pos) && RandRange(0.0f, 1.0f) < 0.5f + 0.5f * gPointImportance)
                        SfxHitRoar(ai.persona, Vector3Distance(cam.position, ai.pos));
                    else
                    SfxHit(ai.anim.lastContact.quality, ai.lastKmh, IsSliceShot(ai.lastType), aiGrunt,
                           DefensiveVoice(&ai.anim.lastContact, ai.lastType, -ai.pos.z, 0.5f), Vector3Distance(cam.position, ball.pos));
                    aiGrunt = false;
                    rallyHits++;
                    trailColor = Fade(SHOTS[ai.lastType].color, 0.7f);
                    trailCount = 0;
                }
            }
            if (botControls && phase == PH_PLAY && bot.planned && ball.age >= bot.swingAge) {
                Ball fb = FlipSides(ball);
                if (AiSwing(&bot, &fb, Rot180(ai.pos))) {
                    ball = FlipSides(fb);
                    ms[0].shots++; ms[0].speedSum += bot.lastKmh;
                    ai.anim.splitT = SPLIT_TIME; ai.anim.celebT = 0.0f;
                    MLogShot(mlPoint, ++mlShot, false, SHOTS[bot.lastType].name, bot.lastKmh, bot.lastQ, bot.lastAttack, bot.lastLapse, bot.anim.lastContact.volley, bot.anim.lastContact.isReturn, P.pos, bot.anim.lastContact.height, ai.pos, Vector3Length(ai.vel));
                    SfxHit(bot.anim.lastContact.quality, bot.lastKmh, IsSliceShot(bot.lastType), botGrunt,
                           DefensiveVoice(&bot.anim.lastContact, bot.lastType, P.pos.z, 0.5f), Vector3Distance(cam.position, ball.pos));
                    botGrunt = false;
                    rallyHits++;
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
                if (fabsf(ball.pos.x - retX) - REACH_SIDE > canMove) { aces++; serveMsg = TR("ACE!", "エース！"); }
                else serveMsg = TR("IN (returnable)", "イン（返される）");
            }
        }

        // バウンドを見つけて目印を出す
        bounceT += simDt; markT += frameDt; hawkT += frameDt;
        if (hawkPending) {   // ポイント（練習ならラリー）が終わったら、際どかった球の拡大図を出す
            bool live = mode == MODE_MATCH ? phase == PH_PLAY
                      : (ball.active && !ball.hitNet && ball.bounces <= 1 && (ball.result == RESULT_NONE || ball.result == RESULT_IN));
            if (!live) { hawkPending = false; hawkT = 0.0f; }
        }
        if (ball.bounces > prevBounces && ball.bounces <= 2) {
            SfxBounce(Vector3Length(ball.vel), Vector3Distance(cam.position, ball.pos));
            bouncePos = (ball.bounces == 1) ? ball.firstBounce : ball.pos;   // 判定に使った、弾んだ瞬間の位置
            bounceT = 0.0f;
            if (ball.bounces == 1) {
                if (mode == MODE_MATCH) MLogLand(&ball);
                markPos = ball.firstBounce; markT = 0.0f;
                // 際どい球（ラインから 8cm 以内）：ホークアイのような拡大図を出す
                if (fabsf(ball.lineDist - BALL_R) < 0.08f && !ball.hitNet) { hawk = ball; hawkPending = true; }   // ラリー中は出さない（画面がふさがる）
            }
            bounceByPlayer = ball.lastHitter == HITTER_PLAYER && ball.bounces == 1 && !botControls;
        }
        prevBounces = ball.bounces;
        if (!ball.active || ball.lastHitter != HITTER_OPP) { if (!(playerServing && P.sv == SV_TOSS)) { playerGrunt = false; playerGruntTried = false; } }
        if (!ball.active || ball.lastHitter != HITTER_PLAYER) { if (!aiTossing) { aiGrunt = false; aiGruntTried = false; } }
        if (ball.active && ball.hitNet && !prevHitNet) SfxNet(Vector3Length(ball.vel), Vector3Distance(cam.position, ball.pos));
        {   // ネットイン（白帯をかすめて越えた）も、ネットの音（小さめ）
            static bool prevNetCord = false;
            if (ball.active && ball.netCord && !prevNetCord) SfxNet(Vector3Length(ball.vel) * 0.6f, Vector3Distance(cam.position, ball.pos));
            prevNetCord = ball.active && ball.netCord;
        }
        prevHitNet = ball.active && ball.hitNet;
        if (ai.sliding && !prevAiSliding) SfxSlide(Vector3Distance(cam.position, ai.pos));
        prevAiSliding = ai.sliding;
        SfxCrowd(mode == MODE_MATCH, ((phase == PH_PLAY || phase == PH_SERVE) ? 0.55f : 1.0f) * (quietT > QUIET_SAY ? 3.0f : quietT > QUIET_THANKS ? 1.2f : quietT > 0.0f ? 0.4f : 1.0f));   // ポイント中は観客は静か。「お静かに」の前は騒がしく、注意されると静まる
        SfxUpdate();
        // フットワークの音（自分）：打つ 0.6 秒ほど前に、まだ動いて位置を合わせていたら「キュッ」を 2〜3 回
        if (!ball.active || ball.lastHitter != HITTER_OPP) playerFoot = false;
        else if (!playerFoot && !botControls && mode != MODE_SERVE && ball.age > 0.2f && Vector3Length(P.vel) > 1.0f && !P.sliding) {
            ShotType ft = P.chargingShot >= 0 ? (ShotType)P.chargingShot : SHOT_TOP;
            Assist fa = ComputeAssist(P.pos, P.vel, &ball, ft, 0.5f);
            if (fa.timingValid && fa.qBest > 0.15f && fa.tBest <= 0.65f && fa.tBest >= 0.40f) {
                SfxFootwork(2 + GetRandomValue(0, 1), Vector3Distance(cam.position, P.pos));
                playerFoot = true;
            }
        }
        if (!ball.active || ball.lastHitter != HITTER_PLAYER) aiFoot = false;

        // ---- プレイ記録：来た球の追跡と、打球の結果 ----
        if (!botControls) {
            LogTrackIncoming(&plog, &ball, P.pos, P.vel, P.chargingShot >= 0, MODE_NAMES[mode], AI_LEVELS[ai.level].name);
            if (plog.pending) {
                if (ball.shotId == plog.pendingId && ball.bounces == 0 && ball.pos.y > plog.pendingApex) plog.pendingApex = ball.pos.y;
                if (ball.shotId != plog.pendingId) LogFlush(&plog, "?");
                else if (ball.result != RESULT_NONE) LogFlush(&plog, ResultText(ball.result));
            }
        }

        // ---- 試合：ポイントの判定 ----
        if (mode == MODE_MATCH && phase == PH_PLAY) {
            // AI の球に一番近づいた距離（打てる高さで、こちらのコートにあるうち）
            if (ball.active && ball.lastHitter == HITTER_OPP && ball.bounces <= 1 && ball.pos.z > 0.0f && ball.pos.y < 2.5f) {
                Vector3 me = botControls ? Rot180(bot.pos) : P.pos;
                (void)me;
                float d = sqrtf((P.pos.x - ball.pos.x) * (P.pos.x - ball.pos.x) + (P.pos.z - ball.pos.z) * (P.pos.z - ball.pos.z));
                if (d < youMinDist) youMinDist = d;
            }
            int hitter = (ball.lastHitter == HITTER_PLAYER) ? 0 : 1;
            int winner = -1;
            bool returnShot = retPending && !ball.isServe;   // 今飛んでいるのはリターン
            int endKind = 0;                                 // 1=エース 2=ウィナー 3=ミス 4=ダブルフォールト 5=体に当てた（バトル）
            if (battle && ball.active && ball.bounces <= 1 && !ball.hitNet && (ball.bounces == 0 || ball.result == RESULT_IN) && ball.pos.y > 0.3f && ball.pos.y < 1.9f) {
                // バトル：打った球が、バウンドする前に相手の体（腰から頭）に当たったら、打った方のポイント。当たった方は倒れる
                Vector3 vic = hitter == 0 ? ai.pos : (botControls ? Rot180(bot.pos) : P.pos);
                float dx = ball.pos.x - vic.x, dz = ball.pos.z - vic.z;
                if (dx * dx + dz * dz < BATTLE_BODY_R * BATTLE_BODY_R) {
                    winner = hitter; endKind = 5;
                    pointMsg = hitter == 0 ? TL("Your BODY SHOT!", "自分のボディショット！") : TL("AI BODY SHOT!", "相手のボディショット！");
                    ball.vel = (Vector3){ -ball.vel.x * 0.15f, 1.5f, -ball.vel.z * 0.15f };   // 当たって跳ね返る
                    int v = 1 - hitter;
                    if (v == 1) { ai.downTimer = DIVE_DOWN_TIME * 1.5f; ai.anim.downT = DIVE_DOWN_TIME * 1.5f; ai.vel = (Vector3){ 0 }; }
                    else if (botControls) { bot.downTimer = DIVE_DOWN_TIME * 1.5f; bot.anim.downT = DIVE_DOWN_TIME * 1.5f; bot.vel = (Vector3){ 0 }; }
                    else { P.downT = DIVE_DOWN_TIME * 1.5f; P.vel = (Vector3){ 0 }; }
                    hitFlashT[v] = 0.5f;
                    SfxHit(0.4f, 80.0f, false, false, false, 8.0f); SfxOoh(0.9f);
                    SfxAngry(v == 0 ? (gYouPersona ? gYouPersona : -1) : ai.persona, 10.0f);
                }
            }
            // ネットに出たか：サービスラインより前に入った
            if (P.pos.z < SERVICE_Z) netFlag[0] = true;
            if (ai.pos.z > -SERVICE_Z) netFlag[1] = true;
            // リターンが入ったかどうか（入らなければ「返されなかったサーブ」）
            if (retPending && !ball.isServe && ball.bounces >= 1 && ball.result != RESULT_NONE) {
                if (ball.result != RESULT_IN) mst[retServer][4]++;
                retPending = false;
            }
            if (ball.isServe && !serveJudged && ball.bounces >= 1 && ball.netCord && ball.result == RESULT_IN) {
                // レット：サーブがネットに触れて入った。同じ番目のサーブ（1st なら 1st）を打ち直す
                pointMsg = TL("Let", "レット");
                TraceLog(LOG_INFO, "LET by %s", hitter == 0 ? "YOU" : "AI");
                serveJudged = true;   // この球はもう判定しない
                phase = PH_BETWEEN;
                phaseTimer = 1.5f;
            } else if (ball.isServe && !serveJudged && ball.bounces >= 1) {
                mst[hitter][serveNo == 1 ? 0 : 2]++;
                if (ball.result == RESULT_IN) mst[hitter][serveNo == 1 ? 1 : 3]++;
                else if (serveNo == 2) mst[hitter][5]++;
                if (ball.result == RESULT_IN) {
                    serveJudged = true;
                    retPending = true; retServer = hitter;
                } else if (serveNo == 1) {
                    serveNo = 2;
                    pointMsg = (ball.result == RESULT_NET) ? TL("Fault (net)", "フォルト（ネット）") : TL("Fault", "フォルト");
                    if (ball.result == RESULT_OUT) { snprintf(outMsg, sizeof(outMsg), TL("Fault (OUT %.0fcm)", "フォルト（アウト %.0fcm）"), fmaxf(ball.outBy * 100.0f, 1.0f)); pointMsg = outMsg;
                                                     lineCallPos = ball.firstBounce; lineCallT = 1.6f; }
                    SfxCallFault();
                    TraceLog(LOG_INFO, "FAULT by %s", hitter == 0 ? "YOU" : "AI");
                    phase = PH_BETWEEN;
                    phaseTimer = 1.5f;
                } else {
                    winner = 1 - hitter;
                    pointMsg = TL("Double fault", "ダブルフォールト");
                    SfxOoh(0.45f + 0.4f * gPointImportance);   // ダブルフォールト：観客がどよめく
                    if (ball.result == RESULT_OUT) { lineCallPos = ball.firstBounce; lineCallT = 1.6f; }
                    SfxCallFault();
                    endKind = 4;
                }
            } else if (endKind == 5) {
                // 体に当てた：上で決まった
            } else if (!ball.isServe || serveJudged) {
                if (ball.bounces >= 1 && ball.result != RESULT_IN && ball.result != RESULT_NONE) {
                    winner = 1 - hitter;
                    endKind = 3;
                    if (ball.result == RESULT_OUT) { SfxCallOut(); lineCallPos = ball.firstBounce; lineCallT = 1.6f; }
                    if ((ball.result == RESULT_OUT && ball.outBy < 0.15f) || rallyHits >= 8)   // 惜しいアウト・長いラリーのミス：観客の「おお〜」
                        SfxOoh(0.55f + 0.05f * fminf(rallyHits, 8) + (ball.outBy < 0.05f ? 0.2f : 0.0f));
                    pointMsg = (ball.result == RESULT_NET) ? (hitter == 0 ? TL("Your error (net)", "自分のミス（ネット）") : TL("AI error (net)", "相手のミス（ネット）"))
                                                           : (hitter == 0 ? TL("Your error (out)", "自分のミス（アウト）") : TL("AI error (out)", "相手のミス（アウト）"));
                    if (ball.result == RESULT_OUT) {   // どれだけ外れたか
                        snprintf(outMsg, sizeof(outMsg), TL("%s error (OUT %.0fcm)", "%sのミス（アウト %.0fcm）"), hitter == 0 ? YouName() : gOppName, fmaxf(ball.outBy * 100.0f, 1.0f));
                        pointMsg = outMsg;
                    }
                } else if (ball.bounces >= 2) {
                    winner = hitter;
                    endKind = ball.isServe ? 1 : 2;
                    if (ball.isServe) { pointMsg = (hitter == 0) ? TL("Your ace / unreturned serve", "自分のサービスエース") : TL("AI ace / unreturned serve", "相手のサービスエース");
                                        mst[hitter][4]++; retPending = false; }
                    else              pointMsg = (hitter == 0) ? TL("Your winner", "自分のウィナー") : TL("AI winner", "相手のウィナー");
                }
            }
            if (winner >= 0) {
                {   // テロップ：「Your ...」「AI ...」を名前に（例 NADAR winner）
                    static char nameMsg[96];
                    if (strncmp(pointMsg, "Your ", 5) == 0)      { snprintf(nameMsg, sizeof(nameMsg), "%s %s", YouName(), pointMsg + 5); pointMsg = nameMsg; }
                    else if (strncmp(pointMsg, "AI ", 3) == 0)   { snprintf(nameMsg, sizeof(nameMsg), "%s %s", gOppName, pointMsg + 3); pointMsg = nameMsg; }
                }
                TraceLog(LOG_INFO, "POINT end: %s -> %s", pointMsg, winner == 0 ? "YOU" : "AI");
                SfxPointEnd(rallyHits, endKind == 1 || endKind == 2);
                crowdExcite = Clamp01(0.25f + (rallyHits - 2) / 12.0f + ((endKind == 1 || endKind == 2) ? 0.35f : 0.0f) + 0.4f * gPointImportance);   // 観客が沸く
                ms[0].rallyTotal += rallyHits; ms[0].rallyPoints++;   // 1 ポイントの打数（サーブも 1 打）
                // ---- 試合の統計 ----
                {
                    int sv = server, rv = 1 - server;
                    ms[winner].points++;
                    lastPtWinner = winner;
                    if (DEMO_ONLY && !demoCounted) { DemoCountMatch(); demoCounted = true; }   // 体験版：1 ポイントでも遊んだら 1 回
                    if (serveNo == 1) { ms[sv].s1Pts++; if (winner == sv) ms[sv].s1Won++; }
                    else              { ms[sv].s2Pts++; if (winner == sv) ms[sv].s2Won++; }
                    if (!score.tiebreak) {
                        // ブレークポイント：レシーバーがこのポイントを取ればゲーム
                        Score tmp = score;
                        AwardPoint(&tmp, rv);
                        if (tmp.games[rv] != score.games[rv] || tmp.sets[rv] != score.sets[rv]) {
                            ms[rv].bpTotal++;
                            if (winner == rv) ms[rv].bpWon++;
                        }
                    }
                    for (int k = 0; k < 2; k++) if (netFlag[k]) { ms[k].netPts++; if (winner == k) ms[k].netWon++; }
                    if (endKind == 1) { ms[hitter].aces++; ms[hitter].winners++; }
                    if (endKind == 2) ms[hitter].winners++;
                    if (endKind == 3 && !returnShot) {
                        // アンフォーストエラー：来た球が遅めで、飛びつきでも伸びきりでもなかったのにミス
                        const Contact *hc = (hitter == 1) ? &ai.anim.lastContact
                                                          : (autopilot ? &bot.anim.lastContact : &P.anim.lastContact);
                        if (!hc->dive && hc->inKmh < 85.0f && hc->qDist >= 0.5f) ms[hitter].unforced++;
                    }
                }
                int gamesBefore = score.games[0] + score.games[1] + 6 * (score.sets[0] + score.sets[1]);
                int setsBefore = score.nSetsDone;
                AwardPoint(&score, winner);
                if (battle) {   // バトル：相手に決められた（ウィナー・エース・ボディショット）ときだけ体力が減る。自分のミスでは減らない。長いラリーは両方が疲れる
                    int lo = 1 - winner;
                    float dmg = endKind == 5 ? BATTLE_DMG_BODY : endKind == 1 ? BATTLE_DMG_ACE : endKind == 2 ? BATTLE_DMG_WINNER : 0.0f;
                    if (winner == 0) dmg *= 1.0f + 0.5f * gYouRage;   // 燃えているとダメージが大きい
                    hp[lo] -= dmg;
                    if (rallyHits > 6) { float tired = 0.6f * (rallyHits - 6); hp[0] -= tired; hp[1] -= tired; }
                    for (int k = 0; k < 2; k++) if (hp[k] < 0.0f) hp[k] = 0.0f;
                    if (!score.over && (hp[0] <= 0.0f || hp[1] <= 0.0f)) {   // K.O.：その場で試合終わり
                        int ko = hp[lo] <= 0.0f ? winner : (hp[0] <= 0.0f ? 1 : 0);   // 勝った方
                        winner = ko;
                        if (score.nSetsDone < 5) { score.setScore[score.nSetsDone][0] = score.games[0]; score.setScore[score.nSetsDone][1] = score.games[1]; score.nSetsDone++; }
                        score.sets[ko] = (score.setsToWin > 0 ? score.setsToWin : 1);
                        score.over = true;
                        koT = 0.0f;
                        SfxMatchWin();
                        TraceLog(LOG_INFO, "KO: %s wins", ko == 0 ? "YOU" : "AI");
                    }
                }
                gYouRage *= YOU_RAGE_DECAY; if (gYouRage < 0.15f) gYouRage = 0.0f;   // 燃えた量はポイントごとに弱まる
                int gamesAfter = score.games[0] + score.games[1] + 6 * (score.sets[0] + score.sets[1]);
                {   // コートチェンジ：このセットのゲーム数が奇数になったら
                    bool setEndedCO = score.nSetsDone != setsBefore;
                    if (!score.over && (gamesAfter != gamesBefore || setEndedCO)) {
                        int gIn = setEndedCO ? score.setScore[score.nSetsDone - 1][0] + score.setScore[score.nSetsDone - 1][1] : score.games[0] + score.games[1];
                        if (gIn % 2 == 1) { changePending = true; changeSit = !(gIn == 1 && !setEndedCO); }   // 1 ゲーム目の後は座らない
                    }
                }
                if (score.nSetsDone != setsBefore) {
                    int *g = score.setScore[score.nSetsDone - 1];
                    snprintf(scoreCall, sizeof(scoreCall), TL("GAME, SET%s  %s   (%d - %d)", "ゲーム、セット%s  %s   (%d - %d)"), score.over ? TL(" AND MATCH", "アンドマッチ") : "",
                             winner == 0 ? YouName() : OPP_SHORT, g[0], g[1]);
                    if (!score.over) { setBreak = true; summaryT = 0.0f; }
                }
                else if (gamesAfter != gamesBefore || (score.points[0] == 0 && score.points[1] == 0))
                    snprintf(scoreCall, sizeof(scoreCall), TL("GAME %s   (%d - %d)", "ゲーム %s   (%d - %d)"), winner == 0 ? YouName() : OPP_SHORT, score.games[0], score.games[1]);
                else                              snprintf(scoreCall, sizeof(scoreCall), "%s", ScoreText(&score));
                // 主審のスコアコール（拍手が少し落ち着いてから）
                SfxScoreCall(&score, server, winner, score.nSetsDone != setsBefore ? 2 : (gamesAfter != gamesBefore ? 1 : 0), 1.1f);
                phase = PH_POINT_OVER;
                phaseTimer = 2.0f;
                if (score.over && koT >= 0.0f) phaseTimer = 4.6f;   // K.O.：大きな文字が消えてから、試合の結果の表を出す（重ならないように）
                // ---- 追い込まれた選手がラケットを叩きつける（ジョコビッチ）。叩きつけた後はしばらく強くなる ----
                {
                    bool setEnded = score.nSetsDone != setsBefore;
                    bool gameEnded = gamesAfter != gamesBefore || setEnded;
                    if (gameEnded) ai.rage *= setEnded ? 0.7f : RAGE_DECAY_GAME;   // 時間がたつと落ち着く
                    bool downBreak = gameEnded && !setEnded && score.games[0] - score.games[1] >= 2;
                    if (winner == 0 && !score.over && (setEnded || downBreak) && ai.rageSet != score.nSetsDone + 1 &&
                        RandRange(0.0f, 1.0f) < RAGE_SMASH[ai.persona % PERSONA_COUNT]) {
                        ai.rageSet = score.nSetsDone + 1;   // 1 セットに 1 回まで
                        ai.smashes++;
                        ai.rage = 1.0f;
                        ai.anim.smashT = SMASH_TIME;
                        ai.anim.swingT = -1.0f; ai.anim.windup = 0.0f;
                        smashPrevE = 0.0f;
                        phaseTimer = fmaxf(phaseTimer, SMASH_TIME + 1.6f);
                        const char *nm = OPP_SHORT;
                        if (ai.smashes < SMASH_PENALTY_FROM) {   // 自分と同じ決まり：それまでは警告だけ
                            snprintf(rageMsg, sizeof(rageMsg), TL("%s SMASHES HIS RACKET!\nCode violation, racket abuse. Warning, Mr. %s", "%s がラケットを叩きつけた！\n行為違反、ラケットの乱用。警告、%s 選手"), nm, nm);
                        } else {
                            // SMASH_PENALTY_FROM 回目からはポイントペナルティ（ゲームの始まりなので、これでゲームが終わることはない）
                            AwardPoint(&score, 0);
                            snprintf(rageMsg, sizeof(rageMsg), TL("%s SMASHES HIS RACKET!\nCode violation, racket abuse. Point penalty, Mr. %s", "%s がラケットを叩きつけた！\n行為違反、ラケットの乱用。ポイントペナルティ、%s 選手"), nm, nm);
                        }
                        rageMsgT = SMASH_TIME + 2.5f;
                        SfxAngry(ai.persona, Vector3Distance(cam.position, ai.pos));
                    }
                }
                // ---- ポイント後のしぐさ：試合・セットを取ったら必ず、大事なポイント・長いラリーで喜ぶ。マレーは落とすと怒る ----
                if (ai.anim.smashT <= 0.0f) {
                    bool setEnded2 = score.nSetsDone != setsBefore;
                    bool gameEnded2 = gamesAfter != gamesBefore || setEnded2;
                    bool big = gPointImportance >= 1.0f || (gPointImportance >= 0.5f && gameEnded2) || rallyHits >= 12;
                    const CelebStyle *cs = &CELEB_STYLE[ai.persona % PERSONA_COUNT];
                    int kind = CELEB_NONE;
                    #define PICK(arr) ((arr)[GetRandomValue(0, (int)(sizeof(arr) / sizeof((arr)[0])) - 1)])
                    if (winner == 1) {
                        if (score.over)                kind = PICK(cs->match);
                        else if (setEnded2)            kind = PICK(cs->set);
                        else {
                            float ch = cs->winChance * (big ? 1.0f : ((endKind == 1 || endKind == 2) ? 0.22f : 0.0f));
                            if (RandRange(0.0f, 1.0f) < ch) kind = PICK(cs->big);
                        }
                        if (kind != CELEB_NONE) SfxCelebrate(ai.persona, Vector3Distance(cam.position, ai.pos));
                    } else if (cs->lose[0] != CELEB_NONE && !score.over) {
                        float ch = cs->loseChance * (big ? 1.0f : ((endKind == 3 && hitter == 1) ? 0.5f : 0.15f));
                        if (RandRange(0.0f, 1.0f) < ch) { { int nz = 0, ids[4]; for (int q = 0; q < 4; q++) if (cs->lose[q]) ids[nz++] = cs->lose[q]; kind = nz ? ids[GetRandomValue(0, nz - 1)] : CELEB_NONE; } SfxAngry(ai.persona, Vector3Distance(cam.position, ai.pos)); }
                    }
                    #undef PICK
                    if (score.over && winner == 0) {   // 自分の勝ち：相手はがっかり（喜ばない）
                        kind = ai.persona == 7 ? CELEB_SHRUG : CELEB_HANDS_HEAD;
                    }
                    {   // 気持ち：大事なポイント・セットを落とすと崩れる（ポイントごとに戻る）。先にセットを取られると切れる（キルギオス）
                        const Mental *mm = &MENTAL[ai.persona % PERSONA_COUNT];
                        ai.tilt *= 0.80f;
                        if (winner == 0 && (gPointImportance >= 1.0f || setEnded2)) ai.tilt = fminf(1.0f, ai.tilt + mm->tilt);
                        if (mm->tank > 0.0f && score.sets[0] > score.sets[1]) ai.tilt = fmaxf(ai.tilt, mm->tank);
                        // ポイントの間のしぐさ（ナダルの儀式、シャラポワは後ろを向いてガットを直す、髪を耳にかける）
                        if (kind == CELEB_NONE && mm->betweenRit && !score.over && !SALE_ONLY && RandRange(0.0f, 1.0f) < mm->betweenChance) {
                            ai.anim.celebKind = mm->betweenRit; ai.anim.ritBounces = 0;
                            ai.anim.celebDur = RitualDuration(mm->betweenRit, 0); ai.anim.celebT = ai.anim.celebDur;
                            ai.anim.swingT = -1.0f; ai.anim.windup = 0.0f;
                        }
                    }
                    if (kind != CELEB_NONE) {
                        if (SALE_ONLY && kind == CELEB_EAR) kind = CELEB_FIST;        // 売る版：特定の選手を思わせるしぐさは、ふつうの喜び方に
                        if (SALE_ONLY && kind == CELEB_HEART) kind = CELEB_ARMS_UP;
                        ai.anim.celebKind = kind; ai.anim.celebDur = CelebDuration(kind); ai.anim.celebT = ai.anim.celebDur;
                        ai.anim.walkT = 0.0f; ai.anim.faceYaw = 0.0f;
                        if (kind == CELEB_RANT) {   // 抗議：審判台の前まで歩いていき、審判の方を向いて文句を言う
                            protestTo = (Vector3){ -(NET_POST_X + 1.0f) + 1.5f, 0.0f, -1.6f };   // 審判台はネットの左横。相手のコート側に立つ
                            float d = Vector3Distance(ai.pos, protestTo);
                            ai.anim.walkT = fminf(d / PROTEST_WALK, 3.2f);
                            ai.anim.celebDur += ai.anim.walkT + 0.6f; ai.anim.celebT = ai.anim.celebDur;
                            Vector3 tu = { -(NET_POST_X + 1.0f) - protestTo.x, 0.0f, 0.0f - protestTo.z };   // 審判の向き（世界）
                            ai.anim.faceYaw = atan2f(-tu.x, tu.z);   // AI の体から見て（右＝世界の -x、前＝世界の +z）
                        }
                        ai.anim.swingT = -1.0f; ai.anim.windup = 0.0f;
                        phaseTimer = fmaxf(phaseTimer, ai.anim.celebDur + 0.4f);
                    }
                }
                // ---- ネットイン（白帯をかすめて入った）で決まった：打った方が手を上げて謝る（喜ばない） ----
                if (ball.netCord && !ball.isServe && endKind == 2 && !score.over) {
                    SwingAnim *sa = hitter == 1 ? &ai.anim : (botControls ? &bot.anim : &P.anim);
                    sa->celebKind = CELEB_RACKET_UP; sa->celebDur = 1.4f; sa->celebT = sa->celebDur;
                    sa->swingT = -1.0f; sa->windup = 0.0f;
                    phaseTimer = fmaxf(phaseTimer, 1.8f);
                    snprintf(toast, sizeof(toast), TL("Net cord - %s raises a hand: sorry!", "ネットイン：%s が手を上げて謝る"), hitter == 1 ? OPP_SHORT : YouName()); toastT = 2.2f;
                }
                // ---- ハプニング：ときどきハトが降りてくる、観客が騒いで主審が「お静かに」 ----
                if (mode == MODE_MATCH && !score.over && !gPigeon.on && quietT <= 0.0f && !quietPending) {
                    float r = RandRange(0.0f, 1.0f);
                    if (r < 0.015f)     { PigeonStart(); phaseTimer = fmaxf(phaseTimer, 2.0f); }   // ハトはたまに（多いと言われた）
                    else if (r < 0.045f) quietPending = true;   // 次のサーブに入るときに、観客が静まらない
                }
                // ---- 試合に勝った：自分が喜ぶ（膝をつく・両手を上げる・跳んで拳）、観客が沸く、勝利の演出 ----
                if (score.over && winner == 0 && DEMO_ONLY && mode == MODE_MATCH) {   // 体験版のチャレンジ
                    if (demoClean && style == STYLE_REAL && ai.level >= 1)
                        DemoMakeCode(demoCode, sizeof(demoCode), ai.level, (int)style, ai.persona,
                                     score.nSetsDone ? score.setScore[0][0] : score.games[0], score.nSetsDone ? score.setScore[0][1] : score.games[1], ms[0].points, ms[1].points);
                    else demoTried = true;
                }
                if (score.over && winner == 0) {
                    SwingAnim *pa = &P.anim;   // 試合が終わると、自動操作でも自分の姿（P.anim）を描く
                    static const int WIN[4] = { CELEB_KNEES, CELEB_ARMS_UP, CELEB_JUMP_FIST, CELEB_RACKET_UP };
                    int wk = WIN[GetRandomValue(0, 3)];
                    pa->celebKind = wk; pa->celebDur = CelebDuration(wk) + 0.8f; pa->celebT = pa->celebDur;
                    pa->swingT = -1.0f; pa->windup = 0.0f;
                    SfxCelebrate(gYouPersona ? gYouPersona : -1, Vector3Distance(cam.position, P.pos));   // 自分の喜ぶ声（sounds/you_celebrate など）
                    SfxMatchWin();      // 大歓声（sounds/match_win があれば重ねる）
                    crowdExcite = 1.0f;
                    winFx = 0.0f;
                    phaseTimer = fmaxf(phaseTimer, 4.5f);   // 喜ぶ間は統計を出さない
                }
                // ---- 試合の記録 ----
                {
                    const char *ek = endKind == 1 ? "ace" : endKind == 2 ? "winner" : endKind == 4 ? "double_fault"
                                   : (ball.result == RESULT_NET ? "error_net" : "error_out");
                    bool youEnd = hitter == 0;
                    ShotType et = youEnd ? (botControls ? bot.lastType : P.lastType) : ai.lastType;
                    const char *etn = ball.isServe ? "SERVE" : SHOTS[et].name;
                    float ekmh = ball.isServe ? (youEnd ? (botControls ? bot.lastKmh : P.lastServeKmh) : ai.lastKmh) : (youEnd ? (botControls ? bot.lastKmh : P.lastKmh) : ai.lastKmh);
                    float eq = youEnd ? (botControls ? bot.lastQ : P.lastContact.quality) : ai.lastQ;
                    MLogPoint(mlPoint, &mlBefore, server, serveNo, rallyHits, winner, ek, hitter, etn, ekmh, eq,
                              (!youEnd && endKind <= 2) ? youMinDist : -1.0f, gPointImportance, ai.persona, ai.level, style);
                    if (score.over) MLogMatchEnd(&score, ai.persona, ai.level, style);
                }
                // ---- ハイライトの候補：長いラリー・ウィナー・エース・大事なポイント・ラケット破壊・試合の最後 ----
                {
                    const char *who = winner == 0 ? YouName() : OPP_SHORT;
                    const char *big = gPointImportance >= 1.0f ? TL("BIG POINT  -  ", "大事なポイント  -  ") : "";
                    if (ai.anim.smashT > 0.0f)  snprintf(hlCap, sizeof(hlCap), TL("%s SMASHES THE RACKET", "%s がラケットを叩きつける"), OPP_SHORT);
                    else if (endKind == 1)      snprintf(hlCap, sizeof(hlCap), TL("%sACE  %s  %.0f km/h", "%sエース  %s  %.0f km/h"), big, who, hitter == 0 ? P.lastServeKmh : ai.lastKmh);
                    else if (endKind == 2)      snprintf(hlCap, sizeof(hlCap), rallyHits >= 8 ? TL("%s%s WINNER  (%d-shot rally)", "%s%s のウィナー（%d 本のラリー）") : TL("%s%s WINNER", "%s%s のウィナー"), big, who, rallyHits);
                    else if (rallyHits >= 10)   snprintf(hlCap, sizeof(hlCap), TL("%s%d-SHOT RALLY  -  point %s", "%s%d 本のラリー  -  %s のポイント"), big, rallyHits, who);
                    else                        snprintf(hlCap, sizeof(hlCap), TL("%sPOINT %s", "%s%s のポイント"), big, who);
                    if (score.over) snprintf(hlCap, sizeof(hlCap), TL("MATCH POINT  -  %s WINS", "マッチポイント  -  %s の勝ち"), who);
                    hlScore = 0.5f * rallyHits + (endKind == 2 ? 3.0f : 0.0f) + (endKind == 1 ? 2.0f : 0.0f) + 4.0f * gPointImportance
                            + (ai.anim.celebT > 0.0f ? 1.5f : 0.0f) + (ai.anim.smashT > 0.0f ? 6.0f : 0.0f) + (score.over ? 8.0f : 0.0f)
                            + RandRange(0.0f, 0.5f);
                    hlPending = true;
                }
            }
        } else if (mode == MODE_MATCH && (phase == PH_BETWEEN || phase == PH_POINT_OVER)) {
            // R：今のポイントをスローで見直す。試合が終わったら、ハイライトを続けて見る
            if (ActPressed(ACT_REPLAY)) {
                if (score.over && phaseTimer <= 0.0f) {
                    if (hlPending) { RecSaveHighlight(pointStart, hlCap, hlScore); hlPending = false; }
                    if (HighlightCount() > 0) { rp.on = true; rp.mode = 1; rp.clip = 0; rp.idx = 0; rp.t = rp.acc = 0.0f; rp.first = true; }
                } else if (RecPrepareLast(pointStart) > 20) {
                    rp.on = true; rp.mode = 0; rp.idx = 0; rp.t = rp.acc = 0.0f; rp.first = true;
                }
            }
            phaseTimer -= simDt;
            if (ai.anim.smashT > 0.0f) {
                // ラケットを叩きつけている間は動かない。地面に当たるたびに音、1 回目でラケットが曲がる
                float e0 = smashPrevE, e1 = SMASH_TIME - fmaxf(ai.anim.smashT - simDt, 0.0f);
                const float hits[SMASH_NHITS] = SMASH_HITS;
                for (int k = 0; k < SMASH_NHITS; k++) if (e0 < hits[k] && e1 >= hits[k]) SfxRacketSmash(Vector3Distance(cam.position, ai.pos), k);
                if ((e0 < hits[0] && e1 >= hits[0]) || (e0 < hits[3] && e1 >= hits[3])) SfxBoo(ai.smashes >= SMASH_PENALTY_FROM ? 1.0f : 0.75f, 0.35f);   // 観客のブーイング（長いので途中でもう一度）
                ai.anim.broken = Clamp01((e1 - 0.55f) / 4.9f);   // 叩くたびに曲がっていく
                smashPrevE = e1;
                ai.anim.smashT = fmaxf(ai.anim.smashT - simDt, 0.0f);
                ai.vel = (Vector3){ 0 };
                ai.anim.velX = ai.anim.velZ = 0.0f;
            }
            // ---- E：観客をあおる（ポイントの間）。取ったポイントの後なら観客が沸いて少し燃える。やりすぎる・取られた後だとブーイング ----
            if (ActPressed(ACT_TAUNT) && !score.over && P.anim.smashT <= 0.0f && P.anim.celebT <= 0.0f && !botControls) {
                static const int TK[] = { CELEB_COMEON, CELEB_FIST, CELEB_JUMP_FIST, CELEB_ARMS_UP, CELEB_ROAR, CELEB_EAR };
                int nk = SALE_ONLY ? 5 : 6;   // 売る版は特定の選手を思わせるしぐさ（耳に手）なし
                int kind = TK[tauntIdx++ % nk];
                P.anim.celebKind = kind; P.anim.celebDur = CelebDuration(kind); P.anim.celebT = P.anim.celebDur;
                P.anim.swingT = -1.0f; P.anim.windup = 0.0f;
                phaseTimer = fmaxf(phaseTimer, P.anim.celebDur + 0.3f);
                SfxCelebrate(gYouPersona ? gYouPersona : -1, Vector3Distance(cam.position, P.pos));
                if (tauntGame != score.nGames) { tauntGame = score.nGames; taunts = 0; }
                taunts++;
                if (lastPtWinner == 0 && taunts <= 2) {   // 観客が応えて沸く。少し燃える
                    crowdExcite = fmaxf(crowdExcite, 0.9f); SfxPointEnd(9, true);
                    gYouRage = fmaxf(gYouRage, 0.35f);
                    snprintf(toast, sizeof(toast), "%s", TL("The crowd is behind you!", "観客が味方についた！")); toastT = 2.0f;
                } else {                                  // 取られた後・やりすぎ：ブーイング
                    SfxBoo(0.45f, 0.3f);
                    snprintf(toast, sizeof(toast), "%s", lastPtWinner == 0 ? TL("Too much. The crowd boos.", "やりすぎ。観客がブーイング") : TL("You just lost the point... the crowd boos.", "取られた後に…観客がブーイング")); toastT = 2.0f;
                }
            }
            // ---- Y：「カモン！」と拳を握って喜ぶ（ポイントの間、何回でも）。ブーイングやペナルティはなし。取った直後なら観客が少し沸く ----
            if (ActPressed(ACT_COMEON) && P.anim.smashT <= 0.0f && P.anim.celebT <= 0.0f && !botControls) {
                P.anim.celebKind = CELEB_COMEON; P.anim.celebDur = CelebDuration(CELEB_COMEON); P.anim.celebT = P.anim.celebDur;
                P.anim.swingT = -1.0f; P.anim.windup = 0.0f;
                phaseTimer = fmaxf(phaseTimer, P.anim.celebDur * 0.6f);
                SfxCelebrate(gYouPersona ? gYouPersona : -1, Vector3Distance(cam.position, P.pos));
                if (lastPtWinner == 0) crowdExcite = fmaxf(crowdExcite, 0.55f);
            }
            // ---- Z：球を観客席へかっ飛ばす（ポイントの間）。行為違反（ラケット破壊と同じ数え方）。燃えるが、ラケット破壊より弱い ----
            if ((ActPressed(ACT_SWAT) || ActPressed(ACT_SMASH)) && gYouPersona == PERSONA_NADAR && !score.over && !botControls) {   // ナダル：感情をコントロールできるので、叩きつけない・かっ飛ばさない
                snprintf(toast, sizeof(toast), "%s", TL("Stays calm. Never smashes a racket.", "感情をコントロールしている。ラケットは壊さない")); toastT = 2.2f;
            } else if (ActPressed(ACT_SWAT) && !score.over && P.anim.smashT <= 0.0f && P.anim.celebT <= 0.0f && !botControls) {
                youSmashes++;
                P.anim.celebKind = CELEB_SWAT; P.anim.celebDur = CelebDuration(CELEB_SWAT); P.anim.celebT = P.anim.celebDur;
                P.anim.swingT = -1.0f; P.anim.windup = 0.0f;
                youSwatPrevE = 0.0f; swatCatchT = 2.4f;
                phaseTimer = fmaxf(phaseTimer, P.anim.celebDur + 2.0f);
                gYouRage = fmaxf(gYouRage, 0.6f);
                const char *nm = gYouPersona ? PERSONAS[gYouPersona].shortName : "YOU";
                if (youSmashes < SMASH_PENALTY_FROM)
                    snprintf(rageMsg, sizeof(rageMsg), TL("%s %s THE BALL INTO THE CROWD!\nCode violation, ball abuse. %s\nFIRED UP: faster and harder for a few points, but more errors", "%s %sが球を観客席へかっ飛ばした！\n行為違反、ボールの乱用。%s\n燃えた：数ポイントの間、速く強くなる。ミスも増える"),
                             nm, gYouPersona ? "SWATS" : "SWAT", youSmashes == SMASH_PENALTY_FROM - 1 ? TL("Final warning.", "最後の警告") : TL("Warning.", "警告"));
                else {
                    AwardPoint(&score, 1);
                    snprintf(rageMsg, sizeof(rageMsg), TL("%s %s THE BALL INTO THE CROWD!\nCode violation, ball abuse. Point penalty.\nFIRED UP: faster and harder for a few points, but more errors", "%s %sが球を観客席へかっ飛ばした！\n行為違反、ボールの乱用。ポイントペナルティ\n燃えた：数ポイントの間、速く強くなる。ミスも増える"), nm, gYouPersona ? "SWATS" : "SWAT");
                }
                rageMsgT = P.anim.celebDur + 2.5f;
                SfxAngry(gYouPersona ? gYouPersona : -1, Vector3Distance(cam.position, P.pos));
            }
            if (P.anim.celebKind == CELEB_SWAT && P.anim.celebT > 0.0f) {   // かっ飛ばす音、ブーイング、観客席の人が捕って沸く
                float e0 = youSwatPrevE, e1 = P.anim.celebDur - fmaxf(P.anim.celebT - simDt, 0.0f);
                float dist = Vector3Distance(cam.position, P.pos);
                if (e0 < 0.68f && e1 >= 0.68f) { SfxHit(0.95f, 150.0f, false, false, false, dist); SfxBoo(0.5f, 0.5f); }
                youSwatPrevE = e1;
            }
            // ---- X：自分がラケットを叩きつける（ポイントの間だけ）。観客にブーイングされるが、しばらく「燃えて」強くなる（ミスも増える）。
            //      1・2 回目は警告だけ、SMASH_PENALTY_FROM 回目からポイントペナルティ ----
            if (ActPressed(ACT_SMASH) && gYouPersona != PERSONA_NADAR && !score.over && P.anim.smashT <= 0.0f && !botControls) {
                youSmashes++;
                P.anim.smashT = SMASH_TIME; P.anim.swingT = -1.0f; P.anim.windup = 0.0f; P.anim.celebT = 0.0f;
                youSmashPrevE = 0.0f;
                phaseTimer = fmaxf(phaseTimer, SMASH_TIME + 1.2f);
                const char *nm = gYouPersona ? PERSONAS[gYouPersona].shortName : "YOU";   // テロップは英語
                gYouRage = 1.0f;   // 燃える
                if (youSmashes < SMASH_PENALTY_FROM) {
                    snprintf(rageMsg, sizeof(rageMsg), TL("%s %s THE RACKET!\nCode violation, racket abuse. %s\nFIRED UP: faster and harder for a few points, but more errors", "%s %sがラケットを叩きつけた！\n行為違反、ラケットの乱用。%s\n燃えた：数ポイントの間、速く強くなる。ミスも増える"),
                             nm, gYouPersona ? "SMASHES" : "SMASH", youSmashes == SMASH_PENALTY_FROM - 1 ? TL("Final warning.", "最後の警告") : TL("Warning.", "警告"));
                } else {
                    AwardPoint(&score, 1);   // ポイントペナルティ：相手のポイント
                    snprintf(rageMsg, sizeof(rageMsg), TL("%s %s THE RACKET!\nCode violation, racket abuse. Point penalty.\nFIRED UP: faster and harder for a few points, but more errors", "%s %sがラケットを叩きつけた！\n行為違反、ラケットの乱用。ポイントペナルティ\n燃えた：数ポイントの間、速く強くなる。ミスも増える"), nm, gYouPersona ? "SMASHES" : "SMASH");
                }
                rageMsgT = SMASH_TIME + 2.5f;
                SfxAngry(gYouPersona ? gYouPersona : -1, Vector3Distance(cam.position, P.pos));
            }
            if (P.anim.smashT > 0.0f) {   // 叩きつけている間：地面に当たるたびに音、ラケットが曲がる
                float e0 = youSmashPrevE, e1 = SMASH_TIME - fmaxf(P.anim.smashT - simDt, 0.0f);
                const float hits[SMASH_NHITS] = SMASH_HITS;
                for (int k = 0; k < SMASH_NHITS; k++) if (e0 < hits[k] && e1 >= hits[k]) SfxRacketSmash(Vector3Distance(cam.position, P.pos), k);
                if ((e0 < hits[0] && e1 >= hits[0]) || (e0 < hits[3] && e1 >= hits[3])) SfxBoo(youSmashes >= SMASH_PENALTY_FROM ? 1.0f : 0.75f, 0.35f);
                P.anim.broken = Clamp01((e1 - 0.55f) / 4.9f);
                youSmashPrevE = e1;
                P.anim.smashT = fmaxf(P.anim.smashT - simDt, 0.0f);
                P.vel = (Vector3){ 0 };
            }
            if (P.anim.celebT > 0.0f)   P.anim.celebT = fmaxf(P.anim.celebT - simDt, 0.0f);
            if (bot.anim.celebT > 0.0f) bot.anim.celebT = fmaxf(bot.anim.celebT - simDt, 0.0f);
            if (ai.anim.smashT <= 0.0f && ai.anim.celebT > 0.0f) {
                // 喜ぶ・怒る間はその場で
                {   // 怒りのしぐさの音：投げたラケットが地面に当たる、球をかっ飛ばす
                    float e0 = ai.anim.celebDur - ai.anim.celebT, e1 = e0 + simDt;
                    float dist = Vector3Distance(cam.position, ai.pos);
                    if (ai.anim.celebKind == CELEB_THROW && e0 < 0.97f && e1 >= 0.97f) { SfxRacketLand(dist); SfxBoo(0.6f, 0.3f); }
                    if (ai.anim.celebKind == CELEB_SWAT && e0 < 0.68f && e1 >= 0.68f) { SfxHit(0.95f, 150.0f, false, false, false, dist); SfxBoo(0.5f, 0.5f); }
                    if (ai.anim.celebKind == CELEB_RANT && ai.anim.walkT > 0.0f && e0 < 1.2f && e1 >= 1.2f) SfxBoo(0.35f, 0.0f);   // 審判に詰め寄る：小さなブーイング
                }
                ai.anim.celebT = fmaxf(ai.anim.celebT - simDt, 0.0f);
                ai.vel = (Vector3){ 0 };
                ai.anim.velX = ai.anim.velZ = 0.0f;
                if (ai.anim.celebKind == CELEB_RANT && ai.anim.walkT > 0.0f && ai.anim.celebDur - ai.anim.celebT < ai.anim.walkT) {
                    // 審判の方へ歩く
                    Vector3 d = Vector3Subtract(protestTo, ai.pos); d.y = 0.0f;
                    float dl = Vector3Length(d);
                    if (dl > 0.05f) {
                        ai.vel = Vector3Scale(d, fminf(PROTEST_WALK, dl / simDt) / dl);
                        ai.pos = Vector3Add(ai.pos, Vector3Scale(ai.vel, simDt));
                        ai.anim.velX = -ai.vel.x; ai.anim.velZ = ai.vel.z;   // AI は +z 向き
                        ai.anim.runPhase += Vector3Length(ai.vel) * simDt * 2.2f;
                    }
                }
            }
            else if (phase == PH_POINT_OVER && ai.anim.smashT <= 0.0f) AiUpdate(&ai, &ball, simDt);   // 打ち終わったら戻る
            if (phaseTimer <= 0.0f && setBreak) {
                // セット間：統計を出して待つ（オートパイロットなら 6 秒で進む）
                summaryT += frameDt;
                scoreCall[0] = 0; pointMsg = "";
                if (IsKeyPressed(KEY_ENTER) || IsMouseButtonPressed(MOUSE_BUTTON_LEFT) || (autopilot && summaryT > 6.0f))
                    setBreak = false;
            }
            if (phaseTimer <= 0.0f && !setBreak && changePending && !changeActive) {   // コートチェンジを始める
                changePending = false; changeActive = true; changeFlipped = false;
                changeT = 0.0f; changeDur = changeSit ? 7.0f : 3.4f;
                coFromP = P.pos; coFromA = ai.pos;
                TraceLog(LOG_INFO, "CHANGEOVER start (sit=%d)", changeSit);
                pointMsg = TL("Changeover", "チェンジコート"); scoreCall[0] = 0;
            }
            if (phaseTimer <= 0.0f && !setBreak && !changeActive) {
                if (phase == PH_POINT_OVER) serveNo = 1;
                needSetup = true;
                pointMsg = "";
                scoreCall[0] = 0;
            }
        }

        // 自分の打球の結果を集計
        if (mode != MODE_SERVE && ball.lastHitter == HITTER_PLAYER && !scoredLast && ball.result != RESULT_NONE) {
            if (ball.result == RESULT_IN) stats[P.lastType][1]++;
            scoredLast = true;
        }

        // ---- リターン練習：試合の AI のサーブと同じ流れ（儀式 → トス → サーブ。フォルトならセカンド）。リターンが落ちたらポイントが決まる ----
        //   意図して試合と違えている所：ラリーにはしない（相手は打ち返さない。リターンだけを繰り返し練習するため）、観客はいない
        if (mode == MODE_RETURN) {
            if (retPhase == RET_LIVE) {
                bool srv = ball.lastHitter == HITTER_OPP;   // まだサーブのまま（自分が触っていない）
                int res = 0;                                // 1 = フォルト、2 = レット、3 = サーバーのポイント、4 = 自分のポイント
                if (srv) {
                    if (ball.hitNet || (ball.bounces >= 1 && ball.result != RESULT_IN)) res = 1;
                    else if (ball.bounces >= 1 && ball.netCord) res = 2;
                    else if (ball.bounces >= 2 || !ball.active) res = 3;   // 返せなかった
                } else {
                    if (ball.hitNet) res = 3;
                    else if (ball.bounces >= 1) res = ball.result == RESULT_IN ? 4 : 3;
                    else if (!ball.active) res = 3;
                }
                if (res == 2) {   // レット：同じ番目のサーブを打ち直す
                    pointMsg = TL("Let", "レット");
                    retPhase = RET_WAIT; feedTimer = 1.5f;
                } else if (res == 1 && serveNo == 1) {   // フォルト：セカンドサーブへ（試合と同じコールと間）
                    serveNo = 2;
                    pointMsg = ball.hitNet ? TL("Fault (net)", "フォルト（ネット）") : TL("Fault", "フォルト");
                    if (!ball.hitNet && ball.result == RESULT_OUT) { snprintf(outMsg, sizeof(outMsg), TL("Fault (OUT %.0fcm)", "フォルト（アウト %.0fcm）"), fmaxf(ball.outBy * 100.0f, 1.0f)); pointMsg = outMsg; }
                    SfxCallFault();
                    retPhase = RET_WAIT; feedTimer = 1.5f;
                } else if (res != 0) {
                    // ポイントが決まった。試合と同じ声を同じ間で（フォルト・アウトのコールはすぐ、主審のスコアコールは 1.1 秒後）
                    static char retMsg[96];
                    int winner = (res == 4 || res == 1) ? 0 : 1;   // リターンが入った・ダブルフォールトは自分、入らない・返せないはサーバー
                    if (res == 1) { pointMsg = TL("Double fault", "ダブルフォールト"); SfxCallFault(); }
                    else if (res == 4) { snprintf(retMsg, sizeof(retMsg), TL("%s return IN", "%s リターンが入った"), YouName()); pointMsg = retMsg; }
                    else if (srv) { snprintf(retMsg, sizeof(retMsg), TL("%s ace / unreturned serve", "%sのサービスエース"), gOppName); pointMsg = retMsg; }
                    else if (ball.result == RESULT_OUT && !ball.hitNet) {
                        SfxCallOut();
                        snprintf(retMsg, sizeof(retMsg), TL("%s error (OUT %.0fcm)", "%sのミス（アウト %.0fcm）"), YouName(), fmaxf(ball.outBy * 100.0f, 1.0f)); pointMsg = retMsg;
                    } else { snprintf(retMsg, sizeof(retMsg), TL("%s error (net)", "%sのミス（ネット）"), YouName()); pointMsg = retMsg; }
                    int gamesBefore = retScore.games[0] + retScore.games[1];
                    AwardPoint(&retScore, winner);
                    bool gameEnded = retScore.games[0] + retScore.games[1] != gamesBefore;
                    if (gameEnded) snprintf(scoreCall, sizeof(scoreCall), TL("GAME %s", "ゲーム %s"), winner == 0 ? YouName() : OPP_SHORT);
                    else           snprintf(scoreCall, sizeof(scoreCall), "%s", ScoreText(&retScore));
                    SfxScoreCall(&retScore, 1, winner, gameEnded ? 1 : 0, 1.1f);
                    if (gameEnded) retScore = (Score){ .server = 1 };   // 次のゲームも相手のサーブ（セットは数えない）
                    serveNo = 1;
                    retPhase = RET_WAIT; feedTimer = RETURN_POINT_GAP;
                }
            } else if (retPhase == RET_WAIT) {
                feedTimer -= frameDt;
                if (feedTimer <= 0.0f) {
                    // 次のサーブの準備（試合のポイントの準備と同じ）：相手はサーブの位置、自分はリターンの位置へ。相手は儀式（ボールつき）を始める
                    bool deuce = DeuceCourt(&retScore);
                    pointMsg = ""; scoreCall[0] = 0;
                    gPointImportance = PointImportance(&retScore); gDecidingSet = false;
                    ball = (Ball){ 0 }; trailCount = 0;
                    ai.planned = false; ai.leaving = false; ai.swung = false; ai.watching = HITTER_NONE; ai.netRush = false;
                    ai.vel = (Vector3){ 0 }; ai.downTimer = 0.0f; ai.sliding = false;
                    ai.anim = (SwingAnim){ .swingT = -1.0f };
                    ai.pos = Rot180((Vector3){ deuce ? 0.6f : -0.6f, 0.0f, HALF_LEN + 0.15f });
                    ai.standPos = ai.pos;
                    P.pos = HumanReturnPos(deuce, serveNo == 2); P.vel = (Vector3){ 0 };
                    P.anim = (SwingAnim){ .swingT = -1.0f };
                    P.downT = 0.0f; P.recoverT = 0.0f; P.sliding = false; P.chargingShot = -1;
                    phaseTimer = AiServePrep(&ai, serveNo);
                    aiTossing = false;
                    retPhase = RET_SERVE;
                }
            } else if (ai.anim.celebT > 0.0f) ai.anim.celebT = fmaxf(ai.anim.celebT - simDt, 0.0f);   // RET_SERVE：儀式（ボールつき）を進める。トスとサーブは上の「AI のサーブ」で
            if (ai.anim.swingT >= 0.0f) ai.anim.swingT = fminf(ai.anim.swingT + simDt, 5.0f);   // サーブの振りを進める
        }
        // 次の球出し（ラリー練習）：打球が落ちた / 打ち損ねて2バウンド / 止まった
        if (mode == MODE_RALLY) {
            bool rallyOver = (!ball.active || ball.hitNet || ball.bounces >= 2 || (ball.bounces >= 1 && ball.result != RESULT_IN && ball.result != RESULT_NONE));   // 相手も打ち返すので、決まるかミスするまで続く
            if (rallyOver) {
                feedTimer -= frameDt;
                // 相手（球出し）：先に次の球を決めて、その球を打つ位置へ動き、振り始める。当たる瞬間に球を出す
                if (!feedSwing && feedTimer <= SWING_LEAD) {
                    feedNext = MachineFeed(proFeed);
                    feedSwing = true;
                    ai.anim = (SwingAnim){ .swingT = 0.0f };
                    // ストローク：フォアで打つ（球は体の右＝世界の -x 側）
                    feederPos = (Vector3){ feedNext.pos.x + 0.70f, 0.0f, feedNext.pos.z - 0.30f };
                    ai.anim.lastContact = (Contact){ .forehand = true, .quality = 0.8f, .height = feedNext.pos.y };
                    ai.anim.contactLocal = (Vector3){ 0.70f, feedNext.pos.y, 0.30f };
                    ai.anim.slice = feedNext.spin.x < 0.0f;
                    ai.anim.swingT = fmaxf(0.0f, SWING_LEAD - feedTimer);
                    ai.pos = feederPos;
                }
                if (feedTimer <= 0.0f) {
                    ball = feedSwing ? feedNext : MachineFeed(proFeed);
                    ai.planned = false; ai.leaving = false; ai.swung = false; ai.watching = HITTER_NONE; ai.netRush = false;   // 相手：次の球に備える
                    ai.downTimer = 0.0f; ai.vel = (Vector3){ 0 };
                    feedSwing = false;
                    trailColor = Fade(LIGHTGRAY, 0.6f);
                    trailCount = 0;
                    feedTimer = 1.2f;
                    P.anim.splitT = SPLIT_TIME;
                }
            }
            if (ai.anim.swingT >= 0.0f) ai.anim.swingT = fminf(ai.anim.swingT + simDt, 5.0f);   // 球出しの振りを進める
        }

        // ---- コートチェンジの動き（ほかの動きの後で位置を上書きする） ----
        if (changeActive) {
            changeT += frameDt;
            if (IsKeyPressed(KEY_ENTER) || IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) changeT = fmaxf(changeT, changeDur);   // 飛ばす
            float mid = changeDur * 0.5f;
            if (!changeFlipped && changeT >= mid) { changeFlipped = true; gUmpSide = -gUmpSide; }   // 暗い間に入れ替わる
            Vector3 benchP = { gUmpSide * (NET_POST_X + 0.9f), 0.0f, 2.3f }, benchA = { gUmpSide * (NET_POST_X + 0.9f), 0.0f, -2.3f };
            Vector3 tp, ta; float k;
            if (changeT < 1.6f) { k = Smooth01(changeT / 1.6f); tp = Vector3Lerp(coFromP, benchP, k); ta = Vector3Lerp(coFromA, benchA, k); }
            else if (changeT < changeDur - 1.6f) { tp = benchP; ta = benchA; }
            else { k = Smooth01((changeT - (changeDur - 1.6f)) / 1.6f); tp = Vector3Lerp(benchP, coFromP, k); ta = Vector3Lerp(benchA, coFromA, k); }
            if (frameDt > 0.0f) {
                P.vel = Vector3Scale(Vector3Subtract(tp, P.pos), 1.0f / frameDt); ai.vel = Vector3Scale(Vector3Subtract(ta, ai.pos), 1.0f / frameDt);
                if (Vector3Length(P.vel) > 8.0f) P.vel = (Vector3){ 0 };   // 暗い間の入れ替わりで跳んだときは走らせない
                if (Vector3Length(ai.vel) > 8.0f) ai.vel = (Vector3){ 0 };
            }
            P.pos = tp; ai.pos = ta;
            P.anim.runPhase += Vector3Length(P.vel) * frameDt * 2.2f; P.anim.velX = P.vel.x; P.anim.velZ = -P.vel.z;
            ai.anim.runPhase += Vector3Length(ai.vel) * frameDt * 2.2f; ai.anim.velX = -ai.vel.x; ai.anim.velZ = ai.vel.z;
            ball.active = false; ball.bounces = 0;
            if (changeT >= changeDur) {
                changeActive = false;
                P.vel = ai.vel = (Vector3){ 0 };
                if (phase == PH_POINT_OVER) serveNo = 1;
                needSetup = true; pointMsg = ""; scoreCall[0] = 0;
            }
        }

        // ---- はじめての人への練習：できたら次へ ----
        if (tut >= 0) {
            tutT += frameDt;
            int inNow = 0; for (int k = 0; k < SHOT_COUNT; k++) inNow += stats[k][1];
            int svIn = svStats[0][1] + svStats[1][1];
            if (tut == 0) {   // 動く：光る円の中へ（その間は球を出さない）
                feedTimer = fmaxf(feedTimer, 1.0f);
                if (Vector3Distance((Vector3){ P.pos.x, 0, P.pos.z }, TUT_SPOT) < 0.8f) { tut = 1; tutT = 0.0f; tutBase = inNow; feedTimer = 0.8f; }
            } else if (tut == 1) {   // 打つ：3 球入れる
                if (inNow - tutBase >= 3) { tut = 2; tutT = 0.0f; tutL = tutR = false; tutLastShot = ball.shotId; }
            } else if (tut == 2) {   // 狙う：左と右に 1 球ずつ
                if (ball.active && ball.lastHitter == HITTER_PLAYER && ball.bounces >= 1 && ball.result == RESULT_IN && ball.shotId != tutLastShot) {
                    tutLastShot = ball.shotId;
                    if (ball.firstBounce.x < -0.6f) tutL = true;
                    if (ball.firstBounce.x > 0.6f)  tutR = true;
                }
                if (tutL && tutR) { tut = 3; tutT = 0.0f; mode = MODE_SERVE; forceRestart = true; tutBase = svIn; }
            } else if (tut == 3) {   // サーブ：2 本入れる
                if (mode != MODE_SERVE) { mode = MODE_SERVE; forceRestart = true; }
                if (svIn - tutBase >= 2) { tut = 4; tutT = 0.0f; }
            } else if (tut == 4 && tutT > 4.0f) {   // 終わり：タイトルへ（次は試合）
                tut = -1; mode = MODE_MATCH; app = APP_TITLE; menuSel = 0;
                mouseLocked = false; EnableCursor();
            }
        }
        // ---- 撮影モード（F3）：押すたびにカメラが変わる。画面の文字や印は出さない ----
        if (IsKeyPressed(KEY_F3)) { cineMode = (cineMode + 1) % (CINE_COUNT + 1); cineLabelT = 1.5f; cineFirst = true; }

        // ---- カメラ ----
        if (changeActive) {        // コートチェンジ：審判台の反対側の高いところから（中継のように）
            cam.position = (Vector3){ -gUmpSide * 15.0f, 5.5f, 0.0f };
            cam.target   = (Vector3){ gUmpSide * 2.5f, 0.6f, 0.0f };
        } else if (camMode == 0) {        // プレイヤーの後ろ
            // 少し高く後ろから：上から見る角度があると、奥行き（ボールまでの距離）が読みやすい
            cam.position = (Vector3){ P.pos.x * 0.6f, 4.6f, P.pos.z + 7.5f };
            cam.target   = (Vector3){ P.pos.x * 0.3f, 0.3f, P.pos.z - 7.0f };
        } else if (camMode == 1) { // 真横（弾道が一番わかる）
            cam.position = (Vector3){ 22.0f, 2.5f, 0.0f };
            cam.target   = (Vector3){ 0.0f, 1.0f, 0.0f };
        } else {                   // ボール追従
            cam.target   = ball.pos;
            cam.position = Vector3Add(ball.pos, (Vector3){ 4.0f, 2.0f, 6.0f });
        }

        if (cineMode > 0 && !changeActive) {   // 撮影モードのカメラ（なめらかに追う）
            Vector3 mid = Vector3Scale(Vector3Add(P.pos, ai.pos), 0.5f);
            Vector3 focus = ball.active ? ball.pos : (Vector3){ mid.x, 1.0f, mid.z };
            Vector3 wp, wt;
            float t = (float)GetTime();
            switch (cineMode) {
                case 1:  wp = (Vector3){ P.pos.x * 0.4f + 4.5f, 1.5f, P.pos.z + 5.5f }; wt = (Vector3){ mid.x * 0.5f, 1.0f, mid.z - 2.0f }; break;   // 中継のように、低めで少し横から
                case 2: { float side = -(float)gUmpSide, zc = 0.5f * focus.z + 0.5f * mid.z;                                                           // コートの外、真横の低い位置から
                          wp = (Vector3){ side * (DOUBLES_W * 0.5f + 3.2f), 1.25f, zc }; wt = (Vector3){ 0.0f, 1.0f, zc }; } break;
                case 3:  wp = Vector3Add(P.pos, (Vector3){ 0.9f, 1.5f, 2.4f }); { bool ahead = ball.active && ball.pos.z < P.pos.z - 1.0f && ball.pos.y < 6.0f;           // 自分のすぐ後ろ。球が前にあるときだけ球を見る
                           Vector3 opp = Vector3Add(ai.pos, (Vector3){ 0, 1.0f, 0 });
                           wt = ahead ? Vector3Lerp(opp, ball.pos, 0.5f) : opp; } break;
                default: wp = Vector3Add(P.pos, (Vector3){ 4.0f * sinf(t * 0.25f), 1.2f, 4.0f * cosf(t * 0.25f) }); wt = Vector3Add(P.pos, (Vector3){ 0, 1.1f, 0 }); break;   // 自分のまわりをゆっくり回る
            }
            float k = cineFirst ? 1.0f : fminf(frameDt * 3.0f, 1.0f);
            cinePos = Vector3Lerp(cinePos, wp, k); cineTgt = Vector3Lerp(cineTgt, wt, k * 1.5f > 1.0f ? 1.0f : k * 1.5f);
            cineFirst = false;
            cam.position = cinePos; cam.target = cineTgt;
        }
        int keepGuide = guideLevel, keepAssist = assistLevel;
        if (cineMode > 0) { guideLevel = 2; assistLevel = 0; }   // 撮影モード：狙いの印や手がかりの円も消す（描く間だけ）

        // ---- 描画 ----
        UpdateSpeedBoard(speedBoardKmh);   // スピードメーター（数字が変わったときだけ描き直す）
        BeginDrawing();
        PixelBegin(); DrawSky(cam);   // 空（時間帯と天気）
        BeginMode3D(cam);
            DrawCourt();
            if (mode == MODE_MATCH) DrawStadium(cam.position, ball.pos, crowdExcite, (float)GetTime(), lineCallPos, lineCallT);
            if (mode == MODE_MATCH) DrawPigeon();
            if (tut == 0) {   // 練習：ここへ動く（光る円）
                float pu = 0.5f + 0.5f * sinf((float)GetTime() * 5.0f);
                DrawCylinder(TUT_SPOT, 0.7f, 0.7f, 0.02f, 32, Fade((Color){ 255, 215, 60, 255 }, 0.35f + 0.3f * pu));
                DrawCylinderWires(TUT_SPOT, 0.7f + 0.1f * pu, 0.7f + 0.1f * pu, 1.2f, 24, Fade((Color){ 255, 230, 120, 255 }, 0.6f));
            }

            // プレイヤー（-z 向き）と AI（+z 向き）
            ApplyLook(&P.anim, gYouPersona ? gYouPersona : -1); ApplyLook(&bot.anim, gYouPersona ? gYouPersona : -1);   // 自分が使う選手の見た目
            P.anim.lefty = bot.anim.lefty = PERSONAS[gYouPersona].lefty;
            P.anim.oneHanded = bot.anim.oneHanded = PERSONAS[gYouPersona].oneHanded;
            Color youShirt = gYouPersona ? PersonaShirt(gYouPersona) : (Color){ 240, 240, 245, 255 };
            // サーブの構え（トスの前）：横向きに立ち、左手にボール
            P.anim.serveStance = P.anim.holdBall = playerServing && P.sv == SV_READY && P.anim.celebT <= 0.0f;   // ボールをついている間は構えない
            bot.anim.serveStance = bot.anim.holdBall = false;
            ai.anim.serveStance = ai.anim.holdBall = ((mode == MODE_MATCH && phase == PH_SERVE && server == 1 && !score.over) || (mode == MODE_RETURN && retPhase == RET_SERVE)) && !aiTossing && ai.anim.celebT <= 0.0f;
            DrawFigure(P.pos, true, youShirt, botControls ? &bot.anim : &P.anim,
                       Vector3Length(P.vel));
            if (mode == MODE_RALLY || mode == MODE_RETURN) {   // 練習：球を出す相手を描く（選んでいる相手の選手）
                ai.anim.lefty = PERSONAS[ai.persona].lefty;   // 選手の利き手で（リターン練習のサーブも、試合と同じく左利きは左で打つ）
                ai.anim.oneHanded = PERSONAS[ai.persona].oneHanded;
                ApplyLook(&ai.anim, ai.persona);
                DrawFigure(ai.pos, false, PersonaShirt(ai.persona), &ai.anim, mode == MODE_RALLY ? Vector3Length(ai.vel) : 0.0f);   // リターン練習のトスは本物の球（試合と同じ）
            }
            if (mode == MODE_MATCH)
            {
                ai.anim.lefty = PERSONAS[ai.persona].lefty;          // 構えやポイントの間も、いつも選手の利き手で描く
                ai.anim.oneHanded = PERSONAS[ai.persona].oneHanded;
                ApplyLook(&ai.anim, ai.persona);
                {   // マレー：ポイントの間は背中を丸めてうつむいて歩く
                    float want = (ai.persona == PERSONA_MURRAYE && (phase == PH_POINT_OVER || phase == PH_BETWEEN) && ai.anim.celebT <= 0.0f) ? 1.0f : 0.0f;
                    ai.anim.slouch += (want - ai.anim.slouch) * Clamp01(frameDt * 3.0f);
                }
                DrawFigure(ai.pos, false, PersonaShirt(ai.persona), &ai.anim, Vector3Length(ai.vel));
            }

            if (playerServing && guideLevel <= 1) {
                // 狙うサービスボックスを薄く塗る
                float x0, x1;
                ServiceBox(P.pos.x, -1, &x0, &x1);
                DrawCube((Vector3){ 0.5f * (x0 + x1), 0.003f, -SERVICE_Z * 0.5f }, x1 - x0, 0.002f, SERVICE_Z,
                         Fade(YELLOW, 0.15f));
                // 理想の打点
                DrawSphereWires(tossIdeal, 0.12f, 6, 8, Fade(RAYWHITE, 0.8f));
            } else if (!playerServing && mode != MODE_SERVE && !botControls && guideLevel == 0) {
                // 届く範囲（地面の輪）：内側の輪が理想の距離、外側が届く限界
                DrawCircle3D((Vector3){ P.pos.x, 0.01f, P.pos.z }, IDEAL_R, (Vector3){ 1, 0, 0 }, 90.0f, Fade(RAYWHITE, 0.35f));
                {   // 届く限界：溜めている球種（スライスは広い）、スライド、簡単モード、身長で広がる
                    float rr = REACH_SIDE + (P.chargingShot >= 0 ? SHOTS[P.chargingShot].reach : 0.0f)
                             + (P.sliding ? SLIDE_REACH : 0.0f) + (style == STYLE_CASUAL ? CASUAL_REACH : 0.0f) + HeightDelta(gYouPersona) * REACH_PER_CM;
                    DrawCircle3D((Vector3){ P.pos.x, 0.01f, P.pos.z }, rr, (Vector3){ 1, 0, 0 }, 90.0f, Fade(RAYWHITE, 0.18f));
                    // その外のごく薄い輪：足と腕を伸ばしきって、ぎりぎり届く限界（バウンドした球。当たりは悪くなる）
                    float rs = rr + fmaxf(STRETCH_GROUND + HeightDelta(gYouPersona) * STRETCH_PER_CM, 0.10f);
                    DrawCircle3D((Vector3){ P.pos.x, 0.01f, P.pos.z }, rs, (Vector3){ 1, 0, 0 }, 90.0f, Fade(RAYWHITE, 0.08f));
                }
                // 理想の打点（フォア・バック）。溜めている球種の理想の高さに出す。頭上で構えていれば頭上
                ShotType t = (P.chargingShot >= 0) ? (ShotType)P.chargingShot : SHOT_TOP;
                Color zc = Fade(SHOTS[t].color, 0.8f);
                if (P.chargingShot >= 0 && P.anim.windStroke == 2) {
                    DrawSphereWires((Vector3){ P.pos.x + OVERHEAD_SIDE * handS, OVERHEAD_IDEAL_H, P.pos.z - OVERHEAD_FWD }, 0.15f, 6, 8, zc);
                } else {
                    float h = SHOTS[t].idealHeight;
                    DrawSphereWires((Vector3){ P.pos.x + IDEAL_SIDE, h, P.pos.z - IDEAL_FWD }, 0.15f, 6, 8, zc);
                    DrawSphereWires((Vector3){ P.pos.x - IDEAL_SIDE, h, P.pos.z - IDEAL_FWD }, 0.15f, 6, 8, zc);
                }
                // 最後に振ったとき：理想の打点（白い網）と、実際のボール（色つき）を線で結ぶ。ずれが一目で分かる
                if (P.hasLast && P.anim.swingT >= 0.0f && P.anim.swingT < 2.5f && P.lastContact.miss != MISS_NO_BALL) {
                    Color qc = P.lastContact.miss == MISS_NONE ? QualityColor(P.lastContact.quality) : RED;
                    DrawSphereWires(P.lastIdealPos, 0.15f, 6, 8, RAYWHITE);
                    DrawSphere(P.lastContactPos, 0.06f, qc);
                    DrawLine3D(P.lastIdealPos, P.lastContactPos, qc);
                    DrawLine3D((Vector3){ P.lastContactPos.x, 0.01f, P.lastContactPos.z }, P.lastContactPos, Fade(qc, 0.5f));
                }
            }

            // 手がかり：弾む点（×）、打ちやすい高さの通り道（緑）、立つ場所（緑の輪＋矢印）、タイミングの輪
            if (assistLevel > 0 && !botControls && !playerServing && mode != MODE_SERVE && P.downT <= 0.0f) {
                ShotType at = (P.chargingShot >= 0) ? (ShotType)P.chargingShot : SHOT_TOP;
                gCasual = (style == STYLE_CASUAL);
                // 予定は球ごとに一度だけ（溜める球種を変えたら、打ちやすい高さが変わるので作り直す）
                bool oppBall = ball.active && ball.lastHitter == HITTER_OPP;
                if (!oppBall) { aplan.valid = false; aplanAge = 1e9f; }
                else if (!aplan.valid || ball.age < aplanAge || (int)at != aplanType) {
                    aplan = PlanAssist(P.pos, P.vel, &ball, at);
                    aplanAge = ball.age; aplanType = (int)at;
                }
                if (oppBall) aplanAge = ball.age;
                Assist as = ComputeAssist(P.pos, P.vel, &ball, at, P.chargingShot >= 0 ? P.charge : 0.5f);
                gCasual = false; gCrouch = false;
                if (assistLevel >= 2 && aplan.valid) {
                    // 弾んだ後の通り道：打ちやすい高さのところだけ緑で太く
                    for (int k = 0; k + 1 < aplan.nPath; k++) {
                        Vector3 a0 = { aplan.path[k].x, 0.014f, aplan.path[k].z };
                        Vector3 a1 = { aplan.path[k + 1].x, 0.014f, aplan.path[k + 1].z };
                        bool g = aplan.good[k] && aplan.good[k + 1];
                        Color pc = g ? Fade(LIME, 0.9f) : Fade(LIGHTGRAY, 0.45f);
                        int nl = g ? 4 : 1;
                        for (int w = 0; w < nl; w++) {
                            float o = (w - (nl - 1) * 0.5f) * 0.035f;
                            DrawLine3D((Vector3){ a0.x + o, a0.y, a0.z }, (Vector3){ a1.x + o, a1.y, a1.z }, pc);
                        }
                    }
                    // 弾む点：まだ弾んでいない間だけ
                    if (aplan.bounceValid && ball.bounces == 0) {
                        Vector3 bp = { aplan.bounce.x, 0.016f, aplan.bounce.z };
                        Color bc = (Color){ 255, 150, 40, 230 };
                        DrawCircle3D(bp, 0.28f, (Vector3){ 1, 0, 0 }, 90.0f, bc);
                        DrawCircle3D(bp, 0.24f, (Vector3){ 1, 0, 0 }, 90.0f, bc);
                        DrawLine3D((Vector3){ bp.x - 0.2f, bp.y, bp.z - 0.2f }, (Vector3){ bp.x + 0.2f, bp.y, bp.z + 0.2f }, bc);
                        DrawLine3D((Vector3){ bp.x - 0.2f, bp.y, bp.z + 0.2f }, (Vector3){ bp.x + 0.2f, bp.y, bp.z - 0.2f }, bc);
                    }
                    if (aplan.standValid) {
                        Vector3 st = { aplan.stand.x, 0.018f, aplan.stand.z };
                        float dx = st.x - P.pos.x, dz = st.z - P.pos.z;
                        float dist = sqrtf(dx * dx + dz * dz);
                        bool there = dist < 0.35f;
                        Color sc = there ? (Color){ 60, 255, 90, 255 } : GREEN;
                        if (there) DrawCylinder((Vector3){ st.x, 0.005f, st.z }, 0.34f, 0.34f, 0.01f, 24, Fade(GREEN, 0.45f));
                        for (int w = 0; w < 3; w++) DrawCircle3D(st, 0.34f - 0.03f * w, (Vector3){ 1, 0, 0 }, 90.0f, sc);
                        // 自分から立つ場所への矢印
                        if (dist > 0.45f) {
                            Vector3 from = { P.pos.x, 0.018f, P.pos.z };
                            float ux = dx / dist, uz = dz / dist;
                            Vector3 tip = { st.x - ux * 0.38f, 0.018f, st.z - uz * 0.38f };
                            for (int w = -1; w <= 1; w++) {
                                float o = w * 0.03f;
                                DrawLine3D((Vector3){ from.x - uz * o, from.y, from.z + ux * o },
                                           (Vector3){ tip.x - uz * o, tip.y, tip.z + ux * o }, Fade(GREEN, 0.75f));
                            }
                            DrawLine3D(tip, (Vector3){ tip.x - ux * 0.3f - uz * 0.18f, tip.y, tip.z - uz * 0.3f + ux * 0.18f }, GREEN);
                            DrawLine3D(tip, (Vector3){ tip.x - ux * 0.3f + uz * 0.18f, tip.y, tip.z - uz * 0.3f - ux * 0.18f }, GREEN);
                        }
                        // そこで打つときのボール（黄色）と、そこへの縦線
                        DrawSphere(aplan.ballAt, 0.05f, Fade(YELLOW, 0.6f));
                        DrawLine3D((Vector3){ aplan.ballAt.x, 0.018f, aplan.ballAt.z }, aplan.ballAt, Fade(YELLOW, 0.5f));
                        DrawLine3D(st, (Vector3){ aplan.ballAt.x, 0.018f, aplan.ballAt.z }, Fade(YELLOW, 0.5f));
                    }
                }
                if (as.timingValid && as.tBest < 1.2f) {
                    Vector3 ip = as.overhead ? (Vector3){ P.pos.x + OVERHEAD_SIDE * handS, OVERHEAD_IDEAL_H, P.pos.z - OVERHEAD_FWD }
                                             : (Vector3){ P.pos.x + (as.fore ? IDEAL_SIDE : -IDEAL_SIDE) * handS, SHOTS[at].idealHeight,
                                                          P.pos.z - IDEAL_FWD };
                    float rr = 0.16f + 1.4f * as.tBest;
                    Color rc = QualityColor(as.qBest);
                    DrawCircle3D(ip, rr, (Vector3){ 1, 0, 0 }, 90.0f, rc);
                    DrawCircle3D(ip, rr + 0.02f, (Vector3){ 1, 0, 0 }, 90.0f, rc);
                }
            }

            // 狙い：相手コートに印（溜めている間は球種の色、それ以外は白）
            if (!botControls && guideLevel <= 1 && (playerServing || (mode != MODE_SERVE && (P.chargingShot >= 0 || (ball.active && ball.lastHitter == HITTER_OPP))))) {
                // 左右はマウス、深さは押している長さ。押していない間は、浅い〜深いの範囲を線で出す
                ShotType mt = (P.chargingShot >= 0) ? (ShotType)P.chargingShot : SHOT_TOP;
                bool pressing = P.chargingShot >= 0 || P.chargingServe >= 0;
                Vector3 et = AimFromHold(mt, P.pos.z, P.aimX, pressing ? P.hold : 0.0f, style);
                Vector3 r0 = AimFromHold(mt, P.pos.z, P.aimX, 0.0f, style), r1 = AimFromHold(mt, P.pos.z, P.aimX, 1.0f, style);
                bool longOut = et.z < -HALF_LEN;
                if (playerServing) {
                    float bx0, bx1;
                    ServiceBox(P.pos.x, -1, &bx0, &bx1);
                    float sx = Clamp(P.aimX, bx0 + 0.2f, bx1 - 0.2f);
                    et = (Vector3){ sx, 0.0f, ServeDepthFromTime(P.chargingServe >= 0 ? P.serveT : 0.0f) };
                    r0 = (Vector3){ sx, 0.0f, SERVE_DEPTH_SHORT }; r1 = (Vector3){ sx, 0.0f, -SERVICE_Z };
                    longOut = et.z < -SERVICE_Z;
                }
                DrawLine3D((Vector3){ r0.x, 0.013f, r0.z }, (Vector3){ r1.x, 0.013f, r1.z }, Fade(RAYWHITE, 0.5f));
                // 選べる位置（ラリー 5 か所、サーブ 3 か所）を小さな点で（5 か所から選ぶ狙い方のときだけ）
                if (aimLanes) {
                    float lx[5]; int nl;
                    if (playerServing) {
                        float bx0, bx1; ServiceBox(P.pos.x, -1, &bx0, &bx1);
                        lx[0] = bx0 + 0.30f; lx[1] = 0.5f * (bx0 + bx1); lx[2] = bx1 - 0.30f; nl = 3;
                    } else {
                        float wide = style == STYLE_CASUAL ? AIM_LANE_WIDE_CASUAL : AIM_LANE_WIDE;
                        lx[0] = -wide; lx[1] = -AIM_LANE_MID; lx[2] = 0.0f; lx[3] = AIM_LANE_MID; lx[4] = wide; nl = 5;
                    }
                    for (int i = 0; i < nl; i++)
                        DrawCircle3D((Vector3){ lx[i], 0.014f, et.z }, 0.12f, (Vector3){ 1, 0, 0 }, 90.0f, Fade(RAYWHITE, 0.55f));
                }
                Vector3 am = { et.x, 0.015f, et.z };
                bool lineZone = !playerServing && style != STYLE_CASUAL && fabsf(et.x) > AIM_SIDE_SAFE;   // ライン際：橙（カジュアルは出ない）
                Color ac = longOut ? RED : lineZone ? ORANGE : (P.chargingShot >= 0) ? SHOTS[P.chargingShot].color
                         : (P.chargingServe >= 0 ? SERVES[P.chargingServe].color : Fade(RAYWHITE, 0.7f));
                DrawCircle3D(am, 0.45f, (Vector3){ 1, 0, 0 }, 90.0f, ac);
                DrawCircle3D(am, 0.25f, (Vector3){ 1, 0, 0 }, 90.0f, ac);
                DrawLine3D((Vector3){ am.x - 0.5f, 0.015f, am.z }, (Vector3){ am.x + 0.5f, 0.015f, am.z }, ac);
                DrawLine3D((Vector3){ am.x, 0.015f, am.z - 0.5f }, (Vector3){ am.x, 0.015f, am.z + 0.5f }, ac);
            }

            // ---- いい当たり：打った点から金色の光が放射状に飛び、輪が広がる ----
            if (sweetT < 0.45f) {
                float k = sweetT / 0.45f, al = 1.0f - k;
                Color gold = { 255, 215, 70, 255 };
                Vector3 vd = Vector3Normalize(Vector3Subtract(cam.position, sweetPos));   // カメラの方を向く輪
                Vector3 ax = Vector3CrossProduct((Vector3){ 0, 0, 1 }, vd);
                float ang = acosf(Clamp(vd.z, -1.0f, 1.0f)) * RAD2DEG;
                if (Vector3Length(ax) < 1e-4f) ax = (Vector3){ 0, 1, 0 };
                ax = Vector3Normalize(ax);
                for (int r = 0; r < 3; r++) DrawCircle3D(sweetPos, 0.15f + 0.9f * k + 0.03f * r, ax, ang, Fade(gold, al * (1.0f - 0.25f * r)));
                Vector3 ux = Vector3Normalize(Vector3CrossProduct(vd, (Vector3){ 0, 1, 0 })), uy = Vector3CrossProduct(ux, vd);
                for (int i = 0; i < 12; i++) {   // 光の筋
                    float a2 = i * (2.0f * PI / 12.0f) + 0.3f;
                    Vector3 dir = Vector3Add(Vector3Scale(ux, cosf(a2)), Vector3Scale(uy, sinf(a2)));
                    float r0 = 0.10f + 0.55f * k, r1 = r0 + 0.25f * (1.0f - k) + 0.05f;
                    DrawLine3D(Vector3Add(sweetPos, Vector3Scale(dir, r0)), Vector3Add(sweetPos, Vector3Scale(dir, r1)), Fade(i % 2 ? RAYWHITE : gold, al));
                }
                DrawSphere(sweetPos, 0.06f + 0.10f * (1.0f - k), Fade((Color){ 255, 245, 200, 255 }, al * 0.8f));
            }
            // 軌跡：新しいところほど濃く。相手の球は明るい色で
            if (guideLevel == 0) {
                bool incoming = ball.lastHitter == HITTER_OPP;
                Color tc = incoming ? (Color){ 255, 255, 170, 255 } : trailColor;
                for (int i = 1; i < trailCount; i++) {
                    float age = (float)(trailCount - i) / 60.0f;          // 0 = 今、1 = 0.5 秒前
                    float alpha = Clamp(1.0f - age, 0.15f, 1.0f);
                    DrawLine3D(trail[i - 1], trail[i], Fade(tc, alpha));
                }
            }

            // クレーはボールの跡が残る（実際の試合でも跡でイン・アウトを確かめる）
            if (gSurface == SURF_CLAY && bounceT < 5.0f) {
                Color mk = Fade((Color){ 120, 55, 25, 255 }, 0.8f * (1.0f - bounceT / 5.0f));
                DrawCylinder((Vector3){ bouncePos.x, 0.0015f, bouncePos.z }, 0.05f, 0.05f, 0.001f, 12, mk);
                DrawCylinder((Vector3){ bouncePos.x, 0.0015f, bouncePos.z + 0.06f }, 0.045f, 0.045f, 0.001f, 12, mk);
            }
            // 自分の打球：狙った場所と落ちた場所を線で結ぶ（1m 以内は緑、2.5m 以内は黄、それ以上は赤）
            if (bounceByPlayer && bounceT < 1.8f && guideLevel == 0) {
                Vector3 at = { P.lastAimTarget.x, 0.02f, P.lastAimTarget.z };
                Vector3 lp = { bouncePos.x, 0.02f, bouncePos.z };
                float miss = Vector3Distance(at, lp);
                Color mc = Fade(miss < 1.0f ? GREEN : (miss < 2.5f ? YELLOW : RED), 1.0f - bounceT / 1.8f);
                DrawLine3D(at, lp, mc);
                DrawCircle3D(at, 0.3f, (Vector3){ 1, 0, 0 }, 90.0f, mc);
                DrawLine3D((Vector3){ at.x - 0.3f, at.y, at.z }, (Vector3){ at.x + 0.3f, at.y, at.z }, mc);
                DrawLine3D((Vector3){ at.x, at.y, at.z - 0.3f }, (Vector3){ at.x, at.y, at.z + 0.3f }, mc);
            }
            // 最初に弾んだ跡：実寸（判定に使った大きさ）で、しばらく残す
            if (markT < 4.0f) {
                float a = Clamp01((4.0f - markT) / 1.0f);
                DrawCylinder((Vector3){ markPos.x, 0.006f, markPos.z }, BALL_R, BALL_R, 0.001f, 20, Fade((Color){ 60, 50, 40, 255 }, 0.75f * a));
                DrawCircle3D((Vector3){ markPos.x, 0.008f, markPos.z }, BALL_R, (Vector3){ 1, 0, 0 }, 90.0f, Fade(YELLOW, a));
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
                // 地面の近くでは実寸にする（ラインとの重なりが判定と一致するように）
                float nearG = Smooth01((ball.pos.y - BALL_R - 0.05f) / 0.6f);
                vr = Lerp(BALL_R, vr, nearG);
                Vector3 g = { ball.pos.x, 0.004f, ball.pos.z };
                DrawBallShadow(ball.pos, vr);                                                  // 影（真下。柔らかく、高いほどぼける）
                if (ball.pos.y > 0.15f) DrawLine3D(g, ball.pos, Fade(RAYWHITE, 0.45f));       // 高さの線
                if (FX_BALL_TRAIL && ball.active && trailCount >= 2) {   // 残像：打ってからの軌跡（1/120 秒ごと）の最後の FX_TRAIL_TIME 秒ぶんを、細く消えていく帯に。速い球ほどはっきり
                    Vector3 pts[FX_TRAIL_N + 1];
                    int m = (int)(FX_TRAIL_TIME * 120.0f); if (m > FX_TRAIL_N) m = FX_TRAIL_N; if (m > trailCount) m = trailCount;
                    for (int i = 0; i < m; i++) pts[i] = trail[trailCount - m + i];
                    pts[m] = ball.pos;
                    bool gold = trailColor.r == 255 && trailColor.g == 210 && trailColor.b == 60;   // いい当たりは金色
                    DrawBallAfterimage(pts, m + 1, cam.position, vr, Smooth01((Vector3Length(ball.vel) - FX_TRAIL_V0) / (FX_TRAIL_V1 - FX_TRAIL_V0)),
                                       gold ? (Color){ 255, 225, 110, 255 } : (Color){ 248, 255, 170, 255 });
                }
                DrawSphere(ball.pos, vr, (Color){ 235, 255, 40, 255 });
                // 光のふち（ボールの後に描く。先に描くと、ふちの奥行きでボールが隠れる）
                rlDrawRenderBatchActive(); rlDisableDepthMask();   // たまっている図形を先に描いてから切り替える（後で描かれると奥が透ける）
                if (nearG > 0.01f) DrawSphere(ball.pos, vr * 1.7f, Fade((Color){ 255, 255, 140, 255 }, 0.22f * nearG));
                rlDrawRenderBatchActive(); rlEnableDepthMask();
            }
        EndMode3D();
        AtmosOverlay(cam); PixelEnd();   // 時間帯と天気の色合い、ナイターのライト
        guideLevel = keepGuide; assistLevel = keepAssist;
        // 試合の統計の表が出ている間は、表にかかるほかの文字（テロップ・左上の説明・下の成績と操作の行・右上のスコア表・際どい判定の図）を出さない。
        //   resultUp：試合の結果・セット間の表（右上のスコア表も出さない。セットのスコアは表の見出しにある）。summaryUp：Tab で開いた表も含む
        bool resultUp  = mode == MODE_MATCH && phaseTimer <= 0.0f && (score.over || setBreak);
        bool summaryUp = resultUp || (mode == MODE_MATCH && showSummary);
        if (MOBILE_ONLY && cineMode == 0) DrawTouchControls(playerServing, mode == MODE_MATCH && score.over && phaseTimer <= 0.0f);   // スマホ版：打つボタン・一時停止・スティック（試合の結果の間は一時停止だけ）
        if (cineMode > 0) {   // 撮影モード：文字は出さない（切り替えた直後だけ、カメラの名前を小さく）
            if (cineLabelT > 0.0f) {
                cineLabelT -= frameDt;
                static const char *CN_EN[CINE_COUNT] = { "TV (low)", "SIDE", "BEHIND YOU", "ORBIT" };
                static const char *CN_JA[CINE_COUNT] = { "中継（低め）", "真横", "すぐ後ろ", "まわりを回る" };
                const char *l = TextFormat(TR("PHOTO MODE %d/%d: %s   (F3: next, F12: screenshot)", "撮影モード %d/%d：%s（F3：次、F12：画面を保存）"), cineMode, CINE_COUNT,
                                           gLang == LANG_JA ? CN_JA[cineMode - 1] : CN_EN[cineMode - 1]);
                DrawText(l, 16, 14, 18, Fade(RAYWHITE, Clamp01(cineLabelT / 0.4f)));
            }
            goto hud_done;
        }
        if (battle && mode == MODE_MATCH) {   // バトル：画面の上に 2 人の体力ゲージ。体に当たると赤く点滅、K.O. の文字
            // 左上の説明（幅 500）と右上の点数（幅 310）の間に収める
            int W = GetScreenWidth(), H = GetScreenHeight(), bh = 20, y0 = 14;
            int gx0 = 520, gx1 = W - 320, cxm = (gx0 + gx1) / 2, bw = Clamp((gx1 - gx0) / 2 - 34, 90, 260);
            for (int k = 0; k < 2; k++) {
                hpShown[k] += (hp[k] - hpShown[k]) * fminf(frameDt * 3.0f, 1.0f);   // ゆっくり減って見える
                hitFlashT[k] = fmaxf(hitFlashT[k] - frameDt, 0.0f);
                int x = k == 0 ? cxm - 28 - bw : cxm + 28;
                float f = hp[k] / BATTLE_HP, fs2 = hpShown[k] / BATTLE_HP;
                Color col = f > 0.5f ? (Color){ 90, 220, 110, 255 } : f > 0.25f ? (Color){ 250, 200, 60, 255 } : (Color){ 240, 70, 60, 255 };
                DrawRectangle(x - 3, y0 - 3, bw + 6, bh + 6, Fade(BLACK, 0.7f));
                if (k == 0) { DrawRectangle(x + (int)(bw * (1 - fs2)), y0, (int)(bw * fs2), bh, (Color){ 255, 255, 255, 200 }); DrawRectangle(x + (int)(bw * (1 - f)), y0, (int)(bw * f), bh, col); }
                else        { DrawRectangle(x, y0, (int)(bw * fs2), bh, (Color){ 255, 255, 255, 200 }); DrawRectangle(x, y0, (int)(bw * f), bh, col); }
                const char *nm = k == 0 ? YouName() : OPP_SHORT;
                DrawText(nm, k == 0 ? x : x + bw - MeasureText(nm, 18), y0 + bh + 4, 18, RAYWHITE);
                if (hitFlashT[k] > 0.0f && k == 0) DrawRectangle(0, 0, W, H, Fade(RED, 0.35f * hitFlashT[k] / 0.5f));
            }
            DrawText("VS", cxm - MeasureText("VS", 22) / 2, y0, 22, YELLOW);
            if (koT >= 0.0f && !resultUp) {
                koT += frameDt;
                float sc = koT < 0.25f ? 3.0f - 8.0f * koT : 1.0f, al = koT < 3.5f ? 1.0f : Clamp01(1.0f - (koT - 3.5f));
                int fsz = (int)(140 * sc), tw = MeasureText("K.O.", fsz);
                DrawText("K.O.", W / 2 - tw / 2 + 6, H / 2 - fsz / 2 - 60 + 6, fsz, Fade(BLACK, 0.6f * al));
                DrawText("K.O.", W / 2 - tw / 2, H / 2 - fsz / 2 - 60, fsz, Fade((Color){ 255, 60, 50, 255 }, al));
            }
        }
        if (mode == MODE_MATCH && gYouRage > 0.0f && !changeActive) {   // 燃えている：残りの強さを見せる
            const char *fu = TL("FIRED UP", "燃えている");
            float pulse = 0.75f + 0.25f * sinf((float)GetTime() * 6.0f);
            DrawRectangle(18, GetScreenHeight() / 2 - 70, 150, 34, Fade(BLACK, 0.45f));
            DrawRectangle(18, GetScreenHeight() / 2 - 40, (int)(150 * gYouRage), 4, (Color){ 255, 120, 40, 255 });
            DrawText(fu, 28, GetScreenHeight() / 2 - 64, 22, Fade((Color){ 255, 140, 60, 255 }, pulse));
        }
        if (changeActive) {   // コートチェンジ：入れ替わる間は一瞬暗く。休んでいる間は表示
            float mid = changeDur * 0.5f, d = fabsf(changeT - mid);
            if (d < 0.45f) DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(), Fade(BLACK, 1.0f - d / 0.45f));
            if (changeSit && changeT > 1.6f && changeT < changeDur - 1.6f) {
                const char *rs = TL("Changeover  -  players rest at the bench", "チェンジコート  -  ベンチで休憩");
                DrawText(rs, GetScreenWidth() / 2 - MeasureText(rs, 22) / 2, GetScreenHeight() - 120, 22, Fade(RAYWHITE, 0.9f));
            }
            const char *sk = TR("Enter: skip", "Enter：飛ばす");
            DrawText(sk, GetScreenWidth() - MeasureText(sk, 18) - 20, GetScreenHeight() - 40, 18, LIGHTGRAY);
        }

        // ポイントの後：リプレイの案内
        if (mode == MODE_MATCH && (phase == PH_POINT_OVER || phase == PH_BETWEEN) && !score.over && !botControls && hudLevel <= 2)
            DrawText(TR("R: replay", "R：リプレイ"), GetScreenWidth() / 2 - MeasureText(TR("R: replay", "R：リプレイ"), 20) / 2, GetScreenHeight() - 170, 20, Fade(RAYWHITE, 0.8f));
        // 大会の名前とラウンド
        if (tour.active && mode == MODE_MATCH && hudLevel <= 2 && !(DEMO_ONLY && resultUp)) {
            int lgk = gLang; gLang = LANG_EN;   // 試合中のテロップは英語
            const char *tl = TextFormat("%s  -  %s", TourName(tour.slam), TourRoundName(tour.round));
            gLang = lgk;
            DrawText(tl, (int)fmaxf(520.0f, GetScreenWidth() / 2 - MeasureText(tl, 18) / 2), 8, 18, Fade(YELLOW, 0.85f));
        }
        // ---- 際どい判定の拡大図（上から見たライン際。1cm = 6 ピクセル）----
        if (hawkT < 3.0f && mode != MODE_SERVE && !summaryUp) {
            const float S = 600.0f;            // 1m あたりのピクセル
            int pw = 380, ph = 230, px = GetScreenWidth() - pw - 20, py = 200;
            float al = Clamp01((3.0f - hawkT) / 0.4f);
            DrawRectangle(px, py, pw, ph, Fade((Color){ 20, 60, 110, 255 }, 0.92f * al));
            // 外側（下）とライン（上へ 5cm）とコートの内側
            int edgeY = py + ph / 2 + 30;
            DrawRectangle(px, py + 26, pw, edgeY - py - 26, Fade(SURFACES[gSurface].court, al));
            DrawRectangle(px, edgeY, pw, py + ph - edgeY, Fade(SURFACES[gSurface].outside, al));
            DrawRectangle(px, edgeY - (int)(LINE_W * S), pw, (int)(LINE_W * S), Fade(RAYWHITE, al));
            // 球：中心はラインの外側の縁から lineDist だけ外。線に沿う向きの位置は真ん中に置く
            float cx = px + pw * 0.5f, cy = edgeY + hawk.lineDist * S;
            DrawCircle((int)cx, (int)cy, BALL_R * S, Fade((Color){ 90, 80, 50, 255 }, 0.55f * al));     // 弾んだ跡
            DrawCircleLines((int)cx, (int)cy, BALL_R * S, Fade(YELLOW, al));
            bool in = hawk.result == RESULT_IN;
            float gap = (hawk.lineDist - BALL_R) * 100.0f;   // 球の端とラインの外側の縁の距離（cm）
            const char *verdict = in ? (hawk.lineDist > 0.0f ? TextFormat(TL("IN  (on the line %.1fcm)", "イン（ラインに %.1fcm かかった）"), fabsf(gap)) : TL("IN", "イン"))
                                     : TextFormat(TL("OUT  %.1fcm", "アウト  %.1fcm"), gap);
            DrawRectangle(px, py, pw, 26, Fade(BLACK, 0.6f * al));
            DrawText(TL("BALL MARK  (close call)", "ボールの跡（際どい判定）"), px + 8, py + 5, 16, Fade(RAYWHITE, al));
            DrawText(verdict, px + 10, py + ph - 30, 22, Fade(in ? (Color){ 120, 255, 120, 255 } : (Color){ 255, 110, 90, 255 }, al));
        }

        // ---- HUD ----
        // 左上のパネルは小さく薄く（相手コートの奥を隠さないように）
        int y = 20;
        if (hudLevel <= 1 && !summaryUp) {   // 左上：当たりの分析（MINIMAL と OFF では出さない。統計の表の間も）
        DrawRectangle(10, 10, 500, 178, Fade(BLACK, 0.35f));
        bool showServeInfo = (mode == MODE_SERVE) || (playerServing && mode == MODE_MATCH);
        if (showServeInfo) {
            DrawText(TextFormat("%s serve  -  %s court", serveNo == 1 ? "1st" : "2nd", deuceSide ? "deuce" : "ad"),
                     20, y, 22, RAYWHITE); y += 30;
            if (P.lastServeKmh > 0.0f || serveMsg[0]) {
                if (P.lastServeKmh > 0.0f) {
                    DrawText(TextFormat("%s  %.0f km/h", ServeNameUI(P.lastServeType), P.lastServeKmh), 20, y, 18, SERVES[P.lastServeType].color); y += 22;
                    DrawText(TextFormat(TR("Contact height %.2f m (ideal %.2f+)  quality %.0f%%", "打点の高さ %.2f m（理想 %.2f 以上）  質 %.0f%%"), P.lastServeH, SERVE_IDEAL_H * HumanServeHeightK(), P.lastServeQ * 100.0f),
                             20, y, 18, QualityColor(P.lastServeQ)); y += 22;
                } else {
                    DrawText(ServeNameUI(P.lastServeType), 20, y, 18, SERVES[P.lastServeType].color); y += 44;
                }
                if (ball.netClearance > -50.0f)
                    DrawText(TextFormat("Net clearance %+.2f m", ball.netClearance), 20, y, 18, RAYWHITE);
                y += 22;
                Color mc = (serveMsg[0] == 'I' || serveMsg[0] == 'A') ? GREEN : (serveMsg[0] == 'L' ? YELLOW : RED);
                DrawText(serveMsg, 20, y, 22, mc); y += 30;
            } else if (MOBILE_ONLY) {   // スマホ版のサーブの説明
                DrawText(TR("Press a button to toss, release at the top to hit.", "ボタンを押してトス、いちばん高い所で離して打つ"), 20, y, 18, RAYWHITE); y += 22;
                DrawText(TR("Kick = safe, Flat = fast. Slide your finger to aim.", "キック＝入りやすい、フラット＝速い。押したまま指を動かすと狙い"), 20, y, 18, RAYWHITE); y += 22;
            } else {
                DrawText(TR("A/D: position.  Mouse: left/right.  Later release = deeper", "A/D：立ち位置。マウス：左右。遅く離すほど深い"), 20, y, 18, RAYWHITE); y += 22;
                DrawText(TR("Hold L-click kick / R-click slice / Space+click flat", "左クリック キック / 右クリック スライス / Space＋クリック フラット"), 20, y, 18, RAYWHITE); y += 22;
                DrawText(TR("to toss, release to hit. Hold to let a bad toss drop.", "押してトス、離して打つ。悪いトスは押したままで見送る"), 20, y, 18, RAYWHITE); y += 22;
            }
        } else if (P.hasLast) {
            const Contact *c = &P.lastContact;
            if (c->miss != MISS_NONE) {
                DrawText(MissText(c->miss), 20, y, 22, RED); y += 30;
                if (c->miss != MISS_NO_BALL) {
                    DrawText(TextFormat(TR("Ball was %.2f m from you, %+.0f deg (side=0, front=90), %.2f m high", "球は %.2f m 先、%+.0f 度（横 0、前 90）、高さ %.2f m"),
                                        c->side, c->angleDeg, c->height), 20, y, 16, RAYWHITE); y += 20;
                    DrawText(TR("White ring on the ground = ideal distance, faint ring = max reach", "地面の白い輪 = 理想の距離、薄い輪 = 届く限界"), 20, y, 16, LIGHTGRAY); y += 20;
                }
            } else {
                const char *stroke = c->overhead ? TR("Overhead", "頭上") : (c->forehand ? TR("Forehand", "フォア") : TR("Backhand", "バック"));
                // バウンド後の球：上がり際（ライジング）・頂点・落ち際
                const char *tag = c->dive ? TR(" DIVE!", " 飛びつき！") : (c->volley ? TR(" volley", " ボレー")
                                : (c->vyBall > 0.6f ? TR(" on the rise", " 上がり際") : (c->vyBall > -0.6f ? TR(" at the top", " 頂点") : TR(" dropping", " 落ち際"))));
                DrawText(TextFormat("%s%s %s  quality %.0f%%", stroke, tag,
                                    c->overhead && P.lastType != SHOT_SLICE && P.lastType < SHOT_NORMAL_COUNT ? TR("SMASH", "スマッシュ") : ShotNameUI(P.lastType),
                                    c->quality * 100.0f), 20, y, 22, QualityColor(c->quality));
                y += 30;
                if (c->overhead) {
                    const char *tStr = c->height > OVERHEAD_IDEAL_H + 0.15f ? TR("early", "早い") : (c->height < OVERHEAD_IDEAL_H - 0.15f ? TR("late", "遅い") : TR("good", "良い"));
                    DrawText(TextFormat(TR("Timing  height %.2f m (ideal %.2f) %-5s %3.0f%%", "タイミング  高さ %.2f m（理想 %.2f）%s %3.0f%%"), c->height, OVERHEAD_IDEAL_H, tStr, c->qTiming * 100.0f), 20, y, 18, QualityColor(c->qTiming)); y += 22;
                    DrawText(TextFormat(TR("Position under the ball         %3.0f%%", "球の下の位置  %3.0f%%"), c->qDist * 100.0f), 20, y, 18, QualityColor(c->qDist)); y += 22;
                    y += 22;
                } else {
                    float idealAng = c->volley ? VOLLEY_ANGLE : IDEAL_ANGLE;
                    const char *tStr = c->angleDeg > idealAng + 8.0f ? TR("early", "早い") : (c->angleDeg < idealAng - 8.0f ? TR("late", "遅い") : TR("good", "良い"));
                    const char *dStr = c->side < IDEAL_R - 0.15f ? TR("jammed", "近すぎ") : (c->side > IDEAL_R + 0.15f ? TR("stretched", "遠すぎ") : TR("good", "良い"));
                    const char *hStr = c->height < SHOTS[P.lastType].idealHeight - 0.2f ? TR("low", "低い")
                                     : (HeightErr(&SHOTS[P.lastType], c->height, c->easeKmh) > 0.2f ? TR("high", "高い") : TR("good", "良い"));
                    DrawText(TextFormat(TR("Timing   %+3.0f deg (%+.0f ms) %-5s %3.0f%%", "タイミング  %+3.0f 度（%+.0f ms）%s %3.0f%%"), c->angleDeg - idealAng, c->timeErrMs, tStr, c->qTiming * 100.0f), 20, y, 18, QualityColor(c->qTiming)); y += 22;
                    DrawText(TextFormat(TR("Distance %.2f m  %-9s %3.0f%%", "距離  %.2f m  %s %3.0f%%"), c->side, dStr, c->qDist * 100.0f), 20, y, 18, QualityColor(c->qDist)); y += 22;
                    DrawText(TextFormat(TR("Height   %.2f m  %-9s %3.0f%%", "高さ  %.2f m  %s %3.0f%%"), c->height, hStr, c->qHeight * 100.0f), 20, y, 18, QualityColor(c->qHeight)); y += 22;
                }
                DrawText(TextFormat("%.0f km/h in -> %.0f km/h out  step %+.1f m/s", c->inKmh, P.lastKmh, c->stepIn), 20, y, 18, RAYWHITE); y += 22;
                if (ball.lastHitter == HITTER_PLAYER) {
                    if (ball.netClearance > -50.0f)
                        DrawText(TextFormat("Net clearance %+.2f m", ball.netClearance), 20, y, 18, RAYWHITE);
                    DrawText(ResultTextUI(ball.result), 300, y, 22, ball.result == RESULT_IN ? GREEN : RED);
                }
                y += 24;
            }
        } else {
            DrawText(TR("Move to the ball. Hold longer = deeper. Release to swing", "球の所へ動く。長く押すほど深い。離して振る"), 20, y, 18, RAYWHITE);
        }
        }   // 左上のパネル
        // 下：成績と操作説明（コートの奥を隠さない場所）。FULL のときだけ（統計の表の間は出さない：表の下の案内にかかる）
        if (hudLevel == 0 && !summaryUp) {
        y = GetScreenHeight() - 98;
        if (mode == MODE_SERVE)
            DrawText(TextFormat(TR("1st in %d/%d   2nd in %d/%d   Aces %d   Double faults %d", "ファースト %d/%d   セカンド %d/%d   エース %d   ダブルフォールト %d"),
                                svStats[0][1], svStats[0][0], svStats[1][1], svStats[1][0], aces, doubleFaults), 20, y, 16, RAYWHITE);
        else
            DrawText(TextFormat(TR("IN  flat %d/%d  top %d/%d  slice %d/%d  lob %d/%d  top lob %d/%d  drop %d/%d", "入った  フラット %d/%d  トップ %d/%d  スライス %d/%d  ロブ %d/%d  トップロブ %d/%d  ドロップ %d/%d"),
                                stats[SHOT_FLAT][1], stats[SHOT_FLAT][0], stats[SHOT_TOP][1], stats[SHOT_TOP][0],
                                stats[SHOT_SLICE][1], stats[SHOT_SLICE][0], stats[SHOT_SLICE_LOB][1], stats[SHOT_SLICE_LOB][0],
                                stats[SHOT_TOP_LOB][1], stats[SHOT_TOP_LOB][0], stats[SHOT_DROP][1], stats[SHOT_DROP][0]), 20, y, 16, RAYWHITE);
        y += 20;
        { const char *cl = ControlsLine(); int cfs = 16; while (cfs > 12 && MeasureText(cl, cfs) > GetScreenWidth() - 30) cfs--;   // 画面の幅に収める
          DrawText(cl, 20, y, cfs, LIGHTGRAY); } y += 18;
        if (mode == MODE_MATCH && !score.over)   // 試合の途中は 1 キーで設定を変えない（Esc のメニューから）
            DrawText(TR("Esc: menu", "Esc：メニュー"), 20, y, 16, LIGHTGRAY);
        else if (mode == MODE_MATCH)
            DrawText(TextFormat(TR("G mode: MATCH   1/2/3 AI level: %s   T slow", "G モード：試合   1/2/3 AI の強さ：%s   T スロー"), LevelNameUI(ai.level)), 20, y, 16, LIGHTGRAY);
        else if (mode == MODE_RETURN)   // リターン練習のサーブは試合の AI と同じ（速さ・コース・フォルトは AI の強さで変わる）
            DrawText(TextFormat(TR("G mode: %s   1/2/3 AI level: %s   F next serve  T slow", "G モード：%s   1/2/3 AI の強さ：%s   F 次のサーブ  T スロー"), ModeName(mode), LevelNameUI(ai.level)), 20, y, 16, LIGHTGRAY);
        else
            DrawText(TextFormat(TR("G mode: %s   V feed: %s   F feed  T slow", "G モード：%s   V 球出し：%s   F 球出し  T スロー"), ModeName(mode), proFeed ? TR("PRO", "プロ") : TR("CLUB", "クラブ")),
                     20, y, 16, LIGHTGRAY);
        }

        if (mode == MODE_RETURN && hudLevel <= 2) {   // リターン練習：ポイントの結果と、主審がコールするスコア（試合と同じ場所に）
            if (pointMsg[0]) {
                int w = MeasureText(pointMsg, 34);
                DrawRectangle(GetScreenWidth() / 2 - w / 2 - 16, 150, w + 32, 50, Fade(BLACK, 0.6f));
                DrawText(pointMsg, GetScreenWidth() / 2 - w / 2, 158, 34, YELLOW);
            }
            if (scoreCall[0]) {
                const char *call = TextToUpper(scoreCall);
                int w = MeasureText(call, 48);
                DrawRectangle(GetScreenWidth() / 2 - w / 2 - 20, 210, w + 40, 64, Fade(BLACK, 0.7f));
                DrawText(call, GetScreenWidth() / 2 - w / 2, 218, 48, RAYWHITE);
            }
            if (hudLevel <= 1) {   // 右上：サーバー・何本目か・ゲームの中のポイント・最後のサーブの速さ
                int sbw = 300, sx = GetScreenWidth() - sbw - 10, sy = 10;
                DrawRectangle(sx, sy, sbw, 58, Fade((Color){ 10, 20, 40, 255 }, 0.85f));
                DrawCircle(sx + 16, sy + 19, 6, YELLOW);
                DrawText(TextFormat("%s  %s", OPP_SHORT, serveNo == 2 ? "2nd" : "1st"), sx + 30, sy + 8, 20, (Color){ 140, 170, 255, 255 });
                const char *pl = TextFormat("%s - %s", PointLabel(&retScore, 1), PointLabel(&retScore, 0));
                DrawText(pl, sx + sbw - 12 - MeasureText(pl, 22), sy + 7, 22, YELLOW);
                if (ai.lastKmh > 0.0f) DrawText(TextFormat(TR("AI last: %s %.0f km/h", "相手の最後の球：%s %.0f km/h"), TR("SERVE", "サーブ"), ai.lastKmh), sx + 8, sy + 36, 16, LIGHTGRAY);
            }
        }
        // スコア（試合）
        if (mode == MODE_MATCH) {
            // スコア表（中継のような形）：サーブの印、名前、ゲーム、ポイント
            int so = 30 * score.nSetsDone;   // 終わったセットの列
            int sbw = 300 + so, sx = GetScreenWidth() - sbw - 10, sy = 10;
            if (!resultUp) {   // 試合の結果・セット間の表の間は出さない（表の右端と重なる。セットのスコアは表の見出しにある）
            DrawRectangle(sx, sy, sbw, 96, Fade((Color){ 10, 20, 40, 255 }, 0.85f));
            DrawText(TR("GAMES", "ゲーム"), sx + 166 + so, sy + 4, 12, LIGHTGRAY);
            DrawText(score.tiebreak ? TR("TIEBREAK", "タイブレーク") : TR("POINT", "ポイント"), sx + 222 + so, sy + 4, 12, LIGHTGRAY);
            for (int k = 0; k < 2; k++) {
                int ry = sy + 20 + k * 36;
                Color nameC = (k == 0) ? RAYWHITE : (Color){ 140, 170, 255, 255 };
                if (k == 1) DrawLine(sx + 8, ry - 2, sx + sbw - 8, ry - 2, Fade(RAYWHITE, 0.2f));
                if (server == k && !score.over) {
                    DrawCircle(sx + 16, ry + 13, 6, YELLOW);
                    if (serveNo == 2) DrawText("2nd", sx + 26, ry + 6, 14, YELLOW);
                }
                {   // 名前：終わったセットの列（またはゲーム数）の手前に収まる大きさに
                    const char *nm = k == 0 ? YouName() : OPP_SHORT;
                    int room = 170 - 58 - 8, nfs = k == 0 ? 24 : 20;
                    while (nfs > 12 && MeasureText(nm, nfs) > room) nfs--;
                    DrawText(nm, sx + 58, ry + 3 + (24 - nfs) / 2, nfs, nameC);
                }
                for (int i = 0; i < score.nSetsDone; i++) {
                    bool won = score.setScore[i][k] > score.setScore[i][1 - k];
                    DrawText(TextFormat("%d", score.setScore[i][k]), sx + 170 + i * 30, ry + 3, 24, won ? RAYWHITE : GRAY);
                }
                DrawText(TextFormat("%d", score.games[k]), sx + 176 + so, ry + 3, 24, RAYWHITE);
                DrawRectangle(sx + 212 + so, ry, 76, 30, Fade(YELLOW, 0.15f));
                const char *pl = PointLabel(&score, k);
                DrawText(pl, sx + 250 + so - MeasureText(pl, 24) / 2, ry + 3, 24, YELLOW);
            }
            // ゲームの流れ：取った方の色。ブレーク（レシーバーが取ったゲーム）は B
            for (int g = 0; g < score.nGames; g++) {
                int gx = sx + 8 + g * 22, gy = sy + 102;
                Color gc = score.gameWinner[g] == 0 ? (Color){ 230, 230, 235, 255 } : (Color){ 90, 120, 230, 255 };
                DrawRectangle(gx, gy, 18, 18, gc);
                if (score.gameWinner[g] != score.gameServer[g]) DrawText("B", gx + 5, gy + 2, 14, RED);
            }
            if (ai.lastKmh > 0.0f && hudLevel <= 1 && !summaryUp) {
                int ly = sy + (score.nGames ? 126 : 102);
                DrawRectangle(sx, ly - 3, sbw, 22, Fade((Color){ 10, 20, 40, 255 }, 0.6f));
                DrawText(TextFormat(TR("AI last: %s %.0f km/h", "相手の最後の球：%s %.0f km/h"), ai.lastServe ? TR("SERVE", "サーブ") : ShotNameUI(ai.lastType), ai.lastKmh),
                         sx + 8, ly, 16, LIGHTGRAY);
            }
            }   // スコア表
            if (pointMsg[0] && !summaryUp) {   // 統計の表の間は出さない（表の下に残って、行のすき間から文字が透けていた）
                int w = MeasureText(pointMsg, 34);
                DrawRectangle(GetScreenWidth() / 2 - w / 2 - 16, 150, w + 32, 50, Fade(BLACK, 0.6f));
                DrawText(pointMsg, GetScreenWidth() / 2 - w / 2, 158, 34, YELLOW);
            }
            if (scoreCall[0] && !score.over && !summaryUp) {
                // スコアのコール（40-15、DEUCE、ADVANTAGE、GAME）を大きく
                const char *call = TextToUpper(scoreCall);
                int w = MeasureText(call, 48);
                DrawRectangle(GetScreenWidth() / 2 - w / 2 - 20, 210, w + 40, 64, Fade(BLACK, 0.7f));
                DrawText(call, GetScreenWidth() / 2 - w / 2, 218, 48, RAYWHITE);
            }
            // ラケットを叩きつけた：審判のコール
            if (rageMsgT > 0.0f && rageMsg[0]) rageMsgT -= frameDt;
            if (rageMsgT > 0.0f && rageMsg[0] && !summaryUp) {
                // 行ごとに測って、いちばん長い行と行の数に合わせた箱にする（説明が長くてもはみ出さない）
                int w = 0, nl = 0;
                {
                    char buf[512]; snprintf(buf, sizeof(buf), "%s", rageMsg);
                    for (char *ln = strtok(buf, "\n"); ln; ln = strtok(NULL, "\n")) { int lw = MeasureText(ln, 26); if (lw > w) w = lw; nl++; }
                }
                float al = Clamp01(rageMsgT / 0.5f);
                DrawRectangle(GetScreenWidth() / 2 - w / 2 - 18, 290, w + 36, 20 + nl * 30, Fade((Color){ 120, 20, 20, 255 }, 0.80f * al));
                DrawText(rageMsg, GetScreenWidth() / 2 - w / 2, 300, 26, Fade(RAYWHITE, al));
            }
            // ---- 試合に勝った：紙吹雪。統計の表より先に描く（表の文字の上に紙吹雪がかからないように、表の後ろを舞う） ----
            if (winFx >= 0.0f && score.over) {
                winFx += frameDt;
                int W = GetScreenWidth(), H = GetScreenHeight();
                static const Color CF[6] = { { 250, 70, 70, 255 }, { 255, 210, 60, 255 }, { 90, 200, 255, 255 }, { 120, 230, 120, 255 }, { 255, 140, 220, 255 }, { 255, 255, 255, 255 } };
                float alC = phaseTimer > 0.0f ? 1.0f : 0.75f;
                for (int i = 0; i < 140; i++) {
                    float r1 = fmodf(sinf(i * 12.9898f) * 43758.5453f, 1.0f), r2 = fmodf(sinf(i * 78.233f) * 12345.678f, 1.0f);
                    r1 = fabsf(r1); r2 = fabsf(r2);
                    float sp = 70.0f + 110.0f * r2;
                    float y = fmodf(r1 * (H + 60) + winFx * sp, (float)(H + 60)) - 30.0f;
                    float x = r2 * W + 35.0f * sinf(winFx * (1.5f + r1 * 2.0f) + i);
                    float rot = winFx * (90.0f + 200.0f * r1) + i * 37.0f;
                    DrawRectanglePro((Rectangle){ x, y, 12.0f, 6.0f }, (Vector2){ 6.0f, 3.0f }, rot, Fade(CF[i % 6], alC));
                }
            }
            // 試合の統計：セット間と試合の後（Tab でもいつでも）
            if (score.over && phaseTimer <= 0.0f)
                DrawMatchSummary(score.sets[0] > score.sets[1] ? TextFormat(TR("MATCH SUMMARY - %s WINS", "試合の結果 - %s の勝ち"), YouName()) : TextFormat(TR("MATCH SUMMARY - %s WINS", "試合の結果 - %s の勝ち"), OPP_SHORT),
                                 TextFormat("%s%s", tour.active ? TR("Enter: back to the draw    R: highlights", "Enter：組み合わせ表へ    R：ハイライト") : TR("Enter: new match    R: highlights", "Enter：次の試合    R：ハイライト"),
                                            matchAssisted ? TR("    (assisted: slow / autopilot / style changed)", "    （補助を使った試合：スロー・自動操作・難しさの変更）") : ""),
                                 &score, ms, mst, OPP_SHORT, GetScreenWidth());
            if (DEMO_ONLY && score.over && phaseTimer <= 0.0f && (demoCode[0] || demoTried)) {   // 体験版：勝利の証明コード
                // 結果の表の上の空き（表は画面の真ん中、上に約 60px 空く）に 2 行で
                int W = GetScreenWidth(), bw = 660, bh = 54, bx = W / 2 - bw / 2, by = 4;   // 右上の点数の表示にかからない幅
                DrawRectangle(bx, by, bw, bh, Fade((Color){ 20, 20, 40, 255 }, 0.94f));
                DrawRectangleLines(bx, by, bw, bh, (Color){ 255, 215, 70, 255 });
                if (demoCode[0]) {
                    if (IsKeyPressed(KEY_C)) { SetClipboardText(demoCode); snprintf(toast, sizeof(toast), "%s", TR("Code copied", "コードをコピーした")); toastT = 2.0f; }
                    const char *l1 = TextFormat(TR("CHALLENGE CLEARED!  %s", "チャレンジ達成！  %s"), demoCode);
                    int f1 = 26; while (f1 > 16 && MeasureText(l1, f1) > bw - 24) f1--;
                    DrawText(l1, W / 2 - MeasureText(l1, f1) / 2, by + 5, f1, (Color){ 255, 215, 70, 255 });
                    const char *h2 = TR("Write it down (C: copy). Send it as the store page says to get the full game free.", "書き留めておく（C：コピー）。ストアページの案内のとおり送ると、製品版が無料でもらえる");
                    int f2 = 16; while (f2 > 12 && MeasureText(h2, f2) > bw - 30) f2--;
                    DrawText(h2, W / 2 - MeasureText(h2, f2) / 2, by + 34, f2, LIGHTGRAY);
                } else {
                    const char *h1 = TR("Win against AI NORMAL or PRO on REAL (whole match) to get the free full game code.", "AI「ふつう」以上・難しさ「リアル」のまま勝つと、製品版が無料になるコードがもらえる");
                    int f1 = 20; while (f1 > 12 && MeasureText(h1, f1) > bw - 30) f1--;
                    DrawText(h1, W / 2 - MeasureText(h1, f1) / 2, by + 16, f1, (Color){ 255, 215, 70, 255 });
                }
            }
            if (score.over && phaseTimer <= 0.0f) { }   // 試合の結果は上で出した（下の 2 つは出さない）
            else if (setBreak && phaseTimer <= 0.0f)
                DrawMatchSummary(TextFormat(TR("AFTER SET %d", "第 %d セット終了"), score.nSetsDone), TR("Enter / click: next set", "Enter / クリック：次のセット"), &score, ms, mst, OPP_SHORT, GetScreenWidth());
            else if (showSummary)
                DrawMatchSummary(TR("MATCH SUMMARY", "試合の統計"), TR("Tab: close", "Tab：閉じる"), &score, ms, mst, OPP_SHORT, sx);   // 試合中：右上のスコア表は出したまま、表をその左に収める
        }

        // 統計（Tab）
        if (showStats) {
            int bx = 240, by = 100, bw = 800, bh = 540;
            DrawRectangle(bx, by, bw, bh, Fade(BLACK, 0.85f));
            int ty = by + 16;
            #define LINE(...) do { DrawText(TextFormat(__VA_ARGS__), bx + 20, ty, 20, RAYWHITE); ty += 26; } while (0)
            DrawText(PN(TR("SESSION STATS  (Tab to close)   saved to playlog.csv", "今回の成績（Tab で閉じる）  playlog.csv に保存"), TR("SESSION STATS  (Tab to close)", "今回の成績（Tab で閉じる）")), bx + 20, ty, 22, YELLOW); ty += 36;
            int sw = plog.swings > 0 ? plog.swings : 1;
            LINE(TR("Swings %d:  hit %d (%d%%)   whiff %d (%d%%)", "振った %d：当たった %d（%d%%）  空振り %d（%d%%）"), plog.swings, plog.hits, plog.hits * 100 / sw, plog.whiffs, plog.whiffs * 100 / sw);
            LINE(TR("  whiffs: early %d  late %d  far %d  high %d  low %d", "  空振り：早い %d  遅い %d  遠い %d  高い %d  低い %d"),
                 plog.miss[MISS_EARLY], plog.miss[MISS_LATE], plog.miss[MISS_FAR], plog.miss[MISS_HIGH], plog.miss[MISS_LOW]);
            int res = plog.shotIn + plog.shotOut + plog.shotNet; if (res < 1) res = 1;
            LINE(TR("Shots that landed: IN %d%%   OUT %d%%   NET %d%%  (of %d)", "打球：イン %d%%   アウト %d%%   ネット %d%%（%d 本）"),
                 plog.shotIn * 100 / res, plog.shotOut * 100 / res, plog.shotNet * 100 / res, plog.shotIn + plog.shotOut + plog.shotNet);
            ty += 8;
            int no = plog.nOffset > 0 ? plog.nOffset : 1;
            LINE(TR("Timing vs best moment: avg %.0f ms off   early %d / on time %d / late %d", "一番いい瞬間とのずれ：平均 %.0f ms   早い %d / ちょうど %d / 遅い %d"),
                 plog.sumAbsOffset / no, plog.early, plog.onTime, plog.late);
            LINE(TR("Contact quality: avg %.0f%%   best possible from where you stood: %.0f%%", "当たりの質：平均 %.0f%%   その位置での最高：%.0f%%"),
                 plog.hits ? plog.sumQ / plog.hits * 100.0f : 0.0f, plog.sumBestQ / no * 100.0f);
            LINE(TR("Main cause:  POSITION (feet) %d   TIMING (hands) %d", "主な原因：位置（足） %d   タイミング（手） %d"), plog.posProblem, plog.timingProblem);
            ty += 8;
            LINE(TR("Balls you didn't swing at: %d landing IN (%d were reachable), %d going OUT", "振らなかった球：イン %d（届いた %d）、アウト %d"),
                 plog.noSwingIn, plog.noSwingReachable, plog.noSwingOut);
            LINE(TR("Reaction (first move after opponent hit): avg %.2f s", "反応（相手が打ってから動き出すまで）：平均 %.2f 秒"),
                 plog.nFirstMove ? plog.sumFirstMove / plog.nFirstMove : 0.0f);
            ty += 8;
            LINE(TR("Serves %d:  in %d   whiff %d", "サーブ %d：入った %d   空振り %d"), plog.serves, plog.servesIn, plog.serveWhiffs);
            if (mode == MODE_MATCH) {
                ty += 8;
                for (int k = 0; k < 2; k++) {
                    int a = mst[k][0] ? mst[k][0] : 1, b = mst[k][2] ? mst[k][2] : 1, in = mst[k][1] + mst[k][3];
                    LINE(TR("%-3s 1st in %d/%d (%d%%)  2nd in %d/%d (%d%%)  unreturned %d/%d  DF %d", "%s  ファースト %d/%d（%d%%）  セカンド %d/%d（%d%%）  返されなかった %d/%d  DF %d"),
                         k == 0 ? YouName() : gOppName, mst[k][1], mst[k][0], mst[k][1] * 100 / a, mst[k][3], mst[k][2], mst[k][3] * 100 / b,
                         mst[k][4], in, mst[k][5]);
                }
            }
            #undef LINE
        }

        // 溜めゲージ
        int gw = 300, gx = GetScreenWidth() / 2 - gw / 2, gy = GetScreenHeight() - 140;   // 下の操作説明と重ならない高さ
        if (P.chargingShot >= 0 && hudLevel <= 2) {
            const ShotSpec *sp = &SHOTS[P.chargingShot];
            DrawRectangle(gx, gy, gw, 20, Fade(BLACK, 0.6f));
            // 押している長さ＝深さ。白い線が一番深いところ、その先（赤）はアウト
            float full = (float)gw / HOLD_MAX;
            DrawRectangle(gx, gy, (int)(full * fminf(P.hold, 1.0f)), 20, sp->color);
            if (P.hold > 1.0f) DrawRectangle(gx + (int)full, gy, (int)(full * (P.hold - 1.0f)), 20, RED);
            DrawRectangle(gx + (int)full - 1, gy - 4, 3, 28, RAYWHITE);
            float pw = HOLD_POWER_BASE + (HOLD_POWER_MAX - HOLD_POWER_BASE) * P.charge;
            float dz = -AimFromHold((ShotType)P.chargingShot, P.pos.z, P.aimX, P.hold, style).z;
            if (sp->touchAngle > 0.0f)
                DrawText(TextFormat(TR("%s  depth %.1f m", "%s  深さ %.1f m"), ShotNameUI(P.chargingShot), dz), gx, gy - 24, 20, RAYWHITE);
            else
                DrawText(TextFormat(TR("%s  %.0f km/h  depth %.1f m%s", "%s  %.0f km/h  深さ %.1f m%s"), ShotNameUI(P.chargingShot), Lerp(sp->minKmh, sp->maxKmh, pw), dz,
                                    dz > HALF_LEN ? TR("  LONG", "  長い") : ""), gx, gy - 24, 20, dz > HALF_LEN ? RED : RAYWHITE);
        }
        if (P.chargingServe >= 0 && hudLevel <= 2) {
            const ServeSpec *sp = &SERVES[P.chargingServe];
            DrawRectangle(gx, gy, gw, 20, Fade(BLACK, 0.6f));
            DrawRectangle(gx, gy, (int)(gw * P.charge), 20, sp->color);
            float sd = -ServeDepthFromTime(P.serveT);
            DrawText(TextFormat(TR("%s  %.0f km/h  depth %.1f m%s", "%s  %.0f km/h  深さ %.1f m%s"), ServeNameUI(P.chargingServe), Lerp(sp->minKmh, sp->maxKmh, P.charge), sd,
                                sd > SERVICE_Z ? TR("  LONG", "  長い") : ""), gx, gy - 24, 20, sd > SERVICE_Z ? RED : RAYWHITE);
        }
        if (toastT > 0.0f) {
            toastT -= frameDt;
            int tw = MeasureText(toast, 30);
            int ty = GetScreenHeight() - 210;
            if (summaryUp) ty = (int)fminf((float)(SummaryBottom() + 8), (float)(GetScreenHeight() - 48));   // 統計の表の間は表の下に（前は表の行の上に重なっていた）
            DrawRectangle(GetScreenWidth() / 2 - tw / 2 - 16, ty, tw + 32, 44, Fade(BLACK, 0.75f));
            DrawText(toast, GetScreenWidth() / 2 - tw / 2, ty + 8, 30, YELLOW);
        }
        // ---- いい当たり：画面の縁が一瞬金色に光る ----
        if (sweetT < 0.25f) {
            float al = (0.25f + 0.5f * Clamp01((sweetQ - SWEET_Q) / 0.2f)) * (1.0f - sweetT / 0.25f);
            int W = GetScreenWidth(), H = GetScreenHeight(), b = 26;
            Color g = Fade((Color){ 255, 215, 70, 255 }, al), z = Fade((Color){ 255, 215, 70, 255 }, 0.0f);
            DrawRectangleGradientV(0, 0, W, b, g, z); DrawRectangleGradientV(0, H - b, W, b, z, g);
            DrawRectangleGradientH(0, 0, b, H, g, z); DrawRectangleGradientH(W - b, 0, b, H, z, g);
        }
        sweetT += frameDt;
        if (slowMo) DrawText(TR("SLOW x0.25", "スロー x0.25"), GetScreenWidth() - 140, GetScreenHeight() - 40, 20, RED);
        if (botControls) DrawText(TR("AUTOPILOT (B)", "自動操作（B）"), GetScreenWidth() - 180, GetScreenHeight() - 128, 20, ORANGE);   // 下の操作説明の行にかからない高さ
        if (hudLevel == 0) DrawText(TR("Tab: stats", "Tab：成績"), GetScreenWidth() - 120, GetScreenHeight() - 30, 16, LIGHTGRAY);
        if (hudLevel == 0 && !summaryUp) {   // 統計の表の間は出さない（表の下に出す知らせと重なる）
            const char *an[3] = { TR("OFF", "なし"), TR("TIMING", "タイミング"), TR("TIMING+SPOT", "タイミング＋立つ場所") };
            DrawText(TextFormat(TR("H assist: %s", "H 手がかり：%s"), an[assistLevel]), 260, GetScreenHeight() - 30, 18, assistLevel ? GREEN : LIGHTGRAY);
            DrawText(TextFormat(TR("N style: %s", "N 難しさ：%s"), StyleNameUI(style)), 560, GetScreenHeight() - 30, 18, style == STYLE_CASUAL ? SKYBLUE : ORANGE);
            DrawText(TextFormat(TR("Court: %s", "コート：%s"), SurfNameUI(gSurface)), 780, GetScreenHeight() - 30, 18, LIGHTGRAY);
            // 音の状態（いつも出す）
            {
                const char *st; Color sc = LIGHTGRAY;
                if (!SfxDeviceOk())               { st = TR("Sound: no audio device", "音：音の出る機器がない"); sc = RED; }
                else if (SfxLoadedCount() == 0)   { st = TextFormat(TR("Sound: no files in %s", "音：%s にファイルがない"), SfxDir()); sc = RED; }
                else if (SfxGetVolume() <= 0.0f)  { st = TR("Sound: OFF (M)", "音：なし（M）"); sc = ORANGE; }
                else                               st = TextFormat(TR("Sound: %.0f%%  %d files (M)", "音：%.0f%%  %d ファイル（M）"), SfxGetVolume() * 100.0f, SfxLoadedCount());
                DrawText(st, 560, GetScreenHeight() - 52, 16, sc);
            }
        }
        if (tut >= 0) {   // はじめての人への練習：やること（画面の上の真ん中）
            // 左上の説明（幅 500）の右に置く（練習では右上に点数は出ない）
            int W = GetScreenWidth(), bx = 525, bw = Clamp(W - bx - 16, 400, 760), by = 12;
            const char *ttl = "", *l1 = "", *l2 = "";
            int inNow = 0; for (int k = 0; k < SHOT_COUNT; k++) inNow += stats[k][1];
            int svIn = svStats[0][1] + svStats[1][1];
            switch (tut) {
                case 0: ttl = TR("1/4  MOVE", "1/4  動く");
                        l1 = TextFormat(TR("Use %s to move into the glowing circle.", "%s で、光る円の中へ動こう"), ControlsMove()); break;
                case 1: ttl = TextFormat(TR("2/4  HIT  (%d/3 in)", "2/4  打つ（%d/3 球）"), inNow - tutBase);
                        l1 = TextFormat(TR("When the ball comes, press L-click (or %s) and RELEASE to swing. Get 3 in.", "球が来たら、左クリック（または %s）を押して、離して振る。3 球入れよう"), KeyName(gKeys[ACT_TOP]));
                        l2 = TR("Hold longer to hit deeper. Stand where the circle shows. Top-left tells you early / late / far.", "長く押すほど深く飛ぶ。円の場所に立つと打ちやすい。早い・遅い・遠いは左上に出る"); break;
                case 2: ttl = TextFormat(TR("3/4  AIM  (left %s / right %s)", "3/4  狙う（左 %s / 右 %s）"), tutL ? "OK" : "-", tutR ? "OK" : "-");
                        l1 = TextFormat(TR("Move the mouse left / right to aim (keys: %s / %s). Land one ball on each side.", "マウスを左右に動かすと狙いが動く（キーなら %s / %s）。左と右に 1 球ずつ入れよう"), KeyName(gKeys[ACT_AIM_L]), KeyName(gKeys[ACT_AIM_R]));
                        l2 = TR("Right click = slice, Shift + click = lob, Ctrl + click = drop shot.", "右クリック＝スライス、Shift＋クリック＝ロブ、Ctrl＋クリック＝ドロップ"); break;
                case 3: ttl = TextFormat(TR("4/4  SERVE  (%d/2 in)", "4/4  サーブ（%d/2 本）"), svIn - tutBase);
                        l1 = TR("Press to toss, RELEASE near the top of the toss. Get 2 serves in.", "押してトスを上げ、いちばん高い所で離す。2 本入れよう");
                        l2 = TR("L-click = kick (safe), R-click = slice, Space + click = flat (fast).", "左クリック＝キック（入りやすい）、右クリック＝スライス、Space＋クリック＝フラット（速い）"); break;
                default: ttl = TR("ALL DONE!", "できた！");
                        l1 = TR("Now try a match. (Back to the title...)", "次は試合をやってみよう（タイトルに戻る）"); break;
            }
            int bh = l2[0] ? 108 : 80;
            DrawRectangle(bx, by, bw, bh, Fade((Color){ 15, 30, 40, 255 }, 0.88f));
            DrawRectangleLines(bx, by, bw, bh, (Color){ 255, 215, 70, 255 });
            DrawText(ttl, bx + 18, by + 10, 26, (Color){ 255, 215, 70, 255 });
            int f1 = 20; while (f1 > 13 && MeasureText(l1, f1) > bw - 36) f1--;
            DrawText(l1, bx + 18, by + 46, f1, RAYWHITE);
            if (l2[0]) { int f2 = 17; while (f2 > 12 && MeasureText(l2, f2) > bw - 36) f2--; DrawText(l2, bx + 18, by + 76, f2, LIGHTGRAY); }
            const char *esc = TR("Esc: menu", "Esc：メニュー");
            DrawText(esc, bx + bw - 14 - MeasureText(esc, 14), by + 12, 14, GRAY);
        }
        hud_done:
        EndDrawing();
        // リプレイ用に記録（試合中だけ。描いたままの姿勢で）
        if (mode == MODE_MATCH && app == APP_PLAY) {
            RFrame f = { simDt, ball.pos, ball.active || ball.bounces > 0, P.pos, ai.pos, botControls ? bot.anim : P.anim, ai.anim, PersonaShirt(ai.persona), gYouPersona ? PersonaShirt(gYouPersona) : (Color){ 240, 240, 245, 255 } };
            RecPush(&f);
        }
    }

    LogFlush(&plog, "?");
    if (plog.f) fclose(plog.f);
    SfxClose();
    CloseWindow();
    return 0;
}
