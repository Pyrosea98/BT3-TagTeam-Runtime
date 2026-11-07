"""Native save layout/roundtrip and existing trophy-slot compatibility."""
from pathlib import Path
import subprocess
import tempfile

r = Path.cwd()
s = (r / 'src/sys/save.c').read_text()
def body(name):
    start = s.index(name)
    first = s.index('{', start)
    level = 1
    end = first + 1
    while level:
        level += (s[end] == '{') - (s[end] == '}')
        end += 1
    return s[start:end]

code = '''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "sys/save.h"
SaveData save;
SaveData *gSaveData=&save;
'''
for name in ('s32 Save_GetStoryBits(', 'void Save_OrStoryBits(',
             's32 Save_GetStoryRank(', 'void Save_ClearStoryRank(', 'void Save_RaiseStoryRank('):
    code += body(name) + '\n'
code += r'''
int main(void){
 unsigned char raw[SAVE_SIZE];int c,i;
 assert(sizeof(SaveData)==SAVE_SIZE);
 memset(&save,0,sizeof(save));
 for(i=0;i<SAVE_SLOT_COUNT;i++)save.slot[i].flags=(int)(0xC500007Fu+i*0x01000000u);
 save.money=10000199;save.unlockFlags=0x7f;
 for(c=0;c<SAVE_STORY_RANK_CAPACITY;c++){
  assert(Save_GetStoryRank(c)==0);
  Save_RaiseStoryRank(c,1);assert(Save_GetStoryRank(c)==1);
  Save_RaiseStoryRank(c,3);Save_RaiseStoryRank(c,2);assert(Save_GetStoryRank(c)==3);
 }
 for(i=0;i<SAVE_SLOT_COUNT;i++)assert((unsigned)save.slot[i].flags==(0xC500007Fu+i*0x01000000u|0x00ffff00u));
 Save_OrStoryBits(3);assert((Save_GetStoryBits()&3)==3);
 memcpy(raw,&save,SAVE_SIZE);memset(&save,0,sizeof(save));memcpy(&save,raw,SAVE_SIZE);
 for(c=0;c<SAVE_STORY_RANK_CAPACITY;c++)assert(Save_GetStoryRank(c)==3);
 assert(save.money==10000199&&save.unlockFlags==0x7f);
 memcpy(raw,&save,SAVE_SIZE);
 Save_RaiseStoryRank(-1,3);Save_RaiseStoryRank(72,3);Save_RaiseStoryRank(0,4);Save_RaiseStoryRank(0,0);
 assert(!memcmp(raw,&save,SAVE_SIZE));assert(!Save_GetStoryRank(-1)&&!Save_GetStoryRank(72));
 Save_ClearStoryRank(0);assert(!Save_GetStoryRank(0));assert(Save_GetStoryRank(1)==3);assert(save.money==10000199&&save.unlockFlags==0x7f);
 gSaveData=NULL;Save_ClearStoryRank(0);assert(!Save_GetStoryRank(0));Save_RaiseStoryRank(0,3);
 puts("PASS real native save: all72 rank records, monotonic upgrades, exact0x4000 roundtrip, old flags/outcomes/money untouched, invalid/null safe");
}
'''
with tempfile.TemporaryDirectory() as d:
    p=Path(d); (p/'save.c').write_text(code)
    subprocess.run(['cc','-DPORT','-Iinclude','-Iport/include','-fsanitize=address,undefined',str(p/'save.c'),'-o',str(p/'save')],check=True)
    subprocess.run([str(p/'save')],check=True)
    # Include the ACTUAL trophy implementation to exercise its load/write slots.
    # No native memory card is opened by this fixture.
    ach = r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "plat_achievements.c"
int main(int argc,char**argv){
 char path[1024];unsigned old=(1u<<0)|(1u<<18)|(1u<<19);
 setenv("BT3_SAVES",argv[1],1);
 snprintf(path,sizeof(path),"%s/achievements-1.dat",argv[1]);
 FILE*f=fopen(path,"wb");assert(f);fprintf(f,"BT3ACH1 7 %u %08x\n",old,checksum(7,old));fclose(f);
 assert(!Port_AchievementUpdate());assert(Port_AchievementMask()==old);
 assert(Port_AchievementRank(20)==MA_SILVER&&Port_AchievementRank(21)==MA_GOLD&&Port_AchievementRank(22)==MA_PLATINUM);
 unsigned fresh=MA_HISTORY_EASY|MA_HISTORY_NORMAL|MA_HISTORY_HARD;
 Port_AchievementSubmit(fresh);assert(Port_AchievementUpdate()==fresh);
 assert(Port_AchievementSaveOk()&&Port_AchievementMask()==(old|fresh));
 sLoaded=sMask=sSequence=0;assert(!Port_AchievementUpdate()&&Port_AchievementMask()==(old|fresh));
 Port_AchievementSubmit(fresh);assert(!Port_AchievementUpdate());
 assert(Port_AchievementRank(23)==MA_SILVER&&Port_AchievementRank(24)==MA_GOLD&&Port_AchievementRank(25)==MA_PLATINUM);
 Port_AchievementSubmit(0xfc000000u);assert(!Port_AchievementUpdate()&&Port_AchievementMask()==(old|fresh));
 // Latest slot interrupted: recover the previous valid original slot.
 snprintf(path,sizeof(path),"%s/achievements-0.dat",argv[1]);f=fopen(path,"wb");assert(f);fputs("BT3ACH1 truncated",f);fclose(f);
 sLoaded=sMask=sSequence=0;assert(!Port_AchievementUpdate()&&Port_AchievementMask()==old);
 puts("PASS real trophy persistence: old20 IDs retained, silver/gold/platinum IDs20..22, reload/idempotence and interrupted-write recovery");
}
'''
    (p/'ach.c').write_text(ach)
    subprocess.run(['cc','-Iport/include','-Iport/src','-fsanitize=address,undefined',str(p/'ach.c'),'-o',str(p/'ach')],check=True)
    subprocess.run([str(p/'ach'),d],check=True)
