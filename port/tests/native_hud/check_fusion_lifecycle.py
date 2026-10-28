from pathlib import Path
import re,subprocess,tempfile
r=Path.cwd();src=(r/'port/src/plat_fighters.c').read_text()
def fn(name):
 m=re.search(r'^(?:static )?(?:void|int) '+name+r'\([^;{}]*\)\s*\{',src,re.M);assert m,name;i=src.index('{',m.start())+1;d=1
 while d:d+=(src[i]=='{')-(src[i]=='}');i+=1
 return src[m.start():i]
h=r'''#include <cstdio>
#include <cmath>
#include <cstring>
#include <cassert>
struct FightExtra{int member,consumed;};FightExtra sFight[8];int sFightCount=6;
int sFusionMirror[12][5],sFusionOwner[12],sFusionLink[12][5];unsigned char actors[8][0x1600];int human[8],teams[8]={0,1,0,1,0,1,0,1},shown[8];
int BtlChar_GetCount(){return 8;}int Port_Team(int i){return teams[i];}int Port_TargetHuman(int i){return human[i];}int Port_FusionRosterReady(){return 1;}
void*BtlChar_Get(int i){return actors[i];}void*BtlMember_GetActive(void*p){return (char*)p+0x9a4;}void*BtlChar_GetPos(void*p){return (char*)p+16;}
int Port_FighterObjId(int i){return i;}void*BtlObj_Get(int i){return actors[i];}void BtlObj_SetPartVisible(void*p,unsigned,int on){shown[((unsigned char*)p-actors[0])/0x1600]=on;}
void Port_ReviveSpectator(int){}void BtlAct_Request(void*,int){}

float BtlCharApi_GetRadius(int){return 4;}
int Port_AssistSafePoint(int,float*){return 1;}
int Port_AssistCommitPosition(int who,const float*point,float yaw){float*p=(float*)BtlChar_GetPos(BtlChar_Get(who));memcpy(p,point,4*sizeof(float));p[5]=yaw;p[0x98/4]=p[0x9c/4]=0;return 1;}
void BtlChar_ClearFlag(void*,int){}void Port_DefusionBurst(int){}void BtlObjBody_Warp(void*,void*){}
'''
b='\n'.join(fn(n) for n in ['Port_FusionPartnerActor','Port_FusionConsumedActor','Port_FusionViewOwner','Port_FusionViews','Port_ExtraRoundReset','Port_TeamFillFusion','Port_ExtraFusionConsume','Port_ExtraConsumedUpdate','Port_ExtraFusionRelease'])
b+='\nstruct BtlCharGetChr{int player,freeze;};typedef int s32;\n#define PORT 1\n'+'s32 BtlChar_IsFrozen(BtlCharGetChr *chr) {\n#ifdef PORT\n    {extern int Port_FusionConsumedActor(int);if(Port_FusionConsumedActor(chr->player))return 1;}\n#endif\n    return chr->freeze > 0;\n}'
t=r'''int main(){
for(int players=2;players<=4;players++)for(int initiator=0;initiator<players;initiator++)for(int partner=0;partner<players;partner++)if(initiator!=partner){
 Port_ExtraRoundReset();memset(actors,0,sizeof(actors));memset(human,0,sizeof(human));int ids[4]={0,2,4,6};
 for(int i=0;i<8;i++){*(int*)actors[i]=i;*(int*)(actors[i]+4)=-1;auto*m=(int*)((char*)BtlMember_GetActive(actors[i]));m[0]=51+i;m[2]=1;m[16]=50000;}
 for(int i=0;i<6;i++){sFight[i].member=(teams[i+2]*8)+(i/2+1);}
 for(int i=0;i<players;i++){human[ids[i]]=1;*(int*)(actors[ids[i]]+4)=i;teams[ids[i]]=0;}
 // All tested allies on one team, assigned real native members for primary initiator.
 sFight[0].member=1;sFight[2].member=2;if(players==4)sFight[4].member=3;
 int a=ids[initiator],p=ids[partner];
 Port_TeamFillFusion(actors[a]);int slot=-1;for(int j=0;j<5;j++)if(Port_FusionPartnerActor(a,j)==p)slot=j;assert(slot>=0);
 unsigned char saved[0xa4];memcpy(saved,BtlMember_GetActive(actors[p]),0xa4);assert(Port_FusionViews(-1)==-1);
 Port_ExtraFusionConsume(a,slot);BtlCharGetChr probe={p,0};assert(BtlChar_IsFrozen(&probe));assert(Port_FusionConsumedActor(p)&&Port_FusionViewOwner(p)==a);assert(Port_FusionViews(-1)==players-1);
 int seen[12]={};for(int q=0;q<players-1;q++){int id=Port_FusionViews(q);assert(!seen[id]++&&id!=p);}assert(seen[a]);
 // A later object/cinematic visibility reset cannot expose the consumed body.
 shown[p]=1;Port_ExtraConsumedUpdate();assert(!shown[p]&&((float*)BtlChar_GetPos(actors[p]))[1]==-100000);
 assert(Port_ExtraFusionRelease(a,slot,saved,actors[a]));assert(!Port_FusionConsumedActor(p)&&Port_FusionViews(-1)==-1&&shown[p]);assert(!BtlChar_IsFrozen(&probe));probe.freeze=3;assert(BtlChar_IsFrozen(&probe));
 assert(!Port_ExtraFusionRelease(a,slot,saved,actors[a]));
}
puts("PASS actual live-actor lifecycle: 2/3/4-player unique view collapse, either ally initiates, consumed-body rehide, one-shot defusion and view restore");}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'lifecycle.cpp').write_text(h+b+t);subprocess.run(['g++','-std=c++17','-fsanitize=address,undefined',str(p/'lifecycle.cpp'),'-o',str(p/'lifecycle')],check=True);subprocess.run([str(p/'lifecycle')],check=True)
# Actual mutual-consent include: the admission/queue callbacks isolate native scheduling.
h=r'''#include <cstdio>
#include <cassert>
#include <cstring>
typedef int s32;struct Member{int chara;};struct Gauge{int health;};struct BtlActJChr{int player;struct {int allowed;}form;} actors[4];Member members[4];Gauge gauges[4];int human[4]={1,1,1,1},net,replay,seq=3,freeBody[4]={1,1,1,1},can=1,queued,requested;
int Port_TargetHuman(int i){return human[i];}int Port_FusionPartnerActor(int who,int member){return member==1?(who^1):-1;}int Port_FusionConsumedActor(int){return 0;}
int Port_NetActive(){return net;}int BattleReplay_IsActive(){return replay;}int BtlSeq_GetState(){return seq;}int BtlChar_GetCount(){return 4;}BtlActJChr*BtlChar_Get(int i){return actors+i;}
Member*BtlMember_GetActive(BtlActJChr*c){return members+c->player;}Gauge*BtlMember_GetActiveGauge(BtlActJChr*c){return gauges+c->player;}int BtlChar_IsFree(BtlActJChr*c){return freeBody[c->player];}
int actionNow[4]={11,11,11,11};int BtlAct_GetCurrent(BtlActJChr*c){return actionNow[c->player];}int BtlChar_IsFrozen(BtlActJChr*){return 0;}int ChrCam_IsCutActive(BtlActJChr*){return 0;}
int BtlAct_CanFuse(BtlActJChr*,int,int,int,int*m){*m=1;return can;}int BtlAct_GetQueued(BtlActJChr*){return 0xf1;}void BtlAct_Request(BtlActJChr*,int a){requested=a;}void Port_FusionLog(const char*,...){}
int BtlAct_QueueFusion(BtlActJChr*,int,int);
'''
b=(r/'port/include/mod_fusion_consent.inc').read_text();t=r'''
int BtlAct_QueueFusion(BtlActJChr*c,int slot,int member){if(Port_FusionInvite(c,slot,member))return 0;queued++;return 1;}
int main(){for(int i=0;i<4;i++){actors[i]={i,{1}};members[i].chara=51+i;gauges[i].health=50000;}
for(int who=0;who<4;who++){Port_FusionConsentReset();queued=requested=0;int p=who^1;
 // Preheld R3 cannot silently accept. Directions are irrelevant; only a new R3 edge accepts.
 Port_FusionConsentInput(p,4,4);assert(!BtlAct_QueueFusion(actors+who,0,1));assert(Port_FusionConsentHud(p)&&!Port_FusionConsentHud(who));
 assert(Port_FusionConsentInput(p,4,4)==4);Port_FusionConsentTick();assert(!queued);
 Port_FusionConsentInput(p,0,4);freeBody[p]=0;assert(Port_FusionConsentInput(p,4,4)==0);Port_FusionConsentTick();assert(!queued&&Port_FusionConsentHud(p));
 freeBody[p]=1;actionNow[p]=261;Port_FusionConsentTick();assert(Port_FusionConsentChoice(actors+who)==-1&&!queued);actionNow[p]=11;assert(Port_FusionConsentChoice(actors+who)==0);assert(BtlAct_QueueFusion(actors+who,0,1));assert(queued==1&&!Port_FusionConsentHud(p));assert(Port_FusionConsentInput(p,4,4)==0);assert(Port_FusionConsentInput(p,0,4)==0);
 assert(!BtlAct_QueueFusion(actors+who,0,1));for(int j=0;j<121;j++)Port_FusionConsentTick();assert(!Port_FusionConsentHud(p)&&queued==1);
 assert(!BtlAct_QueueFusion(actors+who,0,1));members[p].chara++;Port_FusionConsentTick();assert(!Port_FusionConsentHud(p));members[p].chara--;
}
net=1;assert(BtlAct_QueueFusion(actors,0,1));net=0;replay=1;assert(BtlAct_QueueFusion(actors,0,1));
puts("PASS actual R3 consent: all4 actor identities, fresh press only, pending overhead owner, busy acceptance deferred, release consumed, timeout/form-change cancel, online/replay unchanged");}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'consent.cpp').write_text(h+'\n#define PORT 1\n'+b+t);subprocess.run(['g++','-std=c++17','-fsanitize=address,undefined',str(p/'consent.cpp'),'-o',str(p/'consent')],check=True);subprocess.run([str(p/'consent')],check=True)
