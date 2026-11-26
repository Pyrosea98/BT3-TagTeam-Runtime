/* Test authored object collision against the actual scar footprint and vertical
 * excavation volume. +Y down. No renderer pointers, game RNG or per-frame scan. */
#ifndef MOD_TERRAIN_STRUCTURE_H
#define MOD_TERRAIN_STRUCTURE_H
#include "mod_terrain_dynamic.h"
static float ts_plane(const TdScar*s,const float*v){return v[1]-s->ay-s->sx*(v[0]-s->ax)-s->sz*(v[2]-s->az);}
static int ts_clip(float out[8][3],float in[8][3],int n,const TdScar*s,int lower){int i,k,m=0;for(i=0;i<n;i++){float*a=in[i],*b=in[(i+1)%n],da=lower?s->depth-ts_plane(s,a):ts_plane(s,a)+.14f*s->depth,db=lower?s->depth-ts_plane(s,b):ts_plane(s,b)+.14f*s->depth;int ia=da>=-.0001f,ib=db>=-.0001f;if(ia)td_copy(out[m++],a,12);if(ia!=ib){float t=td_clamp(da/(da-db),0,1);for(k=0;k<3;k++)out[m][k]=a[k]+t*(b[k]-a[k]);m++;}}return m;}
static int ts_inside(float p[8][3],int n,float x,float z){int i,pos=0,neg=0;float area=0;for(i=0;i<n;i++){float*a=p[i],*b=p[(i+1)%n],q=(b[0]-a[0])*(z-a[2])-(b[2]-a[2])*(x-a[0]);if(q>.0001f)pos=1;if(q<-.0001f)neg=1;area+=a[0]*b[2]-b[0]*a[2];}return fabsf(area)>.0001f&&!(pos&&neg);}
static int TS_Triangle(const TdState*t,const float*v){int i,j;if(!t||!t->enabled)return 0;for(i=0;i<t->count;i++){const TdScar*s=t->scars+i;float p[8][3],q[8][3],bounds[4];int n;td_bounds(s,bounds);if(td_min(v[0],td_min(v[3],v[6]))>bounds[2]||td_max(v[0],td_max(v[3],v[6]))<bounds[0]||td_min(v[2],td_min(v[5],v[8]))>bounds[3]||td_max(v[2],td_max(v[5],v[8]))<bounds[1])continue;td_copy(p,v,36);n=ts_clip(q,p,3,s,0);if(n<1)continue;n=ts_clip(p,q,n,s,1);if(n<1)continue;
 for(j=0;j<n;j++){TdScar edge={0};float*a=p[j],*b=p[(j+1)%n];if(td_dist2(s,a[0],a[2])<=s->r*s->r+.0001f)return 1;edge.ax=a[0];edge.az=a[2];edge.bx=b[0];edge.bz=b[2];if(td_spine_dist(s,&edge)<=s->r+.0001f)return 1;}
 if(ts_inside(p,n,s->ax,s->az)||ts_inside(p,n,s->bx,s->bz))return 1;
 }return 0;}
/* Visual-only destructibles have no collision triangles. Their authored sphere
 * is the conservative fallback, only for objects without a collision record. */
static int TS_Bounds(const TdState*t,float x,float y,float z,float radius){int i;if(!t||!t->enabled||!isfinite(radius)||radius<0||radius>100000)return 0;for(i=0;i<t->count;i++){const TdScar*s=t->scars+i;float point[3]={x,y,z},dy=ts_plane(s,point),slack=radius*(1+fabsf(s->sx)+fabsf(s->sz));if(dy+slack<-.14f*s->depth||dy-slack>s->depth)continue;if(td_dist2(s,x,z)<=(s->r+radius)*(s->r+radius))return 1;}return 0;}
#endif
