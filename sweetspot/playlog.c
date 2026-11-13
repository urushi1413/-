// SWEET SPOT — プレイ記録（playlog.csv）
#include "game.h"

// ================= プレイ記録（playlog.csv） =================
// 自分のプレイを 1 行ずつ CSV に書き出す。どこで失敗しているかを数字で調べるため。
//   event = swing    : 振った（当たった・空振り）
//           no_swing : 来た球に振らなかった（届いたのか、届かなかったのか）
//           serve    : サーブ
// timing_offset_ms と best_quality_here が一番大事：
//   「その立ち位置のまま、一番いいタイミングで振っていたら質は何%だったか」と「そこから何 ms ずれていたか」
//   best_quality_here が低い → 立ち位置（足）の問題。高いのに quality が低い → タイミング（手）の問題

void LogOpen(PlayLog *L)
{
    // 列が違う古い記録は、別の名前にして残す（混ざると集計できないので）
    FILE *old = fopen("playlog.csv", "r");
    if (old) {
        char line[1024] = { 0 };
        if (fgets(line, sizeof(line), old)) line[strcspn(line, "\r\n")] = 0;
        fclose(old);
        if (line[0] && strcmp(line, LOG_HEADER) != 0) rename("playlog.csv", TextFormat("playlog_old_%ld.csv", (long)time(NULL)));
    }
    L->f = fopen("playlog.csv", "a");
    if (!L->f) return;
    fseek(L->f, 0, SEEK_END);
    if (ftell(L->f) == 0)
        fprintf(L->f, LOG_HEADER "\n");
    fflush(L->f);
}

void LogFlush(PlayLog *L, const char *result)
{
    if (!L->pending) return;
    L->pending = false;
    if (L->pendingServe) { if (strcmp(result, "IN") == 0) L->servesIn++; }
    else {
        if (strcmp(result, "IN") == 0) L->shotIn++;
        else if (strcmp(result, "OUT") == 0) L->shotOut++;
        else if (strcmp(result, "NET") == 0) L->shotNet++;
    }
    if (L->f) {
        if (L->pendingApex > 0.0f) fprintf(L->f, "%s,%s,%d,%.1f,%.2f,%d,%s\n", L->row, result, L->assist, L->pendingLaunch, L->pendingApex, L->style, SURFACES[gSurface].name);
        else                       fprintf(L->f, "%s,%s,%d,,,%d,%s\n", L->row, result, L->assist, L->style, SURFACES[gSurface].name);
        fflush(L->f);
    }
    L->pendingApex = 0.0f;
}

// 「この立ち位置のまま、いつ振るのが一番よかったか」を、来た球の過去の軌道と、この先の軌道から探す
void LogTimingAnalysis(const PlayLog *L, Vector3 player, const Ball *pre, ShotType type, float charge,
                              float *bestQ, float *offsetMs)
{
    *bestQ = 0.0f; *offsetMs = 0.0f;
    float bestAge = -1.0f;
    for (int i = 0; i < L->ringN + 240; i++) {
        Ball b;
        static Ball fwd;
        if (i < L->ringN) b = L->ring[(L->ringHead - L->ringN + i + LOG_RING) % LOG_RING];
        else {
            if (i == L->ringN) fwd = *pre;
            StepBall(&fwd, PHYS_DT);
            b = fwd;
        }
        bool hittable = b.active && b.bounces <= 1 && b.pos.z > 0.0f && !b.hitNet && (!b.isServe || b.bounces == 1);
        if (!hittable) continue;
        Contact c = EvaluateContact(player, &b, type, charge, 0.0f, 0.0f);
        if (c.miss == MISS_NONE && c.quality > *bestQ) { *bestQ = c.quality; bestAge = b.age; }
    }
    if (bestAge >= 0.0f) *offsetMs = (pre->age - bestAge) * 1000.0f;   // プラス＝遅い、マイナス＝早い
}

const char *StrokeName(const Contact *c) { return c->overhead ? "overhead" : (c->forehand ? "forehand" : "backhand"); }

const char *MissName(MissReason m)
{
    static const char *n[] = { "", "early", "late", "far", "high", "low", "no_ball" };
    return n[m];
}

// 来た球を毎フレーム追う：振らなかった球の記録、反応の速さ
void LogTrackIncoming(PlayLog *L, const Ball *ball, Vector3 player, Vector3 playerVel, bool charging,
                             const char *mode, const char *level)
{
    bool opp = ball->active && ball->lastHitter == HITTER_OPP;
    bool newBall = opp && (L->prevHitter != HITTER_OPP || ball->age < L->prevAge);
    bool ended = L->track && (newBall || !opp || ball->bounces >= 2);
    if (ended) {
        if (!L->swung && L->minR < 1e8f) {
            bool reachable = L->minR <= REACH_SIDE && L->hAtMin <= OVERHEAD_MAX_H;
            const char *res = ResultText(L->bounceResult);
            if (L->bounceResult == RESULT_IN || L->bounceResult == RESULT_NONE) {
                L->noSwingIn++;
                if (reachable) L->noSwingReachable++;
            } else {
                L->noSwingOut++;
            }
            if (L->f) {
                fprintf(L->f, "%.2f,%s,%s,no_swing,,,,,,%s,,,,,%.1f,%.2f,%.2f,,,%.0f,,,%.2f,%.2f,,,,,%.2f,%.2f,%.2f,%s,%d,,,%d,%s\n",
                        GetTime(), mode, level, reachable ? "reachable" : "unreachable",
                        L->angAtMin, L->minR, L->hAtMin, L->kmhAtMin, player.x, player.z,
                        L->ageAtMin, L->firstMove, L->chargeStart, res, L->assist, L->style, SURFACES[gSurface].name);
                fflush(L->f);
            }
        }
        L->track = false;
    }
    if (newBall) {
        L->track = true; L->swung = false;
        L->minR = 1e9f; L->firstMove = -1.0f; L->chargeStart = -1.0f;
        L->bounceResult = RESULT_NONE;
        L->ringN = 0;
    }
    if (L->track && opp) {
        if (L->firstMove < 0.0f && Vector3Length(playerVel) > 0.5f) {
            L->firstMove = ball->age;
            L->sumFirstMove += ball->age; L->nFirstMove++;
        }
        if (L->chargeStart < 0.0f && charging) L->chargeStart = ball->age;
        if (ball->bounces >= 1 && L->bounceResult == RESULT_NONE) L->bounceResult = ball->result;
        bool hittable = ball->bounces <= 1 && ball->pos.z > 0.0f && !ball->hitNet && (!ball->isServe || ball->bounces == 1);
        if (hittable) {
            float dx = ball->pos.x - player.x, dz = player.z - ball->pos.z;
            float r = sqrtf(dx * dx + dz * dz);
            if (r < L->minR) {
                L->minR = r; L->angAtMin = atan2f(dz, fabsf(dx)) * RAD2DEG; L->hAtMin = ball->pos.y;
                L->ageAtMin = ball->age; L->kmhAtMin = Vector3Length(ball->vel) * 3.6f;
            }
        }
    }
    L->prevHitter = ball->active ? ball->lastHitter : HITTER_NONE;
    L->prevAge = ball->age;
}

// 来た球の状態を物理ステップごとに覚えておく
void LogRecordBall(PlayLog *L, const Ball *ball)
{
    if (!L->track || ball->lastHitter != HITTER_OPP) return;
    L->ring[L->ringHead] = *ball;
    L->ringHead = (L->ringHead + 1) % LOG_RING;
    if (L->ringN < LOG_RING) L->ringN++;
}
