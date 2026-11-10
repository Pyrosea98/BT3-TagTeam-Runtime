from pathlib import Path
import subprocess,tempfile
r=Path.cwd();s=(r/'src/menu/menu_e_b.c').read_text()
def extract(name):
 a=s.index('static '+('int ' if name in ['Port_ModSelectMusic','Port_ModernRandomSong'] else 'void ')+name+'(');i=s.index('{',a)+1;depth=1
 while depth:depth+=(s[i]=='{')-(s[i]=='}');i+=1
 return s[a:i]
code=r'''#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "sys/pad.h"
#include "mod_select.h"
#include "plat_songs.h"
typedef struct {int id,formCount,form[7];} TsCell;
#include "mod_select_catalog.h"
#define TS_STAGE_RANDOM 35
#define TS_BGM_RANDOM 24
#define TS_BGM_LOCKED 25
#define TEAMSEL_LEAVING 16
#define TEAMSEL_DECIDED 8
#define STGGRID_ID_LOCKED 62
static struct Stage {int bgmCursor,bgm,col,row,stage;} stage;
static struct Team {int flags,stageCount,bgmCount,timer;int *stageIds,*bgmIds;struct Stage *stage;} team,*gTeamSel=&team;
static Pad pad;static int saved=-1,played=-1,plays,requests,writes,sModernStageMusic,sModernStageCursor,sModernRandomBgm=-1;
static s32 sModernStages[96],sModernSongs[72];static int sPortStageRows,sPortStagesAdded,sPortSongsAdded;
int gPortStageReplaceCount=1,gPortReplaceOld[16]={17},gPortReplaceNew[16]={42},gPortExtraStageCount=2,gPortExtraStages[22]={41,42};
int gPortSongCount=2,gPortSongOffsets[PORT_SONG_MAX]={26,27};char gPortSongNames[PORT_SONG_MAX][64];
static Pad*Port_Pad(int p){(void)p;return &pad;}
static int Port_Setting(const char*n,int d){(void)n;return saved<0?d:saved;}
static void Port_SettingSave(const char*n,int v){assert(!strcmp(n,"mod_select_bgm"));saved=v;}
static void Port_SettingsWrite(void){writes++;}
void PortSongs_Init(void){}
static void Bgm_Play(int id){played=id;plays++;}
static int Rand_Range(int n){return n-1;}
static void TeamSel_RequestStage(void){requests++;}
static void Port_LogSelectionIds(int stage,int bgm){(void)stage;(void)bgm;}
static void Port_ModernStageSnapshot(int a){assert(a==1);}
/* FUNCTIONS */
static void input(unsigned pressed,unsigned repeat){pad.pressed=pressed;pad.repeat=repeat;Port_ModernStageInput();}
int main(void){int stages[]={0,62,1,35},songs[]={8,26,27,24,25};team.stage=&stage;team.stageIds=stages;team.stageCount=4;team.bgmIds=songs;team.bgmCount=5;
 assert(Port_ModSelectMusic()==26);saved=27;assert(Port_ModSelectMusic()==27);saved=99;assert(Port_ModSelectMusic()==26);saved=26;
 input(0,PAD_RIGHT);assert(sModernStageCursor==2&&stage.stage==1&&requests==1);assert(plays==0); // stage movement never changes soundtrack
 input(PAD_SQUARE,0);assert(sModernStageMusic);
 input(0,PAD_RIGHT);assert(stage.bgmCursor==1&&played==PORT_BGM_FILE(26));
 input(0,PAD_RIGHT);assert(stage.bgmCursor==2&&played==PORT_BGM_FILE(27));
 input(PAD_CROSS,0);assert(!sModernStageMusic&&stage.bgm==2&&saved==27&&writes==1);
 input(PAD_SQUARE,0);input(0,PAD_LEFT);input(PAD_TRIANGLE,0);assert(stage.bgmCursor==2&&played==PORT_BGM_FILE(27));
 input(PAD_CROSS,0);assert((team.flags&TEAMSEL_LEAVING)&&team.timer==12);assert(team.bgmIds[stage.bgm]==27);
 Port_ModernStageLists();assert(team.stageCount==39&&team.bgmCount==1+gPortSongCount&&team.bgmIds[1]==26&&team.bgmIds[2]==27);int a=0,b=0;for(int i=0;i<team.stageCount;i++){a|=team.stageIds[i]==4;b|=team.stageIds[i]==27;assert(team.stageIds[i]>=0&&team.stageIds[i]<=42);}assert(a&&b&&team.stageIds[0]==35&&team.bgmIds[0]==24);for(int i=0;i<team.bgmCount;i++)assert(team.bgmIds[i]!=25);
 puts("PASS actual stage/music input: locked tiles, grid navigation, same preview/match track, save/cancel, fight confirmation");return 0;}
'''
code=code.replace('/* FUNCTIONS */','\n'.join(extract(n) for n in ['Port_ModernRandomSong','Port_ModSelectMusic','Port_TeamSelPreviewBgm','Port_ModernStageLists','Port_ModernStageInput']))
with tempfile.TemporaryDirectory() as t:
 p=Path(t);(p/'test.c').write_text(code)
 subprocess.run(['cc','-std=c99','-Wall','-Wextra','-Wno-unused-function','-fsanitize=address,undefined','-Iinclude','-Iport/include',str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
