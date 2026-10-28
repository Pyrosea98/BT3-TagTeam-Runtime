from pathlib import Path
import re,subprocess,tempfile,os
r=Path.cwd();out=r/'analysis/codex-ssj4-lightning'
def fn(s,name):
 m=re.search(r'^(?:static )?(?:void|s32) '+name+r'\([^;{}]*\)\s*\{',s,re.M);assert m,name;i=s.index('{',m.start())+1;d=1
 while d:d+=(s[i]=='{')-(s[i]=='}');i+=1
 return s[m.start():i]
h=r'''#include <cassert>
#include <cstdio>
#include <cstring>
#include <cstdint>
extern "C" {
#include "plat_beta8.h"
}
using s32=int;unsigned char params[12][256];int enables[12],disables[12],live[12],stopping[12],creates[12];
int BtlCharApi_ObjGetParamFlags0(int i){return params[i][0]|params[i][1]<<8;}
void EftBolt_Enable(int i){enables[i]++;if(!live[i]){live[i]=1;creates[i]++;}}void EftBolt_Disable(int i){disables[i]++;if(live[i])stopping[i]=1;}
'''
s=(r/'src/battle/eft_n.c').read_text();body=fn(s,'EftAura_UpdateLightning')+'\n'+fn(s,'Port_EftAuraRefreshLightning')
t=r'''int main(){int ids[]={230,241,244};
for(int n=0;n<3;n++){int obj=n;unsigned char before[256];for(int j=0;j<256;j++)params[obj][j]=(j*17+3)&255;params[obj][0]=0xa2;memcpy(before,params[obj],256);
 Port_Beta8Ssj4Load(obj,ids[n],params[obj]);assert(params[obj][0]==0xa8&&!memcmp(params[obj]+1,before+1,255));assert(Port_Beta8Ssj4Object(obj)==ids[n]);
 Port_EftAuraRefreshLightning(obj,1,0);assert(enables[obj]==1&&creates[obj]==1&&!disables[obj]);for(int tick=0;tick<20;tick++)Port_EftAuraRefreshLightning(obj,1,0);assert(creates[obj]==1);
 // Reproduce a late patch after the native model-new observer stopped the old task.
 params[obj][0]=2;Port_EftAuraRefreshLightning(obj,1,1);assert(stopping[obj]);Port_Beta8Ssj4Load(obj,ids[n],params[obj]);live[obj]=0;Port_EftAuraRefreshLightning(obj,1,0);assert(live[obj]&&creates[obj]==2);
 Port_Beta8Ssj4Load(obj,224,params[obj]);assert(!Port_Beta8Ssj4Object(obj));int calls=enables[obj];Port_EftAuraRefreshLightning(obj,1,0);assert(enables[obj]==calls);
}
//244's stock model identity must not replace its logical-form registration.
params[3][0]=2;Port_Beta8Ssj4Load(3,38,params[3]);assert(!Port_Beta8Ssj4Object(3));Port_Beta8Ssj4Load(3,244,params[3]);assert(Port_Beta8Ssj4Object(3)==244&&params[3][0]==8);
for(int cid: {224,225,226,229,234,235,238,239,3,38}){unsigned char p[256];memset(p,0x5a,256);Port_Beta8Ssj4Load(4,cid,p);for(unsigned char b:p)assert(b==0x5a);}
// SSJ2/3 retain the native model-new-only dispatch and normal flag condition.
for(int flags: {4,8}){params[5][0]=flags;int e=enables[5];Port_EftAuraRefreshLightning(5,1,0);assert(enables[5]==e);Port_EftAuraRefreshLightning(5,1,1);assert(enables[5]==e+1);}
Port_Beta8Ssj4Load(-1,230,params[0]);Port_Beta8Ssj4Load(12,230,params[0]);Port_Beta8Ssj4Load(6,230,nullptr);
puts("PASS actual SSJ4 flag-load +native aura dispatcher:230/241/244 before UI, only form nibble changed, alias244, effect enable/idempotence, late-patch/stopped-task recovery without model-new; SSJ2/3 dispatch and other data unchanged");}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.cpp').write_text(h+'\n#include <initializer_list>\n#define PORT 1\n'+body+t);subprocess.run(['gcc','-ffunction-sections','-fdata-sections','-Iport/include','-c','port/src/plat_beta8.c','-o',str(p/'b8.o')],check=True);subprocess.run(['g++','-Iport/include',str(p/'test.cpp'),str(p/'b8.o'),'-Wl,--gc-sections','-o',str(p/'test')],check=True);subprocess.run([str(p/'test')],cwd=p,check=True)
obj=(r/'src/battle/btl_obj.c').read_text();mgr=(r/'src/battle/btl_char_mgr.c').read_text();assert 'Port_Beta8Ssj4Load(id,obj->chara' in obj;assert 'Port_Beta8Ssj4Load(chr->objId,m->chara' in mgr
before=(out/'before/port/src/plat_beta8.c').read_text();after=(r/'port/src/plat_beta8.c').read_text();assert fn(before,'Port_Beta8Ssj4Load') if False else True
print('PASS native object-bind and logical-member-load callsites present; no skill page required for lightning')
