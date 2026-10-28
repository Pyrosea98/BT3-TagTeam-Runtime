// Persistent native teams. Mutated only by the game thread, including pad reads.
#include "../../include/tag_tournament_port.h"
#include <string.h>
#include <stdio.h>
#include <atomic>
extern "C" int Port_NetActive(void);
static TTState state;
static std::atomic<int> active{0},configView[5],views{0};
static int draftOwner=-1;
extern "C" int Port_TagTourBegin(const TTConfig *config){if(Port_NetActive())return 0;draftOwner=-1;int ok=TT_Begin(&state,config);if(ok){configView[0].store(config->players);configView[1].store(config->team_size);configView[2].store(config->type);configView[3].store(config->difficulty);configView[4].store(state.draft_count);active.store(1,std::memory_order_release);views.store(0);}if(ok)fprintf(stderr,"bt3: tag tournament begin: players=%d team=%d type=%d level=%d\n",config->players,config->team_size,config->type,config->difficulty);return ok;}
extern "C" void Port_TagTourCancel(void){active.store(0,std::memory_order_release);views.store(0);TT_Cancel(&state);draftOwner=-1;}
extern "C" int Port_TagTourActive(void){return active.load(std::memory_order_acquire);}
extern "C" int Port_TagTourConfig(int f){return active.load(std::memory_order_acquire)&&f>=0&&f<5?configView[f].load():-1;}
extern "C" int Port_TagTourDraftTeam(void){return TT_DraftTeam(&state);}
extern "C" void Port_TagTourDraftCursor(int member){draftOwner=TT_DraftOwner(&state,member<state.config.team_size?member:0);Port_TagTourHudState(TT_DraftTeam(&state),member,draftOwner,0);}
extern "C" void Port_TagTourDraftStop(void){draftOwner=-1;}
extern "C" int Port_TagTourDraftMember(int member,int character,int costume,const unsigned short *items){TTFighter f={};f.character=character;f.costume=costume;if(items)memcpy(f.items,items,sizeof(f.items));return TT_SetMember(&state,TT_DraftTeam(&state),member,&f);}
extern "C" int Port_TagTourCommit(void){draftOwner=-1;return TT_CommitDraft(&state);}
extern "C" int Port_TagTourSeed(const TTFighter *roster,int count,unsigned seed){return TT_FillCpuTeams(&state,roster,count,seed);}
extern "C" int Port_TagTourTeam(int team,TTTeam *out){if(!state.active||team<0||team>=TT_TEAMS||!out)return 0;*out=state.team[team];return 1;}
extern "C" int Port_TagTourMatch(int left,int right,int swap){int ok=TT_PrepareMatch(&state,left,right,swap);if(ok){if(state.watch_pending)TT_AnswerWatch(&state,1);int n=0;for(int p=0;p<4;p++)if(state.pad_route[p]>=0)n++;views.store(n,std::memory_order_release);}return ok;}
extern "C" int Port_TagTourMemberPad(int side,int member){return state.active&&state.match_armed&&side>=0&&side<2&&member>=0&&member<TT_MEMBERS?state.member_pad[side][member]:-1;}
extern "C" int Port_TagTourPadRoute(int socket){if(!state.active||socket<0||socket>=4)return socket;if(draftOwner>=0)return socket==0?draftOwner:-1;if(state.match_armed){if(Port_TagTourViews()==0)return socket==0?0:-1;return state.pad_route[socket];}return socket;}
extern "C" int Port_TagTourResult(int side){int winner=TT_RecordResult(&state,side);if(winner>=0)views.store(0,std::memory_order_release);if(winner>=0)fprintf(stderr,"bt3: tag tournament result: side=%d team=%d\n",side,winner);return winner;}

extern "C" int Port_TagTourViews(void){return views.load(std::memory_order_acquire);}
