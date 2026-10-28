from pathlib import Path
import tempfile,subprocess,re
r=Path.cwd()
def ex(s,n):
 a=re.search(r'(?m)^[^;\n]*\b'+n+r'\([^;]*?\)\s*\{',s).start();i=s.index('{',a)+1;d=1
 while d:d+=(s[i]=='{')-(s[i]=='}');i+=1
 return s[a:i]
# Exercise the real restart producer with a deliberately faithful eight-slot pool.
head=r"""#include <cassert>
#include <cstring>
using s32=int;using u32=unsigned;
#define PORT 1
#define BTL_JOB_COUNT 8
#define BTL_JOB_KIND_RESTART 2
#define BATTLE_FLAG_LOADING 2
struct BtlJob{int(*step)(BtlJob*);int side,kind,chara,animChara,unk1C,voiceChara,costume,variant,modelOnly,initial;};using Job=BtlJob;
struct BattleRes{void*transition;};struct Work{int flags;};struct BattleMember{void*data;void*buf[2];};struct PortFighterForm{int chara,costume,variant,startChara,startCostume,startVariant,whole;};
static BattleRes resource;static Work work;static BtlJob pool[8];static bool used[8];static int count=10,queued,completed,batches,high;static BattleMember members[2][5];static PortFighterForm sPortFighter[10];static int gPortExtraObjs=8,gPortExtraObj[8],sPortExtraScene[8];
#define BATTLE_RES (&resource)
Work*Battle_GetWork(){return &work;}void Heap_Free(void*){}void BtlRes_CommitReloadEx(){}void Job_Clear(){queued=0;}void BtlJob_InitPool(){memset(used,0,sizeof(used));}
BtlJob*BtlJob_Alloc(){for(int i=0;i<8;i++)if(!used[i]){used[i]=true;return &pool[i];}return nullptr;}
void Job_Push(Job*){queued++;if(queued>high)high=queued;}void Load_RunBlocking(){assert(queued>0);completed+=queued;queued=0;memset(used,0,sizeof(used));batches++;}
int BtlJob_GetFreeCount(){int n=0;for(bool v:used)n+=!v;return n;}int Battle_GetStartStage(){return 0;}int BtlLoad_RequestStageReload(int,int){auto*j=BtlJob_Alloc();assert(j);Job_Push(j);return 1;}
int BattleSide_IsFormChanged(int){return 1;}int BattleSide_GetStartChara(int i){return i;}int BattleSide_GetStartCostume(int){return 0;}int BattleSide_GetStartVariant(int){return 0;}void BattleSide_SetForm(int,int,int,int){}int BtlLoad_StepChara(BtlJob*){return 1;}void BtlScene_FreeChar(int){}int BattleSide_GetObjId(int i){return i;}int BattleSide_GetModelSlot(int i){return i;}void BtlObj_Rebind(int,int){}void Port_ResCommitAll(){}int Port_LoadFighters(){return count;}int Port_FighterSlot(int i){return i;}int Port_LoadObj(int i){return i;}void Port_ResReleaseOld(int){}int BattleSide_GetMemberCount(int){return 5;}BattleMember*BattleSide_GetMember(int s,int i){return &members[s][i];}
"""
body=ex((r/'src/battle/battle_load.c').read_text(),'BtlLoad_Reload')
foot=r"""int main(){for(count=2;count<=10;count++){queued=completed=batches=high=0;for(int i=0;i<10;i++)sPortFighter[i]={100+i,0,1,i,0,0,0};BtlLoad_Reload();assert(completed==count+1&&queued==0&&high<=8&&batches==(count+8)/8);} }
"""
# Track the real full-screen flash against each actual owner, even if native
# fighter1 never takes costume damage and an unrelated native rush is active.
flashhead=r"""#include <cassert>
#include <cstring>
using s32=int;
#define PORT 1
struct Color{float r,g,b,a;};struct EftFlashArg{Color color;float in,hold,out;int objId,wait;};struct EftFlash{EftFlashArg arg;float fade,inMax,outMax;int pause,phase,frames;};struct EftQTask{void*work;int step,dead;};struct Mgr{int flags,rushState,rushBits;};struct Work{int flags;};static Mgr mgr;static Mgr*gEftFlash=&mgr;static EftFlash flash;static EftQTask live{&flash,0,0};static Work work;static int owner;static bool variants[10];
Work*Battle_GetWork(){return &work;}int BtlScene_GetCharCount(){return 10;}int BtlCharApi_IsFlag8Action104(int i){return i==owner;}int BtlCharApi_GetMemberUnk60(int i){return variants[i];}int EftFlash_GetRushState(){return owner>=2?1:0;}void BtlTask_SetDead(EftQTask*t){t->dead=1;}
void EftFlash_Init(EftQTask*,EftFlashArg*);void EftFlash_Start(EftFlashArg*a){live={&flash,0,0};EftFlash_Init(&live,a);}
"""
f=(r/'src/battle/eft_q.c').read_text();fb='\n'.join(ex(f,n)for n in ['EftFlash_Init','EftFlashMgr_Update','EftFlash_Update']).replace('EftFlash *w = task->work;','EftFlash *w = (EftFlash*)task->work;')
ff=r"""int main(){for(owner=0;owner<10;owner++){mgr={};work={};memset(variants,0,sizeof(variants));variants[owner]=true;EftFlashMgr_Update(nullptr);assert((mgr.flags>>8)==owner+1);int n=0;while(!live.dead&&n<220){mgr.rushState=EftFlash_GetRushState();EftFlash_Update(&live);n++;}assert(live.dead&&n<=62);} }
"""
with tempfile.TemporaryDirectory()as d:
 p=Path(d)
 for name,code in [('rematch8slot',head+body+foot),('actual-flash-owner',flashhead+fb+ff)]:
  (p/'t.cpp').write_text(code);subprocess.run(['g++','-fsanitize=address,undefined',str(p/'t.cpp'),'-o',str(p/'t')],check=True);subprocess.run([str(p/'t')],check=True);print('PASS',name)
