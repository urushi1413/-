// SWEET SPOT — 体験版の「NORMAL に勝ったら製品版無料」の勝利の証明コード
//   試合の中身（相手・AI の強さ・難しさ・スコア・日付）と、秘密の鍵から作った確かめの数字を、1 つのコードにする。
//   秘密の鍵はブラウザ版を作るときだけ入れる（web/demo_secret.txt。リポジトリには入れない）。
//   コードを確かめるのは tools/check_code.py（同じ鍵を使う）
#include "game.h"
#include <time.h>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

// 体験版で遊べる試合の数。1 ポイントでも始めたら 1 回と数える（途中でやめてもやり直せないように）。
//   ブラウザ版はブラウザの保存場所（localStorage）に数える。消せば戻るが、小さな体験版の制限としては十分
static int gDemoUsedLocal = 0;
int DemoMatchesUsed(void)
{
#ifdef __EMSCRIPTEN__
    return EM_ASM_INT({ try { return parseInt(localStorage.getItem("sweetspot_demo_matches") || "0", 10) || 0; } catch (e) { return 0; } });
#else
    return gDemoUsedLocal;
#endif
}
void DemoCountMatch(void)
{
#ifdef __EMSCRIPTEN__
    EM_ASM({ try { var n = parseInt(localStorage.getItem("sweetspot_demo_matches") || "0", 10) || 0; localStorage.setItem("sweetspot_demo_matches", String(n + 1)); } catch (e) {} });
#else
    gDemoUsedLocal++;
#endif
}

#ifndef DEMO_SECRET
#define DEMO_SECRET "no-secret"   // 体験版でないときは使わない
#endif

// 鍵つきのハッシュ（FNV-1a に、鍵・中身・鍵の順に通して、最後によく混ぜる）
static unsigned long long KeyedHash(const unsigned char *d, int n)
{
    unsigned long long h = 1469598103934665603ULL;
    const char *k = DEMO_SECRET;
    for (const char *p = k; *p; p++) { h ^= (unsigned char)*p; h *= 1099511628211ULL; }
    for (int i = 0; i < n; i++)       { h ^= d[i]; h *= 1099511628211ULL; }
    for (const char *p = k; *p; p++) { h ^= (unsigned char)*p; h *= 1099511628211ULL; }
    h ^= h >> 30; h *= 0xbf58476d1ce4e5b9ULL; h ^= h >> 27; h *= 0x94d049bb133111ebULL; h ^= h >> 31;
    return h;
}

// 中身 8 バイト＋確かめ 5 バイト = 104 ビットを、読み間違えにくい 32 文字（I・L・O・U なし）で 21 文字に
static const char B32[] = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";

void DemoMakeCode(char *out, int outSize, int aiLevel, int style, int opp, int gamesYou, int gamesOpp, int ptsYou, int ptsOpp)
{
    unsigned char b[13];
    long days = (long)((time(NULL) - 1767225600L) / 86400L);   // 2026-01-01 からの日数
    if (days < 0) days = 0;
    b[0] = (unsigned char)((1 << 4) | ((aiLevel & 3) << 2) | (style & 3));   // 版 1
    b[1] = (unsigned char)opp;
    b[2] = (unsigned char)(((gamesYou & 15) << 4) | (gamesOpp & 15));
    b[3] = (unsigned char)(ptsYou > 255 ? 255 : ptsYou);
    b[4] = (unsigned char)(ptsOpp > 255 ? 255 : ptsOpp);
    b[5] = (unsigned char)(days >> 8); b[6] = (unsigned char)days;
    b[7] = (unsigned char)GetRandomValue(0, 255);                          // 同じ試合でも毎回違うコードに
    unsigned long long h = KeyedHash(b, 8);
    for (int i = 0; i < 5; i++) b[8 + i] = (unsigned char)(h >> (8 * i));
    // 104 ビットを 5 ビットずつ（最後の 1 文字は 4 ビット）
    char s[24]; int n = 0, bits = 0; unsigned acc = 0;
    for (int i = 0; i < 13; i++) {
        acc = (acc << 8) | b[i]; bits += 8;
        while (bits >= 5) { s[n++] = B32[(acc >> (bits - 5)) & 31]; bits -= 5; }
        acc &= (1u << bits) - 1u;
    }
    if (bits > 0) s[n++] = B32[(acc << (5 - bits)) & 31];
    s[n] = 0;   // 21 文字
    snprintf(out, outSize, "SS-%.5s-%.5s-%.5s-%.6s", s, s + 5, s + 10, s + 15);
}
