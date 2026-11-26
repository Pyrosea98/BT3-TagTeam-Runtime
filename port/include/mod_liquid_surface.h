#ifndef MOD_LIQUID_SURFACE_H
#define MOD_LIQUID_SURFACE_H
#include <math.h>
#include <string.h>
/* Extend a verified horizontal native sheet using its own affine UV mapping.
   Keep the animated material/colour/pipeline; never lower the physics floor. */
static int LS_Quad(const float*v,unsigned count,float level,float x0,float z0,float x1,float z1,float*out){
 unsigned i,j,k;const float*a=0,*b=0,*c=0;float det=0;
 if(count<3||!isfinite(level))return 0;
 for(i=0;i<count;i++)if(!isfinite(v[i*12+1])||fabsf(v[i*12+1]-level)>.25f)return 0;
 level=v[1];a=v;
 for(i=1;i+1<count;i++){b=v+i*12;c=v+(i+1)*12;det=(b[0]-a[0])*(c[2]-a[2])-(c[0]-a[0])*(b[2]-a[2]);if(fabsf(det)>.01f)break;}
 if(i+1>=count)return 0;
 for(j=0;j<4;j++){
  float*x=out+j*12;float px=(j&1)?x1:x0,pz=(j&2)?z1:z0;
  float u=((px-a[0])*(c[2]-a[2])-(pz-a[2])*(c[0]-a[0]))/det;
  float w=((b[0]-a[0])*(pz-a[2])-(b[2]-a[2])*(px-a[0]))/det;
  memcpy(x,a,48);x[0]=px;x[1]=level;x[2]=pz;
  for(k=8;k<10;k++){x[k]=a[k]+u*(b[k]-a[k])+w*(c[k]-a[k]);if(!isfinite(x[k]))return 0;}
 }
 return 1;
}
static int LS_Expand(const float*v,unsigned count,float level,float*out){return LS_Quad(v,count,level,-20000,-20000,20000,20000,out);}
#endif
