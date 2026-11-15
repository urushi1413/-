// SWEET SPOT — 打点と球種がちゃんと結果に効く、球足の速いリアル系テニス
// ゲームの流れ（メニュー・入力・試合の進行・画面）
#include "game.h"
#include <ctype.h>

static const char *MODE_NAMES[MODE_COUNT] = { "MATCH", "RALLY", "RETURN", "SERVE" };   // 記録用（英語のまま）
static const char *ModeName(int m)   // 表示用
{
    static const char *JA[MODE_COUNT] = { "試合", "ラリー練習", "リターン練習", "サーブ練習" };
    return gLang == LANG_JA ? JA[m % MODE_COUNT] : MODE_NAMES[m % MODE_COUNT];
}

// 試合の統計（中継のマッチサマリーの形）。mst はサーブの入り [0=1st 打数 1=1st 入 2=2nd 打数 3=2nd 入 4=返されなかった 5=DF]
static void DrawMatchSummary(const char *title, const char *footer, const Score *sc, const MatchStats ms[2], const int mst[2][6], const char *opp)
{
    int W = GetScreenWidth(), H = GetScreenHeight();
    int pw = 700, rowH = 32, nRows = 13;
    int ph = 132 + nRows * rowH + 44;
    int px = W / 2 - pw / 2, py = H / 2 - ph / 2;
    Color head = { 70, 40, 110, 240 }, rowA = { 20, 80, 50, 235 }, rowB = { 25, 95, 60, 235 };
    Color you = RAYWHITE, aiC = (Color){ 255, 215, 90, 255 };
    DrawRectangle(px, py, pw, 70, head);
    DrawText(title, px + 24, py + 18, 36, RAYWHITE);
    // セットのスコア
    char sets[64] = { 0 };
    for (int i = 0; i < sc->nSetsDone; i++)
        snprintf(sets + strlen(sets), sizeof(sets) - strlen(sets), "%s%d-%d", i ? "  " : "", sc->setScore[i][0], sc->setScore[i][1]);
    DrawText(sets, px + pw - 24 - MeasureText(sets, 26), py + 24, 26, RAYWHITE);
    int y = py + 74;
    DrawRectangle(px, y, pw, 40, rowB);
    DrawText(YouName(), px + 70 - MeasureText(YouName(), 24) / 2, y + 8, 24, you);
    DrawText(opp, px + pw - 70 - MeasureText(opp, 24) / 2, y + 8, 24, aiC);
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
    DrawText(footer, px + pw / 2 - MeasureText(footer, 20) / 2, y + 10, 20, LIGHTGRAY);
}

// 体勢が崩れて取った球か（ラケットでぎりぎり取って地面に当たる音を鳴らす）：飛びついた、または手が届くぎりぎり。
//   スライス・ボレー・ネット前（distFromNet が VOICE_NET_Z より近い）は除く
static bool DefensiveVoice(const Contact *c, ShotType t, float distFromNet, float hold)
{
    (void)hold;
    if (IsSliceShot(t) || (c->volley && !c->overhead) || distFromNet < VOICE_NET_Z) return false;
    return c->dive || c->side > OFFBALANCE_R;
}

// 設定の保存（sweetspot_settings.txt）：メニューで変えた設定を次に起動したときも使う
typedef struct { int sets, women, you, opp, level, style, court, assist, feed, lanes, cam, hud, guides; float sens, vol; int lang, tod, wx; } Settings;
static const char *SETTINGS_FILE = "sweetspot_settings.txt";
static void SaveSettingsFile(const Settings *s)
{
    FILE *f = fopen(SETTINGS_FILE, "w");
    if (!f) return;
    fprintf(f, "sets=%d\nwomen=%d\nplay_as=%d\nopponent=%d\nai_level=%d\nstyle=%d\ncourt=%d\nassist=%d\npro_feed=%d\naim_lanes=%d\ncamera=%d\nscreen_info=%d\nguides=%d\nsensitivity=%.2f\nvolume=%.2f\nlanguage=%d\ntime_of_day=%d\nweather=%d\nname=%s\n",
            s->sets, s->women, s->you, s->opp, s->level, s->style, s->court, s->assist, s->feed, s->lanes, s->cam, s->hud, s->guides, s->sens, s->vol, s->lang, s->tod, s->wx, gPlayerName);
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
    }
    fclose(f);
    return true;
}

// 選手の紹介（選手選択の画面に出す）
static const char *PersonaBlurb(int p)
{
    static const char *EN[PERSONA_COUNT] = {
        "No special traits. A solid all-round player.",
        "Elegant all-court genius. One-handed backhand, great slice and volleys. Loves grass.",
        "The best defender and returner. Rubber-band sliding, ice cold on big points.",
        "Left-handed. Huge topspin forehand. King of clay, never gives up.",
        "Tireless counter-puncher. Great lobs and returns. Talks to himself a lot.",
        "Explosive speed and power. Drop shots, net play, loves the big stage.",
        "Hits early and flat. Fewest errors, the best returner of the new generation.",
        "Plays far behind the baseline. Flat, low balls and almost no errors on hard courts.",
        "Huge serve and wild shot-making. Underarm serves, easily frustrated.",
        "Fast feet, takes the ball very early. Great returner, strong in deciding sets.",
        "Big serve and solid two-handed backhand. 198cm. Clay and hard.",
        "The most powerful serve in the women's game. Fierce and clutch.",
        "Heavy topspin and fast feet. Dominant on clay.",
        "Big hitter with a fast serve. Very aggressive.",
        "Fastest legs on tour. Great defence.",
        "Flat and aggressive. Turns away to fix her strings. Strong on clay.",
        "Slice backhand and volleys. Varied and clever. Loves grass.",
    };
    static const char *JA[PERSONA_COUNT] = {
        "癖のない、バランスのいい選手。",
        "優雅なオールラウンダー。片手バック、キレのあるスライスとボレー。芝が得意。",
        "守備とリターンが一番。よく滑って拾い、大事なポイントに一番強い。",
        "左利き。強烈なトップスピンのフォア。クレーの王者で、最後まであきらめない。",
        "粘り強い守備の名手。ロブとリターンがうまい。独り言が多い。",
        "爆発的な足と強打。ドロップとネットプレー。大舞台に強い。",
        "早いタイミングでフラットに打つ。ミスが少なく、リターンがうまい。",
        "ベースラインのずっと後ろで構え、低いフラットでミスをしない。ハードが得意。",
        "とても速いサーブと気まぐれな天才肌。アンダーサーブも。すぐ怒る。",
        "足が速く、上がり際を早く打つ。リターンがうまく、最終セットに強い。",
        "速いサーブと堅い両手バック。198cm。クレーとハードが得意。",
        "女子で一番速いサーブ。強気で、勝負どころに強い。",
        "回転の強いトップスピンと速い足。クレーで圧倒的。",
        "強打と速いサーブ。とても攻撃的。",
        "ツアーで一番の俊足。守備がうまい。",
        "フラットで強気。後ろを向いてガットを直す。クレーにも強い。",
        "スライスとボレー。多彩で頭がいい。芝が得意。",
    };
    // 表は 0〜8 が男子（癖なし＋8 人）、9〜14 が女子、15・16 が錦織・ズベレフ。上の並びに合わせる
    static const int ORDER[PERSONA_COUNT] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 11, 12, 13, 14, 15, 16, 9, 10 };
    int i = ORDER[((p % PERSONA_COUNT) + PERSONA_COUNT) % PERSONA_COUNT];
    return gLang == LANG_JA ? JA[i] : EN[i];
}

// タイトル・選手選択の背景：コートの上に自分と相手が並んで立つ（ボールをつく）。カメラはゆっくり揺れる
static void DrawShowcase(int youP, int oppP, Color youShirt, float t, float lookX, Vector2 *youScr, Vector2 *oppScr)
{
    Camera3D cam = { 0 };
    cam.up = (Vector3){ 0, 1, 0 }; cam.fovy = 38.0f; cam.projection = CAMERA_PERSPECTIVE;
    cam.position = (Vector3){ lookX + 0.5f * sinf(t * 0.25f), 1.75f, -HALF_LEN + 3.4f };
    cam.target   = (Vector3){ lookX, 1.05f, -HALF_LEN + 9.0f };
    DrawSky(cam);   // 空（時間帯と天気）。奥行きの情報も消す（消さないと前のフレームと重なって乱れる）
    BeginMode3D(cam);
    DrawCourt();
    for (int k = 0; k < 2; k++) {
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
    AtmosOverlay(cam);
}

int main(void)
{
    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_WINDOW_RESIZABLE);
    InitWindow(1280, 720, "SWEET SPOT");
    SfxInit();
    UiFontInit();   // 日本語フォント
    InitBoards();   // 奥の壁の看板とスピードメーター
    SetTargetFPS(60);
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
    int   setsToWin = 2;               // 試合の長さ：1=1 セット 2=3 セットマッチ 3=5 セットマッチ
    bool  setBreak = false;
    float crowdExcite = 0.0f;            // 観客の盛り上がり（0〜1）
    float stagS = 0.0f;                  // 自分のよろめきの強さ（見た目）
    Vector3 lineCallPos = { 0 };         // 線審がアウトをコールした球の位置
    float lineCallT = 0.0f;
    char  rageMsg[160] = { 0 };          // ラケットを叩きつけたときの表示（審判のコール）
    float rageMsgT = 0.0f, smashPrevE = 0.0f;            // セット間：統計を見せて、Enter / クリックで次のセットへ
    float summaryT = 0.0f;
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
    ai.level = 1;
    ai.persona = 1;   // 相手の選手（1〜4）
    Score score = { 0 };
    MatchPhase phase = PH_SERVE;
    bool  serveJudged = false;
    float phaseTimer = 0.0f;
    const char *pointMsg = "";
    char  scoreCall[64] = "";          // ポイントの後に大きく出すコール（DEUCE、GAME など）
    bool  needSetup = true;            // 次のポイント（またはセカンドサーブ）の準備をする
    bool  autopilot = false;           // B：自分側も AI が操作する（観戦モード）
    Opponent bot = { 0 };              // 自分側を操作する AI（座標は 180° 回した世界）

    // メニュー
    AppState app = APP_TITLE;
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
#define SaveSettings() do { Settings s_ = { setsToWin, gWomen, gYouPersona, ai.persona, ai.level, (int)style, (int)gSurface, assistLevel, proFeed, aimLanes, \
                                            camMode, hudLevel, guideLevel, aimSens, SfxGetVolume(), gLang, gTimeSetting, gWeatherSetting }; SaveSettingsFile(&s_); } while (0)
    {
        Settings s = { setsToWin, gWomen, gYouPersona, ai.persona, ai.level, (int)style, (int)gSurface, assistLevel, proFeed, aimLanes,
                       camMode, hudLevel, guideLevel, aimSens, SfxGetVolume(), gLang, gTimeSetting, gWeatherSetting };
        if (LoadSettingsFile(&s)) {
            setsToWin = Clamp(s.sets, 1, 3); gWomen = s.women != 0;
            gYouPersona = (s.you > 0 && s.you < PERSONA_COUNT && PERSONAS[s.you].female == gWomen) ? s.you : 0;
            ai.persona = (s.opp > 0 && s.opp < PERSONA_COUNT && PERSONAS[s.opp].female == gWomen) ? s.opp : TourPersonaAt(gWomen, 0);
            ai.level = Clamp(s.level, 0, 2); style = (PlayStyle)Clamp(s.style, 0, STYLE_COUNT - 1); gSurface = (SurfaceType)Clamp(s.court, 0, SURF_COUNT - 1);
            assistLevel = Clamp(s.assist, 0, 2); proFeed = s.feed != 0; aimLanes = s.lanes != 0; camMode = Clamp(s.cam, 0, 2);
            hudLevel = Clamp(s.hud, 0, 3); guideLevel = Clamp(s.guides, 0, 2);
            aimSens = Clamp(s.sens, 0.25f, 3.0f); SfxSetVolume(Clamp(s.vol, 0.0f, 1.0f));
            gLang = s.lang == LANG_EN ? LANG_EN : LANG_JA;
            gTimeSetting = Clamp(s.tod, 0, TOD_COUNT); gWeatherSetting = Clamp(s.wx, 0, WX_COUNT);
        }
        AtmosNewMatch();
    }

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
    bool  bounceByPlayer = false;      // 自分の打球が落ちた（狙いとのずれを表示する）
    int prevBounces = 0;

    while (!WindowShouldClose() && !quitGame) {
        float frameDt = fminf(GetFrameTime(), 0.1f);
        float simDt = frameDt * (slowMo ? 0.25f : 1.0f);
        crowdExcite = fmaxf(crowdExcite - frameDt * 0.35f, 0.0f);
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
                    mode = MODE_MATCH; forceRestart = true; app = APP_PLAY;
                    mouseLocked = true; DisableCursor(); skipMouseDelta = true;
                } else {
                    tour.active = false; app = APP_TITLE; menuSel = 0;
                }
            }
            BeginDrawing();
            TourDraw(&tour, GetScreenWidth(), GetScreenHeight(), (float)GetTime());
            EndDrawing();
            continue;
        }

        // ================= メニュー（タイトル / 一時停止） =================
        if (app != APP_PLAY) menuFrames++; else menuFrames = 0;
        if (app != APP_PLAY) {
            SfxCrowd(false, 0.0f);   // メニューの間は観客の音を止める
            enum { MI_PLAY, MI_TOURNAMENT, MI_MODE, MI_SETS, MI_TOUR, MI_PLAYER, MI_OPP, MI_AI, MI_STYLE, MI_COURT, MI_ASSIST, MI_FEED, MI_SENS, MI_AIM, MI_SOUND, MI_CAMERA, MI_HUD, MI_GUIDES, MI_LANG, MI_RESTART, MI_TITLE, MI_QUIT,
                   MI_PG_MATCH, MI_PG_SETTINGS, MI_BACK, MI_NAME, MI_TIME, MI_WEATHER };
            static bool nameEdit = false;   // 名前を入力している
            // ページ：0 = タイトル / 一時停止、1 = 選手と試合、2 = 設定
            static int menuPage = 0; static AppState lastMenuApp = APP_PLAY;
            if (app != lastMenuApp) { menuPage = 0; lastMenuApp = app; }
            int items[24], n = 0;
            if (menuPage == 0) {
                items[n++] = MI_PLAY;
                if (app == APP_TITLE) items[n++] = MI_TOURNAMENT;
                items[n++] = MI_PG_MATCH; items[n++] = MI_PG_SETTINGS;
                if (app == APP_PAUSE) { items[n++] = MI_RESTART; items[n++] = MI_TITLE; }
                items[n++] = MI_QUIT;
            } else if (menuPage == 1) {
                items[n++] = MI_MODE; items[n++] = MI_TOUR; items[n++] = MI_PLAYER; items[n++] = MI_OPP; items[n++] = MI_AI;
                items[n++] = MI_COURT; items[n++] = MI_TIME; items[n++] = MI_WEATHER; items[n++] = MI_SETS; items[n++] = MI_STYLE; items[n++] = MI_BACK;
            } else {
                items[n++] = MI_NAME; items[n++] = MI_ASSIST; items[n++] = MI_AIM; items[n++] = MI_SENS; items[n++] = MI_FEED; items[n++] = MI_SOUND;
                items[n++] = MI_HUD; items[n++] = MI_GUIDES; items[n++] = MI_LANG; items[n++] = MI_BACK;
            }
            if (menuSel >= n) menuSel = 0;
            const char *assistNames[3] = { TR("OFF", "なし"), TR("TIMING", "タイミング"), TR("TIMING + SPOT", "タイミング＋立つ場所") };
            const char *camNames[3] = { TR("BEHIND", "後ろから"), TR("SIDE VIEW", "横から"), TR("FOLLOW BALL", "ボールを追う") };
            (void)camNames;

            int W = GetScreenWidth(), H = GetScreenHeight();
            bool titleMain = app == APP_TITLE && menuPage == 0;
            // 並べ方：タイトルは左に大きく、選手と試合は左のパネル（右に選手）、それ以外は真ん中のパネル
            int pw, rowH, fs, px, py, ph, rowY0;
            if (titleMain) { pw = 520; rowH = 54; fs = 30; px = 70; rowY0 = H / 2 - 30; py = rowY0 - 20; ph = n * rowH + 40; }
            else if (menuPage == 1) { pw = 600; rowH = Clamp((H - 260) / n, 28, 44); fs = rowH >= 36 ? 22 : 20; px = 40; py = 70; rowY0 = py + 90; ph = 90 + n * rowH + 30; }
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
            // 入力：W/S（↑↓）で選ぶ、A/D（←→）で変える、Enter で決定、Esc で戻る。マウス：左クリックで次、右クリックで前
            int change = 0; bool activate = false;
            bool nameEditing = nameEdit;
            if (!nameEditing && (IsKeyPressed(KEY_W) || IsKeyPressed(KEY_UP)))   menuSel = (menuSel + n - 1) % n;   // 名前の入力中は文字として使う
            if (!nameEditing && (IsKeyPressed(KEY_S) || IsKeyPressed(KEY_DOWN))) menuSel = (menuSel + 1) % n;
            if (IsKeyPressed(KEY_A) || IsKeyPressed(KEY_LEFT))  change = -1;
            if (IsKeyPressed(KEY_D) || IsKeyPressed(KEY_RIGHT)) change = +1;
            if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_SPACE)) activate = true;
            Vector2 mp = GetMousePosition();
            for (int i = 0; i < n; i++) {
                Rectangle rr = { (float)px + 20, (float)(rowY0 + i * rowH), (float)pw - 40, (float)rowH - 4 };
                if (CheckCollisionPointRec(mp, rr)) {
                    if (menuFrames > 10 && Vector2Length(GetMouseDelta()) > 0.0f) menuSel = i;   // 開いた直後のマウスの跳びは無視
                    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT))  { menuSel = i; activate = true; }
                    if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) { menuSel = i; change = -1; }
                }
            }
            if (IsKeyPressed(KEY_ESCAPE) && !nameEditing) {
                if (menuPage != 0) { int back = menuPage == 1 ? MI_PG_MATCH : MI_PG_SETTINGS; menuPage = 0; menuSel = 0;   // 前のページへ
                    int m0[8], k0 = 0; m0[k0++] = MI_PLAY; if (app == APP_TITLE) m0[k0++] = MI_TOURNAMENT; m0[k0++] = MI_PG_MATCH; m0[k0++] = MI_PG_SETTINGS;
                    for (int q = 0; q < k0; q++) if (m0[q] == back) menuSel = q;
                    n = 0; activate = false; change = 0; }
                else if (app == APP_PAUSE) { activate = true; menuSel = 0; }   // Esc でそのまま戻る
            }
            if (nameEditing) { change = 0; activate = false; }
            int it = n > 0 ? items[menuSel] : MI_BACK;
            bool action = it == MI_PLAY || it == MI_TOURNAMENT || it == MI_RESTART || it == MI_TITLE || it == MI_QUIT || it == MI_PG_MATCH || it == MI_PG_SETTINGS || it == MI_BACK || it == MI_NAME;
            if (activate && !action) { change = +1; activate = false; }
            if (change != 0 && n > 0) {
                switch (it) {
                    case MI_PLAY:   if (app == APP_TITLE) { mode = (PracticeMode)((mode + MODE_COUNT + change) % MODE_COUNT); forceRestart = true; } break;   // タイトル：←→でモード
                    case MI_MODE:   mode = (PracticeMode)((mode + MODE_COUNT + change) % MODE_COUNT); forceRestart = true; break;
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
                    case MI_GUIDES: guideLevel = (guideLevel + 3 + change) % 3; break;
                    case MI_LANG:   gLang = gLang == LANG_JA ? LANG_EN : LANG_JA; break;
                    case MI_OPP:    ai.persona = TourPersonaAt(gWomen, TourPersonaIndex(gWomen, ai.persona) + change); forceRestart = true; break;
                    case MI_SETS:   setsToWin = (setsToWin - 1 + 3 + change) % 3 + 1; forceRestart = true; break;
                    case MI_STYLE:  style = (PlayStyle)((style + STYLE_COUNT + change) % STYLE_COUNT); break;
                    case MI_COURT:  gSurface = (SurfaceType)((gSurface + SURF_COUNT + change) % SURF_COUNT); break;
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
                if (it == MI_PLAY || it == MI_RESTART) {
                    if (app == APP_TITLE || it == MI_RESTART) forceRestart = true;
                    app = APP_PLAY;
                    mouseLocked = true; DisableCursor(); skipMouseDelta = true;
                } else if (it == MI_TOURNAMENT) {
                    // 大会を始める：今のツアー（男子・女子）、試合の長さで
                    if (slamSel == SLAM_COUNT) TourStartSeason(&tour, gWomen, setsToWin);
                    else TourStart(&tour, gWomen, slamSel, setsToWin);
                    app = APP_BRACKET;
                } else if (it == MI_NAME) { nameEdit = true; while (GetCharPressed() > 0) { } }
                else if (it == MI_PG_MATCH) { menuPage = 1; menuSel = 0; }
                else if (it == MI_PG_SETTINGS) { menuPage = 2; menuSel = 0; }
                else if (it == MI_BACK) {
                    int back = menuPage == 1 ? MI_PG_MATCH : MI_PG_SETTINGS; menuPage = 0; menuSel = 0;
                    int m0[8], k0 = 0; m0[k0++] = MI_PLAY; if (app == APP_TITLE) m0[k0++] = MI_TOURNAMENT; m0[k0++] = MI_PG_MATCH; m0[k0++] = MI_PG_SETTINGS;
                    for (int q = 0; q < k0; q++) if (m0[q] == back) menuSel = q;
                } else if (it == MI_TITLE) {
                    tour.active = false;   // 大会の途中なら、やめる
                    app = APP_TITLE; menuSel = 0;
                } else if (it == MI_QUIT) {
                    quitGame = true;
                }
            }
            if (n == 0 || app == APP_PLAY || app == APP_BRACKET) { continue; }

            BeginDrawing();
            // ---- 背景 ----
            Color youShirtM = gYouPersona ? PersonaShirt(gYouPersona) : (Color){ 240, 240, 245, 255 };
            Vector2 youScr = { 0 }, oppScr = { 0 };
            if (titleMain || menuPage == 1) {
                DrawShowcase(gYouPersona, ai.persona, youShirtM, (float)GetTime(), titleMain ? 1.55f : 1.35f, &youScr, &oppScr);   // カメラを左へずらして、2 人を画面の右に
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
            } else {
                if (menuPage != 1) { DrawRectangle(px, py, pw, ph, Fade(BLACK, 0.75f)); DrawRectangleLines(px, py, pw, ph, Fade(RAYWHITE, 0.4f)); }
                else DrawRectangle(px, py, pw, ph, Fade(BLACK, 0.55f));
                const char *title = menuPage == 1 ? TR("PLAYERS & MATCH", "選手と試合") : menuPage == 2 ? TR("SETTINGS", "設定") : TR("PAUSED", "一時停止");
                int tx = menuPage == 1 ? px + 24 : W / 2 - MeasureText(title, 46) / 2;
                DrawText(title, tx, py + 22, 46, YELLOW);
                const char *sub = menuPage == 0 ? TR("Esc: resume", "Esc：戻る") : TR("Esc: back", "Esc：戻る");
                DrawText(sub, menuPage == 1 ? px + 26 : W / 2 - MeasureText(sub, 18) / 2, py + 72, 18, LIGHTGRAY);
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
                switch (items[i]) {
                    case MI_PLAY:    label = app == APP_TITLE ? TR("PLAY", "プレイ") : TR("RESUME", "続ける");
                                     if (app == APP_TITLE) { value = ModeName(mode); }
                                     break;
                    case MI_TOURNAMENT: label = TR("GRAND SLAM", "グランドスラム"); value = slamSel == SLAM_COUNT ? TR("SEASON: ALL 4", "シーズン：4 大会") : TourName(slamSel); break;
                    case MI_PG_MATCH:    label = TR("PLAYERS & MATCH", "選手と試合"); break;
                    case MI_PG_SETTINGS: label = TR("SETTINGS", "設定"); break;
                    case MI_BACK:    label = TR("BACK", "戻る"); break;
                    case MI_NAME:    label = TR("Your name (Enter to edit)", "名前（Enter で入力）");
                                     value = nameEdit ? TextFormat("%s%s", gPlayerName, fmodf((float)GetTime(), 1.0f) < 0.5f ? "_" : " ") : gPlayerName; break;
                    case MI_MODE:    label = TR("Mode", "モード");            value = ModeName(mode); break;
                    case MI_SETS:    label = TR("Match length", "試合の長さ");    value = setsToWin == 1 ? TR("1 SET", "1 セット") : (setsToWin == 2 ? TR("BEST OF 3", "3 セットマッチ") : TR("BEST OF 5", "5 セットマッチ")); break;
                    case MI_TOUR:    label = TR("Tour", "ツアー");            value = gWomen ? TR("WOMEN", "女子") : TR("MEN", "男子"); break;
                    case MI_PLAYER:  label = TR("Play as", "使う選手");         value = gYouPersona ? PERSONAS[gYouPersona].name : TextFormat(TR("%s (no traits)", "%s（癖なし）"), gPlayerName); break;
                    case MI_OPP:     label = TR("Opponent", "相手");        value = PERSONAS[ai.persona].name; break;
                    case MI_HUD:     label = TR("Screen info (F1)", "画面の表示（F1）"); value = HUD_NAMES[hudLevel]; break;
                    case MI_GUIDES:  label = TR("Guides (F2)", "ガイド（F2）");     value = GUIDE_NAMES[guideLevel]; break;
                    case MI_LANG:    label = "Language / 言語";  value = gLang == LANG_JA ? (UiJapaneseOk() ? "日本語" : "JAPANESE (font not found)") : "ENGLISH"; break;
                    case MI_AI:      label = TR("AI level", "AI の強さ");        value = LevelNameUI(ai.level); break;
                    case MI_STYLE:   label = TR("Style", "難しさ");           value = style == STYLE_CASUAL ? TR("CASUAL (easy)", "カジュアル（かんたん）") : TR("REAL", "リアル"); break;
                    case MI_COURT:   label = TR("Court", "コート");           value = gSurface == SURF_CLAY ? TR("CLAY (slow, high)", "クレー（遅く、高く弾む）")
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
                    case MI_RESTART: label = TR("RESTART", "やり直す"); break;
                    case MI_TITLE:   label = TR("BACK TO TITLE", "タイトルに戻る"); break;
                    case MI_QUIT:    label = TR("QUIT", "終わる"); break;
                }
                Color lc = sel ? YELLOW : RAYWHITE;
                if (titleMain) {
                    DrawText(label, px + 8, ry + (rowH - 6 - fs) / 2, fs, lc);
                    if (value[0]) { const char *v = TextFormat("<  %s  >", value); DrawText(v, px + pw - 20 - MeasureText(v, 22), ry + (rowH - 6 - 22) / 2, 22, lc); }
                } else {
                    DrawText(label, px + 40, ry + (rowH - 4 - fs) / 2, fs, lc);
                    if (value[0]) { const char *v = TextFormat("<  %s  >", value); DrawText(v, px + pw - 40 - MeasureText(v, fs), ry + (rowH - 4 - fs) / 2, fs, lc); }
                }
            }
            // ---- 選手と試合：右に 2 人の名前・身長・紹介 ----
            if (menuPage == 1) {
                int prevRight = 0;
                for (int k = 0; k < 2; k++) {
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
                    DrawText(TextFormat("%.0f cm", PersonaHeight(pp)), bx + bw - 10 - MeasureText(TextFormat("%.0f cm", PersonaHeight(pp)), 16), cy, 16, LIGHTGRAY);
                }
                // 紹介文（下に 2 人分）
                int by = H - 150, bx0 = pw + 60, bw0 = W - bx0 - 20;
                DrawRectangle(bx0, by, bw0, 130, Fade(BLACK, 0.60f));
                for (int k = 0; k < 2; k++) {   // 枠に収まるように字を小さくする
                    const char *bl = k == 0 ? TextFormat("%s：%s", gYouPersona ? PERSONAS[gYouPersona].shortName : YouName(), PersonaBlurb(gYouPersona))
                                            : TextFormat("%s：%s", PERSONAS[ai.persona].shortName, PersonaBlurb(ai.persona));
                    char buf[256]; snprintf(buf, sizeof(buf), "%s", bl);
                    int bfs = 18; while (bfs > 11 && MeasureText(buf, bfs) > bw0 - 28) bfs--;
                    DrawText(buf, bx0 + 14, by + 14 + k * 36, bfs, k == 0 ? SKYBLUE : ORANGE);
                }
                DrawText(TextFormat(TR("%s  -  %s court  -  %s", "%s  -  %s コート  -  %s"), LevelNameUI(ai.level), SurfNameUI(gSurface), ModeName(mode)), bx0 + 14, by + 90, 18, LIGHTGRAY);
            }
            // ---- 下の案内 ----
            const char *hint = TR("W/S select   A/D change   Enter OK   Esc back   (mouse: L-click next, R-click back)", "W/S：選ぶ   A/D：変える   Enter：決定   Esc：戻る（マウス：左クリックで次、右クリックで前）");
            DrawText(hint, titleMain ? px : (menuPage == 1 ? px + 20 : px + 30), H - 34, 16, LIGHTGRAY);
            if (titleMain || menuPage == 0) {
                DrawText(TR("In game: WASD move, mouse aim, L-click top, R-click slice, Space+click flat, Shift+click lob, Ctrl+click drop, Q crouch, M mute, Esc menu",
                            "試合中：WASD 移動、マウスで狙う、左クリック トップ、右クリック スライス、Space＋クリック フラット、Shift＋クリック ロブ、Ctrl＋クリック ドロップ、Esc メニュー"),
                         titleMain ? px : px + 30, H - 58, 16, Fade(LIGHTGRAY, 0.8f));
            }
            EndDrawing();
            continue;
        }

        camMode = 0;   // プレイ中は後ろからのカメラだけ（カメラの切り替えはリプレイのときに C）
        if (IsKeyPressed(KEY_F1)) { hudLevel = (hudLevel + 1) % 4; snprintf(toast, sizeof(toast), TR("SCREEN INFO: %s  (F1)", "画面の表示：%s（F1）"), HUD_NAMES[hudLevel]); toastT = 2.0f; SaveSettings(); }
        if (IsKeyPressed(KEY_F2)) { guideLevel = (guideLevel + 1) % 3; snprintf(toast, sizeof(toast), TR("GUIDES: %s  (F2)", "ガイド：%s（F2）"), GUIDE_NAMES[guideLevel]); toastT = 2.0f; SaveSettings(); }
        if (IsKeyPressed(KEY_M)) {   // M：消音 / 元に戻す
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
        if (IsKeyPressed(KEY_T)) slowMo = !slowMo;
        if (IsKeyPressed(KEY_V)) proFeed = !proFeed;
        if (IsKeyPressed(KEY_F)) feedTimer = 0.0f;
        // ================= リプレイ（スロー、横からのカメラ） =================
        if (rp.on) {
            int n = 0; const char *cap = "REPLAY";
            const RFrame *fr = rp.mode == 0 ? RecClip(&n) : Highlight(rp.clip, &n, &cap);
            rp.t += frameDt * (rp.mode == 0 ? 0.5f : 0.7f);
            while (rp.idx < n - 1 && rp.acc + fr[rp.idx].dt <= rp.t) { rp.acc += fr[rp.idx].dt; rp.idx++; }
            bool skip = IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_R) || IsKeyPressed(KEY_ESCAPE) || IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
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
                DrawSky(rp.cam);
                BeginMode3D(rp.cam);
                DrawReplayFrame(f, rp.cam);
                EndMode3D();
                AtmosOverlay(rp.cam);
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
        if (IsKeyPressed(KEY_G)) { mode = (PracticeMode)((mode + 1) % MODE_COUNT); restart = true; }
        for (int k = 0; k < 3; k++) if (mode == MODE_MATCH && IsKeyPressed(KEY_ONE + k)) { ai.level = k; restart = true; }
        if (mode == MODE_MATCH && score.over && IsKeyPressed(KEY_ENTER)) {
            if (tour.active) {   // 大会：結果を記録して組み合わせ表へ
                { int r0 = tour.round; TourRecordPlayerMatch(&tour, score.sets[0] > score.sets[1], &score); TourBeginReveal(&tour, r0); }   // 結果を 1 試合ずつ見せる
                app = APP_BRACKET; mouseLocked = false; EnableCursor();
                continue;
            }
            restart = true;
        }
        if (IsKeyPressed(KEY_B)) autopilot = !autopilot;
        if (IsKeyPressed(KEY_TAB)) {
            // Tab：セッションの統計 → （試合中なら）試合の統計 → 閉じる
            if (!showStats && !showSummary) showStats = true;
            else if (showStats && mode == MODE_MATCH) { showStats = false; showSummary = true; }
            else { showStats = false; showSummary = false; }
        }
        if (IsKeyPressed(KEY_H)) assistLevel = (assistLevel + 2) % 3;
        if (IsKeyPressed(KEY_N)) style = (PlayStyle)((style + 1) % STYLE_COUNT);
        plog.assist = assistLevel;
        plog.style = style;
        bot.level = ai.level;
        if (restart) {
            feedTimer = 0.5f;
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
            score = (Score){ 0 };
            score.server = rand() % 2;
            AtmosNewMatch();   // 時間帯と天気（おまかせなら試合ごとに変わる）
            score.setsToWin = setsToWin;
            ai.tilt = 0.0f;
            winFx = -1.0f;
            youSmashes = 0;
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
                int sv = CurrentServer(&score), rv = 1 - sv;
                Score t1 = score, t2 = score;
                AwardPoint(&t1, rv); AwardPoint(&t2, sv);
                bool rvGame = t1.games[rv] != score.games[rv] || t1.sets[rv] != score.sets[rv];
                bool setPt  = t1.sets[rv] != score.sets[rv] || t2.sets[sv] != score.sets[sv];
                if (rvGame || setPt)                      gPointImportance = 1.0f;   // ブレーク／セット／マッチポイント
                else if (score.tiebreak || (score.points[0] >= 2 && score.points[1] >= 2)) gPointImportance = 0.5f;   // 30-30、デュース、タイブレーク
                else                                      gPointImportance = 0.0f;
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
            } else {
                P.pos = HumanReturnPos(deuce, serveNo == 2);
                ai.pos = Rot180((Vector3){ deuce ? 0.6f : -0.6f, 0.0f, HALF_LEN + 0.15f });
                ai.standPos = ai.pos;
                phaseTimer = RandRange(1.2f, 2.2f);   // 構えてからトスまで（毎回同じリズムだと読めてしまう）
                aiTossing = false;
                {   // サーブ前の儀式：選手ごと（ジョコビッチは何度もつく、ナダルは肩・鼻・耳を触る…）。大事なポイントほど長い
                    const Mental *mm = &MENTAL[ai.persona % PERSONA_COUNT];
                    int nb = GetRandomValue(mm->bMin, mm->bMax) + (gPointImportance >= 0.5f ? mm->bBig : 0);
                    if (serveNo == 2 && mm->serveRit != RIT_BOUNCE) nb = GetRandomValue(1, 3);   // セカンドは短く
                    int kind = serveNo == 2 && mm->serveRit == RIT_STRINGS ? RIT_BOUNCE : mm->serveRit;
                    ai.anim.celebKind = kind; ai.anim.ritBounces = nb;
                    ai.anim.celebDur = RitualDuration(kind, nb); ai.anim.celebT = ai.anim.celebDur;
                    ai.anim.swingT = -1.0f; ai.anim.windup = 0.0f;
                    phaseTimer = ai.anim.celebDur + RandRange(0.15f, 0.45f);
                }
            }
            phase = PH_SERVE;
            serveJudged = false;
            bot.pos = Rot180(P.pos);
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
        if (IsKeyPressed(KEY_ESCAPE)) {   // Esc：一時停止して設定メニュー
            app = APP_PAUSE; menuSel = 0;
            mouseLocked = false; EnableCursor();
            P.chargingShot = -1; P.chargingServe = -1;
        }
        if (!mouseLocked && (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) || IsMouseButtonPressed(MOUSE_BUTTON_RIGHT))) {
            mouseLocked = true; DisableCursor(); clickConsumed = true;   // クリックで戻す（このクリックでは打たない）
        }
        PlayerInput in;
        ReadKeyboardMouse(&in, mouseLocked && !clickConsumed, skipMouseDelta, aimSens, frameDt);
        skipMouseDelta = false;   // メニューから戻った直後のマウスの跳びは無視した

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
            if (!P.sliding && P.chargingShot >= 0 && v > SLIDE_MIN_SPEED &&
                Vector3DotProduct(want, Vector3Scale(P.vel, 1.0f / v)) < v * 0.5f)
            { P.sliding = true; SfxSlide(Vector3Distance(cam.position, P.pos)); }
            if (P.sliding) {
                float nv = v - SLIDE_DECEL * simDt;
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
                    if (in.serve != s) continue;
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
                if (!playerGruntTried && P.serveT >= 0.50f - SfxServeLead(gYouPersona ? gYouPersona : -1)) { playerGrunt = SfxServePreBy(gYouPersona ? gYouPersona : -1, Vector3Distance(cam.position, P.pos)); playerGruntTried = true; }
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
                        if (!hb->active || hb->bounces > 0 || hb->pos.y > SERVE_REACH + SERVE_OVERREACH) continue;
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

        // ---- 試合：AI のサーブ ----
        if (mode == MODE_MATCH && phase == PH_SERVE && server == 1 && !score.over) {
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
                    bool dc = DeuceCourt(&score);
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
                    RandRange(0.0f, 1.0f) < fminf(1.0f, SfxGruntChance(ai.persona) + 0.3f))   // サーブは声が出やすい
                    aiGrunt = SfxServePreBy(ai.persona, Vector3Distance(cam.position, ai.pos));
                ai.anim.windStroke = 2;
                ai.anim.windup = 0.3f + 0.7f * Clamp01(aiTossT / SERVE_CHARGE_TIME);
            }
            if (aiTossing && aiTossT >= AI_TOSS_HIT_T) {
                aiTossing = false;
                ai.anim.windup = 0.0f;
                ball = aiUnderarm ? AiServeUnderarm(&ai, DeuceCourt(&score)) : AiServe(&ai, DeuceCourt(&score), serveNo == 1);
                speedBoardKmh = (int)roundf(ai.lastKmh);
                SfxServe(ai.anim.lastContact.quality, ai.lastKmh, aiGrunt, Vector3Distance(cam.position, ball.pos));
                P.anim.splitT = bot.anim.splitT = SPLIT_TIME;   // レシーバーのスプリットステップ
                MLogShot(mlPoint, ++mlShot, true, "SERVE", ai.lastKmh, ai.anim.lastContact.quality, false, false, false, false, ai.pos, ball.pos.y, P.pos, Vector3Length(P.vel));
                youMinDist = 99.0f;
                aiGrunt = false;
                rallyHits = 1;
                trailColor = SERVES[serveNo == 1 ? SERVE_FLAT : SERVE_KICK].color;
                trailCount = 0;
                phase = PH_PLAY;
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
                    if (c.miss == MISS_NONE && c.lunge && !c.dive) { P.anim.lungeT = LUNGE_TIME; P.anim.lungeSide = c.forehand ? 1.0f : -1.0f; }
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
                        ball = PlayerShot(&c, ball.pos, target, type, HOLD_POWER_BASE + (HOLD_POWER_MAX - HOLD_POWER_BASE) * swingCharge,
                                          &P.lastKmh, &P.lastRpm, &P.lastAngle);
                        gHumanShot = false;
                        ms[0].shots++; ms[0].speedSum += P.lastKmh;
                        ai.anim.splitT = SPLIT_TIME; ai.anim.celebT = 0.0f;
                        if (mode == MODE_MATCH) MLogShot(mlPoint, ++mlShot, false, SHOTS[type].name, P.lastKmh, c.quality, false, false, c.volley, c.isReturn, P.pos, c.height, ai.pos, Vector3Length(ai.vel));
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
            if (mode == MODE_MATCH && phase == PH_PLAY && ai.planned && ball.age >= ai.swingAge) {
                if (AiSwing(&ai, &ball, P.pos)) {
                    ms[1].shots++; ms[1].speedSum += ai.lastKmh;
                    MLogShot(mlPoint, ++mlShot, true, SHOTS[ai.lastType].name, ai.lastKmh, ai.lastQ, ai.lastAttack, ai.lastLapse, ai.anim.lastContact.volley, ai.anim.lastContact.isReturn, ai.pos, ai.anim.lastContact.height, P.pos, Vector3Length(P.vel));
                    youMinDist = 99.0f;
                    P.anim.splitT = bot.anim.splitT = SPLIT_TIME;   // 相手が打った：スプリットステップ
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
        if (ball.bounces > prevBounces && ball.bounces <= 2) {
            SfxBounce(Vector3Length(ball.vel), Vector3Distance(cam.position, ball.pos));
            bouncePos = (ball.bounces == 1) ? ball.firstBounce : ball.pos;   // 判定に使った、弾んだ瞬間の位置
            bounceT = 0.0f;
            if (ball.bounces == 1) {
                if (mode == MODE_MATCH) MLogLand(&ball);
                markPos = ball.firstBounce; markT = 0.0f;
                // 際どい球（ラインから 8cm 以内）：ホークアイのような拡大図を出す
                if (fabsf(ball.lineDist - BALL_R) < 0.08f && !ball.hitNet) { hawk = ball; hawkT = 0.0f; }
            }
            bounceByPlayer = ball.lastHitter == HITTER_PLAYER && ball.bounces == 1 && !botControls;
        }
        prevBounces = ball.bounces;
        if (!ball.active || ball.lastHitter != HITTER_OPP) { if (!(playerServing && P.sv == SV_TOSS)) { playerGrunt = false; playerGruntTried = false; } }
        if (!ball.active || ball.lastHitter != HITTER_PLAYER) { if (!aiTossing) { aiGrunt = false; aiGruntTried = false; } }
        if (ball.active && ball.hitNet && !prevHitNet) SfxNet(Vector3Length(ball.vel), Vector3Distance(cam.position, ball.pos));
        prevHitNet = ball.active && ball.hitNet;
        if (ai.sliding && !prevAiSliding) SfxSlide(Vector3Distance(cam.position, ai.pos));
        prevAiSliding = ai.sliding;
        SfxCrowd(mode == MODE_MATCH, (phase == PH_PLAY || phase == PH_SERVE) ? 0.55f : 1.0f);   // ポイント中は観客は静か
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
            int endKind = 0;                                 // 1=エース 2=ウィナー 3=ミス 4=ダブルフォールト
            // ネットに出たか：サービスラインより前に入った
            if (P.pos.z < SERVICE_Z) netFlag[0] = true;
            if (ai.pos.z > -SERVICE_Z) netFlag[1] = true;
            // リターンが入ったかどうか（入らなければ「返されなかったサーブ」）
            if (retPending && !ball.isServe && ball.bounces >= 1 && ball.result != RESULT_NONE) {
                if (ball.result != RESULT_IN) mst[retServer][4]++;
                retPending = false;
            }
            if (ball.isServe && !serveJudged && ball.bounces >= 1) {
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
                int gamesAfter = score.games[0] + score.games[1] + 6 * (score.sets[0] + score.sets[1]);
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
                        if (ai.smashes == 1) {
                            snprintf(rageMsg, sizeof(rageMsg), TL("%s SMASHES HIS RACKET!\nCode violation, racket abuse. Warning, Mr. %s", "%s がラケットを叩きつけた！\n行為違反、ラケットの乱用。警告、%s 選手"), nm, nm);
                        } else {
                            // 2 回目からはポイントペナルティ（ゲームの始まりなので、これでゲームが終わることはない）
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
                        if (kind == CELEB_NONE && mm->betweenRit && !score.over && RandRange(0.0f, 1.0f) < mm->betweenChance) {
                            ai.anim.celebKind = mm->betweenRit; ai.anim.ritBounces = 0;
                            ai.anim.celebDur = RitualDuration(mm->betweenRit, 0); ai.anim.celebT = ai.anim.celebDur;
                            ai.anim.swingT = -1.0f; ai.anim.windup = 0.0f;
                        }
                    }
                    if (kind != CELEB_NONE) {
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
                // ---- 試合に勝った：自分が喜ぶ（膝をつく・両手を上げる・跳んで拳）、観客が沸く、勝利の演出 ----
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
            if (IsKeyPressed(KEY_R)) {
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
                if ((e0 < hits[0] && e1 >= hits[0]) || (e0 < hits[3] && e1 >= hits[3])) SfxBoo(ai.smashes >= 2 ? 1.0f : 0.75f, 0.35f);   // 観客のブーイング（長いので途中でもう一度）
                ai.anim.broken = Clamp01((e1 - 0.55f) / 4.9f);   // 叩くたびに曲がっていく
                smashPrevE = e1;
                ai.anim.smashT = fmaxf(ai.anim.smashT - simDt, 0.0f);
                ai.vel = (Vector3){ 0 };
                ai.anim.velX = ai.anim.velZ = 0.0f;
            }
            // ---- X：自分がラケットを叩きつける（ポイントの間だけ）。1 回目は警告、2 回目からポイントペナルティ ----
            if (IsKeyPressed(KEY_X) && !score.over && P.anim.smashT <= 0.0f && !botControls) {
                youSmashes++;
                P.anim.smashT = SMASH_TIME; P.anim.swingT = -1.0f; P.anim.windup = 0.0f; P.anim.celebT = 0.0f;
                youSmashPrevE = 0.0f;
                phaseTimer = fmaxf(phaseTimer, SMASH_TIME + 1.2f);
                const char *nm = gYouPersona ? PERSONAS[gYouPersona].shortName : "YOU";   // テロップは英語
                if (youSmashes == 1) {
                    snprintf(rageMsg, sizeof(rageMsg), TL("%s SMASHES THE RACKET!\nCode violation, racket abuse. Warning.", "%s がラケットを叩きつけた！\n行為違反、ラケットの乱用。警告"), nm);
                } else {
                    AwardPoint(&score, 1);   // ポイントペナルティ：相手のポイント
                    snprintf(rageMsg, sizeof(rageMsg), TL("%s SMASHES THE RACKET!\nCode violation, racket abuse. Point penalty.", "%s がラケットを叩きつけた！\n行為違反、ラケットの乱用。ポイントペナルティ"), nm);
                }
                rageMsgT = SMASH_TIME + 2.5f;
                SfxAngry(gYouPersona ? gYouPersona : -1, Vector3Distance(cam.position, P.pos));
            }
            if (P.anim.smashT > 0.0f) {   // 叩きつけている間：地面に当たるたびに音、ラケットが曲がる
                float e0 = youSmashPrevE, e1 = SMASH_TIME - fmaxf(P.anim.smashT - simDt, 0.0f);
                const float hits[SMASH_NHITS] = SMASH_HITS;
                for (int k = 0; k < SMASH_NHITS; k++) if (e0 < hits[k] && e1 >= hits[k]) SfxRacketSmash(Vector3Distance(cam.position, P.pos), k);
                if ((e0 < hits[0] && e1 >= hits[0]) || (e0 < hits[3] && e1 >= hits[3])) SfxBoo(youSmashes >= 2 ? 1.0f : 0.75f, 0.35f);
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
            if (phaseTimer <= 0.0f && !setBreak) {
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

        // 次の球出し（練習）：打球が落ちた / 打ち損ねて2バウンド / 止まった
        if (mode == MODE_RALLY || mode == MODE_RETURN) {
            bool rallyOver = !ball.active || ball.hitNet ||
                             (ball.lastHitter == HITTER_PLAYER  && ball.bounces >= 1) ||
                             (ball.lastHitter == HITTER_OPP && ball.bounces >= 2);
            if (rallyOver) {
                feedTimer -= frameDt;
                // 相手（球出し）：先に次の球を決めて、その球を打つ位置へ動き、振り始める。当たる瞬間に球を出す
                float lead = mode == MODE_RETURN ? AI_TOSS_HIT_T * 0.5f : SWING_LEAD;
                if (!feedSwing && feedTimer <= lead) {
                    feedNext = mode == MODE_RETURN ? MachineServe() : MachineFeed(proFeed);
                    feedSwing = true;
                    ai.anim = (SwingAnim){ .swingT = 0.0f };
                    if (mode == MODE_RETURN) {   // サーブ：頭上で打つ（打点の右後ろに立つ）
                        feederPos = (Vector3){ feedNext.pos.x + 0.25f, 0.0f, feedNext.pos.z - 0.35f };
                        ai.anim.lastContact = (Contact){ .overhead = true, .forehand = true, .quality = 0.9f };
                        ai.anim.contactLocal = (Vector3){ 0.25f, feedNext.pos.y, 0.35f };
                    } else {                     // ストローク：フォアで打つ（球は体の右＝世界の -x 側）
                        feederPos = (Vector3){ feedNext.pos.x + 0.70f, 0.0f, feedNext.pos.z - 0.30f };
                        ai.anim.lastContact = (Contact){ .forehand = true, .quality = 0.8f, .height = feedNext.pos.y };
                        ai.anim.contactLocal = (Vector3){ 0.70f, feedNext.pos.y, 0.30f };
                        ai.anim.slice = feedNext.spin.x < 0.0f;
                    }
                    ai.anim.swingT = fmaxf(0.0f, (mode == MODE_RETURN ? 0.0f : SWING_LEAD) - feedTimer);
                    ai.pos = feederPos;
                }
                if (feedTimer <= 0.0f) {
                    if (mode == MODE_RETURN) {
                        ball = feedSwing ? feedNext : MachineServe();
                        P.pos = HumanReturnPos(true, false);   // リターンの構え位置へ
                        P.vel = (Vector3){ 0 };
                    } else {
                        ball = feedSwing ? feedNext : MachineFeed(proFeed);
                    }
                    feedSwing = false;
                    trailColor = Fade(LIGHTGRAY, 0.6f);
                    trailCount = 0;
                    feedTimer = 1.2f;
                    P.anim.splitT = SPLIT_TIME;
                }
            }
            if (ai.anim.swingT >= 0.0f) ai.anim.swingT = fminf(ai.anim.swingT + simDt, 5.0f);   // 球出しの振りを進める
        }

        // ---- カメラ ----
        if (camMode == 0) {        // プレイヤーの後ろ
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

        // ---- 描画 ----
        UpdateSpeedBoard(speedBoardKmh);   // スピードメーター（数字が変わったときだけ描き直す）
        BeginDrawing();
        DrawSky(cam);   // 空（時間帯と天気）
        BeginMode3D(cam);
            DrawCourt();
            if (mode == MODE_MATCH) DrawStadium(cam.position, ball.pos, crowdExcite, (float)GetTime(), lineCallPos, lineCallT);

            // プレイヤー（-z 向き）と AI（+z 向き）
            ApplyLook(&P.anim, gYouPersona ? gYouPersona : -1); ApplyLook(&bot.anim, gYouPersona ? gYouPersona : -1);   // 自分が使う選手の見た目
            P.anim.lefty = bot.anim.lefty = PERSONAS[gYouPersona].lefty;
            P.anim.oneHanded = bot.anim.oneHanded = PERSONAS[gYouPersona].oneHanded;
            Color youShirt = gYouPersona ? PersonaShirt(gYouPersona) : (Color){ 240, 240, 245, 255 };
            // サーブの構え（トスの前）：横向きに立ち、左手にボール
            P.anim.serveStance = P.anim.holdBall = playerServing && P.sv == SV_READY;
            bot.anim.serveStance = bot.anim.holdBall = false;
            ai.anim.serveStance = ai.anim.holdBall = mode == MODE_MATCH && phase == PH_SERVE && server == 1 && !aiTossing && !score.over && ai.anim.celebT <= 0.0f;
            DrawFigure(P.pos, true, youShirt, botControls ? &bot.anim : &P.anim,
                       Vector3Length(P.vel));
            if (mode == MODE_RALLY || mode == MODE_RETURN) {   // 練習：球を出す相手を描く（選んでいる相手の選手）
                ai.anim.lefty = false; ai.anim.oneHanded = PERSONAS[ai.persona].oneHanded;
                ApplyLook(&ai.anim, ai.persona);
                DrawFigure(feederPos, false, PersonaShirt(ai.persona), &ai.anim, 0.0f);
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
                {   // 届く限界：溜めている球種（スライスは広い）、スライド、簡単モードで広がる
                    float rr = REACH_SIDE + (P.chargingShot >= 0 ? SHOTS[P.chargingShot].reach : 0.0f)
                             + (P.sliding ? SLIDE_REACH : 0.0f) + (style == STYLE_CASUAL ? CASUAL_REACH : 0.0f);
                    DrawCircle3D((Vector3){ P.pos.x, 0.01f, P.pos.z }, rr, (Vector3){ 1, 0, 0 }, 90.0f, Fade(RAYWHITE, 0.18f));
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
                DrawCylinder(g, vr * 1.1f, vr * 1.1f, 0.002f, 14, (Color){ 0, 0, 0, 150 });   // 影
                if (ball.pos.y > 0.15f) DrawLine3D(g, ball.pos, Fade(RAYWHITE, 0.45f));       // 高さの線
                DrawSphere(ball.pos, vr, (Color){ 235, 255, 40, 255 });
                // 光のふち（ボールの後に描く。先に描くと、ふちの奥行きでボールが隠れる）
                rlDrawRenderBatchActive(); rlDisableDepthMask();   // たまっている図形を先に描いてから切り替える（後で描かれると奥が透ける）
                if (nearG > 0.01f) DrawSphere(ball.pos, vr * 1.7f, Fade((Color){ 255, 255, 140, 255 }, 0.22f * nearG));
                rlDrawRenderBatchActive(); rlEnableDepthMask();
            }
        EndMode3D();
        AtmosOverlay(cam);   // 時間帯と天気の色合い、ナイターのライト

        // ポイントの後：リプレイの案内
        if (mode == MODE_MATCH && (phase == PH_POINT_OVER || phase == PH_BETWEEN) && !score.over && !botControls && hudLevel <= 2)
            DrawText(TR("R: replay", "R：リプレイ"), GetScreenWidth() / 2 - MeasureText(TR("R: replay", "R：リプレイ"), 20) / 2, GetScreenHeight() - 170, 20, Fade(RAYWHITE, 0.8f));
        // 大会の名前とラウンド
        if (tour.active && mode == MODE_MATCH && hudLevel <= 2) {
            int lgk = gLang; gLang = LANG_EN;   // 試合中のテロップは英語
            const char *tl = TextFormat("%s  -  %s", TourName(tour.slam), TourRoundName(tour.round));
            gLang = lgk;
            DrawText(tl, (int)fmaxf(520.0f, GetScreenWidth() / 2 - MeasureText(tl, 18) / 2), 8, 18, Fade(YELLOW, 0.85f));
        }
        // ---- 際どい判定の拡大図（上から見たライン際。1cm = 6 ピクセル）----
        if (hawkT < 3.0f && mode != MODE_SERVE) {
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
        if (hudLevel <= 1) {   // 左上：当たりの分析（MINIMAL と OFF では出さない）
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
        // 下：成績と操作説明（コートの奥を隠さない場所）。FULL のときだけ
        if (hudLevel == 0) {
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
        DrawText(TR("WASD move  Mouse aim  L-click top  R-click slice  Space+click flat  Shift+click lob  Ctrl+click drop  Q crouch  X smash racket  M mute  Esc menu", "WASD 移動  マウス 狙い  左クリック トップ  右クリック スライス  Space＋クリック フラット  Shift＋クリック ロブ  Ctrl＋クリック ドロップ  Q かがむ  X ラケット破壊  M 消音  Esc メニュー"), 20, y, 16, LIGHTGRAY); y += 18;
        if (mode == MODE_MATCH)
            DrawText(TextFormat(TR("G mode: MATCH   1/2/3 AI level: %s   T slow", "G モード：試合   1/2/3 AI の強さ：%s   T スロー"), LevelNameUI(ai.level)), 20, y, 16, LIGHTGRAY);
        else
            DrawText(TextFormat(TR("G mode: %s   V feed: %s   F feed  T slow", "G モード：%s   V 球出し：%s   F 球出し  T スロー"), ModeName(mode), proFeed ? TR("PRO", "プロ") : TR("CLUB", "クラブ")),
                     20, y, 16, LIGHTGRAY);
        }

        // スコア（試合）
        if (mode == MODE_MATCH) {
            // スコア表（中継のような形）：サーブの印、名前、ゲーム、ポイント
            int so = 30 * score.nSetsDone;   // 終わったセットの列
            int sbw = 300 + so, sx = GetScreenWidth() - sbw - 10, sy = 10;
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
                DrawText(k == 0 ? YouName() : OPP_SHORT, sx + 58, ry + 3 + (k == 1 && strlen(OPP_SHORT) > 8 ? 3 : 0), k == 0 ? 24 : (strlen(OPP_SHORT) > 8 ? 16 : 20), nameC);   // 長い名前は小さく
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
            if (ai.lastKmh > 0.0f && hudLevel <= 1) {
                int ly = sy + (score.nGames ? 126 : 102);
                DrawRectangle(sx, ly - 3, sbw, 22, Fade((Color){ 10, 20, 40, 255 }, 0.6f));
                DrawText(TextFormat(TR("AI last: %s %.0f km/h", "相手の最後の球：%s %.0f km/h"), ai.lastServe ? TR("SERVE", "サーブ") : ShotNameUI(ai.lastType), ai.lastKmh),
                         sx + 8, ly, 16, LIGHTGRAY);
            }
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
            // ラケットを叩きつけた：審判のコール
            if (rageMsgT > 0.0f && rageMsg[0]) {
                rageMsgT -= frameDt;
                int w = MeasureText(rageMsg, 26);
                float al = Clamp01(rageMsgT / 0.5f);
                DrawRectangle(GetScreenWidth() / 2 - w / 2 - 18, 290, w + 36, 76, Fade((Color){ 120, 20, 20, 255 }, 0.80f * al));
                DrawText(rageMsg, GetScreenWidth() / 2 - w / 2, 300, 26, Fade(RAYWHITE, al));
            }
            // 試合の統計：セット間と試合の後（Tab でもいつでも）
            if (score.over && phaseTimer <= 0.0f)
                DrawMatchSummary(score.sets[0] > score.sets[1] ? TextFormat(TR("MATCH SUMMARY - %s WINS", "試合の結果 - %s の勝ち"), YouName()) : TextFormat(TR("MATCH SUMMARY - %s WINS", "試合の結果 - %s の勝ち"), OPP_SHORT),
                                 tour.active ? TR("Enter: back to the draw    R: highlights", "Enter：組み合わせ表へ    R：ハイライト") : TR("Enter: new match    R: highlights", "Enter：次の試合    R：ハイライト"), &score, ms, mst, OPP_SHORT);
            else if (setBreak && phaseTimer <= 0.0f)
                DrawMatchSummary(TextFormat(TR("AFTER SET %d", "第 %d セット終了"), score.nSetsDone), TR("Enter / click: next set", "Enter / クリック：次のセット"), &score, ms, mst, OPP_SHORT);
            else if (showSummary)
                DrawMatchSummary(TR("MATCH SUMMARY", "試合の統計"), TR("Tab: close", "Tab：閉じる"), &score, ms, mst, OPP_SHORT);
            // ---- 試合に勝った：紙吹雪と勝利の文字 ----
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
        }

        // 統計（Tab）
        if (showStats) {
            int bx = 240, by = 100, bw = 800, bh = 540;
            DrawRectangle(bx, by, bw, bh, Fade(BLACK, 0.85f));
            int ty = by + 16;
            #define LINE(...) do { DrawText(TextFormat(__VA_ARGS__), bx + 20, ty, 20, RAYWHITE); ty += 26; } while (0)
            DrawText(TR("SESSION STATS  (Tab to close)   saved to playlog.csv", "今回の成績（Tab で閉じる）  playlog.csv に保存"), bx + 20, ty, 22, YELLOW); ty += 36;
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
            DrawRectangle(GetScreenWidth() / 2 - tw / 2 - 16, GetScreenHeight() - 210, tw + 32, 44, Fade(BLACK, 0.75f));
            DrawText(toast, GetScreenWidth() / 2 - tw / 2, GetScreenHeight() - 202, 30, YELLOW);
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
        if (botControls) DrawText(TR("AUTOPILOT (B)", "自動操作（B）"), GetScreenWidth() - 180, GetScreenHeight() - 70, 20, ORANGE);
        if (hudLevel == 0) DrawText(TR("Tab: stats", "Tab：成績"), GetScreenWidth() - 120, GetScreenHeight() - 30, 16, LIGHTGRAY);
        if (hudLevel == 0) {
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
