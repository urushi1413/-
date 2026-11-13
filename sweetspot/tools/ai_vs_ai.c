// AI 同士で試合をさせて、バランスの数字を出すツール。
//   make sim  （sweetspot/ ディレクトリで）
// main.c をそのまま取り込んで、main だけ名前を変えている。描画は使わない。
#define main game_main
#include "../main.c"
#undef main
#include <stdio.h>
// 自分側も AI にする：世界を 180° 回して、打者の名前を入れ替えれば、同じ AI 関数で動かせる
static Ball Flip(Ball b){ return FlipSides(b); }
typedef struct{int unret,points,rallyShots,aces,df,errNet,errOut,winners,unreached,firstIn,firstTot,svWon[2],svPts[2];} St;
// 1ポイント：server 0=手前(P) 1=奥(O)。勝者を返す
static int Point(Opponent*O,Opponent*P,int server,bool deuce,St*st){
  Ball ball; int serveNo=1;
again:
  O->planned=P->planned=false; O->swung=P->swung=false; O->leaving=P->leaving=false; O->watching=P->watching=HITTER_NONE; O->vel=P->vel=(Vector3){0};
  if(server==1){ ball=AiServe(O,deuce,serveNo==1); P->pos=Rot180(ReturnPos(deuce)); }
  else { Ball bp=AiServe(P,deuce,serveNo==1); ball=Flip(bp); O->pos=Rot180(ReturnPos(deuce)); }
  if(serveNo==1) st->firstTot++;
  bool serveJudged=false; int shots=1;
  for(int f=0; f<60*30; f++){
    float dt=1.0f/60;
    Ball bp=Flip(ball);
    AiUpdate(O,&ball,dt); AiUpdate(P,&bp,dt);
    for(int k=0;k<4;k++){
      StepBall(&ball,PHYS_DT);
      if(O->planned && ball.age>=O->swingAge){ O->planned=false; if(AiSwing(O,&ball,Rot180(P->pos))) {shots++;} }
      bp=Flip(ball);
      if(P->planned && bp.age>=P->swingAge){ P->planned=false; if(AiSwing(P,&bp,Rot180(O->pos))) {ball=Flip(bp); shots++;} }
    }
    int hitter = ball.lastHitter==HITTER_OPP ? 1 : 0;  // 1=奥(O)
    if(ball.isServe && ball.bounces>=1 && !serveJudged){
      if(ball.result==RESULT_IN){ serveJudged=true; if(serveNo==1)st->firstIn++; }
      else { if(serveNo==1){serveNo=2; goto again;} st->df++; st->rallyShots+=shots; return 1-server; }
    }
    if(!ball.isServe || serveJudged){
      if(ball.bounces>=1 && ball.result!=RESULT_IN && ball.result!=RESULT_NONE){ if(ball.result==RESULT_NET)st->errNet++; else st->errOut++; st->rallyShots+=shots; if(shots==2)st->unret++; return 1-hitter; }
      if(ball.bounces>=2){ if(shots==1){st->aces++; st->unret++;} else st->winners++; st->rallyShots+=shots; return hitter; }
    }
  }
  return 0;
}
int main(void){ srand(3);
  for(int sf=0;sf<SURF_COUNT;sf++){ gSurface=(SurfaceType)sf; printf("--- court: %s ---\n",SURFACES[sf].name);
  for(int lv=0;lv<3;lv++){
    Opponent O={0},P={0}; O.level=P.level=lv; St st={0};
    int N=300; for(int n=0;n<N;n++){ int server=n%2; bool deuce=(n/2)%2==0; int w=Point(&O,&P,server,deuce,&st); st.points++; st.svPts[server]++; if(w==server)st.svWon[server]++; }
    printf("%-6s vs same: avg rally %.1f shots | 1st serve in %2d%% | unreturned serve %2d%% (no touch %2d%%) DF %2d%% | ends: winners %2d%% net err %2d%% out err %2d%% | server wins %2d%%\n",
      AI_LEVELS[lv].name,(float)st.rallyShots/st.points, st.firstIn*100/st.firstTot, st.unret*100/st.points, st.aces*100/st.points, st.df*100/st.points,
      st.winners*100/st.points, st.errNet*100/st.points, st.errOut*100/st.points,(st.svWon[0]+st.svWon[1])*100/st.points);
  }
  }
}
