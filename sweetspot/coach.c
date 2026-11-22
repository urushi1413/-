// SWEET SPOT — 今日のコーチ：試合の後に、次に直すことを 1〜3 個、言葉で
//   プレイ記録（PlayLog）はセッションの合計なので、試合の始めの値を写しておき（CoachTake）、終わりとの差から決める。
//   自動操作の球は PlayLog に入らないので、自動操作の試合は「何も当てはまらない」の言葉になる
//   （設計書 8 章のポイントごとの記録ができたら、入力をそちらに差し替える）
#include "game.h"

CoachSnap CoachTake(const PlayLog *L)
{
    CoachSnap s = { L->swings, L->hits, L->whiffs, L->early, L->late, L->onTime, L->posProblem, L->timingProblem,
                    L->noSwingReachable, L->shotIn, L->shotOut, L->shotNet };
    return s;
}

// 試合の始めとの差から、直すことを最大 3 つ（優先順）。戻り値は数。out[i] は TR 済みの文（静的な領域）
int CoachTips(CoachSnap a, CoachSnap b, const MatchStats ms[2], const int mst[2][6], bool won, const char *out[3])
{
    static char buf[3][192];
    int n = 0;
    CoachSnap d = { b.swings - a.swings, b.hits - a.hits, b.whiffs - a.whiffs, b.early - a.early, b.late - a.late, b.onTime - a.onTime,
                    b.pos - a.pos, b.timing - a.timing, b.noSwing - a.noSwing, b.in - a.in, b.out - a.out, b.net - a.net };
    int t = d.early + d.late + d.onTime;
    #define TIP(...) do { if (n < 3) { snprintf(buf[n], sizeof(buf[n]), __VA_ARGS__); out[n] = buf[n]; n++; } } while (0)
    if (d.whiffs >= 3 && d.whiffs * 4 >= d.swings)
        TIP(TR("%d whiffs: start before the ball comes, and move until it's inside the white ring at your feet", "空振りが %d 回：球が来る前に押し始めて、足元の白い輪に球が入る所まで動こう"), d.whiffs);
    if (t >= 8 && d.late * 100 / t >= 45)
        TIP(TR("Late %d%% of the time: start holding as soon as they hit, release a little earlier", "振り遅れが %d%%：相手が打ったらすぐ押し始めて、少し早く離そう"), d.late * 100 / t);
    else if (t >= 8 && d.early * 100 / t >= 45)
        TIP(TR("Early %d%% of the time: keep holding until the ball is in front of you", "早すぎが %d%%：球が体の前に来るまで、押したまま待とう"), d.early * 100 / t);
    if (d.pos + d.timing >= 6 && d.pos * 2 >= d.timing * 3)
        TIP("%s", TR("Most misses come from your feet: get to where the ball is going first (assist 'spot' helps)", "ミスの原因は主に足：球が来る所へ先回りしよう（手がかり「立つ場所」が助けになる）"));
    else if (d.pos + d.timing >= 6 && d.timing * 2 >= d.pos * 3)
        TIP("%s", TR("Your feet are fine. Focus on the moment you release", "足は合っている。離す瞬間だけ意識しよう"));
    if (d.net >= 4 && d.net * 2 >= d.out * 3)
        TIP(TR("%d into the net: hold a bit longer for depth (deeper = higher over the net)", "ネットが %d 本：少し長く押して深めに（深いほど高く越える）"), d.net);
    else if (d.out >= 4 && d.out * 2 >= d.net * 3)
        TIP(TR("%d out: hold shorter. Topspin (left click) dips in", "アウトが %d 本：押す時間を短めに。トップスピン（左クリック）は落ちやすい"), d.out);
    if (d.noSwing >= 3)
        TIP(TR("You let %d reachable balls go: when in doubt, swing", "届く球を %d 回見送った：迷ったら振ろう"), d.noSwing);
    if (mst[0][5] >= 2)
        TIP(TR("%d double faults: the kick (left click) is the safe second serve", "ダブルフォールトが %d 本：セカンドはキック（左クリック）が安全"), mst[0][5]);
    if (ms[1].netPts >= 3 && ms[1].netWon * 10 >= ms[1].netPts * 6)
        TIP(TR("They won %d/%d at the net: lob (Shift+click) or hit at their feet", "相手のネットプレーに %d/%d 取られた：ロブ（Shift＋クリック）か足元へ"), ms[1].netWon, ms[1].netPts);
    if (n == 0) {
        int sh = d.in + d.out + d.net;
        if (won) TIP("%s", TR("Great match! Next time, try one AI level up", "いい試合！ 次は AI の強さを 1 つ上げてみよう"));
        else if (sh > 0) TIP(TR("So close. Shots in: %d%%", "あと少し。入った率は %d%%"), d.in * 100 / sh);   // 自分の打球がない（自動操作）なら言わない
    }
    #undef TIP
    return n;
}
