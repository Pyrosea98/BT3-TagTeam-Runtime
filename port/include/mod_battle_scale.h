#ifndef MOD_BATTLE_SCALE_H
#define MOD_BATTLE_SCALE_H
#include <math.h>
/* Build the positive float representation without an unsupported soft-float i64 conversion.
   Canonical power stays exact; only the bounded normalization uses this ~24-bit approximation. */
static float PS_PositiveFloat(long long power){
 union {unsigned bits;float value;} n;unsigned long long v;unsigned mantissa;int exponent=0;
 if(power<=0)return 0.f;v=(unsigned long long)power;
 while(v>1){v>>=1;exponent++;}
 mantissa=exponent>23?(unsigned)((unsigned long long)power>>(exponent-23)):(unsigned)power<<(23-exponent);
 n.bits=((unsigned)(127+exponent)<<23)|(mantissa&0x7fffffu);return n.value;
}
/* Canonical labels remain intact. Compression is local combat policy only. */
static float PS_Strength(long long power,int mode){
 float lp,z,v;if(mode==0||power<=0)return 1.f;
 lp=logf(PS_PositiveFloat(power));
 if(mode==1){
  /* Smooth bounded differences: no hard-clamped high/low roster bands. */
  z=(lp-18.405439907382451f)/8.4833518659657887f;
  return 1.025f+.225f*z/sqrtf(1.f+z*z);
 }
 /* Keep the accepted upper-tier quarter-root curve. Below Omega use a
    gentler exponent rather than flattening most of the roster at0.25. */
 z=lp-logf(8.e13f);v=expf(z*(z<0.f?.1f:.25f));
 return v;
}
#endif
