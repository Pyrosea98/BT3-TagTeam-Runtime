from pathlib import Path
import re,subprocess,tempfile,os
r=Path.cwd()
def fn(src,name):
 m=re.search(r'^(?:s32|void) '+name+r'\([^;{}]*\)\s*\{',src,re.M);assert m,name;i=src.index('{',m.start())+1;d=1
 while d:d+=(src[i]=='{')-(src[i]=='}');i+=1
 return src[m.start():i]
h=r'''#include <cassert>
#include <cstdio>
#include <cstring>
#include <cmath>
extern "C" {
#include "plat_beta8.h"
int net,replay;int Port_NetActive(){return net;}int BattleReplay_IsActive(){return replay;}
}
using s32=int;using f32=float;
struct FxChr{int player,objId;};struct FxObj{int flags;};struct FxGauge{int ki,kiMax;};FxObj objects[4];FxGauge gauges[4];int charas[4],bits[4],newBits[4],ended[4],paramFlags;
int shakes[4],holds[4],starts[4],stops[4],kills[4];float strength[4],duration[4],levels[4];
FxObj*BtlChar_GetObj(FxChr*c){return objects+c->player;}FxGauge*BtlMember_GetActiveGauge(FxChr*c){return gauges+c->player;}
int BtlParam_GetFlags(FxChr*){return paramFlags;}int BtlMember_HasAnyListedAbility(FxChr*){return 0;}int BtlChar_TestFxBit(FxChr*c,int b){return (bits[c->player]>>b)&1;}int BtlChar_IsFxBitNew(FxChr*c,int b){return (newBits[c->player]>>b)&1;}int BtlChar_IsFxBitEnded(FxChr*c,int b){return (ended[c->player]>>b)&1;}
int BtlCharApi_GetChara(int obj){return charas[obj];}void ChrCam_AddShake(FxChr*c,float s,float t){shakes[c->player]++;strength[c->player]=s;duration[c->player]=t;}
void EftAura_Command(int obj,int cmd){if(cmd==7)kills[obj]++;if(cmd==2)starts[obj]++;if(cmd==3)stops[obj]++;}void EftAura_SetLevel(int obj,float level){levels[obj]=level;}int BtlParam_GetUnk0(FxChr*){return 0;}void Port_EftAuraHoldCharge(int obj){holds[obj]++;}
'''
s=fn((r/'src/battle/btl_char_fx.c').read_text(),'BtlFx_UpdateAura')
t=r'''int main(){FxChr actors[4]={{0,0},{1,1},{2,2},{3,3}};int ids[]={224,225,226,229,230,234,235,238,239,241,244};
for(int id:ids){Port_Beta8ChargeReset();memset(shakes,0,sizeof(shakes));memset(holds,0,sizeof(holds));charas[0]=id;gauges[0]={0,100000};bits[0]=1<<7;newBits[0]=0;
 for(int tick=0;tick<10;tick++)BtlFx_UpdateAura(actors);
 int tier=Port_Beta8EffectTier(id);assert(shakes[0]==10&&holds[0]==10);assert(fabs(strength[0]-(tier==2?3.5f:tier==1?2.f:1.5f))<.001f&&duration[0]==.2f);assert(fabs(Port_Beta8ChargeScale(0)-(tier==2?1.9f:tier==1?1.7f:1.5f))<.001f);assert(levels[0]==1);assert(!shakes[1]&&!holds[1]);
 bits[0]=0;ended[0]=1<<7;BtlFx_UpdateAura(actors);ended[0]=0;for(int tick=0;tick<6;tick++)BtlFx_UpdateAura(actors);assert(Port_Beta8ChargeScale(0)==1&&shakes[0]==10&&stops[0]>0);
}
Port_Beta8ChargeReset();charas[0]=229;charas[1]=225;bits[0]=bits[1]=1<<7;for(int tick=0;tick<10;tick++){BtlFx_UpdateAura(actors);BtlFx_UpdateAura(actors+1);}assert(strength[0]>strength[1]);
int before=shakes[0],hold=holds[0];bits[0]|=1<<0x14;BtlFx_UpdateAura(actors);assert(shakes[0]==before&&holds[0]==hold&&kills[0]>0);bits[0]=1<<7;
net=1;BtlFx_UpdateAura(actors);assert(shakes[0]==before&&holds[0]==hold);net=0;replay=1;BtlFx_UpdateAura(actors);assert(shakes[0]==before&&holds[0]==hold);
puts("PASS actual charge FX consumer: base1.5/SSJ1-2 1.7/SSJ3-4 1.9, 1.5/2/3.5 own-camera shake, held-frame retry, release settle, two actors isolated, kill/online/replay guards");}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.cpp').write_text(h+'\n#define PORT 1\n'+s+t);subprocess.run(['gcc','-ffunction-sections','-fdata-sections','-Iport/include','-c','port/src/plat_beta8.c','-o',str(p/'b8.o')],check=True);subprocess.run(['g++','-Iport/include',str(p/'test.cpp'),str(p/'b8.o'),'-Wl,--gc-sections','-o',str(p/'test')],check=True);subprocess.run([str(p/'test')],check=True)
# Native flame hold: no repeated burst timer reset and safe delayed task creation.
src=(r/'src/battle/eft_n.c').read_text();h=r'''#include <cassert>
#include <cstdio>
using s32=int;struct EftAuraWork{int flags,burst;float burstTime;};struct EftNTask{EftAuraWork*work;};struct EftAuraMgr{int count;EftNTask*tasks[4];};EftAuraMgr pool;void*gEftAuraPool=&pool;
#define PORT 1
#define gPool ((EftAuraMgr*)gEftAuraPool)
''';b='\n'.join(fn(src,n) for n in ['EftAura_StartBurst','Port_EftAuraHoldCharge','EftAura_EndBurst']);t=r'''int main(){pool.count=2;Port_EftAuraHoldCharge(0);EftAuraWork a={0,0,5};EftNTask task={&a};pool.tasks[0]=&task;Port_EftAuraHoldCharge(0);assert(a.burst==0);a.flags=1;Port_EftAuraHoldCharge(0);assert(a.burst==1&&a.burstTime==0);a.burstTime=5;for(int i=0;i<60;i++)Port_EftAuraHoldCharge(0);assert(a.burstTime==5);EftAura_EndBurst(0);assert(a.burst==4);Port_EftAuraHoldCharge(0);assert(a.burst==1&&a.burstTime==0);Port_EftAuraHoldCharge(-1);Port_EftAuraHoldCharge(2);gEftAuraPool=nullptr;Port_EftAuraHoldCharge(0);puts("PASS actual flame hold: missing/inactive task retry, persistent burst without timer reset, native release, object bounds and absent pool");}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.cpp').write_text(h+b+t);subprocess.run(['g++','-fsanitize=address,undefined',str(p/'test.cpp'),'-o',str(p/'test')],check=True);subprocess.run([str(p/'test')],check=True)
