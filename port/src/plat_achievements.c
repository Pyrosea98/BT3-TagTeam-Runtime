/* Local achievements: game thread emits bits, render thread owns disk state.
 * Two alternating versioned/checksummed slots preserve the previous valid save
 * if a write is interrupted. Native memory-card files are never touched. */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#define mkdir(path,mode) _mkdir(path)
#endif
#include "port_host.h"
#include "mod_achievements.h"
static PORT_HOST unsigned sPending,sMask,sSequence;
static PORT_HOST int sLoaded,sSaveOk=1,sDirty;
static PORT_HOST time_t sRetryAfter;
/* Presentation metadata does not change stable unlock IDs or saved progress. */
int Port_AchievementRank(int index){static const int ranks[MA_COUNT]={MA_GOLD,MA_PLATINUM,MA_BRONZE,MA_SILVER,MA_GOLD,MA_PLATINUM,MA_SILVER,MA_GOLD,MA_PLATINUM,MA_SILVER,MA_GOLD,MA_PLATINUM,MA_SILVER,MA_GOLD,MA_PLATINUM,MA_SILVER,MA_GOLD,MA_PLATINUM,MA_GOLD,MA_PLATINUM,MA_SILVER,MA_GOLD,MA_PLATINUM,MA_SILVER,MA_GOLD,MA_PLATINUM};return index>=0&&index<MA_COUNT?ranks[index]:-1;}
static int dragon(int id){switch(id){case 146:case 147:return 1;case 261:return 2;case 76:return 3;case 145:return 4;case 275:return 5;case 213:case 245:return 6;case 276:return 7;}return 0;}
unsigned Port_AchievementEvaluate(const int *ids,const int *teams,const int *pads,int n,int winner,int form,int flags,int reason,int eligible){
 int i,solo=-1,ours=0,theirs=0;unsigned stars=0;
 if(!eligible||!ids||!teams||!pads||n!=8||winner<0||winner>1||flags!=(1<<winner)||!(reason&1)||(reason&0x18000))return 0;
 for(i=0;i<n;i++){
  if(teams[i]<0||teams[i]>1)return 0;
  if(teams[i]==winner){ours++;solo=i;}
  else {int star=dragon(ids[i]);if(!star||(stars&(1u<<star)))return 0;stars|=1u<<star;theirs++;}
 }
 if(ours!=1||theirs!=7||stars!=254u||pads[solo]<0||pads[solo]>3)return 0;
 /* Goku GT, Goku (End), full-power Goku GT. Fusions are deliberately excluded. */
 return MA_DRAGONS|((form==10||form==200||form==233)?MA_SSJ4_GOKU:0);
}
void Port_AchievementSubmit(unsigned bits){__atomic_fetch_or(&sPending,bits&MA_MASK,__ATOMIC_RELEASE);}
static unsigned checksum(unsigned seq,unsigned mask){return 0x42543341u^seq^(mask*2654435761u);}
static int path_for(char *out,unsigned slot){const char *root=getenv("BT3_SAVES");int n;if(!root||!*root)root="saves";mkdir(root,0755);n=snprintf(out,1024,"%s/achievements-%u.dat",root,slot);return n>0&&n<1024;}
static void load(void){unsigned slot;for(slot=0;slot<2;slot++){char path[1024];unsigned seq,mask,check;FILE *f;if(!path_for(path,slot))continue;f=fopen(path,"rb");if(!f)continue;
 if(fscanf(f,"BT3ACH1 %u %u %x",&seq,&mask,&check)==3&&check==checksum(seq,mask)&&seq>=sSequence){sSequence=seq;sMask=mask;}fclose(f);}sLoaded=1;}
static void save(void){char path[1024];unsigned seq=sSequence+1;FILE *f;int ok;if(!path_for(path,seq&1u)){sSaveOk=0;return;}f=fopen(path,"wb");if(!f){sSaveOk=0;return;}
 ok=fprintf(f,"BT3ACH1 %u %u %08x\n",seq,sMask,checksum(seq,sMask))>0;if(fflush(f)!=0)ok=0;if(fclose(f)!=0)ok=0;sSaveOk=ok;if(ok){sSequence=seq;sDirty=0;}}
unsigned Port_AchievementUpdate(void){unsigned fresh;if(!sLoaded)load();fresh=__atomic_exchange_n(&sPending,0,__ATOMIC_ACQ_REL)&~sMask;if(fresh){sMask|=fresh;sDirty=1;}if(sDirty&&(fresh||time(NULL)>=sRetryAfter)){save();sRetryAfter=time(NULL)+5;}return fresh;}
unsigned Port_AchievementMask(void){return sMask;}
int Port_AchievementSaveOk(void){return sSaveOk;}
