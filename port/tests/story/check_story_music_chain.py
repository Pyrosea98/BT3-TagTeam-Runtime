from pathlib import Path
import subprocess,tempfile
r=Path.cwd()
code=r"""
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include "common.h"
#include "sys/adx.h"
AdxPlayer gAdxPlayerTbl[6];int gAdxChannelGainTbl[6];static AdxSaveView save;AdxSaveView*gSaveData=&save;
static int status[6],starts[6],stops[6],volume[6],last[6];
static int channel(ADXT_HN p){return (int)(long)p-1;}
void ADXM_ExecMain(void){assert(!"unexpected audio teardown wait");}
void ADXT_Stop(ADXT_HN p){int c=channel(p);stops[c]++;status[c]=0;}
int ADXT_GetStat(ADXT_HN p){return status[channel(p)];}
void ADXT_Pause(ADXT_HN p,int v){}
void ADXT_SetOutVol(ADXT_HN p,int v){volume[channel(p)]=v;}
int ADXT_GetOutVol(ADXT_HN p){return volume[channel(p)];}
void ADXT_SetOutPan(ADXT_HN p,int c,int v){}
void File_StartAdxById(ADXT_HN p,int id){int c=channel(p);starts[c]++;last[c]=id;status[c]=2;}
static int once;void Port_AdxOnce(void*p){once++;}void Port_AdxRepeat(void*p){}
int BtlScript_IsSlotBusy(int a,int b){return 0;}
float Mathf_Sin(float v){return sinf(v);}
extern void Port_StoryMusicStart(int),Port_StoryMusicEnd(void);
int main(void){int i,n,v;for(i=0;i<6;i++){gAdxPlayerTbl[i].adxt=(ADXT_HN)(long)(i+1);gAdxChannelGainTbl[i]=128;}save.volume[1]=save.volume[2]=9;
 Bgm_Play(100);Port_StoryMusicStart(200);assert(starts[0]==2&&last[0]==200&&once==1);n=starts[0];v=volume[0];
 for(i=0;i<4;i++){StreamSe_Play(0,50,64,0);Adx_StopAll();Bgm_Play(300+i);Bgm_FadeOutStep();Bgm_Stop();assert(starts[0]==n&&status[0]==2&&stops[0]==0&&volume[0]==v);}
 assert(stops[2]>=4);Port_StoryMusicEnd();assert(status[0]==0&&stops[0]==1);Adx_StopAll();assert(stops[0]==2);
 Bgm_Play(500);n=starts[0];Port_StoryMusicStart(-1);Adx_StopAll();Bgm_Play(999);assert(starts[0]==n&&last[0]==500&&status[0]==2);Port_StoryMusicEnd();Bgm_Stop();assert(status[0]==0);
 puts("PASS actual ADX: uninterrupted CH5 recap across four teardowns, CH6 retains current stream, SE stops normally, no teardown deadlock, release restores ordinary BGM");return 0;
}
"""
with tempfile.TemporaryDirectory() as d:
 p=Path(d)/'music';p.with_suffix('.c').write_text(code)
 subprocess.run(['cc','-DPORT','-ffunction-sections','-fdata-sections','-fsanitize=address,undefined','-Iinclude','-Iport/include','src/sys/adx.c',str(p.with_suffix('.c')),'-Wl,--gc-sections','-lm','-o',str(p)],check=True)
 subprocess.run([str(p)],check=True)
