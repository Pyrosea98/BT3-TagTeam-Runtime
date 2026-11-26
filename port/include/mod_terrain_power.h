/* Canonical audited power, never raw labels or calibrated damage. Pointer/int ABI at native boundaries. */
#ifndef MOD_TERRAIN_POWER_H
#define MOD_TERRAIN_POWER_H
#include <math.h>
static int TP_Milli(long long power){volatile int mantissa;int decades=0;float f;if(power<=0)return 1000;while(power>10000000LL){power/=10;decades++;}mantissa=(int)power;f=.2f*((float)decades+logf((float)mantissa)*.4342944819f);if(f<.4f)f=.4f;if(f>4.f)f=4.f;return (int)(f*1000.f+.5f);}
/* Charging starts deforming terrain at the current roster's Goku (End) SSJ3
   power. Below that, the existing size-scaled levitating rocks remain. */
static float TP_ChargeRadius(long long power,long long ssj3){
 long long ratio;int decades=0;volatile int mantissa;float levels,r;
 if(ssj3<=0||power<ssj3)return 0;
 ratio=power/ssj3;while(ratio>10000000LL){ratio/=10;decades++;}
 mantissa=(int)ratio;levels=(float)decades+logf((float)mantissa)*.4342944819f;
 r=20.f+25.f*levels;if(r>240.f)r=240.f;return r;
}

#endif
