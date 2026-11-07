
#define main previous_test_main
#include "test_chapter1.c"
#undef main
static void seed(int ranks){int i;memset(sStubGroups,0,sizeof(sStubGroups));sSlotFlags=3;memset(sRanks,0,sizeof(sRanks));
for(i=0;i<ST_COUNT;i++)if(stChapters[i].saga==0&&i!=11){int k=st_save_index(i);st_group_or(k/4,(ranks?3:ST_DONE_CANON)<<(2*(k%4)));sRanks[k]=ranks;}}
int main(){MsView v;unsigned a;int i;seed(3);sDifficulty=2;
assert(!(Port_StorySagaState(0)&4));assert(!(Port_StorySagaState(1)&2));
Port_StoryChoose(11);fight(&v);for(i=0;i<ST_EVENTS;i++)Port_StoryEventDone(i);
a=Port_StoryResult(0,1,1);assert((Port_StorySagaState(0)&4)&&(Port_StorySagaState(1)&2));assert(Port_StoryUnlockedSaga()==1);assert(Save_GetStoryBitsGroup(8)&16);assert((a&(MA_SAIYAN_EASY|MA_SAIYAN_NORMAL|MA_SAIYAN_HARD))==(MA_SAIYAN_EASY|MA_SAIYAN_NORMAL|MA_SAIYAN_HARD));
Port_StoryAcknowledge();assert(Port_StoryUnlockedSaga()==-1);Port_StoryChoose(11);fight(&v);for(i=0;i<ST_EVENTS;i++)Port_StoryEventDone(i);Port_StoryResult(0,1,1);assert(Port_StoryUnlockedSaga()==-1);
seed(0);sDifficulty=0;Port_StoryChoose(11);fight(&v);for(i=0;i<ST_EVENTS;i++)Port_StoryEventDone(i);a=Port_StoryResult(0,1,1);assert(Port_StoryUnlockedSaga()==1);assert(!(a&(MA_SAIYAN_EASY|MA_SAIYAN_NORMAL|MA_SAIYAN_HARD)));
puts("PASS actual saga completion: Namek unlock, first-only persisted receipt, repeat/ack reset, rank-0 narrative passes never award victory trophies; Hard wins award lower tiers");return 0;}
