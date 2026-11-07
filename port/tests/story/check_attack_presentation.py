from pathlib import Path
import subprocess,tempfile
r=Path.cwd()
def fun(path,name):
 s=(r/path).read_text();a=s.index(name);i=s.index('{',a)+1;n=1
 while n:n+=(s[i]=='{')-(s[i]=='}');i+=1
 return s[a:i]
freeze=fun('src/battle/btl_char_get.c','s32 BtlChar_IsFrozen(')
reaction=fun('src/battle/btl_act_e.c','void Port_StoryBeamVictim(')
code=r"""
#include <assert.h>
#include <stdio.h>
#define PORT 1
typedef int s32;typedef struct {int player,freeze,flags[512];}BtlCharGetChr;typedef BtlCharGetChr BtlActEChr;
BtlActEChr fighters[4];int gPortTarget[4];int consumed,held=1,hit,attacker,damage,damageFlags;
int Port_FusionConsumedActor(int a){return consumed;}
int Port_StoryHeld(int a){return held;}
BtlActEChr*BtlChar_Get(int a){return a>=0&&a<4?fighters+a:0;}
void BtlChar_ClearFlag(BtlActEChr*c,int f){c->flags[f]=0;}
void Port_AssistRushVictim(int who,int winner,int amount){hit=who;attacker=winner;assert(!amount);}
int BtlMember_Damage(BtlActEChr*c,int amount,int flags){damage=amount;damageFlags=flags;assert(gPortTarget[c-fighters]==2);return amount;}
"""+freeze+reaction+r"""
int main(){fighters[0].player=0;assert(held&&!BtlChar_IsFrozen(fighters));
 fighters[0].freeze=1;assert(BtlChar_IsFrozen(fighters));fighters[0].freeze=0;
 consumed=1;assert(BtlChar_IsFrozen(fighters));consumed=0;
 fighters[0].flags[0xFA]=fighters[0].flags[0xFB]=fighters[0].flags[17]=1;
 Port_StoryBeamVictim(0,2,40000);assert(!fighters[0].flags[0xFA]&&!fighters[0].flags[0xFB]&&fighters[0].flags[17]);
 assert(hit==0&&attacker==2&&damage==40000&&damageFlags==0x1C3&&!(damageFlags&8)&&gPortTarget[0]==0);puts("PASS actual native freeze/reaction: logical grip remains hittable, native freeze/consumed protection intact, scene-pose requeue cleared before native damage/blow-away");
}
"""
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.c').write_text(code);subprocess.run(['cc','-fsanitize=address,undefined',str(p/'test.c'),'-o',str(p/'test')],check=True);subprocess.run([str(p/'test')],check=True)
s=(r/'src/battle/btl_char_mgr.c').read_text();a=s.index('void BtlChar_UpdateCamera(');b=s.index('/* Phase 4 body 7',a) if '/* Phase 4 body 7' in s[a:] else a+2200
assert s.index('Port_StoryFxFrame(chr->side)',a)<s.index('BtlFx_UpdateAll(chr)',a)
assert s.index('BtlCharSnd_ClearRequests();')<s.index('BtlChar_UpdateCamera(BtlChar_Get(i));')<s.index('BtlCharSnd_PlayRequests();')
print('PASS real frame order: queued scene sound/FX delivered after native queue clears and before FX update/sound playback')
