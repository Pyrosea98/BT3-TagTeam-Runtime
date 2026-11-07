#ifndef MOD_CLASH_PACE_H
#define MOD_CLASH_PACE_H
/* CPU input points only. Native technique/Potara/passive bonuses stay native.
   30 Hz simulation ticks, independent of rendering. No bank of missed presses. */
typedef struct { long long next; } ModClashPace;
static int MCP_Rate(int level){return level<=6?4:level<=13?6:8;}
static void MCP_Begin(ModClashPace*s,int base){(void)base;s->next=0;}
static int MCP_Point(ModClashPace*s,int frame,int beam,int level){
 long long elapsed=(long long)frame-(beam?16:0),now;
 if(elapsed<0)return 0;now=elapsed*MCP_Rate(level);
 if(now<s->next)return 0;s->next+=30;if(s->next<=now)s->next=now+30;
 return 1;
}
#endif
