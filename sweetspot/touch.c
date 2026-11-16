// SWEET SPOT — スマホ版の操作（画面を触る）。make mobile で使う
//   右手：打つボタン（トップ・スライス・フラット、小さいロブ・ドロップ）。押して溜め、離して打つ。
//         押したまま指を動かすと狙いが動く（左右、上で深く）。サーブも同じボタン（トップ＝キック、スライス、フラット）
//   左手：画面の左半分を押すとスティック（自分で動かすとき）。触らなければ自動で球の所へ走る（main.c）
//   画面は 1280×720 のまま、ページの大きさに合わせて縮めて出す（指の位置は raylib が直してくれる）
#include "game.h"

typedef struct { int id; int btn; Vector2 start, last; } TouchTrack;   // btn：-1 = スティック、0〜4 = ボタン
#define TT_MAX 6
static TouchTrack gTT[TT_MAX];
static int gTTn = 0;
static Vector2 gStick = { 0 };          // スティックの向き（-1〜1）
static bool gStickOn = false;
static Vector2 gStickC = { 0 }, gStickP = { 0 };
static bool gPressed[TOUCH_BTN_COUNT];  // 今押されているボタン（描くときに光らせる）
static bool gPauseTap = false;

// ボタンの位置と大きさ（画面 1280×720 で）
void TouchButtonRect(int b, Vector2 *c, float *r)
{
    float W = (float)GetScreenWidth(), H = (float)GetScreenHeight();
    float R = H * 0.085f;
    switch (b) {
        case TB_TOP:   *c = (Vector2){ W - R * 1.35f, H - R * 1.45f };           *r = R * 1.10f; break;   // いちばん押しやすい右下：トップスピン
        case TB_SLICE: *c = (Vector2){ W - R * 3.75f, H - R * 1.15f };           *r = R * 0.95f; break;   // その左：スライス
        case TB_FLAT:  *c = (Vector2){ W - R * 1.25f, H - R * 3.75f };           *r = R * 0.95f; break;   // その上：フラット
        case TB_LOB:   *c = (Vector2){ W - R * 3.45f, H - R * 3.35f };           *r = R * 0.68f; break;   // 斜め上：ロブ
        case TB_DROP:  *c = (Vector2){ W - R * 5.55f, H - R * 2.05f };           *r = R * 0.62f; break;   // 左上：ドロップ
        default:       *c = (Vector2){ W * 0.5f, 40.0f };                         *r = 32.0f;     break;   // 一時停止
    }
}

static int HitButton(Vector2 p)
{
    for (int b = 0; b <= TB_PAUSE; b++) {
        Vector2 c; float r; TouchButtonRect(b, &c, &r);
        if (Vector2Distance(p, c) < r * 1.15f) return b;   // 少し広めに当たる
    }
    return -2;
}

// ボタンごとの球種・サーブ・入力の種類（離したときに振る）
static void ButtonShot(int b, int *shot, int *serve, InputSource *src)
{
    switch (b) {
        case TB_TOP:   *shot = SHOT_TOP;     *serve = SERVE_KICK;  *src = SRC_KEY_J; break;
        case TB_SLICE: *shot = SHOT_SLICE;   *serve = SERVE_SLICE; *src = SRC_KEY_K; break;
        case TB_FLAT:  *shot = SHOT_FLAT;    *serve = SERVE_FLAT;  *src = SRC_KEY_L; break;
        case TB_LOB:   *shot = SHOT_TOP_LOB; *serve = SERVE_KICK;  *src = SRC_KEY_J; break;
        default:       *shot = SHOT_DROP;    *serve = SERVE_SLICE; *src = SRC_KEY_K; break;
    }
}

void ReadTouch(PlayerInput *in, float aimSens)
{
    int n = GetTouchPointCount();
    bool seen[TT_MAX] = { 0 };
    for (int b = 0; b < TOUCH_BTN_COUNT; b++) gPressed[b] = false;
    for (int i = 0; i < n && i < 10; i++) {
        int id = GetTouchPointId(i);
        Vector2 p = GetTouchPosition(i);
        int k = -1;
        for (int j = 0; j < gTTn; j++) if (gTT[j].id == id) k = j;
        if (k < 0 && gTTn < TT_MAX) {   // 新しく触れた指
            int b = HitButton(p);
            if (b == TB_PAUSE) { gPauseTap = true; continue; }
            if (b < 0 && p.x > GetScreenWidth() * 0.5f) continue;   // 右半分のボタン以外は使わない
            k = gTTn++;
            gTT[k] = (TouchTrack){ id, b >= 0 ? b : -1, p, p };
            if (b >= 0) {   // 打つボタン：溜め始める
                int shot, serve; InputSource src; ButtonShot(b, &shot, &serve, &src);
                in->shot = shot; in->serve = serve; in->source = src;
            } else { gStickOn = true; gStickC = p; }
        }
        if (k < 0) continue;
        seen[k] = true;
        TouchTrack *t = &gTT[k];
        if (t->btn >= 0) {   // 押したまま動かす：狙い（右で右、上で深く）
            gPressed[t->btn] = true;
            in->aim.x += (p.x - t->last.x) * AIM_SENS * 1.6f * aimSens;
            in->aim.y -= (p.y - t->last.y) * AIM_SENS * 1.6f * aimSens;
        } else {             // スティック
            gStickP = p;
            Vector2 d = Vector2Subtract(p, gStickC);
            float R = GetScreenHeight() * 0.11f, l = Vector2Length(d);
            if (l > R) { gStickC = Vector2Add(gStickC, Vector2Scale(d, (l - R) / l)); d = Vector2Scale(d, R / l); }   // 指についていく
            gStick = (Vector2){ d.x / R, -d.y / R };
        }
        t->last = p;
    }
    // 離れた指
    for (int j = gTTn - 1; j >= 0; j--) {
        if (seen[j]) continue;
        if (gTT[j].btn >= 0) { int shot, serve; InputSource src; ButtonShot(gTT[j].btn, &shot, &serve, &src); in->released[src] = true; }
        else { gStickOn = false; gStick = (Vector2){ 0 }; }
        gTT[j] = gTT[--gTTn];
    }
    if (gStickOn && Vector2Length(gStick) > 0.15f) in->move = gStick;
}

bool TouchStickActive(void) { return gStickOn; }
bool TouchPauseTapped(void) { bool t = gPauseTap; gPauseTap = false; return t; }

void DrawTouchControls(bool serving)
{
    static const char *EN[TOUCH_BTN_COUNT] = { "TOP", "SLICE", "FLAT", "LOB", "DROP" };
    static const char *JA[TOUCH_BTN_COUNT] = { "トップ", "スライス", "フラット", "ロブ", "ドロップ" };
    static const char *SV_EN[3] = { "KICK", "SLICE", "FLAT" };
    static const char *SV_JA[3] = { "キック", "スライス", "フラット" };
    static const Color COL[TOUCH_BTN_COUNT] = { { 255, 200, 60, 255 }, { 120, 200, 255, 255 }, { 255, 110, 90, 255 }, { 190, 150, 255, 255 }, { 140, 230, 140, 255 } };
    for (int b = 0; b < TOUCH_BTN_COUNT; b++) {
        if (serving && b >= TB_LOB) continue;   // サーブのときはロブ・ドロップは出さない
        Vector2 c; float r; TouchButtonRect(b, &c, &r);
        bool on = gPressed[b];
        DrawCircleV(c, r, Fade(BLACK, on ? 0.55f : 0.35f));
        DrawCircleLinesV(c, r, Fade(COL[b], on ? 1.0f : 0.8f));
        DrawCircleLinesV(c, r - 2, Fade(COL[b], on ? 1.0f : 0.5f));
        if (on) DrawCircleV(c, r * 0.92f, Fade(COL[b], 0.25f));
        const char *l = serving && b < 3 ? (gLang == LANG_JA ? SV_JA[b] : SV_EN[b]) : (gLang == LANG_JA ? JA[b] : EN[b]);
        int fs = b >= TB_LOB ? 20 : 24;
        DrawText(l, (int)(c.x - MeasureText(l, fs) / 2), (int)(c.y - fs / 2), fs, RAYWHITE);
    }
    {   // 一時停止
        Vector2 c; float r; TouchButtonRect(TB_PAUSE, &c, &r);
        DrawCircleV(c, r, Fade(BLACK, 0.4f));
        DrawRectangle((int)c.x - 9, (int)c.y - 11, 6, 22, RAYWHITE);
        DrawRectangle((int)c.x + 3, (int)c.y - 11, 6, 22, RAYWHITE);
    }
    if (gStickOn) {   // 左手のスティック
        float R = GetScreenHeight() * 0.11f;
        DrawCircleV(gStickC, R, Fade(BLACK, 0.3f));
        DrawCircleLinesV(gStickC, R, Fade(RAYWHITE, 0.6f));
        DrawCircleV(gStickP, R * 0.42f, Fade(RAYWHITE, 0.5f));
    }
}
