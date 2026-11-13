// SWEET SPOT — 打点と球種がちゃんと結果に効く、球足の速いリアル系テニス
// ゲームの流れ（メニュー・入力・試合の進行・画面）
#include "game.h"

static const char *MODE_NAMES[MODE_COUNT] = { "MATCH", "RALLY", "RETURN", "SERVE" };

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
    // コースの狙い（右手の U/O で左右、I/、で深さ。溜めている間に押し続けると動く）
    float aimX = 0.0f, aimDepth = -9.0f;   // マウスで動かす狙い（ずっと残る）
    enum { SRC_KEY, SRC_LMB, SRC_RMB } chargeSrc = SRC_KEY;   // 何で溜め始めたか（離す判定に使う）
    // 簡単モード：早く離したとき、一番いい瞬間まで待って振る
    struct { bool active; ShotType type; Vector3 target; float charge; float age; float releaseAge; float penalty; } swingQ = { 0 };

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
    int prevBounces = 0;

    while (!WindowShouldClose() && !quitGame) {
        float frameDt = fminf(GetFrameTime(), 0.1f);
        float simDt = frameDt * (slowMo ? 0.25f : 1.0f);

        // ================= メニュー（タイトル / 一時停止） =================
        if (app != APP_PLAY) menuFrames++; else menuFrames = 0;
        if (app != APP_PLAY) {
            enum { MI_PLAY, MI_MODE, MI_AI, MI_STYLE, MI_COURT, MI_ASSIST, MI_FEED, MI_SENS, MI_CAMERA, MI_RESTART, MI_TITLE, MI_QUIT };
            int items[13], n = 0;
            items[n++] = MI_PLAY;
            items[n++] = MI_MODE; items[n++] = MI_AI; items[n++] = MI_STYLE; items[n++] = MI_COURT; items[n++] = MI_ASSIST;
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
        if (IsKeyPressed(KEY_TAB)) showStats = !showStats;
        if (IsKeyPressed(KEY_H)) assistLevel = (assistLevel + 2) % 3;
        if (IsKeyPressed(KEY_N)) style = (PlayStyle)((style + 1) % STYLE_COUNT);
        plog.assist = assistLevel;
        plog.style = style;
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

        // ---- マウス：カーソルの固定と、狙いの移動 ----
        bool clickConsumed = false;
        if (IsKeyPressed(KEY_ESCAPE)) {   // Esc：一時停止して設定メニュー
            app = APP_PAUSE; menuSel = 0;
            mouseLocked = false; EnableCursor();
            chargingShot = -1; chargingServe = -1;
        }
        if (!mouseLocked && (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) || IsMouseButtonPressed(MOUSE_BUTTON_RIGHT))) {
            mouseLocked = true; DisableCursor(); clickConsumed = true;   // クリックで戻す（このクリックでは打たない）
        }
        {
            Vector2 md = mouseLocked ? GetMouseDelta() : (Vector2){ 0 };
            if (skipMouseDelta) { md = (Vector2){ 0 }; skipMouseDelta = false; }   // メニューから戻った直後の跳びを無視
            aimX     += md.x * AIM_SENS * aimSens;
            aimDepth += md.y * AIM_SENS * aimSens;   // マウスを奥に（上に）動かすと深く
            // キーボードでも動かせる（U/O で左右、I/、で深さ）
            if (IsKeyDown(KEY_U)) aimX -= 6.0f * frameDt;
            if (IsKeyDown(KEY_O)) aimX += 6.0f * frameDt;
            if (IsKeyDown(KEY_I)) aimDepth -= 6.0f * frameDt;
            if (IsKeyDown(KEY_COMMA)) aimDepth += 6.0f * frameDt;
            aimX = Clamp(aimX, -SINGLES_W * 0.5f, SINGLES_W * 0.5f);
            aimDepth = Clamp(aimDepth, -(HALF_LEN - 0.3f), -1.2f);
        }
        bool lmbDown = mouseLocked && !clickConsumed && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
        bool rmbDown = mouseLocked && !clickConsumed && IsMouseButtonPressed(MOUSE_BUTTON_RIGHT);
        bool spaceHeld = IsKeyDown(KEY_SPACE);

        // ---- サーブ（自分）----
        Vector3 tossIdeal = { player.x + 0.25f, SERVE_IDEAL_H, player.z - 0.35f };   // 理想の打点（これ以上の高さ）
        if (playerServing) {
            if (sv == SV_READY) {
                // サーブの狙いはサービスボックスの中
                {
                    float x0, x1;
                    ServiceBox(player.x, -1, &x0, &x1);
                    aimX = Clamp(aimX, x0 + 0.2f, x1 - 0.2f);
                    aimDepth = Clamp(aimDepth, -SERVICE_Z + 0.2f, -3.0f);
                }
                int mouseServe = -1;
                if (lmbDown || rmbDown) mouseServe = spaceHeld ? SERVE_FLAT : (lmbDown ? SERVE_KICK : SERVE_SLICE);
                for (int s = 0; s < SERVE_COUNT; s++) {
                    bool byKey = IsKeyPressed(serveKeys[s]);
                    if (!byKey && mouseServe != s) continue;
                    chargeSrc = byKey ? SRC_KEY : (lmbDown ? SRC_LMB : SRC_RMB);
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
                charge = fminf(charge + simDt / SERVE_CHARGE_TIME, 1.0f);   // トスと同じ時間の進みで溜める（スローでも同じ）
                playerAnim.windup = 0.3f + 0.7f * charge;
                playerAnim.windStroke = 2;
                bool serveReleased = chargeSrc == SRC_KEY ? IsKeyReleased(serveKeys[chargingServe])
                                   : IsMouseButtonReleased(chargeSrc == SRC_LMB ? MOUSE_BUTTON_LEFT : MOUSE_BUTTON_RIGHT);
                if (serveReleased) {
                    playerAnim.windup = 0.0f;
                    ServeType type = (ServeType)chargingServe;
                    chargingServe = -1;
                    // 直前のフレームの間で、理想の打点の高さに一番近かった瞬間を使う（高すぎる球は打てない）
                    int best = -1;
                    for (int h = 0; h < historyCount; h++) {
                        const Ball *hb = &history[h];
                        if (!hb->active || hb->bounces > 0 || hb->pos.y > SERVE_REACH + SERVE_OVERREACH) continue;
                        if (best < 0 || hb->pos.y > history[best].pos.y) best = h;   // 届く範囲で一番高いところ
                    }
                    lastServeType = type;
                    if (mode == MODE_SERVE) svStats[serveNo - 1][0]++;
                    if (best < 0) {
                        serveMsg = "WHIFF (ball above reach)";   // 空振りはフォルト
                        lastServeKmh = 0.0f;
                        plog.serves++; plog.serveWhiffs++;
                        if (plog.f) {
                            fprintf(plog.f, "%.2f,%s,%s,serve,%s,%.2f,serve,0,0,high,,,,,,,%.2f,,,,,,%.2f,%.2f,,,,,,,,WHIFF,%d,,,%d,%s\n",
                                    GetTime(), MODE_NAMES[mode], AI_LEVELS[ai.level].name, SERVES[type].name, charge,
                                    ball.pos.y, player.x, player.z, plog.assist, plog.style, SURFACES[gSurface].name);
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
                        float qH = Clamp01(1.0f - fmaxf(SERVE_IDEAL_H - hb.pos.y, 0.0f) / SERVE_TOL_H);   // 高い分には減点しない
                        float qT = Clamp01(1.0f - Vector2Length(tossOffset) / TOSS_TOL);
                        float q = qH * qT;
                        // コース：マウス（または U/O/I/、）で動かした狙い。サービスボックスの中
                        float x0, x1;
                        ServiceBox(player.x, -1, &x0, &x1);
                        Vector3 target = { Clamp(aimX, x0 + 0.2f, x1 - 0.2f), 0.0f, Clamp(aimDepth, -SERVICE_Z + 0.2f, -3.0f) };
                        lastServeH = hb.pos.y;
                        lastServeQ = q;
                        lastContactPos = hb.pos;
                        playerAnim.lastContact = (Contact){ .overhead = true, .forehand = true, .quality = q };
                        playerAnim.contactLocal = (Vector3){ hb.pos.x - player.x, hb.pos.y, player.z - hb.pos.z };
                        playerAnim.swingT = 0.0f;
                        gCasual = (style == STYLE_CASUAL);
                        ball = HitServe(hb.pos, target, type, charge, q, x0, x1, &lastServeKmh);
                        gCasual = false;
                        ball.lastHitter = HITTER_PLAYER;
                        ball.shotId = ++plog.shotSerial;
                        plog.serves++;
                        LogFlush(&plog, "?");
                        snprintf(plog.row, sizeof(plog.row),
                                 "%.2f,%s,%s,serve,%s,%.2f,serve,0,0,,%.3f,%.3f,%.3f,,,,%.2f,,,,%.0f,,%.2f,%.2f,,%.2f,%.2f,%.2f,,,",
                                 GetTime(), MODE_NAMES[mode], AI_LEVELS[ai.level].name, SERVES[type].name, charge,
                                 q, qH, qT, hb.pos.y, lastServeKmh, player.x, player.z, hb.pos.x, hb.pos.y, hb.pos.z);
                        plog.pending = true; plog.pendingId = ball.shotId; plog.pendingServe = true;
                        plog.pendingLaunch = atan2f(ball.vel.y, sqrtf(ball.vel.x * ball.vel.x + ball.vel.z * ball.vel.z)) * RAD2DEG;
                        plog.pendingApex = ball.pos.y;
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
                bool ctrl  = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
                for (int s = 0; s < SHOT_NORMAL_COUNT; s++) {
                    if (IsKeyPressed(shotKeys[s])) {
                        chargingShot = ctrl ? SHOT_DROP : (shift ? s + SHOT_NORMAL_COUNT : s);
                        chargeSrc = SRC_KEY;
                        charge = 0.0f;
                    }
                }
                // マウス：左＝トップスピン、右＝スライス、スペース＋クリック＝フラット、
                //          Shift＋クリック＝ロブ（左＝トップスピンロブ、右＝スライスロブ）、Ctrl＋クリック＝ドロップ。
                //          どこに落とすかは狙い（マウス）で自由に決める
                if (chargingShot < 0 && (lmbDown || rmbDown)) {
                    if (ctrl)           chargingShot = SHOT_DROP;
                    else if (shift)     chargingShot = lmbDown ? SHOT_TOP_LOB : SHOT_SLICE_LOB;
                    else if (spaceHeld) chargingShot = SHOT_FLAT;
                    else                chargingShot = lmbDown ? SHOT_TOP : SHOT_SLICE;
                    chargeSrc = lmbDown ? SRC_LMB : SRC_RMB;
                    charge = 0.0f;
                }
            } else {
                charge = fminf(charge + frameDt / CHARGE_TIME, 1.0f);
                // 構えの見た目：ボールのある側で構える（高い球なら頭上）
                playerAnim.windup = charge;
                if (ball.active && ball.lastHitter == HITTER_OPP)
                    playerAnim.windStroke = ball.pos.y > OVERHEAD_MIN_H && Vector3Distance(ball.pos, player) < 6.0f ? 2
                                          : (ball.pos.x >= player.x ? 0 : 1);
                bool released = chargeSrc == SRC_KEY ? IsKeyReleased(shotKeys[chargingShot])
                              : IsMouseButtonReleased(chargeSrc == SRC_LMB ? MOUSE_BUTTON_LEFT : MOUSE_BUTTON_RIGHT);
                if (released) {
                    ShotType rt = (ShotType)chargingShot;
                    float rcharge = charge;
                    chargingShot = -1;
                    playerAnim.windup = 0.0f;
                    swingQ.active = true; swingQ.type = rt; swingQ.charge = rcharge;
                    swingQ.target = EffectiveAim(rt, player.z, aimX, aimDepth, style);
                    swingQ.age = -1.0f;   // すぐ振る
                    swingQ.releaseAge = ball.age;
                    swingQ.penalty = 1.0f;
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
                            Contact hc = EvaluateContact(player, hb, rt, rcharge, 0.0f, 0.0f);
                            if (hc.miss == MISS_NONE && hc.quality > nowQ) nowQ = hc.quality;
                        }
                        Ball fb = ball;
                        for (int i = 0; i < (int)(window / PHYS_DT) && fb.active && fb.bounces < 2; i++) {
                            StepBall(&fb, PHYS_DT);
                            if (!(fb.bounces <= 1 && fb.pos.z > 0.0f && !fb.hitNet && (!fb.isServe || fb.bounces == 1))) continue;
                            Contact fc = EvaluateContact(player, &fb, rt, rcharge, 0.0f, 0.0f);
                            if (fc.miss == MISS_NONE && fc.quality > bestQ) { bestQ = fc.quality; bestAge = fb.age; }
                        }
                        gCasual = false;
                        if (bestAge > 0.0f && bestQ > nowQ + 0.03f) {
                            swingQ.age = bestAge;
                            if (!cas) swingQ.penalty = Clamp(1.0f - (bestAge - ball.age) / ADAPT_PENALTY_T, 0.10f, 1.0f);
                            playerAnim.windup = rcharge;   // 構えたまま待つ
                        }
                    }
                }
            }
        }
        // ---- スイングの実行（すぐ、または簡単モードで待っていた瞬間）----
        if (swingQ.active && (swingQ.age < 0.0f || ball.age >= swingQ.age - 0.5f / 60.0f ||
                              !ball.active || ball.lastHitter != HITTER_OPP)) {
            swingQ.active = false;
            ShotType type = swingQ.type;
            Vector3 target = swingQ.target;
            float swingCharge = swingQ.charge;
            playerAnim.windup = 0.0f;
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
                        Vector3 toBall = { hb->pos.x - player.x, 0.0f, hb->pos.z - player.z };
                        float moveToward = (Vector3Length(toBall) > 0.01f) ? Vector3DotProduct(playerVel, Vector3Normalize(toBall)) : 0.0f;
                        Contact hc = EvaluateContact(player, hb, type, swingCharge, moveToward, playerSliding ? SLIDE_REACH : 0.0f);
                        bool better = (bestIdx < 0) ||
                                      (hc.miss == MISS_NONE && (c.miss != MISS_NONE || hc.quality > c.quality)) ||
                                      (hc.miss != MISS_NONE && c.miss != MISS_NONE);
                        if (better) { c = hc; bestIdx = h; }
                    }
                    if (bestIdx >= 0 && c.miss == MISS_NONE) ball = history[bestIdx];
                    c.stepIn = -playerVel.z;
                    // 早く振ってラケットが合わせにいった分：タイミングの質を落とし、引っ張り気味にする
                    if (c.miss == MISS_NONE && swingQ.age >= 0.0f) {
                        float early = fmaxf(swingQ.age - swingQ.releaseAge, 0.0f);
                        c.qTiming *= swingQ.penalty;
                        c.quality *= swingQ.penalty;
                        c.fwdErr += early * Vector3Length(ball.vel);
                        c.timeErrMs = early * 1000.0f;   // プラス＝早い
                    }

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
                        LogTimingAnalysis(&plog, player, &pre, type, swingCharge, &bestQ, &offMs);
                        if (swingQ.age >= 0.0f) offMs += (swingQ.releaseAge - pre.age) * 1000.0f;   // 実際に離した瞬間で記録
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
                        ball = PlayerShot(&c, ball.pos, target, type, swingCharge, &lastKmh, &lastRpm, &lastAngle);
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
                            plog.pendingLaunch = lastAngle * RAD2DEG; plog.pendingApex = ball.pos.y;
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
                if (ball.shotId == plog.pendingId && ball.bounces == 0 && ball.pos.y > plog.pendingApex) plog.pendingApex = ball.pos.y;
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
            DrawFigure(player, true, (Color){ 240, 240, 245, 255 }, botControls ? &bot.anim : &playerAnim,
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
                gCasual = (style == STYLE_CASUAL);
                Assist as = ComputeAssist(player, playerVel, &ball, at, chargingShot >= 0 ? charge : 0.5f);
                gCasual = false;
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

            // 狙い：相手コートに印（溜めている間は球種の色、それ以外は白）
            if (!botControls && (playerServing || (mode != MODE_SERVE && (chargingShot >= 0 || (ball.active && ball.lastHitter == HITTER_OPP))))) {
                Vector3 et = (chargingShot >= 0) ? EffectiveAim((ShotType)chargingShot, player.z, aimX, aimDepth, style)
                                                 : (Vector3){ aimX, 0.0f, aimDepth };
                if (playerServing) {
                    float bx0, bx1;
                    ServiceBox(player.x, -1, &bx0, &bx1);
                    et = (Vector3){ Clamp(aimX, bx0 + 0.2f, bx1 - 0.2f), 0.0f, Clamp(aimDepth, -SERVICE_Z + 0.2f, -3.0f) };
                }
                Vector3 am = { et.x, 0.015f, et.z };
                Color ac = (chargingShot >= 0) ? SHOTS[chargingShot].color
                         : (chargingServe >= 0 ? SERVES[chargingServe].color : Fade(RAYWHITE, 0.7f));
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
            if (lastServeKmh > 0.0f || serveMsg[0]) {
                if (lastServeKmh > 0.0f) {
                    DrawText(TextFormat("%s  %.0f km/h", SERVES[lastServeType].name, lastServeKmh), 20, y, 18, SERVES[lastServeType].color); y += 22;
                    DrawText(TextFormat("Contact height %.2f m (ideal %.2f+)  quality %.0f%%", lastServeH, SERVE_IDEAL_H, lastServeQ * 100.0f),
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
                DrawText("A/D: position.  Mouse: aim in the box.", 20, y, 18, RAYWHITE); y += 22;
                DrawText("Hold L-click kick / R-click slice / Space+click flat", 20, y, 18, RAYWHITE); y += 22;
                DrawText("to toss, release to hit. Hold to let a bad toss drop.", 20, y, 18, RAYWHITE); y += 22;
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
            DrawText("Move to the ball. Hold a mouse button, release to swing", 20, y, 18, RAYWHITE);
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
        int gw = 300, gx = GetScreenWidth() / 2 - gw / 2, gy = GetScreenHeight() - 140;   // 下の操作説明と重ならない高さ
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
