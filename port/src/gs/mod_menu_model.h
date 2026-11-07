/* Game-thread menu reducer. No rendering or live game pointers. */
#ifndef MOD_MENU_MODEL_H
#define MOD_MENU_MODEL_H
#include "../../include/tag_tournament_setup.h"
#include "../../include/coop_selection.h"
#include "../../include/mod_scenarios.h"
#include "../../include/mod_story.h"
#include "../../include/mod_achievements.h"
struct ModMenuState { int page, cursor, pick; struct TTSetup tour; struct CoopSetup coop; };
enum { MM_CLOSED, MM_ROOT, MM_PLAY, MM_PLAYERS, MM_SETTINGS, MM_CREDITS, MM_TOURSETUP, MM_COOPSETUP, MM_SCENARIOS, MM_CHALLENGES, MM_STORY, MM_ACHIEVEMENTS, MM_STORY_RESULT, MM_STORY_SAGAS };
static int mm_count(int page) {
 return page==MM_ROOT?4:page==MM_PLAY?9:page==MM_PLAYERS?3:page==MM_SETTINGS?12:page==MM_ACHIEVEMENTS?MA_COUNT:1;
}
static int mm_columns(int page){return page==MM_PLAY?3:2;}
/* result: 1..3 launch mode; 10 close; 20..29 settings; 30 native tournament.
   players: Team/Training 1, Co-op 2..4, FFA 1..4 (FFA single default here). */
static void mm_resume(struct ModMenuState *s,int pick){s->page=MM_PLAY;s->cursor=pick>=0&&pick<mm_count(MM_PLAY)?pick:0;s->pick=0;if(pick==7){s->page=MM_STORY;s->cursor=0;}}
static int mm_step(struct ModMenuState *s,unsigned pressed,unsigned repeat,int *players,int *sound) {
 int move=0; *sound=-1;
 if(s->page==MM_STORY_SAGAS){
  if(pressed&0x2000){s->page=MM_CLOSED;*sound=2;return 10;}
  if(pressed&(0x400|0x100)){s->page=MM_PLAY;s->cursor=7;*sound=2;return 0;}
  int delta=(repeat&(1|8))?-1:(repeat&(2|4))?1:0;
  if(delta){s->cursor=(s->cursor+delta+8)%8;*sound=0;}
  if(pressed&0x200){if(ST_SagaFirst(s->cursor)>=0){s->cursor=ST_SagaFirst(s->cursor);s->page=MM_STORY;*sound=1;}else *sound=2;}
  return 0;
 }

 if(s->page==MM_STORY_RESULT){
  if(pressed&0x800){*sound=1;return 45;} /* retry checkpoint */
  if(pressed&0x1000){*sound=1;return 46;} /* restart chapter */
  if(pressed&(0x200|0x400|0x100)){s->page=MM_STORY;*sound=1;return 42;} /* back on the same chapter (cursor) */
  return 0;
 }

 if(s->page==MM_ACHIEVEMENTS){
  if(pressed&0x2000){s->page=MM_CLOSED;*sound=2;return 10;}
  if(pressed&(0x400|0x100)){s->page=MM_PLAY;s->cursor=8;*sound=2;return 0;}
  int delta=(repeat&(8|1))?-1:(repeat&(4|2))?1:0;
  if(delta){s->cursor=(s->cursor+delta+MA_COUNT)%MA_COUNT;*sound=0;}
  if(pressed&0x200)*sound=1;
  return 0;
 }

 if(s->page==MM_SCENARIOS||s->page==MM_CHALLENGES){
  if(pressed&0x2000){s->page=0;*sound=2;return 10;}
  if(pressed&(0x400|0x100)){s->page=s->page==MM_CHALLENGES?MM_SCENARIOS:MM_PLAY;s->cursor=s->page==MM_PLAY?5:0;*sound=2;return 0;}
  if(s->page==MM_SCENARIOS){int delta=(repeat&(8|1))?-1:(repeat&(4|2))?1:0;if(delta){s->cursor=(s->cursor+delta+SC_FAMILIES)%SC_FAMILIES;*sound=0;}}else if(repeat&(8|4)){int n=SC_Count(s->pick);s->cursor=(s->cursor+(repeat&8?n-1:1))%n;*sound=0;}
  if(s->page==MM_CHALLENGES&&(repeat&3)){int variants=SC_First(s->pick)+s->cursor==16?2:3;s->tour.cursor=(s->tour.cursor+(repeat&2?1:variants-1))%variants;*sound=0;}
  if(pressed&0x200){*sound=1;if(s->page==MM_SCENARIOS){s->pick=s->cursor;s->page=MM_CHALLENGES;s->cursor=0;s->tour.cursor=0;}else {*players=1;return 40;}}
  return 0;
 }
 /* Dragon History chapter page (one chapter so far): Cross starts it (41), back returns to Play. */
 if(s->page==MM_STORY){
  if(pressed&0x800){*sound=0;return 44;} /* Full/Reduced story density */
  /* cursor = chapter (up/down), pick = difficulty (left/right) */
  if(repeat&3){s->pick=(s->pick+(repeat&2?1:2))%3;*sound=0;}
  if((repeat&12)&&ST_COUNT>1){s->cursor=ST_SagaNextChapter(s->cursor,repeat&4?1:-1);*sound=0;}
  if(pressed&0x2000){s->page=0;*sound=2;return 10;}
  if(pressed&(0x400|0x100)){s->page=MM_STORY_SAGAS;s->cursor=0;*sound=2;return 0;}
  if(pressed&0x200){*sound=1;*players=1;return 41;}
  return 0;
 }
 if(s->page==MM_COOPSETUP){
  if(pressed&0x2000){s->page=0;*sound=2;return 10;}
  int r=CS_Step(&s->coop,pressed,repeat);s->cursor=s->coop.cursor;
  if(r<0){s->page=MM_PLAYERS;s->cursor=0;*sound=2;return 0;}
  if(r>0){*players=s->coop.players;*sound=1;return s->pick==6?3:1;}
  if(repeat){*sound=0;} return 0;
 }
 if(s->page==MM_TOURSETUP){if(pressed&0x2000){s->page=0;*sound=2;return 10;}int old=s->tour.cursor;int r=TT_SetupStep(&s->tour,pressed,repeat);s->cursor=s->tour.cursor;
  if(r<0){s->page=MM_PLAY;s->cursor=4;*sound=2;return 0;}
  if(r>0){*sound=1;return 30;}
  if(old!=s->tour.cursor||repeat&3)*sound=0;
  if(s->tour.error)*sound=2;
  return 0;
 }
 if(!s->page) { if(pressed&0x2000){s->page=MM_ROOT;s->cursor=0;*sound=1;}return 0; }
 if(pressed&0x2000){s->page=0;*sound=2;return 10;}
 if(pressed&(0x400|0x100)) {
  if(s->page==MM_ROOT){s->page=0;*sound=2;return 10;}
  s->page=s->page==MM_PLAYERS?MM_PLAY:MM_ROOT;s->cursor=0;*sound=2;return 0;
 }
 if(repeat&8)move=-mm_columns(s->page);else if(repeat&4)move=mm_columns(s->page);else if(repeat&1)move=-1;else if(repeat&2)move=1;
 if(move){int n=s->page==MM_PLAYERS?((s->pick==1||s->pick==6)?3:4):mm_count(s->page);s->cursor=(s->cursor+move+n)%n;*sound=0;return 0;}
 if(!(pressed&0x200))return 0;
 *sound=1;
 if(s->page==MM_ROOT){
  if(s->cursor==3){s->page=0;*sound=2;return 10;}
  s->page=s->cursor==0?MM_PLAY:s->cursor==1?MM_SETTINGS:MM_CREDITS;s->cursor=0;
 } else if(s->page==MM_PLAY){
  if(s->cursor==5){s->page=MM_SCENARIOS;s->cursor=0;return 0;}
  if(s->cursor==7){s->page=MM_STORY_SAGAS;s->cursor=0;return 0;}
  if(s->cursor==8){s->page=MM_ACHIEVEMENTS;s->cursor=0;return 0;}
  if(s->cursor==4){TT_SetupInit(&s->tour);s->page=MM_TOURSETUP;s->cursor=0;return 0;}
  if(s->cursor==1||s->cursor==2||s->cursor==6){s->pick=s->cursor;s->page=MM_PLAYERS;s->cursor=0;return 0;}
  *players=1;return s->cursor==0?1:s->cursor==2?2:3;
 } else if(s->page==MM_PLAYERS){
  if(s->pick==1||s->pick==6){CS_Init(&s->coop,s->cursor+2);s->page=MM_COOPSETUP;s->cursor=0;return 0;}
  *players=s->cursor+1;return 2;
 }
 else if(s->page==MM_SETTINGS)return 100+s->cursor;
 else {s->page=MM_ROOT;s->cursor=2;*sound=2;}
 return 0;
}
#endif
