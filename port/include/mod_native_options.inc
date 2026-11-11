/* Game-thread bridge: UI edits persisted Port_Setting keys, never native save memory. */
extern int Port_Setting(const char*,int);
extern void Port_NativeOptionPublish(int,int);
static int nf_bound(int v,int lo,int hi){return v<lo?lo:v>hi?hi:v;}
void Port_NativeRulesApply(void){
 static const char*keys[]={"match_time_rule","match_cpu_level","match_commentary","match_handicap_a","match_handicap_b","match_stage_destruction"};
 static const int max[]={4,4,6,1,1,1};int i,v;
 if(!gSaveData)return;
 for(i=0;i<6;i++){v=nf_bound(Port_Setting(keys[i],gSaveData->rule[i]^(i==5)),0,max[i]);gSaveData->rule[i]=v^(i==5);Port_NativeOptionPublish(i,v);}
}
static void Port_NativeOptionsTick(void){
 int voice,sound,bgm,se,camera,assist,x,y;
 extern void SndOpt_Apply(void);
 if(!gSaveData)return;Port_NativeRulesApply();
 camera=nf_bound(Port_Setting("game_camera_type",gSaveData->unk1694),0,2);
 assist=nf_bound(Port_Setting("game_camera_assist",gSaveData->unk1698),0,1);
 voice=nf_bound(Port_Setting("game_voice_bank",(gSaveData->flags&1)?0:1),0,1);
 sound=nf_bound(Port_Setting("game_sound_mode",gSaveData->soundMode),0,1);
 bgm=nf_bound(Port_Setting("game_music_volume",gSaveData->bgmVolume),0,9);
 se=nf_bound(Port_Setting("game_effect_volume",gSaveData->seVolume),0,9);
 x=nf_bound(Port_Setting("game_screen_x",gSaveData->screenX),-32,32);y=nf_bound(Port_Setting("game_screen_y",gSaveData->screenY),-32,32);
 gSaveData->unk1694=camera;gSaveData->unk1698=assist;gSaveData->screenX=x;gSaveData->screenY=y;
 if(voice)gSaveData->flags&=~1;else gSaveData->flags|=1;
 if(sound!=gSaveData->soundMode||bgm!=gSaveData->bgmVolume||se!=gSaveData->seVolume){gSaveData->soundMode=sound;gSaveData->bgmVolume=bgm;gSaveData->seVolume=se;SndOpt_Apply();}
 Port_NativeOptionPublish(6,camera);Port_NativeOptionPublish(7,assist);Port_NativeOptionPublish(8,voice);Port_NativeOptionPublish(9,sound);Port_NativeOptionPublish(10,bgm);Port_NativeOptionPublish(11,se);Port_NativeOptionPublish(12,x);Port_NativeOptionPublish(13,y);
}
