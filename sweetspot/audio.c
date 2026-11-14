// SWEET SPOT — 効果音：sounds フォルダの録音ファイルを読み込んで鳴らす
//   ファイルがない音は鳴らさない（ゲームはそのまま動く）。
//   同じ名前に _2, _3, _4 を付けたファイルを置くと、毎回その中からランダムに選ぶ（同じ音の繰り返しを避ける）
//   形式は .wav / .ogg / .mp3 / .flac
#include "game.h"

#define POOL      3          // 同じ音が重なっても鳴るように、同じ音を何個か読み込む
#define VARIANTS  6          // 1 つの音につき、録音の種類（_2〜_6）

typedef struct { Sound s[VARIANTS][POOL]; int nVar; int next; } SfxSlot;

enum {
    S_HIT_GOOD, S_HIT_OK, S_HIT_BAD, S_HIT_FRAME, S_HIT_SLICE, S_HIT_VOICE, S_HIT_VOICE_SOFT, S_GRUNT_PRE, S_SERVE,
    S_BOUNCE_HARD, S_BOUNCE_CLAY, S_BOUNCE_GRASS, S_NET, S_WHIFF, S_SQUEAK, S_SLIDE,
    S_CROWD, S_APPLAUSE, S_APPLAUSE_BIG, S_CALL_FAULT, S_CALL_OUT, S_COUNT
};
// sounds フォルダに置くファイルの名前（拡張子なし）
static const char *SFX_NAMES[S_COUNT] = {
    "hit_good", "hit_ok", "hit_bad", "hit_frame", "hit_slice", "hit_voice", "hit_voice_soft", "grunt_pre", "serve",
    "bounce_hard", "bounce_clay", "bounce_grass", "net", "whiff", "squeak", "slide",
    "crowd", "applause", "applause_big", "call_fault", "call_out"
};

static bool    gAudioOk = false;
static float   gVolume  = 0.8f;
static SfxSlot gSlot[S_COUNT];
static int     gLoaded = 0;
static char    gDir[1100] = "sounds";
static Sound   gBeep;                 // 確認用の「ピッ」（ファイルがなくても鳴る）
static bool    gBeepOk = false;

void SfxInit(void)
{
    InitAudioDevice();
    gAudioOk = IsAudioDeviceReady();
    if (!gAudioOk) return;
    // sounds フォルダを探す：実行したフォルダ → 実行ファイルの隣 → sweetspot/sounds（リポジトリの一番上から起動したとき）
    // TextFormat の結果は使い回されて上書きされるので、自分の配列に写しておく
    static char dir[1024];
    if (DirectoryExists("sounds"))                                          snprintf(dir, sizeof(dir), "%s/sounds", GetWorkingDirectory());
    else if (DirectoryExists(TextFormat("%ssounds", GetApplicationDirectory()))) snprintf(dir, sizeof(dir), "%ssounds", GetApplicationDirectory());
    else if (DirectoryExists("sweetspot/sounds"))                           snprintf(dir, sizeof(dir), "%s/sweetspot/sounds", GetWorkingDirectory());
    else                                                                     snprintf(dir, sizeof(dir), "%s/sounds", GetWorkingDirectory());
    snprintf(gDir, sizeof(gDir), "%s", dir);
    if (DirectoryExists(dir)) {
        // 中のフォルダも探す（zip を解凍すると、もう一段フォルダができることがあるので）
        FilePathList files = LoadDirectoryFilesEx(dir, ".wav;.ogg;.mp3;.flac", true);
        for (int i = 0; i < S_COUNT; i++) {
            for (int v = 1; v <= VARIANTS && gSlot[i].nVar < VARIANTS; v++) {
                char want[64];
                if (v == 1) snprintf(want, sizeof(want), "%s", SFX_NAMES[i]);
                else        snprintf(want, sizeof(want), "%s_%d", SFX_NAMES[i], v);
                for (unsigned int f = 0; f < files.count; f++) {
                    char nm[256];
                    snprintf(nm, sizeof(nm), "%s", GetFileNameWithoutExt(files.paths[f]));
                    if (strcmp(nm, want) != 0) continue;
                    Wave w = LoadWave(files.paths[f]);
                    if (w.frameCount == 0) continue;
                    for (int k = 0; k < POOL; k++) gSlot[i].s[gSlot[i].nVar][k] = LoadSoundFromWave(w);
                    UnloadWave(w);
                    gSlot[i].nVar++;
                    break;
                }
            }
            if (gSlot[i].nVar > 0) gLoaded++;
        }
        UnloadDirectoryFiles(files);
    }
    TraceLog(LOG_INFO, "SFX: %d / %d sounds loaded from %s", gLoaded, S_COUNT, dir);
    // 確認用の「ピッ」（880Hz、0.15 秒）
    {
        int n = 44100 * 15 / 100;
        short *pcm = (short *)MemAlloc(n * sizeof(short));
        for (int i = 0; i < n; i++) {
            float t = (float)i / 44100.0f, e = fminf(1.0f, fminf(t / 0.005f, (0.15f - t) / 0.02f));
            pcm[i] = (short)(sinf(2.0f * PI * 880.0f * t) * e * 0.5f * 32767.0f);
        }
        Wave w = { .frameCount = (unsigned int)n, .sampleRate = 44100, .sampleSize = 16, .channels = 1, .data = pcm };
        gBeep = LoadSoundFromWave(w);
        MemFree(pcm);
        gBeepOk = true;
    }
}

void SfxClose(void)
{
    if (!gAudioOk) return;
    for (int i = 0; i < S_COUNT; i++)
        for (int v = 0; v < gSlot[i].nVar; v++)
            for (int k = 0; k < POOL; k++) UnloadSound(gSlot[i].s[v][k]);
    if (gBeepOk) UnloadSound(gBeep);
    CloseAudioDevice();
    gAudioOk = false;
}

void  SfxSetVolume(float v) { gVolume = Clamp01(v); }
float SfxGetVolume(void)    { return gVolume; }
int   SfxLoadedCount(void)  { return gLoaded; }
bool  SfxDeviceOk(void)     { return gAudioOk; }
const char *SfxDir(void)    { return gDir; }

// 鳴らす。ないときは代わりの音を使う（なければ鳴らさない）
static void Play(int id, int fallback, float vol, float pitch)
{
    if (!gAudioOk || vol <= 0.001f || gVolume <= 0.0f) return;
    if (gSlot[id].nVar == 0) { if (fallback >= 0) Play(fallback, -1, vol, pitch); return; }
    SfxSlot *s = &gSlot[id];
    int v = GetRandomValue(0, s->nVar - 1);
    Sound *snd = &s->s[v][s->next];
    s->next = (s->next + 1) % POOL;
    SetSoundVolume(*snd, Clamp01(vol) * gVolume);
    SetSoundPitch(*snd, pitch);
    PlaySound(*snd);
}

// 遠いほど小さく（カメラからの距離）
static float DistVol(float dist) { return Clamp01(1.3f / (1.0f + dist / 14.0f)); }
static float Jitter(void) { return 1.0f + GetRandomValue(-20, 20) / 1000.0f; }   // 毎回少しだけ音の高さを変える

// 最初に見つかった音の番号（なければ -1）
static int First3(int a, int b, int c) { return gSlot[a].nVar ? a : gSlot[b].nVar ? b : gSlot[c].nVar ? c : -1; }

// voice：この打球の前に声（SfxGruntPre）を出し始めたか。出し始めたときだけ「打球音と声の続き」を鳴らす
//   （途中で声が切れたり、始まりのない声の続きだけが鳴ったりしないように）
//   defensive：体勢が崩れて取った球（飛びついた・ぎりぎり）。ラケットでぎりぎり取って地面に当たる音と控えめな声
static double gVoiceUntil = 0.0;
void SfxHit(float q, float kmh, bool slice, bool voice, bool defensive, float dist)
{
    float vol = (0.5f + 0.5f * Clamp01(kmh / 150.0f)) * DistVol(dist);
    if (voice && gSlot[S_HIT_VOICE].nVar) { Play(S_HIT_VOICE, -1, vol, 1.0f); return; }   // 声から続くので音の高さは変えない
    if (defensive && !slice && gSlot[S_HIT_VOICE_SOFT].nVar && GetTime() >= gVoiceUntil) {
        gVoiceUntil = GetTime() + 0.45;   // 声は一度に 1 人
        Play(S_HIT_VOICE_SOFT, -1, vol, 1.0f);
        return;
    }
    if (q < 0.18f)      Play(S_HIT_FRAME, S_HIT_BAD, vol, Jitter());
    else if (slice)     Play(S_HIT_SLICE, q < 0.5f ? S_HIT_BAD : S_HIT_OK, vol, Jitter());
    else if (q < 0.45f) Play(S_HIT_BAD, S_HIT_OK, vol, Jitter());
    else if (q < 0.75f) Play(S_HIT_OK, S_HIT_GOOD, vol, Jitter());
    else                Play(S_HIT_GOOD, S_HIT_OK, vol, Jitter());
}

// ラケットを地面に叩きつける音（フレームの音を低く、大きく）
void SfxRacketSmash(float dist)
{
    Play(S_HIT_FRAME, S_HIT_BAD, 1.0f * DistVol(dist), 0.70f + GetRandomValue(-30, 30) / 1000.0f);
}

void SfxServe(float q, float kmh, bool voice, float dist)
{
    SfxHit(q, kmh, false, voice, false, dist);
}

// 試しに鳴らす（M で音をオンにしたとき）
void SfxTest(void)
{
    int id = First3(S_HIT_VOICE, S_HIT_GOOD, S_HIT_OK);
    if (id >= 0) { Play(id, -1, 0.9f, 1.0f); return; }
    if (gAudioOk && gBeepOk) { SetSoundVolume(gBeep, gVolume); PlaySound(gBeep); }   // ファイルがないときは「ピッ」
}

// 打つ前の声：打つ GRUNT_LEAD 秒前に鳴らし始める（溜めの間に声が出て、そのまま打球音と声の続きにつながる）
//   声は一度に 1 人だけ：前の声（打つ前＋続き）が鳴り終わるまでは始めない。始めたら true
bool SfxGruntPre(float dist)
{
    if (!gAudioOk || gSlot[S_GRUNT_PRE].nVar == 0 || gSlot[S_HIT_VOICE].nVar == 0 || gVolume <= 0.0f) return false;
    double now = GetTime();
    if (now < gVoiceUntil) return false;
    gVoiceUntil = now + GRUNT_LEAD + 0.45;
    Play(S_GRUNT_PRE, -1, 0.9f * DistVol(dist), 1.0f);
    return true;
}

void SfxWhiff(void) { Play(S_WHIFF, -1, 0.7f, Jitter()); }

void SfxBounce(float speed, float dist)
{
    int id = gSurface == SURF_CLAY ? S_BOUNCE_CLAY : gSurface == SURF_GRASS ? S_BOUNCE_GRASS : S_BOUNCE_HARD;
    Play(id, S_BOUNCE_HARD, Clamp01(0.3f + speed / 30.0f) * DistVol(dist), Jitter());
}

void SfxNet(float speed, float dist) { Play(S_NET, -1, Clamp01(0.4f + speed / 30.0f) * DistVol(dist), 1.0f); }

// 滑り込み・急な切り返し：ハードは靴の音、クレーと芝は滑る音
void SfxSlide(float dist)
{
    if (gSurface == SURF_HARD) Play(S_SQUEAK, S_SLIDE, 0.6f * DistVol(dist), Jitter());   // 滑るときは「キュッ」1 回
    else                       Play(S_SLIDE, -1, (gSurface == SURF_CLAY ? 0.9f : 0.5f) * DistVol(dist), Jitter());
}

// 観客のざわめき：on の間は鳴らし続ける（毎フレーム呼ぶ）
void SfxCrowd(bool on, float level)
{
    if (!gAudioOk || gSlot[S_CROWD].nVar == 0) return;
    Sound *s = &gSlot[S_CROWD].s[0][0];
    if (!on) { if (IsSoundPlaying(*s)) StopSound(*s); return; }
    SetSoundVolume(*s, 0.25f * level * gVolume);
    if (!IsSoundPlaying(*s)) PlaySound(*s);
}

// ポイントが終わったときの拍手：長いラリーやウィナーほど大きい
void SfxPointEnd(int rallyHits, bool winner)
{
    float k = Clamp01((rallyHits - 2) / 10.0f) + (winner ? 0.35f : 0.0f);
    if (k < 0.6f) Play(S_APPLAUSE, S_APPLAUSE_BIG, 0.35f + 0.6f * k, 1.0f);
    else          Play(S_APPLAUSE_BIG, S_APPLAUSE, 0.5f + 0.4f * Clamp01(k), 1.0f);
}

// 審判のコール「フォルト」
void SfxCallFault(void) { Play(S_CALL_FAULT, -1, 0.85f, 1.0f); }

// 審判のコール「アウト」（ラリー中の球がアウトになったとき）
void SfxCallOut(void) { Play(S_CALL_OUT, -1, 0.85f, 1.0f); }

// 拍手を止める（サーブのトスを上げたら静かに：ポイント中は観客は静かにする決まり）
void SfxStopApplause(void)
{
    if (!gAudioOk) return;
    int ids[2] = { S_APPLAUSE, S_APPLAUSE_BIG };
    for (int i = 0; i < 2; i++)
        for (int v = 0; v < gSlot[ids[i]].nVar; v++)
            for (int k = 0; k < POOL; k++) if (IsSoundPlaying(gSlot[ids[i]].s[v][k])) StopSound(gSlot[ids[i]].s[v][k]);
}

// ---- フットワーク：球に寄って距離を詰めるとき、「キュッ」を 2〜3 回続けて（ハードだけ） ----
typedef struct { double t; float vol; } QueuedSfx;
static QueuedSfx gQ[8];
static int gQn = 0;
void SfxFootwork(int n, float dist)
{
    if (!gAudioOk || gSurface != SURF_HARD || gSlot[S_SQUEAK].nVar == 0) return;
    double t = GetTime();
    for (int i = 0; i < n && gQn < 8; i++) {
        gQ[gQn++] = (QueuedSfx){ t, (0.35f + 0.15f * (GetRandomValue(0, 100) / 100.0f)) * DistVol(dist) };
        t += 0.09 + GetRandomValue(0, 40) / 1000.0;   // 0.09〜0.13 秒おき
    }
}
// 毎フレーム呼ぶ：時間が来た音を鳴らす
void SfxUpdate(void)
{
    double now = GetTime();
    for (int i = 0; i < gQn; ) {
        if (gQ[i].t <= now) { Play(S_SQUEAK, -1, gQ[i].vol, Jitter()); gQ[i] = gQ[--gQn]; }
        else i++;
    }
}
