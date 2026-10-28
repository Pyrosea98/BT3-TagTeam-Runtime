/* Bounded in-process evidence for the fusion-only melee / beam reaction report.
   Never changes battle data, tables, flags, RNG or admission. Buffered file I/O. */
extern s32 BtlAtk_GetId(BtlMgrChr*);
extern s32 BtlAnim_GetId(BtlMgrChr*);
static void Port_FusionCombatTrace(BtlMgrChr*chr,const char*phase){
 static int armed,count,last[12][9],seen[12],ticks[12];static FILE*log;
 BtlMgrMember*m;BtlMgrObj*obj;u32 param,atk,body,entry1,expectedParam=0,expectedAtk=0;int state[9],i,changed=0,heartbeat,who;
 if(!chr||count>=512)return;who=chr->side;if(who<0||who>=12)return;
 m=BtlMember_GetActive(chr);obj=BtlChar_GetObj(chr);if(!m||!obj)return;
 if(m->chara>=224&&m->chara<=241)armed=1;if(!armed)return;
 {extern int Port_FusionConsumedActor(int);state[0]=m->chara;state[1]=chr->action;state[2]=phase[0]=='t'?BtlAtk_GetId(chr):-1;state[3]=*(s32*)((u8*)chr+0xfb0);state[4]=*(s32*)((u8*)chr+0xf40);state[5]=BtlChar_TestFlag(chr,0xbe);state[6]=m->gauge.unk20;state[7]=chr->objId;state[8]=Port_FusionConsumedActor(who);}
 heartbeat=(++ticks[who]%120)==0;for(i=0;i<9;i++)changed|=last[who][i]!=state[i];
 if(phase[0]=='t'&&seen[who]&&!changed&&!heartbeat)return;
 for(i=0;i<9;i++)last[who][i]=state[i];seen[who]=1;
 param=*(u32*)((u8*)obj+0x91c);atk=*(u32*)((u8*)obj+0x920);entry1=*(u32*)((u8*)obj+0x50);body=*(u32*)((u8*)obj+0x54);
 if(obj->file[0]&&obj->file[0][0]>=80&&obj->file[0][0]<65536){expectedParam=(u32)obj->file[0]+(obj->file[0][18]&~3u);expectedAtk=(u32)obj->file[0]+(obj->file[0][19]&~3u);}
 if(!log){log=fopen("bt3_fusion_combat.log","a");if(log)setvbuf(log,0,_IOFBF,16384);}count++;
 if(log){fprintf(log,"fusion-combat phase=%s actor=%d object=%d chara=%d costume=%d fused=%d variant=%d action=%x anim=%x atk_id=%d reaction=%d pending_target=%d flagBE=%d consumed=%d param=%08x expected_param=%08x atk=%08x expected_atk=%08x atk_match=%d entry1=%08x body=%08x\n",phase,who,chr->objId,m->chara,m->costume,m->gauge.unk34,m->gauge.unk20,chr->action,BtlAnim_GetId(chr),state[2],state[3],state[4],state[5],state[8],param,expectedParam,atk,expectedAtk,expectedAtk?(atk==expectedAtk):-1,entry1,body);
  if(phase[0]!='t'||heartbeat||count%32==0)fflush(log);if(count==512){fclose(log);log=0;}}
}
