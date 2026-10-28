#ifndef TAG_TOURNAMENT_PORT_H
#define TAG_TOURNAMENT_PORT_H
#include "tag_tournament_core.h"
/* Progress flags shared with menu_k.h; that legacy header has incompatible grid views. */
#define PORT_TT_PROGRESS_RUNNING 0x10
#define PORT_TT_PROGRESS_GREETED 0x20
#ifdef __cplusplus
extern "C" {
#endif
int Port_TagTourBegin(const struct TTConfig *config);
void Port_TagTourCancel(void);
int Port_TagTourActive(void);
int Port_TagTourConfig(int field);
int Port_TagTourDraftTeam(void);
void Port_TagTourDraftCursor(int member);
void Port_TagTourDraftStop(void);
int Port_TagTourDraftMember(int member,int character,int costume,const unsigned short *items);
int Port_TagTourCommit(void);
int Port_TagTourSeed(const struct TTFighter *roster,int count,unsigned seed);
int Port_TagTourTeam(int team,struct TTTeam *out);
int Port_TagTourMatch(int left,int right,int swap);
int Port_TagTourMemberPad(int side,int member);
int Port_TagTourPadRoute(int socket);
int Port_TagTourResult(int side);
int Port_TagTourViews(void);
void Port_TagTourHudState(int team,int member,int owner,int cpu_match);
void Port_ModMenuReturnFromTournament(void);
#ifdef __cplusplus
}
#endif
#endif
