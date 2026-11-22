// SWEET SPOT — 記録（sweetspot_records.txt）：通算成績と自己ベスト。次に起動したときに「前の自分」がいるように
//   保存は設定と同じ「key=値」の 1 行ずつ。配列は played_0_1=3 のように添え字を名前に入れる。知らないキーは読み飛ばす（後で足しても壊れない）
//   ブラウザ版は localStorage にも写す（storage.c）
#include "game.h"

Career gCareer;
static const char *CAREER_FILE = "sweetspot_records.txt";

void CareerLoad(void)
{
    memset(&gCareer, 0, sizeof(gCareer));
    StoreRestore(CAREER_FILE);
    FILE *f = fopen(CAREER_FILE, "r");
    if (!f) return;
    char line[128];
    while (fgets(line, sizeof(line), f)) {
        char key[64]; float v; int a, b;
        if (sscanf(line, "%63[^=]=%f", key, &v) != 2) continue;
        int iv = (int)v;
        if (sscanf(key, "played_%d_%d", &a, &b) == 2) { if (a >= 0 && a < 3 && b >= 0 && b < 2) gCareer.played[a][b] = iv; }
        else if (sscanf(key, "won_%d_%d", &a, &b) == 2) { if (a >= 0 && a < 3 && b >= 0 && b < 2) gCareer.won[a][b] = iv; }
        else if (sscanf(key, "vs_played_%d", &a) == 1) { if (a >= 0 && a < PERSONA_COUNT) gCareer.vsPlayed[a] = iv; }
        else if (sscanf(key, "vs_won_%d", &a) == 1) { if (a >= 0 && a < PERSONA_COUNT) gCareer.vsWon[a] = iv; }
        else if (sscanf(key, "titles_%d", &a) == 1) { if (a >= 0 && a < SLAM_COUNT) gCareer.titles[a] = iv; }
        else if (!strcmp(key, "loss_streak"))   gCareer.lossStreak = iv;
        else if (!strcmp(key, "best_serve"))    gCareer.bestServeKmh = v;
        else if (!strcmp(key, "best_shot"))     gCareer.bestShotKmh = v;
        else if (!strcmp(key, "longest_rally")) gCareer.longestRally = iv;
        else if (!strcmp(key, "rally_best"))    gCareer.rallyBest = iv;
        else if (!strcmp(key, "target_best"))   gCareer.targetBest = iv;
        else if (!strcmp(key, "aces"))          gCareer.aces = iv;
        else if (!strcmp(key, "winners"))       gCareer.winners = iv;
        else if (!strcmp(key, "hint_lower"))    gCareer.hintLower = iv != 0;
    }
    fclose(f);
}

void CareerSave(void)
{
    FILE *f = fopen(CAREER_FILE, "w");
    if (!f) return;
    for (int a = 0; a < 3; a++) for (int b = 0; b < 2; b++) fprintf(f, "played_%d_%d=%d\nwon_%d_%d=%d\n", a, b, gCareer.played[a][b], a, b, gCareer.won[a][b]);
    for (int p = 0; p < PERSONA_COUNT; p++) if (gCareer.vsPlayed[p]) fprintf(f, "vs_played_%d=%d\nvs_won_%d=%d\n", p, gCareer.vsPlayed[p], p, gCareer.vsWon[p]);
    for (int k = 0; k < SLAM_COUNT; k++) fprintf(f, "titles_%d=%d\n", k, gCareer.titles[k]);
    fprintf(f, "loss_streak=%d\nbest_serve=%.1f\nbest_shot=%.1f\nlongest_rally=%d\nrally_best=%d\ntarget_best=%d\naces=%d\nwinners=%d\nhint_lower=%d\n",
            gCareer.lossStreak, gCareer.bestServeKmh, gCareer.bestShotKmh, gCareer.longestRally, gCareer.rallyBest, gCareer.targetBest,
            gCareer.aces, gCareer.winners, gCareer.hintLower ? 1 : 0);
    fclose(f);
    StorePersist(CAREER_FILE);
}

// 新記録なら書き換えて true（演出は呼んだ側）
bool CareerBestF(float *slot, float v) { if (v > *slot) { *slot = v; return true; } return false; }
bool CareerBestI(int *slot, int v)     { if (v > *slot) { *slot = v; return true; } return false; }

int CareerPlayed(void) { int n = 0; for (int a = 0; a < 3; a++) for (int b = 0; b < 2; b++) n += gCareer.played[a][b]; return n; }
int CareerWon(void)    { int n = 0; for (int a = 0; a < 3; a++) for (int b = 0; b < 2; b++) n += gCareer.won[a][b]; return n; }
int CareerTitles(void) { int n = 0; for (int k = 0; k < SLAM_COUNT; k++) n += gCareer.titles[k]; return n; }
