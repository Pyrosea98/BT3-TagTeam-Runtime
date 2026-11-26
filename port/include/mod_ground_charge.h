/* Bounded cosmetic ground-charge prototype. +Y down; no collision or game RNG writes. */
#ifndef MOD_GROUND_CHARGE_H
#define MOD_GROUND_CHARGE_H
#include <math.h>
#define GC_ACTORS 16
#define GC_MARKS 32
#define GC_ROCKS 64
typedef struct {float x,y,z;} GcVec;
typedef struct {GcVec p,n;int age,owner,live;float height;int surface,tier;} GcMark;
typedef struct {GcVec p;float size,shade,alpha;int seed,surface;} GcRock;
typedef struct {GcVec p,n;int held,age,fall,live;float height;int surface,tier;} GcActor;
typedef struct {GcActor actors[GC_ACTORS];GcMark marks[GC_MARKS];GcRock rocks[GC_ROCKS];int next,quality,count;} GcState;
static void GC_Reset(GcState*s){unsigned i;unsigned char*p=(unsigned char*)s;for(i=0;i<sizeof(*s);i++)p[i]=0;}
/* Prevent the soft-float compiler from promoting unsigned range deductions to an unsupported i64 conversion. */
static float GC_Float(int x){volatile int v=x;return (float)v;}
/* Visual power bands, not a claim about a fighter's canonical era. Canonical long-scale units. */
static int GC_PowerTier(long long p){return p<=0?0:p<1000LL?0:p<1000000LL?1:p<10000000000LL?2:p<100000000000000LL?3:p<1000000000000000LL?4:5;}
/* God-tier ground presence must not shrink with child-sized donor models. */
static float GC_EffectHeight(float h,int tier){if(!isfinite(h)||h<.2f)h=10.f;if(h>80.f)h=80.f;if(tier==5&&h<20.4f)h=20.4f;return h;}
static float GC_TierScale(int tier){static const float scales[6]={.35f,.65f,1.f,1.35f,1.7f,2.25f};return scales[tier>=0&&tier<6?tier:0];}
static int GC_Finite(GcVec p){return isfinite(p.x)&&isfinite(p.y)&&isfinite(p.z)&&fabsf(p.x)<1.e7f&&fabsf(p.y)<1.e7f&&fabsf(p.z)<1.e7f;}
static void GC_Touch(GcState*s,int actor,GcVec p,GcVec n,int eligible){
 GcActor*a;float dx,dz;if(!s->quality||actor<0||actor>=GC_ACTORS||!eligible||!GC_Finite(p)||!GC_Finite(n)||n.y>-.7f)return;
 a=s->actors+actor;dx=p.x-a->p.x;dz=p.z-a->p.z;
 if(!a->live||a->fall||dx*dx+dz*dz>6400.f){GcMark*m=s->marks+s->next;s->next=(s->next+1)%GC_MARKS;*m=(GcMark){p,n,0,actor,1};*a=(GcActor){p,n,1,0,0,1};}
 a->held=1;
}
static void GC_Step(GcState*s,int quality,int paused){
 int i,j,pass;
 if(quality<1||quality>2){if(s->quality)GC_Reset(s);return;}s->quality=quality;
 if(paused)return;s->count=0;
 for(i=0;i<GC_MARKS;i++)if(s->marks[i].live&&++s->marks[i].age>900)s->marks[i].live=0;
 for(i=0;i<GC_ACTORS;i++){GcActor*a=s->actors+i;if(!a->live)continue;
 if(a->held){a->age=(a->age+1)%10800;a->fall=0;}else if(++a->fall>24){a->live=0;continue;}a->held=0;}
 /* Round-robin allocation: every charger gets a first rock before god-tier extras. */
 for(pass=0;pass<8&&s->count<GC_ROCKS;pass++)for(i=0;i<GC_ACTORS&&s->count<GC_ROCKS;i++){
 GcActor*a=s->actors+i;float h,scale,phase,fade,t,rad;int per,period=108;GcRock*r;
 if(!a->live)continue;per=(quality==1?2:3)+a->tier;if(quality==1)per=(per+1)/2;if(pass>=per)continue;j=pass;
 h=a->height>.1f?a->height:10.f;scale=GC_TierScale(a->tier);
 phase=GC_Float((a->age+j*23+i*7)%period)/GC_Float(period);
 /* A new chip rises from the ground each cycle; both ends are transparent. */
 fade=phase<.13f?phase/.13f:phase>.72f?(1.f-phase)/.28f:1.f;
 if(a->age<24)fade*=GC_Float(a->age)/24.f;fade*=1.f-GC_Float(a->fall)/24.f;
 t=GC_Float(i)*1.77f+GC_Float(j)*2.399963f;
 rad=h*scale*(.38f+.047f*GC_Float(j));r=s->rocks+s->count++;
 r->p=(GcVec){a->p.x+cosf(t)*rad,a->p.y-h*(.02f+scale*phase*(.95f+.09f*GC_Float(j))),a->p.z+sinf(t)*rad};
 r->size=h*scale*(.027f+.004f*GC_Float(j));r->shade=.65f+.035f*GC_Float(j);r->alpha=fade;r->seed=i*37+j*19;r->surface=a->surface;
 }

}
#endif
