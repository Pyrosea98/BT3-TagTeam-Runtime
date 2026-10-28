#include "coop_selection.h"
static struct CoopSetup sCoop;
static int sCoopActive,sCoopTraining;
int Port_CoopActive(void){return sCoopActive&&!Port_NetActive();}
void Port_CoopConfigure(int n,int a,int b,int c,int d,int training){int i,t[4]={a,b,c,d};sCoopActive=0;if(n<2||n>4)return;CS_Init(&sCoop,n);for(i=0;i<n;i++){if(t[i]<0||t[i]>1)return;sCoop.team[i]=t[i];}sCoopTraining=training!=0;sCoopActive=1;}
void Port_CoopReset(void){sCoopActive=0;sCoopTraining=0;}
int Port_CoopTraining(void){return Port_CoopActive()&&sCoopTraining;}
int Port_CoopLeader(int side){return Port_CoopActive()&&side>=0&&side<2?CS_Leader(&sCoop,side):-1;}
int Port_CoopMemberPad(int side,int member){return Port_CoopActive()&&side>=0&&side<2&&member>=0&&member<5?CS_Member(&sCoop,side,member):-1;}
extern int PortInput_PlayerAvailable(int player);
int Port_CoopSelectionPad(int side,int member,int selecting){
 int p,i;
 if(!Port_CoopActive())return -1;
 p=selecting?Port_CoopMemberPad(side,member):-1;
 if(p>=0&&PortInput_PlayerAvailable(p))return p;
 p=Port_CoopLeader(side);
 if(p>=0&&PortInput_PlayerAvailable(p))return p;
 for(i=0;i<sCoop.players;i++)if(sCoop.team[i]==side&&PortInput_PlayerAvailable(i))return i;
 for(i=0;i<sCoop.players;i++)if(PortInput_PlayerAvailable(i))return i;
 return 0; /* no input device: reconnecting any controller can still recover the menu */
}

int Port_CoopPlayerViews(void){extern int Port_FusionViews(int);int n=Port_FusionViews(-1);return Port_CoopActive()?(n>0?n:sCoop.players):0;}
