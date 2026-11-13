// SWEET SPOT — 試合のスコア（ポイント・ゲーム・タイブレーク）
#include "game.h"

// ================= 試合のスコア =================

const char *PointName(int p) { static const char *n[] = { "0", "15", "30", "40" }; return p < 4 ? n[p] : "?"; }

// このポイントのサーバー（タイブレークは 1, 2, 2, 2... で交代）
int CurrentServer(const Score *s)
{
    if (!s->tiebreak) return s->server;
    int n = s->points[0] + s->points[1];
    if (n == 0) return s->server;
    return (((n - 1) / 2) % 2 == 0) ? 1 - s->server : s->server;
}

// デュースサイドか（ゲーム内のポイント数が偶数ならデュース）
bool DeuceCourt(const Score *s) { return (s->points[0] + s->points[1]) % 2 == 0; }

void AwardPoint(Score *s, int w)
{
    int l = 1 - w;
    s->points[w]++;
    bool gameWon;
    if (s->tiebreak) gameWon = s->points[w] >= 7 && s->points[w] - s->points[l] >= 2;
    else             gameWon = s->points[w] >= 4 && s->points[w] - s->points[l] >= 2;
    if (!gameWon) {
        // デュース後は 3-3 に戻して表示を簡単にする（アドバンテージは 4-3）
        if (!s->tiebreak && s->points[0] >= 4 && s->points[1] >= 4) { s->points[0]--; s->points[1]--; }
        return;
    }
    s->games[w]++;
    int firstServer = s->server;
    if (s->nGames < 16) { s->gameWinner[s->nGames] = w; s->gameServer[s->nGames] = firstServer; s->nGames++; }
    s->points[0] = s->points[1] = 0;
    bool setWon = (s->games[w] >= 6 && s->games[w] - s->games[l] >= 2) || (s->tiebreak);
    s->tiebreak = false;
    s->server = 1 - firstServer;
    if (setWon) {
        s->sets[w]++;
        s->games[0] = s->games[1] = 0;
        s->over = true;   // 今は 1 セットマッチ
    } else if (s->games[0] == 6 && s->games[1] == 6) {
        s->tiebreak = true;
    }
}

// スコア表に出す、プレイヤー k のポイント（0/15/30/40/AD。タイブレークは数）
const char *PointLabel(const Score *s, int k)
{
    int a = s->points[k], b = s->points[1 - k];
    if (s->tiebreak) return TextFormat("%d", a);
    if (a >= 3 && b >= 3) return a > b ? "AD" : "40";
    return PointName(a);
}

// スコアはサーバーの点を先に言う（サーバーが 15、レシーバーが 0 なら "15 - 0"）
const char *ScoreText(const Score *s)
{
    int sv = CurrentServer(s), rv = 1 - sv;
    if (s->tiebreak) return TextFormat("Tiebreak %d - %d", s->points[sv], s->points[rv]);
    int a = s->points[0], b = s->points[1];
    if (a >= 3 && b >= 3) {
        if (a == b) return "Deuce";
        return (a > b) ? "Advantage You" : "Advantage AI";
    }
    return TextFormat("%s - %s", PointName(s->points[sv]), PointName(s->points[rv]));
}
