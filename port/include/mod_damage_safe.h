/* Signed32 damage boundaries. Ordinary in-range integer/float rounding is unchanged. */
#ifndef MOD_DAMAGE_SAFE_H
#define MOD_DAMAGE_SAFE_H
#define MD_MAX 2147483647
static int MD_Clamp(long long v){return v>2147483647LL?MD_MAX:v<(-2147483647LL-1)?(-2147483647-1):(int)v;}
static int MD_Add(int a,int b){return MD_Clamp((long long)a+b);}
static int MD_MulDiv(int a,int b,int divisor){return divisor?MD_Clamp((long long)a*b/divisor):0;}
static int MD_FromDouble(double v){if(v!=v)return 0;return v>=2147483647.0?MD_MAX:v<=-2147483648.0?(-2147483647-1):(int)v;}
static int MD_FloatScale(int value,float scale){float product=value*scale;return MD_FromDouble((double)product);}
static int MD_Round10(int value){int rem;if(value<10)return value;rem=value%10;return rem?MD_Add(value-rem,10):value;}
static int MD_ComboTimer(int damage){int v;if(damage>=20000)return 90;v=(int)(damage*30.0f*.0001f)+30;return v>90?90:v;}
#endif
