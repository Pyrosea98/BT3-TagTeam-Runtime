/* Native split-roster selection policy. */
#ifndef B8_SELECTION_CANDIDATE_H
#define B8_SELECTION_CANDIDATE_H
#include <stddef.h>

static int B8CostumeCountRule(int chara,int isAlias,int *costume) {
    int n=-1;
    switch(chara) {
    case 159:case 160:n=4;break;
    case 20:n=3;break; /* Movie-only black-haired C3, with its own Kaioken branch. */
    case 39:case 40:case 312:case 313:case 314:case 315:n=5;break;
    case 74:case 193:n=1;break;
    case 13:n=5;break;
    case 186:n=2;break;
    case 110:n=3;break; /* physical3 is now Gokoro; retain0,1,2 */
    case 29:n=5;break;
    case 180:n=3;break;
    case 196:n=1;break;
    case 202:if(costume&&*costume==3)*costume=2;n=3;break;
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
 {326,"30K"},{327,"30K"},{328,"35K"},{323,"3K"},{324,"3K"},{164,"MAX"},{194,"100B"},{201,"50B"},
 {218,"48B"},{219,"120B"},{220,"1.000B"},{221,"2.000B"},{222,"8.000B"},{223,"80.000B"},
 {225,"2.900B"},{226,"5.800B"},{229,"23.200B"},{230,"232.000B"},
 {235,"5.800B"},{238,"11.600B"},{239,"46.400B"},{241,"464.000B"},{294,"30B"},
 {261,"5.4B"},{275,"5.4B"},{276,"5.4B"},{213,"3.5B"},{245,"7B"},{247,"40MM"},
 {51,"100.000M"},{52,"5B"},{166,"10B"},{149,"40B"},
 {59,"80.000B-80.000B"},{53,"2.25B"},{249,"4.5B"},{172,"18B"},
 {54,"40.000B-40.000B"},{29,"18K-18K-24K-30K-18K"},{244,"320M"}
};
/* The modern selector uses the same promoted roster identities as the native
   form-text patch.  Keep this lookup tiny and data-only so the preview can
   show the actual power label without parsing the rendered text section. */
static const char *B8PowerText(int chara){
    unsigned i;
    for(i=0;i<sizeof(b8PowerTextCandidate)/sizeof(b8PowerTextCandidate[0]);i++)
        if(b8PowerTextCandidate[i].chara==chara)return b8PowerTextCandidate[i].text;
    return NULL;
}

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
