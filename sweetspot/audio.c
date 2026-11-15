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
    S_CROWD, S_APPLAUSE, S_APPLAUSE_BIG, S_CALL_FAULT, S_CALL_OUT, S_CELEBRATE, S_ANGRY, S_MATCH_WIN,
    S_BOO, S_OOH, S_RACKET_SMASH, S_RACKET_LAND,
    S_CALL_LOVE, S_CALL_15, S_CALL_30, S_CALL_40, S_CALL_ADV_S, S_CALL_ADV_R,
    S_CALL_GAME_S, S_CALL_GAME_R, S_CALL_SET_S, S_CALL_SET_R, S_CALL_ALL, S_CALL_DEUCE,
    S_CALL_GSM_S, S_CALL_GSM_R, S_CALL_OUT_LINE, S_COUNT
};
// sounds フォルダに置くファイルの名前（拡張子なし）
static const char *SFX_NAMES[S_COUNT] = {
    "hit_good", "hit_ok", "hit_bad", "hit_frame", "hit_slice", "hit_voice", "hit_voice_soft", "grunt_pre", "serve",
    "bounce_hard", "bounce_clay", "bounce_grass", "net", "whiff", "squeak", "slide",
    "crowd", "applause", "applause_big", "call_fault", "call_out", "celebrate", "angry", "match_win",
    "boo", "ooh", "racket_smash", "racket_land",
    "call_love", "call_15", "call_30", "call_40", "call_adv_server", "call_adv_receiver",
    "call_game_server", "call_game_receiver", "call_set_server", "call_set_receiver", "call_all", "call_deuce",
    "call_gsm_server", "call_gsm_receiver", "call_out_line"
};
// スコアの言い回しごとのコール（call_<サーバーの点>_<レシーバーの点>、例 call_15_0 は「フィフティーン・ラブ」）
static SfxSlot gCallP[4][4];
static const int PTS[4] = { 0, 15, 30, 40 };
static bool gLineJudges = false;   // 線審が声でコールする（クレー：ローランドギャロスなど。電子判定ではない）

// ---- 選手ごとの声 ----
// 選手の録音（<名前>_grunt_pre など）があればそれを使う。なければ共通の声を、選手ごとの高さ・大きさで鳴らす
//   サーブは <名前>_serve_pre / <名前>_serve_voice（なければその選手のストロークの声）
//   雄叫びは <名前>_roar（決まったと確信したショットで、打った瞬間から叫ぶ。打球音入り）。本人の録音がある選手だけ
enum { V_GRUNT_PRE, V_HIT_VOICE, V_CELEBRATE, V_ANGRY, V_SERVE_PRE, V_SERVE_VOICE, V_ROAR, V_COUNT };
static const char *VOICE_NAMES[V_COUNT] = { "grunt_pre", "hit_voice", "celebrate", "angry", "serve_pre", "serve_voice", "roar" };
static const int VOICE_GENERIC[V_COUNT] = { S_GRUNT_PRE, S_HIT_VOICE, S_CELEBRATE, S_ANGRY, S_GRUNT_PRE, S_HIT_VOICE, S_HIT_VOICE };
typedef struct { const char *file; float pitch, vol, chance; } VoiceProfile;
static const VoiceProfile VOICE[PERSONA_COUNT] = {
    { "ai",       1.00f, 0.90f, 0.60f },
    { PN("federerr", "v01"), 1.06f, 0.55f, 0.05f },   // ほぼ声を出さない（息を吐く音くらい）。出しても小さい
    { PN("djokovik", "v02"), 1.10f, 0.90f, 0.65f },   // 短く鋭い
    { PN("nadar", "v03"),    0.86f, 1.00f, 0.92f },   // ほぼ毎回、低く長く大きい
    { PN("murraye", "v04"),  0.95f, 0.85f, 0.60f },
    { PN("alcarazz", "v05"), 1.00f, 0.95f, 0.80f },   // ストロークの約 8 割、サーブは毎回（Betting.com の調べ）
    { PN("sinnerr", "v06"),  1.05f, 0.80f, 0.15f },   // 静か。強く打ったときにたまに短く
    { PN("medvedeb", "v07"), 0.95f, 0.70f, 0.35f },
    { PN("kirgios", "v08"),  1.00f, 0.95f, 0.50f },   // 2 球に 1 球ほど、大きな声
    { PN("williamz", "v09"), 1.30f, 1.00f, 0.85f },   // 女子：声が高い
    { PN("swiontek", "v10"), 1.40f, 0.85f, 0.60f },
    { PN("sabalenco", "v11"),1.28f, 1.00f, 0.95f },   // 大きな声。ストロークの約 95%、サーブは毎回（Betting.com の調べ）
    { PN("gauph", "v12"),    1.42f, 0.80f, 0.55f },
    { PN("sharapowa", "v13"),1.45f, 1.00f, 1.00f },   // 一番大きい声（毎回）
    { PN("bartie", "v14"),   1.38f, 0.70f, 0.35f },
    { PN("nishikoli", "v15"),1.05f, 0.80f, 0.50f },
    { PN("zverew", "v16"),   0.95f, 0.90f, 0.60f },
};
static const VoiceProfile VOICE_HUMAN = { "you", 1.00f, 0.90f, 0.60f };
static const VoiceProfile VOICE_HUMAN_W = { "you", 1.35f, 0.90f, 0.60f };   // 女子の試合：自分と癖なしの AI は声を高く
static const VoiceProfile VOICE_AI_W = { "ai", 1.35f, 0.90f, 0.60f };
static SfxSlot gPV[PERSONA_COUNT + 1][V_COUNT];   // 選手ごとの録音（最後は人間）
static int     gVoiceWho = -1;                    // 今鳴っている声の主（打球の声の続きに使う）
static const VoiceProfile *Prof(int who)
{
    if (who < 0 || who >= PERSONA_COUNT) return gWomen ? &VOICE_HUMAN_W : &VOICE_HUMAN;
    if (who == 0 && gWomen) return &VOICE_AI_W;
    return &VOICE[who];
}
static SfxSlot *PSlot(int who, int v) { return &gPV[(who < 0 || who >= PERSONA_COUNT) ? PERSONA_COUNT : who][v]; }

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
        // 選手ごとの声：<名前>_grunt_pre, <名前>_hit_voice, <名前>_celebrate, <名前>_angry（_2〜 も）
        for (int w = 0; w <= PERSONA_COUNT; w++)
            for (int vi = 0; vi < V_COUNT; vi++) {
                SfxSlot *sl = &gPV[w][vi];
                const char *who = w < PERSONA_COUNT ? VOICE[w].file : VOICE_HUMAN.file;
                for (int v = 1; v <= VARIANTS && sl->nVar < VARIANTS; v++) {
                    char want[96];
                    if (v == 1) snprintf(want, sizeof(want), "%s_%s", who, VOICE_NAMES[vi]);
                    else        snprintf(want, sizeof(want), "%s_%s_%d", who, VOICE_NAMES[vi], v);
                    for (unsigned int f = 0; f < files.count; f++) {
                        if (strcmp(GetFileNameWithoutExt(files.paths[f]), want) != 0) continue;
                        Wave wv = LoadWave(files.paths[f]);
                        if (wv.frameCount == 0) continue;
                        for (int k = 0; k < POOL; k++) sl->s[sl->nVar][k] = LoadSoundFromWave(wv);
                        UnloadWave(wv);
                        sl->nVar++;
                        break;
                    }
                }
                if (sl->nVar > 0) TraceLog(LOG_INFO, "SFX: voice %s_%s (%d)", who, VOICE_NAMES[vi], sl->nVar);
            }
        for (int a = 0; a < 4; a++)   // スコアの言い回しのコール
            for (int b = 0; b < 4; b++) {
                const char *want = TextFormat("call_%d_%d", PTS[a], PTS[b]);
                for (unsigned int f = 0; f < files.count; f++) {
                    if (strcmp(GetFileNameWithoutExt(files.paths[f]), want) != 0) continue;
                    Wave w = LoadWave(files.paths[f]);
                    if (w.frameCount == 0) continue;
                    for (int k = 0; k < POOL; k++) gCallP[a][b].s[0][k] = LoadSoundFromWave(w);
                    UnloadWave(w);
                    gCallP[a][b].nVar = 1;
                    break;
                }
            }
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
    for (int w = 0; w <= PERSONA_COUNT; w++)
        for (int vi = 0; vi < V_COUNT; vi++)
            for (int v = 0; v < gPV[w][vi].nVar; v++)
                for (int k = 0; k < POOL; k++) UnloadSound(gPV[w][vi].s[v][k]);
    for (int a = 0; a < 4; a++) for (int b = 0; b < 4; b++)
        if (gCallP[a][b].nVar) for (int k = 0; k < POOL; k++) UnloadSound(gCallP[a][b].s[0][k]);
    if (gBeepOk) UnloadSound(gBeep);
    CloseAudioDevice();
    gAudioOk = false;
}

void  SfxSetVolume(float v) { gVolume = Clamp01(v); }
float SfxGetVolume(void)    { return gVolume; }
int   SfxLoadedCount(void)  { return gLoaded; }
bool  SfxDeviceOk(void)     { return gAudioOk; }
const char *SfxDir(void)    { return gDir; }

// 代わりの音の、さらに代わり（打球音は hit_frame → hit_bad → hit_ok → hit_good とたどる）
static int NextFallback(int id)
{
    switch (id) {
        case S_HIT_FRAME: return S_HIT_BAD;
        case S_HIT_BAD:   return S_HIT_OK;
        case S_HIT_SLICE: return S_HIT_OK;
        case S_HIT_OK:    return S_HIT_GOOD;
        default:          return -1;
    }
}
// 鳴らす。ないときは代わりの音を使う（なければ鳴らさない）
static void Play(int id, int fallback, float vol, float pitch)
{
    if (!gAudioOk || vol <= 0.001f || gVolume <= 0.0f) return;
    if (gSlot[id].nVar == 0) { if (fallback >= 0) Play(fallback, NextFallback(fallback), vol, pitch); return; }
    SfxSlot *s = &gSlot[id];
    int v = GetRandomValue(0, s->nVar - 1);
    Sound *snd = &s->s[v][s->next];
    s->next = (s->next + 1) % POOL;
    SetSoundVolume(*snd, Clamp01(vol) * gVolume);
    SetSoundPitch(*snd, pitch);
    PlaySound(*snd);
}

// 声は打球音より大きく鳴らす（1 を超えると増幅）。打つ前の声は録音が小さいので、さらに上げる
#define VOICE_GAIN      1.7f
#define GRUNT_PRE_GAIN  4.0f
static void PlaySlot(SfxSlot *s, float vol, float pitch)
{
    if (!gAudioOk || s->nVar == 0 || vol <= 0.001f || gVolume <= 0.0f) return;
    int v = GetRandomValue(0, s->nVar - 1);
    Sound *snd = &s->s[v][s->next];
    s->next = (s->next + 1) % POOL;
    SetSoundVolume(*snd, Clamp(vol, 0.0f, 6.0f) * gVolume);
    SetSoundPitch(*snd, pitch);
    PlaySound(*snd);
}
// 選手の声を鳴らす：本人の録音があればそのまま、なければ共通の声を高さを変えて
// 声は遠くまで届く（打球音ほど距離で小さくならない）
static float VoiceDistVol(float dist) { return Clamp(1.4f / (1.0f + dist / 30.0f), 0.0f, 1.3f); }
static bool PlayVoice(int who, int vi, float vol)
{
    vol *= VOICE_GAIN * (vi == V_GRUNT_PRE ? GRUNT_PRE_GAIN : 1.0f);
    SfxSlot *own = PSlot(who, vi);
    if (own->nVar) { PlaySlot(own, vol, 1.0f); return true; }
    int g = VOICE_GENERIC[vi];
    if (gSlot[g].nVar == 0) return false;
    PlaySlot(&gSlot[g], vol * Prof(who)->vol, Prof(who)->pitch);
    return true;
}
// ---- 打つ前の声と、打った後の声の組（同じ録音の前半と後半） ----
//   選手本人の録音（サーブはサーブの録音）があればそれを、なければ共通の声を選手ごとの高さで。
//   本人の録音は打つ前と打った後を同じ大きさの比で切ってあるので、打つ前の声を大きくしない（共通の声だけ 4 倍）
typedef struct { SfxSlot *pre, *hit; float pitch, vol, preGain; bool own; } VoicePair;
static VoicePair Pair(int who, bool serve)
{
    SfxSlot *sp = PSlot(who, V_SERVE_PRE), *sh = PSlot(who, V_SERVE_VOICE);
    if (serve && sp->nVar && sh->nVar) return (VoicePair){ sp, sh, 1.0f, 1.0f, 1.0f, true };
    SfxSlot *gp = PSlot(who, V_GRUNT_PRE), *gh = PSlot(who, V_HIT_VOICE);
    if (gp->nVar && gh->nVar) return (VoicePair){ gp, gh, 1.0f, 1.0f, 1.0f, true };
    return (VoicePair){ &gSlot[S_GRUNT_PRE], &gSlot[S_HIT_VOICE], Prof(who)->pitch, Prof(who)->vol, GRUNT_PRE_GAIN, false };
}
static float VarLen(const SfxSlot *s, int v)
{
    if (!s->nVar) return 0.0f;
    Sound snd = s->s[v % s->nVar][0];
    return snd.stream.sampleRate ? (float)snd.frameCount / (float)snd.stream.sampleRate : 0.0f;
}
static void PlaySlotVar(SfxSlot *s, int v, float vol, float pitch)
{
    if (!gAudioOk || s->nVar == 0 || vol <= 0.001f || gVolume <= 0.0f) return;
    Sound *snd = &s->s[v % s->nVar][s->next];
    s->next = (s->next + 1) % POOL;
    SetSoundVolume(*snd, Clamp(vol, 0.0f, 6.0f) * gVolume);
    SetSoundPitch(*snd, pitch);
    PlaySound(*snd);
}
static int gNextVar[PERSONA_COUNT + 1][2];   // 次に鳴らす録音（選手ごと、ストローク / サーブ）。鳴らす前に長さが分かるように先に決めておく
static int WhoIdx(int who) { return (who < 0 || who >= PERSONA_COUNT) ? PERSONA_COUNT : who; }
// 打つ何秒前に声を鳴らし始めるか：本人の録音は打つ前の声の長さ（録音ごとに違う）、共通の声は GRUNT_LEAD（高さを変えた分だけ長さも変わる）
static float LeadFor(int who, bool serve)
{
    VoicePair P = Pair(who, serve);
    if (P.own) return VarLen(P.pre, gNextVar[WhoIdx(who)][serve]);
    return GRUNT_LEAD / P.pitch;
}
float SfxGruntLead(int who) { return LeadFor(who, false); }
float SfxServeLead(int who) { return LeadFor(who, true); }
float SfxGruntChance(int who) { return Prof(who)->chance; }
// サーブはストロークより声が出やすい（声の多い選手はほぼ毎回、静かな選手は静かなまま）
float SfxServeChance(int who) { return fminf(1.0f, Prof(who)->chance * 1.25f + 0.05f); }

// 遠いほど小さく（カメラからの距離）
static float DistVol(float dist) { return Clamp01(1.3f / (1.0f + dist / 14.0f)); }
static float Jitter(void) { return 1.0f + GetRandomValue(-20, 20) / 1000.0f; }   // 毎回少しだけ音の高さを変える

// 最初に見つかった音の番号（なければ -1）
static int First3(int a, int b, int c) { return gSlot[a].nVar ? a : gSlot[b].nVar ? b : gSlot[c].nVar ? c : -1; }

// voice：この打球の前に声（SfxGruntPre）を出し始めたか。出し始めたときだけ「打球音と声の続き」を鳴らす
//   （途中で声が切れたり、始まりのない声の続きだけが鳴ったりしないように）
//   defensive：体勢が崩れて取った球（飛びついた・ぎりぎり）。ラケットでぎりぎり取って地面に当たる音と控えめな声
static double gVoiceUntil = 0.0;
static bool   gVoiceServe = false;   // 今の声はサーブの声か
static int    gVoiceVar = 0;         // 今の声の録音（打った後の声も同じ録音から）
void SfxHit(float q, float kmh, bool slice, bool voice, bool defensive, float dist)
{
    float vol = (0.5f + 0.5f * Clamp01(kmh / 150.0f)) * DistVol(dist);
    if (voice) {   // 声から続く（同じ人の、同じ録音の後半）
        VoicePair P = Pair(gVoiceWho, gVoiceServe);
        if (P.hit->nVar) { PlaySlotVar(P.hit, gVoiceVar, (0.5f + 0.5f * Clamp01(kmh / 150.0f)) * VoiceDistVol(dist) * VOICE_GAIN * P.vol, P.pitch); return; }
    }
    if (defensive && !slice && gSlot[S_HIT_VOICE_SOFT].nVar && GetTime() >= gVoiceUntil) {
        gVoiceUntil = GetTime() + 0.45;   // 声は一度に 1 人
        PlaySlot(&gSlot[S_HIT_VOICE_SOFT], vol * VOICE_GAIN, 1.0f);   // 声なので大きめに
        return;
    }
    if (q < 0.18f)      Play(S_HIT_FRAME, S_HIT_BAD, vol, Jitter());
    else if (slice)     Play(S_HIT_SLICE, q < 0.5f ? S_HIT_BAD : S_HIT_OK, vol, Jitter());
    else if (q < 0.45f) Play(S_HIT_BAD, S_HIT_OK, vol, Jitter());
    else if (q < 0.75f) Play(S_HIT_OK, S_HIT_GOOD, vol, Jitter());
    else                Play(S_HIT_GOOD, S_HIT_OK, vol, Jitter());
}

// ラケットを地面に叩きつける音（racket_smash がなければ鳴らさない）。k 回目は k 番目の録音（_2, _3 …）を鳴らす
//   大きく聞かせるため、ほかの音より強めに鳴らす（1 を超えると増幅）
#define SMASH_GAIN 2.0f
static void PlaySmash(int k, float vol)
{
    SfxSlot *s = &gSlot[S_RACKET_SMASH];
    if (!gAudioOk || s->nVar == 0 || vol <= 0.001f || gVolume <= 0.0f) return;
    int v = k % s->nVar;
    Sound *snd = &s->s[v][s->next];
    s->next = (s->next + 1) % POOL;
    SetSoundVolume(*snd, Clamp(vol, 0.0f, 6.0f) * gVolume);
    SetSoundPitch(*snd, Jitter());
    PlaySound(*snd);
}
void SfxRacketSmash(float dist, int k)
{
    PlaySmash(k, SMASH_GAIN * Clamp01(1.6f / (1.0f + dist / 14.0f)));   // 遠くても小さくなりにくい
}
// 投げたラケットが地面に落ちて転がる音
void SfxRacketLand(float dist)
{
    if (gSlot[S_RACKET_LAND].nVar) Play(S_RACKET_LAND, -1, 0.9f * DistVol(dist), Jitter());
    else SfxRacketSmash(dist, GetRandomValue(0, 4));
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

// 打つ前の声：打つ SfxGruntLead（サーブは SfxServeLead）秒前に鳴らし始める（溜めの間に声が出て、そのまま打球音と声の続きにつながる）
//   声は一度に 1 人だけ：前の声（打つ前＋続き）が鳴り終わるまでは始めない。始めたら true
static bool StartPre(int who, bool serve, float dist)
{
    if (!gAudioOk || gVolume <= 0.0f) return false;
    VoicePair P = Pair(who, serve);
    if (P.pre->nVar == 0 || P.hit->nVar == 0) return false;
    double now = GetTime();
    if (now < gVoiceUntil) return false;
    int *nv = &gNextVar[WhoIdx(who)][serve];
    int v = *nv % P.pre->nVar;
    gVoiceUntil = now + LeadFor(who, serve) + (P.own ? VarLen(P.hit, v) : 0.45f / P.pitch);
    gVoiceWho = who; gVoiceServe = serve; gVoiceVar = v;
    PlaySlotVar(P.pre, v, 0.9f * VoiceDistVol(dist) * VOICE_GAIN * P.preGain * P.vol, P.pitch);
    *nv = GetRandomValue(0, 99);   // 次の録音を決めておく（鳴らす前に長さが分かるように）
    return true;
}
bool SfxGruntPreBy(int who, float dist) { return StartPre(who, false, dist); }
// 雄叫び：決まったと確信したショットで、打った瞬間から叫ぶ（打球音も入っている）。最後は拍手の中へ消える
bool SfxHasRoar(int who) { return gAudioOk && PSlot(who, V_ROAR)->nVar > 0; }
void SfxHitRoar(int who, float dist)
{
    SfxSlot *s = PSlot(who, V_ROAR);
    if (!gAudioOk || !s->nVar) return;
    int v = GetRandomValue(0, s->nVar - 1);
    PlaySlotVar(s, v, 1.4f * VoiceDistVol(dist) * VOICE_GAIN, 1.0f);   // 雄叫びは大きく
    gVoiceUntil = GetTime() + VarLen(s, v);   // 叫んでいる間は、ほかの声を出さない
}
bool SfxServePreBy(int who, float dist) { return StartPre(who, true, dist); }
bool SfxGruntPre(float dist) { return SfxGruntPreBy(-1, dist); }

// 喜びの声（カモン！バモス！）と、怒りの声
void SfxCelebrate(int who, float dist) { PlayVoice(who, V_CELEBRATE, 1.0f * VoiceDistVol(dist)); gVoiceUntil = GetTime() + 1.0; }
void SfxAngry(int who, float dist)     { PlayVoice(who, V_ANGRY, 1.0f * VoiceDistVol(dist));     gVoiceUntil = GetTime() + 1.0; }

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
    // ざわめきが何種類かあるときは、1 回鳴り終わるたびにランダムに選び直す
    static int cur = 0;
    if (!gAudioOk || gSlot[S_CROWD].nVar == 0) return;
    Sound *s = &gSlot[S_CROWD].s[cur][0];
    if (!on) { if (IsSoundPlaying(*s)) StopSound(*s); return; }
    if (!IsSoundPlaying(*s)) {
        cur = GetRandomValue(0, gSlot[S_CROWD].nVar - 1);
        s = &gSlot[S_CROWD].s[cur][0];
        SetSoundVolume(*s, 0.25f * level * gVolume);
        PlaySound(*s);
    }
    SetSoundVolume(*s, 0.25f * level * gVolume);
}

// ポイントが終わったときの拍手：長いラリーやウィナーほど大きい
void SfxPointEnd(int rallyHits, bool winner)
{
    float k = Clamp01((rallyHits - 2) / 10.0f) + (winner ? 0.35f : 0.0f);
    if (k < 0.6f) Play(S_APPLAUSE, S_APPLAUSE_BIG, 0.35f + 0.6f * k, 1.0f);
    else          Play(S_APPLAUSE_BIG, S_APPLAUSE, 0.5f + 0.4f * Clamp01(k), 1.0f);
}

// 試合に勝った：大歓声（match_win の録音があればそれも重ねる。拍手もいつもより大きく）
void SfxMatchWin(void)
{
    if (!gAudioOk) return;
    PlaySlot(&gSlot[S_MATCH_WIN], 2.0f, 1.0f);
    PlaySlot(&gSlot[S_APPLAUSE_BIG], 2.0f, 1.0f);
    PlaySlot(&gSlot[S_APPLAUSE_BIG], 1.6f, 0.96f);   // 少し高さを変えて重ね、人数を多く聞かせる
    PlaySlot(&gSlot[S_APPLAUSE], 1.5f, 1.04f);
}

// 審判のコール「フォルト」
void SfxCallFault(void) { Play(S_CALL_FAULT, -1, 0.85f, 1.0f); }

// 審判のコール「アウト」（ラリー中の球がアウトになったとき）
// 線審の声（クレー）か、電子判定の声（スピーカー）か
void SfxSetLineJudges(bool human) { gLineJudges = human; }
void SfxCallOut(void)
{
    if (gLineJudges && gSlot[S_CALL_OUT_LINE].nVar) Play(S_CALL_OUT_LINE, -1, 0.95f, 1.0f);   // 線審が叫ぶ
    else Play(S_CALL_OUT, -1, 0.85f, 1.0f);
}

// 拍手を止める（サーブのトスを上げたら静かに：ポイント中は観客は静かにする決まり）
void SfxStopApplause(void)
{
    if (!gAudioOk) return;
    int ids[2] = { S_APPLAUSE, S_APPLAUSE_BIG };
    for (int i = 0; i < 2; i++)
        for (int v = 0; v < gSlot[ids[i]].nVar; v++)
            for (int k = 0; k < POOL; k++) if (IsSoundPlaying(gSlot[ids[i]].s[v][k])) StopSound(gSlot[ids[i]].s[v][k]);
}

// ---- 少し後で鳴らす音（フットワークの「キュッ」の連続、主審のコール、観客の反応） ----
typedef struct { double t; int id; float vol, pitch; } QueuedSfx;
#define QMAX 16
static QueuedSfx gQ[QMAX];
static int gQn = 0;
static void Queue(double t, int id, float vol, float pitch)
{
    if (gQn < QMAX && gSlot[id].nVar) gQ[gQn++] = (QueuedSfx){ t, id, vol, pitch };
}
// 録音の長さ（秒）。種類がいくつかあるときは最初のもの
static float SlotLen(int id)
{
    if (!gSlot[id].nVar) return 0.0f;
    Sound s = gSlot[id].s[0][0];
    return s.stream.sampleRate ? (float)s.frameCount / (float)s.stream.sampleRate : 0.0f;
}
// フットワーク：球に寄って距離を詰めるとき、「キュッ」を 2〜3 回続けて（ハードだけ）
void SfxFootwork(int n, float dist)
{
    if (!gAudioOk || gSurface != SURF_HARD || gSlot[S_SQUEAK].nVar == 0) return;
    double t = GetTime();
    for (int i = 0; i < n; i++) {
        Queue(t, S_SQUEAK, (0.35f + 0.15f * (GetRandomValue(0, 100) / 100.0f)) * DistVol(dist), Jitter());
        t += 0.09 + GetRandomValue(0, 40) / 1000.0;   // 0.09〜0.13 秒おき
    }
}

// 観客のブーイング（ラケットを叩きつけた・投げた・球をかっ飛ばした・審判に詰め寄った）。delay 秒後に
void SfxBoo(float level, float delay)
{
    if (!gAudioOk) return;
    Queue(GetTime() + delay, S_BOO, Clamp(level, 0.0f, 1.0f) * 0.9f, 1.0f + GetRandomValue(-30, 30) / 1000.0f);
}
// 観客の「おお〜」（惜しいアウト、長いラリーの終わりのミス）
void SfxOoh(float level)
{
    if (!gAudioOk) return;
    Queue(GetTime() + 0.12, S_OOH, Clamp(level, 0.0f, 1.0f) * 0.8f, 1.0f + GetRandomValue(-30, 30) / 1000.0f);
}

// 主審のスコアコール。kind：0 ポイント、1 ゲーム、2 セット。server：そのゲームのサーバー、winner：取った方
//   ポイントはサーバーの点を先に言う（「フィフティーン、ラブ」）。同点は「フィフティーン、オール」「デュース」
//   （call_all / call_deuce があるときだけ）。タイブレークは言わない
void SfxScoreCall(const Score *sc, int server, int winner, int kind, float delay)
{
    if (!gAudioOk) return;
    double t = GetTime() + delay;
    if (sc->over) { Queue(t, winner == server ? S_CALL_GSM_S : S_CALL_GSM_R, 1.0f, 1.0f); return; }   // ゲーム・セット・アンド・マッチ
    const float vol = 1.0f;
    if (kind == 2) { Queue(t, winner == server ? S_CALL_SET_S : S_CALL_SET_R, vol, 1.0f); return; }
    if (kind == 1) { Queue(t, winner == server ? S_CALL_GAME_S : S_CALL_GAME_R, vol, 1.0f); return; }
    if (sc->tiebreak) return;
    int sp = sc->points[server], rp = sc->points[1 - server];
    static const int NUM[4] = { S_CALL_LOVE, S_CALL_15, S_CALL_30, S_CALL_40 };
    if (sp <= 3 && rp <= 3 && !(sp == 3 && rp == 3) && gCallP[sp][rp].nVar) {   // 言い回しの録音があればそれを
        if (gQn < QMAX) gQ[gQn++] = (QueuedSfx){ t, -1 - (sp * 4 + rp), 1.0f, 1.0f };   // id がマイナス：言い回しのコール
        return;
    }
    if (sp >= 3 && rp >= 3) {
        if (sp > rp) Queue(t, S_CALL_ADV_S, vol, 1.0f);
        else if (rp > sp) Queue(t, S_CALL_ADV_R, vol, 1.0f);
        else Queue(t, S_CALL_DEUCE, vol, 1.0f);
        return;
    }
    if (sp == rp) {   // 「フィフティーン、オール」
        int a = NUM[sp];
        if (!gSlot[a].nVar || !gSlot[S_CALL_ALL].nVar) return;
        Queue(t, a, vol, 1.0f);
        Queue(t + SlotLen(a) + 0.08, S_CALL_ALL, vol, 1.0f);
        return;
    }
    int a = NUM[sp > 3 ? 3 : sp], b = NUM[rp > 3 ? 3 : rp];
    if (!gSlot[a].nVar || !gSlot[b].nVar) return;   // 片方だけ言うとおかしいので
    Queue(t, a, vol, 1.0f);
    Queue(t + SlotLen(a) + 0.08, b, vol, 1.0f);
}

// 毎フレーム呼ぶ：時間が来た音を鳴らす
void SfxUpdate(void)
{
    double now = GetTime();
    for (int i = 0; i < gQn; ) {
        if (gQ[i].t <= now) {
            QueuedSfx q = gQ[i]; gQ[i] = gQ[--gQn];
            if (q.id < 0) { int k = -1 - q.id; PlaySlotVar(&gCallP[k / 4][k % 4], 0, q.vol, q.pitch); }   // スコアの言い回し
            else Play(q.id, -1, q.vol, q.pitch);
        }
        else i++;
    }
}
