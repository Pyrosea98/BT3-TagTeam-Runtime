
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
 s={MM_PLAY,5,0};assert(mm_step(&s,0x200,0,&p,&sound)==0&&s.page==MM_SCENARIOS);
 mm_step(&s,0x200,0,&p,&sound);assert(s.page==MM_CHALLENGES);
 assert(mm_step(&s,0x200,0,&p,&sound)==40&&p==1);
 mm_step(&s,0x100,0,&p,&sound);assert(s.page==MM_SCENARIOS);
 mm_step(&s,0x100,0,&p,&sound);assert(s.page==MM_PLAY&&s.cursor==5);
 for(int f=0;f<6;f++){s={MM_SCENARIOS,f,0};int oldp=p;assert(mm_step(&s,0,2,&p,&sound)==0&&s.cursor==(f+1)%6&&p==oldp);mm_step(&s,0,1,&p,&sound);assert(s.cursor==f);assert(mm_step(&s,0x200,0,&p,&sound)==0&&s.page==MM_CHALLENGES&&s.pick==f);}
 // The 3x3 Play grid reaches every mode; the new pages do not alias fight launches.
 assert(mm_count(MM_PLAY)==9&&mm_columns(MM_PLAY)==3);
 s={MM_PLAY,0,0};mm_step(&s,0,4,&p,&sound);assert(s.cursor==3);
 mm_step(&s,0,4,&p,&sound);assert(s.cursor==6);
 mm_step(&s,0,2,&p,&sound);assert(s.cursor==7);
 assert(mm_step(&s,0x200,0,&p,&sound)==0&&s.page==MM_STORY_SAGAS);
 for(int g=1;g<8;g++){mm_step(&s,0,2,&p,&sound);assert(s.cursor==g);assert(mm_step(&s,0x200,0,&p,&sound)==0&&s.page==MM_STORY_SAGAS&&sound==2);}
 mm_step(&s,0,2,&p,&sound);assert(s.cursor==0);
 assert(mm_step(&s,0x200,0,&p,&sound)==0&&s.page==MM_STORY);
 assert(mm_step(&s,0x200,0,&p,&sound)==41&&p==1);
 mm_step(&s,0x100,0,&p,&sound);assert(s.page==MM_STORY_SAGAS);mm_step(&s,0x100,0,&p,&sound);assert(s.page==MM_PLAY&&s.cursor==7);
 mm_step(&s,0,2,&p,&sound);assert(s.cursor==8);
 p=4;assert(mm_step(&s,0x200,0,&p,&sound)==0&&s.page==MM_ACHIEVEMENTS&&p==4);
 mm_step(&s,0,8,&p,&sound);assert(s.cursor==MA_COUNT-1);
 mm_step(&s,0,4,&p,&sound);assert(s.cursor==0);
 assert(mm_step(&s,0x200,0,&p,&sound)==0&&s.page==MM_ACHIEVEMENTS&&p==4);
 mm_step(&s,0x100,0,&p,&sound);assert(s.page==MM_PLAY&&s.cursor==8);
 s={MM_ACHIEVEMENTS,0,0};assert(mm_step(&s,0x2000,0,&p,&sound)==10&&s.page==MM_CLOSED);
 for(int last=0;last<4;last++){mm_resume(&s,last);assert(s.page==MM_PLAY&&s.cursor==last);}
 mm_resume(&s,999);assert(s.page==MM_PLAY&&s.cursor==0);
 // Co-op training has the same team assignment, while normal training remains solo.
 s={MM_PLAY,6,0};mm_step(&s,0x200,0,&p,&sound);assert(s.page==MM_PLAYERS);mm_step(&s,0x200,0,&p,&sound);assert(s.page==MM_COOPSETUP);s.coop.cursor=2;assert(mm_step(&s,0x200,0,&p,&sound)==3&&p==2);
 for(int n=2;n<=4;n++)for(int mask=0;mask<(1<<n);mask++){CoopSetup c;CS_Init(&c,n);for(int k=0;k<n;k++)c.team[k]=(mask>>k)&1;int seen=0;for(int side=0;side<2;side++){int first=-1;for(int member=0;member<5;member++){int pad=CS_Member(&c,side,member);if(pad>=0){assert(c.team[pad]==side);assert(!(seen&(1<<pad)));seen|=1<<pad;if(first<0)first=pad;}}assert(CS_Leader(&c,side)==first);}assert(seen==(1<<n)-1);}
 // Long repeated directional input stays within the selected page.
 for(int page=1;page<=5;page++)for(unsigned d=1;d<=8;d*=2){s={page,0,1};for(int n=0;n<100;n++){mm_step(&s,0,d,&p,&sound);assert(s.cursor>=0&&s.cursor<(page==MM_PLAYERS?3:mm_count(page)));}}
 s={MM_STORY,6,1};assert(mm_step(&s,0x800,0,&p,&sound)==44&&s.cursor==6&&s.pick==1);
 s={MM_STORY_RESULT,6,1};assert(mm_step(&s,0x800,0,&p,&sound)==45);assert(mm_step(&s,0x1000,0,&p,&sound)==46);

}
