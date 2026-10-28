from pathlib import Path
import re,subprocess,tempfile
r=Path.cwd()
def fn(s,name):
 m=re.search(r'^(?:static )?(?:void|int) '+name+r'\([^;{}]*\)\s*\{',s,re.M);assert m,name;i=s.index('{',m.start())+1;d=1
 while d:d+=(s[i]=='{')-(s[i]=='}');i+=1
 return s[m.start():i]
s=(r/'port/src/plat_fighters.c').read_text();body='\n'.join(fn(s,n) for n in ['skill_name_units','skill_has','skill_partners','skill_emit_entry','Port_PatchSkillText']);mgr=(r/'src/battle/btl_char_mgr.c').read_text();helper=fn(mgr,'Port_PatchFighterSkillData')
h=r'''#include <cassert>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <initializer_list>
using u8=unsigned char;struct BtlMgrMember{int chara,costume;};struct BtlMgrObj{unsigned char bytes[0x1000];};struct BtlMgrChr{BtlMgrMember m;BtlMgrObj obj;int objId;};
void Port_Beta8Ssj4Load(int,int,void*){}
BtlMgrObj*BtlChar_GetObj(BtlMgrChr*c){return &c->obj;}BtlMgrMember*BtlMember_GetActive(BtlMgrChr*c){return &c->m;}
int Port_Beta8PatchFormParam(int,void*){return 0;}int Port_Beta8FormTarget(int,int t){return t;}const char*Port_CharaName(int){return "Fighter";}unsigned Port_GameBigAlloc(int){assert(false);return 0;}
'''
t=r'''int main(){int pairs[][4]={{51,110,224,234},{52,53,225,235},{166,249,226,238},{149,172,229,239},{59,54,230,241}};
for(auto&pair:pairs)for(int role=0;role<2;role++)for(int player=0;player<4;player++){
 BtlMgrChr c={};c.m={pair[role],0};unsigned char params[256];memset(params,255,256);void*p=params;memcpy(c.obj.bytes+0x91c,&p,sizeof(p));Port_PatchFighterSkillData(&c);
 assert(params[0xb4]==pair[2]&&params[0xb5]==pair[3]);assert(params[0xb7]==pair[role^1]&&params[0xb8]==pair[role^1]);assert(params[0xb1]==1&&params[0xb2]==2);
 for(int i=0;i<8;i++)assert(params[0xba+i]==pair[role^1]);
}
int fin[][4]={{3,34,110,51},{4,35,53,52},{5,36,249,166},{6,237,172,149},{200,202,54,59}};
for(auto &row:fin)for(int role=0;role<2;role++)for(int costume=0;costume<8;costume++){
 unsigned char p[256];memset(p,255,256);Port_PatchSkillText(row[role],costume,p,nullptr);
 assert(p[0xb4]==row[3]&&p[0xb5]==row[2]&&p[0xb7]==row[role^1]&&p[0xb8]==row[role^1]);assert(p[0xb1]==2&&p[0xb2]==1);
 for(int k=0;k<2;k++)for(int j=0;j<4;j++)assert(p[0xba+k*4+j]==(row[role]==202&&j==1?10:row[role]==202&&j==2?233:row[role^1]));
 if(row[role]==3&&costume==2)assert(p[0xb6]==214&&p[0xb9]==21&&p[0xb3]==2);else assert(p[0xb6]==255);
}
int gt[][4]={{7,122,110,215},{8,240,53,220},{10,38,54,223}};
for(auto &row:gt)for(int role=0;role<2;role++){
 unsigned char p[256];memset(p,255,256);Port_PatchSkillText(row[role],role?0:2,p,nullptr);
 assert(p[0xb4]==row[2]&&p[0xb5]==row[3]&&p[0xb7]==row[role^1]&&p[0xb8]==row[role^1]);
 assert(p[0xb1]==1&&p[0xb2]==2);for(int i=0;i<8;i++)assert(p[0xba+i]==row[role^1]);
}
for(int cid:{214,215,216,218,219,220,221,222,223,224,225,226,229,230,234,235,238,239,241}){
 unsigned char p[256];memset(p,0,256);p[0xb4]=54;p[0xb5]=59;p[0xba]=p[0xbe]=53;Port_PatchSkillText(cid,0,p,nullptr);assert(p[0xb4]==255&&p[0xb5]==255&&p[0xb6]==255);
}
puts("PASS actual initial-load helper +recipe patcher: all5pairs/bothroles/4player identities get Dance/Potara results and actual partner object from pristine FF parameters; no prior transform/text allocation required");}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.cpp').write_text(h+body+helper+t);subprocess.run(['g++','-no-pie',str(p/'test.cpp'),'-o',str(p/'test')],check=True);subprocess.run([str(p/'test')],check=True)
# The initial reset must call the same tested helper after its last member initialization.
a=mgr.index('void BtlChar_Reset(BtlMgrChr *chr) {');b=mgr.index('void BtlChar_OnModelLoaded',a);reset=mgr[a:b];assert reset.rfind('Port_PatchFighterSkillData(chr)')>reset.rfind('BtlMember_LoadParams(');assert mgr.count('Port_PatchFighterSkillData(chr);')==2
print('PASS initial reset and later model reload both use the shared tested recipe setup')
