// SWEET SPOT — 入力（キーボード＋マウス）を、プレイヤーの操作（PlayerInput）に直す
// ゲームの本体は PlayerInput しか見ないので、ゲームパッドや通信相手の入力も同じ形にすれば差し替えられる
#include "game.h"

// キーボードの J/K/L：ショットは L=フラット J=トップ K=スライス（Shift でロブ・ドロップ、Ctrl でドロップ）
//                     サーブは L=フラット K=スライス J=キック
static const int KEY_OF_SOURCE[SRC_COUNT] = { [SRC_KEY_L] = KEY_L, [SRC_KEY_J] = KEY_J, [SRC_KEY_K] = KEY_K };

void ReadKeyboardMouse(PlayerInput *in, bool mouseActive, bool skipMouseDelta, float aimSens, float dt)
{
    *in = (PlayerInput){ .shot = -1, .serve = -1, .source = SRC_NONE };

    // 移動（WASD）
    if (IsKeyDown(KEY_A)) in->move.x -= 1.0f;
    if (IsKeyDown(KEY_D)) in->move.x += 1.0f;
    if (IsKeyDown(KEY_W)) in->move.y += 1.0f;
    if (IsKeyDown(KEY_S)) in->move.y -= 1.0f;

    // 狙い：マウス（奥に動かすと深く）。キーボードの U/O（左右）・I/、（深さ）でも
    if (mouseActive && !skipMouseDelta) {
        Vector2 md = GetMouseDelta();
        in->aim.x += md.x * AIM_SENS * aimSens;
        in->aim.y -= md.y * AIM_SENS * aimSens;
    }
    if (IsKeyDown(KEY_U))     in->aim.x -= 6.0f * dt;
    if (IsKeyDown(KEY_O))     in->aim.x += 6.0f * dt;
    if (IsKeyDown(KEY_I))     in->aim.y += 6.0f * dt;
    if (IsKeyDown(KEY_COMMA)) in->aim.y -= 6.0f * dt;

    bool shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
    bool ctrl  = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
    bool space = IsKeyDown(KEY_SPACE);

    // キーボードの J/K/L
    const InputSource keySrc[SHOT_NORMAL_COUNT] = { SRC_KEY_L, SRC_KEY_J, SRC_KEY_K };   // フラット / トップ / スライス
    const ServeType keyServe[SHOT_NORMAL_COUNT] = { SERVE_FLAT, SERVE_KICK, SERVE_SLICE };
    for (int s = 0; s < SHOT_NORMAL_COUNT; s++) {
        if (!IsKeyPressed(KEY_OF_SOURCE[keySrc[s]])) continue;
        in->shot = ctrl ? SHOT_DROP : (shift ? s + SHOT_NORMAL_COUNT : s);
        in->serve = keyServe[s];
        in->source = keySrc[s];
    }

    // マウス：左＝トップスピン、右＝スライス、スペース＋クリック＝フラット、
    //         Shift＋クリック＝ロブ（左＝トップスピンロブ、右＝スライスロブ）、Ctrl＋クリック＝ドロップ
    //         サーブは 左＝キック、右＝スライス、スペース＋クリック＝フラット
    bool lmb = mouseActive && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
    bool rmb = mouseActive && IsMouseButtonPressed(MOUSE_BUTTON_RIGHT);
    if (lmb || rmb) {
        if (ctrl)       in->shot = SHOT_DROP;
        else if (shift) in->shot = lmb ? SHOT_TOP_LOB : SHOT_SLICE_LOB;
        else if (space) in->shot = SHOT_FLAT;
        else            in->shot = lmb ? SHOT_TOP : SHOT_SLICE;
        in->serve = space ? SERVE_FLAT : (lmb ? SERVE_KICK : SERVE_SLICE);
        in->source = lmb ? SRC_MOUSE_L : SRC_MOUSE_R;
    }

    // 離された入力
    for (int s = SRC_KEY_L; s <= SRC_KEY_K; s++) in->released[s] = IsKeyReleased(KEY_OF_SOURCE[s]);
    in->released[SRC_MOUSE_L] = IsMouseButtonReleased(MOUSE_BUTTON_LEFT);
    in->released[SRC_MOUSE_R] = IsMouseButtonReleased(MOUSE_BUTTON_RIGHT);
}
