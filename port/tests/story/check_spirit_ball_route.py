"""Exercise the actual Story velocity adapter, including its ordinary-fight bypass."""
from pathlib import Path
import subprocess, tempfile

root=Path(__file__).resolve().parents[3]
s=(root/'port/include/mod_story_scene.inc').read_text()
start=s.index('int Port_StorySpiritBallVelocity(')
end=s.index('\n}\n',start)+3
body=s[start:end]
fixture=r'''
#include <assert.h>
#include <math.h>
typedef struct {float x,y,z,w;} Vec4;
int sStAtkFrames=100,sStSbStage=1,sStAtkBy=3,sStSbGohan=2,sStAtkAt=1;
int sStSbSteered,sStSbFlip,donor=0,reports;
int st_sb_return_ready(void){return 1;}
Vec4 positions[4]={{0},{20,0,0,1},{0,0,20,1},{0}};
int st_obj(int a){return a+10;}
int Port_StoryTechniqueDonor(int a){return a==3?donor:-1;}
float st_height(int a){return 20;}
void BtlCharApi_GetNodePos(int o,int n,Vec4*p){assert(n==0x11);*p=positions[o-10];}
void Port_StoryShotReport(int a,float x,float y,float z){assert(a==3);reports++;}
void Port_Dbg3(const char*s,int a,int b,int c,int d){}
'''+body+r'''
int main(void){float p[4]={0},v[4]={2,0,0,0},d[4]={1,0,0,0};
assert(!Port_StorySpiritBallVelocity(11,p,v,d)&&reports==0);
assert(Port_StorySpiritBallVelocity(13,p,v,d)&&v[0]==2&&v[2]==0);
sStSbStage=2;sStSbFlip=1;
assert(Port_StorySpiritBallVelocity(13,p,v,d)&&v[0]==0&&v[2]==2&&!sStSbFlip);
sStSbStage=3;
assert(Port_StorySpiritBallVelocity(13,p,v,d)&&v[0]==2&&v[2]==0);
p[0]=19.5f;assert(Port_StorySpiritBallVelocity(13,p,v,d)&&v[0]==.5f);
donor=-1;assert(!Port_StorySpiritBallVelocity(13,p,v,d));
donor=0;sStAtkFrames=0;assert(!Port_StorySpiritBallVelocity(13,p,v,d));
return 0;}
'''
with tempfile.TemporaryDirectory() as td:
    c=Path(td)/'route.c';out=Path(td)/'route';c.write_text(fixture)
    subprocess.run(['cc','-fsanitize=address,undefined','-g',str(c),'-lm','-o',str(out)],check=True)
    subprocess.run([str(out)],check=True)
print('PASS actual Genkidama route: Vegeta -> Gohan -> Vegeta; native speed/overshoot, owner and lifetime guards; ordinary fights untouched')
