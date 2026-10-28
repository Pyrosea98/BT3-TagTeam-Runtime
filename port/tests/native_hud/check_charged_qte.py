from pathlib import Path
import re,subprocess,tempfile
r=Path.cwd();hit=(r/'src/battle/btl_char_hit.c').read_text();clash=(r/'src/battle/btl_char_flag_clash.c').read_text()
def ex(s,n):
 a=re.search(r'(?m)^(?:static )?s32 '+n+r'\(|(?m:^void '+n+r'\()',s).start();i=s.index('{',a)+1;d=1
 while d:d+=(s[i]=='{')-(s[i]=='}');i+=1
 return s[a:i]
h=r"""#include <cassert>
#include <cstring>
using s32=int;using f32=float;
#define PORT 1
struct HitStatus{int target;};struct HitChr{int player,action,stopLen,stopDelay;float unkD78;HitStatus status;};struct HitGauge{int health;};struct BtlClashPath{int end;float*points;int first;};
static HitChr actors[10];static HitGauge gauges[10];static BtlClashPath paths[4];static float points[48];static int flags[10],team[10],net,replay,mode,state=3,busy,pathCount=4,paired=-1,shakes;
int BtlChar_GetCount(){return 10;}HitChr*BtlChar_Get(int i){return actors+i;}int BtlAct_GetCurrent(HitChr*c){return c->action;}int Port_NetActive(){return net;}int BattleReplay_IsActive(){return replay;}int Battle_GetMode(){return mode;}int BtlSeq_GetState(){return state;}int Port_ClashBusy(){return busy;}int Port_IsEnemy(int a,int b){return team[a]!=team[b];}int Port_ClashPair(int a,int b){paired=a*10+b;return !busy;}
HitGauge*BtlMember_GetActiveGauge(HitChr*c){return &gauges[c->player];}int BtlStage_GetPathCount(){return pathCount;}BtlClashPath*BtlStage_GetPath(int i){return paths+i;}void BtlChar_SetFlag(HitChr*c,int bit){assert(bit==0x62);flags[c->player]++;}void BtlChar_SetHeldFlag(HitChr*,int){}void BtlChar_SetFxBit(HitChr*,int){}void ChrCam_AddShake(HitChr*,float,float){shakes++;}void BtlChar_Vibrate(HitChr*,float,float){}int BtlAtk_GetPriority(HitChr*){return 1;}
void reset(){memset(flags,0,sizeof(flags));net=replay=mode=busy=shakes=0;state=3;paired=-1;pathCount=4;for(int i=0;i<10;i++){actors[i]={i,0x47,0,0,1.f,{-1}};gauges[i].health=100;team[i]=i&1;}for(auto&p:paths)p={4,points,0};}
"""
f=r"""int main(){for(int a=0;a<10;a++)for(int b=a+1;b<10;b++)if((a&1)!=(b&1)){reset();actors[a].status.target=b;actors[b].status.target=a;BtlHit_ResolveTrades();assert(paired==a*10+b&&flags[a]==1&&flags[b]==1&&actors[a].status.target==-1&&actors[b].status.target==-1&&actors[a].stopLen==2&&!shakes);}
for(int gate=0;gate<12;gate++){reset();actors[0].status.target=1;actors[1].status.target=0;if(gate==0)net=1;if(gate==1)replay=1;if(gate==2)mode=1;if(gate==3)state=2;if(gate==4)busy=1;if(gate==5)team[1]=0;if(gate==6)actors[0].action=11;if(gate==7)actors[0].unkD78=0;if(gate==8)gauges[1].health=0;if(gate==9)pathCount=2;if(gate==10)paths[2].end=1;if(gate==11)actors[1].status.target=-1;BtlHit_ResolveTrades();assert(paired==-1&&!flags[0]&&!flags[1]);}
reset();actors[0].status.target=1;actors[1].status.target=0;actors[2].status.target=1;actors[3].status.target=4;BtlHit_ResolveTrades();assert(actors[2].status.target==-1&&actors[3].status.target==4);
for(int a=0x47;a<=0x4C;a++)for(int b=0x47;b<=0x4C;b++){reset();actors[0].action=a;actors[1].action=b;actors[0].unkD78=.5f;assert(Port_ChargedQteTrade(actors,actors+1));}
}"""
with tempfile.TemporaryDirectory()as d:
 p=Path(d);(p/'t.cpp').write_text(h+ex(clash,'Port_VanishPathsReady')+ex(hit,'Port_ChargedQteTrade')+ex(hit,'BtlHit_ResolveTrades')+f);subprocess.run(['g++','-fsanitize=address,undefined',str(p/'t.cpp'),'-o',str(p/'t')],check=True);subprocess.run([str(p/'t')],check=True)
print('PASS actual charged QTE contact/admission:25 enemy pairs,36smash pairs, reciprocal-frame only, positive charge, flags/pair/hit suppression,12safety scopes, valid path guards and unrelated hits preserved; ASan/UBSan')
