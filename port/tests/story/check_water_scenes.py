"""Actual Story scene driver: waterlogged cast and camera, native start coordinates, invalid/ocean-only fallbacks."""
from pathlib import Path
import subprocess,tempfile
r=Path.cwd();s=(r/'port/tests/story/test_chapter2_scenes.c').read_text();s=s[:s.index('int main(')]
s=s.replace('f32 BtlCharApi_GetGroundY(s32 obj) { (void)obj; return 0; }','static float sGroundY;f32 BtlCharApi_GetGroundY(s32 obj) { (void)obj; return sGroundY; }')
s+=r"""
static void fresh(void){MsView v={0};Port_StoryChoose(1);assert(Port_ScenarioDraft(&v));Port_ScenarioRoundReset();memset(&sStRun,0,sizeof(sStRun));memset(sPlacements,0,sizeof(sPlacements));sMode=0;sStageReady=1;sHasWater=1;sGroundY=0;sWater=0;sProbeY=-5;sProbeZone=0;
 for(int i=0;i<N;i++){sTeam[i]=v.pick[i].team;sStarts[i]=(Vec4){i*40,-5,i*10,1};sStartRot[i]=(Vec4){0,i*.2f,0,1};sStartZone[i]=0;sPos[i]=(Vec4){900+i*40,-3,900,1};sRot[i]=sStartRot[i];}for(int k=0;k<stCasts[1].count;k++)sTeam[Port_StoryCastActor(k)]=stCasts[1].actor[k].team;}
static void begin(void){BtlSeqTalkCtx ctx={0};st_scene_begin(&ctx,1,ST_SET_INTRO,0,1,-1,0);}

static void check_cast(void){for(int k=0;k<stCasts[1].count;k++)if(!(stCasts[1].actor[k].flags&ST_ACTOR_SCENE)){int a=actor(k);assert(sPos[a].y<=sWater&&isfinite(sPos[a].x)&&sPos[a].x<500);}
 assert(Port_StorySceneParked(actor(4)));}
int main(void){int a,b,c;
 fresh();a=actor(0);b=actor(2);c=actor(1);assert(a==0&&b==1&&c==2);
 /* A real dialogue gathers the dry cast too; plain fighting subtitles never do. */
 sHasWater=0;sPos[a].y=80;begin();assert(sPos[a].y==sProbeY&&sPos[a].x<500);
 fresh();begin();check_cast();assert(sPos[a].x<500&&sPos[b].z!=sPos[a].z);
 fresh();sPos[b].y=80;begin();check_cast();assert(sPos[a].y==sProbeY&&sPos[b].y==sProbeY);
 fresh();sPos[c].y=90;sStarts[c].y=40;begin();check_cast();assert(sPos[c].x!=sPos[a].x&&sPos[c].y==sProbeY);
 fresh();sWater=-50;sProbeY=100;for(int i=0;i<N;i++){sStarts[i].y=100;sPos[i].y=90;}begin();check_cast();sStRun.actor=a;st_scene_camera();assert(sLastEye.y<sWater);
 fresh();sPos[b].y=80;sStartZone[c]=-1;sStarts[c].y=3.4e38f;sProbeZone=-1;sProbeY=3.4e38f;begin();check_cast();
 fresh();begin();sPos[a].y=80;sStRun.actor=a;sStRun.frames=0;sProbeY=200;st_scene_camera();assert(sLastEye.y<=sWater-3.5f);
 fresh();sStageReady=0;sPos[a].y=80;begin();assert(!sPlacements[a]&&sPos[a].y==80);
 fresh();sPos[a].y=sPos[b].y=80;st_fight_enter();for(int i=0;i<60;i++)st_fight_update();assert(!sPlacements[a]&&sPos[a].y==80);
 fresh();sHasWater=0;sPos[b].y=-200;{BtlSeqTalkCtx ctx={0};st_scene_begin(&ctx,1,ST_SET_ALT,0,1,0,0);}
 assert(sPos[a].y==sProbeY&&sPos[b].y==sProbeY);
 fresh();sHasWater=0;sPos[a].y=-200;sPos[b].y=0;{BtlSeqTalkCtx ctx={0};st_scene_begin(&ctx,1,ST_SET_ALT,0,1,0,0);}assert(sPos[a].y==sProbeY);
 fresh();sHasWater=0;sPos[b].y=-200;sStarts[b].y=-80;sProbeY=-7;
 {BtlSeqTalkCtx ctx={0};st_scene_begin(&ctx,1,ST_SET_ALT,0,1,0,0);}assert(sPos[b].y==-7);
 fresh();sHasWater=0;sPos[b].y=-200;sProbeZone=-1;sProbeY=3.4e38f;
 {BtlSeqTalkCtx ctx={0};st_scene_begin(&ctx,1,ST_SET_ALT,0,1,0,0);}assert(isfinite(sPos[b].y));
 puts("PASS real scene formation on dry/wet/airborne terrain, visible reserves, valid zone0, ocean/invalid fallbacks and safe lens; no relocation during fighting subtitles or before stage ready");return 0;}

"""
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'water.c').write_text(s);subprocess.run(['gcc','-std=gnu99','-w','-DPORT','-fsanitize=address,undefined','-Iinclude','-Iport/include','-Iport/src',str(p/'water.c'),'-lm','-o',str(p/'water')],check=True);subprocess.run([str(p/'water')],check=True)
