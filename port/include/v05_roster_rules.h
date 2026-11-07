/* v0.5 partner admission; no cost/timing/consumed-state policy changes. */
#ifndef V05_ROSTER_RULES_H
#define V05_ROSTER_RULES_H
static void V05GokoroRecipes(int chara,unsigned char *p){
 int k,j,found=0,slot=-1;
 if(!p)return;
 if(chara==3){
  if(p[0xB6]==255){
   p[0xB0]=1;p[0xB3]=2;p[0xB6]=247;p[0xB9]=23;
   for(k=0;k<4;k++)p[0xC2+k]=23;
   return;
  }
  for(k=0;k<4;k++)if(p[0xBA + k]==23)found=1;
  if(!found){
   for(k=0;k<4;k++)if(p[0xBA + k]==255){slot=k;break;}
   if(slot<0)for(k=1;k<4&&slot<0;k++)for(j=0;j<k;j++)if(p[0xBA + k]==p[0xBA + j]){slot=k;break;}
   if(slot>=0)p[0xBA + slot]=23;
  }
 }else if(chara==23){
  p[0xB0]=1;p[0xB3]=2;p[0xB6]=247;p[0xB9]=3;
  for(k=0;k<4;k++)p[0xC2+k]=3;
 }else if(chara==247){
  for(k=0;k<4;k++)p[0x98+k]=255;
  p[0xAC]=255;for(k=0;k<3;k++)p[0xB4+k]=255;
  for(k=0;k<12;k++)p[0xBA+k]=255;
 }
}
#endif
