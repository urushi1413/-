// SWEET SPOT — 入力（キーボード＋マウス）を、プレイヤーの操作（PlayerInput）に直す
// ゲームの本体は PlayerInput しか見ないので、ゲームパッドや通信相手の入力も同じ形にすれば差し替えられる
// キーの割り当ては gKeys[]（設定の「キー設定」で変えられる。sweetspot_settings.txt に保存）
#include "game.h"

static const int DEFAULT_KEYS[ACT_COUNT] = {
    [ACT_UP] = KEY_W, [ACT_DOWN] = KEY_S, [ACT_LEFT] = KEY_A, [ACT_RIGHT] = KEY_D,
    [ACT_AIM_L] = KEY_U, [ACT_AIM_R] = KEY_O, [ACT_AIM_DEEP] = KEY_I, [ACT_AIM_SHORT] = KEY_COMMA,
    [ACT_TOP] = KEY_J, [ACT_SLICE] = KEY_K, [ACT_FLAT] = KEY_L,
    [ACT_LOB] = KEY_LEFT_SHIFT, [ACT_DROP] = KEY_LEFT_CONTROL, [ACT_CLICK_FLAT] = KEY_SPACE,
    [ACT_CROUCH] = KEY_Q, [ACT_SMASH] = KEY_X, [ACT_REPLAY] = KEY_R, [ACT_MUTE] = KEY_M,
    [ACT_TAUNT] = KEY_E, [ACT_SWAT] = KEY_Z,
};
int gKeys[ACT_COUNT];

void KeysReset(void) { for (int a = 0; a < ACT_COUNT; a++) gKeys[a] = DEFAULT_KEYS[a]; }

// 設定のファイルに書く名前（変えない）
const char *ActionId(int a)
{
    static const char *ID[ACT_COUNT] = { "up", "down", "left", "right", "aim_left", "aim_right", "aim_deep", "aim_short",
                                         "topspin", "slice", "flat", "lob", "drop", "click_flat", "crouch", "smash", "replay", "mute", "taunt", "swat" };
    return (a >= 0 && a < ACT_COUNT) ? ID[a] : "";
}

const char *ActionName(int a)
{
    static const char *EN[ACT_COUNT] = { "Move forward", "Move back", "Move left", "Move right",
        "Aim left", "Aim right", "Aim deeper", "Aim shorter",
        "Topspin (serve: kick)", "Slice (serve: slice)", "Flat (serve: flat)",
        "Lob (hold + shot)", "Drop shot (hold + shot)", "Flat (hold + click)",
        "Crouch", "Smash racket", "Replay", "Mute", "Pump up the crowd", "Swat ball into the crowd" };
    static const char *JA[ACT_COUNT] = { "前へ", "後ろへ", "左へ", "右へ",
        "狙い：左", "狙い：右", "狙い：深く", "狙い：浅く",
        "トップスピン（サーブ：キック）", "スライス（サーブ：スライス）", "フラット（サーブ：フラット）",
        "ロブ（押しながら打つ）", "ドロップ（押しながら打つ）", "フラット（押しながらクリック）",
        "かがむ", "ラケットを叩きつける", "リプレイ", "消音", "観客をあおる", "球を観客席へかっ飛ばす" };
    if (a < 0 || a >= ACT_COUNT) return "";
    return gLang == LANG_JA ? JA[a] : EN[a];
}

const char *KeyName(int k)
{
    static char ring[8][16]; static int ri = 0;   // 1 つの表示で何度も呼べるように、自分の置き場を回して使う（TextFormat の置き場は 4 つしかない）
    char *o = ring[ri = (ri + 1) % 8];
    if (k >= KEY_A && k <= KEY_Z) { snprintf(o, 16, "%c", 'A' + (k - KEY_A)); return o; }
    if (k >= KEY_ZERO && k <= KEY_NINE) { snprintf(o, 16, "%c", '0' + (k - KEY_ZERO)); return o; }
    if (k >= KEY_F1 && k <= KEY_F12) { snprintf(o, 16, "F%d", 1 + (k - KEY_F1)); return o; }
    if (k >= KEY_KP_0 && k <= KEY_KP_9) { snprintf(o, 16, "Num %d", k - KEY_KP_0); return o; }
    switch (k) {
        case KEY_SPACE: return "Space";        case KEY_COMMA: return ",";          case KEY_PERIOD: return ".";
        case KEY_SLASH: return "/";            case KEY_SEMICOLON: return ";";      case KEY_APOSTROPHE: return "'";
        case KEY_LEFT_BRACKET: return "[";     case KEY_RIGHT_BRACKET: return "]";  case KEY_BACKSLASH: return "\\";
        case KEY_MINUS: return "-";            case KEY_EQUAL: return "=";          case KEY_GRAVE: return "`";
        case KEY_UP: return "Up";              case KEY_DOWN: return "Down";        case KEY_LEFT: return "Left";   case KEY_RIGHT: return "Right";
        case KEY_LEFT_SHIFT: return "Shift";   case KEY_RIGHT_SHIFT: return "R-Shift";
        case KEY_LEFT_CONTROL: return "Ctrl";  case KEY_RIGHT_CONTROL: return "R-Ctrl";
        case KEY_LEFT_ALT: return "Alt";       case KEY_RIGHT_ALT: return "R-Alt";
        case KEY_BACKSPACE: return "Backspace"; case KEY_INSERT: return "Insert";   case KEY_DELETE: return "Delete";
        case KEY_HOME: return "Home";          case KEY_END: return "End";          case KEY_PAGE_UP: return "PgUp";  case KEY_PAGE_DOWN: return "PgDn";
        case KEY_CAPS_LOCK: return "Caps";     case KEY_KP_ENTER: return "Num Enter";
        case KEY_KP_ADD: return "Num +";       case KEY_KP_SUBTRACT: return "Num -"; case KEY_KP_MULTIPLY: return "Num *";
        case KEY_KP_DIVIDE: return "Num /";    case KEY_KP_DECIMAL: return "Num .";
        default: snprintf(o, 16, "#%d", k); return o;
    }
}

// 割り当てられないキー：メニュー・決定に使うキーと、練習や表示の切り替えに使っている固定のキー
bool KeyReserved(int k)
{
    static const int R[] = { KEY_ESCAPE, KEY_ENTER, KEY_TAB, KEY_F1, KEY_F2, KEY_F3, KEY_F12, KEY_ONE, KEY_TWO, KEY_THREE,
                             KEY_G, KEY_B, KEY_H, KEY_N, KEY_V, KEY_F, KEY_T, KEY_C };
    for (size_t i = 0; i < sizeof(R) / sizeof(R[0]); i++) if (R[i] == k) return true;
    return k <= 0;
}

// a に k を割り当てる。ほかの操作が k を使っていたら、そちらには a の前のキーを渡す（入れ替え）
void KeyAssign(int a, int k)
{
    if (a < 0 || a >= ACT_COUNT) return;
    for (int b = 0; b < ACT_COUNT; b++) if (b != a && gKeys[b] == k) gKeys[b] = gKeys[a];
    gKeys[a] = k;
}

// Shift・Ctrl・Alt は左右どちらでも
static bool KeyDownEither(int k)
{
    if (IsKeyDown(k)) return true;
    if (k == KEY_LEFT_SHIFT)   return IsKeyDown(KEY_RIGHT_SHIFT);
    if (k == KEY_LEFT_CONTROL) return IsKeyDown(KEY_RIGHT_CONTROL);
    if (k == KEY_LEFT_ALT)     return IsKeyDown(KEY_RIGHT_ALT);
    return false;
}
bool ActDown(int a)    { return a >= 0 && a < ACT_COUNT && KeyDownEither(gKeys[a]); }
bool ActPressed(int a) { return a >= 0 && a < ACT_COUNT && IsKeyPressed(gKeys[a]); }

static int KeyOfSource(int src)
{
    return src == SRC_KEY_L ? gKeys[ACT_FLAT] : src == SRC_KEY_J ? gKeys[ACT_TOP] : src == SRC_KEY_K ? gKeys[ACT_SLICE] : 0;
}

void ReadKeyboardMouse(PlayerInput *in, bool mouseActive, bool skipMouseDelta, float aimSens, float dt)
{
    *in = (PlayerInput){ .shot = -1, .serve = -1, .source = SRC_NONE };

    // 移動（はじめは WASD）
    if (ActDown(ACT_LEFT))  in->move.x -= 1.0f;
    if (ActDown(ACT_RIGHT)) in->move.x += 1.0f;
    if (ActDown(ACT_UP))    in->move.y += 1.0f;
    if (ActDown(ACT_DOWN))  in->move.y -= 1.0f;

    // 狙い：マウス（奥に動かすと深く）。キーボードの U/O（左右）・I/、（深さ）でも
    if (mouseActive && !skipMouseDelta) {
        Vector2 md = GetMouseDelta();
        in->aim.x += md.x * AIM_SENS * aimSens;
        in->aim.y -= md.y * AIM_SENS * aimSens;
    }
    if (ActDown(ACT_AIM_L))     in->aim.x -= 6.0f * dt;
    if (ActDown(ACT_AIM_R))     in->aim.x += 6.0f * dt;
    if (ActDown(ACT_AIM_DEEP))  in->aim.y += 6.0f * dt;
    if (ActDown(ACT_AIM_SHORT)) in->aim.y -= 6.0f * dt;

    bool shift = ActDown(ACT_LOB);
    bool ctrl  = ActDown(ACT_DROP);
    bool space = ActDown(ACT_CLICK_FLAT);
    in->crouch = ActDown(ACT_CROUCH);

    // キーボードの打つキー（はじめは J/K/L。ショットは L=フラット J=トップ K=スライス、サーブは L=フラット K=スライス J=キック）
    const InputSource keySrc[SHOT_NORMAL_COUNT] = { SRC_KEY_L, SRC_KEY_J, SRC_KEY_K };   // フラット / トップ / スライス
    const ServeType keyServe[SHOT_NORMAL_COUNT] = { SERVE_FLAT, SERVE_KICK, SERVE_SLICE };
    for (int s = 0; s < SHOT_NORMAL_COUNT; s++) {
        if (!IsKeyPressed(KeyOfSource(keySrc[s]))) continue;
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
    for (int s = SRC_KEY_L; s <= SRC_KEY_K; s++) in->released[s] = IsKeyReleased(KeyOfSource(s));
    in->released[SRC_MOUSE_L] = IsMouseButtonReleased(MOUSE_BUTTON_LEFT);
    in->released[SRC_MOUSE_R] = IsMouseButtonReleased(MOUSE_BUTTON_RIGHT);
}
