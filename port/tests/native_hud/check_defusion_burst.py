from pathlib import Path
import subprocess,tempfile,re
s=Path('src/battle/btl_char_mgr.c').read_text();a=s.index('void Port_DefusionBurst(');i=s.index('{',a)+1;d=1
while d:d+=(s[i]=='{')-(s[i]=='}');i+=1
body=s[a:i]
head=r'''#include <cassert>
#include <cstdio>
#include <cmath>
struct BtlMgrChr{int objId;};struct BtlMgrObj{};BtlMgrChr actors[2]={{0},{2}};BtlMgrObj objects[2];int flashes,waves,bad;
int BtlChar_GetCount(){return 2;}BtlMgrChr*BtlChar_Get(int i){return actors+i;}BtlMgrObj*BtlChar_GetObj(BtlMgrChr*c){return objects+(c-actors);}
int BtlObjFlash_Start(BtlMgrObj*,int kind){assert(kind==1);flashes++;return 1;}
void BtlCharApi_GetNodePos(int id,int node,void*out){assert(node==3);float*p=(float*)out;p[0]=bad?NAN:10;p[1]=20;p[2]=30;p[3]=1;}
int ScrWarp_Spawn(int id,void*point,float seconds,float radius,float width,float speed,float jitter){assert(id==0||id==2);assert(seconds==.55f&&radius==6&&width==32&&speed==8&&jitter==.15f);waves++;return 1;}
'''
tail=r'''int main(){Port_DefusionBurst(-1);Port_DefusionBurst(2);assert(!flashes&&!waves);Port_DefusionBurst(0);Port_DefusionBurst(1);assert(flashes==2&&waves==2);bad=1;Port_DefusionBurst(0);assert(flashes==3&&waves==2);puts("PASS actual paired native reveal: both fighter objects, bounded flash/ripple requests, invalid actor/point guards; no global transformation effect or control/camera writes");}'''
with tempfile.TemporaryDirectory()as tmp:
 p=Path(tmp);(p/'test.cpp').write_text(head+body+tail);subprocess.run(['g++','-fsanitize=address,undefined',str(p/'test.cpp'),'-o',str(p/'test')],check=True);subprocess.run([str(p/'test')],check=True)
