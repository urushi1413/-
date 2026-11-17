// SWEET SPOT — グランドスラム：16 人の勝ち抜き。4 回戦 → 準々決勝 → 準決勝 → 決勝
//   4 大会（全豪＝ハード、全仏＝クレー、ウィンブルドン＝芝、全米＝ハード）。シーズン：4 大会を続けて回る（全部取ると年間グランドスラム）
//   上位 8 人にシードがつく。足りない枠は予選上がり。自分以外の試合は、コートごとの選手の強さで結果を決める
#include "game.h"

// 選手の強さ：癖なしの NORMAL の AI に取るポイントの割合（%）。HARD, CLAY, GRASS。0 番（予選上がり）は 50
static const float TOUR_STR[PERSONA_COUNT][3] = {
    { 50, 50, 50 },
    { 60, 55, 71 }, { 68, 70, 67 }, { 66, 76, 65 }, { 62, 56, 60 },   // フェデラー、ジョコビッチ、ナダル、マレー
    { 72, 71, 69 }, { 74, 64, 62 }, { 65, 50, 56 }, { 60, 52, 65 },   // アルカラス、シナー、メドベデフ、キルギオス
    { 71, 60, 71 }, { 66, 79, 64 }, { 64, 57, 57 }, { 67, 59, 64 }, { 59, 72, 60 }, { 65, 62, 73 },   // 女子
    { 71, 61, 65 }, { 69, 71, 66 },                                    // 錦織、ズベレフ
};


static const char *SLAM_NAME[SLAM_COUNT]  = { PN("AUSTRALIAN OPENN", "SOUTHERN OPEN"), PN("ROLAND GARROZ", "RED CLAY OPEN"),
                                              PN("WIMBLEDONN", "GREEN LAWN CHAMPIONSHIPS"), PN("US OPENN", "CITY LIGHTS OPEN") };   // 売る版は架空の大会
static const int   SLAM_SURF[SLAM_COUNT]  = { SURF_HARD, SURF_CLAY, SURF_GRASS, SURF_HARD };
static const char *SLAM_PLACE_EN[SLAM_COUNT] = { PN("MELBOURNE  -  JANUARY", "HARBOUR CITY  -  JANUARY"), PN("PARIS  -  MAY", "OLD TOWN  -  MAY"),
                                                 PN("LONDON  -  JULY", "RIVERSIDE  -  JULY"), PN("NEW YORK  -  AUGUST", "METRO CITY  -  AUGUST") };
static const char *SLAM_PLACE_JA[SLAM_COUNT] = { PN("メルボルン  -  1 月", "ハーバーシティ  -  1 月"), PN("パリ  -  5 月", "オールドタウン  -  5 月"),
                                                 PN("ロンドン  -  7 月", "リバーサイド  -  7 月"), PN("ニューヨーク  -  8 月", "メトロシティ  -  8 月") };
#define SLAM_PLACE (gLang == LANG_JA ? SLAM_PLACE_JA : SLAM_PLACE_EN)
// 予選上がりの名前（架空）
static const char *QUAL_MEN[] = { "Lukas Brandtner", "Mateo Varga", "Tomas Ruzicka", "Hugo Lefevre", "Diego Arrieta", "Kenta Moriyama",
                                  "Elias Nordqvist", "Bram Dekker", "Ivan Sokolenko", "Marco Bellandi", "Felix Hartung", "Rui Cardoso" };
static const char *QUAL_WOMEN[] = { "Anna Kowalik", "Lea Fournier", "Mia Lindgren", "Elena Moretti", "Sara Okafor", "Yui Takamura",
                                    "Nina Petrova", "Clara Vidal", "Julia Hoffer", "Ana Ribeiro", "Eva Novakova", "Lily Chen", "Zoe Marchetti" };

// 結果を 1 試合ずつ見せる状態（TourBeginReveal）
static struct { bool on; int n; int r[TOUR_SIZE], m[TOUR_SIZE]; double t0; } gRev;
const char *TourName(int slam) { return SLAM_NAME[((slam % SLAM_COUNT) + SLAM_COUNT) % SLAM_COUNT]; }
int TourSlamSurface(int slam) { return SLAM_SURF[((slam % SLAM_COUNT) + SLAM_COUNT) % SLAM_COUNT]; }
const char *TourRoundName(int round)
{
    if (gLang == LANG_JA) return round == 0 ? "4 回戦" : round == 1 ? "準々決勝" : round == 2 ? "準決勝" : round == 3 ? "決勝" : "";
    return round == 0 ? "FOURTH ROUND" : round == 1 ? "QUARTERFINAL" : round == 2 ? "SEMIFINAL" : round == 3 ? "FINAL" : "";
}
const char *TourEntrantName(const Tournament *t, int e)
{
    if (e < 0) return "";
    if (t->slot[e] < 0) return YouName();
    if (t->slot[e] == 0) return t->women ? QUAL_WOMEN[t->qual[e]] : QUAL_MEN[t->qual[e]];
    return PERSONAS[t->slot[e]].name;
}
static float EntrantStr(const Tournament *t, int e)
{
    int p = t->slot[e];
    if (p < 0) return 50.0f;
    if (p == 0) return t->qstr[e];
    return TOUR_STR[p][t->surface];
}
// ランキング（シード順）：3 つのコートの強さの平均
static float RankStr(int p) { return (TOUR_STR[p][0] + TOUR_STR[p][1] + TOUR_STR[p][2]) / 3.0f; }

void TourStart(Tournament *t, bool women, int slam, int setsToWin)
{
    // シーズンの記録は残す
    bool season = t->season && t->active; int titles = t->seasonTitles; bool won[SLAM_COUNT]; memcpy(won, t->seasonWon, sizeof(won));
    memset(t, 0, sizeof(*t));
    gRev.on = false;
    if (season) { t->season = true; t->seasonTitles = titles; memcpy(t->seasonWon, won, sizeof(won)); }
    t->active = true; t->women = women; t->slam = slam; t->surface = TourSlamSurface(slam); t->setsToWin = setsToWin;
    // 出る選手：そのツアーの選手を全員（強い順に並べる）
    int pool[TOUR_SIZE], np = 0;
    for (int i = 0; i < TourPersonaCount(women) && np < TOUR_SIZE - 1; i++)
        if (TourPersonaAt(women, i) != gYouPersona) pool[np++] = TourPersonaAt(women, i);   // 自分が使う選手は出ない
    for (int i = 0; i < np; i++) for (int j = i + 1; j < np; j++) if (RankStr(pool[j]) > RankStr(pool[i])) { int x = pool[i]; pool[i] = pool[j]; pool[j] = x; }
    // シードの位置：1 は一番上、2 は一番下、3・4 は 5 番目と 12 番目（くじ）、5〜8 は 4・8・9・13 番目（くじ）
    for (int i = 0; i < TOUR_SIZE; i++) t->slot[i] = -2;   // まだ空き
    int nSeed = np < 8 ? np : 8;
    int pos34[2] = { 4, 11 }, pos58[4] = { 3, 7, 8, 12 };
    if (GetRandomValue(0, 1)) { pos34[0] = 11; pos34[1] = 4; }
    for (int i = 3; i > 0; i--) { int j = GetRandomValue(0, i); int x = pos58[i]; pos58[i] = pos58[j]; pos58[j] = x; }
    for (int s = 0; s < nSeed; s++) {
        int pos = s == 0 ? 0 : s == 1 ? TOUR_SIZE - 1 : s < 4 ? pos34[s - 2] : pos58[s - 4];
        t->slot[pos] = pool[s]; t->seed[pos] = s + 1;
    }
    // 残り：シードのない選手と自分と予選上がりを、空いた枠にくじで
    int rest[TOUR_SIZE], nr = 0;
    rest[nr++] = -1;
    for (int s = nSeed; s < np; s++) rest[nr++] = pool[s];
    int free[TOUR_SIZE], nf = 0;
    for (int i = 0; i < TOUR_SIZE; i++) if (t->slot[i] == -2) free[nf++] = i;
    while (nr < nf) rest[nr++] = 0;
    for (int i = nr - 1; i > 0; i--) { int j = GetRandomValue(0, i); int x = rest[i]; rest[i] = rest[j]; rest[j] = x; }
    int nq = women ? (int)(sizeof(QUAL_WOMEN) / sizeof(char *)) : (int)(sizeof(QUAL_MEN) / sizeof(char *));
    int qorder[16]; for (int i = 0; i < nq; i++) qorder[i] = i;
    for (int i = nq - 1; i > 0; i--) { int j = GetRandomValue(0, i); int x = qorder[i]; qorder[i] = qorder[j]; qorder[j] = x; }
    int qi = 0;
    for (int i = 0; i < nf; i++) {
        int e = free[i]; t->slot[e] = rest[i];
        if (rest[i] == 0) { t->qual[e] = qorder[qi++ % nq]; t->qstr[e] = RandRange(44.0f, 56.0f); }
    }
    for (int i = 0; i < TOUR_SIZE; i++) t->ent[0][i] = i;
    for (int r = 0; r < TOUR_ROUNDS; r++) for (int m = 0; m < TOUR_SIZE / 2; m++) t->winner[r][m] = -1;
}

void TourStartSeason(Tournament *t, bool women, int setsToWin)
{
    memset(t, 0, sizeof(*t));
    t->season = true; t->active = true;   // TourStart にシーズンを引き継がせる
    TourStart(t, women, 0, setsToWin);
}

// シーズンの次の大会へ。もうなければ false
bool TourNextSlam(Tournament *t)
{
    if (!t->season || t->slam >= SLAM_COUNT - 1) return false;
    TourStart(t, t->women, t->slam + 1, t->setsToWin);
    return true;
}

// 自分の今の試合の相手（選手の番号。0 = 予選上がり）。もう試合がなければ -1
static int OpponentEntrant(const Tournament *t)
{
    if (!t->active || t->playerOut || t->champion || t->round >= TOUR_ROUNDS) return -1;
    int n = TOUR_SIZE >> t->round;
    for (int i = 0; i < n; i++) {
        int e = t->ent[t->round][i];
        if (t->slot[e] < 0) return t->ent[t->round][i ^ 1];
    }
    return -1;
}
int TourOpponent(const Tournament *t)
{
    int e = OpponentEntrant(t);
    return e < 0 ? -1 : t->slot[e];
}
const char *TourOpponentName(const Tournament *t)
{
    int e = OpponentEntrant(t);
    return e < 0 ? "" : TourEntrantName(t, e);
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
    float sa = EntrantStr(t, ea) / 100.0f, sb = EntrantStr(t, eb) / 100.0f;
    float p = Clamp(0.5f + (sa - sb) * 0.40f, 0.30f, 0.70f);
    Score s = { 0 };
    s.setsToWin = LenSets(t->setsToWin); s.setGames = LenGames(t->setsToWin);
    if (t->setsToWin == 0) { s.tbOnly = true; s.tiebreak = true; }   // タイブレークだけの大会：ほかの試合もタイブレーク
    for (int i = 0; i < 4000 && !s.over; i++) AwardPoint(&s, RandRange(0.0f, 1.0f) < p ? 0 : 1);
    int w = s.sets[0] > s.sets[1] ? 0 : 1;
    ScoreText2(&s, w, score, n);
    return w == 0 ? ea : eb;
}

// 自分の試合が終わった：結果を記録して、同じラウンドのほかの試合を決め、次のラウンドへ
void TourRecordPlayerMatch(Tournament *t, bool playerWon, const Score *s)
{
    if (!t->active || t->round >= TOUR_ROUNDS) return;
    int r = t->round, n = TOUR_SIZE >> r;
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
        for (int rr = r + 1; rr < TOUR_ROUNDS; rr++) {
            int nn = TOUR_SIZE >> rr;
            for (int m = 0; m < nn / 2; m++) {
                t->winner[rr][m] = SimMatch(t, t->ent[rr][2 * m], t->ent[rr][2 * m + 1], t->score[rr][m], sizeof(t->score[rr][m]));
                t->ent[rr + 1][m] = t->winner[rr][m];
            }
        }
        t->round = TOUR_ROUNDS;
        t->outRound = r;
        return;
    }
    if (r == TOUR_ROUNDS - 1) {
        t->champion = true; t->round = TOUR_ROUNDS;
        if (t->season && !t->seasonWon[t->slam]) { t->seasonWon[t->slam] = true; t->seasonTitles++; }
        return;
    }
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

static Color SlamColor(int slam)
{
    return slam == 0 ? (Color){ 20, 70, 130, 255 } : slam == 1 ? (Color){ 120, 55, 30, 255 } : slam == 2 ? (Color){ 30, 70, 40, 255 } : (Color){ 25, 40, 95, 255 };
}

// ---- 結果を 1 試合ずつ見せる（自分の試合が終わって組み合わせ表に戻ったとき） ----
//   その試合の 2 人が光る → 負けた方が暗くなる → 勝った方の名前が次のラウンドへ進む → スコアが出る
static float StepLen(int r) { return r >= 2 ? 1.25f : 0.95f; }   // 準決勝と決勝はゆっくり
void TourBeginReveal(const Tournament *t, int fromRound)
{
    gRev.n = 0;
    for (int r = fromRound; r < TOUR_ROUNDS; r++) {
        int n = TOUR_SIZE >> r;
        int mine = -1;
        for (int m = 0; m < n / 2; m++) if (t->slot[t->ent[r][2 * m]] < 0 || t->slot[t->ent[r][2 * m + 1]] < 0) mine = m;
        if (mine >= 0 && t->winner[r][mine] >= 0) { gRev.r[gRev.n] = r; gRev.m[gRev.n++] = mine; }   // 自分の試合を最初に
        for (int m = 0; m < n / 2 && gRev.n < TOUR_SIZE; m++)
            if (m != mine && t->winner[r][m] >= 0) { gRev.r[gRev.n] = r; gRev.m[gRev.n++] = m; }
    }
    gRev.on = gRev.n > 0;
    gRev.t0 = GetTime() + 0.5;   // 表が出てから少し待って始める
}
static double RevealTotal(void) { double s = 0; for (int i = 0; i < gRev.n; i++) s += StepLen(gRev.r[i]); return s; }
bool TourRevealActive(void) { return gRev.on && GetTime() - gRev.t0 < RevealTotal(); }
void TourRevealSkip(void) { gRev.t0 = GetTime() - RevealTotal() - 1.0; }
// 試合 (r, m) の見せ方：-1 = まだ見せない、0〜1 = 見せている途中、2 = 見せ終わった（または見せる対象でない）
static float RevPhase(int r, int m)
{
    if (!gRev.on) return 2.0f;
    double t = GetTime() - gRev.t0;
    for (int i = 0; i < gRev.n; i++) {
        float len = StepLen(gRev.r[i]);
        if (gRev.r[i] == r && gRev.m[i] == m) return t < 0.0 ? -1.0f : (t < len ? (float)(t / len) : 2.0f);
        t -= len;
    }
    return 2.0f;
}
static float Ease(float x) { x = Clamp01(x); return x * x * (3.0f - 2.0f * x); }
// 2 つの色の間（raylib の色を混ぜる関数は古い版にないので自前で）
static Color MixColor(Color a, Color b, float k)
{
    k = Clamp01(k);
    return (Color){ (unsigned char)(a.r + (b.r - a.r) * k), (unsigned char)(a.g + (b.g - a.g) * k),
                    (unsigned char)(a.b + (b.b - a.b) * k), (unsigned char)(a.a + (b.a - a.a) * k) };
}

// 大会の案内（優勝・負け・次の試合）
const char *TourEndMessage(const Tournament *t)
{
    const char *msg;
    if (t->champion) {
        if (t->season && t->slam < SLAM_COUNT - 1) msg = TextFormat(TR("CHAMPION!   Enter: next slam (%s)", "優勝！   Enter：次の大会（%s）"), SLAM_NAME[t->slam + 1]);
        else if (t->season && t->seasonTitles == SLAM_COUNT) msg = TR("CALENDAR GRAND SLAM!!   Enter: back to title", "年間グランドスラム達成！！   Enter：タイトルに戻る");
        else if (t->season) msg = TextFormat(TR("CHAMPION!   Season: %d of 4 slams   Enter: back to title", "優勝！   シーズン：4 大会中 %d 大会   Enter：タイトルに戻る"), t->seasonTitles);
        else msg = TR("CHAMPION!   Enter: back to title", "優勝！   Enter：タイトルに戻る");
    } else if (t->playerOut) {
        if (t->season && t->slam < SLAM_COUNT - 1)
            msg = TextFormat(TR("You lost in the %s.   Enter: next slam (%s)", "%sで負けた   Enter：次の大会（%s）"), TourRoundName(t->outRound), SLAM_NAME[t->slam + 1]);
        else if (t->season)
            msg = TextFormat(TR("You lost in the %s.   Season: %d of 4 slams   Enter: back to title", "%sで負けた   シーズン：4 大会中 %d 大会   Enter：タイトルに戻る"), TourRoundName(t->outRound), t->seasonTitles);
        else
            msg = TextFormat(TR("You lost in the %s.   Enter: back to title", "%sで負けた   Enter：タイトルに戻る"), TourRoundName(t->outRound));
    } else {
        msg = TextFormat(TR("Next: %s vs %s   -   Enter: play", "次：%s  対 %s   -   Enter：試合開始"), TourRoundName(t->round), TourOpponentName(t));
    }
    return msg;
}

void TourDraw(const Tournament *t, int W, int H, float time)
{
    ClearBackground(SlamColor(t->slam));
    Color accent = t->slam == 2 ? (Color){ 200, 170, 230, 255 } : YELLOW;   // ウィンブルドンは紫
    const char *title = TourName(t->slam);
    DrawText(title, W / 2 - MeasureText(title, 44) / 2, 18, 44, accent);
    const char *sub = TextFormat("%s  -  %s  -  %s", SLAM_PLACE[t->slam], t->women ? TR("WOMEN'S SINGLES", "女子シングルス") : TR("MEN'S SINGLES", "男子シングルス"),
                                 t->surface == SURF_CLAY ? TR("CLAY", "クレー") : t->surface == SURF_GRASS ? TR("GRASS (all white)", "芝（服は白だけ）") : TR("HARD", "ハード"));
    DrawText(sub, W / 2 - MeasureText(sub, 18) / 2, 66, 18, LIGHTGRAY);
    // シーズン：4 大会の結果
    if (t->season) {
        int x = W / 2 - 2 * 150;
        for (int k = 0; k < SLAM_COUNT; k++) {
            Color c = t->seasonWon[k] ? (Color){ 235, 190, 60, 255 } : k == t->slam ? RAYWHITE : k < t->slam ? GRAY : (Color){ 120, 130, 140, 255 };
            const char *nm = TextFormat("%s%s", SLAM_NAME[k], t->seasonWon[k] ? " *" : "");
            DrawText(nm, x + k * 150 + 75 - MeasureText(nm, 14) / 2, 92, 14, c);
        }
    }
    bool revealing = TourRevealActive();
    // 列：4 回戦 / 準々決勝 / 準決勝 / 決勝 / 優勝
    int cols = TOUR_ROUNDS + 1;
    int colW = (W - 60) / cols, top = 140, h = H - top - 100;
    #define BOX_X(r)     (30 + (r) * colW)
    #define BOX_Y(r, i)  (top + ((float)h / (TOUR_SIZE >> (r))) * ((i) + 0.5f))
    #define BOX_H(r)     ((int)fminf(30.0f, (float)h / (TOUR_SIZE >> (r)) - 4.0f))
    // 名前（シード・予選上がりの印つき）
    #define ENT_NAME(e)  (t->seed[e] ? TextFormat("[%d] %s", t->seed[e], TourEntrantName(t, e)) : t->slot[e] == 0 ? TextFormat("(Q) %s", TourEntrantName(t, e)) : TourEntrantName(t, e))
    // 勝ち上がりの線（勝者が決まって見せ終わった線は明るく）
    for (int r = 0; r < TOUR_ROUNDS; r++) {
        int n = TOUR_SIZE >> r;
        for (int m = 0; m < n / 2; m++) {
            float ya = BOX_Y(r, 2 * m), yb = BOX_Y(r, 2 * m + 1), yn = BOX_Y(r + 1, m);
            int xr = BOX_X(r) + colW - 16, xm = BOX_X(r) + colW - 8, xn = BOX_X(r + 1);
            Color dim = Fade(RAYWHITE, 0.18f);
            DrawLine(xr, (int)ya, xm, (int)ya, dim); DrawLine(xr, (int)yb, xm, (int)yb, dim);
            DrawLine(xm, (int)ya, xm, (int)yb, dim); DrawLine(xm, (int)yn, xn, (int)yn, dim);
            float ph = RevPhase(r, m);
            int w = t->winner[r][m];
            if (w >= 0 && ph >= 0.4f) {   // 勝った方の道を明るく（見せている間は伸びていく）
                float yw = (t->ent[r][2 * m] == w) ? ya : yb;
                float k = ph >= 2.0f ? 1.0f : Ease((ph - 0.4f) / 0.35f);
                Color lit = Fade(accent, ph >= 2.0f ? 0.55f : 0.95f);
                DrawLineEx((Vector2){ (float)xr, yw }, (Vector2){ xr + (xm - xr) * Clamp01(k * 3.0f), yw }, 2.0f, lit);
                if (k > 0.33f) DrawLineEx((Vector2){ (float)xm, yw }, (Vector2){ (float)xm, yw + (yn - yw) * Clamp01((k - 0.33f) * 3.0f) }, 2.0f, lit);
                if (k > 0.66f) DrawLineEx((Vector2){ (float)xm, yn }, (Vector2){ xm + (xn - xm) * Clamp01((k - 0.66f) * 3.0f), yn }, 2.0f, lit);
            }
        }
    }
    for (int r = 0; r <= TOUR_ROUNDS; r++) {
        int n = TOUR_SIZE >> r, x = BOX_X(r);
        const char *rn = r < TOUR_ROUNDS ? TourRoundName(r) : TR("CHAMPION", "優勝");
        DrawText(rn, x + 4, top - 22, 16, (r == t->round && !t->playerOut && !t->champion && !revealing) ? accent : LIGHTGRAY);
        int bh = BOX_H(r);
        int fs = bh >= 24 ? 16 : 14;
        for (int i = 0; i < n; i++) {
            float y = BOX_Y(r, i);
            int e = t->ent[r][i];
            float inPh = r > 0 ? RevPhase(r - 1, i) : 2.0f;   // この枠に入る勝者を見せている途中か
            if (r > 0 && (t->winner[r - 1][i] < 0 || inPh < 0.75f)) e = -1;   // まだ決まっていない（見せていない）
            float myPh = r < TOUR_ROUNDS ? RevPhase(r, i / 2) : 2.0f;     // この枠の人の試合
            bool active = myPh >= 0.0f && myPh < 1.0f;
            Color box = (e >= 0 && t->slot[e] < 0) ? (Color){ 60, 110, 200, 255 } : (Color){ 30, 35, 45, 255 };
            DrawRectangle(x, (int)y - bh / 2, colW - 16, bh, Fade(box, 0.9f));
            if (active && e >= 0) {   // 試合中：枠が光る
                float pulse = 0.5f + 0.5f * sinf(time * 14.0f);
                DrawRectangleLinesEx((Rectangle){ (float)x - 1, y - bh / 2 - 1, (float)colW - 14, (float)bh + 2 }, 2.0f, Fade(accent, 0.5f + 0.5f * pulse * (1.0f - Clamp01((myPh - 0.4f) * 3.0f))));
            }
            if (e >= 0) {
                bool decided = r < TOUR_ROUNDS && t->winner[r][i / 2] >= 0 && myPh >= 0.4f;
                bool lost = decided && t->winner[r][i / 2] != e;
                Color nc = lost ? GRAY : RAYWHITE;
                if (lost && myPh < 1.0f) nc = MixColor(RAYWHITE, GRAY, Ease((myPh - 0.4f) / 0.2f));
                if (r == TOUR_ROUNDS && inPh < 1.0f) nc = accent;   // 優勝者の登場
                DrawText(ENT_NAME(e), x + 6, (int)y - fs / 2, fs, nc);
                if (lost) DrawLine(x + 6, (int)y, x + 6 + (int)(MeasureText(ENT_NAME(e), fs) * (myPh >= 1.0f ? 1.0f : Ease((myPh - 0.4f) / 0.25f))), (int)y, Fade(GRAY, 0.8f));   // 負けた人に線
            }
            // 前のラウンドの試合のスコア（勝った人の箱の下に）。名前が着いてから出す
            if (r > 0 && t->winner[r - 1][i] >= 0 && inPh >= 0.8f)
                DrawText(t->score[r - 1][i], x + 6, (int)y + bh / 2 + 1, 12, Fade((Color){ 200, 220, 160, 255 }, inPh >= 2.0f ? 1.0f : Ease((inPh - 0.8f) / 0.2f)));
        }
    }
    // 勝った人の名前が次のラウンドの枠へ進む
    if (revealing)
        for (int i = 0; i < gRev.n; i++) {
            int r = gRev.r[i], m = gRev.m[i];
            float ph = RevPhase(r, m);
            if (ph < 0.4f || ph >= 0.75f) continue;
            int w = t->winner[r][m];
            int from = t->ent[r][2 * m] == w ? 2 * m : 2 * m + 1;
            float k = Ease((ph - 0.4f) / 0.35f);
            float x0 = (float)BOX_X(r), y0 = BOX_Y(r, from), x1 = (float)BOX_X(r + 1), y1 = BOX_Y(r + 1, m);
            int fs = BOX_H(r + 1) >= 24 ? 16 : 14;
            Vector2 p = { x0 + (x1 - x0) * k + 6, y0 + (y1 - y0) * k - fs / 2 - 10.0f * sinf(PI * k) };
            DrawText(ENT_NAME(w), (int)p.x, (int)p.y, fs, accent);
        }
    #undef BOX_X
    #undef BOX_Y
    #undef BOX_H
    #undef ENT_NAME
    if (revealing) {
        const char *sk = TR("Results coming in...   Enter: skip", "ほかの試合の結果   Enter：飛ばす");
        DrawText(sk, W / 2 - MeasureText(sk, 22) / 2, H - 56, 22, LIGHTGRAY);
        return;
    }
    if (t->champion) DrawTrophy(W - 150, H - 250, 0.8f, time);
    const char *msg = TourEndMessage(t);
    DrawText(msg, W / 2 - MeasureText(msg, 24) / 2, H - 56, 24, accent);
}
