/*
 * The fight panels (gs/ui.cpp): the side bars of a 4:3 fight in a wide window show each side's fighter, its team and
 * its gauges. This file reads them once per blank on the game's thread (Port_VBlank, plat_stub.c) into a copy the
 * render thread takes (PortPanels_Read).
 *
 * It only reads: nothing here writes the game's memory or draws a random number, so the game runs the same with it
 * on or off and an online session stays in step. Blanks being run again after a rollback are skipped. Its own
 * variables are not game state.
 *
 * The fighter is read at its offsets (include/battle/btl_char_member.h: BtlMemberChr, BtlMember, BtlMemberGauge);
 * the port's files do not include the game's headers.
 */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "gs/fight_panels.h"
#include "plat_beta8.h"

#if defined(__x86_64__) && defined(__clang__)
#undef __ptr32 /* the Windows headers of mingw define these two away */
#undef __uptr
#define GAME_PTR *__ptr32 __uptr
#else
#define GAME_PTR *
#endif

static int sSceneVisible;
void PortPanels_SetSceneVisible(int visible) { __atomic_store_n(&sSceneVisible,visible!=0,__ATOMIC_RELEASE); }

extern int gPortResim; /* gs/state.c: blanks being run again */

/* src/battle/btl_char_mgr.c / btl_seq.c: there from Battle_Init to Battle_Term, NULL outside (see a11y_game.c) */
extern void GAME_PTR gBtlChars;
extern void GAME_PTR gBtlSeq;
extern void GAME_PTR Battle_GetWork(void);
typedef struct {
    char unk0[0x30];
    uint32_t flags; /* 0x30: HUD_SHOW_* */
} FpHud;
extern FpHud GAME_PTR gHud; /* include/battle/hud_a.h; read only while this battle's roster and sequence are there */
#define HUD_SHOW_GAUGES 0x01

extern void GAME_PTR BtlChar_FindBySide(int side);
extern int BtlCharApi_GetChara(int objId);
extern int BtlChar_TestFlag(void GAME_PTR chr, unsigned n);
#define CHR_POWERED_UP 6

extern int Port_FilePath(int ptid, int flid, const char *fname, char *out, int size); /* plat_file.c */
#define FACE_FILE 0x2F9 /* the large portraits (the character select's): file FACE_FILE + character of partition 1 */
#define CHARA_MAX 291 /* the grid's marker cells and up are not characters */

/* include/battle/btl_char_member.h: BtlMemberChr */
#define CHR_ACTIVE 0x994
#define CHR_MEMBER_COUNT 0x998
#define CHR_SWITCH_TARGET 0x9A0
#define CHR_MEMBERS 0x9A4
#define MEMBER_SIZE 0xA4
#define MEMBER_CHARA 0x00
#define MEMBER_COSTUME 0x04
#define MEMBER_PRESENT 0x08
#define MEMBER_GAUGE 0x40 /* hp, hpMax, unk8, ki, kiMax, blast, blastMax, maxPower */
#define CHR_COMBO 0xD40   /* damage, hits, timer: the combo landing on this fighter */

static FpState sState;           /* the copy the render thread reads */
static volatile unsigned sSeq;   /* odd while it is written */
static int sWritten;

/* Which portrait each slot's path is for, so the files are looked up only when a member changes. */
static int sPathKey[2][FP_MEMBERS];
static char sPath[2][FP_MEMBERS][FP_PATH];

static int rd(uint8_t GAME_PTR p, int off) {
    return *(int GAME_PTR)(p + off);
}

static void portrait_path(int side, int slot, int chara, int costume) {
    int key = (chara << 8 | (costume & 0xFF)) + 1, id;

    if (sPathKey[side][slot] == key) {
        return;
    }
    sPathKey[side][slot] = key;
    sPath[side][slot][0] = '\0';
    if (chara < 0 || chara >= CHARA_MAX) {
        return;
    }
    id=Port_Beta8PortraitFileId(chara,costume);
    if(id>=0){if(!Port_FilePath((id>>24)&15,id&0xffffff,NULL,sPath[side][slot],FP_PATH))sPath[side][slot][0]=0;}
    else if(!Port_FilePath(1,FACE_FILE+chara-1,NULL,sPath[side][slot],FP_PATH))sPath[side][slot][0]=0;

}

static void read_side(FpState *st, int side) {
    FpSide *s = &st->side[side];
    uint8_t GAME_PTR chr = (uint8_t GAME_PTR)BtlChar_FindBySide(side);
    uint8_t GAME_PTR foe = (uint8_t GAME_PTR)BtlChar_FindBySide(side ^ 1);
    uint8_t GAME_PTR m;
    int i, objChara;

    memset(s, 0, sizeof(*s));
    s->next = -1;
    if (chr == NULL) {
        return;
    }
    s->active = rd(chr, CHR_ACTIVE);
    s->count = rd(chr, CHR_MEMBER_COUNT);
    if (s->count < 1 || s->count > FP_MEMBERS || s->active < 0 || s->active >= s->count) {
        return;
    }
    s->found = 1;
    objChara = BtlCharApi_GetChara(rd(chr,12));
    for (i = 0; i < s->count; i++) {
        FpMember *o = &s->m[i];
        m = chr + CHR_MEMBERS + i * MEMBER_SIZE;
        o->present = rd(m, MEMBER_PRESENT) != 0;
        o->chara = i == s->active && objChara >= 0 && objChara < CHARA_MAX ? objChara : rd(m, MEMBER_CHARA);
        o->costume = rd(m, MEMBER_COSTUME);
        o->hp = rd(m, MEMBER_GAUGE + 0x00);
        o->hpMax = rd(m, MEMBER_GAUGE + 0x04);
        if (o->present) {
            portrait_path(side, i, o->chara, o->costume);
            memcpy(o->path, sPath[side][i], FP_PATH);
        }
    }
    m = chr + CHR_MEMBERS + s->active * MEMBER_SIZE;
    s->ki = rd(m, MEMBER_GAUGE + 0x0C);
    s->kiMax = rd(m, MEMBER_GAUGE + 0x10);
    s->blast = rd(m, MEMBER_GAUGE + 0x14);
    s->blastMax = rd(m, MEMBER_GAUGE + 0x18);
    s->maxPower = rd(m, MEMBER_GAUGE + 0x1C);
    s->poweredUp = BtlChar_TestFlag(chr, CHR_POWERED_UP) != 0;
    i = rd(chr, CHR_SWITCH_TARGET); /* (raw: BtlMember_GetSwitchTarget would move it) */
    if (i >= 0 && i < s->count && i != s->active && s->m[i].present && s->m[i].hp > 0) {
        s->next = i;
    }
    if (foe != NULL) {
        s->comboDamage = rd(foe, CHR_COMBO + 0x00);
        s->comboHits = rd(foe, CHR_COMBO + 0x04);
        s->comboTimer = rd(foe, CHR_COMBO + 0x08);
    }
}

typedef struct FpPoint {float x,y,z,w;} __attribute__((aligned(16))) FpPoint;
extern void GAME_PTR BtlChar_Get(int);
extern void GAME_PTR BtlMember_GetActive(void GAME_PTR);
extern int BtlChar_GetCount(void),Port_FightConsumed(int),Port_Team(int),Port_TargetHuman(int),Port_FusionClockSeconds(int);
extern int Port_ModHudViewCount(void),Port_ModHudProject(int,FpPoint*,int*,int*),Port_HudPartFighter(int),Port_CamTarget(int),Port_QuadMode(void);
extern void BtlCharApi_GetNodePos(int,int,FpPoint*);
extern const char *Port_CharaName(int);
extern int Port_Setting(const char*,int);
static FpActor sHudLastAlive[FP_ACTORS];
static void read_fusion_partners(FpState *st){
 extern int Port_FusionPartnerInfo(void*,int*,int*,int*);
 extern void *BtlMember_Get(void*,int);
 int i,q,realCount=st->actorCount;
 for(i=0;i<realCount;i++){
  void *c=BtlChar_Get(i);int source,obj,member,slot;unsigned char *m;FpActor *a;FpPoint head;
  if(!c||!Port_FusionPartnerInfo(c,&source,&obj,&member))continue;
  m=(unsigned char*)BtlMember_Get(c,member);if(!m)continue;
  slot=source>=0&&source<realCount?source:st->actorCount;
  if(slot>=FP_ACTORS)continue;
  a=&st->actor[slot];
  if(!a->present&&source>=0&&sHudLastAlive[source].chara==rd(m,0)&&sHudLastAlive[source].costume==rd(m,4))*a=sHudLastAlive[source];
  if(!a->present){memset(a,0,sizeof(*a));a->present=1;a->chara=rd(m,0);a->costume=rd(m,4);a->hp=rd(m,0x40);a->hpMax=rd(m,0x44);a->ki=rd(m,0x4c);a->kiMax=rd(m,0x50);a->blast=rd(m,0x54);a->blastMax=rd(m,0x58);a->team=Port_Team(i);a->fusionMove=a->fusionAttack=-1;snprintf(a->name,sizeof(a->name),"%s",Port_CharaName(a->chara));}
  if(slot==st->actorCount)st->actorCount++;
  BtlCharApi_GetNodePos(obj,0x30,&head);
  for(q=0;q<st->viewCount;q++)a->visible[q]=Port_ModHudProject(q,&head,&a->x[q],&a->y[q]);
 }
}
static void read_revive_rings(FpState*st){
 extern int Port_Setting(const char*,int),Port_NetActive(void),BattleReplay_IsActive(void),BtlChar_FrameMod(int);
 extern float BtlCharApi_GetGroundY(int);extern void*BtlChar_GetPos(void*);
 static float circle[25][2];static int made;int i,j,q,radius,height,speed;float phase,wave;
 if(!Port_Setting("revive_enabled",1)||!Port_Setting("show_revive_ring",1)||Port_NetActive()||BattleReplay_IsActive())return;
 if(!made){for(j=0;j<25;j++){float angle=j*6.283185307f/24;circle[j][0]=cosf(angle);circle[j][1]=sinf(angle);}made=1;}
 radius=Port_Setting("revive_radius",80);if(radius<5)radius=5;if(radius>400)radius=400;
 height=Port_Setting("revive_ring_wave_height",8);if(height<0)height=0;if(height>40)height=40;
 speed=Port_Setting("revive_ring_wave_speed_percent",100);if(speed<10)speed=10;if(speed>400)speed=400;
 phase=BtlChar_FrameMod(30000)*speed*.0003333333f;wave=.5f+.5f*sinf(phase*6.283185307f);
 for(i=0;i<BtlChar_GetCount()&&i<FP_ACTORS;i++){
  void*c=BtlChar_Get(i),*m=c?BtlMember_GetActive(c):0;float*p,y;FpReviveRing*ring=&st->reviveRing[i];
  if(!m||!rd(m,8)||rd(m,0x40)>0||rd(m,0x44)<=0||(i>=2&&Port_FightConsumed(i-2)))continue;
  p=(float*)BtlChar_GetPos(c);if(!p)continue;y=BtlCharApi_GetGroundY(rd(c,12));if(!(y>=-1000000&&y<=1000000))continue;
  ring->active=1;ring->team=Port_Team(i);
  for(j=0;j<25;j++){FpPoint point={p[0]+circle[j][0]*radius,y-1-height*wave,p[2]+circle[j][1]*radius,1};
   for(q=0;q<st->viewCount;q++)ring->visible[q][j]=Port_ModHudProject(q,&point,&ring->x[q][j],&ring->y[q][j]);
  }
 }
}
static void read_actors(FpState *st){
 int i,q,n=Port_ModHudViewCount(),quad=Port_QuadMode();FpPoint head;
 if(n<1||n>4)n=1;st->viewCount=n;st->actorCount=BtlChar_GetCount();if(st->actorCount>12)st->actorCount=12;
 for(q=0;q<n;q++){
  FpView *v=&st->view[q];v->x=n==1?0:(q&1)*256;v->y=n>2&&q>=2?224:0;
  v->w=n==1||(n==3&&q==2)?512:256;v->h=n>2?224:448;v->me=Port_HudPartFighter(q);v->target=Port_CamTarget(v->me);{extern int Port_TakeoverHud(int);v->takeover=Port_TakeoverHud(v->me);}
 }
 for(i=0;i<st->actorCount;i++){
  uint8_t GAME_PTR c=(uint8_t GAME_PTR)BtlChar_Get(i);uint8_t GAME_PTR m;FpActor *a=&st->actor[i];
  a->team=Port_Team(i);
  if(!c||(i>=2&&Port_FightConsumed(i-2)))continue;m=(uint8_t GAME_PTR)BtlMember_GetActive(c);if(!m||!rd(m,8)||rd(m,0x40)<=0)continue;
  a->present=1;a->chara=rd(m,0);a->costume=rd(m,4);a->hp=rd(m,0x40);a->hpMax=rd(m,0x44);a->ki=rd(m,0x4c);a->kiMax=rd(m,0x50);
  a->blast=rd(m,0x54);a->blastMax=rd(m,0x58);a->poweredUp=BtlChar_TestFlag(c,6);a->player=Port_TargetHuman(i)?rd(c,4)+1:0;a->team=Port_Team(i);a->fusionSeconds=Port_FusionClockSeconds(i);a->fusionMove=a->fusionAttack=-1;a->fusionSwap=0;
  {extern int Port_FusionControlPads(int,int*,int*),Port_FusionControlSeconds(int);if(Port_FusionControlPads(i,&a->fusionMove,&a->fusionAttack)){a->player=a->fusionAttack+1;a->fusionSwap=Port_FusionControlSeconds(i);}}

  snprintf(a->name,sizeof(a->name),"%s",Port_CharaName(a->chara));
  {extern void Port_ReviveHud(int,int*,int*,int*,int*);Port_ReviveHud(i,&a->reviveTarget,&a->reviveProgress,&a->reviveRecovering,&a->reviveInsufficient);}
  {extern void Port_AssistHud(int,int*,int*,int*);Port_AssistHud(i,&a->assistKind,&a->assistState,&a->assistHelpers);}
  {extern int Port_FusionReadyMask(void GAME_PTR),Port_FusionConsentHud(int),Port_FusionClockProgress(int);a->fusionReadyMask=Port_FusionReadyMask(c);a->fusionAccept=Port_FusionConsentHud(i);a->fusionProgress=Port_FusionClockProgress(i);}
  sHudLastAlive[i]=*a;
  BtlCharApi_GetNodePos(rd(c,12),0x30,&head);
  for(q=0;q<n;q++)a->visible[q]=Port_ModHudProject(q,&head,&a->x[q],&a->y[q]);
 }
 read_revive_rings(st);
}

/* The defuse cinematic's crack, read even while the stock close-up hides the gauges. */
static short crack_clamp(int v){return (short)(v<-32000?-32000:v>32000?32000:v);}
static void read_defuse_lights(FpState *st){
 extern int Port_DefuseLight(void GAME_PTR,int*,int*),Port_DefuseCrack(void GAME_PTR,int,int*,int);
 int i,q,k,n=0,grow,white,buf[6*FP_CRACK_SEGS];
 if(!st->viewCount){int n=Port_ModHudViewCount();if(n<1||n>4)n=1;st->viewCount=n;
  for(q=0;q<n;q++){FpView *v=&st->view[q];v->x=n==1?0:(q&1)*256;v->y=n>2&&q>=2?224:0;v->w=n==1||(n==3&&q==2)?512:256;v->h=n>2?224:448;v->me=Port_HudPartFighter(q);v->target=Port_CamTarget(v->me);}}
 for(i=0;i<BtlChar_GetCount()&&i<FP_ACTORS;i++){
  uint8_t GAME_PTR c=(uint8_t GAME_PTR)BtlChar_Get(i);FpDefuseLight *l;
  if(n>=FP_CRACKS||!c||!Port_DefuseLight(c,&grow,&white))continue;
  l=&st->defuse[n++];l->active=1;
  for(q=0;q<st->viewCount&&q<FP_VIEWS;q++){
   l->count[q]=Port_DefuseCrack(c,q,buf,FP_CRACK_SEGS);
   for(k=0;k<l->count[q];k++){FpCrackSeg *s=&l->seg[q][k];int *b=buf+6*k;
    s->x0=crack_clamp(b[0]);s->y0=crack_clamp(b[1]);s->x1=crack_clamp(b[2]);s->y1=crack_clamp(b[3]);
    s->alpha=(unsigned char)(b[4]<0?0:b[4]>255?255:b[4]);s->width=(unsigned short)(b[5]<1?1:b[5]>4000?4000:b[5]);}
  }
 }
}

void PortPanels_GameTick(void) {
    static FpState st;
    FpHud GAME_PTR h;

    if (gPortResim) {
        return;
    }
    memset(&st, 0, sizeof(st));
    if (__atomic_load_n(&sSceneVisible,__ATOMIC_ACQUIRE) && gBtlChars != NULL && gBtlSeq != NULL) {
        read_side(&st, 0);
        read_side(&st, 1);
        st.battle = st.side[0].found && st.side[1].found;
        h = gHud;
        st.gauges = st.battle && h != NULL && (h->flags & HUD_SHOW_GAUGES) != 0;
        if(st.gauges){uint8_t GAME_PTR work=(uint8_t GAME_PTR)Battle_GetWork();st.paused=work&&(rd(work,0x19F0)&0x100)!=0;if(!work||(rd(work,0x19F0)&(0x4000|0x2000|0x8000)))st.gauges=0;else {read_actors(&st);read_fusion_partners(&st);}}
        if (st.battle && !st.paused) read_defuse_lights(&st);
    }
    if (!st.battle) {
        memset(sHudLastAlive,0,sizeof(sHudLastAlive));
        memset(sPathKey, 0, sizeof(sPathKey)); /* (a mod switched between fights is looked up again) */
    }
    __atomic_add_fetch(&sSeq, 1, __ATOMIC_ACQ_REL);
    memcpy(&sState, &st, sizeof(st));
    __atomic_add_fetch(&sSeq, 1, __ATOMIC_ACQ_REL);
    sWritten = 1;
}

int PortPanels_Read(FpState *out) {
    if (!__atomic_load_n(&sSceneVisible,__ATOMIC_ACQUIRE)) { memset(out,0,sizeof(*out)); return 1; }
    unsigned a, b;
    int tries;

    if (!sWritten) {
        return 0;
    }
    for (tries = 0; tries < 64; tries++) {
        a = __atomic_load_n(&sSeq, __ATOMIC_ACQUIRE);
        if (a & 1) {
            continue;
        }
        memcpy(out, &sState, sizeof(*out));
        __atomic_thread_fence(__ATOMIC_ACQUIRE);
        b = __atomic_load_n(&sSeq, __ATOMIC_ACQUIRE);
        if (a == b) {
            return 1;
        }
    }
    return 0;
}
