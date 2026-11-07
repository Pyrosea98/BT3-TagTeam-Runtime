#include "mod_costume_ownership.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../src/plat_fighters.c"
int Port_Beta8CostumeSelectable(int c,int s){return B8CostumeSelectable(c,s);}
int Port_ScenarioActive(void){return 0;}
void Port_ModSelectPublish(const MsView*v){}
static int test_mode=1,test_online,test_tour,test_missing;
int Port_Setting(const char *name,int def){if(!strcmp(name,"modes_fight"))return test_mode;return def;}
void Port_SettingSave(const char*n,int v){(void)n;(void)v;}
void Port_SettingsWrite(void){}
int Port_NetActive(void){return test_online;}
int Port_TagTourActive(void){return test_tour;}
int Port_TagTourConfig(int i){(void)i;return 4;}
const char *Port_ModesName(int m){(void)m;return "test";}
int Port_ModesFight(void){return test_online?0:test_mode;}
int Port_ModesCount(void){return 0;}
int Port_Beta8StockAlias(int c){return c==161?3:c==163?17:c==164?28:-1;}
int Port_Beta8FileId(int c,int s,int d,int k){(void)c;(void)s;(void)d;(void)k;return 0x43000000;}
int Port_Beta8ResolveFile(int c,int s,int d,int k,int fallback){(void)c;(void)s;(void)d;(void)k;(void)fallback;return 0x43000000;}
int Port_FilePath(int p,int e,const char*n,char*out,int size){(void)p;(void)e;(void)n;snprintf(out,size,"/tmp/ms-model.bin");return !test_missing;}
int Port_TagTourMemberPad(int s,int m){(void)s;(void)m;return -1;}
static int partitions;
static void verify_partition(int*sizes,int count){MsView v={0};MsPick p;int at=0;v.mode=test_mode=1;v.a=sizes[0];v.team_count=count;
 for(int t=0;t<count;t++)for(int j=0;j<sizes[t];j++){v.pick[at].chara=at+3;v.pick[at].team=t;v.pick[at].pad=at? -1:0;at++;}v.total=at;
 assert(Port_ModSelectCommit(&v));for(int round=0;round<2;round++){assert(!Port_ExtraSetup(0,1,1,1,0,0));assert(Port_FightExtras()==at-2);Port_ExtraDone();
 for(int a=2;a<at;a++){assert(Port_ModSelectPlan(&p,a));assert(Port_FightTeam(a-2)==p.team&&Port_FightChara(a-2)==p.chara&&Port_FightPad(a-2)==p.pad);}}
 partitions++;}
static void enumerate_partitions(int*sizes,int n,int left){if(n>=2)verify_partition(sizes,n);if(n==10)return;for(int size=1;size<=left;size++){sizes[n]=size;enumerate_partitions(sizes,n+1,left-size);}}
int main(void){
 MsView v;MsPick p;int mode,a,b,i,k;FILE*f=fopen("/tmp/ms-model.bin","wb");assert(f);for(i=0;i<256;i++)fputc(0,f);fclose(f);
 for(mode=1;mode<=3;mode++)for(a=1;a<=9;a++)for(b=1;b<=10-a;b++){
  memset(&v,0,sizeof(v));v.mode=test_mode=mode;v.total=a+b;v.a=a;
  for(i=0;i<v.total;i++){v.pick[i].chara=i+3;v.pick[i].costume=0;v.pick[i].team=MS_Side(&v,i);v.pick[i].pad=i<4?i:-1;v.pick[i].items[0]=i+1;}
  assert(Port_ModSelectCommit(&v));for(i=0;i<v.total;i++){unsigned short items[8];MsPick selected;assert(Port_ModSelectPlan(&selected,i)&&Port_ModSelectItems(i,items)&&items[0]==selected.items[0]);}
  for(k=0;k<2;k++){ /* actual rematch setup, not just pure layout helpers */
   assert(Port_ExtraSetup(mode==3?6:0,1,1,1,0,0)==0);assert(Port_FightExtras()==v.total-2);Port_ExtraDone();
   for(i=2;i<v.total;i++){assert(Port_ModSelectPlan(&p,i));assert(Port_FightChara(i-2)==p.chara);assert(Port_FightCostume(i-2)==p.costume);assert(Port_FightTeam(i-2)==p.team);assert(Port_FightPad(i-2)==p.pad);assert(Port_FightMember(i-2)==-1);}
  }
  assert(Port_ExtraSetup(0,1,1,0,1,0)==0&&Port_FightExtras()==0); /* replay excludes draft */
  assert(Port_ExtraSetup(0,1,1,0,0,1)==0&&Port_FightExtras()==0); /* scripted excludes draft */
 }
 v.mode=test_mode=1;v.a=1;v.total=8;for(i=0;i<8;i++)v.pick[i].team=MS_Side(&v,i);assert(Port_ModSelectCommit(&v));assert(!Port_ExtraSetup(0,1,1,0,0,0)&&Port_FightExtras()==6);
 test_online=1;assert(!Port_ModSelectCommit(&v));assert(!Port_ExtraSetup(0,1,1,0,0,0)&&!Port_FightExtras());test_online=0;
 test_tour=1;assert(!Port_ModSelectEnabled()&&!Port_ModSelectPlanCount());test_tour=0;
 test_mode=0;assert(!Port_ModSelectEnabled()&&!Port_ModSelectPlanCount());test_mode=1;
 test_missing=1;assert(!Port_ModSelectCommit(&v));test_missing=0;
 {int sizes[10];enumerate_partitions(sizes,0,10);assert(partitions==1013);}
 {MsView v={0};int sizes[]={1,1,2};v.mode=1;assert(MS_Layout(&v,sizes,3));for(int i=0;i<4;i++){v.pick[i].chara=i+3;v.pick[i].pad=-1;}v.pick[2].pad=0;v.pick[3].pad=1;assert(Port_ModSelectCommit(&v)&&Port_ModSelectMulti());Port_ModSelectResult(2);assert(Port_ModSelectPlayerWin()&&Port_ModSelectWinnerPad()==0);Port_ModSelectResult(1);assert(!Port_ModSelectPlayerWin());Port_ModSelectResult(-1);assert(!sMsPlan.result_team);}
 Port_ModSelectClear();assert(!Port_ModSelectPlanCount());
 puts("PASS 1013 multi-team compositions through real loader and repeated rematches; actual native extra loader: all 45 team-size pairs, FFA, ownership, repeated rematches, stock/tournament/online/replay/script guards");
 return 0;
}
