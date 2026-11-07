#include <assert.h>
#include <math.h>
#include <stdio.h>
#include "mod_preview_frame.h"
int main(void){float ll[3]={-3,-15,-2},hh[3]={3,0,2},dd,yy;assert(MPF_FitBody(ll,hh,1,&dd,&yy)&&dd>20&&dd<100);float m[16]={0},rel[3]={0},ext[3]={1,2,1};m[5]=1;m[13]=-1;assert(MPF_FootBottom(m,rel,ext)==1);float d,y,lo[3]={-1,0,-1},hi[3]={1,2,1};assert(!MPF_Fit(lo,hi,1,6,&d,&y));
float giantHi[3]={8,16,5},giantLo[3]={-8,0,-5};for(int i=0;i<3;i++){float wide=i==0?1:i==1?1.333333f:1.75f;assert(MPF_Fit(giantLo,giantHi,wide,6,&d,&y));assert(d>38&&y>8&&y<10);}assert(!MPF_Fit(giantLo,giantHi,1,60,&d,&y));
hi[1]=NAN;assert(!MPF_Fit(lo,hi,1,6,&d,&y));hi[1]=-5;assert(!MPF_Fit(lo,hi,1,6,&d,&y));assert(MPF_GroundLift(-1,-0.5f,-20,1)==0);assert(MPF_GroundLift(-8,-6,-20,-5)==0);assert(MPF_GroundLift(3,2,-15,5)<-3);assert(MPF_GroundLift(NAN,2,-15,5)==0);assert(MPF_GroundLift(100,90,-15,5)==0);puts("PASS foot penetration corrected; planted/hovering/invalid foot positions preserved; oversized models fit at all aspects; normal/authored ape cameras preserved; malformed bounds rejected");return 0;}
