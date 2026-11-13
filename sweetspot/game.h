// SWEET SPOT — 共通の定数・型・関数の宣言
// 単位はすべてメートル・秒・ラジアン。座標系: x=横, y=上, z=縦（自分側が +z、相手側が -z）
#ifndef SWEETSPOT_GAME_H
#define SWEETSPOT_GAME_H

#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"
#include <math.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

// SWEET SPOT — 打点と球種がちゃんと結果に効く、球足の速いリアル系テニス
// AI と試合をする（練習モード：ラリー・リターン・サーブもある）。
// 打点（タイミング・体からの距離・高さ）の良し悪しで、球速・回転・コースのずれが決まる。
// 単位はすべてメートル・秒・ラジアン。座標系: x=横, y=上, z=縦（自分側が +z、相手側が -z）

// ---- コート寸法（ITF 規格） ----
#define COURT_LEN        23.77f
#define HALF_LEN         (COURT_LEN * 0.5f)
#define SINGLES_W        8.23f
#define DOUBLES_W        10.97f
#define SERVICE_Z        6.40f              // ネットからサービスラインまで
#define NET_H_CENTER     0.914f
#define NET_H_POST       1.07f
#define NET_POST_X       (DOUBLES_W * 0.5f + 0.914f)
#define LINE_W           0.05f

// ---- ボールの物理 ----
#define BALL_R           0.0335f
#define BALL_MASS        0.057f
#define GRAVITY          9.81f
#define AIR_RHO          1.21f
#define DRAG_CD          0.55f
#define BALL_AREA        (PI * BALL_R * BALL_R)
#define K_AIR            (0.5f * AIR_RHO * BALL_AREA / BALL_MASS)   // 空気の力 = K_AIR * 係数 * 速さ^2
#define SPIN_DECAY_TAU   4.0f    // 回転が空気で弱まる時定数（秒）

// ---- コートの種類 ----
// 摩擦（mu）：バウンドで横の速さがどれだけ落ちるか。大きいほど遅いコート
// 反発（e）  ：どれだけ跳ねるか
// ITF のコート速度（Court Pace Rating）= 100(1 - mu) + 150(e - 0.81) で、クレー 約 19（遅い）、ハード 約 34、芝 約 45（速い）
typedef enum { SURF_HARD, SURF_CLAY, SURF_GRASS, SURF_COUNT } SurfaceType;
typedef struct {
    const char *name;
    float mu, e;
    float keep;           // 表面のめり込みで、回転に関係なく横の速さがこれだけ残る（クレーは柔らかいので減る）
    float slideDecel;     // 滑るときの減速（m/s^2）。小さいほど長く滑る
    float slideMin;       // これより速く走っていると滑れる
    Color court, outside, lines;
} Surface;

#define BOUNCE_E         (SURFACES[gSurface].e)    // 反発（ハードは 0.77：空気抵抗込みで 2.54m から落として 1.40m、規格 1.35〜1.47m）
#define COURT_MU         (SURFACES[gSurface].mu)   // 摩擦
#define ROLL_FRICTION    1.5f    // 転がり中の減速（1/秒）

#define RPM_TO_RADS      (2.0f * PI / 60.0f)
#define PHYS_DT          (1.0f / 240.0f)   // 物理は固定ステップで回す（速い球でもすり抜けない）
#define TRAIL_N          900
#define CHARGE_TIME      0.5f              // 押す長さ：これだけ押すとベースライン近くまで深く（強さも最大）
#define HOLD_MAX         1.0f              // 押し続けても一番深いところで止まる（ボールを待つ間押していてもアウトにはならない）
#define HOLD_DEEP_MARGIN 0.5f              // 一番深いところは、球種の一番深い狙いより 0.5m 浅く（ベースラインの約 2m 手前）
#define HOLD_POWER_BASE  0.60f             // 押す長さ 0 でも、強さはこの割合から（強い球に溜めは要らない）
#define SIT_H0           1.10f   // 上から叩ける球：この高さから効き始め
#define SIT_H1           1.70f   //   この高さで最大
#define SIT_BONUS        0.60f   //   当たりの悪さを最大でこの割合だけ取り戻す（質 30% → 72%）
#define HOLD_POWER_MAX   0.85f             // 押しきったときの強さ（全力で振り切るのとは違う）

// ---- 打点（右利き。体の右がフォア、左がバック） ----
#define PLAYER_SPEED     6.0f    // 走る速さ（m/s）
#define PLAYER_ACCEL     9.0f    // 加速（m/s^2）。最高速まで 0.67 秒
#define PLAYER_BRAKE     20.0f   // 減速（m/s^2）。最高速から 0.3 秒・0.9m で止まる
#define CHARGE_MOVE      1.00f   // 溜めている（テイクバックしている）間の走る速さの割合
// スライド：溜めながら速く走っていて減速・切り返しをすると、足を滑らせながら打てる（止まる必要がない）
#define SLIDE_MIN_SPEED  (SURFACES[gSurface].slideMin)     // これより速く走っているとスライドになる（クレーは遅くても滑れる）
#define SLIDE_DECEL      (SURFACES[gSurface].slideDecel)   // 滑っている間の減速。ハード 7（6m/s から 2.6m）、クレー 4.5（4m）、芝 9
#define SLIDE_END_SPEED  0.8f
#define SLIDE_REACH      0.30f   // スライド中は低く広い構えになり、リーチがこれだけ伸びる
#define NO_BRAKE         1e6f    // TravelTime に渡すと「止まらずに滑り込む」計算になる
// 打点は「体のまわりの立体的な範囲」で考える。上から見て、体の中心からボールまでの
//   距離 r と、向き θ（真横が 0°、真正面が 90°、後ろがマイナス）で評価する
#define IDEAL_FWD        0.40f   // 理想の打点：体より 40cm 前
#define IDEAL_SIDE       0.85f   // 理想の打点：体から横に 85cm
#define IDEAL_R          0.94f   //   → 体からの距離 94cm
#define IDEAL_ANGLE      25.0f   //   → 向き 25°（横やや前）
#define VOLLEY_ANGLE     40.0f   // ボレーはもっと前で触る
#define TOL_ANG_COMPACT  55.0f   // タイミング（向き）の許容幅。溜めなしのコンパクトなスイング
#define TOL_ANG_FULL     40.0f   //   〃 フルスイング。大きく振るほどシビア
#define VOLLEY_TOL_BONUS 1.30f   // ボレーはテイクバックが小さいので許容幅が広い
#define BEHIND_LIMIT    -75.0f   // これより後ろに入られたら振り遅れ（空振り）。それより手前なら、遅れても押し出すように当たる
// 振っている途中でもラケットはボールに合わせにいく（人間は振りながら微調整している）。
// 早く振っても、この時間先までなら当たる。ただし早かった分だけ質が落ち、引っ張り気味になる
#define ADAPT_EARLY      0.20f   // 秒
#define ADAPT_PENALTY_T  0.30f   // この時間早いと、タイミングの質が最低（10%）になる
#define REACH_SIDE       2.10f   // これ以上遠いと届かない。腕＋ラケット 1.6m ＋最後の踏み込み 0.5m
#define REACH_DIVE       2.80f   // 走っている勢いで飛びつけば、ここまで届く（打った後は倒れる）
#define DIVE_MIN_SPEED   2.5f    // 飛びつくには、ボールの方向にこの速さ（m/s）以上で走っている必要がある
#define DIVE_QUALITY     0.35f   // 飛びついた球の質は 35% に落ちる
#define DIVE_DOWN_TIME   1.10f   // 飛びついた後、起き上がるまでの時間（秒）
#define OVERHEAD_MIN_H   1.85f   // これより高い球は頭上で打つ（スマッシュ・ハイボレー）
#define OVERHEAD_IDEAL_H 2.55f   // 頭上の理想の打点
#define OVERHEAD_TOL_H   0.70f
#define OVERHEAD_TOL_H_LOW 1.20f // 理想より低い頭上の球（2m 前後）は、少し低くても叩ける
#define OVERHEAD_MAX_H   3.05f   // ジャンプして届く高さ
#define OVERHEAD_FWD     0.45f   // 頭上の理想の打点：体より少し前、少し右
#define OVERHEAD_SIDE    0.30f
#define OVERHEAD_TOL_R   0.90f

// ---- 難易度：本格（REAL）/ 簡単（CASUAL）----
// 簡単モードは「ボールの近くなら返る」：届く範囲と許容幅を広げ、当たりの質に下限をつけ、ぶれを小さくする。
// 早めに離しても、一番いい瞬間まで待って振る。AI には効かない（自分の打球を評価するときだけ）
typedef enum { STYLE_REAL, STYLE_CASUAL, STYLE_COUNT } PlayStyle;

#define CASUAL_REACH     0.50f   // 届く範囲 +50cm
#define CASUAL_TOL       1.8f    // タイミング・距離の許容幅 1.8 倍
#define CASUAL_TOL_H     1.6f    // 高さの許容幅 1.6 倍
#define CASUAL_QFLOOR    0.35f   // 当たりの質は最低 35%
#define CASUAL_ERR       0.45f   // ぶれ 45%
#define CASUAL_WAIT      1.00f   // 早く離したとき、この時間まで先の一番いい瞬間を待って振る
// タイミングのずれによる方向ずれ。早いと引っ張り、遅いと流れる。
// コースはマウスの印で決める。人間の手で揃えられる ±30ms の中では方向は変わらず、それを超えた分だけずれる
#define TIMING_PULL_DEAD_MS 30.0f
#define TIMING_PULL_DEG_MS  0.03f  // 30ms を超えた 1ms ごとに 0.03°（100ms 遅れで約 2°＝向こうのベースラインで約 0.9m）

// ---- 来た球の勢い ----
// ラケットに当たったボールの速さ ≒ EA × 来た球の速さ ＋ (1 + EA) × ラケットの速さ
// （EA はラケットとボールの「見かけの反発係数」。実測で 0.4 前後）
// 球種表の km/h は「REF_IN_KMH の球を打ち返したとき」の値として、そこからラケットの速さを逆算する
#define RACKET_EA        0.40f
#define REF_IN_KMH       60.0f
#define VOLLEY_POWER     0.60f   // ボレーは大きく振らない（パンチ）ので、自分の力は 6 割。スマッシュ（頭上）は全力で振るので減らさない

// ---- 打球のばらつき（標準偏差、度）----
// 実際のばらつき = (BASE + (1 - 質)^2 * Q) × 勢いの倍率
// 2 乗にしているのは、普通にいい当たり（質 60% 前後）ではぶれが小さく、悪い当たりで急に大きくなるように
// 勢いの倍率：ラケット面が δ ずれると、跳ね返る成分は 2δ、自分で振った成分は δ ずれる。
//            なので来た球が速いほど、同じ面のずれでも狙いが大きく狂う（1〜2 倍）
#define ERR_YAW_BASE     0.5f
#define ERR_YAW_Q        2.5f
#define ERR_PITCH_BASE   0.2f
#define ERR_PITCH_Q      3.5f
// 芯を外したときのラケットのねじれ。来た球が重い（速い）ほど大きい
// 倍率 = 1 + (1 - 質) × (来た球の速さ - TWIST_FREE_KMH) / TWIST_KMH
// ラリーの普通の球（45km/h 以下）では効かず、速いサーブや強打で効く
#define TWIST_FREE_KMH   45.0f
#define TWIST_KMH        18.0f

// ---- 踏み込み ----
// 前に踏み込みながら打つと、体重が乗って球が重くなり、ラケットもぶれにくい。下がりながらだと逆
#define STEP_FULL        3.0f    // この速さ（m/s）で踏み込むと効果が最大
#define STEP_POWER_KMH   12.0f   // 最大の踏み込みでラケットの速さに上乗せされる量
#define STEP_STABILITY   0.35f   // 最大の踏み込みで、ねじれ・ばらつきが 35% 減る（下がると 35% 増える）

// ---- サーブ ----
#define SERVE_REACH      2.80f   // 腕とラケットを伸ばした打点の高さ
#define SERVE_IDEAL_H    2.90f   // これ以上の高さで打てば満点（腕を伸ばしきった一番高いところ）。低いと減点
#define SERVE_CHARGE_TIME 0.45f  // サーブの溜めが最大になるまで（トスの一番上までに溜めきれる）
// サーブの深さ：トスを上げてから離すまでの時間で決まる（0.30 秒で浅い、0.70 秒でサービスラインの 0.8m 外）
#define SERVE_DEPTH_T0    0.30f
#define SERVE_DEPTH_T1    0.70f
#define SERVE_DEPTH_SHORT (-4.0f)
#define SERVE_DEPTH_LONG  (-7.2f)
#define AI_TOSS_HIT_T     0.52f   // AI はトスを上げてからこの時間で打つ（一番上を少し過ぎたところ）
#define SERVE_TOL_H      0.30f   // 打点の高さがこれだけずれると質 0
#define SERVE_OVERREACH  0.25f   // 理想の打点よりこれだけ高い球まで、背伸びして届く。トスの最高点（2.95m）は必ず届く
#define TOSS_SPEED       4.55f   // トスの初速（上向き、m/s）。手から 1.9m で離すと最高点 2.95m（届く高さの中）
#define TOSS_SPREAD      0.06f   // トスのぶれ（標準偏差、m）。悪いトスは見送れる（押しっぱなしで落とす）
#define TOSS_TOL         0.50f   // トスがこれだけずれると質 0

typedef enum { RESULT_NONE, RESULT_IN, RESULT_OUT, RESULT_NET } ShotResult;
// 前の3つが通常ショット、後ろの3つが Shift を押しながら打つタッチショット
typedef enum {
    SHOT_FLAT, SHOT_TOP, SHOT_SLICE,             // L / J / K
    SHOT_SLICE_LOB, SHOT_TOP_LOB, SHOT_DROP,     // Shift + L / J / K
    SHOT_COUNT
} ShotType;
#define SHOT_NORMAL_COUNT 3
typedef enum { HITTER_NONE, HITTER_OPP, HITTER_PLAYER } Hitter;   // OPP = 相手（球出しマシンか AI）

// 球種ごとの性格。溜め 0→1 で min→max に変わる
// rpm はプラスがトップスピン、マイナスがバックスピン（スライス）
// idealHeight / tolHeight: その球種が一番打ちやすい打点の高さと、許容幅
// depth / shortDepth / deepDepth: 狙う深さ（何も押さない / S / W）
// highArc: 同じ着地点に落ちる2つの弾道のうち、高い方を選ぶ（守りのロブ）
// touchAngle: 0 以外なら、打ち出し角をこの角度に固定して「届く速さ」を逆算する（ドロップ）。
//             このとき min/maxKmh は使わず、溜めは回転量だけに効く
typedef struct {
    const char *name;
    float minKmh, maxKmh;
    float minRpm, maxRpm;
    float idealHeight, tolHeight;
    float tolSide;          // 体からの距離の許容幅。スライスは腕を伸ばしても打てるので広い
    float depth, shortDepth, deepDepth;
    bool  highArc;
    float touchAngle;
    Color color;
    float tolTiming;        // タイミングの許容幅の倍率（スライスは面を合わせるだけなので広い、フラットは狭い）
    float reach;            // リーチの上乗せ（m）。スライスは片手で腕を伸ばして届く
    float highBand;         // 理想の高さより上でも質が落ちない幅（m）。フラットは肩の高さから上から叩ける、トップスピンは肩より上だと振り上げにくい
} ShotSpec;

typedef struct {
    Vector3 pos;
    Vector3 vel;
    Vector3 spin;          // 角速度ベクトル（rad/s）。向き＝回転軸、長さ＝回転の速さ
    bool    active;
    bool    hitNet;
    int     side;          // 狙った側：-1=相手コート(z<0)、+1=自分のコート(z>0)
    int     shotId;        // 自分の打球の通し番号（プレイ記録で結果と結び付ける）
    int     bounces;
    float   netClearance;  // ネット上を通過したときの余裕（m）。マイナスならネットに当たった
    float   peakAfterFirstBounce;
    ShotResult result;
    Hitter  lastHitter;
    float   age;           // 打たれてからの時間（秒）
    bool    isServe;       // サーブならサービスボックスで判定する
    float   boxX0, boxX1;  // サービスボックスの横の範囲
} Ball;

// 打点の評価結果
typedef enum { MISS_NONE, MISS_EARLY, MISS_LATE, MISS_FAR, MISS_HIGH, MISS_LOW, MISS_NO_BALL } MissReason;
typedef struct {
    MissReason miss;
    bool  forehand;
    bool  overhead;    // 頭上で打った（スマッシュ・ハイボレー）
    bool  dive;        // 飛びついて打った
    float angleDeg;    // 上から見たボールの向き（真横 0°、正面 90°）
    float fwdErr;      // プラス＝早い（ボールがまだ前）、マイナス＝遅い（食い込まれた）
    float timeErrMs;   // fwdErr を時間に直したもの（参考表示用）
    float inKmh;       // 来た球の速さ
    float easeKmh;     // 「遅い球なので余裕がある」の判定に使う速さ（サーブのリターンは反応で打つので余裕なし扱い）
    bool  volley;      // ノーバウンドで打った
    float stepIn;      // 打つ瞬間に前（ネット方向）へ踏み込んでいた速さ（m/s）。マイナスは下がりながら
    float side;        // 体からの横距離
    float height;      // 打点の高さ
    float vyBall;      // 打った瞬間のボールの上下の速さ（プラス＝上がり際＝ライジング）
    float qTiming, qDist, qHeight;
    float quality;     // 0〜1。3つの積
} Contact;

// 見た目用：体とラケットの動き
typedef struct {
    float   swingT;        // 振ってからの時間（秒）。マイナスなら振っていない
    Contact lastContact;   // 最後のスイングの評価
    Vector3 contactLocal;  // 振った瞬間のボールの位置（体から見た座標：x=右, y=高さ, z=前）
    float   downT;         // 飛びついて倒れている残り時間
    float   windup;        // 溜め（0〜1）
    int     windStroke;    // 構え：0=フォア 1=バック 2=頭上
    float   runPhase;      // 走りの足の動き
    bool    sliding;       // 滑っている（低く広い構え）
    float   slideSide;     // 滑っている向き（体から見て右 +1、左 -1）
} SwingAnim;

// ---- 定数（ほかのファイルから移したもの） ----
#define MIN_ANGLE  (-15.0f * DEG2RAD)
#define MAX_ANGLE  ( 75.0f * DEG2RAD)
#define SWING_TIME 0.30f   // フォロースルーまでの時間（見た目）
#define LOG_RING 256
#define LOG_HEADER "time_s,mode,ai_level,event,shot,charge,stroke,volley,dive,miss,quality,q_timing,q_dist,q_height," \
                   "angle_deg,dist_m,height_m,timing_offset_ms,best_quality_here,in_kmh,out_kmh,step_in," \
                   "player_x,player_z,player_speed,ball_x,ball_y,ball_z,ball_age_s,first_move_s,charge_start_s,result,assist," \
                   "launch_deg,apex_m,style,court,ball_vy"
#define AIM_SENS 0.015f   // マウス 1 ピクセルで狙いが動く距離（m）

// ---- 型（ほかのファイルから移したもの） ----
typedef enum { SERVE_FLAT, SERVE_SLICE, SERVE_KICK, SERVE_COUNT } ServeType;

typedef struct {
    const char *name;
    float minKmh, maxKmh;
    float top0, top1, side0, side1, gyro0, gyro1;   // 溜め 0 → 1
    Color color;
} ServeSpec;

typedef struct {
    const char *name;
    float speed;          // 走る速さ（m/s）
    float reaction;       // 相手が打ってから動き出すまで（秒）
    float timingSigmaMs;  // スイングのタイミングのばらつき
    float posErr;         // 立ち位置のずれ（m）
    float aggression;     // 0〜1。強打・コースの厳しさ
    float lineJudge;      // ライン際の球の見極めのあまさ（m）。これより際どいアウトは打ってしまうことがある
    float serve1Kmh;      // ファーストサーブの速さの目安
    float serve2Kmh;      // セカンドサーブの速さの目安（キック）
    float serveSkill;     // サーブの安定感（打点の質に掛ける。低いほどぶれて入りにくい）
} AiLevel;

typedef struct {
    Vector3 pos;          // 足元（ワールド座標）
    Vector3 vel;
    Vector3 standPos;     // 向かっている位置（ワールド座標）
    bool    planned;      // 打つ計画がある
    bool    leaving;      // アウトと判断して見送る
    bool    swung;        // この球にはもう振った（空振りでも振り直さない）
    float   swingAge;     // ボールの age がこの値になったら振る
    float   seeTimer;     // 相手が打ってから反応するまでの残り時間
    Hitter  watching;     // 最後に見た打者（打者が変わったら反応を始める）
    int     level;
    float   downTimer;    // 飛びついて倒れている間は動けない
    bool    sliding;      // 滑り込んでいる
    bool    netRush;      // ネットに出ている（出ようとしている）
    float   lastTargetX;  // 最後に打ったコース（AI 座標）。ネットではその側を守る
    bool    planFore;     // フォアで打つ予定
    SwingAnim anim;
    // 最後のショット（HUD 用）
    ShotType lastType;
    float   lastQ, lastKmh;
} Opponent;

typedef struct {
    int points[2];   // 0=自分, 1=AI
    int games[2];
    int sets[2];
    bool tiebreak;
    int server;      // 0=自分, 1=AI（ゲームの最初のサーバー）
    bool over;
    int nGames;              // ここまでのゲーム数
    int gameWinner[16];      // 各ゲームを取った方
    int gameServer[16];      // 各ゲームのサーバー（取った方と違えばブレーク）
    int setsToWin;           // 何セット取ったら勝ちか（1 / 2 / 3）。0 なら 1
    int nSetsDone;           // 終わったセットの数
    int setScore[5][2];      // 終わったセットのゲーム数
} Score;

// 試合の統計（中継のマッチサマリーと同じ項目）。1st サーブの入り・ダブルフォールトは mst から
typedef struct {
    int aces;
    int s1Pts, s1Won;        // 1st サーブで始まったポイント / 取った数
    int s2Pts, s2Won;        // 2nd サーブ（ダブルフォールト含む）
    int bpTotal, bpWon;      // ブレークポイント（レシーバーとして）
    int netPts, netWon;      // ネットに出たポイント（サービスラインより前に入った）
    int unforced;            // アンフォーストエラー（時間も体勢もあったのにミス）
    int winners;             // ウィナー（エース含む）
    int points;              // 取ったポイント
} MatchStats;

typedef struct {
    bool    timingValid;  // タイミングの輪を出せる
    float   tBest;        // 今の立ち位置で一番いい瞬間まで（秒）
    float   qBest;        // そのときの質
    bool    fore, overhead;
    bool    standValid;   // 立つ場所の目印を出せる
    Vector3 stand;        // 立つ場所（地面）
    Vector3 ballAt;       // そのときのボール
} Assist;

// 相手が打った瞬間に一度だけ作る「この球の予定」。毎フレーム作り直さないので目印が跳ねない
#define ASSIST_PATH_MAX 80
typedef struct {
    bool    valid;
    bool    bounceValid;
    Vector3 bounce;                   // 自分のコートで最初に弾む点
    int     nPath;
    Vector3 path[ASSIST_PATH_MAX];    // 弾んだ後の球の通り道
    bool    good[ASSIST_PATH_MAX];    // その点で打ちやすい高さか
    bool    standValid;
    Vector3 stand;                    // 立つ場所（地面）
    Vector3 ballAt;                   // そこで打つときのボール
    bool    fore, overhead;
    float   tContact;                 // 打った瞬間から打点まで（秒）
} AssistPlan;

typedef struct {
    FILE  *f;
    int    shotSerial;
    bool   pending;          // 結果待ちの行（打球がバウンドしたら書き出す）
    int    pendingId;
    bool   pendingServe;
    char   row[1024];
    // 来た球の追跡
    bool   track, swung;
    float  prevAge;
    Hitter prevHitter;
    float  minR, angAtMin, hAtMin, ageAtMin, kmhAtMin;
    float  firstMove, chargeStart;
    ShotResult bounceResult;
    Ball   ring[LOG_RING];   // 来た球の最近の状態（タイミング分析用）
    int    ringN, ringHead;
    // このセッションの集計（Tab で表示）
    int    swings, hits, whiffs, miss[8], shotIn, shotOut, shotNet;
    int    early, late, onTime, posProblem, timingProblem;
    float  sumAbsOffset; int nOffset;
    float  sumQ, sumBestQ;
    int    noSwingIn, noSwingReachable, noSwingOut;
    float  sumFirstMove; int nFirstMove;
    int    serves, servesIn, serveWhiffs;
    int    assist;           // 手がかり表示のレベル
    float  pendingLaunch;    // 結果待ちの打球の打ち出し角（度）
    float  pendingApex;      // 結果待ちの打球の最高点（m）
    float  pendingVy;        // 打った瞬間のボールの上下の速さ（プラス＝上がり際＝ライジング）
    bool   hasVy;
    int    style;            // 難易度（0=REAL 1=CASUAL）
} PlayLog;

typedef enum { APP_TITLE, APP_PLAY, APP_PAUSE } AppState;

typedef enum { MODE_MATCH, MODE_RALLY, MODE_RETURN, MODE_SERVE, MODE_COUNT } PracticeMode;

typedef enum { SV_READY, SV_TOSS, SV_FLIGHT, SV_WAIT } ServeState;

typedef enum { PH_SERVE, PH_PLAY, PH_BETWEEN, PH_POINT_OVER } MatchPhase;

// ---- 表とグローバル変数 ----
extern SurfaceType gSurface;
extern bool gCasual;
extern const Surface SURFACES[SURF_COUNT];
extern const char *STYLE_NAMES[STYLE_COUNT];
extern const ShotSpec SHOTS[SHOT_COUNT];
extern const ServeSpec SERVES[SERVE_COUNT];
extern const AiLevel AI_LEVELS[3];

// ---- 関数 ----
float NetHeightAt(float x);
float Clamp01(float v);
float RandNormal(void);
float RandRange(float a, float b);
void JudgeFirstBounce(Ball *b);
Vector3 AirAccel(Vector3 vel, Vector3 spin);
void Bounce(Ball *b);
void StepBall(Ball *b, float dt);
Ball MakeShot(Vector3 from, Vector3 dirFlat, float speed, float angle, float rpm);
float PredictLandingDist(Vector3 from, Vector3 dirFlat, float speed, float angle, float rpm);
float PredictNetClearance(Vector3 from, Vector3 dirFlat, float speed, float angle, float rpm);
float SolveLaunchAngle(Vector3 from, Vector3 dirFlat, float dist, float speed, float rpm, bool high);
float SolveSpeed(Vector3 from, Vector3 dirFlat, float dist, float angle, float rpm);
Ball AimedShot(Vector3 from, Vector3 target, float kmh, float rpm, bool high, float *outAngle);
float HeightErr(const ShotSpec *sp, float y, float inKmh);
float HeightTolScale(float inKmh);
Contact EvaluateContact(Vector3 player, const Ball *ball, ShotType type, float charge, float moveToward, float reachBonus);
Ball PlayerShot(const Contact *c, Vector3 ballPos, Vector3 target, ShotType type, float charge, float *outKmh, float *outRpm, float *outAngle);
Vector3 SpinVector(Vector3 dirFlat, float top, float side, float gyro);
Ball MakeServeBall(Vector3 from, Vector3 dirFlat, float speed, float pitch, Vector3 spinRpm);
Ball FlyToFirstBounce(Ball b);
void SolveServe(Vector3 from, Vector3 target, float speed, Vector3 spinRpm, Vector3 *outDir, float *outPitch);
void ServiceBox(float serverX, int side, float *x0, float *x1);
Ball HitServe(Vector3 contact, Vector3 target, ServeType type, float charge, float q, float x0, float x1, float *outKmh);
Ball MachineServe(void);
Ball MachineFeed(bool pro);
void LineAlongX(float z, float x0, float x1);
void LineAlongZ(float x, float z0, float z1);
void DrawCourt(void);
Vector3 LocalToWorld(Vector3 base, bool facingNegZ, Vector3 l);
float EaseOut(float u);
Vector3 RacketLocal(const SwingAnim *a, int *strokeOut, Vector3 *tangent);
void DrawRacketHead(Vector3 center, Vector3 normal, Color frame);
void DrawFigure(Vector3 pos, bool facingNegZ, Color shirt, const SwingAnim *a, float speed);
const char *ResultText(ShotResult r);
const char *MissText(MissReason m);
Color QualityColor(float q);
Vector3 Rot180(Vector3 v);
Ball RotBall(Ball b);
float TravelTime(float d, float v0, float vmax, float a, float b);
Vector3 Accelerate(Vector3 vel, Vector3 want, float a, float b, float dt);
Vector3 MoveToward(Vector3 *pos, Vector3 vel, Vector3 target, float vmax, float dt);
Ball FlipSides(Ball b);
Vector3 AiHome(void);
Vector3 AiNetPos(const Opponent *ai);
Vector3 ReturnPos(bool deuce, bool second);
void AiPlan(Opponent *ai, const Ball *worldBall);
ShotType AiChooseShot(const Opponent *ai, const Contact *c, Vector3 playerAi, Vector3 meAi, float *charge, Vector3 *target);
bool AiSwing(Opponent *ai, Ball *ball, Vector3 player);
Ball AiServe(Opponent *ai, bool deuce, bool first);
void AiUpdate(Opponent *ai, const Ball *ball, float dt);
const char *PointName(int p);
int CurrentServer(const Score *s);
bool DeuceCourt(const Score *s);
void AwardPoint(Score *s, int w);
const char *ScoreText(const Score *s);
const char *PointLabel(const Score *s, int k);
Assist ComputeAssist(Vector3 player, Vector3 playerVel, const Ball *ball, ShotType type, float charge);
AssistPlan PlanAssist(Vector3 player, Vector3 playerVel, const Ball *ball, ShotType type);
void LogOpen(PlayLog *L);
void LogFlush(PlayLog *L, const char *result);
void LogTimingAnalysis(const PlayLog *L, Vector3 player, const Ball *pre, ShotType type, float charge, float *bestQ, float *offsetMs);
const char *StrokeName(const Contact *c);
const char *MissName(MissReason m);
void LogTrackIncoming(PlayLog *L, const Ball *ball, Vector3 player, Vector3 playerVel, bool charging, const char *mode, const char *level);
void LogRecordBall(PlayLog *L, const Ball *ball);
Vector3 EffectiveAim(ShotType t, float playerZ, float aimX, float aimDepth, PlayStyle style);
Vector3 AimFromHold(ShotType t, float playerZ, float aimX, float hold, PlayStyle style);
float ServeDepthFromTime(float t);
void DisableIme(void *hwnd);   // platform_ime.c（Windows で日本語入力を切る）

// ---- プレイヤー（自分。将来は 2 人目も） ----
// 入力の種類（溜めを始めた入力を覚えて、その入力が離されたら振る）
typedef enum { SRC_NONE, SRC_KEY_L, SRC_KEY_J, SRC_KEY_K, SRC_MOUSE_L, SRC_MOUSE_R, SRC_COUNT } InputSource;

// 簡単モードや「ラケットが合わせにいく」ときの、待っているスイング
typedef struct {
    bool active; ShotType type; Vector3 target; float charge; float age; float releaseAge; float penalty;
} QueuedSwing;

typedef struct {
    Vector3 pos, vel;          // 足元の位置と速度
    SwingAnim anim;            // 見た目（体とラケット）
    float downT;               // 飛びついて倒れている残り時間
    bool  sliding;             // 滑り込んでいる
    float aimX, aimDepth;      // 狙い（マウスで動かす。ずっと残る）
    int   chargingShot;        // 溜めている球種（-1：溜めていない）
    int   chargingServe;       // 溜めているサーブ（-1：溜めていない）
    float charge;              // 強さ（0〜1）
    float hold;                // 押している長さ（CHARGE_TIME で 1、HOLD_MAX まで）。深さを決める
    float serveT;              // トスを上げてからの時間（秒）。サーブの深さを決める
    InputSource chargeSrc;     // 何で溜め始めたか
    QueuedSwing swingQ;
    // 最後のショット（画面表示用）
    bool  hasLast;
    Contact lastContact;
    ShotType lastType;
    float lastKmh, lastRpm, lastAngle;
    Vector3 lastContactPos, lastIdealPos;
    Vector3 lastAimTarget;     // 最後に狙った場所（落ちた場所と比べて表示する）
    // サーブ
    ServeState sv;
    Vector2 tossOffset;
    ServeType lastServeType;
    float lastServeKmh, lastServeH, lastServeQ;
} Player;

Player NewPlayer(Vector3 pos);

// 1 フレーム分のプレイヤーの操作。キーボード＋マウスでも、ゲームパッドでも、この形にそろえる
typedef struct {
    Vector2 move;               // 移動。x=右、y=前（相手コートの方）。長さ 0〜1
    Vector2 aim;                // 狙いの移動（m）。x=右、y=深く
    int  shot;                  // 押された球種（-1：なし）。Shift や Ctrl などの組み合わせは決定済み
    int  serve;                 // 押されたサーブ（-1：なし）
    InputSource source;         // 押した入力（離されたら振る）
    bool released[SRC_COUNT];   // 離された入力
} PlayerInput;

void ReadKeyboardMouse(PlayerInput *in, bool mouseActive, bool skipMouseDelta, float aimSens, float dt);

#endif
