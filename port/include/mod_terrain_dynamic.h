/* One deterministic remesher shared by native collision and GPU stage rendering. +Y is down.
   Original triangles are replaced, never covered by a decal.

   Revision 2 (Claude, 2026-11-13), same entry points and event layout as revision 1 (Codex):
   - Height-field displacement instead of a plane match. Every triangle inside a scar's footprint is moved
     down by one continuous function of position, so rolling ground, slopes and the walls that stand on the
     ground stay joined. Revision 1 only accepted triangles within 2 units of the event plane.
   - The cutting grid follows the scar (along and across a trench), 8 to 16 cells across the width, and only the
     cells a triangle touches are visited. Revision 1 visited every cell of a world-aligned 8..24 grid.
   - Separate profiles for bowl, sphere cap and trench, a raised uneven rim and a scorched ring.
   - Overlap is tested on the real footprints (capsule against capsule), not their bounding boxes.
   - TD_RemeshBox emits only what overlaps a query box, for collision queries.

   Revision 3 (Claude, 2026-11-13): TD_Soil, a second drawing pass that lays exposed soil inside each scar. */
#ifndef MOD_TERRAIN_DYNAMIC_H
#define MOD_TERRAIN_DYNAMIC_H
#include <math.h>
#include "mod_terrain_appearance.h"

#define TD_MAX 64
#define TD_SMALL_MAX 48
#define TD_LARGE_MAX 32
#define TD_ATTR 12
#define TD_CELLS_ACROSS 16
#define TD_CELLS_ALONG 48
#define TD_POLY 20
typedef struct {float ax,ay,az,bx,bz,r,depth,sx,sz;int kind,level,fx;} TdScar;
typedef struct {int enabled,count,serial,stage;TdScar scars[TD_MAX];} TdState;
typedef struct {int kind,owner,fx,pad;float ax,ay,az,bx,bz,r,depth,sx,sz;} TdEvent;
typedef void (*TdEmit)(const float*,void*);
/* Soil pass: the lowered pieces again, with soil texture coordinates, tint and a fading alpha. */
typedef struct {TdEmit cb;void*user;float rgb[3];} TdSoil;

static void td_copy(void*d,const void*s,int n){unsigned char*a=d;const unsigned char*b=s;while(n-->0)*a++=*b++;}
static float td_min(float a,float b){return a<b?a:b;}
static float td_max(float a,float b){return a>b?a:b;}
static float td_clamp(float a,float l,float h){return a<l?l:a>h?h:a;}
static int td_finite(float v){return isfinite(v)&&fabsf(v)<1000000.f;}
/* Squared distance in XZ from a point to the scar's spine (a point for round scars). */
static float td_dist2(const TdScar*s,float x,float z){
 float dx=s->bx-s->ax,dz=s->bz-s->az,l=dx*dx+dz*dz,t=l>0.001f?td_clamp(((x-s->ax)*dx+(z-s->az)*dz)/l,0,1):0;
 x-=s->ax+t*dx;z-=s->az+t*dz;return x*x+z*z;
}
static void td_bounds(const TdScar*s,float*b){
 b[0]=td_min(s->ax,s->bx)-s->r;b[1]=td_min(s->az,s->bz)-s->r;b[2]=td_max(s->ax,s->bx)+s->r;b[3]=td_max(s->az,s->bz)+s->r;
}
/* Smallest XZ distance between the spines of two scars. */
static float td_spine_dist(const TdScar*p,const TdScar*q){
 float d=td_dist2(p,q->ax,q->az),e;
 e=td_dist2(p,q->bx,q->bz);if(e<d)d=e;e=td_dist2(q,p->ax,p->az);if(e<d)d=e;e=td_dist2(q,p->bx,p->bz);if(e<d)d=e;
 {float rx=p->bx-p->ax,rz=p->bz-p->az,sx=q->bx-q->ax,sz=q->bz-q->az,den=rx*sz-rz*sx;
  if(fabsf(den)>0.0001f){float t=((q->ax-p->ax)*sz-(q->az-p->az)*sx)/den,u=((q->ax-p->ax)*rz-(q->az-p->az)*rx)/den;if(t>=0&&t<=1&&u>=0&&u<=1)d=0;}}
 return sqrtf(d);
}
/* Persistent bounded pool: 64 total, 48 small / 32 large. No eviction or healing.
   Huge destruction remains capped separately; rendering never writes this state. */
static int TD_Add(TdState*t,const TdEvent*e){
 TdScar s;int i,small=0,large=0;
 if(!t->enabled||!e||!td_finite(e->ax)||!td_finite(e->ay)||!td_finite(e->az)||!td_finite(e->bx)||!td_finite(e->bz)||!isfinite(e->r)
    ||!isfinite(e->depth)||e->r<3||e->r>600||e->depth<=0||e->depth>240||!isfinite(e->sx)||!isfinite(e->sz)||fabsf(e->sx)>.7f||fabsf(e->sz)>.7f)return 0;
 s=(TdScar){e->ax,e->ay,e->az,e->bx,e->bz,e->r,e->depth,e->sx,e->sz,e->kind,1,e->fx};
 if(e->kind==0){s.bx=s.ax;s.bz=s.az;}
 for(i=0;i<t->count;i++){
  TdScar*p=&t->scars[i];
  if(p->kind||p->r>60)large++;else small++;
  if(td_spine_dist(p,&s)<p->r+s.r+2&&fabsf(p->ay-s.ay)<140){
   if(!s.kind&&!p->kind&&td_dist2(p,s.ax,s.az)<p->r*p->r*.15f&&s.r<=p->r*1.05f&&p->level<3){
    p->depth=td_min(240,p->depth+s.depth*.35f);p->level++;t->serial++;return 2;
   }
   return 0;
  }
 }
 if(t->count>=TD_MAX||((s.kind||s.r>60)?large>=TD_LARGE_MAX:small>=TD_SMALL_MAX))return 0;
 /* mass-destruction craters (over 250 units) at most two at a time, so the terrain cost stays bounded */
 if(s.r>250){int big=0;for(i=0;i<t->count;i++)if(t->scars[i].r>250)big++;if(big>=2)return 0;}
 t->scars[t->count++]=s;t->serial++;return 1;
}
/* Depth (positive is down, negative is the raised rim) and vertex shade at a point of the footprint. */
static float td_profile(const TdScar*s,float x,float z,float*shade,float*cover){
 float q=sqrtf(td_dist2(s,x,z))/s->r,k=5.3f/s->r,ph=s->ax*.37f+s->az*.61f,n,f,lip,scorch=0,p;
 if(shade)*shade=1;
 if(cover)*cover=0;
 if(q>=1)return 0;
 n=.5f*sinf(x*k+ph)+.5f*sinf(z*k*1.31f+1.7f*ph);      /* uneven rim, fixed by the scar's position */
 q*=1.12f+.10f*n;
 if(q>=1)return 0;
 if(cover)*cover=td_clamp((1-q)/.1f,0,1);     /* exposed soil: all of the inside, fading out across the rim */
 if(q>.6f)scorch=.2f*sinf(3.14159265f*(q-.6f)/.4f);
 if(q>=.8f){
  lip=(s->fx==6||s->fx==12)?.08f:.14f;f=-lip*sinf(3.14159265f*(q-.8f)/.2f);
  if(shade)*shade=1-scorch;
  return s->depth*f;
 }
 q/=.8f;
 if(s->kind){float u=td_clamp((q-.3f)/.7f,0,1);p=1-u*u*(3-2*u);}      /* trench: flat bottom */
 else if(s->fx==6||s->fx==12)p=powf(1-q*q,.6f);                        /* sphere cap: steep sides */
 else p=1-q*q;                                                          /* bowl */
 if(shade)*shade=1-scorch-.55f*p;
 return s->depth*p;
}
static float TD_Depth(const TdScar*s,float x,float z){return td_profile(s,x,z,0,0);}
/* How much of the depth a vertex takes, from its height against the scar's own ground plane: all of it near
   the ground, fading to nothing above and below, so ceilings, bridges and lower floors are left alone. */
static float td_weight(const TdScar*s,float x,float y,float z){
 float dy=y-(s->ay+s->sx*(x-s->ax)+s->sz*(z-s->az)),up=.35f*s->r+3,down=.6f*s->r+3;
 if(dy<-up)return td_clamp(1-(-dy-up)/up,0,1);
 if(dy>down)return td_clamp(1-(dy-down)/down,0,1);
 return 1;
}
static void td_lerp(float*d,const float*a,const float*b,float t){int i;for(i=0;i<TD_ATTR;i++)d[i]=a[i]+(b[i]-a[i])*t;}
/* Convex clip against the line ux*x + uz*z = cut; keep = +1 keeps the side where the value is larger. */
static int td_clip(float out[TD_POLY][TD_ATTR],float in[TD_POLY][TD_ATTR],int n,float ux,float uz,float cut,int keep){
 int i,m=0;
 for(i=0;i<n;i++){
  float*a=in[i],*b=in[(i+1)%n],da=(a[0]*ux+a[2]*uz-cut)*keep,db=(b[0]*ux+b[2]*uz-cut)*keep;int ia=da>=-0.00001f,ib=db>=-0.00001f;
  if(ia&&m<TD_POLY)td_copy(out[m++],a,TD_ATTR*4);
  if(ia!=ib&&m<TD_POLY)td_lerp(out[m++],a,b,td_clamp(da/(da-db),0,1));     /* clamped: an edge lying along the cut must not shoot past its ends */
 }
 return m;
}
static int td_poly_in_box(float p[TD_POLY][TD_ATTR],int n,const float*box){
 int i;float x0=p[0][0],x1=x0,z0=p[0][2],z1=z0;
 if(!box)return 1;
 for(i=1;i<n;i++){x0=td_min(x0,p[i][0]);x1=td_max(x1,p[i][0]);z0=td_min(z0,p[i][2]);z1=td_max(z1,p[i][2]);}
 return x0<=box[2]&&x1>=box[0]&&z0<=box[3]&&z1>=box[1];
}
/* Soil texture coordinates: the texture lies once across a round scar. Along a trench its two halves cap the
   ends and its middle column is drawn out along the length, wandering a little so the streaks vary. */
static void td_soil_uv(const TdScar*s,float x,float z,float*u,float*v){
 /* Planar world coordinates, one texture tile per 40 units: the soil keeps its size on small and huge craters alike,
    so nothing is drawn out. */
 (void)s;*u=x/40.f;*v=z/40.f;
}
/* Emit a convex piece as a fan. h, shade and cover, when given, hold the depth, shade and soil cover of each
   corner. With soil set, lowered pieces go to the soil callback and untouched pieces only travel on. */
static void td_fan(float p[TD_POLY][TD_ATTR],int n,const TdScar*s,const float*h,const float*shade,const float*cover,const float*box,const TdSoil*soil,TdEmit cb,void*user){
 float tri[3*TD_ATTR];int i,j;
 if(n<3||!td_poly_in_box(p,n,box))return;
 if(s)for(i=0;i<n;i++){
  float w=td_weight(s,p[i][0],p[i][1],p[i][2]),k=1-(1-shade[i])*w;
  p[i][1]+=h[i]*w;
  if(soil){float l=.62f+.38f*k,a=128*cover[i]*w;
   if(!s->kind)TVA_FacetedSoil((int)(s->ax*.31f+s->az*.17f),p[i][0]-s->ax,p[i][2]-s->az,s->r,&l,&a);
   p[i][4]=soil->rgb[0]*l;p[i][5]=soil->rgb[1]*l;p[i][6]=soil->rgb[2]*l;p[i][7]=a;td_soil_uv(s,p[i][0],p[i][2],&p[i][8],&p[i][9]);}
  else{p[i][4]*=k;p[i][5]*=k;p[i][6]*=k;}
 }
 if(s&&soil){for(i=0;i<n;i++)if(p[i][7]>.5f)break;if(i==n)return;}
 for(i=1;i+1<n;i++){
  int ids[3]={0,i,i+1};
  float cx=(p[i][1]-p[0][1])*(p[i+1][2]-p[0][2])-(p[i][2]-p[0][2])*(p[i+1][1]-p[0][1]),cy=(p[i][2]-p[0][2])*(p[i+1][0]-p[0][0])-(p[i][0]-p[0][0])*(p[i+1][2]-p[0][2]),
        cz=(p[i][0]-p[0][0])*(p[i+1][1]-p[0][1])-(p[i][1]-p[0][1])*(p[i+1][0]-p[0][0]);
  if(cx*cx+cy*cy+cz*cz<.000000000001f)continue;
  for(j=0;j<3;j++)td_copy(tri+j*TD_ATTR,p[ids[j]],TD_ATTR*4);
  if(s&&soil)soil->cb(tri,soil->user);else cb(tri,user);
 }
}
/* One half of a grid cell. The depth inside it is the flat interpolation of the three cell corners it owns,
   so the lowered surface is one fixed sheet: every mesh cut by it (the drawn ground, a second texture layer
   lying on it, the collision ground) lands on exactly the same heights whatever its own triangles were. */
static void td_half(float p[TD_POLY][TD_ATTR],int n,const TdScar*s,const float*f,const float*c,const float*box,const TdSoil*soil,TdEmit cb,void*user){
 float h[TD_POLY],sh[TD_POLY],cv[TD_POLY];int i;
 /* f: ux,uz,vx,vz,s0,t0,1/ds,1/dt   c: a0, b0, then base, d/da, d/db for depth, shade and cover */
 for(i=0;i<n;i++){
  float a=td_clamp((p[i][0]*f[0]+p[i][2]*f[1]-f[4])*f[6],0,1)-c[0],b=td_clamp((p[i][0]*f[2]+p[i][2]*f[3]-f[5])*f[7],0,1)-c[1];
  h[i]=c[2]+a*c[3]+b*c[4];sh[i]=c[5]+a*c[6]+b*c[7];cv[i]=c[8]+a*c[9]+b*c[10];
 }
 td_fan(p,n,s,h,sh,cv,box,soil,cb,user);
}
/* One triangle against one scar. Returns 0 when the scar leaves the triangle alone (nothing was emitted). */
static int td_remesh_one(const TdScar*s,const float*tri,const float*box,const TdSoil*soil,TdEmit cb,void*user){
 float rem[TD_POLY][TD_ATTR],p[TD_POLY][TD_ATTR],q[TD_POLY][TD_ATTR],bb[4],x0,x1,z0,z1,wsum=0;
 float ux=s->bx-s->ax,uz=s->bz-s->az,len=sqrtf(ux*ux+uz*uz),vx,vz,a0,c0,cuts[4],step_s,step_t,smin,smax,tmin,tmax;
 int i,j,k,n=3,ns,nt,i0,i1,j0,j1;
 x0=td_min(tri[0],td_min(tri[12],tri[24]));x1=td_max(tri[0],td_max(tri[12],tri[24]));z0=td_min(tri[2],td_min(tri[14],tri[26]));z1=td_max(tri[2],td_max(tri[14],tri[26]));
 td_bounds(s,bb);if(x0>=bb[2]||x1<=bb[0]||z0>=bb[3]||z1<=bb[1])return 0;
 for(i=0;i<3;i++){const float*v=tri+i*TD_ATTR;if(!td_finite(v[0])||!td_finite(v[1])||!td_finite(v[2]))return 0;wsum+=td_weight(s,v[0],v[1],v[2]);}
 if(wsum<=0){     /* all three corners out of the height window: only untouched if they are on the same side of it */
  float d0=tri[1]-(s->ay+s->sx*(tri[0]-s->ax)+s->sz*(tri[2]-s->az)),d1=tri[13]-(s->ay+s->sx*(tri[12]-s->ax)+s->sz*(tri[14]-s->az)),d2=tri[25]-(s->ay+s->sx*(tri[24]-s->ax)+s->sz*(tri[26]-s->az));
  if((d0<0&&d1<0&&d2<0)||(d0>0&&d1>0&&d2>0))return 0;
 }
 if(len>0.03f){ux/=len;uz/=len;}else{ux=1;uz=0;len=0;}
 vx=-uz;vz=ux;a0=s->ax*ux+s->az*uz;c0=s->ax*vx+s->az*vz;
 cuts[0]=a0-s->r;cuts[1]=a0+len+s->r;cuts[2]=c0-s->r;cuts[3]=c0+s->r;
 td_copy(rem,tri,3*TD_ATTR*4);
 for(k=0;k<4&&n>=3;k++){     /* peel off what lies outside the scar's rectangle, unchanged */
  float lx=k<2?ux:vx,lz=k<2?uz:vz;int keep=(k&1)?-1:1,m=td_clip(p,rem,n,lx,lz,cuts[k],-keep);
  if(m>=3)td_fan(p,m,0,0,0,0,box,soil,cb,user);
  m=td_clip(q,rem,n,lx,lz,cuts[k],keep);td_copy(rem,q,m*TD_ATTR*4);n=m;
 }
 if(n<3)return 1;
 nt=(int)ceilf(s->r/4.5f);if(nt<8)nt=8;if(nt>TD_CELLS_ACROSS)nt=TD_CELLS_ACROSS;     /* cells about 9 units wide: 8 across a small bowl, 16 across a big one */
 ns=(int)ceilf((len+2*s->r)/(s->r*2/nt));if(ns<nt)ns=nt;if(ns>TD_CELLS_ALONG)ns=TD_CELLS_ALONG;
 step_s=(len+2*s->r)/ns;step_t=2*s->r/nt;
 smin=smax=rem[0][0]*ux+rem[0][2]*uz;tmin=tmax=rem[0][0]*vx+rem[0][2]*vz;
 for(i=1;i<n;i++){float a=rem[i][0]*ux+rem[i][2]*uz,c=rem[i][0]*vx+rem[i][2]*vz;smin=td_min(smin,a);smax=td_max(smax,a);tmin=td_min(tmin,c);tmax=td_max(tmax,c);}
 i0=(int)floorf((smin-cuts[0])/step_s);i1=(int)floorf((smax-cuts[0])/step_s);j0=(int)floorf((tmin-cuts[2])/step_t);j1=(int)floorf((tmax-cuts[2])/step_t);
 if(i0<0)i0=0;
 if(j0<0)j0=0;
 if(i1>ns-1)i1=ns-1;
 if(j1>nt-1)j1=nt-1;
 if(box){     /* a collision query keeps only pieces touching its box: skip grid cells beyond the box's extent (one spare cell each side) */
  float bs0=1e30f,bs1=-1e30f,bt0=1e30f,bt1=-1e30f;int e;
  for(e=0;e<4;e++){float x=box[(e&1)?2:0],z=box[(e&2)?3:1],a=x*ux+z*uz,c=x*vx+z*vz;bs0=td_min(bs0,a);bs1=td_max(bs1,a);bt0=td_min(bt0,c);bt1=td_max(bt1,c);}
  if(bs1<cuts[0]-step_s||bs0>cuts[1]+step_s||bt1<cuts[2]-step_t||bt0>cuts[3]+step_t)return 1;
  e=(int)floorf((bs0-cuts[0])/step_s)-1;if(e>i0)i0=e;e=(int)floorf((bs1-cuts[0])/step_s)+1;if(e<i1)i1=e;
  e=(int)floorf((bt0-cuts[2])/step_t)-1;if(e>j0)j0=e;e=(int)floorf((bt1-cuts[2])/step_t)+1;if(e<j1)j1=e;
  if(i0>i1||j0>j1)return 1;
 }
 for(i=i0;i<=i1;i++){
  int mi;float row[TD_POLY][TD_ATTR],g[2][3][TD_CELLS_ACROSS+1];     /* [line s0 / s1][depth, shade, cover][j] */
  float s0=cuts[0]+step_s*i,s1=i==ns-1?cuts[1]:cuts[0]+step_s*(i+1);
  mi=td_clip(q,rem,n,ux,uz,s0,1);mi=td_clip(row,q,mi,ux,uz,s1,-1);
  if(mi<3)continue;
  for(j=j0;j<=j1+1;j++){
   float tt=j==nt?cuts[3]:cuts[2]+step_t*j;
   g[0][0][j]=td_profile(s,ux*s0+vx*tt,uz*s0+vz*tt,&g[0][1][j],&g[0][2][j]);g[1][0][j]=td_profile(s,ux*s1+vx*tt,uz*s1+vz*tt,&g[1][1][j],&g[1][2][j]);
  }
  for(j=j0;j<=j1;j++){
   float t0=cuts[2]+step_t*j,t1=j==nt-1?cuts[3]:cuts[2]+step_t*(j+1),f[8]={ux,uz,vx,vz,s0,t0,1/(s1-s0),1/(t1-t0)},c[11];
   int m=td_clip(q,row,mi,vx,vz,t0,1),k2,e;m=td_clip(p,q,m,vx,vz,t1,-1);
   if(m<3)continue;
   if(g[0][0][j]==0&&g[1][0][j]==0&&g[0][0][j+1]==0&&g[1][0][j+1]==0&&g[0][1][j]==1&&g[1][1][j]==1&&g[0][1][j+1]==1&&g[1][1][j+1]==1){td_fan(p,m,0,0,0,0,box,soil,cb,user);continue;}   /* corner of the rectangle outside the scar */
   /* split along the diagonal whose ends are closer in depth: rims come out rounder */
   if(fabsf(g[0][0][j]-g[1][0][j+1])<=fabsf(g[1][0][j]-g[0][0][j+1])){
    float nx=ux*f[6]-vx*f[7],nz=uz*f[6]-vz*f[7],cut=s0*f[6]-t0*f[7];
    k2=td_clip(q,p,m,nx,nz,cut,1);       /* a >= b: corners 00, 10, 11 */
    c[0]=0;c[1]=0;for(e=0;e<3;e++){c[2+e*3]=g[0][e][j];c[3+e*3]=g[1][e][j]-g[0][e][j];c[4+e*3]=g[1][e][j+1]-g[1][e][j];}
    if(k2>=3)td_half(q,k2,s,f,c,box,soil,cb,user);
    k2=td_clip(q,p,m,nx,nz,cut,-1);      /* a <= b: corners 00, 01, 11 */
    for(e=0;e<3;e++){c[3+e*3]=g[1][e][j+1]-g[0][e][j+1];c[4+e*3]=g[0][e][j+1]-g[0][e][j];}
    if(k2>=3)td_half(q,k2,s,f,c,box,soil,cb,user);
   }else{
    float nx=ux*f[6]+vx*f[7],nz=uz*f[6]+vz*f[7],cut=s0*f[6]+t0*f[7]+1;
    k2=td_clip(q,p,m,nx,nz,cut,-1);      /* a + b <= 1: corners 00, 10, 01 */
    c[0]=0;c[1]=0;for(e=0;e<3;e++){c[2+e*3]=g[0][e][j];c[3+e*3]=g[1][e][j]-g[0][e][j];c[4+e*3]=g[0][e][j+1]-g[0][e][j];}
    if(k2>=3)td_half(q,k2,s,f,c,box,soil,cb,user);
    k2=td_clip(q,p,m,nx,nz,cut,1);       /* a + b >= 1: corners 11, 01, 10 */
    c[0]=1;c[1]=1;for(e=0;e<3;e++){c[2+e*3]=g[1][e][j+1];c[3+e*3]=g[1][e][j+1]-g[0][e][j+1];c[4+e*3]=g[1][e][j+1]-g[1][e][j];}
    if(k2>=3)td_half(q,k2,s,f,c,box,soil,cb,user);
   }
  }
 }
 return 1;
}
/* Pieces a scar leaves unchanged go on to the later scars. Accepted scars never overlap, so a lowered piece
   is final. */
typedef struct {const TdState*t;TdEmit cb;void*user;const float*box;int start;const TdSoil*soil;} TdNext;
static void td_next_emit(const float*,void*);
static void td_remesh_from(const TdState*t,const float*v,int start,const float*box,const TdSoil*soil,TdEmit cb,void*user){
 int i;
 for(i=start;i<t->count;i++){TdNext next={t,cb,user,box,i+1,soil};if(td_remesh_one(&t->scars[i],v,box,soil,td_next_emit,&next))return;}
 if(!soil)cb(v,user);
}
static void td_next_emit(const float*v,void*u){TdNext*n=u;td_remesh_from(n->t,v,n->start,n->box,n->soil,n->cb,n->user);}
static int td_remesh_top(const TdState*t,const float*v,const float*box,const TdSoil*soil,TdEmit cb,void*user){
 int i;float b[4],x0,x1,z0,z1;
 if(!t||!t->enabled||!t->count)return 0;
 x0=td_min(v[0],td_min(v[12],v[24]));x1=td_max(v[0],td_max(v[12],v[24]));z0=td_min(v[2],td_min(v[14],v[26]));z1=td_max(v[2],td_max(v[14],v[26]));
 for(i=0;i<t->count;i++){
  const TdScar*s=&t->scars[i];td_bounds(s,b);
  if(x0<b[2]&&x1>b[0]&&z0<b[3]&&z1>b[1]){
   float lo=td_min(v[1],td_min(v[13],v[25])),hi=td_max(v[1],td_max(v[13],v[25])),slack=(fabsf(s->sx)+fabsf(s->sz))*(s->r+td_max(x1-x0,z1-z0))+1.2f*s->r+8;
   if(hi>=s->ay-slack&&lo<=s->ay+slack){
    /* when every scar declines, nothing was emitted and the caller keeps the original */
    int j;for(j=0;j<t->count;j++){TdNext next={t,cb,user,box,j+1,soil};if(td_remesh_one(&t->scars[j],v,box,soil,td_next_emit,&next))return 1;}
    return 0;
   }
  }
 }
 return 0;
}
/* Returns 0 when no scar touches the triangle: the caller then uses the original triangle. Otherwise the
   replacement triangles were emitted. box, when given, is {minX, minZ, maxX, maxZ}: pieces wholly outside it
   are skipped, which is all a collision query needs. */
static int TD_RemeshBox(const TdState*t,const float*v,const float*box,TdEmit cb,void*user){return td_remesh_top(t,v,box,0,cb,user);}
static int TD_Remesh(const TdState*t,const float*v,TdEmit cb,void*user){return td_remesh_top(t,v,0,0,cb,user);}
/* Soil pass for drawing only. Emits just the lowered pieces of the triangle, at the same positions TD_Remesh
   gives them, with: colour = rgb (the map's soil tint, 128 is neutral) times the scar shading, alpha = 128 at
   full cover fading to 0 across the rim, texture coordinates 0..1 across the scar. Draw them after the ground
   with the soil texture, alpha blending and a depth test that passes on equal depth. */
static int TD_Soil(const TdState*t,const float*v,const float*rgb,TdEmit cb,void*user){
 TdSoil soil={cb,user,{rgb[0],rgb[1],rgb[2]}};return td_remesh_top(t,v,0,&soil,0,0);
}
#endif
