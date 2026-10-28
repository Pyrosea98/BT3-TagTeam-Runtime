from pathlib import Path
import subprocess,tempfile
r=Path.cwd()
def function(s,name):
 a=s.index('void '+name+'(');i=s.index('{',a)+1;level=1
 while level:level+=(s[i]=='{')-(s[i]=='}');i+=1
 return s[a:i]
mgr=(r/'src/battle/btl_char_mgr.c').read_text()
assert 'Port_FusionControlsCapture(chr,chr->fusionMember)' in mgr and 'Port_FusionControlsTick();' in mgr
assert 'Port_FusionControlPads(chr->player,&move,&attack)' in (r/'src/battle/btl_input.c').read_text()
s=(r/'src/battle/btl_ai_seq.c').read_text();body=function(s,'BtlAi_SendInput')
head=r"""#define PORT 1
#include <assert.h>
#include <string.h>
#include <stdio.h>
struct BtlAiWork{int objId;struct{unsigned buttons;float stickX,stickY;}out;};
static int behavior=1,mode=6,net,replay,frame,spark,refillKi,refillStocks,received;static int gauge[8];static char chr[0x1600];
int Port_Setting(const char *k,int d){if(!strcmp(k,"training_cpu_behavior"))return behavior;if(!strcmp(k,"training_refill_ki"))return refillKi;if(!strcmp(k,"training_refill_stocks"))return refillStocks;return d;}
int Port_NetActive(){return net;}int BattleReplay_IsActive(){return replay;}int Battle_GetMode(){return mode;}int Port_ModesFight(){return 0;}int BtlAiMgr_GetFrame(){return frame;}
void *BtlChar_Get(int){return chr;}void *BtlMember_GetActive(void*){return (char*)gauge-0x40;}int BtlChar_TestFlag(void*,int){return spark;}
void BtlCharApi_SetInjectedInput(int,unsigned b,float,float){received=b;}
"""
foot=r"""int main(){BtlAiWork ai={1,{0x88,.5f,.4f}};gauge[0]=100;gauge[3]=gauge[4]=100000;gauge[5]=0;gauge[6]=300000;
 BtlAi_SendInput(&ai);assert(received==0x88);behavior=0;BtlAi_SendInput(&ai);assert(received==0);behavior=2;BtlAi_SendInput(&ai);assert(received==0x204);behavior=3;BtlAi_SendInput(&ai);assert(received==0x202);behavior=4;BtlAi_SendInput(&ai);assert(received==0x200);spark=1;BtlAi_SendInput(&ai);assert(received==0x224);frame=20;BtlAi_SendInput(&ai);assert(!received);frame=0;spark=0;refillStocks=1;BtlAi_SendInput(&ai);assert(gauge[5]==gauge[6]);behavior=2;gauge[3]=0;BtlAi_SendInput(&ai);assert(received==0x200);refillKi=1;BtlAi_SendInput(&ai);assert(gauge[3]==gauge[4]&&received==0x204);
 net=1;BtlAi_SendInput(&ai);assert(received==0x88);net=0;replay=1;BtlAi_SendInput(&ai);assert(received==0x88);replay=0;mode=0;BtlAi_SendInput(&ai);assert(received==0x88);puts("PASS actual native training CPU: all5 behaviors, native charge/attack input, refill switches, online/replay/nontraining guards");}
"""
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp);(p/'test.cpp').write_text(head+body+foot);subprocess.run(['g++',str(p/'test.cpp'),'-o',str(p/'test')],check=True);subprocess.run([str(p/'test')],check=True)
