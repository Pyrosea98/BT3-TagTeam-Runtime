#ifndef MOD_ENVIRONMENT_RULES_H
#define MOD_ENVIRONMENT_RULES_H
/* Native roster identities, including the independent Multiverse Buu family. */
static int ENV_Buu(int id){return (id>=112&&id<=117)||(id>=291&&id<=294);}
/* Use the level from the loaded map: priority mods already include scaling.
   Dying Namek has a native sheet at60 without the water feature flag. */
static int ENV_Liquid(int stage,int has,float native,float*out){
 if(stage==3){*out=60.f;return 1;}
 if(!has)return 0;
 *out=native;return 1;
}
/* Positive = damage, negative = healing. One tick per simulated second. */
static int ENV_Amount(int maxHp,int lava,int inside,int buu){
 int amount;if(maxHp<=0)return 0;
 if(lava){amount=maxHp/20;return amount>0?amount:1;}
 if(!inside)return 0;
 amount=maxHp/1000;if(amount<1)amount=1;if(amount>250)amount=250;return buu?-amount:amount;
}
#endif
