"""Real native intro prefixes + presentation driver, no live game needed."""
from pathlib import Path
import tempfile,subprocess
r=Path.cwd();code=(r/'port/tests/story/test_chapter2_scenes.c').read_text();code=code[:code.index('int main(')]
s=(r/'src/battle/btl_seq.c').read_text()
for name in ('BtlSeqIntroTalk_PreUpdate','BtlSeqIntroTalk_Update'):
 a=s.index('s32 '+name+'(');b=s.index('#ifdef PORT',a);b=s.index('#endif',b)+len('#endif');prefix=s[a:b];code+=prefix.replace(name,'TEST_'+name)+'\n return 99;\n}\n'
code+=r"""
static void choose(int ch){MsView v={0};Port_StoryChoose(ch);assert(Port_ScenarioDraft(&v));Port_ScenarioRoundReset();for(int i=0;i<N;i++){sPos[i]=(Vec4){0,0,i*50,1};sTeam[i]=i<v.total?v.pick[i].team:1;}sMode=0;sPresentationEnabled=1;sPresentationInput=0;sPresentationClock=0;}
static void tick(BtlSeqTalkCtx*c,int ms,int input){sPresentationClock+=ms;sPresentationInput=input;assert(TEST_BtlSeqIntroTalk_PreUpdate(c)==1);sPresentationInput=0;}
int main(){BtlSeqTalkCtx ctx={0};int ch,phase,p,health=0;
 for(int i=0;i<ST_COUNT;i++){if(!ST_Interlude(i))assert(*STP_Text(i,0)&&*STP_Text(i,1));assert(STP_RecapMs(i)>=22000&&STP_RecapMs(i)<=38000);}
 assert(STP_Profile(0)==0&&STP_Profile(1)==0&&STP_Profile(2)==2&&STP_Profile(3)==3&&STP_Profile(5)==5&&STP_Profile(6)==6);
 choose(0);assert(st_intro_enter(&ctx)&&ctx.step==ST_STEP_PREFIGHT&&!sStRun.active);
 assert(Port_StoryPresentationView(&ch,&phase,&p)&&ch==0&&phase==1&&p==0&&sPresentationBgm==900);
 assert(TEST_BtlSeqIntroTalk_Update(&ctx)==1); // no Duel path while overlay owns intro
 tick(&ctx,100,0x200);assert(sStPreludePhase==1); // ignores opening confirm bounce
 tick(&ctx,200,0);tick(&ctx,100,0x200);assert(sStPreludePhase==2&&sPresentationBgm==901);
 tick(&ctx,4000,0);assert(sStPreludeElapsed==200); // stalled/unfocused clock capped
 tick(&ctx,200,0x200);assert(!sStPreludePhase&&ctx.step==ST_STEP_HOLD&&sStRun.active&&sPresentationBgm==0x10B19);
 assert(!Port_StoryPresentationView(0,0,0)&&TEST_BtlSeqIntroTalk_Update(&ctx)==1); // confirm cannot leak into intro
 assert(TEST_BtlSeqIntroTalk_Update(&ctx)==99);st_scene_end();
 // Automatic reading/title durations are wall-time based, independent of 30/60 rendered updates.
 for(int hz=30;hz<=60;hz+=30){choose(1);ctx=(BtlSeqTalkCtx){0};assert(st_intro_enter(&ctx));int wall=0;while(ctx.step==ST_STEP_PREFIGHT){int delta=hz==30?33:17;tick(&ctx,delta,0);wall+=delta;assert(wall<60000);}int expected=STP_RecapMs(1)+STP_TitleMs(0);assert(wall>=expected&&wall<expected+80);assert(sStRun.active&&!Port_StoryPresentationView(0,0,0));st_scene_end();}
 // Start omits both presentation stages, but the authored intro camera/subtitle starts normally.
 choose(1);ctx=(BtlSeqTalkCtx){0};st_intro_enter(&ctx);tick(&ctx,200,0);tick(&ctx,200,0x1000);assert(ctx.step==ST_STEP_HOLD&&!sStPreludePhase&&sStRun.active);st_scene_end();
 // Exit and next attempt cannot carry stale overlay/music/progress into another mode.
 choose(1);ctx=(BtlSeqTalkCtx){0};st_intro_enter(&ctx);st_scene_end();assert(!Port_StoryPresentationView(0,0,0)&&!sStPreludePhase);
 Port_ScenarioClear();assert(!Port_StoryPresentationView(0,0,0)&&!st_intro_enter(&ctx));
 puts("PASS pre-fight:17 EN/ES recaps; recap/card/intro ordering; native intro guards; audio restoration; auto duration at30/60; stall cap; confirm debounce; Start skip without skipping intro; cleanup and other-mode isolation");}
"""
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.c').write_text(code);subprocess.run(['gcc','-std=gnu99','-DPORT','-w','-fsanitize=address,undefined','-Iinclude','-Iport/include','-Iport/src',str(p/'test.c'),'-lm','-o',str(p/'test')],check=True);subprocess.run([str(p/'test')],check=True)
