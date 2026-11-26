#ifndef MOD_HUD_TRAILS_H
#define MOD_HUD_TRAILS_H
#include <string.h>
typedef struct {int ready,chara,costume,hp,maxHp,delta;float high,low,hold,notice;} HtState;
static void HT_Update(HtState*s,int chara,int costume,int hp,int maxHp,float dt){
 if(maxHp<=0){memset(s,0,sizeof(*s));return;}
 float value=(float)hp/maxHp;
 if(!s->ready||s->chara!=chara||s->costume!=costume||s->maxHp!=maxHp){memset(s,0,sizeof(*s));s->ready=1;s->chara=chara;s->costume=costume;s->hp=hp;s->maxHp=maxHp;s->high=s->low=value;return;}
 if(dt<0)dt=0;if(dt>.1f)dt=.1f;
 if(hp!=s->hp){int delta=hp-s->hp;{long long total=(s->notice>0&&((s->delta<0)==(delta<0)))?(long long)s->delta+delta:delta;if(total>2147483647LL)total=2147483647LL;if(total< -2147483647LL)total= -2147483647LL;s->delta=(int)total;}s->notice=.9f;s->hold=.35f;if(delta<0){if(s->high<(float)s->hp/maxHp)s->high=(float)s->hp/maxHp;s->low=value;}else{if(s->low>(float)s->hp/maxHp)s->low=(float)s->hp/maxHp;s->high=value;}s->hp=hp;}
 else if(s->hold>0)s->hold-=dt;
 else {float step=dt*.7f;if(s->high>value){s->high-=step;if(s->high<value)s->high=value;}if(s->low<value){s->low+=step;if(s->low>value)s->low=value;}}
 if(s->notice>0)s->notice-=dt;
}
#endif
