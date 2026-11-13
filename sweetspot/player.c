// SWEET SPOT — プレイヤー（自分）の状態
#include "game.h"

Player NewPlayer(Vector3 pos)
{
    Player p = { 0 };
    p.pos = pos;
    p.anim.swingT = -1.0f;
    p.aimDepth = -9.0f;
    p.chargingShot = -1;
    p.chargingServe = -1;
    p.lastType = SHOT_TOP;
    p.lastServeType = SERVE_FLAT;
    p.sv = SV_READY;
    return p;
}
