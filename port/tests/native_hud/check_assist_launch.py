from pathlib import Path
import subprocess,tempfile
r=Path.cwd();s=(r/'src/battle/btl_act_e.c').read_text();a=s.index('void Port_AssistRushVictim(');b=s.index('\n}',a)+2;body=s[a:b]
h=r'''#include <cassert>
#include <initializer_list>
#include <cmath>
using s32=int;using f32=float;struct Vec4{float x,y,z,w;};struct BtlActEPose{Vec4 pos;};struct BtlActEChr{int player;BtlActEPose pose;};static BtlActEChr actors[12];s32 gPortTarget[12];static int damage[12],requests[12];static float yaw[12];static int air;int BtlAct_IsAirMotion(BtlActEChr*,int){return air;}
BtlActEChr*BtlChar_Get(s32 i){return i>=0&&i<12?&actors[i]:nullptr;}BtlActEPose*BtlChar_GetPos(BtlActEChr*c){return &c->pose;}
s32 BtlMember_Damage(BtlActEChr*c,s32 n,s32 flags){assert(flags==0x18B&&gPortTarget[c->player]==0&&n<=10000);damage[c->player]+=n;return n;}
void BtlMove_SetHeading(BtlActEChr*c,f32 a,f32 pitch){assert(pitch==0);yaw[c->player]=a;}
void BtlAct_Request(BtlActEChr*c,s32 a){requests[c->player]=a;}
'''
f=r'''int main(){for(int i=0;i<12;i++){actors[i].player=i;gPortTarget[i]=11;}actors[0].pose.pos={0,0,0,1};actors[3].pose.pos={10,0,0,1};Port_AssistRushVictim(3,0,10000);assert(damage[3]==10000&&requests[3]==0xD5&&fabs(yaw[3]+1.5707963f)<.0001f&&gPortTarget[3]==11);actors[5].pose.pos={0,0,-10,1};Port_AssistRushVictim(5,0,1000);assert(damage[5]==1000&&requests[5]==0xD5&&fabs(yaw[5])<.0001f&&gPortTarget[5]==11);
 air=1;Port_AssistRushVictim(3,0,0);Port_AssistRushVictim(5,0,0);
 for(int i:{3,5}){float dx=actors[i].pose.pos.x-actors[0].pose.pos.x,dz=actors[i].pose.pos.z-actors[0].pose.pos.z;assert((sin(yaw[i])*dx+cos(yaw[i])*dz)>0);}
 air=0;Port_AssistRushVictim(3,0,0);Port_AssistRushVictim(5,0,0);
 // Native D5 travels BACKWARD (dirMode5): verify travel, not just displacement.
 for(int i:{3,5}){float dx=actors[i].pose.pos.x-actors[0].pose.pos.x,dz=actors[i].pose.pos.z-actors[0].pose.pos.z;assert((-sin(yaw[i])*dx-cos(yaw[i])*dz)>0);}
}
'''
with tempfile.TemporaryDirectory()as d:
 p=Path(d);(p/'t.cpp').write_text(h+body+f);subprocess.run(['c++','-fsanitize=address,undefined',str(p/'t.cpp'),'-o',str(p/'t')],check=True);subprocess.run([str(p/'t')],check=True)
print('PASS actual native helper damage/knockback adapter: exact bounded damage flags, winner attribution, original target restored, away heading and native D5; ASan/UBSan')
assert s.count('Port_AssistRushFinish(chr->player,BtlOpp_GetClashCountB(chr),')==2
clash=(r/'src/battle/btl_char_flag_clash.c').read_text();assert clash.count('Port_AssistRushOutcome(winner->player,loser->player)')==2

assert s.count("Port_AssistRushLaunch(chr->player);")==2
