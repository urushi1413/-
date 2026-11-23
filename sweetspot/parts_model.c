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
    int kind;                      // 0 = ふつう、1 = 顔の絵（face）、2〜5 = 髪型の候補（hair_short / wave / tied / buzz）
    float len;                     // 元の骨の長さ（glb の extras.len）
    int tris;
    Vector3 *p, *nc, *ns;          // 三角の角ごとの位置（パーツの座標）、法線（glb の。スムーズ・シャープエッジ込み）、輪郭線用の法線（同じ位置でならす）
    Vector2 *uv;                   // 角ごとの UV（顔の絵のときだけ）
    unsigned char *cell;           // 三角ごとのパレットのマス（0〜255）
} Part;
static Part gPt[PT_MAX];
static int gPtN = 0;
static bool gPtTried = false, gPtOk = false;
static Color gPal[256];            // 初めの色（models/palette.png）

typedef struct { const char *name; int head, tail, side, thick; bool stretch; int kind; } PartMap;
static const PartMap PMAP[] = {
    { "pelvis", BJ_PELVIS, BJ_WAIST, 0, 3, true }, { "torso", BJ_WAIST, BJ_NECK, 1, 3, true },
    { "neck", BJ_NECK, BJ_HEADBASE, 1, 0, true }, { "head", BJ_HEADBASE, BJ_HEADTOP, 2, 0, false },
    { "hair_short", BJ_HEADBASE, BJ_HEADTOP, 2, 0, false, 2 }, { "hair_wave", BJ_HEADBASE, BJ_HEADTOP, 2, 0, false, 3 },
    { "hair_tied", BJ_HEADBASE, BJ_HEADTOP, 2, 0, false, 4 }, { "hair_buzz", BJ_HEADBASE, BJ_HEADTOP, 2, 0, false, 5 },
    { "face", BJ_HEADBASE, BJ_HEADTOP, 2, 0, false, 1 },   // 顔の絵は最後に描く（髪の後。透けるところがあるので）
    { "upperarm.R", BJ_SH_R, BJ_EL_R, 1, 1, true }, { "forearm.R", BJ_EL_R, BJ_HD_R, 1, 1, true }, { "hand.R", BJ_HD_R, BJ_TIP_R, 1, 0, false },
    { "upperarm.L", BJ_SH_L, BJ_EL_L, 1, 1, true }, { "forearm.L", BJ_EL_L, BJ_HD_L, 1, 1, true }, { "hand.L", BJ_HD_L, BJ_TIP_L, 1, 0, false },
    { "thigh.R", BJ_HIP_R, BJ_KN_R, 0, 2, true }, { "shin.R", BJ_KN_R, BJ_AN_R, 0, 2, true }, { "shoe.R", BJ_AN_R, BJ_TOE_R, 3, 0, false },
    { "thigh.L", BJ_HIP_L, BJ_KN_L, 0, 2, true }, { "shin.L", BJ_KN_L, BJ_AN_L, 0, 2, true }, { "shoe.L", BJ_AN_L, BJ_TOE_L, 4, 0, false },
};
#define PMAP_N ((int)(sizeof(PMAP) / sizeof(PMAP[0])))

// パレットのマス（tools/make_player.py の PAL と同じ）
enum { PC_SKIN = 0, PC_SHIRT, PC_ACCENT, PC_PANTS, PC_SHOES, PC_SOCKS, PC_HAIR, PC_WRIST, PC_EYE, PC_SOLE, PC_NOSE, PC_EAR };

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
        pt->head = PMAP[map].head; pt->tail = PMAP[map].tail; pt->side = PMAP[map].side; pt->thick = PMAP[map].thick; pt->stretch = PMAP[map].stretch; pt->kind = PMAP[map].kind;
        pt->len = JNum(JGet(JGet(nd, "extras"), "len"), 0.0f);
        int tris = 0; for (int q = 0; q < nprim; q++) tris += mdl.meshes[mi + q].triangleCount;
        pt->tris = tris;
        pt->p = malloc(sizeof(Vector3) * tris * 3); pt->nc = malloc(sizeof(Vector3) * tris * 3); pt->ns = malloc(sizeof(Vector3) * tris * 3);
        pt->uv = pt->kind == 1 ? malloc(sizeof(Vector2) * tris * 3) : NULL;
        pt->cell = malloc(tris);
        Vector3 *nfTmp = malloc(sizeof(Vector3) * tris);
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
                    if (pt->uv) pt->uv[3 * t + c] = m->texcoords ? (Vector2){ m->texcoords[2 * vi], m->texcoords[2 * vi + 1] } : (Vector2){ 0 };
                    // 法線：焼き込まれた向きを戻す（大きさは 1 のはず）
                    Vector3 n = m->normals ? (Vector3){ m->normals[3 * vi], m->normals[3 * vi + 1], m->normals[3 * vi + 2] } : (Vector3){ 0, 0, 0 };
                    pt->nc[3 * t + c] = Vector3Normalize((Vector3){ R[0][0] * n.x + R[1][0] * n.y + R[2][0] * n.z, R[0][1] * n.x + R[1][1] * n.y + R[2][1] * n.z, R[0][2] * n.x + R[1][2] * n.y + R[2][2] * n.z });
                }
                Vector3 a = pt->p[3 * t], b = pt->p[3 * t + 1], cc = pt->p[3 * t + 2];
                nfTmp[t] = Vector3Normalize(Vector3CrossProduct(Vector3Subtract(b, a), Vector3Subtract(cc, a)));
                if (!m->normals) for (int c = 0; c < 3; c++) pt->nc[3 * t + c] = nfTmp[t];
                int col = Clamp((int)(u * 16.0f), 0, 15), row = Clamp((int)(v * 16.0f), 0, 15);
                pt->cell[t] = (unsigned char)(row * 16 + col);
            }
        }
        // 輪郭線用の頂点の法線：同じ位置の角の面の法線をならす
        for (int i = 0; i < tris * 3; i++) {
            Vector3 s = { 0 };
            for (int j = 0; j < tris * 3; j++) if (Vector3DistanceSqr(pt->p[i], pt->p[j]) < 1e-10f) s = Vector3Add(s, nfTmp[j / 3]);
            pt->ns[i] = Vector3Normalize(s);
        }
        free(nfTmp);
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
    {   // 肌：暖色寄りで少し彩度を上げる（血色）。鼻はごく薄い赤み（7%）
        Color sk = bp->col[BR_SKIN];
        Vector3 hsv = ColorToHSV(sk);
        if (hsv.x > 22.0f && hsv.x < 60.0f) hsv.x -= 3.0f;   // 黄みを少し赤みへ
        hsv.y = Clamp(hsv.y * 1.15f + 0.02f, 0.0f, 0.75f);
        hsv.z = Clamp(hsv.z * 1.02f, 0.0f, 1.0f);
        Color w = ColorFromHSV(hsv.x, hsv.y, hsv.z);
        pal[PC_SKIN] = w;
        pal[PC_EAR] = w;   // 耳（スキニングのモデル）：肌と同じ色。マスを分けてあるのは、測るときに色で見分けるため
        pal[PC_NOSE] = (Color){ (unsigned char)(w.r + (220 - w.r) * 0.07f), (unsigned char)(w.g + (80 - w.g) * 0.07f), (unsigned char)(w.b + (70 - w.b) * 0.07f), 255 };
    }
 pal[PC_SHIRT] = bp->col[BR_SHIRT]; pal[PC_ACCENT] = bp->col[BR_ACCENT];
    pal[PC_PANTS] = bp->col[BR_SHORTS]; pal[PC_SHOES] = bp->col[BR_SHOES]; pal[PC_SOCKS] = bp->col[BR_SOCKS];
    pal[PC_HAIR] = bp->col[BR_HAIR]; pal[PC_WRIST] = bp->col[BR_WRIST]; pal[PC_EYE] = bp->col[BR_EYE]; pal[PC_SOLE] = bp->col[BR_SOLE];
    return pal;
}

// ---- 選手ごとの髪型・顔の絵・色：models/players.txt ----
//   「番号 hair=short|wave|tied|buzz face=01 skin=#rrggbb eye=#rrggbb haircolor=#rrggbb beard=0|1|2」（どれも省略できる）。
//   書いていないものは、ゲームの選手の値（髪型・男女・肌・瞳・髪の色・ひげ）から決める
typedef struct { signed char hair, face, beard; Color skin, eye, hairCol; } PlayerLook;
static PlayerLook gLookCfg[PERSONA_COUNT];
static bool gLookLoaded = false;
static Color HexCol(const char *p) { unsigned int v = 0; if (*p == '#') p++; sscanf(p, "%6x", &v); return (Color){ (unsigned char)(v >> 16), (unsigned char)(v >> 8), (unsigned char)v, 255 }; }
static void LoadLookConfig(void)
{
    gLookLoaded = true;
    for (int i = 0; i < PERSONA_COUNT; i++) gLookCfg[i] = (PlayerLook){ -1, -1, -1, { 0 }, { 0 }, { 0 } };
    const char *f = FindFile("models/players.txt");
    if (!f) return;
    char *txt = LoadFileText(f);
    if (!txt) return;
    for (char *line = strtok(txt, "\n"); line; line = strtok(NULL, "\n")) {
        char *hash = strchr(line, '#');
        while (hash && hash > line && hash[-1] == '=') hash = strchr(hash + 1, '#');   // 「=#rrggbb」の # はメモではない
        if (hash) *hash = 0;
        int id = -1; if (sscanf(line, " %d", &id) != 1 || id < 0 || id >= PERSONA_COUNT) continue;
        PlayerLook *L = &gLookCfg[id];
        char *h = strstr(line, "hair="), *fc = strstr(line, "face="), *sk = strstr(line, "skin="), *ey = strstr(line, "eye="), *hc = strstr(line, "haircolor="), *bd = strstr(line, "beard=");
        if (h) { h += 5; L->hair = !strncmp(h, "short", 5) ? 0 : !strncmp(h, "wave", 4) ? 1 : !strncmp(h, "tied", 4) ? 2 : !strncmp(h, "buzz", 4) ? 3 : -1; }
        if (fc) L->face = (signed char)Clamp(atoi(fc + 5), 1, 99);
        if (sk) L->skin = HexCol(sk + 5);
        if (ey) L->eye = HexCol(ey + 4);
        if (hc) L->hairCol = HexCol(hc + 10);
        if (bd) L->beard = (signed char)Clamp(atoi(bd + 6), 0, 2);
    }
    UnloadFileText(txt);
}
static const PlayerLook *LookOf(const BodyPose *bp)
{
    static const PlayerLook none = { -1, -1, -1, { 0 }, { 0 }, { 0 } };
    if (!gLookLoaded) LoadLookConfig();
    return (bp->lookId >= 0 && bp->lookId < PERSONA_COUNT) ? &gLookCfg[bp->lookId] : &none;
}
static int HairFor(const BodyPose *bp)   // 0 短髪 1 ウェーブ 2 束ねる 3 坊主寄り
{
    const PlayerLook *L = LookOf(bp);
    if (L->hair >= 0) return L->hair;
    switch (bp->hairStyle) {
        case HS_CURLY: case HS_CURLY_SHORT: case HS_NADAL: return 1;
        case HS_PONY: case HS_BUN: return 2;
        case HS_FADE: return 3;
        default: return 0;
    }
}
// 顔の絵を選手ごとに組み立てる：ひげ（髪の色を混ぜる）の上に顔の絵、瞳の色を入れる。同じ組み合わせは使い回す
//   dir：顔の絵のフォルダ（スキニングのモデルは models/players/faces。なければ models/faces）
typedef struct { int face, beard; Color eye, hair; const char *dir; Texture2D tex; } FaceEntry;
static FaceEntry gFaces[48]; static int gFaceN = 0;
static Texture2D FaceTexIn(const BodyPose *bp, const char *dir)
{
    const PlayerLook *L = LookOf(bp);
    int face = L->face > 0 ? L->face : (bp->female ? 3 : 1);
    int beard = L->beard >= 0 ? L->beard : 0;   // 剃り跡・ひげは初めはなし。players.txt の beard=1（剃り跡）・2（ひげ）で選手ごとに付ける
    Color eye = bp->eyeCol, hair = bp->col[BR_HAIR];
    for (int i = 0; i < gFaceN; i++) {
        FaceEntry *e = &gFaces[i];
        if (e->face == face && e->beard == beard && !strcmp(e->dir, dir) && ColorToInt(e->eye) == ColorToInt(eye) && ColorToInt(e->hair) == ColorToInt(hair)) return e->tex;
    }
    const char *f = FindFile(TextFormat("%s/face_%02d.png", dir, face));
    if (!f) f = FindFile(TextFormat("%s/face_01.png", dir));
    if (!f && strcmp(dir, "models/faces") != 0) return FaceTexIn(bp, "models/faces");
    if (!f || gFaceN >= 48) return (Texture2D){ 0 };
    Image im = LoadImage(f);
    ImageFormat(&im, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
    Color *px = (Color *)im.data;
    for (int i = 0; i < im.width * im.height; i++) {   // 瞳：決まった色（赤紫）のところを、選手の瞳の色に
        Color c = px[i];
        int key = (c.r < c.b ? c.r : c.b) - c.g;   // 赤紫らしさ
        if (c.a > 0 && key > 60) {
            float k = Clamp((key - 60) / 140.0f, 0.0f, 1.0f);
            px[i].r = (unsigned char)(c.r + (eye.r - c.r) * k); px[i].g = (unsigned char)(c.g + (eye.g - c.g) * k); px[i].b = (unsigned char)(c.b + (eye.b - c.b) * k);
        }
    }
    Image out = im;
    if (beard > 0) {   // 剃り跡（絵の青灰色のまま）・ひげ（髪の色を暗くした色）を、顔の絵の下に
        const char *bf = FindFile(TextFormat("%s/beard_%d.png", dir, beard));
        if (bf) {
            Image bi = LoadImage(bf);
            ImageFormat(&bi, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
            Color *bp2 = (Color *)bi.data;
            if (beard == 2) for (int i = 0; i < bi.width * bi.height; i++) { bp2[i].r = (unsigned char)(hair.r * 0.55f); bp2[i].g = (unsigned char)(hair.g * 0.55f); bp2[i].b = (unsigned char)(hair.b * 0.55f); }
            if (bi.width != im.width || bi.height != im.height) ImageResize(&bi, im.width, im.height);
            {   // 顔の絵をひげの上に重ねる（アルファで合成。raylib の版で関数の名前が違うので自分で）
                Color *d = (Color *)bi.data, *sp = (Color *)im.data;
                for (int i = 0; i < im.width * im.height; i++) {
                    float sa = sp[i].a / 255.0f, da = d[i].a / 255.0f, oa = sa + da * (1.0f - sa);
                    if (oa <= 0.0f) continue;
                    d[i].r = (unsigned char)((sp[i].r * sa + d[i].r * da * (1.0f - sa)) / oa);
                    d[i].g = (unsigned char)((sp[i].g * sa + d[i].g * da * (1.0f - sa)) / oa);
                    d[i].b = (unsigned char)((sp[i].b * sa + d[i].b * da * (1.0f - sa)) / oa);
                    d[i].a = (unsigned char)(oa * 255.0f);
                }
            }
            UnloadImage(im);
            out = bi;
        }
    }
    Texture2D t = LoadTextureFromImage(out);
    UnloadImage(out);
    SetTextureFilter(t, TEXTURE_FILTER_BILINEAR);
    gFaces[gFaceN++] = (FaceEntry){ face, beard, eye, hair, dir, t };
    return t;
}

void DrawPartsModel(const BodyPose *bpIn)
{
    if (!PartsModelReady()) return;
    BodyPose bpL = *bpIn;   // 選手ごとの色（models/players.txt）を上書きした写し
    const BodyPose *bp = &bpL;
    {
        const PlayerLook *L = LookOf(bpIn);
        if (L->skin.a) bpL.col[BR_SKIN] = L->skin;
        if (L->eye.a) bpL.eyeCol = L->eye;
        if (L->hairCol.a) bpL.col[BR_HAIR] = L->hairCol;
    }
    const Color *pal = PaletteFor(bp);
    bool outline = LitOutlinePass();
    int hair = HairFor(bp);
    float sx = bp->facingNegZ ? 1.0f : -1.0f;
    #define TOW(v) ((Vector3){ bp->pos.x + sx * bp->mx * (v).x * bp->sc, bp->pos.y + (v).y * bp->sc, bp->pos.z - sx * (v).z * bp->sc })
    #define TON(v) ((Vector3){ sx * bp->mx * (v).x, (v).y, -sx * (v).z })
    #define ROT(v) ((Vector3){ m[0][0] * (v).x + m[0][1] * (v).y + m[0][2] * (v).z, m[1][0] * (v).x + m[1][1] * (v).y + m[1][2] * (v).z, m[2][0] * (v).x + m[2][1] * (v).y + m[2][2] * (v).z })
    bool flip = bp->mx > 0.0f;   // 体の座標 → 世界が鏡（行列式が負）なら三角の回りを逆に
    for (int pass = 0; pass < 2; pass++)   // 顔の絵（透けるところがある）は、ほかを全部描いてから
    for (int i = 0; i < gPtN; i++) {
        const Part *pt = &gPt[i];
        if ((pt->kind == 1) != (pass == 1)) continue;
        if (pt->kind >= 2 && pt->kind - 2 != hair) continue;   // ほかの髪型
        Texture2D ftex = { 0 };
        if (pt->kind == 1) {   // 顔の絵：輪郭線の回では描かない。絵がなければ描かない
            if (outline) continue;
            ftex = FaceTexIn(bp, "models/faces");
            if (!ftex.id) continue;
            rlDrawRenderBatchActive(); rlDisableDepthMask();
        }
        Vector3 h = bp->j[pt->head], tl = bp->j[pt->tail];
        float m[3][3], len;
        BodyFrame(h, tl, bp->side[pt->side], bp->fwd, m, &len);
        float sy = pt->stretch ? Clamp(len / pt->len, 0.75f, 1.30f) : 1.0f;
        float k = pt->thick == 1 ? bp->thickArm : pt->thick == 2 ? bp->thickLeg : pt->thick == 3 ? bp->thickBody : 1.0f;
        for (int t0 = 0; t0 < pt->tris; t0 += 200) {
            int t1 = t0 + 200 < pt->tris ? t0 + 200 : pt->tris;
            rlCheckRenderBatchLimit((t1 - t0) * 3);
            rlBegin(RL_TRIANGLES);
            if (ftex.id) rlSetTexture(ftex.id);   // rlBegin は形の種類が変わると絵を既定に戻すので、その後で
            for (int t = t0; t < t1; t++) {
                Color col = pt->kind == 1 ? WHITE : pal[pt->cell[t]];
                for (int c0 = 0; c0 < 3; c0++) {
                    int c = flip ? (c0 == 0 ? 0 : 3 - c0) : c0;
                    Vector3 l = pt->p[3 * t + c];
                    l.x *= k; l.z *= k; l.y *= sy;
                    Vector3 b = ROT(l); b = Vector3Add(b, h);
                    Vector3 nl = outline ? pt->ns[3 * t + c] : pt->nc[3 * t + c];
                    Vector3 nb = ROT(nl);
                    Vector3 n = Vector3Normalize(TON(nb));
                    if (pt->uv) rlTexCoord2f(pt->uv[3 * t + c].x, pt->uv[3 * t + c].y);
                    LitVertexExt(TOW(b), n, col);
                }
            }
            rlEnd();
        }
        if (pt->kind == 1) { rlDrawRenderBatchActive(); rlSetTexture(0); rlEnableDepthMask(); }
    }
    #undef TOW
    #undef TON
    #undef ROT
}

// スキニングのモデル（body_model.c）からも使う
const Color *PartsPaletteFor(const BodyPose *bp) { return PaletteFor(bp); }
Texture2D PartsFaceTex(const BodyPose *bp) { return FaceTexIn(bp, "models/faces"); }
Texture2D PartsFaceTexIn(const BodyPose *bp, const char *dir) { return FaceTexIn(bp, dir); }
void PartsApplyLook(BodyPose *bp)
{
    const PlayerLook *L = LookOf(bp);
    if (L->skin.a) bp->col[BR_SKIN] = L->skin;
    if (L->eye.a) bp->eyeCol = L->eye;
    if (L->hairCol.a) bp->col[BR_HAIR] = L->hairCol;
}
