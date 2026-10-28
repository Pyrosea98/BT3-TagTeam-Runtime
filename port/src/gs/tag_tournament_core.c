/* Native state contract: no MIPS hooks, emulator clients or savestate hand-back. */
#include "../../include/tag_tournament_core.h"
#include <string.h>
static int valid_fighter(const struct TTFighter *f){return f&&f->character>=0&&f->character<291&&f->costume>=0&&f->costume<8;}
void TT_Cancel(struct TTState *s){if(s)memset(s,0,sizeof(*s));}
int TT_Begin(struct TTState *s,const struct TTConfig *c){
 int i,j,counts[4]={0};struct TTState n;
 if(!s||!c||c->players<0||c->players>4||c->team_size<1||c->team_size>5||c->type<0||c->type>4||c->difficulty<0||c->difficulty>2)return 0;
 for(i=0;i<c->players;i++){if(c->player_team[i]<0||c->player_team[i]>3)return 0;if(++counts[c->player_team[i]]>c->team_size)return 0;}
 memset(&n,0,sizeof(n));n.active=1;n.config=*c;
 for(i=0;i<TT_TEAMS;i++){n.team[i].count=c->team_size;for(j=0;j<TT_MEMBERS;j++){n.team[i].owner[j]=-1;n.team[i].fighter[j].character=-1;}}
 for(i=0;i<4;i++)n.pad_route[i]=-1;
 for(i=0;i<4;i++)if(counts[i]){
  int slot=0;n.draft[n.draft_count++]=i;n.team[i].human=1;
  for(j=0;j<c->players;j++)if(c->player_team[j]==i)n.team[i].owner[slot++]=j;
 }
 if(!n.draft_count)n.draft[n.draft_count++]=0; /* CPU-only: draft the team to follow. */
 *s=n;return 1;
}
int TT_DraftTeam(const struct TTState *s){return s&&s->active&&s->draft_pos<s->draft_count?s->draft[s->draft_pos]:-1;}
int TT_DraftOwner(const struct TTState *s,int member){int t=TT_DraftTeam(s);if(t<0||member<0||member>=s->config.team_size)return -1;return s->team[t].owner[member]>=0?s->team[t].owner[member]:s->team[t].owner[0]>=0?s->team[t].owner[0]:0;}
int TT_SetMember(struct TTState *s,int t,int m,const struct TTFighter *f){
 if(!s||!s->active||t<0||t>=TT_TEAMS||m<0||m>=s->config.team_size||!valid_fighter(f))return 0;
 s->team[t].fighter[m]=*f;return 1;
}
int TT_CommitDraft(struct TTState *s){int t=TT_DraftTeam(s),i;if(t<0)return 0;for(i=0;i<s->config.team_size;i++)if(!valid_fighter(&s->team[t].fighter[i]))return 0;s->draft_pos++;return 1;}
static uint32_t random_next(uint32_t *seed){uint32_t x=*seed?*seed:1;x^=x<<13;x^=x>>17;x^=x<<5;return *seed=x;}
int TT_FillCpuTeams(struct TTState *s,const struct TTFighter *roster,int count,uint32_t seed){
 int t,m,i;
 if(!s||!s->active||s->draft_pos!=s->draft_count||!roster||count<s->config.team_size)return 0;
 for(i=0;i<count;i++)if(!valid_fighter(&roster[i]))return 0;
 for(t=0;t<TT_TEAMS;t++)for(m=0;m<s->config.team_size;m++)if(!valid_fighter(&s->team[t].fighter[m])){
  int start=(int)(random_next(&seed)%(unsigned)count),chosen=-1,k;
  for(k=0;k<count;k++){int candidate=(start+k)%count,duplicate=0;for(i=0;i<m;i++)if(s->team[t].fighter[i].character==roster[candidate].character)duplicate=1;if(!duplicate){chosen=candidate;break;}}
  if(chosen<0)return 0;
  s->team[t].fighter[m]=roster[chosen];
 }
 s->ready=1;return 1;
}
int TT_PrepareMatch(struct TTState *s,int left,int right,int swap){
 int side,m;
 if(!s||!s->active||!s->ready||left<0||right<0||left>=TT_TEAMS||right>=TT_TEAMS||left==right||s->team[left].eliminated||s->team[right].eliminated)return 0;
 s->side_team[0]=swap?right:left;s->side_team[1]=swap?left:right;
 for(side=0;side<4;side++)s->pad_route[side]=-1;
 for(side=0;side<2;side++)for(m=0;m<TT_MEMBERS;m++)s->member_pad[side][m]=-1;
 for(side=0;side<2;side++){
  struct TTTeam *team=&s->team[s->side_team[side]];
  if(team->owner[0]>=0){s->pad_route[side]=team->owner[0];s->member_pad[side][0]=side;}
 }
 for(side=0;side<2;side++){
  struct TTTeam *team=&s->team[s->side_team[side]];
  for(m=1;m<team->count;m++)if(team->owner[m]>=0){int pad;for(pad=0;pad<4;pad++)if(s->pad_route[pad]<0)break;if(pad==4)return 0;s->pad_route[pad]=team->owner[m];s->member_pad[side][m]=pad;}
 }
 s->watch_pending=!s->team[left].human&&!s->team[right].human;s->watch_answer=0;s->match_armed=1;return 1;
}
int TT_AnswerWatch(struct TTState *s,int watch){if(!s||!s->watch_pending)return 0;s->watch_answer=watch?1:2;s->watch_pending=0;return 1;}
int TT_RecordResult(struct TTState *s,int side){int winner;if(!s||!s->active||!s->ready||!s->match_armed||side<0||side>1||s->watch_pending)return -1;winner=s->side_team[side];s->team[s->side_team[1-side]].eliminated=1;s->match_armed=0;return winner;}
