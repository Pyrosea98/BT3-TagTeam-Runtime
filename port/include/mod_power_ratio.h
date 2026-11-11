#ifndef MOD_POWER_RATIO_H
#define MOD_POWER_RATIO_H
/* Exact percentage comparison without signed cross-product overflow. Callers use 90/150. */
static int PU_AtLeastPercent(long long value,long long reference,int percent){
 unsigned long long threshold;
 if(value<0||reference<=0||percent<0||percent>150)return 0;
 threshold=(unsigned long long)(reference/100)*(unsigned)percent+
           ((unsigned long long)(reference%100)*(unsigned)percent+99)/100;
 return (unsigned long long)value>=threshold;
}
#endif
