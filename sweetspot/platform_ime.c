// ゲームのウィンドウで日本語入力（IME）を切る。
// IME が ON だと文字キーが IME に取られて、キーを離したことがゲームに届かず
// 「押しっぱなし」と判定されることがある（何もしていないのに歩き続ける）。
// windows.h は raylib と名前がぶつかる（Rectangle、DrawText など）ので、別ファイルにしている。
#ifdef _WIN32
#include <windows.h>
#include <imm.h>
void DisableIme(void *hwnd)
{
    if (hwnd) ImmAssociateContext((HWND)hwnd, NULL);
}
// パソコンの表示の言語が日本語か（初めての起動の言語を決める）
int SystemLangIsJapanese(void)
{
    return PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_JAPANESE;
}
#elif defined(__EMSCRIPTEN__)
#include <emscripten.h>
void DisableIme(void *hwnd) { (void)hwnd; }
// ブラウザの言語が日本語か（初めての起動の言語を決める。英語の人に日本語の画面を出していた）
int SystemLangIsJapanese(void)
{
    return EM_ASM_INT({ try { var l = (navigator.languages && navigator.languages[0]) || navigator.language || ''; return l.toLowerCase().indexOf('ja') == 0 ? 1 : 0; } catch (e) { return 0; } });
}
#else
void DisableIme(void *hwnd)
{
    (void)hwnd;
}
#include <stdlib.h>
#include <string.h>
int SystemLangIsJapanese(void)
{
    const char *l = getenv("LANG");
    return l && strncmp(l, "ja", 2) == 0;
}
#endif
