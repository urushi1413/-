// SWEET SPOT — 選手のパーツモデル（tools/make_player.py が作る models/player.glb）を、関節の位置と向きに置いて描く
//
//   ・パーツは部位ごとの別のオブジェクト（pelvis torso neck head、upperarm forearm hand thigh shin shoe の .R / .L）。スキニングはしない
//   ・各パーツは「骨の向き」で作ってある：原点が親の関節、+Y が子の関節の方、+X が選手の右、+Z = X×Y。
//     ゲームは render.c が計算した関節の位置から同じ規則で向きを作り（BodyFrame）、そこにパーツを置くだけ
//   ・raylib の glTF の読み込みは、オブジェクトの位置・向きを頂点に焼き込んでしまう（5.5 も 6 も）。
//     そこで glb の JSON からオブジェクトの位置・向き・大きさを読み、焼き込まれた分を戻してパーツの座標にする
//   ・色は 16x16 のパレットのマス（UV で指す）。マスの色は選手ごとに入れ替える（models/palettes/<短い名前>.png があればそれ）
//   ・陰影と輪郭線（背面法）は、ほかの図形と同じ LitVertexExt を通す。面は平らに塗る（フラットシェーディング）。
//     輪郭線の回だけ、頂点の向きをそろえた法線で膨らませる（面の法線のままだと角で輪郭線が割れる）
#include "game.h"
#include "rlgl.h"
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

void LitVertexExt(Vector3 p, Vector3 n, Color c);   // render.c

#define PT_PATH "models/player.glb"
#define PT_MAX  32

// ---- 小さな JSON の読み取り（glb のオブジェクトの一覧を読むだけ）----
typedef enum { JN_NULL, JN_BOOL, JN_NUM, JN_STR, JN_ARR, JN_OBJ } JType;
typedef struct JNode { JType t; double num; char *str; int n; struct JNode **kids; char **keys; } JNode;
static const char *JWs(const char *p) { while (*p && isspace((unsigned char)*p)) p++; return p; }
static JNode *JNew(JType t) { JNode *j = calloc(1, sizeof(JNode)); j->t = t; return j; }
static void JPush(JNode *j, char *key, JNode *kid)
{
    j->kids = realloc(j->kids, sizeof(JNode *) * (j->n + 1));
    if (j->t == JN_OBJ) j->keys = realloc(j->keys, sizeof(char *) * (j->n + 1)), j->keys[j->n] = key;
    j->kids[j->n++] = kid;
}
static char *JStr(const char **pp)
{
    const char *p = *pp + 1; size_t cap = 16, n = 0; char *s = malloc(cap);
    while (*p && *p != '"') {
        char c = *p++;
        if (c == '\\' && *p) { c = *p++; if (c == 'n') c = '\n'; else if (c == 't') c = '\t'; else if (c == 'u') { p += 4; c = '?'; } }
        if (n + 2 > cap) s = realloc(s, cap *= 2);
        s[n++] = c;
    }
    s[n] = 0; *pp = *p ? p + 1 : p;
    return s;
}
static JNode *JParse(const char **pp)
{
    const char *p = JWs(*pp); JNode *j = NULL;
    if (*p == '{') {
        j = JNew(JN_OBJ); p = JWs(p + 1);
        while (*p && *p != '}') {
            if (*p != '"') break;
            char *k = JStr(&p); p = JWs(p); if (*p == ':') p++;
            JNode *v = JParse(&p); JPush(j, k, v); p = JWs(p);
            if (*p == ',') p = JWs(p + 1);
        }
        if (*p == '}') p++;
    } else if (*p == '[') {
        j = JNew(JN_ARR); p = JWs(p + 1);
        while (*p && *p != ']') { JNode *v = JParse(&p); JPush(j, NULL, v); p = JWs(p); if (*p == ',') p = JWs(p + 1); }
        if (*p == ']') p++;
    } else if (*p == '"') { j = JNew(JN_STR); j->str = JStr(&p); }
    else if (!strncmp(p, "true", 4)) { j = JNew(JN_BOOL); j->num = 1; p += 4; }
    else if (!strncmp(p, "false", 5)) { j = JNew(JN_BOOL); p += 5; }
    else if (!strncmp(p, "null", 4)) { j = JNew(JN_NULL); p += 4; }
    else { char *e; j = JNew(JN_NUM); j->num = strtod(p, &e); p = e > p ? e : p + 1; }
    *pp = p;
    return j;
}
static void JFree(JNode *j)
{
    if (!j) return;
    for (int i = 0; i < j->n; i++) { JFree(j->kids[i]); if (j->keys) free(j->keys[i]); }
    free(j->kids); free(j->keys); free(j->str); free(j);
}
static JNode *JGet(const JNode *j, const char *key)
{
    if (!j || j->t != JN_OBJ) return NULL;
    for (int i = 0; i < j->n; i++) if (!strcmp(j->keys[i], key)) return j->kids[i];
    return NULL;
}
static float JNum(const JNode *j, float def) { return (j && (j->t == JN_NUM || j->t == JN_BOOL)) ? (float)j->num : def; }

// ---- パーツ ----
typedef struct {
    char name[24];
    int head, tail, side, thick;   // ピボットの関節・向きの子の関節・横の基準（BodyPose.side の番号）・太さの倍率の種類（0 なし 1 腕 2 脚 3 胴）
    bool stretch;                  // 関節の間の長さに合わせて縦に伸び縮みする（頭・手・靴は形を保つ）
    float len;                     // 元の骨の長さ（glb の extras.len）
    int tris;
    Vector3 *p, *nf, *ns;          // 三角の角ごとの位置（パーツの座標）、面の法線、頂点の法線（輪郭線用）
    unsigned char *cell;           // 三角ごとのパレットのマス（0〜255）
} Part;
static Part gPt[PT_MAX];
static int gPtN = 0;
static bool gPtTried = false, gPtOk = false;
static Color gPal[256];            // 初めの色（models/palette.png）

typedef struct { const char *name; int head, tail, side, thick; bool stretch; } PartMap;
static const PartMap PMAP[] = {
    { "pelvis", BJ_PELVIS, BJ_WAIST, 0, 3, true }, { "torso", BJ_WAIST, BJ_NECK, 1, 3, true },
    { "neck", BJ_NECK, BJ_HEADBASE, 1, 0, true }, { "head", BJ_HEADBASE, BJ_HEADTOP, 2, 0, false },
    { "upperarm.R", BJ_SH_R, BJ_EL_R, 1, 1, true }, { "forearm.R", BJ_EL_R, BJ_HD_R, 1, 1, true }, { "hand.R", BJ_HD_R, BJ_TIP_R, 1, 0, false },
    { "upperarm.L", BJ_SH_L, BJ_EL_L, 1, 1, true }, { "forearm.L", BJ_EL_L, BJ_HD_L, 1, 1, true }, { "hand.L", BJ_HD_L, BJ_TIP_L, 1, 0, false },
    { "thigh.R", BJ_HIP_R, BJ_KN_R, 0, 2, true }, { "shin.R", BJ_KN_R, BJ_AN_R, 0, 2, true }, { "shoe.R", BJ_AN_R, BJ_TOE_R, 3, 0, false },
    { "thigh.L", BJ_HIP_L, BJ_KN_L, 0, 2, true }, { "shin.L", BJ_KN_L, BJ_AN_L, 0, 2, true }, { "shoe.L", BJ_AN_L, BJ_TOE_L, 4, 0, false },
};
#define PMAP_N ((int)(sizeof(PMAP) / sizeof(PMAP[0])))

// パレットのマス（tools/make_player.py の PAL と同じ）
enum { PC_SKIN = 0, PC_SHIRT, PC_ACCENT, PC_PANTS, PC_SHOES, PC_SOCKS, PC_HAIR, PC_WRIST, PC_EYE, PC_SOLE };

static const char *FindFile(const char *rel)
{
    static char buf[512];
    const char *cands[3] = { rel, TextFormat("%s%s", GetApplicationDirectory(), rel), TextFormat("sweetspot/%s", rel) };
    for (int i = 0; i < 3; i++) if (FileExists(cands[i])) { snprintf(buf, sizeof(buf), "%s", cands[i]); return buf; }
    return NULL;
}

// 四元数 (x, y, z, w) → 3x3（列が軸）
static void QuatMat(float x, float y, float z, float w, float m[3][3])
{
    m[0][0] = 1 - 2 * (y * y + z * z); m[0][1] = 2 * (x * y - z * w);     m[0][2] = 2 * (x * z + y * w);
    m[1][0] = 2 * (x * y + z * w);     m[1][1] = 1 - 2 * (x * x + z * z); m[1][2] = 2 * (y * z - x * w);
    m[2][0] = 2 * (x * z - y * w);     m[2][1] = 2 * (y * z + x * w);     m[2][2] = 1 - 2 * (x * x + y * y);
}

bool PartsModelReady(void)
{
    if (gPtTried) return gPtOk;
    gPtTried = true;
    const char *found = FindFile(PT_PATH);
    if (!found) { TraceLog(LOG_WARNING, "PARTS MODEL: %s not found", PT_PATH); return false; }
    char path[512]; snprintf(path, sizeof(path), "%s", found);
    // 初めの色の表
    for (int i = 0; i < 256; i++) gPal[i] = (Color){ 200, 200, 200, 255 };
    const char *pp = FindFile("models/palette.png");
    if (pp) {
        Image im = LoadImage(pp);
        if (im.width >= 16 && im.height >= 16) for (int r = 0; r < 16; r++) for (int c = 0; c < 16; c++) gPal[r * 16 + c] = GetImageColor(im, c * im.width / 16, r * im.height / 16);
        UnloadImage(im);
    }
    // glb の JSON：オブジェクトの名前・位置・向き・大きさ・骨の長さ、メッシュごとの部分（primitive）の数
    int sz = 0; unsigned char *raw = LoadFileData(path, &sz);
    if (!raw || sz < 20 || memcmp(raw, "glTF", 4)) { if (raw) UnloadFileData(raw); TraceLog(LOG_WARNING, "PARTS MODEL: not a glb"); return false; }
    unsigned int jl = raw[12] | raw[13] << 8 | raw[14] << 16 | (unsigned)raw[15] << 24;
    if (20 + jl > (unsigned)sz) { UnloadFileData(raw); return false; }
    char *json = malloc(jl + 1); memcpy(json, raw + 20, jl); json[jl] = 0;
    UnloadFileData(raw);
    const char *jp = json; JNode *root = JParse(&jp); free(json);
    JNode *nodes = JGet(root, "nodes"), *meshes = JGet(root, "meshes");
    Model mdl = LoadModel(path);
    if (!nodes || nodes->t != JN_ARR || mdl.meshCount == 0) { JFree(root); UnloadModel(mdl); TraceLog(LOG_WARNING, "PARTS MODEL: no objects"); return false; }
    // raylib はオブジェクトの順に、メッシュの部分ごとに 1 つの Mesh を作る（三角の部分だけ）
    int mi = 0;
    for (int ni = 0; ni < nodes->n && mi < mdl.meshCount; ni++) {
        JNode *nd = nodes->kids[ni], *jm = JGet(nd, "mesh");
        if (!jm) continue;
        int nprim = 1;
        if (meshes && meshes->t == JN_ARR && (int)jm->num < meshes->n) { JNode *pr = JGet(meshes->kids[(int)jm->num], "primitives"); if (pr) nprim = pr->n; }
        JNode *jn = JGet(nd, "name");
        const char *nm = jn && jn->t == JN_STR ? jn->str : "";
        int map = -1; for (int k = 0; k < PMAP_N; k++) if (!strcmp(PMAP[k].name, nm)) map = k;
        if (map < 0 || gPtN >= PT_MAX || JGet(nd, "children")) { mi += nprim; continue; }   // 知らない名前・子を持つオブジェクトは使わない
        // オブジェクトの位置・向き・大きさ（焼き込まれた分を戻すため）
        JNode *jt = JGet(nd, "translation"), *jr = JGet(nd, "rotation"), *js = JGet(nd, "scale");
        Vector3 T = { 0 }, S = { 1, 1, 1 }; float R[3][3];
        if (jt && jt->n == 3) T = (Vector3){ JNum(jt->kids[0], 0), JNum(jt->kids[1], 0), JNum(jt->kids[2], 0) };
        if (js && js->n == 3) S = (Vector3){ JNum(js->kids[0], 1), JNum(js->kids[1], 1), JNum(js->kids[2], 1) };
        if (jr && jr->n == 4) QuatMat(JNum(jr->kids[0], 0), JNum(jr->kids[1], 0), JNum(jr->kids[2], 0), JNum(jr->kids[3], 1), R);
        else QuatMat(0, 0, 0, 1, R);
        Part *pt = &gPt[gPtN];
        memset(pt, 0, sizeof(*pt));
        snprintf(pt->name, sizeof(pt->name), "%s", nm);
        pt->head = PMAP[map].head; pt->tail = PMAP[map].tail; pt->side = PMAP[map].side; pt->thick = PMAP[map].thick; pt->stretch = PMAP[map].stretch;
        pt->len = JNum(JGet(JGet(nd, "extras"), "len"), 0.0f);
        int tris = 0; for (int q = 0; q < nprim; q++) tris += mdl.meshes[mi + q].triangleCount;
        pt->tris = tris;
        pt->p = malloc(sizeof(Vector3) * tris * 3); pt->nf = malloc(sizeof(Vector3) * tris); pt->ns = malloc(sizeof(Vector3) * tris * 3);
        pt->cell = malloc(tris);
        int t = 0;
        for (int q = 0; q < nprim; q++, mi++) {
            Mesh *m = &mdl.meshes[mi];
            for (int k = 0; k < m->triangleCount; k++, t++) {
                float u = 0.0f, v = 0.0f;
                for (int c = 0; c < 3; c++) {
                    int vi = m->indices ? m->indices[3 * k + c] : 3 * k + c;
                    Vector3 w = { m->vertices[3 * vi] - T.x, m->vertices[3 * vi + 1] - T.y, m->vertices[3 * vi + 2] - T.z };
                    // 焼き込まれた向きを戻す（R の転置）、大きさを戻す
                    Vector3 l = { R[0][0] * w.x + R[1][0] * w.y + R[2][0] * w.z, R[0][1] * w.x + R[1][1] * w.y + R[2][1] * w.z, R[0][2] * w.x + R[1][2] * w.y + R[2][2] * w.z };
                    l.x /= S.x != 0.0f ? S.x : 1.0f; l.y /= S.y != 0.0f ? S.y : 1.0f; l.z /= S.z != 0.0f ? S.z : 1.0f;
                    pt->p[3 * t + c] = l;
                    if (m->texcoords) { u += m->texcoords[2 * vi] / 3.0f; v += m->texcoords[2 * vi + 1] / 3.0f; }
                }
                Vector3 a = pt->p[3 * t], b = pt->p[3 * t + 1], cc = pt->p[3 * t + 2];
                pt->nf[t] = Vector3Normalize(Vector3CrossProduct(Vector3Subtract(b, a), Vector3Subtract(cc, a)));
                int col = Clamp((int)(u * 16.0f), 0, 15), row = Clamp((int)(v * 16.0f), 0, 15);
                pt->cell[t] = (unsigned char)(row * 16 + col);
            }
        }
        // 輪郭線用の頂点の法線：同じ位置の角の面の法線をならす
        for (int i = 0; i < tris * 3; i++) {
            Vector3 s = { 0 };
            for (int j = 0; j < tris * 3; j++) if (Vector3DistanceSqr(pt->p[i], pt->p[j]) < 1e-10f) s = Vector3Add(s, pt->nf[j / 3]);
            pt->ns[i] = Vector3Normalize(s);
        }
        if (pt->len <= 0.0f) pt->len = 0.3f;
        gPtN++;
    }
    JFree(root);
    UnloadModel(mdl);   // 頂点はパーツの座標に写したので、もう要らない
    int tot = 0; for (int i = 0; i < gPtN; i++) tot += gPt[i].tris;
    TraceLog(LOG_INFO, "PARTS MODEL: %s  parts %d  triangles %d  palette %s", path, gPtN, tot, pp ? "yes" : "no");
    gPtOk = gPtN > 0;
    return gPtOk;
}

// 選手ごとの色の表：models/palettes/<短い名前>.png があればそれ。なければ初めの表に、選手の色（肌・シャツ・パンツ・髪…）を入れる
static const Color *PaletteFor(const BodyPose *bp)
{
    static Color pal[256];
    static signed char state[PERSONA_COUNT];   // 0 = まだ見ていない、1 = ファイルなし、2 = ファイルの色
    static Color filePal[PERSONA_COUNT][256];
    int id = bp->lookId;
    if (id >= 0 && id < PERSONA_COUNT) {
        if (state[id] == 0) {
            state[id] = 1;
            const char *f = FindFile(TextFormat("models/palettes/%s.png", PERSONAS[id].shortName));
            if (f) {
                Image im = LoadImage(f);
                if (im.width >= 16 && im.height >= 16) {
                    for (int r = 0; r < 16; r++) for (int c = 0; c < 16; c++) filePal[id][r * 16 + c] = GetImageColor(im, c * im.width / 16, r * im.height / 16);
                    state[id] = 2;
                    TraceLog(LOG_INFO, "PARTS MODEL: palette %s", f);
                }
                UnloadImage(im);
            }
        }
        if (state[id] == 2) return filePal[id];
    }
    memcpy(pal, gPal, sizeof(pal));
    pal[PC_SKIN] = bp->col[BR_SKIN]; pal[PC_SHIRT] = bp->col[BR_SHIRT]; pal[PC_ACCENT] = bp->col[BR_ACCENT];
    pal[PC_PANTS] = bp->col[BR_SHORTS]; pal[PC_SHOES] = bp->col[BR_SHOES]; pal[PC_SOCKS] = bp->col[BR_SOCKS];
    pal[PC_HAIR] = bp->col[BR_HAIR]; pal[PC_WRIST] = bp->col[BR_WRIST]; pal[PC_EYE] = bp->col[BR_EYE]; pal[PC_SOLE] = bp->col[BR_SOLE];
    return pal;
}

void DrawPartsModel(const BodyPose *bp)
{
    if (!PartsModelReady()) return;
    const Color *pal = PaletteFor(bp);
    bool outline = LitOutlinePass();
    float sx = bp->facingNegZ ? 1.0f : -1.0f;
    #define TOW(v) ((Vector3){ bp->pos.x + sx * bp->mx * (v).x * bp->sc, bp->pos.y + (v).y * bp->sc, bp->pos.z - sx * (v).z * bp->sc })
    #define TON(v) ((Vector3){ sx * bp->mx * (v).x, (v).y, -sx * (v).z })
    bool flip = bp->mx > 0.0f;   // 体の座標 → 世界が鏡（行列式が負）なら三角の回りを逆に
    for (int i = 0; i < gPtN; i++) {
        const Part *pt = &gPt[i];
        Vector3 h = bp->j[pt->head], tl = bp->j[pt->tail];
        float m[3][3], len;
        BodyFrame(h, tl, bp->side[pt->side], bp->fwd, m, &len);
        float sy = pt->stretch ? Clamp(len / pt->len, 0.75f, 1.30f) : 1.0f;
        float k = pt->thick == 1 ? bp->thickArm : pt->thick == 2 ? bp->thickLeg : pt->thick == 3 ? bp->thickBody : 1.0f;
        for (int t0 = 0; t0 < pt->tris; t0 += 200) {
            int t1 = t0 + 200 < pt->tris ? t0 + 200 : pt->tris;
            rlCheckRenderBatchLimit((t1 - t0) * 3);
            rlBegin(RL_TRIANGLES);
            for (int t = t0; t < t1; t++) {
                Color col = pal[pt->cell[t]];
                Vector3 nf = pt->nf[t];
                Vector3 nfB = { m[0][0] * nf.x + m[0][1] * nf.y + m[0][2] * nf.z, m[1][0] * nf.x + m[1][1] * nf.y + m[1][2] * nf.z, m[2][0] * nf.x + m[2][1] * nf.y + m[2][2] * nf.z };
                Vector3 nfW = Vector3Normalize(TON(nfB));
                for (int c0 = 0; c0 < 3; c0++) {
                    int c = flip ? (c0 == 0 ? 0 : 3 - c0) : c0;
                    Vector3 l = pt->p[3 * t + c];
                    l.x *= k; l.z *= k; l.y *= sy;
                    Vector3 b = { h.x + m[0][0] * l.x + m[0][1] * l.y + m[0][2] * l.z,
                                  h.y + m[1][0] * l.x + m[1][1] * l.y + m[1][2] * l.z,
                                  h.z + m[2][0] * l.x + m[2][1] * l.y + m[2][2] * l.z };
                    Vector3 n = nfW;
                    if (outline) {
                        Vector3 ns = pt->ns[3 * t + c];
                        Vector3 nsB = { m[0][0] * ns.x + m[0][1] * ns.y + m[0][2] * ns.z, m[1][0] * ns.x + m[1][1] * ns.y + m[1][2] * ns.z, m[2][0] * ns.x + m[2][1] * ns.y + m[2][2] * ns.z };
                        n = Vector3Normalize(TON(nsB));
                    }
                    LitVertexExt(TOW(b), n, col);
                }
            }
            rlEnd();
        }
    }
    #undef TOW
    #undef TON
}
