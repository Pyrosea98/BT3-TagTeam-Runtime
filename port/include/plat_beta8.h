#ifndef PORT_PLAT_BETA8_H
#define PORT_PLAT_BETA8_H

/* Native file-id routing into the Beta8 model volumes for the mod's added characters (ids above the stock
   roster). Reads <BT3_DATA>/beta8/bundle-map.bin once. Returns a port-only global id (0x40000000 | transport)
   for ADXF_OpenAfs(volume 3/4, entry), or -1 when the character/costume has no verified bundle. */
int Port_Beta8CostumeCount(int chara,int *costume);
int Port_Beta8FileId(int chara, int costume, int damaged, int kind); /* kind 0 model, 1 anim16, 2 anim24 */

int Port_Beta8VoiceFileId(int chara,int language); /* source bank; same-character alternate language if absent, -1 if unavailable */
int Port_Beta8ResolveFile(int chara,int costume,int damaged,int kind,int stock_id);
int Port_Beta8CharacterForModel(int file_id);
int Port_Beta8PortraitFileId(int chara,int costume);
int Port_Beta8PortraitResolve(int chara,int costume,int legacy_id);
int Port_Beta8HasCharacter(int chara);
int Port_Beta8SizeClass(int chara,int costume); /* exact ISO parameter byte2; -1 unavailable */
int Port_Beta8HasParam(int chara); /* its 256-byte BtlParam is in beta8/btlparam.bin */
int Port_Beta8IsTransform(int source, int target); /* target is one of source's four transformation targets */
int Port_Beta8IsAnyTransform(int target); /* target is a transformation of some character (a form, not DLC) */
int Port_Beta8MemberFileId(int chara,int side);
void *Port_Beta8LoadSmallPortrait(int chara,int costume);
void Port_Beta8FreeSmallPortrait(void *data);
void *Port_Beta8LoadChipBundle(int chara);
void Port_Beta8FreeChipBundle(void *data);
int Port_Beta8StockAlias(int chara); /* stock id (0..160) an alias resolves to, or -1 */
int Port_Beta8FormList(int id, int *out, int max); /* a split cell's entries, base first; 0 = none */
int Port_Beta8PatchFormParam(int chara, void *param); /* writes the split form chain into a live BtlParam */
int Port_Beta8FormTarget(int source, int target); /* the entry a transformation of `source` really loads */
int Port_Beta8FormSlotTarget(int source, int slot, int target); /* ... with the free-slot SSJ4 link */
int Port_Beta8FusionResult(int source, int scost, int partner, int pcost, int result); /* the result entry */
int Port_Beta8AppendSelectForms(int chara,int *forms,int count,int capacity);
void Port_Beta8PatchPowerText(void *section);
void Port_Beta8ApplyMemberStats(void *rows,int count);
int Port_Beta8UsesMemberStats(int chara);
int Port_Beta8EffectTier(int chara);
float Port_Beta8AuraMultiplier(int chara);
void Port_Beta8ChargeReset(void);
float Port_Beta8ChargeUpdate(int player,int obj,int chara,int charging);
float Port_Beta8ChargeScale(int obj);
int Port_Beta8Ssj4Object(int obj);
void Port_Beta8Ssj4Load(int obj,int chara,void*param);
void Port_Beta8Ssj4TraceAura(int obj,int flags);
#endif
