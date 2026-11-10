/* Circle sweep in relative XZ space. Supplies a solid core without changing hit volumes. */
#ifndef MOD_GIANT_SEPARATION_H
#define MOD_GIANT_SEPARATION_H
#include <math.h>
static int GS_Separate(float ax,float az,float bx,float bz,float r,float*outx,float*outz){
 float r2=r*r,a2=ax*ax+az*az,b2=bx*bx+bz*bz,dx=bx-ax,dz=bz-az,dd=dx*dx+dz*dz;
 if(!(r>0)||!isfinite(r)||!isfinite(a2)||!isfinite(b2))return 0;
 if(a2>=r2&&dd>1e-8f){
  float dot=ax*dx+az*dz,disc=dot*dot-dd*(a2-r2);
  if(dot<0&&disc>=0){float t=(-dot-sqrtf(disc))/dd;
   if(t>=0&&t<=1){float x=ax+dx*t,z=az+dz*t,nx=x/r,nz=z/r;
    float rx=dx*(1-t),rz=dz*(1-t),into=rx*nx+rz*nz;
    if(into<0){rx-=into*nx;rz-=into*nz;}
    *outx=x+rx;*outz=z+rz;return 1;
   }
  }
 }
 if(b2>=r2)return 0;
 if(b2>1e-8f){float f=r/sqrtf(b2);*outx=bx*f;*outz=bz*f;}
 else if(a2>1e-8f){float f=r/sqrtf(a2);*outx=ax*f;*outz=az*f;}
 else {*outx=r;*outz=0;}
 return 1;
}
#endif
