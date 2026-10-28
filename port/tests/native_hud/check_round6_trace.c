#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <assert.h>
typedef int s32;typedef uint32_t u32;typedef unsigned char u8;
typedef struct {int unk20,unk34;} Gauge;typedef struct {int chara,costume;Gauge gauge;} BtlMgrMember;
typedef struct{char pad[0xa4];u32*file[3];char rest[0x2000];}BtlMgrObj;
typedef struct{int side,objId,action;char pad[0x1600];}BtlMgrChr;
static BtlMgrObj object;static BtlMgrMember member;static u32 model[512];static int attack=4;
BtlMgrMember*BtlMember_GetActive(BtlMgrChr*c){(void)c;return &member;}BtlMgrObj*BtlChar_GetObj(BtlMgrChr*c){(void)c;return &object;}int BtlChar_TestFlag(BtlMgrChr*c,int f){(void)c;(void)f;return 0;}int Port_FusionConsumedActor(int n){return n==1;}int BtlAtk_GetId(BtlMgrChr*c){(void)c;return attack;}int BtlAnim_GetId(BtlMgrChr*c){(void)c;return 0x3c;}
#include "../../include/mod_fusion_combat_trace.inc"
int main(){BtlMgrChr c={0};member.chara=224;member.gauge.unk34=1;model[0]=1008;model[18]=512;model[19]=768;object.file[0]=model;u32 p=(u32)(uintptr_t)model+512,a=(u32)(uintptr_t)model+768;memcpy((char*)&object+0x91c,&p,4);memcpy((char*)&object+0x920,&a,4);BtlMgrChr before=c;BtlMgrObj ob=object;BtlMgrMember mb=member;
Port_FusionCombatTrace(&c,"bind");Port_FusionCombatTrace(&c,"tick");for(int i=0;i<800;i++){c.action=i;Port_FusionCombatTrace(&c,"tick");}FILE*f=fopen("bt3_fusion_combat.log","r");assert(f);char line[1500];int n=0,match=0;while(fgets(line,sizeof(line),f)){n++;match+=strstr(line,"atk_match=1")!=0;}fclose(f);assert(n==512&&match==512);c.action=before.action;assert(!memcmp(&c,&before,sizeof(c))&&!memcmp(&object,&ob,sizeof(ob))&&!memcmp(&member,&mb,sizeof(mb)));puts("PASS actual bounded combat trace: direct/fused metadata, expected attack-pointer comparison,512-record cap, unchanged fighter/member/object state");}
