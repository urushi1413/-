// SWEET SPOT — 効果音（すべてプログラムで合成する。音声ファイルは使わない）
//   打球音は当たりの質で変わる：芯は乾いた「ポン」、外すと「カシャ」、フレームは「カン」
//   バウンドの音はコートで変わる：ハードは高く硬い、クレーはにぶく砂っぽい、芝は柔らかい
#include "game.h"

#define SR        44100
#define POOL      3          // 同じ音が重なっても鳴るように、同じ音を何個か用意する
#define HIT_LV    5          // 打球音の段階（フレーム → 芯）

typedef struct { Sound s[POOL]; int next; bool ok; } SfxSlot;

static bool    gAudioOk = false;
static float   gVolume  = 0.8f;
static SfxSlot sHit[HIT_LV], sHitSlice[HIT_LV], sServe;
static SfxSlot sBounce[SURF_COUNT], sNet, sWhiff, sSqueak, sSlide, sCrowd, sApplause, sApplauseBig;
static unsigned int rngState = 12345u;

static float Noise(void)
{
    rngState = rngState * 1664525u + 1013904223u;
    return ((rngState >> 8) & 0xFFFF) / 32768.0f - 1.0f;
}

// 音のもと（float の配列）を作って、最大音量をそろえてから Sound にする
static void MakeSlot(SfxSlot *slot, const float *buf, int n, float peak)
{
    float m = 1e-6f;
    for (int i = 0; i < n; i++) m = fmaxf(m, fabsf(buf[i]));
    short *pcm = (short *)MemAlloc(n * sizeof(short));
    for (int i = 0; i < n; i++) pcm[i] = (short)(Clamp(buf[i] / m * peak, -1.0f, 1.0f) * 32767.0f);
    Wave w = { .frameCount = (unsigned int)n, .sampleRate = SR, .sampleSize = 16, .channels = 1, .data = pcm };
    for (int k = 0; k < POOL; k++) slot->s[k] = LoadSoundFromWave(w);
    MemFree(pcm);
    slot->next = 0;
    slot->ok = true;
}

static void PlaySlot(SfxSlot *slot, float vol, float pitch)
{
    if (!gAudioOk || !slot->ok || vol <= 0.001f) return;
    Sound *s = &slot->s[slot->next];
    slot->next = (slot->next + 1) % POOL;
    SetSoundVolume(*s, Clamp(vol, 0.0f, 1.0f) * gVolume);
    SetSoundPitch(*s, pitch);
    PlaySound(*s);
}

static float Env(float t, float tau) { return expf(-t / tau); }

// 2 段の低域通過（高い「シャー」を削る）。fc：おおよその上限の周波数
typedef struct { float a, y1, y2; } Lp2;
static Lp2 MakeLp(float fc) { Lp2 f = { 1.0f - expf(-2.0f * PI * fc / SR), 0.0f, 0.0f }; return f; }
static float LpRun(Lp2 *f, float x) { f->y1 += f->a * (x - f->y1); f->y2 += f->a * (f->y1 - f->y2); return f->y2; }
// 帯域通過：lo〜hi の音だけ残す
typedef struct { Lp2 hi, lo; } Bp;
static Bp MakeBp(float lo, float hi) { Bp b = { MakeLp(hi), MakeLp(lo) }; return b; }
static float BpRun(Bp *b, float x) { float h = LpRun(&b->hi, x); return h - LpRun(&b->lo, h); }

// ---- 打球音 ----
//   q：当たりの質（0〜1）。slice：こする音を先に足す
static void SynthHit(float *b, int n, float q, bool slice)
{
    Bp brush = MakeBp(1500.0f, 6000.0f), crack = MakeBp(1200.0f, 4500.0f);
    Lp2 thud = MakeLp(2500.0f);
    float pre = slice ? 0.025f : 0.0f;          // スライスは面でこする「シュッ」が先に来る
    float sweet = Clamp01((q - 0.2f) / 0.8f);  // 芯の度合い
    float frame = Clamp01((0.25f - q) / 0.25f); // フレームの度合い
    for (int i = 0; i < n; i++) {
        float t = (float)i / SR;
        float x = 0.0f;
        if (slice && t < pre + 0.03f) {
            float e = (t < pre) ? t / pre : expf(-(t - pre) / 0.010f);
            x += 0.6f * BpRun(&brush, Noise()) * e;
        }
        float u = t - pre;
        if (u >= 0.0f) {
            float nz = Noise();
            float lp = LpRun(&thud, nz), cr = BpRun(&crack, nz);
            // 芯：ガットが鳴る乾いた「ポン」（約 1kHz）と、ボールのつぶれる低い音
            float pock = 0.9f * sinf(2 * PI * 1050.0f * u) * Env(u, 0.012f)
                       + 0.5f * sinf(2 * PI * 620.0f * u) * Env(u, 0.020f)
                       + 0.3f * sinf(2 * PI * 160.0f * u) * Env(u, 0.030f)
                       + 0.35f * lp * Env(u, 0.004f);
            // 外した：にぶい「カシャ」（雑音が多く、響きが濁る）
            float clack = 0.6f * sinf(2 * PI * 1500.0f * u) * Env(u, 0.008f)
                        + 1.2f * cr * Env(u, 0.015f)
                        + 0.3f * sinf(2 * PI * 2400.0f * u) * Env(u, 0.020f);
            // フレーム：金属っぽい「カン」（倍音がそろっていない音が長く残る）
            float ring = 0.5f * sinf(2 * PI * 1730.0f * u) * Env(u, 0.07f)
                       + 0.4f * sinf(2 * PI * 2890.0f * u) * Env(u, 0.06f)
                       + 0.3f * sinf(2 * PI * 4210.0f * u) * Env(u, 0.05f)
                       + 0.6f * cr * Env(u, 0.003f);
            x += sweet * pock + (1.0f - sweet) * (1.0f - frame) * clack + frame * ring;
        }
        b[i] = x;
    }
}

static void SynthBounce(float *b, int n, SurfaceType s)
{
    Lp2 grit = MakeLp(1800.0f), soft = MakeLp(700.0f);
    Bp click = MakeBp(1500.0f, 5000.0f);
    for (int i = 0; i < n; i++) {
        float t = (float)i / SR, nz = Noise(), x;
        switch (s) {
            case SURF_CLAY:   // にぶく、砂をこする音が少し残る
                x = 0.8f * sinf(2 * PI * 140.0f * t) * Env(t, 0.020f) + 2.0f * LpRun(&grit, nz) * Env(t, 0.030f);
                break;
            case SURF_GRASS:  // 柔らかく短い
                x = 0.7f * sinf(2 * PI * 110.0f * t) * Env(t, 0.015f) + 2.0f * LpRun(&soft, nz) * Env(t, 0.010f);
                break;
            default:          // ハード：高く硬い
                x = 0.8f * sinf(2 * PI * 230.0f * t) * Env(t, 0.018f) + 0.8f * BpRun(&click, nz) * Env(t, 0.003f)
                  + 0.3f * sinf(2 * PI * 900.0f * t) * Env(t, 0.006f);
                break;
        }
        b[i] = x;
    }
}

static void SynthNet(float *b, int n)
{
    Lp2 body = MakeLp(500.0f);
    Bp rattle = MakeBp(400.0f, 1800.0f);
    for (int i = 0; i < n; i++) {
        float t = (float)i / SR, nz = Noise();
        b[i] = sinf(2 * PI * 85.0f * t) * Env(t, 0.06f) + 3.0f * LpRun(&body, nz) * Env(t, 0.08f)
             + 1.0f * BpRun(&rattle, nz) * (0.5f + 0.5f * sinf(2 * PI * 30.0f * t)) * Env(t, 0.10f);   // 網がゆれる音
    }
}

// 空振り：風を切る「ブン」
static void SynthWhiff(float *b, int n)
{
    Lp2 f = MakeLp(2500.0f);
    for (int i = 0; i < n; i++) {
        float t = (float)i / n;
        f.a = 1.0f - expf(-2.0f * PI * (500.0f + 2500.0f * (1.0f - t)) / SR);   // 高い音から低い音へ
        b[i] = LpRun(&f, Noise()) * sinf(PI * t);
    }
}

// 靴がこすれる「キュッ」（ハード）
static void SynthSqueak(float *b, int n)
{
    float ph = 0.0f;
    for (int i = 0; i < n; i++) {
        float t = (float)i / n;
        float f = 2200.0f + 900.0f * t + 120.0f * sinf(2 * PI * 35.0f * t);
        ph += 2 * PI * f / SR;
        b[i] = sinf(ph) * sinf(PI * t);
    }
}

// 滑る音（クレーの砂、芝）
static void SynthSlide(float *b, int n)
{
    Bp f = MakeBp(300.0f, 3000.0f);
    for (int i = 0; i < n; i++) {
        float t = (float)i / n;
        b[i] = BpRun(&f, Noise()) * (t < 0.1f ? t / 0.1f : 1.0f - (t - 0.1f) / 0.9f);
    }
}

// 観客のざわめき（低い雑音を、ゆっくり大きさを変えて）
static void SynthCrowd(float *b, int n)
{
    Lp2 low = MakeLp(350.0f);
    Bp voice = MakeBp(250.0f, 1100.0f);   // 話し声の高さ
    for (int i = 0; i < n; i++) {
        float t = (float)i / SR;
        float nz = Noise();
        float mod = 0.7f + 0.3f * sinf(2 * PI * 0.35f * t) * sinf(2 * PI * 0.13f * t + 1.0f);
        float edge = fminf(fminf(t / 0.3f, ((float)n / SR - t) / 0.3f), 1.0f);   // つなぎ目を目立たせない
        b[i] = (LpRun(&low, nz) * 2.0f + BpRun(&voice, nz) * 1.2f) * mod * edge;
    }
}

// 拍手：短い「パン」をたくさん、ばらばらのタイミングで
static void SynthApplause(float *b, int n, float rate)
{
    for (int i = 0; i < n; i++) b[i] = 0.0f;
    float dur = (float)n / SR;
    for (float t = 0.0f; t < dur; ) {
        float env = sinf(PI * fminf(t / dur * 1.15f, 1.0f));
        float r = rate * (0.2f + env);
        t += -logf(fmaxf((Noise() + 1.0f) * 0.5f, 1e-4f)) / r;
        int s0 = (int)(t * SR);
        float amp = (0.5f + 0.5f * (Noise() + 1.0f) * 0.5f) * env;
        float c = 900.0f + 1500.0f * (Noise() + 1.0f) * 0.5f;   // 手の大きさで音の高さが違う
        Bp f = MakeBp(c * 0.5f, c * 2.2f);
        for (int k = 0; k < 600 && s0 + k < n; k++)
            b[s0 + k] += BpRun(&f, Noise()) * amp * expf(-(float)k / (SR * 0.006f));
    }
}

void SfxInit(void)
{
    InitAudioDevice();
    gAudioOk = IsAudioDeviceReady();
    if (!gAudioOk) return;
    int maxN = SR * 6;
    float *b = (float *)MemAlloc(maxN * sizeof(float));
    const float qs[HIT_LV] = { 0.05f, 0.30f, 0.55f, 0.78f, 0.97f };
    for (int i = 0; i < HIT_LV; i++) {
        int n = (int)(0.16f * SR);
        SynthHit(b, n, qs[i], false); MakeSlot(&sHit[i], b, n, 0.9f);
        SynthHit(b, n, qs[i], true);  MakeSlot(&sHitSlice[i], b, n, 0.9f);
    }
    { int n = (int)(0.16f * SR); SynthHit(b, n, 1.0f, false); MakeSlot(&sServe, b, n, 0.95f); }
    for (int s = 0; s < SURF_COUNT; s++) { int n = (int)(0.12f * SR); SynthBounce(b, n, (SurfaceType)s); MakeSlot(&sBounce[s], b, n, 0.8f); }
    { int n = (int)(0.30f * SR); SynthNet(b, n); MakeSlot(&sNet, b, n, 0.8f); }
    { int n = (int)(0.20f * SR); SynthWhiff(b, n); MakeSlot(&sWhiff, b, n, 0.6f); }
    { int n = (int)(0.08f * SR); SynthSqueak(b, n); MakeSlot(&sSqueak, b, n, 0.35f); }
    { int n = (int)(0.40f * SR); SynthSlide(b, n); MakeSlot(&sSlide, b, n, 0.5f); }
    { int n = (int)(5.0f * SR); SynthCrowd(b, n); MakeSlot(&sCrowd, b, n, 0.5f); }
    { int n = (int)(1.8f * SR); SynthApplause(b, n, 120.0f); MakeSlot(&sApplause, b, n, 0.7f); }
    { int n = (int)(3.5f * SR); SynthApplause(b, n, 400.0f); MakeSlot(&sApplauseBig, b, n, 0.8f); }
    MemFree(b);
}

void SfxClose(void)
{
    if (!gAudioOk) return;
    SfxSlot *all[] = { &sServe, &sNet, &sWhiff, &sSqueak, &sSlide, &sCrowd, &sApplause, &sApplauseBig };
    for (unsigned i = 0; i < sizeof(all) / sizeof(all[0]); i++) for (int k = 0; k < POOL; k++) UnloadSound(all[i]->s[k]);
    for (int i = 0; i < HIT_LV; i++) for (int k = 0; k < POOL; k++) { UnloadSound(sHit[i].s[k]); UnloadSound(sHitSlice[i].s[k]); }
    for (int s = 0; s < SURF_COUNT; s++) for (int k = 0; k < POOL; k++) UnloadSound(sBounce[s].s[k]);
    CloseAudioDevice();
    gAudioOk = false;
}

void  SfxSetVolume(float v) { gVolume = Clamp01(v); }
float SfxGetVolume(void)    { return gVolume; }

// 遠いほど小さく（カメラからの距離）
static float DistVol(float dist) { return 1.0f / (1.0f + dist / 14.0f) * 1.3f; }

void SfxHit(float q, float kmh, bool slice, float dist)
{
    int lv = q < 0.18f ? 0 : q < 0.42f ? 1 : q < 0.66f ? 2 : q < 0.88f ? 3 : 4;
    float vol = (0.45f + 0.55f * Clamp01(kmh / 150.0f)) * DistVol(dist);
    float pitch = 0.94f + 0.12f * Clamp01(kmh / 180.0f) + 0.02f * Noise();
    PlaySlot(slice ? &sHitSlice[lv] : &sHit[lv], vol, pitch);
}

void SfxServe(float q, float kmh, float dist)
{
    if (q > 0.7f) PlaySlot(&sServe, (0.6f + 0.4f * Clamp01(kmh / 200.0f)) * DistVol(dist), 0.92f + 0.1f * Clamp01(kmh / 220.0f));
    else          SfxHit(q, kmh, false, dist);
}

void SfxWhiff(void) { PlaySlot(&sWhiff, 0.7f, 0.9f + 0.2f * (Noise() + 1.0f) * 0.5f); }

void SfxBounce(float speed, float dist)
{
    float vol = Clamp01(0.25f + speed / 30.0f) * DistVol(dist);
    PlaySlot(&sBounce[gSurface], vol, 0.95f + 0.05f * Noise());
}

void SfxNet(float speed, float dist) { PlaySlot(&sNet, Clamp01(0.3f + speed / 30.0f) * DistVol(dist), 1.0f); }

// 滑り込み・急な切り返し：ハードは「キュッ」、クレーと芝は滑る音
void SfxSlide(float dist)
{
    if (gSurface == SURF_HARD) PlaySlot(&sSqueak, 0.8f * DistVol(dist), 0.9f + 0.2f * (Noise() + 1.0f) * 0.5f);
    else                       PlaySlot(&sSlide, (gSurface == SURF_CLAY ? 0.9f : 0.5f) * DistVol(dist), gSurface == SURF_CLAY ? 1.0f : 0.7f);
}

// 観客のざわめき：on の間は鳴らし続ける（毎フレーム呼ぶ）
void SfxCrowd(bool on, float level)
{
    if (!gAudioOk || !sCrowd.ok) return;
    Sound *s = &sCrowd.s[0];
    if (!on) { if (IsSoundPlaying(*s)) StopSound(*s); return; }
    SetSoundVolume(*s, 0.12f * level * gVolume);
    if (!IsSoundPlaying(*s)) PlaySound(*s);
}

// ポイントが終わったときの拍手：長いラリーやウィナーほど大きい
void SfxPointEnd(int rallyHits, bool winner)
{
    float k = Clamp01((rallyHits - 2) / 10.0f) + (winner ? 0.35f : 0.0f);
    if (k < 0.15f) { PlaySlot(&sApplause, 0.35f, 1.0f); return; }
    if (k < 0.6f)  PlaySlot(&sApplause, 0.45f + 0.5f * k, 1.0f);
    else           PlaySlot(&sApplauseBig, 0.5f + 0.4f * Clamp01(k), 1.0f);
}
