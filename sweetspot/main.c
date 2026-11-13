// SWEET SPOT — 打点と球種がちゃんと結果に効く、球足の速いリアル系テニス
// ゲームの流れ（メニュー・入力・試合の進行・画面）
#include "game.h"

static const char *MODE_NAMES[MODE_COUNT] = { "MATCH", "RALLY", "RETURN", "SERVE" };

// 試合の統計（中継のマッチサマリーの形）。mst はサーブの入り [0=1st 打数 1=1st 入 2=2nd 打数 3=2nd 入 4=返されなかった 5=DF]
static void DrawMatchSummary(const char *title, const char *footer, const Score *sc, const MatchStats ms[2], const int mst[2][6])
{
    int W = GetScreenWidth(), H = GetScreenHeight();
    int pw = 700, rowH = 34, nRows = 11;
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
    DrawText("YOU", px + 70 - MeasureText("YOU", 24) / 2, y + 8, 24, you);
    DrawText("AI", px + pw - 70 - MeasureText("AI", 24) / 2, y + 8, 24, aiC);
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
                case 0:  label = "Aces";                    NUM(m->aces); break;
                case 1:  label = "Double Faults";           NUM(mst[k][5]); break;
                case 2:  label = "1st Serves IN";           PCT(mst[k][1], mst[k][0]); break;
                case 3:  label = "1st Serve Points Won";    PCT(m->s1Won, m->s1Pts); break;
                case 4:  label = "2nd Serve Points Won";    PCT(m->s2Won, m->s2Pts); break;
                case 5:  label = "Total Serve Returns IN";  PCT(retIn, retFaced); break;
                case 6:  label = "Break Points Won";        FRAC(m->bpWon, m->bpTotal); break;
                case 7:  label = "Net Approach Points Won"; FRAC(m->netWon, m->netPts); break;
                case 8:  label = "Total Unforced Errors";   NUM(m->unforced); break;
                case 9:  label = "Total Winners";           NUM(m->winners); break;
                default: label = "Total Points Won";        NUM(m->points); break;
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

int main(void)
{
    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_WINDOW_RESIZABLE);
    InitWindow(1280, 720, "SWEET SPOT");
    SetTargetFPS(60);
    srand((unsigned)GetTime() ^ 0x5eed);
    DisableIme(GetWindowHandle());   // 日本語入力が ON だとキーを離したことが届かず、押しっぱなしになることがある
    // マウス：マイクラと同じようにカーソルをウィンドウに固定して隠す。Esc で外す／戻す（Esc で終了しない）
    SetExitKey(KEY_NULL);
    bool mouseLocked = false;      // タイトルメニューではカーソルを出す

    static PlayLog plog = { 0 };   // プレイ記録（playlog.csv）
    LogOpen(&plog);
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
    bool  setBreak = false;            // セット間：統計を見せて、Enter / クリックで次のセットへ
    float summaryT = 0.0f;
    bool  showSummary = false;
    bool  aiTossing = false;           // AI がトスを上げている
    float aiTossT = 0.0f;         // Tab の 2 回目：試合の統計

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

    // メニュー
    AppState app = APP_TITLE;
    int   menuSel = 0;
    float aimSens = 1.0f;          // マウス感度（倍率）
    bool  forceRestart = false;    // メニューでモードや AI を変えたら、戻ったときにやり直す
    bool  quitGame = false;
    bool  skipMouseDelta = false;
    int   menuFrames = 0;              // メニューを開いてからのフレーム数

    // 直前のフレームの物理ステップごとのボールの状態。
    // キー入力はフレーム単位（1/60 秒）でしか取れないので、「離したのはこのフレームの間のどこか」として
    // その間で一番いいタイミングの瞬間を採用する（入力の粗さで損をしないように）
    Ball history[64];
    int historyCount = 0;

    // バウンドの目印
    Vector3 bouncePos = { 0 };
    float bounceT = 10.0f;
    bool  bounceByPlayer = false;      // 自分の打球が落ちた（狙いとのずれを表示する）
    int prevBounces = 0;

    while (!WindowShouldClose() && !quitGame) {
        float frameDt = fminf(GetFrameTime(), 0.1f);
        float simDt = frameDt * (slowMo ? 0.25f : 1.0f);

        // ================= メニュー（タイトル / 一時停止） =================
        if (app != APP_PLAY) menuFrames++; else menuFrames = 0;
        if (app != APP_PLAY) {
            enum { MI_PLAY, MI_MODE, MI_SETS, MI_AI, MI_STYLE, MI_COURT, MI_ASSIST, MI_FEED, MI_SENS, MI_CAMERA, MI_RESTART, MI_TITLE, MI_QUIT };
            int items[16], n = 0;
            items[n++] = MI_PLAY;
            items[n++] = MI_MODE; items[n++] = MI_SETS; items[n++] = MI_AI; items[n++] = MI_STYLE; items[n++] = MI_COURT; items[n++] = MI_ASSIST;
            items[n++] = MI_FEED; items[n++] = MI_SENS; items[n++] = MI_CAMERA;
            if (app == APP_PAUSE) { items[n++] = MI_RESTART; items[n++] = MI_TITLE; }
            items[n++] = MI_QUIT;
            static const char *assistNames[3] = { "OFF", "TIMING", "TIMING + SPOT" };
            static const char *camNames[3] = { "BEHIND", "SIDE VIEW", "FOLLOW BALL" };

            int W = GetScreenWidth(), H = GetScreenHeight();
            int pw = 640, rowH = 40, ph = 150 + n * rowH + 110;
            int px = W / 2 - pw / 2, py = H / 2 - ph / 2;
            int rowY0 = py + 110;

            // 入力：W/S（↑↓）で選ぶ、A/D（←→）で変える、Enter で決定。マウス：左クリックで次、右クリックで前
            int change = 0; bool activate = false;
            if (IsKeyPressed(KEY_W) || IsKeyPressed(KEY_UP))   menuSel = (menuSel + n - 1) % n;
            if (IsKeyPressed(KEY_S) || IsKeyPressed(KEY_DOWN)) menuSel = (menuSel + 1) % n;
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
            if (app == APP_PAUSE && IsKeyPressed(KEY_ESCAPE)) { activate = true; menuSel = 0; }   // Esc でそのまま戻る
            int it = items[menuSel];
            if (activate && it != MI_PLAY && it != MI_RESTART && it != MI_TITLE && it != MI_QUIT) { change = +1; activate = false; }
            if (change != 0) {
                switch (it) {
                    case MI_MODE:   mode = (PracticeMode)((mode + MODE_COUNT + change) % MODE_COUNT); forceRestart = true; break;
                    case MI_AI:     ai.level = (ai.level + 3 + change) % 3; forceRestart = true; break;
                    case MI_SETS:   setsToWin = (setsToWin - 1 + 3 + change) % 3 + 1; forceRestart = true; break;
                    case MI_STYLE:  style = (PlayStyle)((style + STYLE_COUNT + change) % STYLE_COUNT); break;
                    case MI_COURT:  gSurface = (SurfaceType)((gSurface + SURF_COUNT + change) % SURF_COUNT); break;
                    case MI_ASSIST: assistLevel = (assistLevel + 3 + change) % 3; break;
                    case MI_FEED:   proFeed = !proFeed; break;
                    case MI_SENS:   aimSens = Clamp(aimSens + 0.25f * change, 0.25f, 3.0f); break;
                    case MI_CAMERA: camMode = (camMode + 3 + change) % 3; break;
                    default: break;
                }
            }
            if (activate) {
                if (it == MI_PLAY || it == MI_RESTART) {
                    if (app == APP_TITLE || it == MI_RESTART) forceRestart = true;
                    app = APP_PLAY;
                    mouseLocked = true; DisableCursor(); skipMouseDelta = true;
                } else if (it == MI_TITLE) {
                    app = APP_TITLE; menuSel = 0;
                } else if (it == MI_QUIT) {
                    quitGame = true;
                }
            }

            BeginDrawing();
            ClearBackground((Color){ 30, 70, 60, 255 });
            // 背景：コートの線をうっすら
            for (int i = 0; i < 9; i++) DrawLine(W / 2 - 300 + i * 75, H, W / 2 - 120 + i * 30, 0, Fade(RAYWHITE, 0.06f));
            DrawRectangle(px, py, pw, ph, Fade(BLACK, 0.75f));
            DrawRectangleLines(px, py, pw, ph, Fade(RAYWHITE, 0.4f));
            const char *title = app == APP_TITLE ? "SWEET SPOT" : "PAUSED";
            DrawText(title, W / 2 - MeasureText(title, 52) / 2, py + 24, 52, YELLOW);
            const char *sub = app == APP_TITLE ? "real tennis where contact point and spin matter" : "Esc: resume";
            DrawText(sub, W / 2 - MeasureText(sub, 18) / 2, py + 82, 18, LIGHTGRAY);
            for (int i = 0; i < n; i++) {
                int ry = rowY0 + i * rowH;
                bool sel = i == menuSel;
                if (sel) DrawRectangle(px + 20, ry, pw - 40, rowH - 4, Fade(YELLOW, 0.18f));
                const char *label = "", *value = "";
                switch (items[i]) {
                    case MI_PLAY:    label = app == APP_TITLE ? TextFormat("PLAY  (%s)", MODE_NAMES[mode]) : "RESUME"; break;
                    case MI_MODE:    label = "Mode";            value = MODE_NAMES[mode]; break;
                    case MI_SETS:    label = "Match length";    value = setsToWin == 1 ? "1 SET" : (setsToWin == 2 ? "BEST OF 3" : "BEST OF 5"); break;
                    case MI_AI:      label = "AI level";        value = AI_LEVELS[ai.level].name; break;
                    case MI_STYLE:   label = "Style";           value = style == STYLE_CASUAL ? "CASUAL (easy)" : "REAL"; break;
                    case MI_COURT:   label = "Court";           value = gSurface == SURF_CLAY ? "CLAY (slow, high)"
                                                                       : gSurface == SURF_GRASS ? "GRASS (fast, low)" : "HARD"; break;
                    case MI_ASSIST:  label = "Assist";          value = assistNames[assistLevel]; break;
                    case MI_FEED:    label = "Practice feed";   value = proFeed ? "PRO" : "CLUB"; break;
                    case MI_SENS:    label = "Mouse sensitivity"; value = TextFormat("%.2f", aimSens); break;
                    case MI_CAMERA:  label = "Camera";          value = camNames[camMode]; break;
                    case MI_RESTART: label = "RESTART"; break;
                    case MI_TITLE:   label = "BACK TO TITLE"; break;
                    case MI_QUIT:    label = "QUIT"; break;
                }
                DrawText(label, px + 40, ry + 8, 24, sel ? YELLOW : RAYWHITE);
                if (value[0]) {
                    const char *v = TextFormat("<  %s  >", value);
                    DrawText(v, px + pw - 40 - MeasureText(v, 24), ry + 8, 24, sel ? YELLOW : RAYWHITE);
                }
            }
            int hy = rowY0 + n * rowH + 14;
            DrawText("W/S select   A/D change   Enter OK   (mouse: L-click next, R-click back)", px + 30, hy, 16, LIGHTGRAY);
            DrawText("In game: WASD move, mouse aim, L-click top, R-click slice, Space+click flat,", px + 30, hy + 24, 16, LIGHTGRAY);
            DrawText("Shift+click lob, Ctrl+click drop, Esc menu", px + 30, hy + 46, 16, LIGHTGRAY);
            EndDrawing();
            continue;
        }

        if (IsKeyPressed(KEY_C)) camMode = (camMode + 1) % 3;
        if (IsKeyPressed(KEY_T)) slowMo = !slowMo;
        if (IsKeyPressed(KEY_V)) proFeed = !proFeed;
        if (IsKeyPressed(KEY_F)) feedTimer = 0.0f;
        bool restart = forceRestart;
        forceRestart = false;
        if (IsKeyPressed(KEY_G)) { mode = (PracticeMode)((mode + 1) % MODE_COUNT); restart = true; }
        for (int k = 0; k < 3; k++) if (mode == MODE_MATCH && IsKeyPressed(KEY_ONE + k)) { ai.level = k; restart = true; }
        if (mode == MODE_MATCH && score.over && IsKeyPressed(KEY_ENTER)) restart = true;
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
            int lvl = ai.level;
            ai = (Opponent){ 0 };
            ai.level = lvl;
            ai.pos = AiHome();
            ai.anim.swingT = -1.0f;
            P.anim = (SwingAnim){ .swingT = -1.0f };
            P.downT = 0.0f;
            P.sliding = false;
            score = (Score){ 0 };
            score.server = rand() % 2;
            score.setsToWin = setsToWin;
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
            netFlag[0] = netFlag[1] = false;
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
            P.downT = 0.0f;
            P.sliding = false;
            ai.sliding = false; bot.sliding = false;
            P.chargingShot = -1; P.chargingServe = -1;
            if (server == 0) {
                P.pos = (Vector3){ deuce ? 0.6f : -0.6f, 0.0f, HALF_LEN + 0.15f };
                ai.pos = Rot180(ReturnPos(deuce, serveNo == 2));
                ai.standPos = ai.pos;
                P.sv = SV_READY;
            } else {
                P.pos = ReturnPos(deuce, serveNo == 2);
                ai.pos = Rot180((Vector3){ deuce ? 0.6f : -0.6f, 0.0f, HALF_LEN + 0.15f });
                ai.standPos = ai.pos;
                phaseTimer = RandRange(1.2f, 2.2f);   // 構えてからトスまで（毎回同じリズムだと読めてしまう）
                aiTossing = false;
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
        } else if (P.downT > 0.0f) {
            // 飛びついて倒れている：起き上がるまで動けない
            P.downT -= simDt;
            P.vel = (Vector3){ 0 };
        } else {
            // 溜めている間は少し遅くなる（構えながら動く）
            float spd = PLAYER_SPEED * (P.chargingShot >= 0 ? CHARGE_MOVE : 1.0f);
            Vector3 want = (Vector3Length(move) > 0.0f) ? Vector3Scale(Vector3Normalize(move), spd) : (Vector3){ 0 };
            float v = Vector3Length(P.vel);
            // 溜めながら速く走っていて、減速・切り返しをしたら滑り込む
            if (!P.sliding && P.chargingShot >= 0 && v > SLIDE_MIN_SPEED &&
                Vector3DotProduct(want, Vector3Scale(P.vel, 1.0f / v)) < v * 0.5f)
                P.sliding = true;
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
        P.anim.downT = P.downT;
        P.anim.sliding = P.sliding;
        if (Vector3Length(P.vel) > 0.3f) P.anim.slideSide = P.vel.x;   // 自分は -z 向きなので右＝+x
        P.anim.runPhase += Vector3Length(P.vel) * simDt * 2.2f;

        // ---- 狙い（ずっと残る）。自分は -z を向いているので、深く＝ -z ----
        P.aimX += in.aim.x;
        P.aimDepth -= in.aim.y;
        P.aimX = Clamp(P.aimX, -SINGLES_W * 0.5f, SINGLES_W * 0.5f);
        P.aimDepth = Clamp(P.aimDepth, -(HALF_LEN - 0.3f), -1.2f);

        // ---- サーブ（自分）----
        Vector3 tossIdeal = { P.pos.x + 0.25f, SERVE_IDEAL_H, P.pos.z - 0.35f };   // 理想の打点（これ以上の高さ）
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
                    P.tossOffset = (Vector2){ RandNormal() * TOSS_SPREAD, RandNormal() * TOSS_SPREAD };
                    ball = (Ball){ 0 };
                    ball.pos = (Vector3){ tossIdeal.x + P.tossOffset.x, 1.9f, tossIdeal.z + P.tossOffset.y };
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
                        serveMsg = "WHIFF (ball above reach)";   // 空振りはフォルト
                        P.lastServeKmh = 0.0f;
                        plog.serves++; plog.serveWhiffs++;
                        if (plog.f) {
                            fprintf(plog.f, "%.2f,%s,%s,serve,%s,%.2f,serve,0,0,high,,,,,,,%.2f,,,,,,%.2f,%.2f,,,,,,,,WHIFF,%d,,,%d,%s\n",
                                    GetTime(), MODE_NAMES[mode], AI_LEVELS[ai.level].name, SERVES[type].name, P.charge,
                                    ball.pos.y, P.pos.x, P.pos.z, plog.assist, plog.style, SURFACES[gSurface].name);
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
                        float qH = Clamp01(1.0f - fmaxf(SERVE_IDEAL_H - hb.pos.y, 0.0f) / SERVE_TOL_H);   // 高い分には減点しない
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
                        P.anim.contactLocal = (Vector3){ hb.pos.x - P.pos.x, hb.pos.y, P.pos.z - hb.pos.z };
                        P.anim.swingT = 0.0f;
                        gCasual = (style == STYLE_CASUAL);
                        ball = HitServe(hb.pos, target, type, P.charge, q, x0, x1, &P.lastServeKmh);
                        P.lastAimTarget = target;
                        gCasual = false;
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
                        trailCount = 0;
                        serveAce = false;
                        P.sv = SV_FLIGHT;
                        if (mode == MODE_MATCH) { phase = PH_PLAY; serveJudged = false; }
                    }
                } else if (ball.pos.y < 1.2f && ball.vel.y < 0.0f) {
                    // 打たずに落とした：トスのやり直し（ペナルティなし）
                    P.chargingServe = -1;
                    ball = (Ball){ 0 };
                    serveMsg = "Let the toss drop - toss again";
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
                    serveMsg = (ball.result == RESULT_NET) ? "FAULT (net)" : (serveMsg[0] ? serveMsg : "FAULT (out)");
                } else {
                    serveMsg = (ball.result == RESULT_NET) ? "DOUBLE FAULT (net)" : "DOUBLE FAULT";
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
                if (phaseTimer <= 0.0f) {
                    aiTossing = true; aiTossT = 0.0f;
                    bool dc = DeuceCourt(&score);
                    Vector3 me = { dc ? 0.6f : -0.6f, 0.0f, HALF_LEN + 0.15f };
                    ball = (Ball){ 0 };
                    ball.pos = Rot180((Vector3){ me.x + 0.25f, 1.9f, me.z - 0.35f });
                    ball.vel = (Vector3){ 0.0f, TOSS_SPEED, 0.0f };
                    ball.active = true;
                    ball.netClearance = -99.0f;
                    trailCount = 0;
                }
            } else {
                aiTossT += simDt;
                ai.anim.windStroke = 2;
                ai.anim.windup = 0.3f + 0.7f * Clamp01(aiTossT / SERVE_CHARGE_TIME);
            }
            if (aiTossing && aiTossT >= AI_TOSS_HIT_T) {
                aiTossing = false;
                ai.anim.windup = 0.0f;
                ball = AiServe(&ai, DeuceCourt(&score), serveNo == 1);
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
                P.charge = fminf(P.hold, 1.0f);
                // 構えの見た目：ボールのある側で構える（高い球なら頭上）
                P.anim.windup = P.charge;
                if (ball.active && ball.lastHitter == HITTER_OPP)
                    P.anim.windStroke = ball.pos.y > OVERHEAD_MIN_H && Vector3Distance(ball.pos, P.pos) < 6.0f ? 2
                                          : (ball.pos.x >= P.pos.x ? 0 : 1);
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
                        gCasual = cas;
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
                        gCasual = false;
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
            gCasual = (style == STYLE_CASUAL);
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
                    P.anim.contactLocal = (Vector3){ sb.x - P.pos.x, sb.y, P.pos.z - sb.z };
                    P.anim.swingT = 0.0f;
                    if (c.overhead) P.lastIdealPos = (Vector3){ P.pos.x + OVERHEAD_SIDE, OVERHEAD_IDEAL_H, P.pos.z - OVERHEAD_FWD };
                    else P.lastIdealPos = (Vector3){ P.pos.x + (c.forehand ? IDEAL_SIDE : -IDEAL_SIDE),
                                                   SHOTS[type].idealHeight, P.pos.z - IDEAL_FWD };
                    P.lastContactPos = sb;
                    if (c.miss == MISS_NONE && c.dive) { P.downT = DIVE_DOWN_TIME; P.vel = (Vector3){ 0 }; }

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

                    if (c.miss == MISS_NONE) {
                        ball = PlayerShot(&c, ball.pos, target, type, HOLD_POWER_BASE + (HOLD_POWER_MAX - HOLD_POWER_BASE) * swingCharge,
                                          &P.lastKmh, &P.lastRpm, &P.lastAngle);
                        P.lastAimTarget = target;
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
                        trailCount = 0;
                        stats[type][0]++;
                        scoredLast = false;
                    }
            }
            gCasual = false;
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
                if (AiSwing(&ai, &ball, P.pos)) {
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
        if (ball.bounces > prevBounces && ball.bounces <= 2) {
            bouncePos = ball.pos; bounceT = 0.0f;
            bounceByPlayer = ball.lastHitter == HITTER_PLAYER && ball.bounces == 1 && !botControls;
        }
        prevBounces = ball.bounces;

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
                    pointMsg = (ball.result == RESULT_NET) ? "Fault (net)" : "Fault";
                    TraceLog(LOG_INFO, "FAULT by %s", hitter == 0 ? "YOU" : "AI");
                    phase = PH_BETWEEN;
                    phaseTimer = 1.5f;
                } else {
                    winner = 1 - hitter;
                    pointMsg = "Double fault";
                    endKind = 4;
                }
            } else if (!ball.isServe || serveJudged) {
                if (ball.bounces >= 1 && ball.result != RESULT_IN && ball.result != RESULT_NONE) {
                    winner = 1 - hitter;
                    endKind = 3;
                    pointMsg = (ball.result == RESULT_NET) ? (hitter == 0 ? "Your error (net)" : "AI error (net)")
                                                           : (hitter == 0 ? "Your error (out)" : "AI error (out)");
                } else if (ball.bounces >= 2) {
                    winner = hitter;
                    endKind = ball.isServe ? 1 : 2;
                    if (ball.isServe) { pointMsg = (hitter == 0) ? "Your ace / unreturned serve" : "AI ace / unreturned serve";
                                        mst[hitter][4]++; retPending = false; }
                    else              pointMsg = (hitter == 0) ? "Your winner" : "AI winner";
                }
            }
            if (winner >= 0) {
                TraceLog(LOG_INFO, "POINT end: %s -> %s", pointMsg, winner == 0 ? "YOU" : "AI");
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
                    snprintf(scoreCall, sizeof(scoreCall), "GAME, SET%s  %s   (%d - %d)", score.over ? " AND MATCH" : "",
                             winner == 0 ? "YOU" : "AI", g[0], g[1]);
                    if (!score.over) { setBreak = true; summaryT = 0.0f; }
                }
                else if (gamesAfter != gamesBefore || (score.points[0] == 0 && score.points[1] == 0))
                    snprintf(scoreCall, sizeof(scoreCall), "GAME %s   (%d - %d)", winner == 0 ? "YOU" : "AI", score.games[0], score.games[1]);
                else                              snprintf(scoreCall, sizeof(scoreCall), "%s", ScoreText(&score));
                phase = PH_POINT_OVER;
                phaseTimer = 2.0f;
            }
        } else if (mode == MODE_MATCH && (phase == PH_BETWEEN || phase == PH_POINT_OVER)) {
            phaseTimer -= simDt;
            if (phase == PH_POINT_OVER) AiUpdate(&ai, &ball, simDt);   // 打ち終わったら戻る
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
                if (feedTimer <= 0.0f) {
                    if (mode == MODE_RETURN) {
                        ball = MachineServe();
                        P.pos = ReturnPos(true, false);   // リターンの構え位置へ
                        P.vel = (Vector3){ 0 };
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
        BeginDrawing();
        ClearBackground((Color){ 150, 190, 230, 255 });
        BeginMode3D(cam);
            DrawCourt();

            // プレイヤー（-z 向き）と AI（+z 向き）
            DrawFigure(P.pos, true, (Color){ 240, 240, 245, 255 }, botControls ? &bot.anim : &P.anim,
                       Vector3Length(P.vel));
            if (mode == MODE_MATCH)
                DrawFigure(ai.pos, false, (Color){ 70, 90, 200, 255 }, &ai.anim, Vector3Length(ai.vel));
            // サーブの構え：トスの前はボールを手に持っている
            if (mode == MODE_MATCH && phase == PH_SERVE && server == 1 && !aiTossing && !score.over)
                DrawSphere((Vector3){ ai.pos.x - 0.25f, 1.0f, ai.pos.z + 0.35f }, BALL_R * 1.6f, (Color){ 220, 255, 60, 255 });

            if (playerServing) {
                // 狙うサービスボックスを薄く塗る
                float x0, x1;
                ServiceBox(P.pos.x, -1, &x0, &x1);
                DrawCube((Vector3){ 0.5f * (x0 + x1), 0.003f, -SERVICE_Z * 0.5f }, x1 - x0, 0.002f, SERVICE_Z,
                         Fade(YELLOW, 0.15f));
                // 理想の打点
                DrawSphereWires(tossIdeal, 0.12f, 6, 8, Fade(RAYWHITE, 0.8f));
                // 構えているときはボールを手に持つ
                if (P.sv == SV_READY)
                    DrawSphere((Vector3){ P.pos.x + 0.25f, 1.0f, P.pos.z - 0.3f }, BALL_R, (Color){ 210, 240, 40, 255 });
            } else if (mode != MODE_SERVE && !botControls) {
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
                    DrawSphereWires((Vector3){ P.pos.x + OVERHEAD_SIDE, OVERHEAD_IDEAL_H, P.pos.z - OVERHEAD_FWD }, 0.15f, 6, 8, zc);
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
                gCasual = false;
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
                    Vector3 ip = as.overhead ? (Vector3){ P.pos.x + OVERHEAD_SIDE, OVERHEAD_IDEAL_H, P.pos.z - OVERHEAD_FWD }
                                             : (Vector3){ P.pos.x + (as.fore ? IDEAL_SIDE : -IDEAL_SIDE), SHOTS[at].idealHeight,
                                                          P.pos.z - IDEAL_FWD };
                    float rr = 0.16f + 1.4f * as.tBest;
                    Color rc = QualityColor(as.qBest);
                    DrawCircle3D(ip, rr, (Vector3){ 1, 0, 0 }, 90.0f, rc);
                    DrawCircle3D(ip, rr + 0.02f, (Vector3){ 1, 0, 0 }, 90.0f, rc);
                }
            }

            // 狙い：相手コートに印（溜めている間は球種の色、それ以外は白）
            if (!botControls && (playerServing || (mode != MODE_SERVE && (P.chargingShot >= 0 || (ball.active && ball.lastHitter == HITTER_OPP))))) {
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
                Vector3 am = { et.x, 0.015f, et.z };
                Color ac = longOut ? RED : (P.chargingShot >= 0) ? SHOTS[P.chargingShot].color
                         : (P.chargingServe >= 0 ? SERVES[P.chargingServe].color : Fade(RAYWHITE, 0.7f));
                DrawCircle3D(am, 0.45f, (Vector3){ 1, 0, 0 }, 90.0f, ac);
                DrawCircle3D(am, 0.25f, (Vector3){ 1, 0, 0 }, 90.0f, ac);
                DrawLine3D((Vector3){ am.x - 0.5f, 0.015f, am.z }, (Vector3){ am.x + 0.5f, 0.015f, am.z }, ac);
                DrawLine3D((Vector3){ am.x, 0.015f, am.z - 0.5f }, (Vector3){ am.x, 0.015f, am.z + 0.5f }, ac);
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

            // クレーはボールの跡が残る（実際の試合でも跡でイン・アウトを確かめる）
            if (gSurface == SURF_CLAY && bounceT < 5.0f) {
                Color mk = Fade((Color){ 120, 55, 25, 255 }, 0.8f * (1.0f - bounceT / 5.0f));
                DrawCylinder((Vector3){ bouncePos.x, 0.0015f, bouncePos.z }, 0.05f, 0.05f, 0.001f, 12, mk);
                DrawCylinder((Vector3){ bouncePos.x, 0.0015f, bouncePos.z + 0.06f }, 0.045f, 0.045f, 0.001f, 12, mk);
            }
            // 自分の打球：狙った場所と落ちた場所を線で結ぶ（1m 以内は緑、2.5m 以内は黄、それ以上は赤）
            if (bounceByPlayer && bounceT < 1.8f) {
                Vector3 at = { P.lastAimTarget.x, 0.02f, P.lastAimTarget.z };
                Vector3 lp = { bouncePos.x, 0.02f, bouncePos.z };
                float miss = Vector3Distance(at, lp);
                Color mc = Fade(miss < 1.0f ? GREEN : (miss < 2.5f ? YELLOW : RED), 1.0f - bounceT / 1.8f);
                DrawLine3D(at, lp, mc);
                DrawCircle3D(at, 0.3f, (Vector3){ 1, 0, 0 }, 90.0f, mc);
                DrawLine3D((Vector3){ at.x - 0.3f, at.y, at.z }, (Vector3){ at.x + 0.3f, at.y, at.z }, mc);
                DrawLine3D((Vector3){ at.x, at.y, at.z - 0.3f }, (Vector3){ at.x, at.y, at.z + 0.3f }, mc);
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
        // 左上のパネルは小さく薄く（相手コートの奥を隠さないように）
        DrawRectangle(10, 10, 500, 178, Fade(BLACK, 0.35f));
        int y = 20;
        bool showServeInfo = (mode == MODE_SERVE) || (playerServing && mode == MODE_MATCH);
        if (showServeInfo) {
            DrawText(TextFormat("%s serve  -  %s court", serveNo == 1 ? "1st" : "2nd", deuceSide ? "deuce" : "ad"),
                     20, y, 22, RAYWHITE); y += 30;
            if (P.lastServeKmh > 0.0f || serveMsg[0]) {
                if (P.lastServeKmh > 0.0f) {
                    DrawText(TextFormat("%s  %.0f km/h", SERVES[P.lastServeType].name, P.lastServeKmh), 20, y, 18, SERVES[P.lastServeType].color); y += 22;
                    DrawText(TextFormat("Contact height %.2f m (ideal %.2f+)  quality %.0f%%", P.lastServeH, SERVE_IDEAL_H, P.lastServeQ * 100.0f),
                             20, y, 18, QualityColor(P.lastServeQ)); y += 22;
                } else {
                    DrawText(SERVES[P.lastServeType].name, 20, y, 18, SERVES[P.lastServeType].color); y += 44;
                }
                if (ball.netClearance > -50.0f)
                    DrawText(TextFormat("Net clearance %+.2f m", ball.netClearance), 20, y, 18, RAYWHITE);
                y += 22;
                Color mc = (serveMsg[0] == 'I' || serveMsg[0] == 'A') ? GREEN : (serveMsg[0] == 'L' ? YELLOW : RED);
                DrawText(serveMsg, 20, y, 22, mc); y += 30;
            } else {
                DrawText("A/D: position.  Mouse: left/right.  Later release = deeper", 20, y, 18, RAYWHITE); y += 22;
                DrawText("Hold L-click kick / R-click slice / Space+click flat", 20, y, 18, RAYWHITE); y += 22;
                DrawText("to toss, release to hit. Hold to let a bad toss drop.", 20, y, 18, RAYWHITE); y += 22;
            }
        } else if (P.hasLast) {
            const Contact *c = &P.lastContact;
            if (c->miss != MISS_NONE) {
                DrawText(MissText(c->miss), 20, y, 22, RED); y += 30;
                if (c->miss != MISS_NO_BALL) {
                    DrawText(TextFormat("Ball was %.2f m from you, %+.0f deg (side=0, front=90), %.2f m high",
                                        c->side, c->angleDeg, c->height), 20, y, 16, RAYWHITE); y += 20;
                    DrawText("White ring on the ground = ideal distance, faint ring = max reach", 20, y, 16, LIGHTGRAY); y += 20;
                }
            } else {
                const char *stroke = c->overhead ? "Overhead" : (c->forehand ? "Forehand" : "Backhand");
                // バウンド後の球：上がり際（ライジング）・頂点・落ち際
                const char *tag = c->dive ? " DIVE!" : (c->volley ? " volley"
                                : (c->vyBall > 0.6f ? " on the rise" : (c->vyBall > -0.6f ? " at the top" : " dropping")));
                DrawText(TextFormat("%s%s %s  quality %.0f%%", stroke, tag,
                                    c->overhead && P.lastType != SHOT_SLICE && P.lastType < SHOT_NORMAL_COUNT ? "SMASH" : SHOTS[P.lastType].name,
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
                    const char *hStr = c->height < SHOTS[P.lastType].idealHeight - 0.2f ? "low"
                                     : (HeightErr(&SHOTS[P.lastType], c->height, c->easeKmh) > 0.2f ? "high" : "good");
                    DrawText(TextFormat("Timing   %+3.0f deg (%+.0f ms) %-5s %3.0f%%", c->angleDeg - idealAng, c->timeErrMs, tStr, c->qTiming * 100.0f), 20, y, 18, QualityColor(c->qTiming)); y += 22;
                    DrawText(TextFormat("Distance %.2f m  %-9s %3.0f%%", c->side, dStr, c->qDist * 100.0f), 20, y, 18, QualityColor(c->qDist)); y += 22;
                    DrawText(TextFormat("Height   %.2f m  %-9s %3.0f%%", c->height, hStr, c->qHeight * 100.0f), 20, y, 18, QualityColor(c->qHeight)); y += 22;
                }
                DrawText(TextFormat("%.0f km/h in -> %.0f km/h out  step %+.1f m/s", c->inKmh, P.lastKmh, c->stepIn), 20, y, 18, RAYWHITE); y += 22;
                if (ball.lastHitter == HITTER_PLAYER) {
                    if (ball.netClearance > -50.0f)
                        DrawText(TextFormat("Net clearance %+.2f m", ball.netClearance), 20, y, 18, RAYWHITE);
                    DrawText(ResultText(ball.result), 300, y, 22, ball.result == RESULT_IN ? GREEN : RED);
                }
                y += 24;
            }
        } else {
            DrawText("Move to the ball. Hold longer = deeper. Release to swing", 20, y, 18, RAYWHITE);
        }
        // 下：成績と操作説明（コートの奥を隠さない場所）
        y = GetScreenHeight() - 98;
        if (mode == MODE_SERVE)
            DrawText(TextFormat("1st in %d/%d   2nd in %d/%d   Aces %d   Double faults %d",
                                svStats[0][1], svStats[0][0], svStats[1][1], svStats[1][0], aces, doubleFaults), 20, y, 16, RAYWHITE);
        else
            DrawText(TextFormat("IN  flat %d/%d  top %d/%d  slice %d/%d  lob %d/%d  top lob %d/%d  drop %d/%d",
                                stats[SHOT_FLAT][1], stats[SHOT_FLAT][0], stats[SHOT_TOP][1], stats[SHOT_TOP][0],
                                stats[SHOT_SLICE][1], stats[SHOT_SLICE][0], stats[SHOT_SLICE_LOB][1], stats[SHOT_SLICE_LOB][0],
                                stats[SHOT_TOP_LOB][1], stats[SHOT_TOP_LOB][0], stats[SHOT_DROP][1], stats[SHOT_DROP][0]), 20, y, 16, RAYWHITE);
        y += 20;
        DrawText("WASD move  Mouse aim  L-click top  R-click slice  Space+click flat  Shift+click lob  Ctrl+click drop  Esc menu", 20, y, 16, LIGHTGRAY); y += 18;
        if (mode == MODE_MATCH)
            DrawText(TextFormat("G mode: MATCH   1/2/3 AI level: %s   C cam  T slow", AI_LEVELS[ai.level].name), 20, y, 16, LIGHTGRAY);
        else
            DrawText(TextFormat("G mode: %s   V feed: %s   F feed  C cam  T slow", MODE_NAMES[mode], proFeed ? "PRO" : "CLUB"),
                     20, y, 16, LIGHTGRAY);

        // スコア（試合）
        if (mode == MODE_MATCH) {
            // スコア表（中継のような形）：サーブの印、名前、ゲーム、ポイント
            int so = 30 * score.nSetsDone;   // 終わったセットの列
            int sbw = 300 + so, sx = GetScreenWidth() - sbw - 10, sy = 10;
            DrawRectangle(sx, sy, sbw, 96, Fade((Color){ 10, 20, 40, 255 }, 0.85f));
            DrawText("GAMES", sx + 166 + so, sy + 4, 12, LIGHTGRAY);
            DrawText(score.tiebreak ? "TIEBREAK" : "POINT", sx + 222 + so, sy + 4, 12, LIGHTGRAY);
            for (int k = 0; k < 2; k++) {
                int ry = sy + 20 + k * 36;
                Color nameC = (k == 0) ? RAYWHITE : (Color){ 140, 170, 255, 255 };
                if (k == 1) DrawLine(sx + 8, ry - 2, sx + sbw - 8, ry - 2, Fade(RAYWHITE, 0.2f));
                if (server == k && !score.over) {
                    DrawCircle(sx + 16, ry + 13, 6, YELLOW);
                    if (serveNo == 2) DrawText("2nd", sx + 26, ry + 6, 14, YELLOW);
                }
                DrawText(k == 0 ? "YOU" : "AI", sx + 58, ry + 3, 24, nameC);
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
            if (ai.lastKmh > 0.0f) {
                int ly = sy + (score.nGames ? 126 : 102);
                DrawRectangle(sx, ly - 3, sbw, 22, Fade((Color){ 10, 20, 40, 255 }, 0.6f));
                DrawText(TextFormat("AI last: %s %.0f km/h", ball.isServe && ball.lastHitter == HITTER_OPP ? "SERVE" : SHOTS[ai.lastType].name, ai.lastKmh),
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
            // 試合の統計：セット間と試合の後（Tab でもいつでも）
            if (score.over && phaseTimer <= 0.0f)
                DrawMatchSummary(score.sets[0] > score.sets[1] ? "MATCH SUMMARY - YOU WIN" : "MATCH SUMMARY - AI WINS",
                                 "Enter: new match", &score, ms, mst);
            else if (setBreak && phaseTimer <= 0.0f)
                DrawMatchSummary(TextFormat("AFTER SET %d", score.nSetsDone), "Enter / click: next set", &score, ms, mst);
            else if (showSummary)
                DrawMatchSummary("MATCH SUMMARY", "Tab: close", &score, ms, mst);
        }

        // 統計（Tab）
        if (showStats) {
            int bx = 240, by = 100, bw = 800, bh = 540;
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
            if (mode == MODE_MATCH) {
                ty += 8;
                for (int k = 0; k < 2; k++) {
                    int a = mst[k][0] ? mst[k][0] : 1, b = mst[k][2] ? mst[k][2] : 1, in = mst[k][1] + mst[k][3];
                    LINE("%-3s 1st in %d/%d (%d%%)  2nd in %d/%d (%d%%)  unreturned %d/%d  DF %d",
                         k == 0 ? "YOU" : "AI", mst[k][1], mst[k][0], mst[k][1] * 100 / a, mst[k][3], mst[k][2], mst[k][3] * 100 / b,
                         mst[k][4], in, mst[k][5]);
                }
            }
            #undef LINE
        }

        // 溜めゲージ
        int gw = 300, gx = GetScreenWidth() / 2 - gw / 2, gy = GetScreenHeight() - 140;   // 下の操作説明と重ならない高さ
        if (P.chargingShot >= 0) {
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
                DrawText(TextFormat("%s  depth %.1f m", sp->name, dz), gx, gy - 24, 20, RAYWHITE);
            else
                DrawText(TextFormat("%s  %.0f km/h  depth %.1f m%s", sp->name, Lerp(sp->minKmh, sp->maxKmh, pw), dz,
                                    dz > HALF_LEN ? "  LONG" : ""), gx, gy - 24, 20, dz > HALF_LEN ? RED : RAYWHITE);
        }
        if (P.chargingServe >= 0) {
            const ServeSpec *sp = &SERVES[P.chargingServe];
            DrawRectangle(gx, gy, gw, 20, Fade(BLACK, 0.6f));
            DrawRectangle(gx, gy, (int)(gw * P.charge), 20, sp->color);
            float sd = -ServeDepthFromTime(P.serveT);
            DrawText(TextFormat("%s  %.0f km/h  depth %.1f m%s", sp->name, Lerp(sp->minKmh, sp->maxKmh, P.charge), sd,
                                sd > SERVICE_Z ? "  LONG" : ""), gx, gy - 24, 20, sd > SERVICE_Z ? RED : RAYWHITE);
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
            DrawText(TextFormat("N style: %s", STYLE_NAMES[style]), 560, GetScreenHeight() - 30, 18, style == STYLE_CASUAL ? SKYBLUE : ORANGE);
            DrawText(TextFormat("Court: %s", SURFACES[gSurface].name), 780, GetScreenHeight() - 30, 18, LIGHTGRAY);
        }
        EndDrawing();
    }

    LogFlush(&plog, "?");
    if (plog.f) fclose(plog.f);
    CloseWindow();
    return 0;
}
