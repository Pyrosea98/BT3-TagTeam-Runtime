/* Source/display convention approved by Juan: M=10^6, MM=10^9, B=10^12, T=10^18.
   Canonical battle power is signed 64-bit; damage calibration is a separate scale. */
#ifndef MOD_POWER_UNITS_H
#define MOD_POWER_UNITS_H
#include <stdio.h>
#include <string.h>
#define PU_MAX 9223372036854775807LL
static long long PU_BUnit(int id,int costume){(void)id;(void)costume;return 1000000000000LL;}
static long long PU_Parse(const char*s,long long bunit){
 const char*p,*q;long long whole=0,fraction=0,scale=1,mult=1,answer;int digits=0,frac=0,k;char suffix[6];
 if(!s)return 0;while(*s==' ')s++;
 if(!strcmp(s,"\xe2\x88\x9e"))return PU_MAX;
 if((s[0]=='M'||s[0]=='m')&&(s[1]=='A'||s[1]=='a')&&(s[2]=='X'||s[2]=='x'))return PU_MAX;
 for(p=s;*p;p++){
  if(*p>='0'&&*p<='9'){
   int v=*p-'0';digits++;
   if(frac){if(scale<1000000000LL){fraction=fraction*10+v;scale*=10;}}
   else {if(whole>(PU_MAX-v)/10)return 0;whole=whole*10+v;}
  }else if(*p=='.'||*p==','){
   if(frac)return 0;
   for(q=p+1,k=0;q[k]>='0'&&q[k]<='9';k++){}
   if(k==3)continue;frac=1;
  }else break;
 }
 if(!digits)return 0;while(*p==' ')p++;
 for(k=0;k<5&&p[k];k++){char c=p[k];suffix[k]=(c>='A'&&c<='Z')?c-'A'+'a':c;}suffix[k]=0;
 if(!strcmp(suffix,"k")||!strcmp(suffix,"mil"))mult=1000LL;
 else if(!strcmp(suffix,"m"))mult=1000000LL;
 else if(!strcmp(suffix,"mm")||!strcmp(suffix,"g"))mult=1000000000LL;
 else if(!strcmp(suffix,"b"))mult=bunit;
 else if(!strcmp(suffix,"t"))mult=1000000000000000000LL;
 else if(!strcmp(suffix,"p"))mult=1000000000000000LL;
 else if(!strcmp(suffix,"e"))mult=1000000000000000000LL;
 else if(*suffix)return 0;
 if(whole>PU_MAX/mult)return 0;answer=whole*mult;
 /* All decimal suffix scales are powers of ten; divide first to avoid overflow. */
 if(scale>1){long long f=(mult>=scale)?fraction*(mult/scale):fraction/(scale/mult);if(answer>PU_MAX-f)return 0;answer+=f;}
 return answer;
}
#include "mod_power_ratio.h"
/* Exact integer grouping, independent of the operating system locale. */
static void PU_FormatInteger(long long value, char *out, size_t cap) {
    char digits[24], grouped[32];
    size_t i, n, at = 0;
    snprintf(digits, sizeof(digits), "%lld", value);
    n = strlen(digits);
    for (i = 0; i < n; i++) {
        if (i > (digits[0] == '-' ? 1u : 0u) && (n - i) % 3 == 0)
            grouped[at++] = ' ';
        grouped[at++] = digits[i];
    }
    grouped[at] = 0;
    snprintf(out, cap, "%s", grouped);
}

/* Keep Juan's long scale: M = millón, MM = mil millones, B = billón, T = trillón.
   Abbreviate only when the entire integer is representable with three decimal places. */
static void PU_Format(long long value, char *out, size_t cap, int spanish) {
    static const long long units[] = {
        1000000000000000000LL, 1000000000000LL, 1000000000LL, 1000000LL
    };
    static const char *const suffix[] = {"T", "B", "MM", "M"};
    size_t i;

    for (i = 0; i < sizeof(units) / sizeof(units[0]); i++) {
        long long unit = units[i];
        long long remainder;
        char whole[32];
        if (value < unit)
            continue;
        remainder = value % unit;
        if (remainder % (unit / 1000) != 0)
            break;
        PU_FormatInteger(value / unit, whole, sizeof(whole));
        if (remainder) {
            char fraction[8];
            size_t n;
            snprintf(fraction, sizeof(fraction), "%03d", (int)(remainder / (unit / 1000)));
            n = strlen(fraction);
            while (n > 0 && fraction[n - 1] == '0')
                fraction[--n] = 0;
            snprintf(out, cap, "%s%c%s %s", whole, spanish ? ',' : '.', fraction, suffix[i]);
        } else {
            snprintf(out, cap, "%s %s", whole, suffix[i]);
        }
        return;
    }
    PU_FormatInteger(value, out, cap);
}
#endif
