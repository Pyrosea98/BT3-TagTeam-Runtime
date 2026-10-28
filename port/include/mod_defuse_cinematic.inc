/* Defuse cinematic (Settings > Fusion > "Defuse cinematic", fusion_defuse_cinematic, default on). Off keeps the
   plain defusion: stock change action 0xEC, then the partner shown beside the leader.
   1 split crack  0xEC plays the fusion's own opening instead of the transformation strain (BtlAct_DefusePresent in
                  btl_act_h_b.c: clips 0x1F / 0x20, the fusion's camera cut and sound 0x31) while a lightning crack
                  grows down the front of the body from the top of the head to the crotch, on the model's own surface
                  (Port_DefuseCrack: the vertices tools/defuse-previewer picks; drawn by mod_battle_hud.inc);
   2 white out    on the 0x20 hold the model is pushed to white (the stock white flash held, BtlObjFlash) and the crack
                  dims into it; the original-body reload runs under the hold;
   3 pushed apart the original body's closing is its stagger clip 0xB4 pushed to its left (air 0xB1); the partner is
                  shown beside it at once in the stock stagger toward its own side (0xC2: 0xB3, 0xC3: the mirrored
                  0xB4), both still white and fading back to their colours; presentation only
                  (no hit, damage, combo or down state); slid apart sideways, the partner's own hurt voice (the
                  leader's comes with its closing), one wider cut framing both on each fighter's camera, opponents
                  held by hit-stop 0x125; the whole cinematic is one screen: the view the reveal camera last
                  showed is held (Port_DemoCamHold) until it is over, as a fusion is one screen too;
   4 back         each reaction ends with its own clip (the stock idle follow-up); the cut ends by its timer and the
                  normal / split cameras return.
   A fighter without the fusion clips keeps the stock 0xEC strain for beats 1-2. Durations count fight updates (30 per
   second), the clock of the stock clips and cuts they run beside.
   BT3_DEFUSE_PREVIEW=<update>[,<chara>[,<costume>]] (a tool, see pd_preview_tick) stages one in a CPU battle. */
#ifdef PORT
#include <stdlib.h>
extern int Port_DefuseClipOk(int,int),Port_FusionPartnerActor(int,int);
extern void Port_DefuseWhiten(int,float);
extern int Port_AssistSafePoint(int,float*),Port_AssistCommitPosition(int,const float*,float);
extern int Port_DefuseNodeFrame(int,int,float*,float*,float*);
#include "mod_defuse_crack.h"
extern int Port_DefuseCrackBuild(int,PortCrackPt*,int),Port_DefuseCrackPlace(int,const PortCrackPt*,float*,float*);
extern s32 Port_ModHudProjectFine(s32,Vec4*,s32*,s32*);
extern void ChrCam_SetCut(void*,Vec4*,Vec4*,Vec4*,Vec4*,Vec4*,Vec4*,s32,f32,f32,f32,f32,f32,f32,s32,s32,s32,s32,s32);
extern f32 BtlCharApi_GetHeight(s32),atan2f(f32,f32),Mathf_Sin(f32),Mathf_Cos(f32);
extern void BtlCharApi_GetNodePos(s32,s32,Vec4*),BtlChar_PlayVoice(BtlActJChr*,s32);
#define PD_GROW 24      /* beat 1: the line reaches the crotch in 0.8 s */
#define PD_WHITE 15     /* beat 2: the model is white in 0.5 s */
#define PD_FADE 14      /* beat 3: both fade back to their colours */
#define PD_PUSH 45      /* beat 3: the hold and the two-fighter cut, 1.5 s (most of the 50-update stagger) */
#define PD_LIMIT 750    /* a cinematic that outlives this many updates is dropped (the shared hold bound is 900) */
#define PD_SPEED 3.0f   /* beat 3: first sideways step, units per update (about 320 km/h) */
#define PD_BRAKE 0.35f  /* beat 3: the step shrinks by this every update (about 14 units in all, 9 updates) */
#define PD_PEAK 0.9f    /* the white: 0.9 of the full flash blend, so the shape still reads */
#define PD_THICK 0.10f  /* the crack: core width, world units (Juan: 0.1, no branches) */
#define PD_FLICKER 0.45f
typedef struct PortDefuseCine {int stage,style,frames,white,closing,partner,pushed,crackN,crackTried,held;f32 angle,camYaw,camDist,camGrow;Vec4 camAt;PortCrackPt crack[PD_CRACK_POINTS];} PortDefuseCine;
static PortDefuseCine sDefuse[12];
extern void Port_DemoCamHold(s32);
/* The held view goes the moment the cinematic of its fighter ends, whatever ended it. */
static void pd_release_camera(PortDefuseCine *d){if(d->held){d->held=0;Port_DemoCamHold(0);}}
static void pd_end(PortDefuseCine *d){d->stage=0;pd_release_camera(d);}
static void pd_reset(void){int i;for(i=0;i<12;i++){pd_end(&sDefuse[i]);sDefuse[i].style=0;}}
/* Beat 1 starts with the expiry request of action 0xEC. */
static void pd_begin(BtlActJChr *c){PortDefuseCine *d;
 if(c->player<0||c->player>=12||!Port_Setting("fusion_defuse_cinematic",1))return;
 d=&sDefuse[c->player];pd_release_camera(d);d->stage=1;d->style=0;d->frames=d->white=d->closing=d->pushed=d->crackN=d->crackTried=0;d->partner=-1;
}
/* btl_act_h_b.c (BtlAct_TransformA): 1 = this 0xEC is a defusion presented the fusion's way. Decided on entry. */
s32 Port_DefuseStyle(void *actor,s32 phase){BtlActJChr *c=(BtlActJChr*)actor;PortDefuseCine *d;
 if(!c||c->player<0||c->player>=12)return 0;d=&sDefuse[c->player];
 if(phase==0)d->style=d->stage==1&&Port_DefuseClipOk(c->objId,0x1F)&&Port_DefuseClipOk(c->objId,0x20);
 return d->style;
}
/* fight_panels.c: the line of fighter `actor`, 0..1000 grown and whitened (the line fades as the model whitens). */
int Port_DefuseLight(void *actor,int *grow,int *white){BtlActJChr *c=(BtlActJChr*)actor;PortDefuseCine *d;int g,w;
 if(!c||c->player<0||c->player>=12)return 0;d=&sDefuse[c->player];if(d->stage!=1)return 0;
 g=d->frames*1000/PD_GROW;w=d->white*1000/PD_WHITE;
 *grow=g>1000?1000:g;*white=w>1000?1000:w;return 1;
}
/* btl_act_h_b.c (BtlAct_DefusePresent): the closing waits until the crack is grown and the white has held a moment.
   It does not wait for the fused character's reveal camera (Juan: the glow then lasted far too long): the split stops
   that camera. */
s32 Port_DefuseReady(void *actor){BtlActJChr *c=(BtlActJChr*)actor;
 return c&&c->player>=0&&c->player<12&&sDefuse[c->player].white>=PD_WHITE+4;
}
/* btl_act_h_b.c, the update the split is decided (before the body is swapped): when the reveal camera is showing, its
   view is held until the cinematic ends, so the split and the reactions stay on that one screen. */
extern s32 DemoCam_IsActive(void);
void Port_DefuseHoldCamera(void *actor){BtlActJChr *c=(BtlActJChr*)actor;PortDefuseCine *d;
 if(!c||c->player<0||c->player>=12)return;d=&sDefuse[c->player];
 if(d->stage==1&&!d->held&&DemoCam_IsActive()){Port_DemoCamHold(1);d->held=1;}
}
/* ---- the crack (Port_DefuseCrack) -------------------------------------------------------------------------------
   Real surface points of the fused model (Port_DefuseCrackBuild, the vertices tools/defuse-previewer picks), placed
   every frame through the same two-node skinning the game draws them with, so the crack lies on the body, hair
   included. Drawn over the picture: each segment shows as much as its surface faces the camera. */
static int pd_project(int view,const float *p,float *x,float *y){Vec4 v;s32 sx,sy;v.x=p[0];v.y=p[1];v.z=p[2];v.w=1.0f;
 if(!Port_ModHudProjectFine(view,&v,&sx,&sy))return 0;*x=(float)sx;*y=(float)sy;return 1;}
static void pd_unit(float *v){float l=sqrtf(v[0]*v[0]+v[1]*v[1]+v[2]*v[2]);if(l>1e-6f){v[0]/=l;v[1]/=l;v[2]/=l;}}
/* How much a surface of normal `n` at `p` faces the camera of `view`, 0..1: the winding of a small projected
   triangle (a, b) with a x b = -n (the same sense as the body's right, then down, for its front). */
static float pd_facing(int view,const float *p,const float *normal,float size){float n[3],a[3],b[3],pa[3],pb[3],s0[2],s1[2],s2[2],u,v,w;int i;
 for(i=0;i<3;i++)n[i]=normal[i];pd_unit(n);
 a[0]=-n[2];a[1]=0.0f;a[2]=n[0];if(a[0]*a[0]+a[2]*a[2]<1e-4f){a[0]=1.0f;a[1]=0.0f;a[2]=0.0f;}pd_unit(a); /* level, across the normal */
 b[0]=a[1]*n[2]-a[2]*n[1];b[1]=a[2]*n[0]-a[0]*n[2];b[2]=a[0]*n[1]-a[1]*n[0]; /* a x n: then a x b = -n */
 for(i=0;i<3;i++){pa[i]=p[i]+a[i]*size;pb[i]=p[i]+b[i]*size;}
 if(!pd_project(view,p,&s0[0],&s0[1])||!pd_project(view,pa,&s1[0],&s1[1])||!pd_project(view,pb,&s2[0],&s2[1]))return 0.0f;
 u=(s1[0]-s0[0])*(s2[1]-s0[1])-(s1[1]-s0[1])*(s2[0]-s0[0]);
 v=sqrtf((s1[0]-s0[0])*(s1[0]-s0[0])+(s1[1]-s0[1])*(s1[1]-s0[1]))*sqrtf((s2[0]-s0[0])*(s2[0]-s0[0])+(s2[1]-s0[1])*(s2[1]-s0[1]));
 if(v<1e-3f)return 0.0f;w=3.0f*u/v;return w<0.0f?0.0f:w>1.0f?1.0f:w;}
/* fight_panels.c: the crack of fighter `actor` in view `view`, as segments of 6 ints (x0, y0, x1, y1 in 1/16 HUD
   units, alpha 0..255, width in 1/16 HUD units); returns how many (at most `max`), 0 when there is none. */
int Port_DefuseCrack(void *actor,int view,int *out,int max){
 BtlActJChr *c=(BtlActJChr*)actor;PortDefuseCine *d;float P[PD_CRACK_POINTS][3],Nn[PD_CRACK_POINTS][3],head[3],hip[3],f0[3],r0[3],h,grow,white,glow,reach,flick,pxu,ax,ay,bx,by;
 int i,j,k,n=0,count;
 if(!c||c->player<0||c->player>=12||max<1)return 0;d=&sDefuse[c->player];
 if(d->stage!=1||d->white>=PD_WHITE)return 0;
 if(!d->crackTried){d->crackTried=1;d->crackN=Port_DefuseCrackBuild(c->objId,d->crack,PD_CRACK_POINTS);
  fprintf(stderr,"bt3: defuse cinematic: actor%d crack of %d surface points\n",c->player,d->crackN);}
 count=d->crackN;if(count<2)return 0;
 for(i=0;i<count;i++)if(!Port_DefuseCrackPlace(c->objId,&d->crack[i],P[i],Nn[i]))return 0;
 if(!Port_DefuseNodeFrame(c->objId,48,head,f0,r0)||!Port_DefuseNodeFrame(c->objId,2,hip,f0,r0))return 0;
 h=BtlCharApi_GetHeight(c->objId);if(!(h>1.0f&&h<1000.0f))return 0;
 grow=(float)d->frames/PD_GROW;if(grow>1.0f)grow=1.0f;
 white=(float)d->white/PD_WHITE;
 flick=1.0f-PD_FLICKER*0.5f*(1.0f+Mathf_Sin(d->frames*2.1f)*Mathf_Sin(d->frames*5.3f+1.7f));
 glow=(1.0f-0.7f*white)*flick;
 /* HUD units per world unit here: the head-to-hips span on screen */
 if(!pd_project(view,head,&ax,&ay)||!pd_project(view,hip,&bx,&by))return 0;
 {float x=head[0]-hip[0],y=head[1]-hip[1],z=head[2]-hip[2],w=sqrtf(x*x+y*y+z*z);if(w<1e-3f)return 0;pxu=sqrtf((ax-bx)*(ax-bx)+(ay-by)*(ay-by))/w;}
 reach=grow*(count-1);
 for(k=0;k+1<count&&n<max;k++){float f,b[3],x0,y0,x1,y1,face;
  if(k>=reach)break;f=reach-k;if(f>1.0f)f=1.0f;
  for(j=0;j<3;j++)b[j]=P[k][j]+(P[k+1][j]-P[k][j])*f;
  face=pd_facing(view,P[k],Nn[k],h*0.05f);
  if(face<=0.004f||!pd_project(view,P[k],&x0,&y0)||!pd_project(view,b,&x1,&y1))continue;
  {int *o=out+6*n++;float a=glow*face;o[0]=(int)x0;o[1]=(int)y0;o[2]=(int)x1;o[3]=(int)y1;o[4]=(int)(255.0f*(a>1.0f?1.0f:a));o[5]=(int)(PD_THICK*face*pxu);}
 }
 return n;
}
/* Port_FusionClockTick: the partner is shown the update the original body's closing has ended the change action
   (it ends at once: BtlAct_DefusePresent), without waiting for the cut or the hold, so the two leave the white
   together. Shown while the change action still ran, the partner was hidden (its flag 0x12A draws one fighter). */
static int pd_release_now(BtlActJChr *c){PortDefuseCine *d;int a;
 if(c->player<0||c->player>=12)return 0;d=&sDefuse[c->player];a=BtlAct_GetCurrent(c);
 return d->stage==1&&d->style&&d->closing>=1&&(a<0xEC||a>0xF8);
}
static void pd_cut(BtlActJChr *c,Vec4 *mid,f32 yaw,f32 dist,f32 grow,int time){
 static Vec4 zero;Vec4 at=*mid;
 /* fixed points (-1: no node tracking), front view, slightly above; snap in and back (1, 4), the whole screen (0x40) */
 ChrCam_SetCut(c,&at,&zero,&at,&zero,&at,&zero,-1,BtlUtil_WrapAngle(yaw+3.14159265f),0.0f,-0.2f,0.0f,dist,grow,-1,-1,-1,time,0x45);
}
/* Beat 3, after Port_ExtraFusionRelease has shown the partner beside the leader. Returns 1 when it runs. */
/* The stock stagger that throws fighter `x` the way it slides (yaw `angle`): 0xC2 plays 0xB3, which throws the body
   toward the model's +x; 0xC3 plays its mirror 0xB4. The model's +x in the world is (cos, 0, -sin) of its yaw. */
static int pd_stagger(BtlActJChr *x,f32 angle){f32 yaw=BtlChar_GetPos(x)->rot.y;
 return Mathf_Sin(angle)*Mathf_Cos(yaw)-Mathf_Cos(angle)*Mathf_Sin(yaw)>=0.0f?0xC2:0xC3;
}
static int pd_push(BtlActJChr *c,int member){PortDefuseCine *d;BtlActJChr *p;int t,pa,ca;Vec4 a,b,mid;f32 dx,dz,h,dist;
 if(c->player<0||c->player>=12)return 0;d=&sDefuse[c->player];if(d->stage!=1){pd_end(d);return 0;}d->stage=0;
 t=Port_FusionPartnerActor(c->player,member);p=t>=0&&t<BtlChar_GetCount()?BtlChar_Get(t):NULL;if(!p||p==c){pd_release_camera(d);return 0;}
 dx=BtlChar_GetPos(p)->pos.x-BtlChar_GetPos(c)->pos.x;dz=BtlChar_GetPos(p)->pos.z-BtlChar_GetPos(c)->pos.z;
 d->angle=dx*dx+dz*dz>0.0001f?atan2f(dx,dz):BtlUtil_WrapAngle(BtlChar_GetPos(c)->rot.y+1.5707963f);
 pa=pd_stagger(p,d->angle);ca=pd_stagger(c,BtlUtil_WrapAngle(d->angle+3.14159265f));
 /* a missing stagger clip would end the reaction at once: skip the beat instead */
 if(!Port_DefuseClipOk(p->objId,pa==0xC2?0xB3:0xB4)||!Port_DefuseClipOk(p->objId,0xB1)||
    !Port_DefuseClipOk(c->objId,ca==0xC2?0xB3:0xB4)||!Port_DefuseClipOk(c->objId,0xB1)){pd_release_camera(d);return 0;}
 /* the leader's stagger goes on as an action of its own (the fusion-style closing already gave its voice) */
 BtlAct_Request(c,ca);if(!d->style)BtlChar_PlayVoice(c,1);
 BtlAct_Request(p,pa);BtlChar_PlayVoice(p,1);
 BtlCharApi_GetNodePos(c->objId,3,&a);BtlCharApi_GetNodePos(p->objId,3,&b);
 mid.x=(a.x+b.x)*0.5f;mid.y=(a.y+b.y)*0.5f;mid.z=(a.z+b.z)*0.5f;mid.w=1.0f;
 h=BtlCharApi_GetHeight(c->objId);if(BtlCharApi_GetHeight(p->objId)>h)h=BtlCharApi_GetHeight(p->objId);if(!(h>1.0f&&h<1000.0f))h=20.0f;
 /* frame the pair tightly: their spread once the slide is over (gap + 2 x the slide) plus most of a body height */
 {f32 gap=sqrtf(dx*dx+dz*dz),slide=0.0f,step;for(step=PD_SPEED;step>0.0f;step-=PD_BRAKE)slide+=step;
  dist=(gap+2.0f*slide)*0.9f+h*0.8f;if(dist<h*1.6f)dist=h*1.6f;if(dist>h*3.0f)dist=h*3.0f;}
 /* centre on the middle of the slide, not of the first frame */
 d->camAt=mid;d->camYaw=BtlChar_GetPos(c)->rot.y;d->camDist=dist;d->camGrow=h*0.3f;
 if(!d->held)pd_cut(c,&mid,d->camYaw,dist,d->camGrow,PD_PUSH);
 d->stage=2;d->frames=0;d->pushed=0;d->partner=t;
 fprintf(stderr,"bt3: defuse cinematic: actor%d and actor%d pushed apart\n",c->player,t);return 1;
}
/* One update of the slide: the place only, through the stage-checked helpers the assists use. The heading is not
   touched: a sideways heading would turn the model to face it (BtlMove_TurnModelYaw, 36 degrees an update). */
static void pd_shift(int who,f32 angle,f32 step){BtlActJChr *x=BtlChar_Get(who);BtlActJPose *pose=x?BtlChar_GetPos(x):NULL;float point[4];
 if(!pose||!(step>0.0f))return;
 point[0]=pose->pos.x+Mathf_Sin(angle)*step;point[1]=pose->pos.y;point[2]=pose->pos.z+Mathf_Cos(angle)*step;point[3]=1.0f;
 if(Port_AssistSafePoint(who,point))Port_AssistCommitPosition(who,point,pose->rot.y);
}
/* Once per fight update, after Port_FusionClockTick. */
static void pd_tick(void){int i;
 for(i=0;i<12;i++){PortDefuseCine *d=&sDefuse[i];BtlActJChr *c,*p;int a,anim;f32 fade;
  if(!d->stage)continue;
  c=i<BtlChar_GetCount()?BtlChar_Get(i):NULL;
  if(!c||c->player!=i||BtlMember_GetActiveGauge(c)->health<=0){pd_end(d);continue;}
  if(d->stage==1){
   if(sFusionReceipts[i].state!=2&&sFusionReceipts[i].state!=3){pd_end(d);continue;}
   a=BtlAct_GetCurrent(c);if(a<0xEC||a>0xF8)continue;
   if(++d->frames>PD_LIMIT){pd_end(d);continue;}
   anim=BtlAnim_GetId(c);
   /* white from the fusion hold once the line is grown (the stock fallback: from its closing flash) */
   if(d->white||(d->frames>=PD_GROW&&(d->style?anim==0x20:anim==0x179)))d->white++;
   if(d->style&&(anim==0xB4||anim==0xB1))d->closing++;
   if(d->white)Port_DefuseWhiten(c->objId,d->white>=PD_WHITE?PD_PEAK:PD_PEAK*d->white/PD_WHITE);
   continue;
  }
  p=d->partner>=0&&d->partner<BtlChar_GetCount()?BtlChar_Get(d->partner):NULL;
  if(!p||BtlMember_GetActiveGauge(p)->health<=0){if(BtlAct_GetCurrent(c)==0xC2||BtlAct_GetCurrent(c)==0xC3)BtlAct_Request(c,0xB);pd_end(d);continue;}
  d->frames++;
  if(d->frames==2&&!d->held)pd_cut(c,&d->camAt,d->camYaw,d->camDist,d->camGrow,PD_PUSH-2);
  fade=d->frames<PD_FADE?PD_PEAK*(PD_FADE-d->frames)/PD_FADE:0.0f;
  Port_DefuseWhiten(c->objId,fade);Port_DefuseWhiten(p->objId,fade);
  {f32 step=PD_SPEED-PD_BRAKE*(d->frames-1);
   if(step>0.0f){pd_shift(i,BtlUtil_WrapAngle(d->angle+3.14159265f),step);pd_shift(d->partner,d->angle,step);d->pushed=3;}}
  /* the hold and the wide cut last PD_PUSH; each reaction then ends with its own clip (stagger -> idle follow-up) */
  if(d->frames<PD_PUSH){BtlChar_SetFlag(c,0x125);BtlChar_SetFlag(p,0x125);continue;}
  pd_end(d);
 }
}

/* BT3_DEFUSE_PREVIEW=<update>[,<chara>[,<costume>]]: a tool for judging the cinematic without playing for a fusion.
   In a fight where actor 0 has an extra ally of its own team (BT3_DEMO=1 BT3_EXTRA_FIGHTERS=<ally>:0), <update>
   updates into the fight the ally is merged into actor 0 exactly as a dance fusion merges it (Port_ExtraFusionConsume)
   and actor 0 gets a timed-fusion receipt that has already run out, with <chara>/<costume> as the original body
   (default: its current one). The real expiry, reload, release and cinematic then run. While it runs
   Port_DefusePreviewRecording is 1 (gs_gl.c: BT3_DEFUSE_SHOTS=<folder> saves the window then). Never online. */
extern int Port_DefusePreviewLink(int,int,int),Port_Team(int);
static volatile int sDefusePreviewRec;
static int sDefusePreviewState,sDefusePreviewTicks,sDefusePreviewTail;
int Port_DefusePreviewRecording(void){return sDefusePreviewRec;}
/* btl_seq.c: 1 while a BT3_DEFUSE_PREVIEW cinematic is still to be played (the demo battle must not end before). */
s32 Port_DefusePreviewHolding(void){static int read;static const char *spec;if(!read){read=1;spec=getenv("BT3_DEFUSE_PREVIEW");}
 return spec!=NULL&&!Port_NetActive()&&sDefusePreviewState!=2;}
static void pd_preview_tick(void){
 static const char *spec;static int read;int at=0,chara=-1,costume=-1,i,t=-1,member;BtlActJChr *c;PortFusionReceipt *r;
 if(!read){read=1;spec=getenv("BT3_DEFUSE_PREVIEW");}
 if(!spec||Port_NetActive()||sDefusePreviewState==2)return;
 if(sDefusePreviewState==1){
  /* recording: from the merge until the fight is back, plus a second */
  if(sDefuse[0].stage||sFusionReceipts[0].state){sDefusePreviewTail=0;return;}
  if(++sDefusePreviewTail>=30){sDefusePreviewRec=0;sDefusePreviewState=2;fprintf(stderr,"bt3: defuse preview: done\n");}
  return;
 }
 {static int calls;if(calls++%120==0)fprintf(stderr,"bt3: defuse preview: waiting (sequence %d, fight updates %d, fighters %d, actor0 free %d, teams %d/%d)\n",
   BtlSeq_GetState(),sDefusePreviewTicks,BtlChar_GetCount(),BtlChar_GetCount()>0?BtlChar_IsFree(BtlChar_Get(0)):-1,Port_Team(0),BtlChar_GetCount()>2?Port_Team(2):-1);}
 if(BtlSeq_GetState()!=3)return;
 sscanf(spec,"%d,%d,%d",&at,&chara,&costume);
 if(++sDefusePreviewTicks<at)return;
 c=BtlChar_GetCount()>0?BtlChar_Get(0):NULL;if(!c||!BtlChar_IsFree(c))return;
 for(i=2;i<BtlChar_GetCount()&&i<12;i++)if(Port_Team(i)==Port_Team(0)){t=i;break;}
 if(t<0){fprintf(stderr,"bt3: defuse preview: actor 0 has no ally (BT3_EXTRA_FIGHTERS=<chara>:0)\n");sDefusePreviewState=2;return;}
 r=&sFusionReceipts[0];r->slot=pf_rd(c,0x994);member=r->slot==1?2:1;
 pf_copy(r->leader,BtlMember_GetActive(c),0xA4);pf_copy(r->partner,BtlMember_GetActive(BtlChar_Get(t)),0xA4);
 if(chara>=0){pf_wr(r->leader,0,chara);pf_wr(r->leader,4,costume>=0?costume:0);}
 if(!Port_DefusePreviewLink(0,member,t)){sDefusePreviewState=2;return;}
 r->member=member;r->kind=0xF1;r->remaining=0;r->total=30*65536;r->state=1;
 sDefusePreviewState=1;sDefusePreviewRec=1;
 fprintf(stderr,"bt3: defuse preview: actor%d merged into actor0 (member %d), original body %d costume %d\n",t,member,pf_rd(r->leader,0),pf_rd(r->leader,4));
}
#endif
