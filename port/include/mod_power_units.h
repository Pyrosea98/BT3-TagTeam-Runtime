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
static void PU_Format(long long v,char*out,size_t cap){
 static const long long steps[]={1LL,1000LL,1000000LL,1000000000LL,1000000000000LL,1000000000000000000LL};
 static const char*units[]={"","K","M","MM","B","T"};int i=0;char dec[5];long long unit,f;
 if(v<=0){snprintf(out,cap,"?");return;}if(v==PU_MAX){snprintf(out,cap,"MAX");return;}
 while(i<5&&v>=steps[i+1])i++;unit=steps[i];
 if(!i||v%unit==0){snprintf(out,cap,"%lld%s",v/unit,units[i]);return;}
 f=(v%unit)/(unit/1000);snprintf(dec,sizeof(dec),"%03lld",f);
 {int n=(int)strlen(dec);while(n&&dec[n-1]=='0')dec[--n]=0;}
 snprintf(out,cap,"%lld.%s%s",v/unit,dec,units[i]);
}
#endif
