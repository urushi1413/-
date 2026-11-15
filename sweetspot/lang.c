// SWEET SPOT — 言語（英語 / 日本語）と日本語フォント
//   日本語のときは fonts/ipagp.ttf（IPA Pゴシック）で描く。英語のときは今までどおり raylib の標準フォント
//   読み込む文字は ja_glyphs.h（ソースの文字列に出てくる日本語の文字。tools/gen_glyphs.py で作り直す）
#define UI_NO_TEXT_OVERRIDE
#include "game.h"
#include "ja_glyphs.h"

static Font gJaFont;
static bool gJaOk = false;
#define JA_FONT_PX 40   // 読み込む大きさ（これより小さく描くときは縮める）

void UiFontInit(void)
{
    const char *cands[4] = { "fonts/ipagp.ttf", TextFormat("%sfonts/ipagp.ttf", GetApplicationDirectory()), "sweetspot/fonts/ipagp.ttf", NULL };
    char path[1100] = { 0 };
    for (int i = 0; cands[i]; i++) if (FileExists(cands[i])) { snprintf(path, sizeof(path), "%s", cands[i]); break; }
    if (!path[0]) { TraceLog(LOG_WARNING, "Japanese font not found (fonts/ipagp.ttf)"); gJaOk = false; return; }
    int nJa = 0;
    int *ja = LoadCodepoints(JA_GLYPHS, &nJa);
    int n = 95 + nJa;
    int *cps = (int *)MemAlloc(sizeof(int) * n);
    for (int i = 0; i < 95; i++) cps[i] = 32 + i;   // ASCII
    for (int i = 0; i < nJa; i++) cps[95 + i] = ja[i];
    UnloadCodepoints(ja);
    gJaFont = LoadFontEx(path, JA_FONT_PX, cps, n);
    MemFree(cps);
    gJaOk = gJaFont.texture.id != 0 && gJaFont.glyphCount > 95;
    if (gJaOk) {
        GenTextureMipmaps(&gJaFont.texture);                       // 小さい字もにじみすぎないように
        SetTextureFilter(gJaFont.texture, TEXTURE_FILTER_TRILINEAR);
    }
}
bool UiJapaneseOk(void) { return gJaOk; }

static bool UseJa(void) { return gLang == LANG_JA && gJaOk; }
void UiDrawText(const char *t, int x, int y, int size, Color c)
{
    if (!t) return;
    if (UseJa()) DrawTextEx(gJaFont, t, (Vector2){ (float)x, (float)y }, (float)size, size * 0.04f, c);
    else DrawText(t, x, y, size, c);
}
int UiMeasureText(const char *t, int size)
{
    if (!t) return 0;
    if (UseJa()) return (int)MeasureTextEx(gJaFont, t, (float)size, size * 0.04f).x;
    return MeasureText(t, size);
}

// 表示用の名前（記録のファイルには英語の名前をそのまま書く）
const char *ShotNameUI(int t)
{
    static const char *JA[SHOT_COUNT] = { "フラット", "トップスピン", "スライス", "スライスロブ", "トップスピンロブ", "ドロップ" };
    return gLang == LANG_JA && t >= 0 && t < SHOT_COUNT ? JA[t] : SHOTS[t].name;
}
const char *ServeNameUI(int t)
{
    static const char *JA[SERVE_COUNT] = { "フラットサーブ", "スライスサーブ", "キックサーブ" };
    return gLang == LANG_JA && t >= 0 && t < SERVE_COUNT ? JA[t] : SERVES[t].name;
}
const char *LevelNameUI(int l)
{
    static const char *JA[3] = { "やさしい", "ふつう", "プロ" };
    return gLang == LANG_JA ? JA[l % 3] : AI_LEVELS[l % 3].name;
}
const char *SurfNameUI(int s)
{
    static const char *JA[SURF_COUNT] = { "ハード", "クレー", "芝" };
    return gLang == LANG_JA ? JA[s % SURF_COUNT] : SURFACES[s % SURF_COUNT].name;
}
const char *StyleNameUI(int s)
{
    static const char *JA[STYLE_COUNT] = { "リアル", "カジュアル" };
    return gLang == LANG_JA ? JA[s % STYLE_COUNT] : STYLE_NAMES[s % STYLE_COUNT];
}
const char *ResultTextUI(int r)
{
    if (gLang != LANG_JA) return ResultText((ShotResult)r);
    return r == RESULT_IN ? "イン" : r == RESULT_OUT ? "アウト" : r == RESULT_NET ? "ネット" : "-";
}
