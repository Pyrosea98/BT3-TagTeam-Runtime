#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "mod_select.h"
static void fill(MsView *v,int mode,int a,int total){int i;memset(v,0,sizeof(*v));v->mode=mode;v->a=a;v->total=total;for(i=0;i<total;i++){v->pick[i].chara=(i*17)%291;v->pick[i].costume=i%8;v->pick[i].team=MS_Side(v,i);v->pick[i].pad=i<4?i:-1;}}
int main(void){
 MsView v;int mode,a,b,i,j,seen[10],pairs=0;
 for(mode=1;mode<=3;mode++)for(a=1;a<=9;a++)for(b=1;b<=10-a;b++){
  fill(&v,mode,a,a+b);if(mode==1)pairs++;assert(MS_Valid(&v));memset(seen,0,sizeof(seen));
  for(i=0;i<v.total;i++){j=MS_RootSlot(&v,i);assert(j>=0&&j<v.total&&!seen[j]++);}
  assert(MS_RootSlot(&v,-1)==-1&&MS_RootSlot(&v,v.total)==-1);
  if(mode!=2){assert(MS_RootSlot(&v,0)==0&&MS_RootSlot(&v,1)==a);}
 }
 fill(&v,1,1,8);assert(MS_Valid(&v));v.total=11;assert(!MS_Valid(&v));
 fill(&v,1,5,10);v.pick[9].pad=0;assert(!MS_Valid(&v));
 fill(&v,1,5,10);v.pick[9].chara=-1;assert(!MS_Valid(&v));
 fill(&v,1,5,10);v.pick[9].team=0;assert(!MS_Valid(&v));
 fill(&v,1,5,10);v.pick[9].costume=8;assert(!MS_Valid(&v));
 fill(&v,1,5,10);v.mode=0;assert(!MS_Valid(&v));
 for(i=1;i<=291;i++)for(j=0;j<i;j++){assert(MS_Move(j,-MS_PAGE,i)>=0&&MS_Move(j,-MS_PAGE,i)<i);assert(MS_Move(j,MS_PAGE,i)>=0&&MS_Move(j,MS_PAGE,i)<i);}
 assert(pairs==45);
 fill(&v,1,3,7);assert(MS_Resize(&v,0,-1)&&v.a==2&&v.total-v.a==4);
 assert(MS_Resize(&v,1,1)&&v.a==2&&v.total-v.a==5);
 fill(&v,1,1,2);assert(!MS_Resize(&v,0,-1)&&!MS_Resize(&v,1,-1));
 fill(&v,1,3,10);assert(!MS_Resize(&v,0,1)&&!MS_Resize(&v,1,1));
 assert(MS_Size(0)==1&&MS_Size(99)==9);
 puts("PASS all 45 team-size pairs and FFA actor mappings, independent resizing, caps, owner uniqueness and roster paging");
 return 0;
}
