#ifndef MOD_BLOOD_PROGRESS_H
#define MOD_BLOOD_PROGRESS_H
/* One entry per real team member, not per visible actor. Bounded to the
 * native 16 actors x 10 members; reset exclusively with battle lifecycle. */
typedef struct { unsigned int member; int level; } BloodProgressEntry;
typedef struct { BloodProgressEntry entry[160]; } BloodProgress;
static int BloodProgress_Get(const BloodProgress *s,unsigned int member){
    int i;for(i=0;i<160;i++)if(member&&s->entry[i].member==member)return s->entry[i].level;return 0;
}
static int BloodProgress_Level(int health,int maximum) {
    long long lost;
    if(maximum<=0)return 0;
    lost=(long long)maximum-health;
    if(lost*100>=maximum*70LL)return 3;
    if(lost*100>=maximum*40LL)return 2;
    if(lost*100>=maximum*15LL)return 1;
    return 0;
}
static void BloodProgress_Clear(BloodProgress *s) {
    int i;for(i=0;i<160;i++){s->entry[i].member=0;s->entry[i].level=0;}
}
static int BloodProgress_Update(BloodProgress *s,unsigned int member,int health,int maximum,int excluded) {
    int i,empty=-1,level;
    if(!member||maximum<=0)return 0;
    for(i=0;i<160;i++) {
        if(s->entry[i].member==member)break;
        if(empty<0&&!s->entry[i].member)empty=i;
    }
    /* Never transfer another body's marks by recycling an occupied slot. */
    if(i==160){if(empty<0||excluded)return 0;i=empty;s->entry[i].member=member;}
    if(excluded)return 0;
    level=BloodProgress_Level(health,maximum);
    if(level>s->entry[i].level)s->entry[i].level=level;
    return s->entry[i].level;
}
#endif
