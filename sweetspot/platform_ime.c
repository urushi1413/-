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
#else
void DisableIme(void *hwnd)
{
    (void)hwnd;
}
#endif
