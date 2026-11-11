#ifndef TAG_TOURNAMENT_RANDOM_H
#define TAG_TOURNAMENT_RANDOM_H
#include "tag_tournament_core.h"
#define TT_RANDOM_MAX (329*8)
struct TTRandomChoice {int family,character,costume;};
static __inline__ unsigned TT_DrawNext(unsigned*seed){unsigned x=*seed?*seed:1;x^=x<<13;x^=x>>17;x^=x<<5;return *seed=x;}
/* Equal chance per family, then per available form, then per valid outfit.
   Do not weight a family more heavily merely because it has more costumes. */
static __inline__ int TT_DrawFighter(const struct TTRandomChoice*p,int n,const int*exclude,int ne,unsigned*seed,struct TTFighter*out){
 int families[329],forms[329],nf=0,nt=0,i,j,family,form,cost[8],nc=0;
 if(!p||!out||!seed||n<1||n>TT_RANDOM_MAX||ne<0||ne>5||(!exclude&&ne))return 0;
 for(i=0;i<n;i++){
  int reject=0;for(j=0;j<ne;j++)if(p[i].family==exclude[j])reject=1;
  if(reject)continue;
  for(j=0;j<nf;j++)if(families[j]==p[i].family)break;
  if(j==nf&&nf<329)families[nf++]=p[i].family;
 }
 if(!nf)return 0;
 family=families[TT_DrawNext(seed)%(unsigned)nf];
 for(i=0;i<n;i++)if(p[i].family==family){for(j=0;j<nt;j++)if(forms[j]==p[i].character)break;if(j==nt&&nt<329)forms[nt++]=p[i].character;}
 if(!nt)return 0;
 form=forms[TT_DrawNext(seed)%(unsigned)nt];
 for(i=0;i<n;i++)if(p[i].family==family&&p[i].character==form){for(j=0;j<nc;j++)if(cost[j]==p[i].costume)break;if(j==nc&&nc<8)cost[nc++]=p[i].costume;}
 if(!nc)return 0;
 out->character=form;out->costume=cost[TT_DrawNext(seed)%(unsigned)nc];for(i=0;i<8;i++)out->items[i]=0;return 1;
}
#endif
