#ifndef MOD_IMPORTED_MUZZLE_H
#define MOD_IMPORTED_MUZZLE_H
/* Native +Y is down. A muzzle at the feet/root is invalid only for the imported Oceanus pair.
   Keep a valid authored muzzle and every other bone/model untouched. */
static int MIM_Repair(int chara,int node,float rootY,const float *anchor,const float *left,const float *right,float *out){
 int i;float y;if((chara!=213&&chara!=245)||node!=0x36||!left||!right)return 0;
 for(i=0;i<3;i++)if(!(left[i]>-100000&&left[i]<100000&&right[i]>-100000&&right[i]<100000))return 0;
 y=(left[1]+right[1])*.5f;if(!(rootY-y>.5f)|| (anchor&&anchor[1]<(rootY+y)*.5f))return 0;
 for(i=0;i<3;i++)out[i]=(left[i]+right[i])*.5f;out[3]=1;return 1;
}
#endif
