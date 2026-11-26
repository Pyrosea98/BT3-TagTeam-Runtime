/* Original ground boundary skirts. The lower edge comes from TD_Remesh itself,
 * not the analytic profile: collision and rendering therefore share every seam. */
#ifndef MOD_TERRAIN_WALLS_H
#define MOD_TERRAIN_WALLS_H
#include <stdlib.h>
#include <string.h>
#include "mod_terrain_dynamic.h"
#define TW_INPUT_MAX 65536
#define TW_OUTPUT_MAX 16384
typedef struct {float p[9];} TwTri;
typedef struct {float p[6];} TwEdge;
typedef struct {float *v;int n,cap,failed;} TwMesh;
typedef struct {TwTri *tri;int n,cap,failed;} TwInput;
static int tw_point_cmp(const float*a,const float*b){int k;for(k=0;k<3;k++){if(a[k]<b[k])return -1;if(a[k]>b[k])return 1;}return 0;}
static int tw_tri_cmp(const void*a,const void*b){const TwTri*x=a,*y=b;int k;for(k=0;k<9;k++){if(x->p[k]<y->p[k])return -1;if(x->p[k]>y->p[k])return 1;}return 0;}
static int tw_edge_cmp(const void*a,const void*b){const TwEdge*x=a,*y=b;int k;for(k=0;k<6;k++){if(x->p[k]<y->p[k])return -1;if(x->p[k]>y->p[k])return 1;}return 0;}
static void TW_Clear(TwMesh*m){free(m->v);memset(m,0,sizeof(*m));}
static void TW_InputClear(TwInput*i){free(i->tri);memset(i,0,sizeof(*i));}
static void TW_Collect(TwInput*i,const TdState*t,const float*v){
 int j,k,hit=0;float x0=td_min(v[0],td_min(v[3],v[6])),x1=td_max(v[0],td_max(v[3],v[6])),z0=td_min(v[2],td_min(v[5],v[8])),z1=td_max(v[2],td_max(v[5],v[8]));TwTri q;
 if(i->failed)return;
 for(j=0;j<9;j++)if(!td_finite(v[j])){i->failed=1;return;}
 for(j=0;j<t->count;j++){float b[4];td_bounds(t->scars+j,b);if(x0<=b[2]+1&&x1>=b[0]-1&&z0<=b[3]+1&&z1>=b[1]-1){hit=1;break;}}
 if(!hit)return;
 if(i->n==i->cap){int cap=i->cap?i->cap*2:1024;void*p;if(cap>TW_INPUT_MAX)cap=TW_INPUT_MAX;if(cap==i->cap){i->failed=1;return;}p=realloc(i->tri,cap*sizeof(TwTri));if(!p){i->failed=1;return;}i->tri=p;i->cap=cap;}
 memcpy(q.p,v,36);for(j=0;j<2;j++)for(k=j+1;k<3;k++)if(tw_point_cmp(q.p+3*j,q.p+3*k)>0){float tmp[3];memcpy(tmp,q.p+3*j,12);memcpy(q.p+3*j,q.p+3*k,12);memcpy(q.p+3*k,tmp,12);}i->tri[i->n++]=q;
}
static void tw_store(TwMesh*m,const float*a,const float*b,const float*c){
 float*v;int j;float nx=(b[1]-a[1])*(c[2]-a[2])-(b[2]-a[2])*(c[1]-a[1]),ny=(b[2]-a[2])*(c[0]-a[0])-(b[0]-a[0])*(c[2]-a[2]),nz=(b[0]-a[0])*(c[1]-a[1])-(b[1]-a[1])*(c[0]-a[0]);
 if(m->failed||nx*nx+ny*ny+nz*nz<1e-10f)return;
 if(m->n==m->cap){int cap=m->cap?m->cap*2:256;void*p;if(cap>TW_OUTPUT_MAX)cap=TW_OUTPUT_MAX;if(cap==m->cap){m->failed=1;return;}p=realloc(m->v,cap*36*sizeof(float));if(!p){m->failed=1;return;}m->v=p;m->cap=cap;}
 v=m->v+36*m->n++;memset(v,0,144);for(j=0;j<3;j++){const float*p=j==0?a:j==1?b:c;memcpy(v+12*j,p,12);v[12*j+3]=1;v[12*j+4]=v[12*j+5]=v[12*j+6]=80;v[12*j+7]=128;v[12*j+8]=(p[0]+p[2])/40;v[12*j+9]=p[1]/40;v[12*j+11]=1;}
}
typedef struct {TwMesh*m;float a[3],d[3],len2;} TwSide;
static void tw_lower_edge(const float*v,void*user){
 TwSide*s=user;int j;for(j=0;j<3;j++){
 const float*a=v+12*j,*b=v+12*((j+1)%3);float ta=((a[0]-s->a[0])*s->d[0]+(a[2]-s->a[2])*s->d[2])/s->len2,tb=((b[0]-s->a[0])*s->d[0]+(b[2]-s->a[2])*s->d[2])/s->len2;
 float ca=(a[0]-s->a[0])*s->d[2]-(a[2]-s->a[2])*s->d[0],cb=(b[0]-s->a[0])*s->d[2]-(b[2]-s->a[2])*s->d[0],up[3],vp[3];int k;
 if(ca*ca>s->len2*1e-7f||cb*cb>s->len2*1e-7f||ta<-.0001f||ta>1.0001f||tb<-.0001f||tb>1.0001f||fabsf(ta-tb)<1e-6f)continue;
 for(k=0;k<3;k++){up[k]=s->a[k]+ta*s->d[k];vp[k]=s->a[k]+tb*s->d[k];}
 if(fabsf(up[1]-a[1])<.0001f&&fabsf(vp[1]-b[1])<.0001f)continue;
 tw_store(s->m,up,vp,b);tw_store(s->m,up,b,a);
 }
}
static void tw_skirt(TwMesh*m,const TdState*t,const float*a,const float*b){
 TwSide s;float v[36]={0},l;int k;s.m=m;memcpy(s.a,a,12);for(k=0;k<3;k++)s.d[k]=b[k]-a[k];s.len2=s.d[0]*s.d[0]+s.d[2]*s.d[2];if(s.len2<.000001f)return;l=sqrtf(s.len2);
 for(k=0;k<3;k++){v[k]=a[k];v[12+k]=b[k];v[24+k]=a[k];}v[24]-=s.d[2]/l;v[26]+=s.d[0]/l;v[3]=v[15]=v[27]=1;
 TD_Remesh(t,v,tw_lower_edge,&s);
}
/* Remove coincident triangles first: neighbouring zones repeat the same floor.
 * Exact internal edges cancel. Collinear partial edges are subtracted as intervals
 * too, so T-junctions do not become invisible internal barriers. */
static int TW_Build(TwInput*i,const TdState*t,TwMesh*out){
 TwEdge*e;int n=0,j,k,bn=0;float*cuts;TW_Clear(out);if(i->failed)return 0;if(!i->n)return 1;
 qsort(i->tri,i->n,sizeof(TwTri),tw_tri_cmp);e=malloc((size_t)i->n*3*sizeof(TwEdge));if(!e)return 0;
 for(j=0;j<i->n;j++){if(j&&!tw_tri_cmp(i->tri+j,i->tri+j-1))continue;for(k=0;k<3;k++){float*a=i->tri[j].p+3*k,*b=i->tri[j].p+3*((k+1)%3);if(tw_point_cmp(a,b)>0){float*p=a;a=b;b=p;}memcpy(e[n].p,a,12);memcpy(e[n++].p+3,b,12);}}
 qsort(e,n,sizeof(TwEdge),tw_edge_cmp);for(j=0;j<n;){k=j+1;while(k<n&&!tw_edge_cmp(e+j,e+k))k++;if(k-j==1)e[bn++]=e[j];j=k;}
 cuts=malloc((size_t)(2*bn+2)*sizeof(float));if(!cuts){free(e);return 0;}
 for(j=0;j<bn&&!out->failed;j++){
 float*a=e[j].p,*b=a+3,d[3],ll=0;int nc=2;for(k=0;k<3;k++){d[k]=b[k]-a[k];ll+=d[k]*d[k];}if(ll<1e-8f)continue;cuts[0]=0;cuts[1]=1;
 for(k=0;k<bn;k++)if(k!=j){float u[2],err=0;int q,h;for(q=0;q<2;q++){float v[3];u[q]=0;for(h=0;h<3;h++){v[h]=e[k].p[q*3+h]-a[h];u[q]+=v[h]*d[h];}u[q]/=ll;for(h=0;h<3;h++){float z=v[h]-u[q]*d[h];err+=z*z;}}if(err>.00001f)continue;if(u[0]>u[1]){float z=u[0];u[0]=u[1];u[1]=z;}if(u[1]<=0||u[0]>=1)continue;cuts[nc++]=td_clamp(u[0],0,1);cuts[nc++]=td_clamp(u[1],0,1);}
 /* Sort cuts; test the midpoint against every collinear covering edge. */
 for(k=1;k<nc;k++){float z=cuts[k];int q=k;while(q>0&&cuts[q-1]>z){cuts[q]=cuts[q-1];q--;}cuts[q]=z;}
 for(k=0;k+1<nc;k++)if(cuts[k+1]-cuts[k]>1e-6f){float mid=(cuts[k]+cuts[k+1])*.5f,p[3],q[3];int h,z,covered=0;for(h=0;h<3;h++)p[h]=a[h]+mid*d[h];
 for(z=0;z<bn&&!covered;z++)if(z!=j){float den=0,u=0,err=0;for(h=0;h<3;h++){float v=e[z].p[h+3]-e[z].p[h];den+=v*v;u+=(p[h]-e[z].p[h])*v;}if(den<1e-8f)continue;u/=den;if(u<-.00001f||u>1.00001f)continue;for(h=0;h<3;h++){float v=p[h]-(e[z].p[h]+u*(e[z].p[h+3]-e[z].p[h]));err+=v*v;}if(err<.00001f)covered=1;}
 if(!covered){for(h=0;h<3;h++){p[h]=a[h]+cuts[k]*d[h];q[h]=a[h]+cuts[k+1]*d[h];}tw_skirt(out,t,p,q);}}
 }
 free(cuts);free(e);if(out->failed){TW_Clear(out);return 0;}return 1;
}
#endif
