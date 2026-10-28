from pathlib import Path
import ast,subprocess,tempfile
r=Path.cwd()
def extract(text,name):
 a=text.index(name+'(');a=text.rfind('\n',0,a)+1;b=text.index('{',a)+1;depth=1
 while depth:depth+=(text[b]=='{')-(text[b]=='}');b+=1
 return text[a:b]
tree=ast.parse((r/'port/tests/native_hud/check_native_assists.py').read_text());vals={}
for n in tree.body:
 if isinstance(n,ast.Assign) and isinstance(n.value,ast.Constant):
  for k in n.targets:
   if isinstance(k,ast.Name):vals[k.id]=n.value.value
head=vals['head'].replace('pos[4][4]','pos[4][64]')
head=head.replace('int Port_AssistSafePoint(int,float*){return 1;}int Port_AssistCommitPosition(int,const float*,float){return 1;}','int Port_AssistSafePoint(int,float*);int Port_AssistCommitPosition(int,const float*,float);')
head=head.replace('if(!strcmp(k,"assist_formation_enabled"))return 0;','if(!strcmp(k,"assist_formation_enabled"))return 1;')
head=head.replace('int Port_CamTarget(int who){return who^1;}','static int follow=1;int Port_CamTarget(int who){return who^1;}')
head=head.replace('if(strstr(k,"enabled"))','if(!strcmp(k,"assist_follow_camera"))return follow;if(strstr(k,"enabled"))')
head+='''
using s32=int;using f32=float;struct Vec4{float x,y,z,w;};struct StgGroundHit{float x,y,z;unsigned char rest[36];};using BtlMgrChr=Actor;struct BtlMgrObj{int marker;};static BtlMgrObj objects[4];
static int stageReady=1,pit,objectMissing,poseSync,matrixSync,clampSync;static float stageRadius=1000,ceiling=-1000,bottom=1000,floorY=0;
int BtlStage_IsReady(){return stageReady;}float BtlStage_GetInnerRadius(){return stageRadius;}float BtlStage_GetTop(){return ceiling;}float BtlStage_GetBottom(){return bottom;}int Port_FighterObjId(int who){return who;}
int BtlStage_ProbeGround(int,Vec4*p,StgGroundHit*out){out->y=pit?p->y:floorY;return 0;}
BtlMgrObj*BtlChar_GetObj(void*p){return objectMissing?nullptr:&objects[idx(p)];}void BtlMove_ClampToStage(void*){clampSync++;}void BtlChar_PoseToObj(void*,int root){assert(root==0);poseSync++;}void BtlObjPose_CalcMatrices(void*){matrixSync++;}
'''
safe=extract((r/'src/battle/stg_a_b.c').read_text(),'Port_AssistSafePoint')
commit=extract((r/'src/battle/btl_char_mgr.c').read_text(),'Port_AssistCommitPosition').replace('c=BtlChar_Get(who);','c=(BtlMgrChr*)BtlChar_Get(who);')
init=vals['foot'].split('int main()')[0]
foot=r'''int main(){
 init();for(int i=0;i<4;i++){*(int*)((char*)&actors[i]+12)=i;}float p[4]={2000,10,0,1};assert(Port_AssistSafePoint(2,p));assert(p[0]<=986.01f&&p[1]==-1);stageReady=0;assert(!Port_AssistSafePoint(2,p));stageReady=1;pit=1;assert(!Port_AssistSafePoint(2,p));pit=0;p[0]=NAN;assert(!Port_AssistSafePoint(2,p));
 float valid[4]={50,-10,30,1};assert(Port_AssistCommitPosition(2,valid,.75f));assert(pos[2][0]==50&&pos[2][1]==-10&&pos[2][5]==.75f&&poseSync==1&&matrixSync==1&&clampSync==1);frozen=1;assert(!Port_AssistCommitPosition(2,valid,0));frozen=0;objectMissing=1;assert(!Port_AssistCommitPosition(2,valid,0));objectMissing=0;
 init();human[2]=1;team[3]=0;pos[1][2]=100;pos[2][0]=100;pos[3][0]=120;as_begin(2,0);as_begin(3,0);assert(sAssist[2].slot!=sAssist[3].slot);action[2]=action[3]=15;
 for(int tick=0;tick<15;tick++){Port_AssistTick();assert(members[2][0x4c/4]==100000&&members[3][0x4c/4]==100000);if(!tick)assert(pos[2][0]>80&&pos[2][0]<100);assert(pos[2][1]<=-1);}
 assert(sAssist[2].state==1&&sAssist[2].placeTicks==15&&Port_AssistView(2)==-1);action[2]=action[3]=7;Port_AssistTick();assert(sAssist[2].state==2&&members[2][0x4c/4]==90000&&members[3][0x4c/4]==90000);assert(fabsf(pos[2][0]-pos[3][0])>=9.99f);assert(Port_AssistView(2)==0);follow=0;assert(Port_AssistView(2)==-1);follow=1;
 float x=pos[2][0];pos[0][0]+=10;pos[1][0]+=10;Port_AssistTick();assert(fabsf(pos[2][0]-x-10)<.01f&&members[2][0x4c/4]==90000);pause=1;x=pos[2][0];pos[0][0]+=10;Port_AssistTick();assert(pos[2][0]==x);pause=0;
 action[2]=183;Port_AssistTick();assert(!sAssist[2].state&&Port_AssistView(2)==-1&&pos[2][0]==x);
 init();pit=1;as_begin(2,0);action[2]=15;Port_AssistTick();assert(!sAssist[2].state&&members[2][0x4c/4]==100000);pit=0;
 init();pos[1][2]=100;as_begin(2,0);sAssist[2].slot=1;as_begin(3,0);assert(sAssist[3].slot==0);Port_AssistReset();assert(Port_AssistView(2)==-1);
 init();team[3]=0;pos[1][2]=100;
 as_begin(2,0);as_begin(3,0);sAssist[2].kind=sAssist[3].kind=2;sAssist[2].state=sAssist[3].state=1;sAssist[2].forming=sAssist[3].forming=1;sAssist[2].placeTicks=sAssist[3].placeTicks=15;
 assert(as_place(2)&&as_place(3));assert(hypotf(pos[2][0]-pos[0][0],pos[2][2]-pos[0][2])>9&&hypotf(pos[2][0]-pos[0][0],pos[2][2]-pos[0][2])<11);assert(fabsf(pos[2][0]-pos[3][0])>=9.99f);
 init();pos[1][2]=40;as_begin(2,0);as_begin(3,1);
 for(int i:{2,3}){sAssist[i].kind=2;sAssist[i].state=1;sAssist[i].forming=1;sAssist[i].placeTicks=15;}
 assert(as_place(2)&&as_place(3));
 assert(fabsf(pos[2][0]-pos[3][0])<.01f); /* opposing helpers share a flank */
 assert(pos[2][2]>0&&pos[3][2]<40); /* neither stands behind the exchange */
 assert(hypotf(pos[2][0]-pos[3][0],pos[2][2]-pos[3][2])<40);
 assert(hypotf(pos[2][0]-pos[0][0],pos[2][2]-pos[0][2])>=9);
 for(int slot=0;slot<4;slot++){sAssist[2].slot=slot;assert(as_place(2));assert(fabsf(pos[2][0])>=9);}
 puts("PASS actual assist formation: native terrain/cylinder rejection, smooth15tick glide, separate/reused slots, no charge before arrival, pose/matrix sync, caster tracking, scoped camera restore, pause/hit/pit/reset; ASan/UBSan");}
'''
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp);(p/'test.cpp').write_text('#include <initializer_list>\n'+head+safe+commit+(r/'port/src/mod_assists_native.inc').read_text()+init+foot)
 subprocess.run(['g++','-std=c++17','-fsanitize=address,undefined','-fno-omit-frame-pointer',str(p/'test.cpp'),'-o',str(p/'test')],check=True);subprocess.run([str(p/'test')],check=True)
