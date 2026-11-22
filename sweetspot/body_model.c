// SWEET SPOT — 選手の体のモデル（Blender で作った models/player.glb）を、ゲームの関節の位置で曲げて描く（試作）
//
//   動きは今までどおりコード（render.c の DrawFigureBody）が関節の位置を計算する。ここは、その関節の位置に
//   モデルの骨を合わせて、頂点を骨の重みで動かす（CPU でのスキニング）だけ。だからモデルを差し替えても、打ち方・走り方は変わらない。
//   ・骨は名前で探す（hips spine chest neck head upperarm.R forearm.R hand.R thigh.R shin.R foot.R、.L も）。
//     骨のしっぽの位置は、子の骨（head_end hand_end.R foot_end.R など）の頭の位置で知る
//   ・色はマテリアルの名前で決める（skin shirt shorts socks shoes sole hair）。ほかの名前は Blender の色のまま
//   ・陰影・アニメ調の輪郭線は、ほかの図形と同じ描き方（LitVertexExt）を通す
#include "game.h"
#include "rlgl.h"
#include <string.h>
#include <stdlib.h>

void LitVertexExt(Vector3 p, Vector3 n, Color c);   // render.c

#define PM_PATH "models/player.glb"
#define PM_MAX_BONES 64
#define PM_MAX_MATS  32

typedef struct { float m[3][3]; Vector3 t; } Xf;   // 3x3（回転・伸び）と平行移動

static Model gPM;
static bool  gPMTried = false, gPMOk = false;
static int   gPMRole[PM_MAX_MATS];                 // マテリアルごとの色の役（BodyRole）。-1 は Blender の色のまま
static int   gPMBoneJ[PM_MAX_BONES][2];            // 骨ごとに、頭としっぽの関節（BodyJoint）。-1 は使わない骨（親に付いて動く）
static int   gPMBoneRef[PM_MAX_BONES];             // 骨の横向きの基準（0 腰の線 1 肩の線 2 顔の右 3・4 右足・左足のつま先の右）
static int   gPMBoneThick[PM_MAX_BONES];           // 太さの倍率の種類（0 なし 1 腕 2 脚 3 胴）
static Xf    gPMBindInv[PM_MAX_BONES];             // 元の姿勢の骨の向き（逆）
static float gPMBindLen[PM_MAX_BONES];
static Vector3 *gPMPos, *gPMNrm;                   // 曲げた後の頂点（作業用）
static int   gPMMaxVerts;

// ---- glb の JSON から、マテリアルの名前を順に取り出す（raylib の Material には名前がないので）----
static const char *JsonSkipStr(const char *p) { p++; while (*p && *p != '"') { if (*p == '\\' && p[1]) p++; p++; } return *p ? p + 1 : p; }
static const char *JsonSkipVal(const char *p)
{
    while (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t') p++;
    if (*p == '"') return JsonSkipStr(p);
    if (*p == '{' || *p == '[') {
        int d = 0;
        while (*p) {
            if (*p == '"') { p = JsonSkipStr(p); continue; }
            if (*p == '{' || *p == '[') d++;
            else if (*p == '}' || *p == ']') { d--; if (d == 0) return p + 1; }
            p++;
        }
        return p;
    }
    while (*p && *p != ',' && *p != '}' && *p != ']') p++;
    return p;
}
static int GlbMaterialNames(const char *json, char names[][32], int maxN)
{
    // 一番外の { の中で "materials" の配列を探す
    const char *p = strchr(json, '{'); if (!p) return 0; p++;
    while (*p) {
        while (*p && *p != '"' && *p != '}') p++;
        if (*p != '"') return 0;
        const char *k0 = p + 1; p = JsonSkipStr(p); int klen = (int)(p - 1 - k0);
        while (*p && *p != ':') p++;
        if (!*p) return 0;
        p++;
        while (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t') p++;
        if (klen == 9 && !strncmp(k0, "materials", 9) && *p == '[') {
            int n = 0; p++;
            while (*p && *p != ']' && n < maxN) {
                while (*p && *p != '{' && *p != ']') p++;
                if (*p != '{') break;
                const char *end = JsonSkipVal(p);
                names[n][0] = 0;
                const char *q = p + 1;   // オブジェクトの中のキーを順に見る（深さ 1 だけ）
                while (q < end) {
                    while (q < end && *q != '"' && *q != '}') q++;
                    if (q >= end || *q != '"') break;
                    const char *kk = q + 1; q = JsonSkipStr(q); int kl = (int)(q - 1 - kk);
                    while (q < end && *q != ':') q++;
                    q++;
                    while (*q == ' ' || *q == '\n') q++;
                    if (kl == 4 && !strncmp(kk, "name", 4) && *q == '"') {
                        const char *v0 = q + 1, *v1 = JsonSkipStr(q) - 1; int vl = (int)(v1 - v0); if (vl > 31) vl = 31;
                        memcpy(names[n], v0, vl); names[n][vl] = 0;
                    }
                    q = JsonSkipVal(q);
                    while (q < end && (*q == ',' || *q == ' ' || *q == '\n')) q++;
                }
                n++; p = end;
            }
            return n;
        }
        p = JsonSkipVal(p);
    }
    return 0;
}

// ---- 骨の名前 → ゲームの関節 ----
typedef struct { const char *name; int head, tail, ref, thick; } BoneMap;
static const BoneMap BMAP[] = {
    { "hips",  BJ_PELVIS, BJ_WAIST,   0, 3 }, { "spine", BJ_WAIST, BJ_CHEST, 0, 3 },
    { "chest", BJ_CHEST,  BJ_NECK,    1, 3 }, { "neck",  BJ_NECK,  BJ_HEAD,  1, 0 },
    { "head",  BJ_HEAD,   BJ_HEADTOP, 2, 0 },
    { "upperarm.R", BJ_SH_R, BJ_EL_R, 1, 1 }, { "forearm.R", BJ_EL_R, BJ_HD_R, 1, 1 }, { "hand.R", BJ_HD_R, BJ_TIP_R, 1, 0 },
    { "upperarm.L", BJ_SH_L, BJ_EL_L, 1, 1 }, { "forearm.L", BJ_EL_L, BJ_HD_L, 1, 1 }, { "hand.L", BJ_HD_L, BJ_TIP_L, 1, 0 },
    { "thigh.R", BJ_HIP_R, BJ_KN_R, 0, 2 }, { "shin.R", BJ_KN_R, BJ_AN_R, 0, 2 }, { "foot.R", BJ_AN_R, BJ_TOE_R, 3, 0 },
    { "thigh.L", BJ_HIP_L, BJ_KN_L, 0, 2 }, { "shin.L", BJ_KN_L, BJ_AN_L, 0, 2 }, { "foot.L", BJ_AN_L, BJ_TOE_L, 4, 0 },
};
static const char *TAILBONE[][2] = {   // しっぽの位置を知るための子の骨
    { "hips", "spine" }, { "spine", "chest" }, { "chest", "neck" }, { "neck", "head" }, { "head", "head_end" },
    { "upperarm.R", "forearm.R" }, { "forearm.R", "hand.R" }, { "hand.R", "hand_end.R" },
    { "upperarm.L", "forearm.L" }, { "forearm.L", "hand.L" }, { "hand.L", "hand_end.L" },
    { "thigh.R", "shin.R" }, { "shin.R", "foot.R" }, { "foot.R", "foot_end.R" },
    { "thigh.L", "shin.L" }, { "shin.L", "foot.L" }, { "foot.L", "foot_end.L" },
};
static int BoneIndex(const char *name)
{
    for (int b = 0; b < gPM.boneCount; b++) if (!strcmp(gPM.bones[b].name, name)) return b;
    return -1;
}

// 骨の向き：Y = 頭からしっぽ、X = 基準の横向き（骨に直角にしたもの）、Z = X×Y
static void Frame(Vector3 h, Vector3 t, Vector3 side, Vector3 alt, float m[3][3], float *len)
{
    Vector3 y = Vector3Subtract(t, h); *len = Vector3Length(y);
    y = *len > 1e-6f ? Vector3Scale(y, 1.0f / *len) : (Vector3){ 0, 1, 0 };
    Vector3 x = Vector3Subtract(side, Vector3Scale(y, Vector3DotProduct(side, y)));
    float xl = Vector3Length(x);
    if (xl < 0.35f) {   // 骨が横の基準とほぼ平行（腕を横に伸ばした）：前の向きから作る（向きは基準にそろえる）
        Vector3 x2 = Vector3CrossProduct(y, alt);
        if (Vector3DotProduct(x2, side) < 0.0f) x2 = Vector3Negate(x2);
        float l2 = Vector3Length(x2);
        if (l2 > 1e-4f) { x2 = Vector3Scale(x2, 1.0f / l2); float k = xl / 0.35f; x = Vector3Add(Vector3Scale(x2, 1.0f - k), Vector3Scale(xl > 1e-6f ? Vector3Scale(x, 1.0f / xl) : x2, k)); }
        else x = x2;
        x = Vector3Subtract(x, Vector3Scale(y, Vector3DotProduct(x, y)));
        xl = Vector3Length(x);
    }
    x = xl > 1e-6f ? Vector3Scale(x, 1.0f / xl) : (Vector3){ 1, 0, 0 };
    Vector3 z = Vector3CrossProduct(x, y);
    m[0][0] = x.x; m[1][0] = x.y; m[2][0] = x.z;   // 列が軸
    m[0][1] = y.x; m[1][1] = y.y; m[2][1] = y.z;
    m[0][2] = z.x; m[1][2] = z.y; m[2][2] = z.z;
}
static Vector3 XfMul(const Xf *a, Vector3 v)
{
    return (Vector3){ a->m[0][0] * v.x + a->m[0][1] * v.y + a->m[0][2] * v.z + a->t.x,
                      a->m[1][0] * v.x + a->m[1][1] * v.y + a->m[1][2] * v.z + a->t.y,
                      a->m[2][0] * v.x + a->m[2][1] * v.y + a->m[2][2] * v.z + a->t.z };
}
static Vector3 XfDir(const Xf *a, Vector3 v)
{
    return (Vector3){ a->m[0][0] * v.x + a->m[0][1] * v.y + a->m[0][2] * v.z,
                      a->m[1][0] * v.x + a->m[1][1] * v.y + a->m[1][2] * v.z,
                      a->m[2][0] * v.x + a->m[2][1] * v.y + a->m[2][2] * v.z };
}
static Xf XfCompose(const Xf *a, const Xf *b)   // a・b
{
    Xf r;
    for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++)
        r.m[i][j] = a->m[i][0] * b->m[0][j] + a->m[i][1] * b->m[1][j] + a->m[i][2] * b->m[2][j];
    r.t = XfMul(a, b->t);
    return r;
}

bool BodyModelReady(void)
{
    if (gPMTried) return gPMOk;
    gPMTried = true;
    const char *cands[3] = { PM_PATH, TextFormat("%s" PM_PATH, GetApplicationDirectory()), "sweetspot/" PM_PATH };
    const char *path = NULL;
    for (int i = 0; i < 3; i++) if (FileExists(cands[i])) { path = cands[i]; break; }
    if (!path) { TraceLog(LOG_WARNING, "BODY MODEL: %s not found", PM_PATH); return false; }
    char pathCopy[512]; snprintf(pathCopy, sizeof(pathCopy), "%s", path);
    gPM = LoadModel(pathCopy);
    if (gPM.meshCount == 0 || gPM.boneCount == 0 || gPM.boneCount > PM_MAX_BONES) { TraceLog(LOG_WARNING, "BODY MODEL: no skinned mesh"); return false; }
    // マテリアルの名前（glb の JSON）
    for (int i = 0; i < PM_MAX_MATS; i++) gPMRole[i] = -1;
    int sz = 0; unsigned char *raw = LoadFileData(pathCopy, &sz);
    if (raw && sz > 20 && !memcmp(raw, "glTF", 4)) {
        unsigned int jl = raw[12] | raw[13] << 8 | raw[14] << 16 | (unsigned)raw[15] << 24;
        if (20 + jl <= (unsigned)sz) {
            char *json = malloc(jl + 1); memcpy(json, raw + 20, jl); json[jl] = 0;
            char names[PM_MAX_MATS][32]; int n = GlbMaterialNames(json, names, PM_MAX_MATS - 1);
            static const char *ROLE[BR_COUNT] = { "skin", "shirt", "shorts", "socks", "shoes", "sole", "hair" };
            for (int i = 0; i < n; i++) for (int r = 0; r < BR_COUNT; r++)
                if (!strcmp(names[i], ROLE[r])) gPMRole[i + 1] = r;   // raylib は 0 番に既定のマテリアルを入れるので +1
            free(json);
        }
    }
    if (raw) UnloadFileData(raw);
    // 骨：関節の対応と、元の姿勢の向き
    for (int b = 0; b < gPM.boneCount; b++) { gPMBoneJ[b][0] = gPMBoneJ[b][1] = -1; gPMBoneRef[b] = 0; gPMBoneThick[b] = 0; }
    for (int k = 0; k < (int)(sizeof(BMAP) / sizeof(BMAP[0])); k++) {
        int b = BoneIndex(BMAP[k].name), tb = BoneIndex(TAILBONE[k][1]);
        if (b < 0 || tb < 0) continue;
        gPMBoneJ[b][0] = BMAP[k].head; gPMBoneJ[b][1] = BMAP[k].tail; gPMBoneRef[b] = BMAP[k].ref; gPMBoneThick[b] = BMAP[k].thick;
        Vector3 h = gPM.bindPose[b].translation, t = gPM.bindPose[tb].translation;
        Xf f; Frame(h, t, (Vector3){ 1, 0, 0 }, (Vector3){ 0, 0, 1 }, f.m, &gPMBindLen[b]);
        // 逆：回転は転置、平行移動は -R^T h
        Xf inv;
        for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) inv.m[i][j] = f.m[j][i];
        inv.t = Vector3Negate(XfDir(&inv, h));
        gPMBindInv[b] = inv;
    }
    if (BoneIndex("hips") < 0) { TraceLog(LOG_WARNING, "BODY MODEL: bone 'hips' missing"); return false; }
    gPMMaxVerts = 0;
    for (int i = 0; i < gPM.meshCount; i++) if (gPM.meshes[i].vertexCount > gPMMaxVerts) gPMMaxVerts = gPM.meshes[i].vertexCount;
    gPMPos = malloc(sizeof(Vector3) * gPMMaxVerts); gPMNrm = malloc(sizeof(Vector3) * gPMMaxVerts);
    int tris = 0; for (int i = 0; i < gPM.meshCount; i++) tris += gPM.meshes[i].triangleCount;
    TraceLog(LOG_INFO, "BODY MODEL: %s  meshes %d  bones %d  triangles %d", pathCopy, gPM.meshCount, gPM.boneCount, tris);
    gPMOk = true;
    return true;
}

void DrawBodyModel(const BodyPose *bp)
{
    if (!BodyModelReady()) return;
    // 骨ごとの動き（元の姿勢 → 今の姿勢）
    static Xf skin[PM_MAX_BONES]; static bool done[PM_MAX_BONES];
    for (int b = 0; b < gPM.boneCount; b++) done[b] = false;
    for (int pass = 0; pass < 2; pass++)
    for (int b = 0; b < gPM.boneCount; b++) {
        if (done[b]) continue;
        if (gPMBoneJ[b][0] < 0) {   // 使わない骨：親と同じに動く（親がまだなら次の回で）
            int p = gPM.bones[b].parent;
            if (p < 0) { skin[b] = (Xf){ { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } }, { 0, 0, 0 } }; done[b] = true; }
            else if (done[p]) { skin[b] = skin[p]; done[b] = true; }
            continue;
        }
        Vector3 h = bp->j[gPMBoneJ[b][0]], t = bp->j[gPMBoneJ[b][1]];
        Vector3 side = bp->side[gPMBoneRef[b]];
        Xf cur; float len;
        Frame(h, t, side, bp->fwd, cur.m, &len);
        float s = gPMBindLen[b] > 1e-5f ? len / gPMBindLen[b] : 1.0f;
        float k = gPMBoneThick[b] == 1 ? bp->thickArm : gPMBoneThick[b] == 2 ? bp->thickLeg : gPMBoneThick[b] == 3 ? bp->thickBody : 1.0f;
        for (int i = 0; i < 3; i++) { cur.m[i][0] *= k; cur.m[i][1] *= s; cur.m[i][2] *= k; }   // 骨の向きの中で：横・前後は太さ、縦は長さ
        cur.t = h;
        skin[b] = XfCompose(&cur, &gPMBindInv[b]);
        done[b] = true;
    }
    for (int b = 0; b < gPM.boneCount; b++) if (!done[b]) skin[b] = (Xf){ { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } }, { 0, 0, 0 } };

    // 体の座標 → 世界（render.c の W と同じ：左右の反転・身長・向き）
    float sx = bp->facingNegZ ? 1.0f : -1.0f;
    #define TOW(v) ((Vector3){ bp->pos.x + sx * bp->mx * (v).x * bp->sc, bp->pos.y + (v).y * bp->sc, bp->pos.z - sx * (v).z * bp->sc })
    #define TON(v) ((Vector3){ sx * bp->mx * (v).x, (v).y, -sx * (v).z })
    bool flip = (sx * bp->mx * -sx) < 0.0f;   // 鏡に映した向き（行列式が負）なら三角の回りを逆に

    for (int mi = 0; mi < gPM.meshCount; mi++) {
        Mesh *m = &gPM.meshes[mi];
        if (!m->vertices || !m->boneIds || !m->boneWeights) continue;
        int mat = gPM.meshMaterial ? gPM.meshMaterial[mi] : 0;
        int role = (mat >= 0 && mat < PM_MAX_MATS) ? gPMRole[mat] : -1;
        Color col = role >= 0 ? bp->col[role] : gPM.materials[mat].maps[MATERIAL_MAP_DIFFUSE].color;
        if (col.a == 0) continue;   // 色を透明にした部分は描かない（女子のショーツなど）
        for (int v = 0; v < m->vertexCount; v++) {
            Vector3 p0 = { m->vertices[3 * v], m->vertices[3 * v + 1], m->vertices[3 * v + 2] };
            Vector3 n0 = m->normals ? (Vector3){ m->normals[3 * v], m->normals[3 * v + 1], m->normals[3 * v + 2] } : (Vector3){ 0, 1, 0 };
            Vector3 p = { 0 }, n = { 0 };
            for (int q = 0; q < 4; q++) {
                float w = m->boneWeights[4 * v + q]; if (w <= 0.0f) continue;
                int bi = m->boneIds[4 * v + q]; if (bi >= gPM.boneCount) continue;
                p = Vector3Add(p, Vector3Scale(XfMul(&skin[bi], p0), w));
                n = Vector3Add(n, Vector3Scale(XfDir(&skin[bi], n0), w));
            }
            gPMPos[v] = TOW(p);
            gPMNrm[v] = Vector3Normalize(TON(n));
        }
        int triN = m->triangleCount;
        for (int t0 = 0; t0 < triN; t0 += 256) {
            int t1 = t0 + 256 < triN ? t0 + 256 : triN;
            rlCheckRenderBatchLimit((t1 - t0) * 3);
            rlBegin(RL_TRIANGLES);
            for (int t = t0; t < t1; t++) {
                int i0, i1, i2;
                if (m->indices) { i0 = m->indices[3 * t]; i1 = m->indices[3 * t + 1]; i2 = m->indices[3 * t + 2]; }
                else { i0 = 3 * t; i1 = 3 * t + 1; i2 = 3 * t + 2; }
                if (flip) { int tmp = i1; i1 = i2; i2 = tmp; }
                LitVertexExt(gPMPos[i0], gPMNrm[i0], col);
                LitVertexExt(gPMPos[i1], gPMNrm[i1], col);
                LitVertexExt(gPMPos[i2], gPMNrm[i2], col);
            }
            rlEnd();
        }
    }
    #undef TOW
    #undef TON
}
