#ifndef TAG_TOURNAMENT_CORE_H
#define TAG_TOURNAMENT_CORE_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define TT_TEAMS 17
#define TT_MEMBERS 5
struct TTConfig {int players,team_size,type,difficulty,player_team[4];};
struct TTFighter {int character,costume;uint16_t items[8];};
struct TTTeam {int count,owner[TT_MEMBERS],human,eliminated;struct TTFighter fighter[TT_MEMBERS];};
struct TTState {
 int active,draft[4],draft_count,draft_pos,ready,match_armed,side_team[2],pad_route[4],member_pad[2][TT_MEMBERS],watch_pending,watch_answer;
 struct TTConfig config;struct TTTeam team[TT_TEAMS];
};
int TT_Begin(struct TTState*,const struct TTConfig*);
void TT_Cancel(struct TTState*);
int TT_DraftTeam(const struct TTState*);
int TT_DraftOwner(const struct TTState*,int member);
int TT_SetMember(struct TTState*,int team,int member,const struct TTFighter*);
int TT_CommitDraft(struct TTState*);
int TT_FillCpuTeams(struct TTState*,const struct TTFighter *valid_roster,int count,uint32_t seed);
int TT_PrepareMatch(struct TTState*,int native_left_team,int native_right_team,int native_swaps_sides);
int TT_RecordResult(struct TTState*,int native_winner_side);
int TT_AnswerWatch(struct TTState*,int watch);
#ifdef __cplusplus
}
#endif
#endif
