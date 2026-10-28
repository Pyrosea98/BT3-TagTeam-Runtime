
#include <assert.h>
#include "../../src/gs/mod_menu_model.h"
int main(){
 ModMenuState s={0,0,0};int p=0,sound=0;
 assert(mm_step(&s,0x2000,0,&p,&sound)==0&&s.page==MM_ROOT&&sound==1);
 assert(mm_step(&s,0,2,&p,&sound)==0&&s.cursor==1); // right
 assert(mm_step(&s,0x200,0,&p,&sound)==0&&s.page==MM_SETTINGS);
 s.cursor=4;assert(mm_step(&s,0x200,0,&p,&sound)==104); // actual Controls tab
 for(int i=0;i<12;i++){s.cursor=i;assert(mm_step(&s,0x200,0,&p,&sound)==100+i);}
 assert(mm_step(&s,0x400,0,&p,&sound)==0&&s.page==MM_ROOT);
 assert(mm_step(&s,0x200,0,&p,&sound)==0&&s.page==MM_PLAY);
 assert(mm_step(&s,0x200,0,&p,&sound)==1&&p==1); // solo team battle
 s={MM_PLAY,1,0};mm_step(&s,0x200,0,&p,&sound);assert(s.page==MM_PLAYERS);
 for(int i=0;i<3;i++){s={MM_PLAYERS,i,1};assert(mm_step(&s,0x200,0,&p,&sound)==0&&s.page==MM_COOPSETUP&&s.coop.players==i+2);s.coop.cursor=i+2;assert(mm_step(&s,0x200,0,&p,&sound)==1&&p==i+2);}
 s={MM_PLAY,2,0};mm_step(&s,0x200,0,&p,&sound);
 for(int i=0;i<4;i++){s.cursor=i;assert(mm_step(&s,0x200,0,&p,&sound)==2&&p==i+1);}
 s={MM_PLAY,3,0};assert(mm_step(&s,0x200,0,&p,&sound)==3&&p==1);
 s={MM_ROOT,3,0};assert(mm_step(&s,0x200,0,&p,&sound)==10&&s.page==0);
 s={MM_PLAYERS,2,1};assert(mm_step(&s,0x2000,0,&p,&sound)==10&&s.page==0);
 s={MM_PLAY,4,0};assert(mm_step(&s,0x200,0,&p,&sound)==0&&s.page==MM_TOURSETUP);s.tour.cursor=8;assert(mm_step(&s,0x200,0,&p,&sound)==30);
 s={MM_PLAY,4,0};mm_step(&s,0x200,0,&p,&sound);assert(mm_step(&s,0x2000,0,&p,&sound)==10&&s.page==0);
 s={MM_PLAY,5,0};assert(mm_step(&s,0x200,0,&p,&sound)==0&&s.page==MM_PLAY&&s.cursor==5&&sound==2);
 for(int last=0;last<4;last++){mm_resume(&s,last);assert(s.page==MM_PLAY&&s.cursor==last);}
 mm_resume(&s,999);assert(s.page==MM_PLAY&&s.cursor==0);
 // Co-op training has the same team assignment, while normal training remains solo.
 s={MM_PLAY,6,0};mm_step(&s,0x200,0,&p,&sound);assert(s.page==MM_PLAYERS);mm_step(&s,0x200,0,&p,&sound);assert(s.page==MM_COOPSETUP);s.coop.cursor=2;assert(mm_step(&s,0x200,0,&p,&sound)==3&&p==2);
 for(int n=2;n<=4;n++)for(int mask=0;mask<(1<<n);mask++){CoopSetup c;CS_Init(&c,n);for(int k=0;k<n;k++)c.team[k]=(mask>>k)&1;int seen=0;for(int side=0;side<2;side++){int first=-1;for(int member=0;member<5;member++){int pad=CS_Member(&c,side,member);if(pad>=0){assert(c.team[pad]==side);assert(!(seen&(1<<pad)));seen|=1<<pad;if(first<0)first=pad;}}assert(CS_Leader(&c,side)==first);}assert(seen==(1<<n)-1);}
 // Long repeated directional input stays within the selected page.
 for(int page=1;page<=5;page++)for(unsigned d=1;d<=8;d*=2){s={page,0,1};for(int n=0;n<100;n++){mm_step(&s,0,d,&p,&sound);assert(s.cursor>=0&&s.cursor<(page==MM_PLAYERS?3:mm_count(page)));}}
}
