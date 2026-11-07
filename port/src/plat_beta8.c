/*
 * The Beta8 model volumes: the mod's added characters (ids above the stock roster) are not in the stock id
 * space (0x590 + id * 10 + costume), which runs into the voice banks. Their verified bundles are listed in
 * <root>/beta8/bundle-map.bin (B8RM): character, zero-based costume, damaged, and one transport id per file
 * (upper 8 bits volume 3 or 4, lower 24 bits AFS entry). The battle loader asks for a port-only id, and
 * File_OpenById opens that (volume, entry) through ADXF_OpenAfs. The stock characters keep their stock ids.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "plat_beta8.h"
#include "b8_selection_rules.h"
#include "mod_costume_ownership.h"

#define B8_MAX_ROWS 4096

typedef struct B8Row {
    uint32_t chara, costume, damaged;
    uint32_t model, anim16, anim24;
    uint32_t param; /* index of the character's 256-byte BtlParam in beta8/btlparam.bin */
} B8Row;

/* The separated select entries and the transformation-only form entries. Each one is an alias of a
   stock character and one of its costumes: the entry has its own select cell, name and power, while the
   model, animation, voice, portrait, parameter block and squared chip come from the stock costume. */
typedef struct B8Alias {
    int id, chara, costume;
} B8Alias;

static const B8Alias sAliases[] = {
    {300,109,1}, /* Mucchi: independent GT fighter, not an equipment prop */
    {295,96,1},{296,126,2},{297,127,2},{298,128,2},{299,96,2}, /* audited Mecha Frieza / Cold forms */
    {291,113,3},{292,114,3},{293,115,3},{294,116,3}, /* independent Multiverse Buu */
    { 213, 158, 2 }, /* Oceanus human: imported six-star model */
    { 245, 158, 3 }, /* Oceanus dragon: imported six-star model */
    { 247, 110, 3 }, /* Gokoro: independent Potara result, not Gogeta costume */
    { 212, 210, 1 }, /* Yamu: the separated Spopovich costume 2 (UI 2) */
    { 214,  51, 2 }, /* Gokhan: the separated Vegetto costume 3 (UI 3) */
    { 215,  51, 1 }, /* Vegetto GT: the separated Vegetto costume 2 (UI 2) */
    { 216,  52, 2 }, /* Gokhan Super Saiyajin */
    { 218, 166, 2 }, /* Gokhan Super Saiyajin 2 */
    { 219, 149, 2 }, /* Gokhan Super Saiyajin 3 */
    { 220,  52, 1 }, /* Vegetto GT Super Saiyajin */
    { 221, 166, 1 }, /* Vegetto GT Super Saiyajin 2 */
    { 222, 149, 1 }, /* Vegetto GT Super Saiyajin 3 */
    { 223,  59, 1 }, /* Vegetto GT Super Saiyajin 4 */
    { 224, 110, 0 }, /* Gogetto (Metamoran): Gogeta body, base hair */
    { 225,  54, 2 }, /* Gogetto Super Saiyajin: the dance fusion-of-fusions model */
    { 226, 249, 0 }, /* Gogetto Super Saiyajin 2: Gogeta SSJ2 body */
    { 229, 172, 0 }, /* Gogetto Super Saiyajin 3: Gogeta SSJ3 body */
    { 230,  54, 0 }, /* Gogetto Super Saiyajin 4: Gogeta SSJ4 body */
    { 234,  51, 0 }, /* Gogetto Potara: Vegetto body, base hair */
    { 235,  59, 2 }, /* Gogetto Potara Super Saiyajin: the potara fusion-of-fusions model */
    { 238, 166, 0 }, /* Gogetto Potara Super Saiyajin 2: Vegetto SSJ2 body */
    { 239, 149, 0 }, /* Gogetto Potara Super Saiyajin 3: Vegetto SSJ3 body */
    { 241,  59, 0 }, /* Gogetto Potara Super Saiyajin 4: Vegetto SSJ4 body */
    { 244, 122, 2 },  /* Vegeta Explorador SSJ4: the GT costume 3 model (form of costume 5) */
    { 161, 3, 3 }, { 163, 17, 3 }, { 164, 28, 3 }, { 170, 34, 2 },
    { 177, 39, 3 }, { 178, 41, 3 }, { 179, 44, 3 }, { 181, 45, 3 },
    { 182, 46, 2 }, { 183, 55, 1 }, { 184, 61, 2 }, { 185, 61, 3 },
    { 186, 63, 0 }, { 187, 66, 3 }, { 189, 68, 2 }, { 190, 71, 2 },
    { 192, 71, 3 }, { 194, 76, 2 }, { 195, 77, 3 }, { 197, 80, 2 },
    { 198, 80, 3 }, { 199, 81, 3 }, { 203, 82, 2 }, { 204, 84, 2 },
    { 205, 85, 2 }, { 206, 87, 3 }, { 207, 88, 2 }, { 208, 90, 2 },
    { 209, 90, 3 }, { 242, 98, 2 }, { 251, 99, 2 }, { 252, 100, 2 },
    { 253, 103, 2 }, { 254, 103, 3 }, { 255, 109, 2 }, { 256, 109, 3 }, { 257, 111, 1 },
    { 258, 111, 3 }, { 259, 113, 2 }, { 260, 118, 3 }, { 261, 123, 2 },
    { 262, 124, 2 }, { 263, 124, 3 }, { 264, 129, 2 }, { 265, 130, 2 },
    { 266, 130, 3 }, { 267, 131, 3 }, { 268, 134, 2 }, { 269, 135, 2 },
    { 270, 135, 3 }, { 271, 136, 3 }, { 272, 140, 1 }, { 273, 140, 3 },
    { 274, 145, 2 }, { 275, 146, 1 }, { 276, 146, 2 }, { 277, 151, 1 },
    { 278, 153, 3 }, { 279, 154, 1 }, { 280, 155, 2 }, { 281, 18, 3 },
    { 282, 42, 3 }, { 283, 43, 3 }, { 284, 47, 2 }, { 285, 79, 2 },
    { 286, 136, 2 }, { 287, 142, 3 }, { 288, 143, 0 }, { 289, 143, 3 },
    { 290, 153, 2 }
};
#define B8_ALIAS_COUNT ((int)(sizeof(sAliases) / sizeof(sAliases[0])))

static const B8Alias *b8_alias(int id) {
    int i;

    for (i = 0; i < B8_ALIAS_COUNT; i++) {
        if (sAliases[i].id == id) {
            return &sAliases[i];
        }
    }
    return NULL;
}

/* New outfits are appended; original physical costumes0..3 remain untouched. */
static void b8_profile_route(int *chara,int *costume){
 if(*costume!=4)return;
 if(*chara==159){*chara=17;*costume=3;}
 else if(*chara==160){*chara=18;*costume=3;}
 else if(*chara==13){*chara=46;*costume=2;}
}
int Port_Beta8CostumeIdentity(int chara,int costume){if(costume==4){if(chara==159)return 163;if(chara==160)return 281;if(chara==13)return 182;}return chara;}
void Port_Beta8PowerIdentity(int *chara,int *costume){const B8Alias*a;if(!chara||!costume)return;b8_profile_route(chara,costume);a=b8_alias(*chara);if(a){*chara=a->chara;*costume=a->costume;}}
/* Costumes that exist in the UI but borrow an existing model. Vegeta (Explorador) costume 5
   is the 5th UI costume and uses the costume-1 model. */
static int b8_costume_alias(int chara, int costume) {
    if (chara == 29 && costume == 4) {
        return 0;
    }
    return costume;
}

/* The transform target an entry asks for, translated to the entry that owns that form. The stock
   target ids are kept when the source has no split form (every other character). */
typedef struct B8FormTarget {
    int source, target, mapped;
} B8FormTarget;

static const B8FormTarget sFormTargets[] = {
    {95,96,295},{97,126,296},{296,127,297},{297,128,298},{298,96,299},
    {291,115,292},{292,116,293},{293,117,294},
    {163,18,281},{281,17,163},
    {178,42,282},{178,43,283},{282,43,283},{282,41,178},{283,41,178},
    {182,47,284},{284,46,182},{13,47,284},
    {269,136,286},{269,153,290},{286,153,290},{286,135,269},{290,135,269},
    {260,142,287},{260,143,289},{287,143,289},{287,118,260},{289,118,260},
    { 214,  52, 216 }, { 214, 166, 218 }, { 214, 149, 219 },
    { 216, 166, 218 }, { 216, 149, 219 }, { 216,  51, 214 },
    { 218, 149, 219 }, { 218,  51, 214 },
    { 219,  51, 214 },
    { 215,  52, 220 }, { 215, 166, 221 }, { 215, 149, 222 },
    { 220, 166, 221 }, { 220, 149, 222 }, { 220,  51, 215 },
    { 221, 149, 222 }, { 221,  59, 223 }, { 221,  51, 215 },
    { 222,  59, 223 }, { 222,  51, 215 },
    { 223,  51, 215 }
};
#define B8_FORM_TARGET_COUNT ((int)(sizeof(sFormTargets) / sizeof(sFormTargets[0])))

/* The table holds 1541 rows today; keep it off the exe's static data so the game's own data layout (and the
   adapted initializer .dat) does not move. */
static B8Row *sRows;
static int sCount = -1;

static const char *root_dir(void) {
    const char *r = getenv("BT3_DATA");
    return r != NULL ? r : "gamedata";
}

static void load_map(void) {
    char path[600];
    unsigned char hdr[16];
    FILE *fp;
    uint32_t count, stride, i;

    if (sCount >= 0) {
        return;
    }
    sCount = 0;
    snprintf(path, sizeof(path), "%s/beta8/bundle-map.bin", root_dir());
    fp = fopen(path, "rb");
    if (fp == NULL) {
        return;
    }
    if (fread(hdr, 1, 16, fp) != 16 || memcmp(hdr, "B8RM", 4) != 0) {
        fclose(fp);
        return;
    }
    memcpy(&count, hdr + 8, 4);
    memcpy(&stride, hdr + 12, 4);
    if (stride != 32 || count > B8_MAX_ROWS) {
        fclose(fp);
        return;
    }
    sRows = (B8Row *)malloc((size_t)count * sizeof(B8Row));
    if (sRows == NULL) {
        fclose(fp);
        return;
    }
    for (i = 0; i < count; i++) {
        uint32_t r[8];

        if (fread(r, 4, 8, fp) != 8) {
            break;
        }
        sRows[sCount].chara = r[0];
        sRows[sCount].costume = r[1];
        sRows[sCount].damaged = r[2];
        sRows[sCount].model = r[3];
        sRows[sCount].anim16 = r[4];
        sRows[sCount].anim24 = r[5];
        sRows[sCount].param = r[6];
        sCount++;
    }
    fclose(fp);
    fprintf(stderr, "bt3: beta8: %d bundle routes\n", sCount);
}

/* Offline models authored by Claude: the form ids whose body/hair comes from a new file in
   <root>/mods/pak3us3/06500..06507.bin. Animation sets follow the verified per-form attack-entry19 donor.
   Damaged model IDs avoid the existing6520..6529portrait reservation. */
typedef struct B8ModelOverride {
    int id, baseChara, baseCostume;
    uint32_t model;
    int costume; /* -1: the custom single-costume entry, 0: stock59 costume0 only */
    uint32_t damagedModel;
} B8ModelOverride;

static const B8ModelOverride sModelOverrides[] = {
    { 213, 134, 0, 0x30018B2u, -1, 0 }, /* archive158*40+2, not158*10+2 */
    { 245, 145, 0, 0x30018B3u, -1, 0 },
    { 224, 110, 0, 0x3001964u, -1, 0x3001970u },
    { 225, 54, 2, 0x300196Cu, -1, 0 },
    { 226, 249, 0, 0x3001965u, -1, 0x3001971u },
    { 229, 172, 0, 0x3001966u, -1, 0x3001972u },
    { 230, 54, 0, 0x3001967u, -1, 0x3001973u },
    { 234, 51, 0, 0x3001968u, -1, 0x3001974u },
    { 235, 59, 2, 0x300196Du, -1, 0 },
    { 238, 166, 0, 0x3001969u, -1, 0x3001975u },
    { 239, 149, 0, 0x300196Au, -1, 0x3001976u },
    { 241, 59, 0, 0x300196Bu, -1, 0x3001977u },
    { 59, 59, 1, 0x300196Fu, 0, 0x3001996u },
};
#define B8_MODEL_OVERRIDE_COUNT ((int)(sizeof(sModelOverrides) / sizeof(sModelOverrides[0])))

static int b8_model_override(int chara,int costume,int damaged,int kind) {
 int i,j,pass;
 for(i=0;i<B8_MODEL_OVERRIDE_COUNT;i++){
  const B8ModelOverride*o=&sModelOverrides[i];
  if(o->id!=chara||(o->costume>=0&&o->costume!=costume))continue;
  if(kind==0)return (int)(0x40000000u|(damaged&&o->damagedModel?o->damagedModel:o->model));
  /* Use the exact damaged animation row when present; an absent damaged
     donor retains its normal animation (the ISO has no damaged54-2/59-2). */
  for(pass=0;pass<(damaged?2:1);pass++)for(j=0;j<sCount;j++){
   int want=damaged&&!pass;
   if((int)sRows[j].chara==o->baseChara&&(int)sRows[j].costume==o->baseCostume&&(int)sRows[j].damaged==want){
    uint32_t t=kind==1?sRows[j].anim16:sRows[j].anim24;
    if(t)return (int)(0x40000000u|t);
   }
  }
  return -1;
 }
 return -1;
}

int Port_Beta8FileId(int chara, int costume, int damaged, int kind) {
    uint32_t transport;
    const B8Alias *alias;
    int i;

    /* Native story import301: exact stock Yajirobe resources in private mod entries.
       Slot60 remains Beerus in every ordinary mod mode. */
    if(chara==301&&costume==0&&kind>=0&&kind<=2)
        return (int)(0x43000000u+(unsigned)(kind==0?(damaged?6801:6800):kind==1?6802:6803));
    load_map();
    {
        int ov = b8_model_override(chara, costume, damaged, kind);
        if (ov >= 0) {
            return ov;
        }
    }
    /* Vegeta (Explorador) costume 5: the model is the 29-0 body with its BtlParam target
       replaced by 244 (the GT costume 3 SSJ4 form). */
    if (chara == 29 && costume == 4 && kind == 0) {
        return (int)(0x40000000u | 0x300196Eu);
    }
    b8_profile_route(&chara,&costume);
    /* The separated entries use the stock costume's model bundle (the select costume is not used). */
    alias = b8_alias(chara);
    if (alias != NULL) {
        chara = alias->chara;
        costume = alias->id==186 ? costume : alias->costume;
    }
    costume = b8_costume_alias(chara, costume);
    load_map();
    transport = 0;
    for (i = 0; i < sCount; i++) {
        if ((int)sRows[i].chara == chara && (int)sRows[i].costume == costume &&
            (int)sRows[i].damaged == (damaged != 0)) {
            transport = kind == 0 ? sRows[i].model : kind == 1 ? sRows[i].anim16 : sRows[i].anim24;
            break;
        }
    }
    if (transport == 0) {
        return -1;
    }
    return (int)(0x40000000u | transport);
}

/* The mod's Spopovich cell (210) keeps only its base costume; every separated entry has one costume too.
   -1 leaves the stock table alone. */
int Port_Beta8CostumeCount(int chara,int *costume) { if(chara==301){if(costume)*costume=0;return 1;} return B8CostumeCountRule(chara,b8_alias(chara)!=NULL,costume); }

/* Source-verified added-character banks. They use the ISO's separate 46-entry
   rows, not the stock +character voice IDs (which overlap other resources). */
typedef struct B8VoiceRow {uint32_t chara,language,transport,bytes;} B8VoiceRow;
static B8VoiceRow *sVoices;
static int sVoiceCount=-1;
/* Voice manifests are data-driven in the recomp; keep warning bookkeeping separate from the
   stock 0..290 roster so a future character bank cannot index past this table. */
static unsigned char sVoiceWarned[1024];
static void load_voices(void){
 char path[600];FILE *fp;uint32_t h[4],i;
 if(sVoiceCount>=0)return;sVoiceCount=0;
 snprintf(path,sizeof(path),"%s/beta8/voice-map.bin",root_dir());fp=fopen(path,"rb");if(!fp)return;
 if(fread(h,1,16,fp)!=16||memcmp(h,"B8VC",4)||h[1]!=1||h[2]>4096||h[3]!=16){fclose(fp);return;}
 sVoices=(B8VoiceRow*)malloc(h[2]*sizeof(B8VoiceRow));if(!sVoices){fclose(fp);return;}
 for(i=0;i<h[2];i++){
  B8VoiceRow row;
  if(fread(&row,1,16,fp)!=16||row.chara<165||row.chara>290||row.language>1||(row.transport>>24)!=4||(row.transport&0xffffff)>=5796||row.bytes<20||row.bytes>0x90800){free(sVoices);sVoices=NULL;fclose(fp);return;}
  sVoices[i]=row;
 }
 if(fgetc(fp)!=EOF){free(sVoices);sVoices=NULL;fclose(fp);return;}
 fclose(fp);sVoiceCount=h[2];fprintf(stderr,"bt3: beta8: %d verified ISO voice routes\n",sVoiceCount);
}
int Port_Beta8VoiceFileId(int chara,int language){
 if(chara==301&&language>=0&&language<=1)return (int)(0x43000000u+6805u+(unsigned)language);
 int i,alternate=-1;const B8Alias *a=b8_alias(chara);if(a!=NULL)chara=a->chara;if(chara<0||language<0||language>1)return -1;
 load_voices();
 for(i=0;i<sVoiceCount;i++)if((int)sVoices[i].chara==chara){if((int)sVoices[i].language==language)return (int)(0x40000000u|sVoices[i].transport);alternate=i;}
 if(alternate>=0){if(chara<1024&&!sVoiceWarned[chara])fprintf(stderr,"bt3: beta8: character %d language %d absent; using its ISO language %u bank\n",chara,language,sVoices[alternate].language);if(chara<1024)sVoiceWarned[chara]=1;return (int)(0x40000000u|sVoices[alternate].transport);}
 if(chara<1024&&!sVoiceWarned[chara])fprintf(stderr,"bt3: beta8: character %d has no verified ISO voice bank; stock compatibility voice remains\n",chara);if(chara<1024)sVoiceWarned[chara]=1;return -1;
}

int Port_Beta8ResolveFile(int chara,int costume,int damaged,int kind,int stock_id){
 int mapped=Port_Beta8FileId(chara,costume,damaged,kind);return mapped>=0?mapped:stock_id;
}
int Port_Beta8CharacterForModel(int id){
 if((unsigned)id==0x43001A90u||(unsigned)id==0x43001A91u)return 301;
 int i;uint32_t transport;if(((uint32_t)id&0xf0000000u)!=0x40000000u)return -1;
 transport=(uint32_t)id&0x0fffffffu;load_map();
 for(i=0;i<B8_MODEL_OVERRIDE_COUNT;i++)if(sModelOverrides[i].model==transport||(sModelOverrides[i].damagedModel&&sModelOverrides[i].damagedModel==transport))return sModelOverrides[i].id;
 if(transport==0x300196eu)return 29;
 for(i=0;i<sCount;i++)if(sRows[i].model==transport)return sRows[i].chara;
 return -1;
}

typedef struct B8PortraitRow {uint32_t chara,costume,transport,bytes;} B8PortraitRow;
static B8PortraitRow *sPortraits;static int sPortraitCount=-1;
static void load_portraits(void){
 char path[600];FILE *fp;uint32_t h[4],i;
 if(sPortraitCount>=0)return;sPortraitCount=0;
 snprintf(path,sizeof(path),"%s/beta8/portrait-map.bin",root_dir());fp=fopen(path,"rb");if(!fp)return;
 if(fread(h,1,16,fp)!=16||memcmp(h,"B8PM",4)||h[1]!=1||h[2]>4096||h[3]!=16){fclose(fp);return;}
 sPortraits=(B8PortraitRow*)malloc(h[2]*sizeof(B8PortraitRow));if(!sPortraits){fclose(fp);return;}
 for(i=0;i<h[2];i++){
  B8PortraitRow row;uint32_t volume,entry;
  if(fread(&row,1,16,fp)!=16)goto bad;
  volume=row.transport>>24;entry=row.transport&0xffffff;
  if(row.chara>290||row.costume>7||(volume!=3&&volume!=4)||entry>=(volume==3?6440u:5796u)||row.bytes<8||row.bytes>0x16800)goto bad;
  sPortraits[i]=row;
 }
 if(fgetc(fp)!=EOF)goto bad;
 fclose(fp);sPortraitCount=h[2];fprintf(stderr,"bt3: beta8: %d original costume portraits\n",sPortraitCount);return;
bad:free(sPortraits);sPortraits=NULL;fclose(fp);
}

/* Validate native resource file sizes before returning a custom transport.
   Portraits are ordinary BPE resources, never copied into relocated memory. */
static int b8_custom_index(int chara) {
 static const int ids[]={212,214,215,216,218,219,220,221,222,223,224,225,226,229,230,234,235,238,239,241,244,213,245,247};int i;
 for(i=0;i<(int)(sizeof(ids)/sizeof(ids[0]));i++)if(ids[i]==chara)return i;
 return -1;
}
static int b8_mod_resource(int entry,int portrait) {
 char path[700];FILE *fp;long n;uint32_t h[2];int ok;
 snprintf(path,sizeof(path),"%s/mods/pak3us3/%05d.bin",root_dir(),entry);
 fp=fopen(path,"rb");if(!fp)return 0;
 if(fseek(fp,0,SEEK_END)!=0){fclose(fp);return 0;}n=ftell(fp);rewind(fp);
 ok=fread(h,1,8,fp)==8;fclose(fp);
 if(!ok)return 0;
 if(portrait)return n>8&&n<=0x20800&&h[0]==0x20560&&h[1]==(uint32_t)n-8;
 return n==3424&&h[0]==1&&h[1]==8;
}
static int b8_custom_portrait(int chara) {
 static const int ids[]={224,225,226,229,230,234,235,238,239,241};static signed char valid[10];int i;
 for(i=0;i<10;i++)if(ids[i]==chara){
  if(!valid[i])valid[i]=b8_mod_resource(6520+i,1)?1:-1;
  return valid[i]>0?(int)(0x43000000u+(unsigned)(6520+i)):-1;
 }return -1;
}

int Port_Beta8PortraitFileId(int chara,int costume){
 if(chara==301)return (int)(0x43000000u+6804u);
 b8_profile_route(&chara,&costume);
 /* Pan's promoted roster id owns the model, but her large portrait is the
    stock Pan face. Keep this mapping here so every consumer (entry select,
    tournament cards, versus/team select and battle panels) agrees. */
 if(chara==186)chara=63;
 if(chara==213&&b8_mod_resource(6702,1))return (int)0x43001A2Eu;
 if(chara==245&&b8_mod_resource(6703,1))return (int)0x43001A2Fu;
 if(chara==247&&b8_mod_resource(6704,1))return (int)0x43001A30u;
 /* New normal Vegetto SSJ4 only; costume1 and logical223 retain GT art. */
 if(chara==59&&costume==0&&b8_mod_resource(6551,1))return (int)(0x43000000u+6551u);
 int custom=b8_custom_portrait(chara);if(custom>=0)return custom;
 int i,fallback=-1;const B8Alias *a=b8_alias(chara);if(a!=NULL){costume=a->id==186?costume:a->costume;chara=a->chara;}costume=b8_costume_alias(chara,costume);if(chara<0||chara>290||costume<0||costume>7)return -1;load_portraits();
 for(i=0;i<sPortraitCount;i++)if((int)sPortraits[i].chara==chara){if((int)sPortraits[i].costume==costume)return (int)(0x40000000u|sPortraits[i].transport);if(!sPortraits[i].costume)fallback=i;}
 return fallback>=0?(int)(0x40000000u|sPortraits[fallback].transport):-1;
}
int Port_Beta8PortraitResolve(int chara,int costume,int legacy){int id=Port_Beta8PortraitFileId(chara,costume);return id>=0?id:legacy;}

int Port_Beta8HasCharacter(int chara){
 if(chara==301)return 1;
 int i;
 /* The separated entries (Yamu, Gokhan, Vegetto GT) and their split forms are aliases of stock
    characters' costumes. Their bundle lookup maps below the added-range check, so without this the
    grid would mark their cells locked (the "Z" card) even though the models resolve. */
 if(b8_alias(chara)!=NULL)return 1;
 if(chara<165||chara>290)return 0;load_map();for(i=0;i<sCount;i++)if((int)sRows[i].chara==chara&&!sRows[i].damaged)return 1;return 0;}

/* The character's BtlParam blocks: the transformation tables of the mod's roster, used to tell a select form
   (a transformation target) from a character that is DLC of its own. */
static unsigned char *sParams;
static int sParamCount = -1;
static void load_params(void) {
    char path[600];
    FILE *fp;
    long size;

    if (sParamCount >= 0) {
        return;
    }
    sParamCount = 0;
    snprintf(path, sizeof(path), "%s/beta8/btlparam.bin", root_dir());
    fp = fopen(path, "rb");
    if (fp == NULL) {
        return;
    }
    fseek(fp, 0, SEEK_END);
    size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (size <= 0 || size % 256 != 0 || size > 256 * 4096) {
        fclose(fp);
        return;
    }
    sParams = (unsigned char *)malloc((size_t)size);
    if (sParams == NULL) {
        fclose(fp);
        return;
    }
    if (fread(sParams, 1, (size_t)size, fp) != (size_t)size) {
        free(sParams);
        sParams = NULL;
        fclose(fp);
        return;
    }
    fclose(fp);
    sParamCount = (int)(size / 256);
}

int Port_Beta8SizeClass(int chara,int costume){
 b8_profile_route(&chara,&costume);
 int i,pass;const B8Alias *a=b8_alias(chara);if(a!=NULL){costume=a->id==186?costume:a->costume;chara=a->chara;}costume=b8_costume_alias(chara,costume);load_map();load_params();
 for(pass=0;pass<2;pass++)for(i=0;i<sCount;i++)if((int)sRows[i].chara==chara&&sRows[i].costume==(pass?0:costume)&&!sRows[i].damaged&&(int)sRows[i].param<sParamCount)return (signed char)sParams[sRows[i].param*256+2];
 return -1;
}
int Port_Beta8HasParam(int chara) {
    int i;
    int costume = 0;
    const B8Alias *a = b8_alias(chara);

    if (a != NULL) {
        chara = a->chara;
        costume = a->costume;
    }
    load_map();
    load_params();
    if (sParamCount <= 0) {
        return 0;
    }
    for (i = 0; i < sCount; i++) {
        if ((int)sRows[i].chara == chara && (int)sRows[i].costume == costume && !sRows[i].damaged) {
            return (int)sRows[i].param < sParamCount;
        }
    }
    return 0;
}

int Port_Beta8IsTransform(int source, int target) {
    if(source==95&&target==295)return 1;
    if(source==97&&target==296)return 1;
    if(source>=296&&source<=299)return target==97||(source<299&&target==source+1);
    if(source==213)return target==245;
    if(source==245)return target==213;
    int i;
    int costume = 0;
    const unsigned char *b;
    const B8Alias *a = b8_alias(source);

    if (a != NULL) {
        source = a->chara;
        costume = a->costume;
    }
    load_map();
    load_params();
    if (sParamCount <= 0) {
        return 0;
    }
    for (i = 0; i < sCount; i++) {
        if ((int)sRows[i].chara == source && (int)sRows[i].costume == costume && !sRows[i].damaged) {
            if ((int)sRows[i].param >= sParamCount) {
                return 0;
            }
            b = sParams + sRows[i].param * 256;
            return b[0x98] == target || b[0x99] == target || b[0x9A] == target || b[0x9B] == target;
        }
    }
    return 0;
}

/* Whether any character in the roster transforms into `target`: the target is a form, not a DLC character of
   its own, wherever the select table groups it. */
int Port_Beta8IsAnyTransform(int target) {
    if(target>=295&&target<=299)return 1;
    if(Port_Beta8SelectFormOwner(target)>=0)return 1;
    static const int sExplicitForms[] = {
        199,243,244,245,216,218,219,220,221,222,223,225,226,229,230,235,238,239,241,
        281,282,283,284,285,286,287,288,289,290
    };
    int i, j;
    for(i=0;i<(int)(sizeof(sExplicitForms)/sizeof(sExplicitForms[0]));i++)if(target==sExplicitForms[i])return 1;

    load_map();
    load_params();
    if (sParamCount <= 0) {
        return 0;
    }
    for (i = 0; i < sCount; i++) {
        const unsigned char *b;

        if (sRows[i].costume != 0 || sRows[i].damaged != 0 || (int)sRows[i].param >= sParamCount) {
            continue;
        }
        b = sParams + sRows[i].param * 256;
        for (j = 0; j < 4; j++) {
            if (b[0x98 + j] == target) {
                return 1;
            }
        }
    }
    return 0;
}

static B8PortraitRow *sMembers;static int sMemberCount=-1;
int Port_Beta8MemberFileId(int chara,int side){
 if(chara==301&&side>=0&&side<=1)return (int)(0x43000000u+6807u+(unsigned)side);
 int custom=b8_custom_index(chara);if(custom>=0&&side>=0&&side<2){int entry=6560+custom*2+side;if(b8_mod_resource(entry,0))return (int)(0x43000000u+(unsigned)entry);}
 int i;const B8Alias *a=b8_alias(chara);if(a!=NULL)chara=a->chara;if(chara<165||chara>290||side<0||side>1)return -1;
 if(sMemberCount<0){
  char path[600];FILE *fp;uint32_t h[4],n;sMemberCount=0;
  snprintf(path,sizeof(path),"%s/beta8/member-map.bin",root_dir());fp=fopen(path,"rb");if(!fp)return -1;
  if(fread(h,1,16,fp)!=16||memcmp(h,"B8MM",4)||h[1]!=1||h[2]>582||h[3]!=16){fclose(fp);return -1;}
  sMembers=(B8PortraitRow*)malloc(h[2]*sizeof(B8PortraitRow));if(!sMembers){fclose(fp);return -1;}
  for(n=0;n<h[2];n++){
   B8PortraitRow row;
   if(fread(&row,1,16,fp)!=16||row.chara<165||row.chara>290||row.costume>1||(row.transport>>24)!=4||(row.transport&0xffffff)>=5796||row.bytes<8||row.bytes>0x1000){free(sMembers);sMembers=NULL;fclose(fp);return -1;}
   sMembers[n]=row;
  }
  if(fgetc(fp)!=EOF){free(sMembers);sMembers=NULL;fclose(fp);return -1;}
  fclose(fp);sMemberCount=h[2];
 }
 for(i=0;i<sMemberCount;i++)if((int)sMembers[i].chara==chara&&(int)sMembers[i].costume==side)return (int)(0x40000000u|sMembers[i].transport);
 return -1;
}

/* These bundles enter game-owned 32-bit pointer fields. Host malloc is unsafe on native64. */
extern void *Port_LowAlloc(size_t);
extern void Port_LowFree(void *);
static void *b8_game_bundle_alloc(size_t n){return Port_LowAlloc(n);}
static void b8_game_bundle_free(void *p){if(p)Port_LowFree(p);}

/* Exact source model face bundles, exported once; ownership stays with the menu cache. */
void *Port_Beta8LoadSmallPortrait(int chara,int costume){
 char path[700];FILE *fp;unsigned char *b;uint32_t h[3];int i;const B8Alias *a=b8_alias(chara);

 if((chara>=291&&chara<=301)||b8_custom_index(chara)>=0||(chara==59&&costume==0)){
  snprintf(path,sizeof(path),"%s/beta8/custom-small-portraits/%03d.bin",root_dir(),chara);
  fp=fopen(path,"rb");if(fp){b=(unsigned char*)b8_game_bundle_alloc(0x2ac0);if(!b){fclose(fp);return NULL;}
   if(fread(b,1,0x2ac0,fp)==0x2ac0&&fgetc(fp)==EOF){memcpy(h,b,12);if(h[0]==2&&h[1]==8&&h[2]==32){fclose(fp);return b;}}
   b8_game_bundle_free(b);fclose(fp);
  }
 }
 if(a!=NULL){costume=a->id==186?costume:a->costume;chara=a->chara;}costume=b8_costume_alias(chara,costume);
 if(chara<165||chara>290||costume<0||costume>7)return NULL;
 for(i=0;i<2;i++){
  snprintf(path,sizeof(path),"%s/beta8/small-portraits/%03d-%d.bin",root_dir(),chara,i?0:costume);
  fp=fopen(path,"rb");if(!fp){if(!costume)return NULL;continue;}
  b=(unsigned char*)b8_game_bundle_alloc(0x2ac0);if(!b){fclose(fp);return NULL;}
  if(fread(b,1,0x2ac0,fp)!=0x2ac0||fgetc(fp)!=EOF){b8_game_bundle_free(b);fclose(fp);return NULL;}
  fclose(fp);memcpy(h,b,12);
  if(h[0]!=2||h[1]!=8||h[2]!=32){b8_game_bundle_free(b);return NULL;}
  return b;
 }
 return NULL;
}
void Port_Beta8FreeSmallPortrait(void *data){b8_game_bundle_free(data);}

/* Exact 64x64 squared chip bundles authored from the large portraits. The stock 212 template is the
   MTexRes/PSMT8 face chip; the menu frame is separate geometry. The split forms use their base entry's
   chip until their own art is authored (the form's large portrait already comes from the stock costume). */
void *Port_Beta8LoadChipBundle(int chara){
 char path[700];FILE *fp;unsigned char *b;
 if(chara<0||chara>301)return NULL;
 snprintf(path,sizeof(path),"%s/beta8/chips/%03d.bin",root_dir(),chara);
 fp=fopen(path,"rb");
 if(fp==NULL){
  int base=-1;
  if(chara==216||chara==218||chara==219)base=214;
  else if(chara>=220&&chara<=223)base=215;
  else if(chara==224)base=110;
  else if(chara==225||chara==230)base=54;
  else if(chara==226)base=249;
  else if(chara==229)base=172;
  else if(chara==234)base=51;
  else if(chara==235||chara==241)base=59;
  else if(chara==238)base=166;
  else if(chara==239)base=149;
  else if(chara==242)base=29;
  else if(chara==244)base=122;
  if(base<0)return NULL;
  snprintf(path,sizeof(path),"%s/beta8/chips/%03d.bin",root_dir(),base);
  fp=fopen(path,"rb");if(fp==NULL)return NULL;
 }
 b=(unsigned char*)b8_game_bundle_alloc(0x1560);if(!b){fclose(fp);return NULL;}
 if(fread(b,1,0x1560,fp)!=0x1560||fgetc(fp)!=EOF){b8_game_bundle_free(b);fclose(fp);return NULL;}
 fclose(fp);return b;
}
void Port_Beta8FreeChipBundle(void *data){b8_game_bundle_free(data);}

/* The stock character an alias resolves to, or -1. The battle loader uses it for the stock voice bank
   and the stock member-face file, which only exist for ids 0..160. */
int Port_Beta8StockAlias(int chara) {
    if(chara==301)return 60; /* Stock table compatibility only; model/voice/face routes are private. */
    if(chara==245)return 134; /* Oceanus retains a female voice; model animation remains Nuova. */
    const B8Alias *a = b8_alias(chara);

    return a != NULL && a->chara < 165 ? a->chara : -1;
}

/* An id the port can actually field: a bundle character (165..290) or an alias entry whose donor is a
   stock character. Aliases hold no bundle row, so Port_Beta8HasCharacter alone reports them missing. */
int Port_Beta8HasEntry(int chara) {
    return Port_Beta8HasCharacter(chara) || Port_Beta8StockAlias(chara) >= 0;
}

/* The form entries of a split cell (the base entry first), or 0 for a character without one. */
int Port_Beta8FormList(int id, int *out, int max) {
    static const int frieza[]={91,92,93,94,95,295},cold[]={97,296,297,298,299};
    static const int sGokhanForms[] = { 214, 216, 218, 219 };
    static const int sGtForms[] = { 215, 220, 221, 222, 223 };
    static const int sGogettoForms[] = { 224, 225, 226, 229, 230 };
    static const int sGogettoPotaraForms[] = { 234, 235, 238, 239, 241 };
    static const int sShallotForms[] = { 178, 282, 283 };
    static const int sGohanForms[] = { 182, 284 };
    static const int sFutureGohanForms[] = { 163, 281 };
    static const int sBabyForms[] = { 285 };
    static const int sBardockForms[] = { 73, 176, 199 };
    static const int sKidGohanForms[] = { 13, 284 };
    static const int sRilldForms[] = { 269, 286, 290 };
    static const int sGomahForms[] = { 260, 287, 289 };
    const int *src = NULL;
    int n = 0;
    int i;

    if(id==91){src=frieza;n=6;}else if(id==97){src=cold;n=5;}
    else if(id==291){static const int mv[]={291,292,293,294};src=mv;n=4;} else if (id == 213) {
        static const int oceanus[] = {213,245}; src=oceanus; n=2;
    } else if (id == 214) {
        src = sGokhanForms;
        n = (int)(sizeof(sGokhanForms) / sizeof(sGokhanForms[0]));
    } else if (id == 215) {
        src = sGtForms;
        n = (int)(sizeof(sGtForms) / sizeof(sGtForms[0]));
    } else if (id == 224) {
        src = sGogettoForms;
        n = (int)(sizeof(sGogettoForms) / sizeof(sGogettoForms[0]));
    } else if (id == 234) {
        src = sGogettoPotaraForms;
        n = (int)(sizeof(sGogettoPotaraForms) / sizeof(sGogettoPotaraForms[0]));
    } else if (id == 178) {
        src = sShallotForms; n = (int)(sizeof(sShallotForms) / sizeof(sShallotForms[0]));
    } else if (id == 182) {
        src = sGohanForms; n = (int)(sizeof(sGohanForms) / sizeof(sGohanForms[0]));
    } else if (id == 163) {
        src = sFutureGohanForms; n = (int)(sizeof(sFutureGohanForms) / sizeof(sFutureGohanForms[0]));
    } else if (id == 73) {
        src=sBardockForms; n=3;
    } else if (id == 13) {
        src=sKidGohanForms; n=2;
    } else if (id == 285) {
        src = sBabyForms; n = (int)(sizeof(sBabyForms) / sizeof(sBabyForms[0]));
    } else if (id == 269) {
        src = sRilldForms; n = (int)(sizeof(sRilldForms) / sizeof(sRilldForms[0]));
    } else if (id == 260) {
        src = sGomahForms; n = (int)(sizeof(sGomahForms) / sizeof(sGomahForms[0]));
    }
    if (src == NULL || out == NULL || max <= 0) {
        return 0;
    }
    if (n > max) {
        n = max;
    }
    for (i = 0; i < n; i++) {
        out[i] = src[i];
    }
    return n;
}

/* The entry a transformation of `source` really loads: the split forms own their target costume, so the
   stock target ids (52/166/149/59/51) become the separated form entries. */
/* The form chains written into the live BtlParam of the split entries at model load. The stock
   blocks either share the base character's targets or have no upward slots at all, so the
   transformation input, the AI and the skill page all read the split entries from here. */
typedef struct B8FormParam {
    int id;
    int target[4];
} B8FormParam;

static const B8FormParam sFormParams[] = {
    {95,{96,255,255,255}},{295,{255,255,255,255}},
    {97,{126,255,255,255}},{296,{127,255,255,97}},{297,{128,255,255,97}},
    {298,{96,255,255,97}},{299,{255,255,255,97}},
    {291,{115,255,255,255}},{292,{116,255,255,255}},{293,{117,255,255,255}},{294,{255,255,255,255}},
    {163,{18,255,255,255}},{281,{255,255,255,17}},
    {178,{42,43,255,255}},{282,{43,255,255,41}},{283,{255,255,255,41}},
    {182,{47,255,255,255}},{284,{255,255,255,46}},
    {269,{136,255,255,255}},{286,{153,255,255,135}},{290,{255,255,255,135}},
    {260,{142,143,255,255}},{287,{143,255,255,118}},{289,{255,255,255,118}},
    { 213, { 245, 255, 255, 255 } },
    { 245, { 255, 255, 255, 213 } },
    { 247, { 255, 255, 255, 255 } },
    { 214, { 216, 218, 219, 255 } },
    { 216, { 218, 219, 214, 255 } },
    { 218, { 219, 214, 255, 255 } },
    { 219, { 214, 255, 255, 255 } },
    { 215, { 220, 221, 222, 223 } },
    { 220, { 221, 222, 215, 255 } },
    { 221, { 222, 223, 215, 255 } },
    { 222, { 223, 215, 255, 255 } },
    { 223, { 215, 255, 255, 255 } },
    { 224, { 225, 226, 229, 230 } },
    { 225, { 226, 229, 230, 224 } },
    { 226, { 229, 230, 255, 224 } },
    { 229, { 230, 255, 255, 224 } },
    { 230, { 255, 255, 255, 224 } },
    { 234, { 235, 238, 239, 241 } },
    { 235, { 238, 239, 241, 234 } },
    { 238, { 239, 241, 255, 234 } },
    { 239, { 241, 255, 255, 234 } },
    { 241, { 255, 255, 255, 234 } },
    { 244, { 255, 255, 255, 29 } },
    { 51, { 52, 166, 149, 59 } },
    { 59, { 255, 255, 255, 51 } },
    { 149, {  59, 255, 255,  51 } }, /* Vegetto SSJ3 -> SSJ4, back to base */
    { 172, {  54, 255, 255, 110 } }, /* Gogeta SSJ3 -> SSJ4, back to base */
    /* Juan 2026-10-27: Gogeta could only reach SSJ4 from SSJ3: the family's other rows were never
       listed, so they kept the stock blocks with no upward slot. Every form lists 54 now, exactly the
       way the Gogetto family's rows all list 230; costs follow the same rule (top form = one blast). */
    { 110, {  53, 249, 172,  54 } },
    {  53, { 249, 172,  54, 110 } },
    { 249, { 172,  54, 255, 110 } },
    {  54, { 255, 255, 255, 110 } }  /* Gogeta SSJ4 -> back to base, like 59/230 */
};
#define B8_FORM_PARAM_COUNT ((int)(sizeof(sFormParams) / sizeof(sFormParams[0])))

typedef struct {int id;unsigned char seq[4],kind[4],obj[4];} B8TransformBits;
static const B8TransformBits sRound2TransformBits[]={
 {163,{0,0,0,0},{2,0,0,0},{255,255,255,255}},
 {281,{0,0,0,1},{0,0,0,1},{255,255,255,255}},
 {178,{0,0,0,0},{2,2,0,0},{255,255,255,255}},
 {282,{0,0,0,1},{2,0,0,1},{255,255,255,255}},
 {283,{0,0,0,1},{0,0,0,1},{255,255,255,255}},
 {182,{0,0,0,0},{2,0,0,0},{255,255,255,255}},
 {284,{0,0,0,1},{0,0,0,1},{255,255,255,255}},
 {269,{0,0,0,0},{2,0,0,0},{255,255,255,255}},
 {286,{0,0,0,1},{2,0,0,1},{255,255,255,255}},
 {290,{0,0,0,1},{0,0,0,1},{255,255,255,255}},
 {260,{0,0,0,0},{2,2,0,0},{255,255,255,255}},
 {287,{0,0,0,1},{2,0,0,1},{255,255,255,255}},
 {289,{0,0,0,1},{0,0,0,1},{255,255,255,255}},
 {213,{0,0,0,0},{2,0,0,0},{255,255,255,255}},
 {245,{0,0,0,1},{0,0,0,1},{255,255,255,255}},
 {224,{0,0,0,0},{2,2,2,2},{255,255,255,255}},
 {225,{0,0,0,1},{2,2,2,1},{255,255,255,255}},
 {226,{0,0,0,1},{2,2,0,1},{255,255,255,255}},
 {229,{0,0,0,1},{2,0,0,1},{255,255,255,255}},
 {230,{0,0,0,1},{0,0,0,1},{255,255,255,255}},
 {234,{0,0,0,0},{2,2,2,2},{255,255,255,255}},
 {235,{0,0,0,1},{2,2,2,1},{255,255,255,255}},
 {238,{0,0,0,1},{2,2,0,1},{255,255,255,255}},
 {239,{0,0,0,1},{2,0,0,1},{255,255,255,255}},
 {241,{0,0,0,1},{0,0,0,1},{255,255,255,255}},
 {214,{0,0,0,0},{2,2,2,1},{255,255,255,255}},
 {216,{0,0,1,0},{2,2,1,1},{255,255,255,255}},
 {218,{0,1,0,0},{2,1,1,1},{255,255,255,255}},
 {219,{1,0,0,0},{1,1,1,1},{255,255,255,255}},
 {215,{0,0,0,0},{2,2,2,2},{255,255,255,255}},
 {220,{0,0,1,0},{2,2,1,1},{255,255,255,255}},
 {221,{0,0,1,0},{2,2,1,1},{255,255,255,255}},
 {222,{0,1,0,0},{2,1,1,1},{255,255,255,255}},
 {223,{1,0,0,0},{1,1,1,1},{255,255,255,255}},
 {244,{0,0,0,1},{0,0,0,1},{255,255,255,255}},
 {51,{0,0,0,0},{2,2,2,2},{255,255,255,255}},
 {59,{0,0,0,1},{0,0,0,1},{255,255,255,255}},
 {149,{0,0,0,1},{2,1,1,1},{255,255,255,255}},
 {172,{0,0,0,1},{2,1,1,1},{255,255,255,255}},
 {110,{0,0,0,0},{2,2,2,2},{255,255,255,255}},
 {53,{0,0,0,1},{2,2,2,1},{255,255,255,255}},
 {249,{0,0,0,1},{2,2,0,1},{255,255,255,255}},
 {54,{0,0,0,1},{0,0,0,1},{255,255,255,255}},
 /* Gogeta family (Juan 2026-10-27): the rows above give 110/53/249 their SSJ4 in the first free
    slot and 54 the back-to-base link, using the Gogetto family's own seq/kind convention. */
};
int Port_Beta8PatchFormParam(int chara, void *param) {
    unsigned char *p = (unsigned char *)param;
    int i, j;

    if (param == NULL) {
        return 0;
    }
    if(chara==146)for(j=0;j<4;j++)if(p[0x98+j]==147&&p[0x9c+j]<1)p[0x9c+j]=1;
    for (i = 0; i < B8_FORM_PARAM_COUNT; i++) {
        if (sFormParams[i].id != chara) {
            continue;
        }
        for (j = 0; j < 4; j++) {
            int t = sFormParams[i].target[j];

            p[0x98 + j] = (unsigned char)t;
            /* The top form (SSJ4) costs one blast gauge; the rest are free. */
            if(!(chara==163||chara==178||chara==182||chara>=260))
            p[0x9C + j] = (unsigned char)((t ==245 || t ==147 || t == 59 || t == 54 || t == 223 || t == 230 || t == 241 || t == 244) ? 1 : 0);
            { int k;for(k=0;k<(int)(sizeof(sRound2TransformBits)/sizeof(sRound2TransformBits[0]));k++)if(sRound2TransformBits[k].id==chara){
                p[0xA0+j]=sRound2TransformBits[k].seq[j];p[0xA4+j]=sRound2TransformBits[k].kind[j];p[0xA8+j]=sRound2TransformBits[k].obj[j];break;
            }}
        }
        if(chara==224||chara==234){p[0]=1;p[3]=5;p[0xAD]=0;}else p[3]=0;
        p[0xAC] = (unsigned char)((chara==59||chara==54||chara==230||chara==241||chara==244||chara==245||chara==247)?255:0);
        if((chara>=291&&chara<=299)||chara==95||chara==97){
            p[0xAC]=chara==294?255:0;
            for(j=0;j<4;j++){int t=sFormParams[i].target[j];p[0x9c+j]=(t!=255&&j!=3)?1:0;p[0xa0+j]=j==3?1:0;p[0xa4+j]=j==3?1:2;p[0xa8+j]=255;}
        }
        /* Native Gohan SSJ2/Goku SSJ3 lightning is flags0 bit4/8;
           retain item ability words and unrelated parameter flags. */
        {int form=0;switch(chara){case 224:case 234:form=1;break;case 225:case 235:form=2;break;
         case 226:case 238:form=4;break;case 229:case 239:case 230:case 241:case 244:form=8;break;}
         if(form)p[0]=(p[0]&0xf0)|form;}
        return 1;
    }
    return 0;
}

int Port_Beta8FormTarget(int source, int target) {
    int i;

    for (i = 0; i < B8_FORM_TARGET_COUNT; i++) {
        if (sFormTargets[i].source == source && sFormTargets[i].target == target) {
            return sFormTargets[i].mapped;
        }
    }
    return target;
}

/* A stock SSJ3 with no upward slot (149 Vegetto / 172 Gogeta, costume 1) gets its SSJ4 on the first
   free slot, so the transformation input reaches it. */
int Port_Beta8FormSlotTarget(int source, int slot, int target) {
    if (target == 0xFF) {
        if (source == 149 && slot == 0) {
            return 59;
        }
        if (source == 172 && slot == 0) {
            return 54;
        }
    }
    return Port_Beta8FormTarget(source, target);
}

/* The entry a fusion really produces. Gokhan is the Goku (Fin) costume 3 + Gohan recipe and Vegetto GT
   the GT base pair; every other result keeps its stock id. `scost`/`pcost` are the UI costume indexes. */
int Port_Beta8FusionResult(int source, int scost, int partner, int pcost, int result) {
    if (((source==3 && partner==23)||(source==23 && partner==3)) && result==51) return 247;
    (void)scost;
    (void)pcost;
    if (((source == 52 && partner == 53) || (source == 53 && partner == 52))) {
        /* Fusion of fusions: the two SSJ1 results are the Gogetto SSJ1 entries, not the base cells. */
        if (result == 54) {
            return 225;
        }
        if (result == 59) {
            return 235;
        }
    }
    if (result == 51) {
        if ((source == 3 && partner == 21) || (source == 21 && partner == 3)) {
            return 214;
        }
    }
    return result;
}


int Port_Beta8AppendSelectForms(int chara,int *forms,int count,int capacity){
 int n=count;B8ExploradorFormsRule(chara,forms,&n,capacity);return n;
}
extern void Port_TextPatch(void *section,int line,const char *name);
/* One spelling/number convention across native menus, modern select, HUD and skill lists. */
const char *Port_Beta8DragonName(int chara){
 switch(chara){
 case 146:return "Syn Shenron (1-star)";
 case 147:return "Omega Shenron (1-star)";
 case 261:return "Haze Shenron (2-star)";
 case 76:return "Eis Shenron (3-star)";
 case 145:return "Neo Shenron (4-star)";
 case 275:return "Rage Shenron (5-star)";
 case 213:return "Oceanus Shenron (6-star)";
 case 245:return "Oceanus Shenron (6-star) Dragon";
 case 276:return "Naturon Shenron (7-star)";
 default:return NULL;
 }
}
void Port_Beta8PatchDragonNames(void*section){
 static const int ids[]={146,147,261,76,145,275,213,245,276};unsigned i;if(!section)return;
 for(i=0;i<sizeof(ids)/sizeof(ids[0]);i++)Port_TextPatch(section,ids[i],Port_Beta8DragonName(ids[i]));
}
void Port_Beta8PatchPowerText(void *section){
 unsigned i;if(!section)return;for(i=0;i<sizeof(b8PowerTextCandidate)/sizeof(b8PowerTextCandidate[0]);i++)
 Port_TextPatch(section,b8PowerTextCandidate[i].chara,b8PowerTextCandidate[i].text);
}
typedef struct {int id;int row[4];} B8MemberStats;
static const B8MemberStats sRound2Stats[]={
 {261,{400000,100000,100000,6}},
 {275,{400000,100000,100000,6}},
 {276,{400000,100000,100000,6}},
 {213,{225000,100000,100000,6}},
 {245,{450000,100000,100000,6}},
 {224,{250000,100000,100000,6}},
 {225,{450000,100000,100000,7}},
 {226,{450000,100000,100000,7}},
 {229,{450000,100000,100000,7}},
 {230,{450000,100000,100000,8}},
 {234,{350000,100000,100000,6}},
 {235,{450000,100000,100000,7}},
 {238,{450000,100000,100000,7}},
 {239,{450000,100000,100000,7}},
 {241,{450000,100000,100000,8}},
 {214,{350000,100000,100000,6}},
 {215,{350000,100000,100000,6}},
 {216,{450000,100000,100000,7}},
 {218,{450000,100000,100000,7}},
 {219,{450000,100000,100000,6}},
 {220,{450000,100000,100000,7}},
 {221,{450000,100000,100000,7}},
 {222,{450000,100000,100000,6}},
 {223,{450000,100000,100000,6}},
 {244,{80000,60000,100000,6}},
};
int Port_Beta8UsesMemberStats(int chara){unsigned i;for(i=0;i<sizeof(sRound2Stats)/sizeof(sRound2Stats[0]);i++)if(sRound2Stats[i].id==chara)return 1;return 0;}
void Port_Beta8ApplyMemberStats(void *rows,int count){
 unsigned i;if(!rows||count<0||count>4096)return;
 for(i=0;i<sizeof(sRound2Stats)/sizeof(sRound2Stats[0]);i++)if(sRound2Stats[i].id<count)
 memcpy((unsigned char*)rows+sRound2Stats[i].id*16,sRound2Stats[i].row,16);
}
int Port_Beta8EffectTier(int chara){
 if(chara==60||chara==229||chara==239||chara==230||chara==241||chara==244)return 2;
 switch(chara){case 225:case 226:case 235:case 238:return 1;default:return 0;}
}
float Port_Beta8AuraMultiplier(int chara){int tier=Port_Beta8EffectTier(chara);return chara==230||chara==241||chara==244?1.50f:tier==2?1.35f:tier==1?1.20f:1.0f;}
/* Logical identity is needed for244: its model otherwise identifies as its stock alias. */
static int sSsj4Logical[12];
static unsigned sSsj4TraceCount;
int Port_Beta8Ssj4Object(int obj){return obj>=0&&obj<12?sSsj4Logical[obj]:0;}
void Port_Beta8Ssj4Load(int obj,int chara,void*param){
 unsigned char*p=(unsigned char*)param;int before;FILE*f;
 if(obj<0||obj>=12)return;sSsj4Logical[obj]=(chara==230||chara==241||chara==244)?chara:0;
 if(!sSsj4Logical[obj]||!p)return;before=p[0]|(p[1]<<8);p[0]=(p[0]&0xf0)|8;
 if(sSsj4TraceCount++<128){f=fopen("bt3_ssj4_lightning.log","a");if(f){fprintf(f,"ssj4-bind object=%d logical=%d flags_before=%04x flags_after=%04x\n",obj,chara,before,p[0]|(p[1]<<8));fclose(f);}}
}
void Port_Beta8Ssj4TraceAura(int obj,int flags){
 static int last[12],logical[12];int c=Port_Beta8Ssj4Object(obj);FILE*f;
 if(!c||sSsj4TraceCount>=128)return;if(last[obj]==flags&&logical[obj]==c)return;last[obj]=flags;logical[obj]=c;sSsj4TraceCount++;
 f=fopen("bt3_ssj4_lightning.log","a");if(f){fprintf(f,"ssj4-aura object=%d logical=%d actual_flags=%04x enable_requested=%d\n",obj,c,flags,(flags&12)!=0);fclose(f);}
}
static struct {int obj,chara;float amount;} sRound3Charge[12];
void Port_Beta8ChargeReset(void){memset(sRound3Charge,0,sizeof(sRound3Charge));memset(sSsj4Logical,0,sizeof(sSsj4Logical));}
float Port_Beta8ChargeUpdate(int player,int obj,int chara,int charging){
 extern int Port_NetActive(void),BattleReplay_IsActive(void);float *v;
 if(player<0||player>=12)return 0;
 if(Port_NetActive()||BattleReplay_IsActive()||(!Port_Beta8EffectTier(chara)&&chara!=224&&chara!=234)){sRound3Charge[player].amount=0;return 0;}
 if(sRound3Charge[player].obj!=obj||sRound3Charge[player].chara!=chara)sRound3Charge[player].amount=0;
 sRound3Charge[player].obj=obj;sRound3Charge[player].chara=chara;v=&sRound3Charge[player].amount;
 *v+=charging?.12f:-.18f;if(*v<0)*v=0;if(*v>1)*v=1;return *v;
}
float Port_Beta8ChargeScale(int obj){int i;for(i=0;i<12;i++)if(sRound3Charge[i].obj==obj&&sRound3Charge[i].amount>0){
 int c=sRound3Charge[i].chara;float add=Port_Beta8EffectTier(c)==2?.9f:Port_Beta8EffectTier(c)==1?.7f:.5f;
 return 1+sRound3Charge[i].amount*add;}return 1;}


/* Optional, bounded text-only evidence. Never invalidates textures or expressions. */
static unsigned sB8TraceEpoch,sB8TraceBinds;
static const char*b8_trace_path(void){static const char*path=(const char*)-1;const char*p=__atomic_load_n(&path,__ATOMIC_ACQUIRE);if(p==(const char*)-1){p=getenv("BT3_ROUND3_TRACE");__atomic_store_n(&path,p,__ATOMIC_RELEASE);}return p&&p[0]?p:NULL;}
static unsigned b8_trace_hash(const unsigned char*p,unsigned n){unsigned h=2166136261u,i;for(i=0;i<n;i++)h=(h^p[i])*16777619u;return h;}
void Port_Beta8TraceBind(int fileId,const void*buffer,int size,const void*eyes){
 const char*path=b8_trace_path();const unsigned*w=(const unsigned*)buffer;unsigned a,b,c,h12=0,h13=0,e;FILE*f;
 if(!path||!buffer||size<64||sB8TraceBinds>=64)return;
 a=w[12]&~3u;b=w[13]&~3u;c=w[14]&~3u;
 if(a<=b&&b<=c&&c<=(unsigned)size){h12=b8_trace_hash((const unsigned char*)buffer+a,b-a);h13=b8_trace_hash((const unsigned char*)buffer+b,c-b);}
 e=__atomic_add_fetch(&sB8TraceEpoch,1,__ATOMIC_RELEASE);sB8TraceBinds++;
 f=fopen(path,"ab");if(!f)return;
 fprintf(f,"bind epoch=%u file=%d bytes=%d entry12=%u:%u hash=%08x entry13=%u:%u hash=%08x eyes_current=%d\n",e,fileId,size,a,b,h12,b,c,h13,eyes==(const unsigned char*)buffer+b);fclose(f);
}
unsigned Port_Beta8TraceEpoch(void){return b8_trace_path()?__atomic_load_n(&sB8TraceEpoch,__ATOMIC_ACQUIRE):0;}
void Port_Beta8TraceTexture(unsigned epoch,unsigned long long tex0,unsigned gen,const void*pixels,unsigned bytes,int hit){
 static unsigned lastEpoch,count,total;static unsigned long long keys[64];const char*path=b8_trace_path();unsigned long long key=tex0^((unsigned long long)gen<<27)^((unsigned long long)hit<<63);unsigned i,h=0;FILE*f;
 if(!path||!epoch||total>=512)return;if(lastEpoch!=epoch){lastEpoch=epoch;count=0;}
 for(i=0;i<count;i++)if(keys[i]==key)return;if(count>=64)return;keys[count++]=key;total++;
 if(pixels&&bytes<=4*1024*1024)h=b8_trace_hash((const unsigned char*)pixels,bytes);
 f=fopen(path,"ab");if(!f)return;fprintf(f,"texture epoch=%u tex0=%016llx generation=%08x cache_hit=%d decoded_bytes=%u rgba_hash=%08x\n",epoch,tex0,gen,hit,bytes,h);fclose(f);
}

int Port_Beta8CostumeFormTarget(int source,int costume,int target){
 if(source==114&&target==145)return 274;if(source==255&&target==100)return 272;
 if(source==281&&target==17)return costume==4?159:163;
 if(source==284&&target==46)return 13;
 return Port_Beta8FormTarget(source,target);
}
int Port_Beta8FormSlotTargetForCostume(int source,int costume,int slot,int target){
 if(target==255)return Port_Beta8FormSlotTarget(source,slot,target);
 return Port_Beta8CostumeFormTarget(source,costume,target);
}
int Port_Beta8TransformCostume(int source,int costume,int target){
 if(source>=295||target>=295)return 0;
 if(source>=291&&source<=294)return 0;
 if(source==114&&target==114)return 1;
 if((source==284&&target==13)||(source==281&&target==159))return 4;
 return costume;
}
int Port_Beta8HasFormPolicy(int source,int costume){
 int i;if((source==159||source==160||source==13)&&costume==4)return 1;
 if(source==73||source==10||source==233||source==146||source==114||source==255)return 1;
 for(i=0;i<B8_FORM_PARAM_COUNT;i++)if(sFormParams[i].id==source)return 1;return 0;
}
void Port_Beta8PatchCostumeFormParam(int source,int costume,void *param){
 unsigned char*p=(unsigned char*)param;int j;if(!p)return;Port_Beta8PatchFormParam(source,param);
 if((source==159||source==160||source==13)&&costume==4){
  for(j=0;j<4;j++){p[0x98+j]=255;p[0x9c+j]=0;p[0xa0+j]=j==3;p[0xa4+j]=j==3?1:2;p[0xa8+j]=255;}
  if(source==159){p[0x98]=160;p[0xac]=0;}
  if(source==160){p[0x9b]=159;p[0xac]=3;}
  if(source==13){p[0x98]=47;p[0xac]=0;}
 }
 {extern int Port_Setting(const char*,int),Port_NetActive(void),BattleReplay_IsActive(void),Port_ScenarioActive(void);if((Port_Setting("easter_eggs",0)||Port_ScenarioActive())&&!Port_NetActive()&&!BattleReplay_IsActive()){
  int targets[2],n=0,k;if(source==114){targets[n++]=114;targets[n++]=145;}if(source==255){targets[n++]=140;targets[n++]=100;}
  for(k=0;k<n;k++){int have=0;for(j=0;j<4;j++)if(p[0x98+j]==targets[k])have=1;if(have)continue;for(j=0;j<4;j++)if(p[0x98+j]==255){p[0x98+j]=targets[k];p[0x9c+j]=1;p[0xa0+j]=0;p[0xa4+j]=2;p[0xa8+j]=255;p[0xac]=0;break;}}
 }}
 {extern int Port_StoryFormSlot(int);int t=Port_StoryFormSlot(source);
  if(t>0&&t<255){int have=0;for(j=0;j<4;j++)if(p[0x98+j]==t)have=1;
   if(!have)for(j=0;j<4;j++)if(p[0x98+j]==255){int ape=t==12||t==30||t==78||t==81||t==180;p[0x98+j]=t;p[0x9c+j]=1;p[0xa0+j]=0;p[0xa4+j]=ape?3:2;p[0xa8+j]=255;break;}}}
 if(source==10){int have=0;for(j=0;j<4;j++)if(p[0x98+j]==233)have=1;if(!have)for(j=0;j<4;j++)if(p[0x98+j]==255){p[0x98+j]=233;p[0x9c+j]=1;p[0xa0+j]=0;p[0xa4+j]=2;p[0xa8+j]=255;break;}}
 if(source==73){for(j=0;j<4;j++)if(p[0x98+j]==199)return;for(j=0;j<4;j++)if(p[0x98+j]==255){p[0x98+j]=199;p[0x9c+j]=1;p[0xa0+j]=0;p[0xa4+j]=2;p[0xa8+j]=255;break;}}
}

int Port_Beta8SelectFormOwner(int id){if(id==295)return 91;if(id>=296&&id<=299)return 97;static const int rows[][2]={{292,291},{293,291},{294,291},{180,11},{191,66},{168,152},{173,118},{231,0},{188,77},{174,75},{176,73},{175,80},{228,71},{211,82},{196,121},{165,20},{232,1},{248,131},{217,131},{171,58},{200,3},{237,34},{202,34},{250,17},{166,51},{249,110},{172,110},{169,7},{233,7},{240,122},{236,122},{246,72},{243,81},{193,74},{227,64},{201,144},{54,110},{59,51},{160,159},{163,159},{182,13},{199,73},{244,29},{281,159},{282,178},{283,178},{284,13},{286,269},{287,260},{289,260},{290,269},{216,214},{218,214},{219,214},{220,215},{221,215},{222,215},{223,215},{225,224},{226,224},{229,224},{230,224},{235,234},{238,234},{239,234},{241,234}};int i;for(i=0;i<(int)(sizeof(rows)/sizeof(rows[0]));i++)if(rows[i][0]==id)return rows[i][1];return -1;}

/* Expand only gauge rows in native memory; no original common-file bytes change. */
int *Port_Beta8MultiverseGaugeRows(int *rows,int count){
 static int extended[302*4];static const int donors[]={113,114,115,116,96,126,127,128,96,109,60};int i;
 if(!rows||count<291)return rows;
 memcpy(extended,rows,291*16);
 for(i=0;i<11;i++)memcpy(extended+(291+i)*4,rows+donors[i]*4,16);
 return extended;
}

int Port_Beta8CostumeSelectable(int c,int slot){return B8CostumeSelectable(c,slot);}
int Port_Beta8NextCostume(int c,int current,int delta,int count){return B8NextOwnedCostume(c,current,delta,count);}
