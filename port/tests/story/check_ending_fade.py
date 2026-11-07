"""Exercise actual WinTalk -> native end fade -> cleanup, including skip/Duel."""
from pathlib import Path
import subprocess
import tempfile

r=Path.cwd()
fixture=(r/'port/tests/story/test_chapter1_scenes.c').read_text()
fixture=fixture[:fixture.index('int main(')]
s=(r/'src/battle/btl_seq.c').read_text()
def function(name):
    start=s.index('s32 '+name+'('); first=s.index('{',start); end=first+1; level=1
    while level:
        level+=(s[end]=='{')-(s[end]=='}'); end+=1
    return s[start:end]+'\n'

fixture+=r'''
static int stoppedTalk,fade,fadeFrames,resultPad,aborted,skip;
void BtlSeq_StopTalk(void){stoppedTalk++;}
s32 Voice_IsStopped(s32 side){return 1;}
s32 BattleResult_IsAborted(void){return aborted;}
s32 BtlSeq_IsModeZero(void){return sMode==0;}
void Fade_Start(s32 index,s32 dir,f32 seconds){assert(index==0&&dir==0&&seconds==1);fade++;fadeFrames=0;}
void BtlSeq_SetResultPad(void){resultPad++;}
s32 BtlPause_GetMenuPad(void){return 0;}
s32 PauseMenu_Update(s32 pad,s32 result){return 0;}
void PauseMenu_Draw(void){}
s32 DemoCam_IsInUse(void){return 0;}
void DemoCam_Stop(void){}
static s32 poll(void){return skip;}
'''
for n in ('BtlSeqWinTalk_Update','BtlSeqWinTalk_Exit','BtlSeqEnd_Enter',
          'BtlSeqEnd_PreUpdate','BtlSeqEnd_Update','BtlSeqEnd_Exit'):
    fixture+=function(n)
fixture+=r'''
static void ending(int winner,int skipped){
 BtlSeqTalkCtx talk={0};BtlSeqWaitCtx end={0};int frames=0,clear=sCameraClear,stop=sStopped[1];
 Port_StoryChoose(0);sResult.winner=winner;sResult.reason=1;sMode=0;skip=skipped;
 sPositions[0]=(Vec4){0,0,0,1};sPositions[1]=(Vec4){100,0,100,1};
 assert(st_ending_enter(&talk));talk.poll=poll;
 int state=5;
 while(state==5&&frames++<3000){st_scene_beat(&talk);state=BtlSeqWinTalk_Update(&talk);}
 assert(state==6&&sStRun.finishing&&sStRun.active&&sStopped[1]==stop&&sCameraClear==clear);
 Vec4 p=sPositions[1],eye=sEye,rot=sAngles;
 BtlSeqWinTalk_Exit(&talk);
 assert(sCameraClear==clear&&sStopped[1]==stop&&Port_StorySceneActorShown(1));
 int oldFade=fade;BtlSeqEnd_Enter(&end);assert(end.step==1&&fade==oldFade+1&&!resultPad);
 state=6;
 while(state==6&&fadeFrames++<100){
  // Simulate gravity trying to drop Raditz; final scene owns the pose until black.
  sPositions[1].y+=100;
  BtlSeqEnd_PreUpdate(&end);
  assert(!memcmp(&sPositions[1],&p,sizeof(p))&&!memcmp(&sEye,&eye,sizeof(eye))&&!memcmp(&sAngles,&rot,sizeof(rot)));
  assert(sCameraClear==clear&&sStopped[1]==stop);
  state=BtlSeqEnd_Update(&end);
 }
 assert(state==BTL_SEQ_EXIT&&fadeFrames>=30&&fadeFrames<100);
 BtlSeqEnd_Exit(&end);
 assert(sCameraClear==clear+1&&sStopped[1]==stop+1&&!sStRun.active&&!sStRun.finishing&&!Port_StorySceneActorShown(1));
}
int main(void){
 ending(1,0); // Piccolo wins: alternative, Raditz departs.
 ending(2,0); // Canonical ending uses the same transition.
 ending(1,1); // Early skip keeps the last authored shot through black too.
 Port_StoryClear();skip=0;BtlSeqWaitCtx end={0};int oldFade=fade,clear=sCameraClear;
 BtlSeqEnd_Enter(&end);BtlSeqEnd_PreUpdate(&end);
 assert(end.step==0&&resultPad==1&&fade==oldFade&&sCameraClear==clear);
 aborted=1;BtlSeqEnd_Enter(&end);assert(end.step==1&&fade==oldFade+1);
 puts("PASS actual canonical/alternate/skipped endings: hold camera and actors through opaque native fade, then release once; no Duel shot/fall; normal Duel and abort preserved");
}
'''
with tempfile.TemporaryDirectory() as d:
    p=Path(d)/'ending.c';p.write_text(fixture)
    subprocess.run(['cc','-w','-DPORT','-Iinclude','-Iport/include','-fsanitize=address,undefined',str(p),'-lm','-o',str(p.with_suffix(''))],check=True)
    subprocess.run([str(p.with_suffix(''))],check=True)
