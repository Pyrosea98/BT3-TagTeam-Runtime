#ifndef TAG_TOURNAMENT_VIEW_H
#define TAG_TOURNAMENT_VIEW_H
/* Immutable game-to-render snapshot; no native pointers or gameplay mutation. */
struct TTBracketView {
 int active,round,match,flags,win_side;
 int sequence;
 int team[17],entry_flags[17],count[17],human[17];
 int character[17][5],costume[17][5];
 int match_flags[16],entrant[16][2],next[16];
};
/* Native timelines still drive voices and input. All tree/versus motion uses our team portraits. */
static int TT_BracketModern(int flags){(void)flags;return 1;}
static int TT_BracketIntro(int sequence){return sequence>=0x33&&sequence<=0x39;}
static int TT_BracketWinner(const struct TTBracketView*v,int m){
 int e,n,side;if(!v||m<0||m>=16||!(v->match_flags[m]&1))return -1;
 if(m==v->match){if(v->win_side<0||v->win_side>1)return -1;e=v->entrant[m][v->win_side];}
 else{n=v->next[m];if(n<0||n>=16||n==m)return -1;side=(v->match_flags[m]&4)?0:1;e=v->entrant[n][side];}
 return e>=0&&e<17?e:-1;
}
#ifdef __cplusplus
extern "C" {
#endif
void Port_TagTourBracketPublish(const struct TTBracketView*view);
#ifdef __cplusplus
}
#endif
#endif
