#include <cassert>
#include <cstring>
#include <cstdio>
#include "../../src/gs/mod_story_guide.h"
#include "mod_story.h"
#include "../../src/gs/mod_story_commentary.h"
int main(){
 for(int guide=0;guide<9;guide++)for(int lang=0;lang<2;lang++){
  StGuideTalk s={};const char*const*idle=stGuideIdle[guide][lang];
  const char*opening="A short greeting for the next adventure.";
  int frame=0,key=100+guide*2+lang;
  assert(STG_Advance(&s,0,frame++,key,0,opening,idle)&&s.line==-1);
  double end=s.until;assert(!STG_Advance(&s,end,frame++,key,0,opening,idle));
  assert(!STG_Advance(&s,end+13.9,frame++,key,0,opening,idle)&&s.line==-1);
  assert(STG_Advance(&s,end+14,frame++,key,0,opening,idle)&&s.line==0);
  end=s.until;assert(!STG_Advance(&s,end,frame++,key,0,opening,idle));
  assert(!STG_Advance(&s,end+17.9,frame++,key,0,opening,idle));
  assert(STG_Advance(&s,end+18,frame++,key,0,opening,idle)&&s.line==1);
  end=s.until;assert(!STG_Advance(&s,end,frame++,key,0,opening,idle));
  assert(STG_Advance(&s,end+18,frame++,key,0,opening,idle)&&s.line==2);
  end=s.until;STG_Advance(&s,end,frame++,key,0,opening,idle);
  assert(STG_Advance(&s,end+18,frame++,key,0,opening,idle)&&s.line==0);
  // Interaction returns to the chapter description, mouth closed; no greeting replay.
  double now=end+18.2;assert(!STG_Advance(&s,now,frame++,key,1,opening,idle)&&s.line==-1);
  assert(!STG_Advance(&s,now+13.9,frame++,key,1,opening,idle));
  // Leaving and reopening the SAME section always restarts the opening speech.
  frame+=10;assert(STG_Advance(&s,now+14,frame++,key,1,opening,idle)&&s.line==-1);
  // New section/guide/locale also starts fresh.
  assert(STG_Advance(&s,now+14.1,frame++,key+50,1,opening,idle)&&s.line==-1);
  // A long stalled frame starts only one new line, never queues a burst.
  assert(STG_Advance(&s,now+500,frame++,key+50,1,opening,idle)&&s.line==0&&s.nextLine==1);
  assert(s.until<=now+507);
  for(int i=0;i<3;i++)assert(idle[i]&&*idle[i]&&STG_Duration(idle[i])>=2.5&&STG_Duration(idle[i])<=7);
 }
 assert(STG_Duration("a")==STG_Duration("\303\241")); // codepoints, not UTF-8 bytes
 // Current chapter commentary is distinct, fully localized, and changing
 // chapters while the menu stays open starts its own greeting/mouth cycle.
 auto*a=STG_Chapter(stChapters[0].id);auto*b=STG_Chapter(stChapters[1].id);assert(a&&b&&a!=b);
 for(int lang=0;lang<2;lang++){
  for(int outcome=ST_NONE;outcome<=ST_FAIL;outcome++){
   const char*x=STG_ChapterOpening(a,lang,outcome),*y=STG_ChapterOpening(b,lang,outcome);
   assert(x&&*x&&y&&*y&&strcmp(x,y));
  }
  for(int n=0;n<STG_IDLE_LINES;n++)assert(a->idle[lang][n][0]&&b->idle[lang][n][0]&&strcmp(a->idle[lang][n],b->idle[lang][n]));
  StGuideTalk s={};assert(STG_Advance(&s,0,0,100,1,a->opening[lang],a->idle[lang]));
  STG_Advance(&s,10,1,100,1,a->opening[lang],a->idle[lang]);
  assert(STG_Advance(&s,11,2,200,1,b->opening[lang],b->idle[lang])&&s.line==-1);
  double end=s.until;assert(!STG_Advance(&s,end,3,200,1,b->opening[lang],b->idle[lang]));
  assert(STG_Advance(&s,end+14,4,200,1,b->opening[lang],b->idle[lang])&&s.line==0);
 }
 auto*c=STG_Chapter("saiyan_03_unlikely_alliance");assert(c);
 for(int lang=0;lang<2;lang++){
  const char*texts[4];for(int outcome=0;outcome<4;outcome++){texts[outcome]=STG_ChapterOpening(c,lang,outcome);assert(texts[outcome]&&*texts[outcome]);assert(strcmp(texts[outcome],stChapters[2].recap[lang]));for(int previous=0;previous<outcome;previous++)assert(strcmp(texts[outcome],texts[previous]));}
 }
 int count2=0,count3=0;bool krillin=false,piccolo=false;
 for(int n=0;n<stCasts[1].count;n++)if(!(stCasts[1].actor[n].flags&ST_ACTOR_SCENE)){count2++;krillin|=stCasts[1].actor[n].chara==25;}
 for(int n=0;n<stCasts[2].count;n++)if(!(stCasts[2].actor[n].flags&ST_ACTOR_SCENE)){count3++;piccolo|=stCasts[2].actor[n].chara==22;}
 assert(count2==3&&count3==3&&krillin&&piccolo);
 for(int ch=0;ch<ST_COUNT;ch++)if(stChapters[ch].saga==0){auto*entry=STG_Chapter(stChapters[ch].id);assert(entry);for(int l=0;l<2;l++){assert(entry->opening[l]&&entry->canon[l]&&entry->failure[l]);assert(strcmp(entry->opening[l],stChapters[ch].recap[l]));for(int n=0;n<STG_IDLE_LINES;n++)assert(entry->idle[l][n]&&*entry->idle[l][n]);}}
 assert(!STG_Chapter("future_chapter")&&!STG_ChapterOpening(nullptr,0,ST_NONE));
 puts("PASS guide speech: all9 guides/both languages; greeting,14s silence, rotating idle,18s silence, closed mouth at end, activity, same-page reopen, focus/locale reset, stalled frame, UTF-8 timing");
}
