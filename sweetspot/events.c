// SWEET SPOT — テニスの外のハプニング：コートに降りてくるハト（ボールキッズが追い払う）
//   ポイントの間にたまに起きる。ハトが降りてきて歩き回り、ボールキッズが走って追い払うと飛んでいく。観客は笑って拍手
#include "game.h"

Pigeon gPigeon = { 0 };

static Vector3 KidHome(void) { return (Vector3){ gUmpSide * (NET_POST_X + 0.3f), 0.0f, 0.9f }; }   // 審判台の側のボールキッズ

void PigeonStart(void)
{
    Pigeon *p = &gPigeon;
    *p = (Pigeon){ 0 };
    p->on = true;
    // 自分の側のコート（カメラから見える所）、ネットとサービスラインの間あたりに降りる
    p->target = (Vector3){ RandRange(-3.2f, 3.2f), 0.0f, RandRange(4.5f, 8.0f) };   // 自分とネットの間（カメラから見やすい）
    p->pos = Vector3Add(p->target, (Vector3){ -gUmpSide * 6.0f, 7.0f, -3.0f });   // 審判台と反対の上の方から
    p->yaw = RandRange(0.0f, 2.0f * PI);
    p->kid = KidHome();
}

// 1 フレーム進める。戻り値：1 = 降りた、2 = 飛んでいった（観客の反応に使う）、0 = なし
int PigeonUpdate(float dt)
{
    Pigeon *p = &gPigeon;
    if (!p->on) return 0;
    int ev = 0;
    p->t += dt; p->wing += dt * (p->phase == 1 ? 0.0f : 18.0f);
    if (p->phase == 0) {          // 降りてくる：弧を描いて着地
        float u = Clamp01(p->t / 1.4f);
        Vector3 from = Vector3Add(p->target, (Vector3){ -gUmpSide * 6.0f, 7.0f, -3.0f });
        p->pos = Vector3Lerp(from, p->target, 1.0f - (1.0f - u) * (1.0f - u));
        p->yaw = atan2f(p->target.x - from.x, p->target.z - from.z);
        if (u >= 1.0f) { p->phase = 1; p->t = 0.0f; p->pos = p->target; ev = 1; }
    } else if (p->phase == 1) {   // 歩き回ってつつく。少しするとボールキッズが走ってくる
        p->peck = fmaxf(0.0f, sinf(p->t * 5.0f)) * (fmodf(p->t, 2.2f) < 1.1f ? 1.0f : 0.0f);
        if (fmodf(p->t, 2.2f) >= 1.1f) {   // ちょこちょこ歩く
            p->pos.x += sinf(p->yaw) * 0.35f * dt; p->pos.z += cosf(p->yaw) * 0.35f * dt;
            p->hop = fabsf(sinf(p->t * 14.0f)) * 0.012f;
        } else p->hop = 0.0f;
        if (fmodf(p->t, 2.2f) < dt) p->yaw += RandRange(-1.4f, 1.4f);
        if (p->t > 1.8f && p->kidPhase == 0) p->kidPhase = 1;
        if (p->kidPhase == 1) {
            Vector3 d = Vector3Subtract(p->pos, p->kid); d.y = 0.0f;
            float l = Vector3Length(d);
            if (l > 0.05f) { p->kid = Vector3Add(p->kid, Vector3Scale(d, fminf(3.4f * dt, l) / l)); p->kidYaw = atan2f(d.x, d.z); }
            p->kidRun += dt * 9.0f;
            if (l < 1.7f || p->t > 9.0f) {   // 近づかれたら飛んでいく（ボールキッズから離れる向きへ）
                p->phase = 2; p->t = 0.0f; p->kidPhase = 2;
                Vector3 away = l > 0.05f ? Vector3Scale(d, 1.0f / l) : (Vector3){ 0, 0, -1 };
                p->vel = (Vector3){ away.x * 4.0f, 4.5f, away.z * 4.0f - 2.0f };
                p->yaw = atan2f(p->vel.x, p->vel.z);
                ev = 2;
            }
        }
    } else {                      // 飛んでいく。ボールキッズは元の場所へ戻る
        p->vel.y += 1.5f * dt;
        p->pos = Vector3Add(p->pos, Vector3Scale(p->vel, dt));
    }
    if (p->kidPhase == 2) {
        Vector3 d = Vector3Subtract(KidHome(), p->kid); d.y = 0.0f;
        float l = Vector3Length(d);
        if (l > 0.05f) { p->kid = Vector3Add(p->kid, Vector3Scale(d, fminf(2.6f * dt, l) / l)); p->kidYaw = atan2f(d.x, d.z); p->kidRun += dt * 7.0f; }
        else p->kidPhase = 3;
    }
    if (p->phase == 2 && p->kidPhase == 3 && p->t > 1.0f) p->on = false;   // ボールキッズが戻ったら終わり
    return ev;
}

// ボールキッズが走って出ている間は、いつもの場所にしゃがむボールキッズを描かない
bool PigeonKidOut(void) { return gPigeon.on && gPigeon.kidPhase >= 1 && gPigeon.kidPhase <= 2; }
