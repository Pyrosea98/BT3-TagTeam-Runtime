/* Native split-roster selection policy. */
#ifndef B8_SELECTION_CANDIDATE_H
#define B8_SELECTION_CANDIDATE_H
#include <stddef.h>

static int B8CostumeCountRule(int chara,int isAlias,int *costume) {
    int n=-1;
    switch(chara) {
    case 29:n=5;break;
    case 122:n=2;break;
    case 59:case 54:n=2;break;
    case 52:case 166:case 149:case 53:case 249:case 172:n=1;break;
    default:if(chara==210||chara==51||isAlias)n=1;break;
    }
    /* 110 intentionally falls through to stock: retain its current costumes. */
    if(n>0&&costume&&(*costume<0||*costume>=n))*costume=0;
    return n;
}
typedef struct {int chara;const char *text;} B8PowerTextCandidate;
static const B8PowerTextCandidate b8PowerTextCandidate[]={
 {51,"100.000M"},{52,"5B"},{166,"10B"},{149,"40B"},
 {59,"80.000B-80.000B"},{53,"2.25B"},{249,"4.5B"},{172,"18B"},
 {54,"40.000B-40.000B"},{29,"18K-18K-24K-30K-18K"},{244,"320M"}
};

/* Stage missing base/Oozaru/SSJ4 ids atomically; never drop an existing form.
   Integrate after the existing split-form list override so244 cannot be lost. */
static int B8ExploradorFormsRule(int chara,int *forms,int *count,int capacity) {
    static const int wanted[]={29,30,244};
    int missing[3],need=0,i,j,found;
    if(chara!=29||!forms||!count||*count<0||*count>capacity||capacity<0)return 0;
    for(i=0;i<3;i++){
        found=0;for(j=0;j<*count;j++)if(forms[j]==wanted[i])found=1;
        if(!found)missing[need++]=wanted[i];
    }
    if(need>capacity-*count)return 0;
    for(i=0;i<need;i++)forms[(*count)++]=missing[i];
    return 1;
}
#endif
