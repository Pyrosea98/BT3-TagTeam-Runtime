from pathlib import Path
import re,subprocess,tempfile
r=Path.cwd();src=(r/'port/src/plat_fighters.c').read_text()
def fn(name):
 m=re.search(r'^(?:static )?(?:void|int) '+name+r'\([^;{}]*\)\s*\{',src,re.M);assert m,name;i=src.index('{',m.start())+1;d=1
 while d:d+=(src[i]=='{')-(src[i]=='}');i+=1
 return src[m.start():i]
head=r'''#include <cassert>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cmath>
extern "C" {
#include "plat_beta8.h"
int net,replay;
int Port_NetActive(){return net;}int BattleReplay_IsActive(){return replay;}
}
static unsigned char arena[65536];unsigned Port_GameBigAlloc(int n){assert(n<=65536);return (unsigned)(uintptr_t)arena;}
const char*Port_CharaName(int id){static char names[291][30];snprintf(names[id],30,"Form%d",id);return names[id];}
'''
body='\n'.join(fn(n) for n in ['skill_name_units','skill_has','skill_partners','skill_emit_entry','Port_PatchSkillText'])
test=r'''int count(const uint16_t*p,const char*needle){uint16_t n[200];skill_name_units(n,199,needle);int len=strlen(needle),found=0;for(int i=0;p[i];i++)if(!memcmp(p+i,n,len*2))found++;return found;}
int main(){int rows[][5]={{51,52,166,149,59},{59,255,255,255,51},{224,225,226,229,230},{226,229,230,255,224},{229,230,255,255,224},{230,255,255,255,224},{238,239,241,255,234},{239,241,255,255,234},{241,255,255,255,234},{244,255,255,255,29}};
for(auto &row:rows){unsigned char p[256];memset(p,255,256);p[0]=0xb2;assert(Port_Beta8PatchFormParam(row[0],p));int reachable=0;for(int j=0;j<4;j++){assert(p[0x98+j]==row[j+1]&&p[0xa8+j]==255);if(row[j+1]!=255)reachable++;}assert((signed char)p[0xac]==((row[0]==59||row[0]==230||row[0]==241||row[0]==244)?-1:0));
 uint16_t script[4096];int n=skill_name_units(script,4095,"!F$0Abilities\n!F*002KeepThisAttack\n!F%0KeepThisDescription\n!F$4Transformacion\n!F*000OldTransform\n!F%0Inherited combo should vanish\n!D#<PAD=old>\n!F*000Another old transform\n!F%0Another inherited combo\n!F@\n");script[n]=0;uint32_t addr=(uint32_t)(uintptr_t)script; // stack is not a game pointer; use separate low fixture.
 static uint16_t lowScript[4096];memcpy(lowScript,script,(n+1)*2);addr=(uint32_t)(uintptr_t)lowScript;Port_PatchSkillText(row[0],0,p,&addr);auto*out=(uint16_t*)(uintptr_t)addr;
 assert(count(out,"KeepThisAttack")==1&&count(out,"KeepThisDescription")==1);assert(!count(out,"Inherited combo")&&!count(out,"Another inherited combo")&&!count(out,"PAD=old"));assert(count(out,"!D#<PAD=")==reachable);assert(count(out,"!F*")==reachable+1+((row[0]==51||row[0]==59)?2:0));
 if((signed char)p[0xac]<0){bool down=false;for(int i=0;out[i];i++)if(out[i]==0x2193)down=true;assert(down);}
}
assert(Port_Beta8EffectTier(224)==0&&Port_Beta8EffectTier(234)==0);assert(Port_Beta8EffectTier(229)==2&&Port_Beta8EffectTier(239)==2);assert(Port_Beta8AuraMultiplier(230)>Port_Beta8AuraMultiplier(229));
Port_Beta8ChargeReset();float prior=1;for(int i=0;i<12;i++){Port_Beta8ChargeUpdate(0,3,230,1);float v=Port_Beta8ChargeScale(3);assert(v>=prior&&v<=1.901f);prior=v;}assert(prior>1.89f);assert(Port_Beta8ChargeScale(9)==1);for(int i=0;i<12;i++)Port_Beta8ChargeUpdate(0,3,230,0);assert(Port_Beta8ChargeScale(3)==1);net=1;Port_Beta8ChargeUpdate(0,3,230,1);assert(Port_Beta8ChargeScale(3)==1);
puts("PASS actual Round3 native parameters + skill consumer: sparse slots, Down-only revert glyph, live costs, other-page preservation, stale combo removal, native tiers and isolated bounded charge ramp");}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);subprocess.run(['gcc','-std=gnu99','-ffunction-sections','-fdata-sections','-Iport/include','-c','port/src/plat_beta8.c','-o',str(p/'b8.o')],check=True);(p/'test.cpp').write_text(head+body+test);subprocess.run(['g++','-std=c++17','-no-pie','-Iport/include',str(p/'test.cpp'),str(p/'b8.o'),'-Wl,--gc-sections','-o',str(p/'test')],check=True);subprocess.run([str(p/'test')],check=True)
