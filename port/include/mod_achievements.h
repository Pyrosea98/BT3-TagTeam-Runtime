#ifndef MOD_ACHIEVEMENTS_H
#define MOD_ACHIEVEMENTS_H
#ifdef __cplusplus
extern "C" {
#endif
/* Stable bit IDs: never reorder or reuse a shipped achievement. */
#define MA_DRAGONS 1u
#define MA_SSJ4_GOKU 2u
#define MA_HISTORY_EASY (1u<<20)
#define MA_HISTORY_NORMAL (1u<<21)
#define MA_HISTORY_HARD (1u<<22)
/* Saiyan saga (Codex/Juan 2026-11-05): a real player WIN of every required Saiyan combat chapter at Easy+/Normal+/Hard. */
#define MA_SAIYAN_EASY (1u<<23)
#define MA_SAIYAN_NORMAL (1u<<24)
#define MA_SAIYAN_HARD (1u<<25)
#define MA_COUNT 26
#define MA_MASK ((1u<<MA_COUNT)-1u)
enum {MA_BRONZE,MA_SILVER,MA_GOLD,MA_PLATINUM,MA_RANK_COUNT};
int Port_AchievementRank(int index);
/* Pure rule evaluator; starting teams, final solo form, real result. */
unsigned Port_AchievementEvaluate(const int *ids,const int *teams,const int *pads,int count,int winner,int finalForm,int flags,int reason,int eligible);
void Port_AchievementSubmit(unsigned bits);
/* Render-thread only: load, drain result events, persist. Returns newly unlocked bits. */
unsigned Port_AchievementUpdate(void);
unsigned Port_AchievementMask(void);
int Port_AchievementSaveOk(void);
#ifdef __cplusplus
}
#endif
#endif
