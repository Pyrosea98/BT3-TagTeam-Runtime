#ifndef TAG_TOURNAMENT_SETUP_H
#define TAG_TOURNAMENT_SETUP_H
#include "tag_tournament_core.h"
struct TTSetup {struct TTConfig config;int cursor,error;};
static void TT_SetupInit(struct TTSetup *u){int i;u->config.players=1;u->config.team_size=3;u->config.type=0;u->config.difficulty=1;for(i=0;i<4;i++)u->config.player_team[i]=i;u->cursor=0;u->error=0;}
/* 1 confirmed; -1 cancelled; 0 still choosing. Original game button meanings. */
static int TT_SetupStep(struct TTSetup *u,unsigned pressed,unsigned repeat){
 int *value=0,limit=0,dir=(repeat&2)?1:(repeat&1)?-1:0;
 if(pressed&(0x400|0x100|0x2000))return -1;
 if(repeat&8){u->cursor=(u->cursor+8)%9;return 0;}
 if(repeat&4){u->cursor=(u->cursor+1)%9;return 0;}
 if(u->cursor==0){value=&u->config.players;limit=5;}
 else if(u->cursor==1){value=&u->config.team_size;limit=5;}
 else if(u->cursor==2){value=&u->config.type;limit=5;}
 else if(u->cursor==3){value=&u->config.difficulty;limit=3;}
 else if(u->cursor<8&&u->cursor-4<u->config.players){value=&u->config.player_team[u->cursor-4];limit=4;}
 if(value&&dir){int start=u->cursor==1?1:0;*value=(*value-start+dir+limit)%limit+start;u->error=0;}
 if((pressed&0x200)&&u->cursor==8){struct TTState check;if(TT_Begin(&check,&u->config))return 1;u->error=1;}
 return 0;
}
#endif
