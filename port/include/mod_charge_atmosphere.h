/* Independent atmosphere policy. Regular battles use the canonical power of Goku (End) SSJ3 in costume 0; canonical power
 * only. Crater sizing/tiers and battle normalization are deliberately separate. */
#ifndef MOD_CHARGE_ATMOSPHERE_H
#define MOD_CHARGE_ATMOSPHERE_H
#include "mod_ground_charge.h"
/* Atmosphere belongs to outdoor skies, never space, interiors or the void. */
static int GCA_StageAllowed(int stage){switch(stage){case 0:case 1:case 2:case 3:case 4:case 5:case 6:case 7:case 9:case 10:case 11:case 12:case 13:case 14:case 15:case 17:case 19:case 20:case 21:case 22:case 23:case 24:case 25:case 26:case 27:case 28:case 29:case 30:case 31:case 32:case 34:case 40:case 42:case 43:return 1;default:return 0;}}
/* Goku (End) SSJ3, character 6 and costume 0. Use the internal value. */
#define GCA_THRESHOLD 40000000000LL
#define GCA_PREVIEW_THRESHOLD 5000000000000LL
#define GCA_STORY_THRESHOLD 10000000000000LL
static int GCA_Power(long long power){int decades=0;volatile int mantissa;float exponent;int level;
 if(power<GCA_THRESHOLD)return 0;
 int lowerBand=power<GCA_PREVIEW_THRESHOLD;
 if(!lowerBand&&power<GCA_STORY_THRESHOLD){
  unsigned steps=(unsigned)((power-GCA_PREVIEW_THRESHOLD)/200000000000LL);
  return 225+(int)steps;
 }
 while(power>10000000LL){power/=10;decades++;}mantissa=(int)power;
 exponent=(float)decades+logf((float)mantissa)*.4342944819f;
 if(lowerBand){
  /* log10 of the SSJ3 boundary and the width of the new power band. */
  level=(int)(175.f+50.f*(exponent-10.602060f)/2.096910f);
  if(level<175)level=175;
  if(level>224)level=224;
  return level;
 }
 level=(int)(250.f+125.f*(exponent-13.f));if(level<250)level=250;if(level>1000)level=1000;return level;
}
typedef struct {int tier,age,ttl,charging,max,strength;} GcaActor;
typedef struct {GcaActor actors[GC_ACTORS];int level;} GcaState;
static void GCA_Reset(GcaState*s){unsigned i;unsigned char*p=(unsigned char*)s;for(i=0;i<sizeof(*s);i++)p[i]=0;}
static void GCA_Touch(GcaState*s,int actor,long long power,int charging,int max,int eligible){
 GcaActor*a;if(actor<0||actor>=GC_ACTORS)return;a=s->actors+actor;
 if(!eligible||(!charging&&!max)){*a=(GcaActor){0};return;}
 if(!a->ttl)a->age=0;a->tier=GC_PowerTier(power);a->strength=GCA_Power(power);a->ttl=2;a->charging=charging;a->max=max;
 if(max&&a->age<19)a->age=19; /* persistent native flag6 */
}
static void GCA_Step(GcaState*s,int quality,int paused){int i,target=0;if(quality<1||quality>2){GCA_Reset(s);return;}if(paused)return;
 for(i=0;i<GC_ACTORS;i++){GcaActor*a=s->actors+i;if(a->ttl>0){a->ttl--;if(a->age<10800)a->age++;}else *a=(GcaActor){0};
 if(a->ttl>0&&a->age>18&&(a->charging||a->max)&&a->strength>target)target=a->strength;}
 /* About0.2 seconds at the native30Hz tick; never stack ten fighters' darkness. */
 if(target>s->level){s->level+=167;if(s->level>target)s->level=target;}
 else if(target<s->level){s->level-=167;if(s->level<target)s->level=target;}
}
static int GCA_Active(const GcaState*s){return s->level>0;}
static int GCA_Storm(const GcaState*s){int i,n=0;for(i=0;i<GC_ACTORS;i++)if(s->actors[i].ttl>0&&s->actors[i].max&&s->actors[i].strength>n)n=s->actors[i].strength;return n<s->level?n:s->level;}
#endif
