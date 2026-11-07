from pathlib import Path
import subprocess,tempfile
r=Path.cwd();s=(r/'port/include/mod_story_scene.inc').read_text();a=s.index('int Port_StoryBeamChargeGate(');i=s.index('{',a)+1;n=1
while n:n+=(s[i]=='{')-(s[i]=='}');i+=1
code="""#include <assert.h>
#include <stdio.h>
static int ch=11,sStAtkFrames=1,sStAtkBy=0,sStAtkBack=1;static unsigned sStPairChargeReady;
int Port_StoryChapter(void){return ch;}
"""+s[a:i]+"""
int main(){assert(Port_StoryBeamChargeGate(3)==0&&!sStPairChargeReady);assert(Port_StoryBeamChargeGate(0)==1);assert(Port_StoryBeamChargeGate(0)==1);assert(Port_StoryBeamChargeGate(1)==2);assert(Port_StoryBeamChargeGate(0)==2);sStAtkFrames=0;assert(!Port_StoryBeamChargeGate(0));sStAtkFrames=1;ch=-1;assert(!Port_StoryBeamChargeGate(1));ch=11;sStAtkBy=14;sStAtkBack=15;sStPairChargeReady=0;assert(Port_StoryBeamChargeGate(15)==1&&Port_StoryBeamChargeGate(14)==2&&Port_StoryBeamChargeGate(15)==2);puts("PASS actual Story beam barrier: both native charge loops required; release latched for both actors0..15, other fighters/modes untouched");}
"""
with tempfile.TemporaryDirectory() as d:
 p=Path(d)/'sync';p.with_suffix('.c').write_text(code);subprocess.run(['cc','-fsanitize=address,undefined',str(p.with_suffix('.c')),'-o',str(p)],check=True);subprocess.run([str(p)],check=True)

# Native struggles deal damage outside projectile collision; Story must receive the exact decided class.
native=(r/'src/battle/btl_char_flag_clash.c').read_text()
a=native.index('BtlMember_Damage(loser, damage, flags);')
b=native.index('BtlChar_PlayVoice(loser, 0x16);',a)
assert 'Port_StoryAttackContact(winner->player,loser->player,0x100+cls)' in native[a:b]
print('PASS native beam outcome reports exact winner/class after real damage, before reactions')

# CH8 (Juan): Goku's Kaioken x4 ultimate runs BtlAct_SuperRepeatHandler (0x112..0x114), Vegeta's Galick Gun the beam
# handler. Every native handler whose charge loop fires on flag 0xA7 must hold for the Story pair, or the x4 fires
# while Vegeta still charges and lands with no struggle (native run evidence: action 0x114 vs 0x108, then 0xC0 hit).
act=(r/'src/battle/btl_act_f.c').read_text()
for name in ['void BtlAct_SuperRepeatHandler(']:
 a=act.index(name);b=act.index('\nvoid ',a+10);body=act[a:b]
 loop=body.index('case 0x14A:');fire=body.index('if (BtlChar_TestFlag(chr, 0xA7))',loop)
 gate=body.find('Port_StoryBeamChargeGate(chr->player)',loop)
 assert loop<gate<fire,name
 assert 'if(gate==1){*timer=0;BtlChar_ClearFlag(chr,0xA7);}' in body[gate:fire] and 'else if(gate==2)BtlChar_SetHeldFlag(chr,0xA7);' in body[gate:fire]
print('PASS native Kaioken x4 repeat ultimate holds at its charge loop for the Story beam pair, released together with the partner')
