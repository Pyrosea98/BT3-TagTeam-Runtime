from pathlib import Path
import subprocess,tempfile
r=Path.cwd();s=(r/'src/battle/btl_char_flag_clash.c').read_text();body=s[s.index('void Port_ClashCpuReset('):s.index('static int port_clash_scale')]
code=r'''#include <assert.h>
#include <limits.h>
#include "mod_clash_pace.h"
typedef struct{int objId;}BtlFlagChr;static BtlFlagChr c[16];static ModClashPace sCpuClashPace[16][2];static int net,replay,cpu[16],level[16];int Port_NetActive(void){return net;}int BattleReplay_IsActive(void){return replay;}BtlFlagChr*BtlChar_Get(int a){return c+a;}int BtlCharApi_IsInputInjected(int a){return cpu[a];}int BtlCharApi_GetCpuLevel(int a){return level[a];}
'''+body+r'''
int main(void){for(int i=0;i<16;i++){c[i].objId=i;cpu[i]=1;level[i]=21;}
 for(int beam=0;beam<2;beam++)for(int lev=0;lev<=21;lev++)for(int actor=0;actor<16;actor++){
  int n=0,rate=lev<=6?4:lev<=13?6:8,start=beam?16:0;level[actor]=lev;Port_ClashCpuReset(actor,INT_MAX,beam);
  for(int f=0;f<301;f++){int p=Port_ClashCpuPoint(actor,f,beam);assert(p==0||p==1);n+=p;assert(n<=1+(f<start?0:(f-start)*rate/30));if(p)assert(!Port_ClashCpuPoint(actor,f,beam));if(f<start)assert(!p);}
  assert(n>rate*8);
 }
 Port_ClashCpuReset(1,31,1);assert(!Port_ClashCpuPoint(1,15,1));assert(Port_ClashCpuPoint(1,16,1));for(int j=0;j<50;j++)assert(!Port_ClashCpuPoint(1,16,1));
 assert(Port_ClashCpuPoint(1,1000000,1));assert(!Port_ClashCpuPoint(1,1000000,1));
 cpu[1]=0;assert(Port_ClashCpuPoint(1,16,1));cpu[1]=1;net=1;assert(Port_ClashCpuPoint(1,16,1));net=0;replay=1;assert(Port_ClashCpuPoint(1,16,1));replay=0;
 assert(Port_ClashCpuPoint(-1,0,1)&&Port_ClashCpuPoint(16,0,1));
 for(int i=0;i<16;i++){level[i]=21;Port_ClashCpuReset(i,INT_MAX-5,0);assert(Port_ClashCpuPoint(i,INT_MAX,0));assert(!Port_ClashCpuPoint(i,INT_MAX,0));}
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d)/'test';p.with_suffix('.c').write_text(code);subprocess.run(['cc','-fsanitize=address,undefined','-Iport/include',str(p.with_suffix('.c')),'-o',str(p)],check=True);subprocess.run([str(p)],check=True)
for name,fn,counter,beam in [('src/battle/btl_act_e.c','BtlAct_ClashBlowsHandler','clashCountB',0),('src/battle/btl_act_f.c','BtlAct_ClashStruggleHandler','clashPower',1)]:
 t=(r/name).read_text();a=t.index('void '+fn+'(');i=t.index('{',a)+1;depth=1
 while depth:depth+=(t[i]=='{')-(t[i]=='}');i+=1
 t=t[a:i];assert ('Port_ClashCpuReset(chr->player,chr->'+counter+','+str(beam)+')') in t;assert ('Port_ClashCpuPoint(chr->player,chr->actionFrame,'+str(beam)+')') in t;assert 'Port_ClashCpuLimit' not in t
assert 'Port_AssistScore' in s and 'port_clash_count' in s
print('PASS native CPU input pacing: 16 actors, Easy4/Normal6/Hard8, warmup/reset/no bank/overflow, human/network/replay bypass; passive/assist scoring preserved')
