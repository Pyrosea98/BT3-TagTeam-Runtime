#ifndef MOD_DRAGON_ABSORB_H
#define MOD_DRAGON_ABSORB_H
/* Same -20..40 member-bonus bounds used by native fusion. Five guaranteed
 * levels per absorbed sibling, plus its positive equipped stat contributions.
 * Health refill is deferred until Omega parameters load; no speed, Ki or arbitrary ability-bit inheritance. */
static void DA_Merge(int *dst,const int *donor){
 static const int slots[]={1,2,5,6};int i;
 for(i=0;i<4;i++){int k=slots[i];long long v=(long long)dst[k]+5+(donor[k]>0?donor[k]:0);dst[k]=v>40?40:v< -20?-20:(int)v;}
}
#endif
