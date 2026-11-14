// SWEET SPOT — 試合の記録（matchlog.csv）：AI がなぜ強いのかを数字で調べるため
//   1 球ごと（event=shot：自分と AI の両方、サーブも）と、1 ポイントごと（event=point）、試合の終わり（event=match）を書く。
//   集計：python tools/analyze_match.py matchlog.csv
#include "game.h"
#include <time.h>

#define ML_HEADER "event,match,point,shot,who,type,kmh,quality,attack,lapse,volley,return,hit_x,hit_z,contact_h,opp_x,opp_z,opp_speed," \
                  "land_x,land_z,result,out_cm," \
                  "set,games_you,games_ai,pts_you,pts_ai,server,serve_no,rally,winner,end_kind,ender,ender_type,ender_kmh,ender_q,you_min_dist," \
                  "importance,persona,level,surface,women,style"

static FILE *gML = NULL;
static long  gMatch = 0;
static char  gPend[512];            // 着地を待っている球の記録
static bool  gPending = false;

void MLogOpen(void)
{
    FILE *old = fopen("matchlog.csv", "r");
    if (old) {
        char line[1024] = { 0 };
        if (fgets(line, sizeof(line), old)) line[strcspn(line, "\r\n")] = 0;
        fclose(old);
        if (line[0] && strcmp(line, ML_HEADER) != 0) rename("matchlog.csv", TextFormat("matchlog_old_%ld.csv", (long)time(NULL)));
    }
    gML = fopen("matchlog.csv", "a");
    if (!gML) return;
    fseek(gML, 0, SEEK_END);
    if (ftell(gML) == 0) fprintf(gML, ML_HEADER "\n");
    fflush(gML);
}
void MLogNewMatch(void) { gMatch = (long)time(NULL); gPending = false; }
long MLogMatchId(void) { return gMatch; }

// 前の球の着地が決まらないまま次の球が打たれた（ノーバウンドで返した）
static void FlushPending(const char *result, float lx, float lz, float outCm)
{
    if (!gPending || !gML) { gPending = false; return; }
    fprintf(gML, "%s,%.2f,%.2f,%s,%.0f,,,,,,,,,,,,,,,,,,,,,\n", gPend, lx, lz, result, outCm);
    fflush(gML);
    gPending = false;
}

// 球を打った（サーブも）。座標はワールド（自分は +z 側、AI は -z 側）
void MLogShot(int point, int shotNo, bool ai, const char *type, float kmh, float q, bool attack, bool lapse, bool volley, bool ret,
              Vector3 hit, float contactH, Vector3 opp, float oppSpeed)
{
    if (!gML) return;
    if (gPending) FlushPending("VOLLEYED", 0.0f, 0.0f, 0.0f);
    snprintf(gPend, sizeof(gPend), "shot,%ld,%d,%d,%s,%s,%.0f,%.2f,%d,%d,%d,%d,%.2f,%.2f,%.2f,%.2f,%.2f,%.1f",
             gMatch, point, shotNo, ai ? "AI" : "YOU", type, kmh, q, attack, lapse, volley, ret, hit.x, hit.z, contactH, opp.x, opp.z, oppSpeed);
    gPending = true;
}
// 打った球の 1 バウンド目（またはネット）
void MLogLand(const Ball *b)
{
    if (!gPending) return;
    const char *r = b->result == RESULT_IN ? "IN" : b->result == RESULT_NET ? "NET" : "OUT";
    FlushPending(r, b->firstBounce.x, b->firstBounce.z, b->result == RESULT_OUT ? b->outBy * 100.0f : 0.0f);
}

void MLogPoint(int point, const Score *before, int server, int serveNo, int rally, int winner, const char *endKind, int ender,
               const char *enderType, float enderKmh, float enderQ, float youMinDist, float importance, int persona, int level, int style)
{
    if (!gML) return;
    if (gPending) FlushPending("NONE", 0.0f, 0.0f, 0.0f);
    fprintf(gML, "point,%ld,%d,,,,,,,,,,,,,,,,,,,,%d,%d,%d,%d,%d,%s,%d,%d,%s,%s,%s,%s,%.0f,%.2f,%.2f,%.2f,%d,%d,%s,%d,%d\n",
            gMatch, point, before->nSetsDone + 1, before->games[0], before->games[1], before->points[0], before->points[1],
            server == 0 ? "YOU" : "AI", serveNo, rally, winner == 0 ? "YOU" : "AI", endKind, ender == 0 ? "YOU" : "AI",
            enderType, enderKmh, enderQ, youMinDist, importance, persona, level, SURFACES[gSurface].name, gWomen ? 1 : 0, style);
    fflush(gML);
}

void MLogMatchEnd(const Score *s, int persona, int level, int style)
{
    if (!gML) return;
    char sets[64] = { 0 };
    for (int k = 0; k < s->nSetsDone && k < 5; k++) { char b[12]; snprintf(b, sizeof(b), "%s%d-%d", k ? " " : "", s->setScore[k][0], s->setScore[k][1]); strncat(sets, b, sizeof(sets) - strlen(sets) - 1); }
    fprintf(gML, "match,%ld,,,%s,%s,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,%d,%d,%s,%d,%d\n", gMatch, s->sets[0] > s->sets[1] ? "YOU" : "AI", sets,
            persona, level, SURFACES[gSurface].name, gWomen ? 1 : 0, style);
    fflush(gML);
}
