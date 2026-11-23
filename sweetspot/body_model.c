// SWEET SPOT — 選手のスキニングのモデル（models/players/pNN.glb。tools/make_skinned_players.py が作る）を、GPU で曲げて描く
//
//   ・動きは今までどおりコード（render.c の DrawFigureBody）が関節の位置を計算する。ここはその関節の位置・向きを、
//     glb の骨（決まった名前：hips spine1 … toe.L）に毎フレーム書き込み、シェーダーの中で頂点を骨の重みで動かす（GPU スキニング）
//   ・骨と関節の対応は SKMAP の表。骨のねじれは、骨ごとに決めた基準の向き（腕：肘が曲がる側、脚：後ろ、胴・頭：前、足：上）で決める。
//     元の姿勢（Tポーズ）でも同じ規則で向きを作るので、どの姿勢でもねじれない
//   ・glb は右手系（選手の右が -x）。ゲームの体の座標（選手の右が +x）に合わせて、読むときに x を反転し、三角の回りを逆にする
//   ・色は選手ごとのパレット（16x16 の絵）、顔は選手ごとの顔の絵（parts_model.c と同じもの）。陰影と輪郭線はほかの図形と同じ光で
//   ・シェーダーが使えないとき（読めなかったとき）は、CPU で曲げて今までの描き方（LitVertexExt）で描く
#include "game.h"
#include "rlgl.h"
#include "raymath.h"
#include <string.h>
#include <stdlib.h>

void LitVertexExt(Vector3 p, Vector3 n, Color c);   // render.c

// raylib の版の違い：6 から骨の情報は Model.skeleton にまとまり、頂点の骨の番号は boneIndices になった（5.5 は Model.bones・Mesh.boneIds）
#if RAYLIB_VERSION_MAJOR >= 6
#define PM_BONECOUNT(m)   ((int)(m).skeleton.boneCount)
#define PM_BONES(m)       ((m).skeleton.bones)
#define PM_BINDPOSE(m)    ((m).skeleton.bindPose)
#define PM_BONEIDS(mesh)  ((mesh)->boneIndices)
#else
#define PM_BONECOUNT(m)   ((m).boneCount)
#define PM_BONES(m)       ((m).bones)
#define PM_BINDPOSE(m)    ((m).bindPose)
#define PM_BONEIDS(mesh)  ((mesh)->boneIds)
#endif

#define SK_MAX_BONES 32
#define SK_MAX_MESH  8

// 骨の向き：Y = 頭→しっぽ、X = 横の基準を骨に直角にしたもの、Z = X×Y（パーツモデルで使う）
void BodyFrame(Vector3 h, Vector3 t, Vector3 side, Vector3 alt, float m[3][3], float *len)
{
    Vector3 y = Vector3Subtract(t, h); *len = Vector3Length(y);
    y = *len > 1e-6f ? Vector3Scale(y, 1.0f / *len) : (Vector3){ 0, 1, 0 };
    Vector3 x = Vector3Subtract(side, Vector3Scale(y, Vector3DotProduct(side, y)));
    float xl = Vector3Length(x);
    if (xl < 0.35f) {
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
    m[0][0] = x.x; m[1][0] = x.y; m[2][0] = x.z;
    m[0][1] = y.x; m[1][1] = y.y; m[2][1] = y.z;
    m[0][2] = z.x; m[1][2] = z.y; m[2][2] = z.z;
}

// スキニングの骨の向き：Y = 頭→しっぽ、Z = 基準の向き（ref）を骨に直角にしたもの、X = Y×Z。ref が骨とほぼ平行なら alt を混ぜる
static void FrameZ(Vector3 h, Vector3 t, Vector3 ref, Vector3 alt, float m[3][3], float *len)
{
    Vector3 y = Vector3Subtract(t, h); *len = Vector3Length(y);
    y = *len > 1e-6f ? Vector3Scale(y, 1.0f / *len) : (Vector3){ 0, 1, 0 };
    Vector3 z = Vector3Subtract(ref, Vector3Scale(y, Vector3DotProduct(ref, y)));
    float zl = Vector3Length(z);
    if (zl < 0.30f) {
        Vector3 z2 = Vector3Subtract(alt, Vector3Scale(y, Vector3DotProduct(alt, y)));
        float l2 = Vector3Length(z2);
        if (l2 > 1e-4f) {
            z2 = Vector3Scale(z2, 1.0f / l2);
            float k = zl / 0.30f;
            z = Vector3Add(Vector3Scale(z2, 1.0f - k), Vector3Scale(zl > 1e-6f ? Vector3Scale(z, 1.0f / zl) : z2, k));
            z = Vector3Subtract(z, Vector3Scale(y, Vector3DotProduct(z, y)));
            zl = Vector3Length(z);
        }
    }
    z = zl > 1e-6f ? Vector3Scale(z, 1.0f / zl) : (Vector3){ 0, 0, 1 };
    Vector3 x = Vector3CrossProduct(y, z);
    m[0][0] = x.x; m[1][0] = x.y; m[2][0] = x.z;
    m[0][1] = y.x; m[1][1] = y.y; m[2][1] = y.z;
    m[0][2] = z.x; m[1][2] = z.y; m[2][2] = z.z;
}

// ---- 骨（決まった名前）とゲームの関節の対応表 ----
typedef struct { const char *name; int head, tail; int ref; Vector3 bindRef; bool stretch; const char *child; } SkMap;
static const SkMap SKMAP[] = {
    //  骨           頭の関節       しっぽの関節     ねじれの基準（今）  （元の姿勢）       伸び縮み  しっぽを知る子の骨
    { "hips",       BJ_PELVIS,  BJ_WAIST,     BREF_HIPF,   { 0, 0, 1 },  true,  "spine1" },
    { "spine1",     BJ_WAIST,   BJ_SPINE,     BREF_SP1,    { 0, 0, 1 },  true,  "spine2" },
    { "spine2",     BJ_SPINE,   BJ_CHEST,     BREF_SP2,    { 0, 0, 1 },  true,  "spine3" },
    { "spine3",     BJ_CHEST,   BJ_NECK,      BREF_CHESTF, { 0, 0, 1 },  true,  "neck" },
    { "neck",       BJ_NECK,    BJ_HEADBASE,  BREF_CHESTF, { 0, 0, 1 },  true,  "head" },
    { "head",       BJ_HEADBASE, BJ_HEADTOP,  BREF_FACEF,  { 0, 0, 1 },  false, NULL },
    { "clavicle.R", BJ_CLAV,    BJ_SH_R,      BREF_CHESTF, { 0, 0, 1 },  true,  "upperarm.R" },
    { "upperarm.R", BJ_SH_R,    BJ_EL_R,      BREF_ARM_R,  { 0, 0, 1 },  true,  "forearm.R" },
    { "forearm.R",  BJ_EL_R,    BJ_HD_R,      BREF_ARM_R,  { 0, 0, 1 },  true,  "hand.R" },
    { "hand.R",     BJ_HD_R,    BJ_TIP_R,     BREF_ARM_R,  { 0, 0, 1 },  false, NULL },
    { "clavicle.L", BJ_CLAV,    BJ_SH_L,      BREF_CHESTF, { 0, 0, 1 },  true,  "upperarm.L" },
    { "upperarm.L", BJ_SH_L,    BJ_EL_L,      BREF_ARM_L,  { 0, 0, 1 },  true,  "forearm.L" },
    { "forearm.L",  BJ_EL_L,    BJ_HD_L,      BREF_ARM_L,  { 0, 0, 1 },  true,  "hand.L" },
    { "hand.L",     BJ_HD_L,    BJ_TIP_L,     BREF_ARM_L,  { 0, 0, 1 },  false, NULL },
    { "thigh.R",    BJ_HIP_R,   BJ_KN_R,      BREF_BACK,   { 0, 0, -1 }, true,  "shin.R" },
    { "shin.R",     BJ_KN_R,    BJ_AN_R,      BREF_BACK,   { 0, 0, -1 }, true,  "foot.R" },
    { "foot.R",     BJ_AN_R,    BJ_BALL_R,    BREF_UP,     { 0, 1, 0 },  true,  "toe.R" },
    { "toe.R",      BJ_BALL_R,  BJ_TOETIP_R,  BREF_UP,     { 0, 1, 0 },  false, NULL },
    { "thigh.L",    BJ_HIP_L,   BJ_KN_L,      BREF_BACK,   { 0, 0, -1 }, true,  "shin.L" },
    { "shin.L",     BJ_KN_L,    BJ_AN_L,      BREF_BACK,   { 0, 0, -1 }, true,  "foot.L" },
    { "foot.L",     BJ_AN_L,    BJ_BALL_L,    BREF_UP,     { 0, 1, 0 },  true,  "toe.L" },
    { "toe.L",      BJ_BALL_L,  BJ_TOETIP_L,  BREF_UP,     { 0, 1, 0 },  false, NULL },
};
#define SKMAP_N ((int)(sizeof(SKMAP) / sizeof(SKMAP[0])))

typedef struct { float m[3][3]; Vector3 t; } Xf;
static Vector3 XfMul(const Xf *a, Vector3 v) { return (Vector3){ a->m[0][0] * v.x + a->m[0][1] * v.y + a->m[0][2] * v.z + a->t.x, a->m[1][0] * v.x + a->m[1][1] * v.y + a->m[1][2] * v.z + a->t.y, a->m[2][0] * v.x + a->m[2][1] * v.y + a->m[2][2] * v.z + a->t.z }; }
static Vector3 XfDir(const Xf *a, Vector3 v) { return (Vector3){ a->m[0][0] * v.x + a->m[0][1] * v.y + a->m[0][2] * v.z, a->m[1][0] * v.x + a->m[1][1] * v.y + a->m[1][2] * v.z, a->m[2][0] * v.x + a->m[2][1] * v.y + a->m[2][2] * v.z }; }
static Xf XfCompose(const Xf *a, const Xf *b)
{
    Xf r;
    for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) r.m[i][j] = a->m[i][0] * b->m[0][j] + a->m[i][1] * b->m[1][j] + a->m[i][2] * b->m[2][j];
    r.t = XfMul(a, b->t);
    return r;
}

typedef struct {
    int vcount, icount, kind;              // kind 0 = 体（パレット）、1 = 顔の絵
    float *pos, *nrm, *uv, *w;
    unsigned char *ids;
    unsigned short *idx;
    unsigned int vao, vbo[5], ebo;
} SkMesh;
typedef struct {
    int state;                              // 0 まだ、1 読めた、-1 読めない
    int boneCount;
    int map[SK_MAX_BONES], parent[SK_MAX_BONES];
    Xf bindInv[SK_MAX_BONES]; float bindLen[SK_MAX_BONES];
    SkMesh mesh[SK_MAX_MESH]; int meshN;
} SkModel;
static SkModel gSk[PERSONA_COUNT + 1];      // 選手の番号 + 1（0 = 自分＝癖なし）

// ---- シェーダー（GPU スキニング）。骨は 3 行（vec4）ずつの配列で渡す（ブラウザ版の uniform の数に収める）----
#if defined(PLATFORM_WEB)
#define GLSL_VS_HEAD "#version 100\nprecision highp float;\n#define IN attribute\n#define OUT varying\n"
#define GLSL_FS_HEAD "#version 100\nprecision mediump float;\n#define IN varying\n#define FRAG gl_FragColor\n#define TEX texture2D\n"
#else
#define GLSL_VS_HEAD "#version 330\n#define IN in\n#define OUT out\n"
#define GLSL_FS_HEAD "#version 330\n#define IN in\nout vec4 fragColorOut;\n#define FRAG fragColorOut\n#define TEX texture\n"
#endif
static const char *SK_VS = GLSL_VS_HEAD
    "IN vec3 aPos; IN vec3 aNrm; IN vec2 aUV; IN vec4 aBone; IN vec4 aW;\n"
    "uniform mat4 mvp; uniform vec4 boneRows[96]; uniform vec3 nSign; uniform float outlineW;\n"
    "OUT vec2 vUV; OUT vec3 vN;\n"
    "vec3 bx(float b, vec4 p) { int i = int(b + 0.5) * 3; return vec3(dot(boneRows[i], p), dot(boneRows[i + 1], p), dot(boneRows[i + 2], p)); }\n"
    "void main() {\n"
    "  vec4 p = vec4(aPos, 1.0), n = vec4(aNrm, 0.0);\n"
    "  vec3 sp = aW.x * bx(aBone.x, p) + aW.y * bx(aBone.y, p) + aW.z * bx(aBone.z, p) + aW.w * bx(aBone.w, p);\n"
    "  vec3 sn = aW.x * bx(aBone.x, n) + aW.y * bx(aBone.y, n) + aW.z * bx(aBone.z, n) + aW.w * bx(aBone.w, n);\n"
    "  sn = normalize(sn);\n"
    "  sp += sn * outlineW;\n"
    "  vN = normalize(sn * nSign); vUV = aUV;\n"
    "  gl_Position = mvp * vec4(sp, 1.0);\n"
    "}\n";
static const char *SK_FS = GLSL_FS_HEAD
    "IN vec2 vUV; IN vec3 vN;\n"
    "uniform sampler2D texture0; uniform vec3 lightL; uniform float amb; uniform float diff; uniform float look; uniform float outline; uniform vec4 outlineCol;\n"
    "void main() {\n"
    "  if (outline > 0.5) { FRAG = outlineCol; return; }\n"
    "  vec4 c = TEX(texture0, vUV);\n"
    "  float d = dot(normalize(vN), lightL);\n"
    "  float l = amb + diff * max(d, 0.0);\n"
    "  if (look > 0.5) { l = d > 0.45 ? amb + diff * 0.95 : (d > 0.0 ? amb + diff * 0.55 : amb * 0.92); float g = (c.r + c.g + c.b) / 3.0; c.rgb = clamp(vec3(g) + (c.rgb - vec3(g)) * 1.18, 0.0, 1.0); }\n"
    "  FRAG = vec4(c.rgb * min(l, 1.05), c.a);\n"
    "}\n";
static Shader gSh; static int gShState = 0;   // 0 まだ、1 使える、-1 使えない（CPU で曲げる）
static int locPos, locNrm, locUV, locBone, locW, locMvp, locRows, locNSign, locOutW, locTex, locL, locAmb, locDiff, locLook, locOutline, locOutCol;
static bool ShaderReady(void)
{
    if (gShState) return gShState > 0;
    if (getenv("SWEETSPOT_CPU_SKIN")) { gShState = -1; return false; }   // 確かめる用：CPU で曲げる
    gSh = LoadShaderFromMemory(SK_VS, SK_FS);
    if (gSh.id == 0 || gSh.id == rlGetShaderIdDefault()) { gShState = -1; TraceLog(LOG_WARNING, "SKIN MODEL: shader failed -> CPU skinning"); return false; }
    locPos = rlGetLocationAttrib(gSh.id, "aPos"); locNrm = rlGetLocationAttrib(gSh.id, "aNrm"); locUV = rlGetLocationAttrib(gSh.id, "aUV");
    locBone = rlGetLocationAttrib(gSh.id, "aBone"); locW = rlGetLocationAttrib(gSh.id, "aW");
    locMvp = rlGetLocationUniform(gSh.id, "mvp"); locRows = rlGetLocationUniform(gSh.id, "boneRows"); locNSign = rlGetLocationUniform(gSh.id, "nSign");
    locOutW = rlGetLocationUniform(gSh.id, "outlineW"); locTex = rlGetLocationUniform(gSh.id, "texture0"); locL = rlGetLocationUniform(gSh.id, "lightL");
    locAmb = rlGetLocationUniform(gSh.id, "amb"); locDiff = rlGetLocationUniform(gSh.id, "diff"); locLook = rlGetLocationUniform(gSh.id, "look");
    locOutline = rlGetLocationUniform(gSh.id, "outline"); locOutCol = rlGetLocationUniform(gSh.id, "outlineCol");
    if (locPos < 0 || locBone < 0 || locW < 0 || locRows < 0) { gShState = -1; TraceLog(LOG_WARNING, "SKIN MODEL: shader locations missing -> CPU skinning"); return false; }
    gShState = 1;
    TraceLog(LOG_INFO, "SKIN MODEL: GPU skinning shader ready");
    return true;
}
static void BindMesh(const SkMesh *m)
{
    rlEnableVertexBuffer(m->vbo[0]); rlSetVertexAttribute(locPos, 3, RL_FLOAT, false, 0, 0); rlEnableVertexAttribute(locPos);
    if (locNrm >= 0) { rlEnableVertexBuffer(m->vbo[1]); rlSetVertexAttribute(locNrm, 3, RL_FLOAT, false, 0, 0); rlEnableVertexAttribute(locNrm); }
    if (locUV >= 0) { rlEnableVertexBuffer(m->vbo[2]); rlSetVertexAttribute(locUV, 2, RL_FLOAT, false, 0, 0); rlEnableVertexAttribute(locUV); }
    rlEnableVertexBuffer(m->vbo[3]); rlSetVertexAttribute(locBone, 4, RL_UNSIGNED_BYTE, false, 0, 0); rlEnableVertexAttribute(locBone);
    rlEnableVertexBuffer(m->vbo[4]); rlSetVertexAttribute(locW, 4, RL_FLOAT, false, 0, 0); rlEnableVertexAttribute(locW);
    rlEnableVertexBufferElement(m->ebo);
}

// ---- glb を読む（選手ごと）----
static int GlbMaterialNames(const char *path, char names[][32], int maxN);
static int CanonIndex(const char *name) { for (int k = 0; k < SKMAP_N; k++) if (!strcmp(SKMAP[k].name, name)) return k; return -1; }
static SkModel *LoadSk(int lookId)
{
    int slot = (lookId >= 0 && lookId < PERSONA_COUNT) ? lookId + 1 : 0;
    SkModel *S = &gSk[slot];
    if (S->state) return S->state > 0 ? S : NULL;
    S->state = -1;
    int fileId = slot == 0 ? 0 : lookId;
    const char *rel = TextFormat("models/players/p%02d.glb", fileId);
    const char *cands[3] = { rel, TextFormat("%s%s", GetApplicationDirectory(), rel), TextFormat("sweetspot/%s", rel) };
    char path[512] = { 0 };
    for (int i = 0; i < 3; i++) if (FileExists(cands[i])) { snprintf(path, sizeof(path), "%s", cands[i]); break; }
    if (!path[0]) {   // その選手の glb がなければ p00（癖なし）
        if (fileId != 0) { SkModel *d = LoadSk(-1); if (d) { *S = *d; S->state = 1; return S; } }
        TraceLog(LOG_WARNING, "SKIN MODEL: %s not found", rel); return NULL;
    }
    Model mdl = LoadModel(path);
    int nb = PM_BONECOUNT(mdl);
    if (mdl.meshCount == 0 || nb == 0 || nb > SK_MAX_BONES) { TraceLog(LOG_WARNING, "SKIN MODEL: %s has no skeleton (bones %d)", path, nb); UnloadModel(mdl); return NULL; }
    char mats[16][32]; int nm = GlbMaterialNames(path, mats, 15);
    S->boneCount = nb;
    // 骨：対応表の番号、親、元の姿勢の向き（x を反転してゲームの座標に）
    Vector3 bh[SK_MAX_BONES]; Quaternion bq[SK_MAX_BONES];
    for (int b = 0; b < nb; b++) {
        S->map[b] = CanonIndex(PM_BONES(mdl)[b].name);
        S->parent[b] = PM_BONES(mdl)[b].parent;
        Vector3 t = PM_BINDPOSE(mdl)[b].translation; bh[b] = (Vector3){ -t.x, t.y, t.z };
        bq[b] = PM_BINDPOSE(mdl)[b].rotation;
    }
    int found = 0;
    for (int b = 0; b < nb; b++) {
        int k = S->map[b];
        if (k < 0) continue;
        found++;
        Vector3 tail;
        int cb = -1;
        if (SKMAP[k].child) for (int c = 0; c < nb; c++) if (!strcmp(PM_BONES(mdl)[c].name, SKMAP[k].child)) cb = c;
        if (cb >= 0) tail = bh[cb];
        else {   // 子がない骨（頭・手・つま先）：骨の向き（glTF の骨の +Y）へ 10cm
            Vector3 y = Vector3RotateByQuaternion((Vector3){ 0, 1, 0 }, bq[b]);
            tail = Vector3Add(bh[b], (Vector3){ -y.x * 0.10f, y.y * 0.10f, y.z * 0.10f });
        }
        Xf f; FrameZ(bh[b], tail, SKMAP[k].bindRef, (Vector3){ 0, 1, 0 }, f.m, &S->bindLen[b]);
        Xf inv;
        for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) inv.m[i][j] = f.m[j][i];
        inv.t = Vector3Negate(XfDir(&inv, bh[b]));
        S->bindInv[b] = inv;
    }
    if (found < 10) { TraceLog(LOG_WARNING, "SKIN MODEL: %s bones not renamed (found %d). Run tools/make_skinned_players.py", path, found); UnloadModel(mdl); return NULL; }
    // メッシュ：x を反転、三角の回りを逆に
    S->meshN = 0;
    for (int mi = 0; mi < mdl.meshCount && S->meshN < SK_MAX_MESH; mi++) {
        Mesh *m = &mdl.meshes[mi];
        if (!m->vertices || !PM_BONEIDS(m) || !m->boneWeights) continue;
        SkMesh *o = &S->mesh[S->meshN++];
        memset(o, 0, sizeof(*o));
        int mat = mdl.meshMaterial ? mdl.meshMaterial[mi] : 0;
        o->kind = (mat >= 1 && mat - 1 < nm && !strcmp(mats[mat - 1], "face")) ? 1 : 0;
        int n = m->vertexCount; o->vcount = n;
        o->pos = malloc(n * 12); o->nrm = malloc(n * 12); o->uv = malloc(n * 8); o->w = malloc(n * 16); o->ids = malloc(n * 4);
        for (int v = 0; v < n; v++) {
            o->pos[3 * v] = -m->vertices[3 * v]; o->pos[3 * v + 1] = m->vertices[3 * v + 1]; o->pos[3 * v + 2] = m->vertices[3 * v + 2];
            if (m->normals) { o->nrm[3 * v] = -m->normals[3 * v]; o->nrm[3 * v + 1] = m->normals[3 * v + 1]; o->nrm[3 * v + 2] = m->normals[3 * v + 2]; }
            else { o->nrm[3 * v] = 0; o->nrm[3 * v + 1] = 1; o->nrm[3 * v + 2] = 0; }
            o->uv[2 * v] = m->texcoords ? m->texcoords[2 * v] : 0.0f; o->uv[2 * v + 1] = m->texcoords ? m->texcoords[2 * v + 1] : 0.0f;
            for (int q = 0; q < 4; q++) { o->ids[4 * v + q] = PM_BONEIDS(m)[4 * v + q]; o->w[4 * v + q] = m->boneWeights[4 * v + q]; }
        }
        int tri = m->triangleCount; o->icount = tri * 3;
        o->idx = malloc(sizeof(unsigned short) * o->icount);
        for (int t = 0; t < tri; t++) {
            int a = m->indices ? m->indices[3 * t] : 3 * t, b = m->indices ? m->indices[3 * t + 1] : 3 * t + 1, c = m->indices ? m->indices[3 * t + 2] : 3 * t + 2;
            o->idx[3 * t] = (unsigned short)a; o->idx[3 * t + 1] = (unsigned short)c; o->idx[3 * t + 2] = (unsigned short)b;   // 反転したので回りを逆に
        }
        if (ShaderReady()) {
            o->vao = rlLoadVertexArray();
            bool vao = o->vao && rlEnableVertexArray(o->vao);
            o->vbo[0] = rlLoadVertexBuffer(o->pos, n * 12, false); o->vbo[1] = rlLoadVertexBuffer(o->nrm, n * 12, false);
            o->vbo[2] = rlLoadVertexBuffer(o->uv, n * 8, false); o->vbo[3] = rlLoadVertexBuffer(o->ids, n * 4, false);
            o->vbo[4] = rlLoadVertexBuffer(o->w, n * 16, false);
            o->ebo = rlLoadVertexBufferElement(o->idx, o->icount * (int)sizeof(unsigned short), false);
            if (vao) { BindMesh(o); rlDisableVertexArray(); }
            else o->vao = 0;
        }
    }
    UnloadModel(mdl);
    TraceLog(LOG_INFO, "SKIN MODEL: %s  bones %d (mapped %d)  meshes %d  %s", path, nb, found, S->meshN, gShState > 0 ? "GPU" : "CPU");
    S->state = 1;
    return S;
}

bool BodyModelReady(void) { return LoadSk(-1) != NULL; }

// ---- 選手ごとの色の表を、16x16 の絵にする ----
typedef struct { unsigned int key; Texture2D tex; } PalEntry;
static Texture2D PaletteTex(const BodyPose *bp)
{
    static PalEntry cache[40]; static int n = 0;
    const Color *pal = PartsPaletteFor(bp);
    unsigned int key = 2166136261u;
    for (int i = 0; i < 16; i++) key = (key ^ (unsigned int)ColorToInt(pal[i])) * 16777619u;
    for (int i = 0; i < n; i++) if (cache[i].key == key) return cache[i].tex;
    Image im = GenImageColor(16, 16, WHITE);
    for (int y = 0; y < 16; y++) for (int x = 0; x < 16; x++) ImageDrawPixel(&im, x, y, pal[y * 16 + x]);
    Texture2D t = LoadTextureFromImage(im); UnloadImage(im);
    SetTextureFilter(t, TEXTURE_FILTER_POINT);
    if (n < 40) cache[n++] = (PalEntry){ key, t };
    return t;
}

// ---- 毎フレーム：骨の行列を作って描く ----
void DrawBodyModel(const BodyPose *bpIn)
{
    BodyPose bpL = *bpIn; PartsApplyLook(&bpL);
    const BodyPose *bp = &bpL;
    SkModel *S = LoadSk(bp->lookId);
    if (!S) return;
    // 骨ごとの動き（元の姿勢 → 今の姿勢。体の座標）
    static Xf skin[SK_MAX_BONES]; bool done[SK_MAX_BONES] = { 0 };
    int hipsB = -1;
    for (int b = 0; b < S->boneCount; b++) {
        int k = S->map[b];
        if (k < 0) continue;
        if (!strcmp(SKMAP[k].name, "hips")) hipsB = b;
        Vector3 h = bp->j[SKMAP[k].head], t = bp->j[SKMAP[k].tail];
        Xf cur; float len;
        FrameZ(h, t, bp->ref[SKMAP[k].ref], bp->ref[BREF_UP], cur.m, &len);
        float s = (SKMAP[k].stretch && S->bindLen[b] > 1e-4f) ? Clamp(len / S->bindLen[b], 0.6f, 1.7f) : 1.0f;
        for (int i = 0; i < 3; i++) cur.m[i][1] *= s;
        cur.t = h;
        skin[b] = XfCompose(&cur, &S->bindInv[b]);
        done[b] = true;
    }
    for (int pass = 0; pass < 3; pass++)   // 対応表にない骨（root など）：親と同じ。親もなければ腰と同じ
        for (int b = 0; b < S->boneCount; b++) {
            if (done[b]) continue;
            int p = S->parent[b];
            if (p >= 0 && done[p]) { skin[b] = skin[p]; done[b] = true; }
            else if (pass == 2) { skin[b] = hipsB >= 0 ? skin[hipsB] : (Xf){ { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } }, { 0, 0, 0 } }; done[b] = true; }
        }
    bool outline = LitOutlinePass();
    float sx = bp->facingNegZ ? 1.0f : -1.0f;
    bool flip = bp->mx > 0.0f;   // 体の座標 → 世界が鏡なら、表と裏が入れ替わる
    if (gShState > 0) {
        Texture2D palT = PaletteTex(bp), faceT = PartsFaceTexIn(bp, "models/players/faces");
        // 体の座標 → 世界：x は sx*mx*sc、y は sc、z は -sx*sc、それに足元の位置
        Matrix model = { sx * bp->mx * bp->sc, 0, 0, bp->pos.x,
                         0, bp->sc, 0, bp->pos.y,
                         0, 0, -sx * bp->sc, bp->pos.z,
                         0, 0, 0, 1 };
        Matrix mvp = MatrixMultiply(MatrixMultiply(model, rlGetMatrixModelview()), rlGetMatrixProjection());
        static float rows[SK_MAX_BONES * 12];
        for (int b = 0; b < S->boneCount; b++)
            for (int r = 0; r < 3; r++) { rows[b * 12 + r * 4] = skin[b].m[r][0]; rows[b * 12 + r * 4 + 1] = skin[b].m[r][1]; rows[b * 12 + r * 4 + 2] = skin[b].m[r][2]; rows[b * 12 + r * 4 + 3] = r == 0 ? skin[b].t.x : r == 1 ? skin[b].t.y : skin[b].t.z; }
        Vector3 L; float amb, dif, ow; Color oc;
        LitGetParams(&L, &amb, &dif, &ow, &oc);
        float nsign[3] = { sx * bp->mx, 1.0f, -sx }, outW = outline ? ow / bp->sc : 0.0f, lookF = gLook >= 1 ? 1.0f : 0.0f, outF = outline ? 1.0f : 0.0f;
        float ocv[4] = { oc.r / 255.0f, oc.g / 255.0f, oc.b / 255.0f, 1.0f };
        int zero = 0;
        rlDrawRenderBatchActive();
        rlSetCullFace((outline != flip) ? RL_CULL_FACE_FRONT : RL_CULL_FACE_BACK);
        rlEnableShader(gSh.id);
        rlSetUniformMatrix(locMvp, mvp);
        rlSetUniform(locRows, rows, RL_SHADER_UNIFORM_VEC4, S->boneCount * 3);
        rlSetUniform(locNSign, nsign, RL_SHADER_UNIFORM_VEC3, 1);
        rlSetUniform(locOutW, &outW, RL_SHADER_UNIFORM_FLOAT, 1);
        rlSetUniform(locL, &L, RL_SHADER_UNIFORM_VEC3, 1);
        rlSetUniform(locAmb, &amb, RL_SHADER_UNIFORM_FLOAT, 1);
        rlSetUniform(locDiff, &dif, RL_SHADER_UNIFORM_FLOAT, 1);
        rlSetUniform(locLook, &lookF, RL_SHADER_UNIFORM_FLOAT, 1);
        rlSetUniform(locOutline, &outF, RL_SHADER_UNIFORM_FLOAT, 1);
        rlSetUniform(locOutCol, ocv, RL_SHADER_UNIFORM_VEC4, 1);
        rlSetUniform(locTex, &zero, RL_SHADER_UNIFORM_INT, 1);
        for (int pass = 0; pass < 2; pass++)   // 顔の絵（透ける）は後で、奥行きを書かずに
            for (int i = 0; i < S->meshN; i++) {
                const SkMesh *m = &S->mesh[i];
                if ((m->kind == 1) != (pass == 1)) continue;
                if (m->kind == 1 && (outline || !faceT.id)) continue;
                if (m->kind == 1) rlDisableDepthMask();
                rlActiveTextureSlot(0); rlEnableTexture(m->kind == 1 ? faceT.id : palT.id);
                if (m->vao) rlEnableVertexArray(m->vao); else BindMesh(m);
                rlDrawVertexArrayElements(0, m->icount, 0);
                rlDisableVertexArray();
                if (m->kind == 1) rlEnableDepthMask();
            }
        rlDisableTexture();
        rlDisableShader();
        rlSetCullFace(outline ? RL_CULL_FACE_FRONT : RL_CULL_FACE_BACK);
        return;
    }
    // ---- CPU で曲げる（シェーダーが使えないとき）。顔の絵は描かない ----
    const Color *pal = PartsPaletteFor(bp);
    #define TOW(v) ((Vector3){ bp->pos.x + sx * bp->mx * (v).x * bp->sc, bp->pos.y + (v).y * bp->sc, bp->pos.z - sx * (v).z * bp->sc })
    #define TON(v) ((Vector3){ sx * bp->mx * (v).x, (v).y, -sx * (v).z })
    static Vector3 *P = NULL, *N = NULL; static int cap = 0;
    for (int i = 0; i < S->meshN; i++) {
        const SkMesh *m = &S->mesh[i];
        if (m->kind == 1) continue;
        if (cap < m->vcount) { cap = m->vcount; P = realloc(P, sizeof(Vector3) * cap); N = realloc(N, sizeof(Vector3) * cap); }
        for (int v = 0; v < m->vcount; v++) {
            Vector3 p0 = { m->pos[3 * v], m->pos[3 * v + 1], m->pos[3 * v + 2] }, n0 = { m->nrm[3 * v], m->nrm[3 * v + 1], m->nrm[3 * v + 2] };
            Vector3 p = { 0 }, n = { 0 };
            for (int q = 0; q < 4; q++) {
                float w = m->w[4 * v + q]; if (w <= 0.0f) continue;
                int bi = m->ids[4 * v + q]; if (bi >= S->boneCount) continue;
                p = Vector3Add(p, Vector3Scale(XfMul(&skin[bi], p0), w)); n = Vector3Add(n, Vector3Scale(XfDir(&skin[bi], n0), w));
            }
            P[v] = TOW(p); N[v] = Vector3Normalize(TON(n));
        }
        for (int t0 = 0; t0 < m->icount; t0 += 600) {
            int t1 = t0 + 600 < m->icount ? t0 + 600 : m->icount;
            rlCheckRenderBatchLimit(t1 - t0);
            rlBegin(RL_TRIANGLES);
            for (int t = t0; t < t1; t += 3) {
                int a = m->idx[t], b = m->idx[t + 1], c = m->idx[t + 2];
                if (flip) { int tmp = b; b = c; c = tmp; }
                float u = (m->uv[2 * a] + m->uv[2 * b] + m->uv[2 * c]) / 3.0f, vv = (m->uv[2 * a + 1] + m->uv[2 * b + 1] + m->uv[2 * c + 1]) / 3.0f;
                Color col = pal[(int)Clamp((int)(vv * 16), 0, 15) * 16 + (int)Clamp((int)(u * 16), 0, 15)];
                LitVertexExt(P[a], N[a], col); LitVertexExt(P[b], N[b], col); LitVertexExt(P[c], N[c], col);
            }
            rlEnd();
        }
    }
    #undef TOW
    #undef TON
}

// ---- glb の JSON から、マテリアルの名前を順に取り出す（raylib の Material には名前がないので）----
static const char *JsSkipStr(const char *p) { p++; while (*p && *p != '"') { if (*p == '\\' && p[1]) p++; p++; } return *p ? p + 1 : p; }
static const char *JsSkipVal(const char *p)
{
    while (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t') p++;
    if (*p == '"') return JsSkipStr(p);
    if (*p == '{' || *p == '[') { int d = 0; while (*p) { if (*p == '"') { p = JsSkipStr(p); continue; } if (*p == '{' || *p == '[') d++; else if (*p == '}' || *p == ']') { d--; if (d == 0) return p + 1; } p++; } return p; }
    while (*p && *p != ',' && *p != '}' && *p != ']') p++;
    return p;
}
static int GlbMaterialNames(const char *path, char names[][32], int maxN)
{
    int sz = 0, n = 0; unsigned char *raw = LoadFileData(path, &sz);
    if (!raw || sz < 20 || memcmp(raw, "glTF", 4)) { if (raw) UnloadFileData(raw); return 0; }
    unsigned int jl = raw[12] | raw[13] << 8 | raw[14] << 16 | (unsigned)raw[15] << 24;
    if (20 + jl > (unsigned)sz) { UnloadFileData(raw); return 0; }
    char *json = malloc(jl + 1); memcpy(json, raw + 20, jl); json[jl] = 0; UnloadFileData(raw);
    const char *p = strchr(json, '{');
    if (p) p++;
    while (p && *p) {
        while (*p && *p != '"' && *p != '}') p++;
        if (*p != '"') break;
        const char *k0 = p + 1; p = JsSkipStr(p); int klen = (int)(p - 1 - k0);
        while (*p && *p != ':') p++;
        if (!*p) break;
        p++;
        while (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t') p++;
        if (klen == 9 && !strncmp(k0, "materials", 9) && *p == '[') {
            p++;
            while (*p && *p != ']' && n < maxN) {
                while (*p && *p != '{' && *p != ']') p++;
                if (*p != '{') break;
                const char *end = JsSkipVal(p), *q = p + 1;
                names[n][0] = 0;
                while (q < end) {
                    while (q < end && *q != '"' && *q != '}') q++;
                    if (q >= end || *q != '"') break;
                    const char *kk = q + 1; q = JsSkipStr(q); int kl = (int)(q - 1 - kk);
                    while (q < end && *q != ':') q++;
                    q++;
                    while (*q == ' ' || *q == '\n') q++;
                    if (kl == 4 && !strncmp(kk, "name", 4) && *q == '"') { const char *v0 = q + 1, *v1 = JsSkipStr(q) - 1; int vl = (int)(v1 - v0); if (vl > 31) vl = 31; memcpy(names[n], v0, vl); names[n][vl] = 0; }
                    q = JsSkipVal(q);
                    while (q < end && (*q == ',' || *q == ' ' || *q == '\n')) q++;
                }
                n++; p = end;
            }
            break;
        }
        p = JsSkipVal(p);
    }
    free(json);
    return n;
}
