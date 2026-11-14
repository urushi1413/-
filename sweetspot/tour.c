// SWEET SPOT — 大会（トーナメント）：8 人の勝ち抜き。準々決勝 → 準決勝 → 決勝
//   自分以外の試合は、コートごとの選手の強さで結果を決める
#include "game.h"

// 選手の強さ：癖なしの NORMAL の AI に取るポイントの割合（%）。HARD, CLAY, GRASS。0 番（予選上がり）は 50
static const float TOUR_STR[PERSONA_COUNT][3] = {
    { 50, 50, 50 },
    { 60, 52, 68 }, { 69, 65, 65 }, { 64, 74, 67 }, { 62, 53, 62 },   // フェデラー、ジョコビッチ、ナダル、マレー
    { 70, 67, 67 }, { 70, 60, 65 }, { 62, 49, 60 }, { 61, 49, 67 },   // アルカラス、シナー、メドベデフ、キルギオス
    { 69, 58, 71 }, { 64, 76, 62 }, { 66, 54, 61 }, { 64, 58, 64 }, { 64, 67, 58 }, { 62, 56, 74 },   // 女子
};

const char *TourName(int surface)
{
    return surface == SURF_CLAY ? "TERRE BATTUE CLASSIC" : surface == SURF_GRASS ? "LAWN CHAMPIONSHIPS" : "SWEET SPOT OPEN";
}
const char *TourRoundName(int round)
{
    return round == 0 ? "QUARTERFINAL" : round == 1 ? "SEMIFINAL" : round == 2 ? "FINAL" : "";
}
const char *TourEntrantName(const Tournament *t, int e)
{
    if (e < 0) return "";
    if (t->slot[e] < 0) return "YOU";
    if (t->slot[e] == 0) return "QUALIFIER";
    return PERSONAS[t->slot[e]].name;
}

void TourStart(Tournament *t, bool women, int surface, int setsToWin)
{
    memset(t, 0, sizeof(*t));
    t->active = true; t->women = women; t->surface = surface; t->setsToWin = setsToWin;
    // 出る選手：そのツアーの選手から 7 人（足りなければ予選上がり）＋自分。組み合わせはくじ
    int pool[16], np = 0;
    int first = women ? WOMEN_FIRST : MEN_FIRST, last = women ? WOMEN_LAST : MEN_LAST;
    for (int p = first; p <= last; p++) pool[np++] = p;
    for (int i = np - 1; i > 0; i--) { int j = GetRandomValue(0, i); int tmp = pool[i]; pool[i] = pool[j]; pool[j] = tmp; }
    int entr[8], ne = 0;
    entr[ne++] = -1;
    for (int i = 0; i < np && ne < 8; i++) entr[ne++] = pool[i];
    while (ne < 8) entr[ne++] = 0;
    for (int i = 7; i > 0; i--) { int j = GetRandomValue(0, i); int tmp = entr[i]; entr[i] = entr[j]; entr[j] = tmp; }
    for (int i = 0; i < 8; i++) { t->slot[i] = entr[i]; t->ent[0][i] = i; }
    for (int r = 0; r < 3; r++) for (int m = 0; m < 4; m++) t->winner[r][m] = -1;
}

// 自分の今の試合の相手（選手の番号）。もう試合がなければ -1
int TourOpponent(const Tournament *t)
{
    if (!t->active || t->playerOut || t->champion || t->round > 2) return -1;
    int n = 8 >> t->round;
    for (int i = 0; i < n; i++) {
        int e = t->ent[t->round][i];
        if (t->slot[e] < 0) return t->slot[t->ent[t->round][i ^ 1]];
    }
    return -1;
}

static void ScoreText2(const Score *s, int a, char *out, int n)
{
    out[0] = 0;
    for (int k = 0; k < s->nSetsDone && k < 5; k++) {
        char b[16];
        snprintf(b, sizeof(b), "%s%d-%d", k ? " " : "", s->setScore[k][a], s->setScore[k][1 - a]);
        strncat(out, b, n - strlen(out) - 1);
    }
}

// 選手どうしの試合：1 ポイントずつ、強さの差で取る確率を決める
static int SimMatch(const Tournament *t, int ea, int eb, char *score, int n)
{
    int pa = t->slot[ea], pb = t->slot[eb];
    float sa = TOUR_STR[pa < 0 ? 0 : pa][t->surface] / 100.0f, sb = TOUR_STR[pb < 0 ? 0 : pb][t->surface] / 100.0f;
    float p = Clamp(0.5f + (sa - sb) * 0.40f, 0.30f, 0.70f);
    Score s = { 0 };
    s.setsToWin = t->setsToWin;
    for (int i = 0; i < 2000 && !s.over; i++) AwardPoint(&s, RandRange(0.0f, 1.0f) < p ? 0 : 1);
    int w = s.sets[0] > s.sets[1] ? 0 : 1;
    ScoreText2(&s, w, score, n);
    return w == 0 ? ea : eb;
}

// 自分の試合が終わった：結果を記録して、同じラウンドのほかの試合を決め、次のラウンドへ
void TourRecordPlayerMatch(Tournament *t, bool playerWon, const Score *s)
{
    if (!t->active || t->round > 2) return;
    int r = t->round, n = 8 >> r;
    for (int m = 0; m < n / 2; m++) {
        int ea = t->ent[r][2 * m], eb = t->ent[r][2 * m + 1];
        bool mine = t->slot[ea] < 0 || t->slot[eb] < 0;
        if (mine) {
            int me = t->slot[ea] < 0 ? ea : eb, other = me == ea ? eb : ea;
            t->winner[r][m] = playerWon ? me : other;
            ScoreText2(s, playerWon ? 0 : 1, t->score[r][m], sizeof(t->score[r][m]));
        } else {
            t->winner[r][m] = SimMatch(t, ea, eb, t->score[r][m], sizeof(t->score[r][m]));
        }
        t->ent[r + 1][m] = t->winner[r][m];
    }
    if (!playerWon) {
        t->playerOut = true;
        // 残りのラウンドも決めて、優勝者まで出す
        for (int rr = r + 1; rr <= 2; rr++) {
            int nn = 8 >> rr;
            for (int m = 0; m < nn / 2; m++) {
                t->winner[rr][m] = SimMatch(t, t->ent[rr][2 * m], t->ent[rr][2 * m + 1], t->score[rr][m], sizeof(t->score[rr][m]));
                t->ent[rr + 1][m] = t->winner[rr][m];
            }
        }
        t->round = 3;
        t->outRound = r;
        return;
    }
    if (r == 2) { t->champion = true; t->round = 3; return; }
    t->round++;
}

// 組み合わせ表
static void DrawTrophy(int cx, int cy, float s, float time)
{
    Color gold = { 235, 190, 60, 255 }, dark = { 170, 125, 30, 255 };
    DrawRectangle(cx - (int)(40 * s), cy + (int)(70 * s), (int)(80 * s), (int)(16 * s), dark);       // 台
    DrawRectangle(cx - (int)(10 * s), cy + (int)(30 * s), (int)(20 * s), (int)(42 * s), gold);       // 柄
    DrawCircleSector((Vector2){ (float)cx, (float)cy - 10 * s }, 48 * s, 0, 180, 24, gold);          // 杯
    DrawRectangle(cx - (int)(48 * s), cy - (int)(40 * s), (int)(96 * s), (int)(30 * s), gold);
    DrawRing((Vector2){ cx - 52 * s, cy - 20 * s }, 12 * s, 18 * s, 90, 270, 16, dark);               // 取っ手
    DrawRing((Vector2){ cx + 52 * s, cy - 20 * s }, 12 * s, 18 * s, -90, 90, 16, dark);
    for (int i = 0; i < 40; i++) {   // 紙吹雪
        float x = cx + sinf(i * 12.9898f) * 300.0f, y = fmodf(i * 37.0f + time * (60.0f + i % 7 * 15.0f), 520.0f) - 260.0f + cy;
        Color c = (Color[]){ RED, YELLOW, SKYBLUE, GREEN, PINK, ORANGE }[i % 6];
        DrawRectangle((int)x, (int)y, 8, 5, c);
    }
}

void TourDraw(const Tournament *t, int W, int H, float time)
{
    ClearBackground((Color){ 25, 55, 50, 255 });
    const char *title = TourName(t->surface);
    DrawText(title, W / 2 - MeasureText(title, 44) / 2, 30, 44, YELLOW);
    const char *sub = TextFormat("%s  -  %s", t->women ? "WOMEN'S SINGLES" : "MEN'S SINGLES",
                                 t->surface == SURF_CLAY ? "CLAY" : t->surface == SURF_GRASS ? "GRASS" : "HARD");
    DrawText(sub, W / 2 - MeasureText(sub, 20) / 2, 82, 20, LIGHTGRAY);
    // 列：準々決勝 / 準決勝 / 決勝 / 優勝
    int colW = (W - 80) / 4, top = 130, h = H - top - 120;
    for (int r = 0; r <= 3; r++) {
        int n = 8 >> r, x = 40 + r * colW;
        const char *rn = r < 3 ? TourRoundName(r) : "CHAMPION";
        DrawText(rn, x + 4, top - 24, 18, (r == t->round && !t->playerOut && !t->champion) ? YELLOW : LIGHTGRAY);
        for (int i = 0; i < n; i++) {
            float y = top + h * (i + 0.5f) / n;
            int e = t->ent[r][i];
            if (r > 0 && t->winner[r - 1][i] < 0) e = -1;   // まだ決まっていない
            Color box = (e >= 0 && t->slot[e] < 0) ? (Color){ 60, 110, 200, 255 } : (Color){ 40, 45, 55, 255 };
            DrawRectangle(x, (int)y - 16, colW - 30, 32, Fade(box, 0.9f));
            if (e >= 0) {
                bool lost = r < 3 && t->winner[r][i / 2] >= 0 && t->winner[r][i / 2] != e;
                DrawText(TourEntrantName(t, e), x + 8, (int)y - 9, 18, lost ? GRAY : RAYWHITE);
            }
            // 試合のスコア（勝った人の側から）
            if (r < 3 && i % 2 == 0 && t->winner[r][i / 2] >= 0)
                DrawText(t->score[r][i / 2], x + 8, (int)(top + h * (i + 1.0f) / n) - 8, 14, (Color){ 200, 220, 160, 255 });
        }
    }
    const char *msg;
    if (t->champion) {
        DrawTrophy(W - 170, H - 230, 1.0f, time);
        msg = "CHAMPION!   Enter: back to title";
    } else if (t->playerOut) {
        msg = TextFormat("You lost in the %s.   Enter: back to title", TourRoundName(t->outRound));
    } else {
        int op = TourOpponent(t);
        msg = TextFormat("Next: %s vs %s   -   Enter: play", TourRoundName(t->round), op <= 0 ? "QUALIFIER" : PERSONAS[op].name);
    }
    DrawText(msg, W / 2 - MeasureText(msg, 26) / 2, H - 70, 26, YELLOW);
}
