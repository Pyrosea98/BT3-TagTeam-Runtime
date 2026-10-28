/* Delayed refill for all participants of native modded Practice, including humans. */
extern int Battle_GetMode(void),Port_ModesFight(void);
static int sPracticeHp[12],sPracticeDelay[12],sPracticeCid[12];
static int th_scope(void){int m=Battle_GetMode();return !Port_NetActive()&&!BattleReplay_IsActive()&&(m==5||m==6||Port_ModesFight()==3);}
void Port_TrainingHealthReset(void){memset(sPracticeHp,0,sizeof(sPracticeHp));memset(sPracticeDelay,0,sizeof(sPracticeDelay));memset(sPracticeCid,0,sizeof(sPracticeCid));}
int Port_TrainingNoKO(int who){return who>=0&&who<BtlChar_GetCount()&&th_scope()&&Port_Setting("training_refill_health",1)&&as_alive(who);}
void Port_TrainingHealthTick(void){int i,n=BtlChar_GetCount(),delay;if(!th_scope()){Port_TrainingHealthReset();return;}if(as_paused()||BtlSeq_GetState()!=3)return;if(n>12)n=12;
 delay=as_bound("training_health_delay_ms",2000,0,30000);delay=(delay*30+999)/1000;
 for(i=0;i<n;i++){void*c=BtlChar_Get(i),*m=c?BtlMember_GetActive(c):0;int hp,cid;if(!m||!as_alive(i)){sPracticeHp[i]=sPracticeDelay[i]=0;continue;}
  hp=as_read(m,0x40);cid=as_read(m,0)+1;
  if(Port_Setting("training_refill_health",1)){
   if(sPracticeCid[i]!=cid||!sPracticeHp[i]||hp<sPracticeHp[i])sPracticeDelay[i]=delay;
   if(sPracticeDelay[i]>0)sPracticeDelay[i]--;else if(as_read(m,0x44)>hp)*(int*)((unsigned char*)m+0x40)=as_read(m,0x44);
  }else sPracticeDelay[i]=0;
  sPracticeHp[i]=as_read(m,0x40);sPracticeCid[i]=cid;
  if(Port_Setting("training_refill_ki",0)&&!(as_read(c,0x1278)==1&&Port_Setting("training_cpu_behavior",1)==4))*(int*)((unsigned char*)m+0x4c)=as_read(m,0x50);
  if(Port_Setting("training_refill_stocks",0))*(int*)((unsigned char*)m+0x54)=as_read(m,0x58);
 }
}
