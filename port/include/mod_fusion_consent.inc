/* Game-thread mutual consent. R3 is intercepted before transform/assist records. */
#ifdef PORT
extern int Port_TargetHuman(int),Port_FusionPartnerActor(int,int),Port_FusionConsumedActor(int),Port_NetActive(void),BattleReplay_IsActive(void);
extern void BtlAct_Request(BtlActJChr*,int);
extern BtlActJChr*BtlChar_Get(int);extern int BtlChar_GetCount(void),BtlSeq_GetState(void);
typedef struct FusionOffer {int active,partner,slot,member,age,accepted,sourceChara,partnerChara;} FusionOffer;
static FusionOffer sFusionOffer[12];static unsigned sFusionConsentHeld[12],sFusionConsentBlocked[12];
void Port_FusionConsentReset(void){int i;for(i=0;i<12;i++){sFusionOffer[i].active=0;sFusionConsentHeld[i]=sFusionConsentBlocked[i]=0;}}
static int pf_offer_for(int who){int i;for(i=0;i<12;i++)if(sFusionOffer[i].active&&(i==who||sFusionOffer[i].partner==who))return i;return -1;}
int Port_FusionConsentHud(int who){int i=pf_offer_for(who);return i>=0&&i!=who;}
unsigned Port_FusionConsentInput(int who,unsigned raw,unsigned r3){int i,press;if(who<0||who>=12)return raw;
 press=(raw&r3)&&!sFusionConsentHeld[who];sFusionConsentHeld[who]=raw&r3;
 if(Port_NetActive()||BattleReplay_IsActive()||BtlSeq_GetState()!=3)return raw;
 if(sFusionConsentBlocked[who]){if(!(raw&r3))sFusionConsentBlocked[who]=0;return raw&~r3;}
 i=pf_offer_for(who);if(i>=0&&i!=who&&press){sFusionOffer[i].accepted=1;sFusionConsentBlocked[who]=1;return raw&~r3;}
 return raw;
}
extern int BtlChar_IsFrozen(BtlActJChr*),ChrCam_IsCutActive(BtlActJChr*);
static int pf_consent_ready(BtlActJChr*c){int a=BtlAct_GetCurrent(c);return BtlChar_IsFree(c)&&c->form.allowed&&!BtlChar_IsFrozen(c)&&!ChrCam_IsCutActive(c)&&((a>=11&&a<=26)||a==67);}
static int Port_FusionConsentChoice(BtlActJChr*c){int who=c->player;FusionOffer*f;BtlActJChr*p;
 if(who<0||who>=12||Port_NetActive()||BattleReplay_IsActive())return -1;f=&sFusionOffer[who];if(!f->active||!f->accepted)return -1;
 p=BtlChar_Get(f->partner);return p&&pf_consent_ready(c)&&pf_consent_ready(p)?f->slot:-1;
}
static int Port_FusionInvite(BtlActJChr*c,int slot,int member){int who=c->player,target=Port_FusionPartnerActor(who,member),i;FusionOffer*f;BtlActJChr*p;
 if(Port_NetActive()||BattleReplay_IsActive()||!Port_TargetHuman(who)||target<0||!Port_TargetHuman(target))return 0;
 if(who<0||who>=12||target>=12||target==who)return 1;
 i=pf_offer_for(who);if(i==who){f=&sFusionOffer[who];p=BtlChar_Get(target);
  if(f->accepted&&f->partner==target&&f->slot==slot&&p&&pf_consent_ready(c)&&pf_consent_ready(p)){f->active=0;return 0;}
 }if(i>=0||pf_offer_for(target)>=0)return 1;
 p=BtlChar_Get(target);if(!p||Port_FusionConsumedActor(target)||BtlMember_GetActiveGauge(p)->health<=0)return 1;
 f=&sFusionOffer[who];f->active=1;f->partner=target;f->slot=slot;f->member=member;f->age=f->accepted=0;
 f->sourceChara=BtlMember_GetActive(c)->chara;f->partnerChara=BtlMember_GetActive(p)->chara;
 Port_FusionLog("fusion-consent: actor%d offers actor%d slot%d; partner taps R3\n",who,target,slot);return 1;
}
void Port_FusionConsentTick(void){int i,n=BtlChar_GetCount();if(Port_NetActive()||BattleReplay_IsActive()||BtlSeq_GetState()!=3){Port_FusionConsentReset();return;}
 for(i=0;i<12;i++)if(sFusionOffer[i].active){FusionOffer*f=&sFusionOffer[i];BtlActJChr*c=i<n?BtlChar_Get(i):0,*p=f->partner<n?BtlChar_Get(f->partner):0;int member=-1;
  if(++f->age>120||!c||!p||!Port_TargetHuman(i)||!Port_TargetHuman(f->partner)||Port_FusionConsumedActor(i)||Port_FusionConsumedActor(f->partner)||
     BtlMember_GetActiveGauge(c)->health<=0||BtlMember_GetActiveGauge(p)->health<=0||BtlMember_GetActive(c)->chara!=f->sourceChara||BtlMember_GetActive(p)->chara!=f->partnerChara){f->active=0;continue;}
  /* Accepted offers are consumed only by the ordinary native fusion decision,
     never by forcing an action from this late maintenance tick. */
 }
}
#endif
