#include <assert.h>
#include <string.h>
#include <stdio.h>
#include "mod_select.h"
#include "coop_selection.h"
static struct CoopSetup coop;static int active,nplayers;
int Port_CoopActive(void){return active;}
int Port_ModSelectPlayerCount(void){return nplayers;}
int Port_CoopLeader(int side){return CS_Leader(&coop,side);}
int Port_CoopMemberPad(int side,int member){return CS_Member(&coop,side,member);}
int PortInput_PlayerAvailable(int player){return player>=0&&player<4;}
typedef struct {int id,formCount,form[7];} TsCell;
#include "mod_select_catalog.h"
static int Port_Beta8SelectFormOwner(int id){return id==160?159:-1;}
static int Rand_Range(int n){return n-1;}
/* ACTUAL_DRAFT_HELPERS */
int main(void){MsView v;int n,a,b,mask,i,p,counts[2],found;
 for(n=2;n<=4;n++)for(mask=0;mask<(1<<n);mask++)for(a=1;a<=9;a++)for(b=1;b<=10-a;b++){
  active=1;nplayers=n;CS_Init(&coop,n);counts[0]=counts[1]=0;
  for(p=0;p<n;p++){coop.team[p]=(mask>>p)&1;counts[coop.team[p]]++;}
  memset(&v,0,sizeof(v));v.mode=1;v.total=a+b;v.a=a;v.roster_count=10;
  for(i=0;i<v.total;i++){v.pick[i].chara=-1;v.roster[i]=i;}
  found=ms_owners(&v);assert(found==(counts[0]<=a&&counts[1]<=b));if(!found)continue;
  for(p=0;p<n;p++){i=ms_slot_for_pad(&v,p);assert(i>=0&&v.pick[i].pad==p);} // each player can choose immediately
  for(p=0;p<n;p++){i=ms_slot_for_pad(&v,p);v.pick[i].chara=p;}
  while((i=ms_first_empty(&v))>=0){int side=MS_Side(&v,i),leader=CS_Leader(&coop,side);if(leader<0)leader=0;int slot=ms_slot_for_pad(&v,leader);assert(slot>=0&&v.pick[slot].pad<0);v.pick[slot].chara=0;}
  assert(MS_Valid(&v));
 }
 active=0;for(n=1;n<=4;n++)for(a=1;a<=9;a++){
  memset(&v,0,sizeof(v));v.mode=2;v.total=10;v.a=a;v.roster_count=10;nplayers=n;
  for(i=0;i<10;i++){v.roster[i]=i;v.pick[i].chara=-1;}
  assert(ms_owners(&v));assert(ms_fill(&v));for(p=0;p<n;p++)assert(v.pick[p].pad==p);
 }
 memset(&v,0,sizeof(v));v.mode=1;v.a=2;v.total=4;for(i=0;i<4;i++)v.pick[i].chara=-1;v.pick[0].chara=159;v.slot=1;assert(ms_taken(&v,159)&&ms_taken(&v,160));v.slot=2;assert(!ms_taken(&v,159)&&!ms_taken(&v,160));
 puts("PASS actual draft helpers: all4-player team assignments across all45 team-size pairs, immediate owned picks, leader-only CPU fill, FFA owners");return 0;}
