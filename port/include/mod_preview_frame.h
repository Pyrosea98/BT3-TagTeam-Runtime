/* Pure menu-only framing. Invalid/normal/authored giant views retain their camera. */
#ifndef MOD_PREVIEW_FRAME_H
#define MOD_PREVIEW_FRAME_H
static int MPF_Fit(const float *lo,const float *hi,float wide,float current,float *distance,float *height){
 float h,w,d,span,fit;int i;
 if(!(current>0&&current<100000))return 0;
 for(i=0;i<3;i++)if(!(lo[i]>-100000&&hi[i]<100000&&hi[i]>=lo[i]))return 0;
 h=hi[1]-lo[1];w=hi[0]-lo[0];d=hi[2]-lo[2];span=h>w?h:w;if(d>span)span=d;
 if(h<0.01f||span<=current*0.75f)return 0;
 if(!(wide>0.5f&&wide<4.0f))wide=1;
 w/=wide*0.7f;fit=(h>w?h:w)*2.4f+d*0.5f;
 if(!(fit>current&&fit<100000))return 0;
 *distance=fit;*height=(lo[1]+hi[1])*0.5f+h*0.07f;return 1;
}
static int MPF_FitBody(const float *lo,const float *hi,float wide,float *dist,float *height){
 float h,w,d;int i;for(i=0;i<3;i++)if(!(lo[i]>-100000&&hi[i]<100000&&hi[i]>=lo[i]))return 0;
 h=hi[1]-lo[1];w=hi[0]-lo[0];d=hi[2]-lo[2];if(h<1||h>10000)return 0;
 if(!(wide>.5f&&wide<4)){wide=1;}
 w/=wide*.7f;*dist=(h>w?h:w)*2.6f+d*.5f;
 *height=(lo[1]+hi[1])*.5f+h*.05f;return *dist>1&&*dist<100000;
}
static float MPF_Abs(float x){return x<0?-x:x;}
/* Same world-box projection as BtlObj_UpdateBounds, restricted to a foot. */
static float MPF_FootBottom(const float *m,const float *rel,const float *extent){
 return m[13]+rel[0]*m[1]+rel[1]*m[5]+rel[2]*m[9]
  +MPF_Abs(extent[0]*m[1])+MPF_Abs(extent[1]*m[5])+MPF_Abs(extent[2]*m[9]);
}
/* Native +Y is down. Correct penetration only; keep airborne poses and valid feet alone. */
static float MPF_GroundLift(float left,float right,float top,float bottom){
 float h=bottom-top,foot=left>right?left:right;
 if(!(h>0.01f&&h<100000&&left>-100000&&left<100000&&right>-100000&&right<100000))return 0;
 if(!(foot>=top-h*0.1f&&foot<=bottom+h*0.1f)||foot<=0.5f)return 0;
 return -foot-0.05f;
}
#endif
