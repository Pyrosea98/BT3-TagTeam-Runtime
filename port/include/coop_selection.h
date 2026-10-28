#ifndef COOP_SELECTION_H
#define COOP_SELECTION_H
struct CoopSetup {int players,team[4],cursor;};
static void CS_Init(struct CoopSetup *s,int n){int i;s->players=n<2?2:n>4?4:n;s->cursor=0;for(i=0;i<4;i++)s->team[i]=i<2?0:1;}
static int CS_Leader(const struct CoopSetup *s,int side){int i;for(i=0;i<s->players;i++)if(s->team[i]==side)return i;return -1;}
static int CS_Member(const struct CoopSetup *s,int side,int member){int i,k=0;for(i=0;i<s->players;i++)if(s->team[i]==side){if(k++==member)return i;}return -1;}
static int CS_Step(struct CoopSetup *s,unsigned p,unsigned repeat){
 if(p&(0x400|0x100))return -1;
 if(repeat&8)s->cursor=(s->cursor+s->players)%(s->players+1);
 else if(repeat&4)s->cursor=(s->cursor+1)%(s->players+1);
 else if((repeat&3)&&s->cursor<s->players)s->team[s->cursor]^=1;
 if((p&0x200)&&s->cursor==s->players)return 1;
 return 0;
}
#ifdef __cplusplus
extern "C" {
#endif
int Port_CoopActive(void);
void Port_CoopConfigure(int players,int t0,int t1,int t2,int t3,int training);
void Port_CoopReset(void);
int Port_CoopLeader(int side);
int Port_CoopMemberPad(int side,int member);
int Port_CoopSelectionPad(int side,int member,int selecting);
int Port_CoopTraining(void);
int Port_CoopPlayerViews(void);
#ifdef __cplusplus
}
#endif
#endif
