/* The manifest of songs added to the music select (see plat_songs.h). Runs at start-up: it gives the game the
   added tracks' list offsets and names, and the file layer where their ADX files live. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "plat_songs.h"

char gPortSongNames[PORT_SONG_MAX][64];
int gPortSongCount;
int gPortSongOffsets[PORT_SONG_MAX];

typedef struct SongAlias {
    char rel[48];     /* the path the game asks for, under the data root */
    char target[192]; /* where it is served from */
} SongAlias;

static SongAlias sAliases[PORT_SONG_MAX];
static int sAliasCount;
static int sDone;

static const char *data_root(void) {
    const char *r = getenv("BT3_DATA");
    return r != NULL ? r : "gamedata";
}

static char *trim(char *s) {
    char *e;
    while (*s == ' ' || *s == '\t') {
        s++;
    }
    e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n')) {
        *--e = '\0';
    }
    return s;
}

static char sDir[256]; /* the folder the songs are in (plat_extras.c) */
static char sFiles[PORT_SONG_MAX][128];

extern int PortExtras_Dir(const char *kind, const char *env, char *out, unsigned size);
extern int PortExtras_Scan(const char *dir, const char *ext, char names[][128], int max);
extern void PortExtras_NameFromFile(const char *file, char *name, unsigned size);
extern void PortSongs_Mp3Start(const char *dir); /* gs/song_mp3.c: the folder's MP3 files, written as ADX in the background */
extern int PortSongs_Mp3Made(void);              /* how many it has written so far */

void PortSongs_Init(void);

const char *PortSongs_Dir(void) {
    PortSongs_Init(); /* (the overlay may ask before the game has started) */
    return sDir;
}

/* Adds one song: its place in the music list, its name, and the file the game's id for it stands for. */
static void add_song(const char *file, const char *name) {
    int i, offset, index;

    for (i = 0; i < gPortSongCount; i++) {
        if (strcmp(sFiles[i], file) == 0) {
            return;
        }
    }
    if (gPortSongCount >= PORT_SONG_MAX) {
        fprintf(stderr, "bt3: songs: more than %d added, %s is left out\n", PORT_SONG_MAX, file);
        return;
    }
    if (strlen(file) >= sizeof(sFiles[0]) || strlen(sDir) + 1 + strlen(file) >= sizeof(sAliases[0].target)) {
        /* (cut short, the name was that of no file: the song was in the list and silent) */
        fprintf(stderr, "bt3: songs: %s is left out: the folder and the name together are too long\n", file);
        return;
    }
    i = gPortSongCount;
    snprintf(sFiles[i], sizeof(sFiles[i]), "%s", file);
    offset = PORT_SONG_FIRST_OFFSET + i;
    index = PORT_SONG_FILE_INDEX + i; /* PORT_BGM_FILE(offset), as an index of the second archive */
    gPortSongOffsets[i] = offset;
    snprintf(gPortSongNames[i], 64, "%s", name);
    snprintf(sAliases[sAliasCount].rel, sizeof(sAliases[sAliasCount].rel), "pzs3us2/%05d.bin", index);
    snprintf(sAliases[sAliasCount].target, sizeof(sAliases[sAliasCount].target), "%s/%s", sDir, file);
    sAliasCount++;
    gPortSongCount++;
}

/* Reads the folder and adds the songs that are not in the list yet, at its end: a song keeps its place (the game
   holds the place's number while a fight with that music is set up and runs). */
static void collect(void) {
    char path[512];
    char line[256];
    FILE *fp;

    /* the list first (its names and its order), then whatever else is in the folder, by name */
    snprintf(path, sizeof(path), "%s/songs.txt", sDir);
    fp = fopen(path, "rb");
    while (fp != NULL && fgets(line, sizeof(line), fp) != NULL) {
        char *bar;
        if (line[0] == '\0' || line[0] == '#' || line[0] == '\n' || line[0] == '\r') {
            continue;
        }
        bar = strchr(line, '|');
        if (bar == NULL) {
            continue;
        }
        *bar = '\0';
        {
            /* a list line may name the MP3: its ADX is the file, and none means the MP3 could not be converted */
            char *file = trim(line), adx[160];
            size_t l = strlen(file);
            if (l > 4 && l < sizeof(adx) && file[l - 4] == '.' && (file[l - 3] | 0x20) == 'm' && (file[l - 2] | 0x20) == 'p' &&
                file[l - 1] == '3') { /* ".mp3" in any case of the letters, as the converter takes it (".Mp3" was listed as a song) */
                FILE *f;
                snprintf(adx, sizeof(adx), "%.*s.adx", (int)(l - 4), file);
                snprintf(path, sizeof(path), "%s/%s", sDir, adx);
                if ((f = fopen(path, "rb")) == NULL) {
                    continue;
                }
                fclose(f);
                file = adx;
            }
            add_song(file, trim(bar + 1));
        }
    }
    if (fp != NULL) {
        fclose(fp);
    }
    {
        static char found[64][128];
        int n = PortExtras_Scan(sDir, ".adx", found, 64), k;
        for (k = 0; k < n; k++) {
            char name[64];
            PortExtras_NameFromFile(found[k], name, sizeof(name));
            add_song(found[k], name);
        }
    }
}

static void collect_iso(void){
 char path[512],line[256];FILE *fp;const char *root=data_root();int before=gPortSongCount;
 snprintf(path,sizeof(path),"%s/beta8/iso-songs.tsv",root);fp=fopen(path,"r");if(!fp)return;
 while(fgets(line,sizeof(line),fp)&&gPortSongCount<PORT_SONG_MAX){
  char *end,*name;long entry=strtol(line,&end,10);unsigned char head[20];FILE *audio;int i;char target[192];
  if(end==line||*end!='\t'||entry<65201||entry>=65495)continue;name=trim(end+1);if(!*name)continue;
  if(snprintf(target,sizeof(target),"%s/pak3us2/%05ld.bin",root,entry)>=(int)sizeof(target))continue;
  audio=fopen(target,"rb");if(!audio)continue;i=fread(head,1,20,audio);fclose(audio);
  if(i!=20||head[0]!=0x80||head[1]!=0||head[7]!=2)continue;
  i=gPortSongCount++;snprintf(sFiles[i],sizeof(sFiles[i]),"iso:%05ld",entry);
  gPortSongOffsets[i]=PORT_SONG_FIRST_OFFSET+i;snprintf(gPortSongNames[i],64,"%s",name);
  snprintf(sAliases[sAliasCount].rel,sizeof(sAliases[0].rel),"pzs3us2/%05d.bin",PORT_SONG_FILE_INDEX+i);
  snprintf(sAliases[sAliasCount].target,sizeof(sAliases[0].target),"%s",target);sAliasCount++;
 }
 fclose(fp);if(gPortSongCount>before)fprintf(stderr,"bt3: songs: %d unique ISO soundtrack tracks imported\n",gPortSongCount-before);
}

static int sMadeSeen; /* PortSongs_Mp3Made() when the folder was last read */

void PortSongs_Init(void) {
    if (sDone) {
        return;
    }
    sDone = 1;
    collect_iso();
    if (!PortExtras_Dir("songs", "BT3_SONGS", sDir, sizeof(sDir))) {
        return;
    }
    PortSongs_Mp3Start(sDir);
    sMadeSeen = PortSongs_Mp3Made(); /* (taken before the folder is read: a song finished meanwhile is looked for again) */
    collect();
    if (gPortSongCount > 0) {
        fprintf(stderr, "bt3: songs: %d added from %s\n", gPortSongCount, sDir);
    }
}

/* The songs whose conversion from MP3 has finished since the list was last made join it (song_mp3.c). Called by
   the game when it sets up a select screen's music list (src/menu/menu_c_e.c, menu_e_b.c), on the game's thread:
   the list does not change at any other time, and not in an online session, where added songs are not offered and
   nothing the two copies might not share is to change. */
void PortSongs_Refresh(void) {
    extern int Port_NetSession(void); /* gs/net.c */
    int made, had = gPortSongCount;

    if (!sDone || sDir[0] == '\0' || Port_NetSession()) {
        return;
    }
    made = PortSongs_Mp3Made();
    if (made == sMadeSeen) {
        return;
    }
    sMadeSeen = made;
    collect();
    if (gPortSongCount > had) {
        fprintf(stderr, "bt3: songs: %d more converted from MP3, %d added now\n", gPortSongCount - had, gPortSongCount);
    }
}

const char *PortSongs_Name(int index) {
    return index >= 0 && index < gPortSongCount ? gPortSongNames[index] : "";
}

int PortSongs_Count(void) {
    return gPortSongCount;
}

int PortSongs_Offset(int index) {
    if (index < 0 || index >= gPortSongCount) {
        return 0;
    }
    return gPortSongOffsets[index];
}

/* Story presentation ADX slots are independent of the music list (no index shifts in saves).
   Chapter-specific files override saga families, allowing a different intro for each movie. */
#define STORY_TRACK_FIRST 75000
#define STORY_TRACK_MAX 160
static struct {char target[512];int loop,durationMs;} sStoryTracks[STORY_TRACK_MAX];
static int sStoryTrackCount;
int PortSongs_StoryBgm(const char *chapter,int saga,int title){
 char path[512];FILE*f;int i,pass,p=saga==0||saga==1?0:saga>=2&&saga<8?saga:0;
 for(pass=0;pass<3;pass++){
  int durationMs=0;
  if(pass==0){if(!chapter||!*chapter||strchr(chapter,'/')||strchr(chapter,'\\'))continue;snprintf(path,sizeof(path),"%s/tagteam-ui/history-prefight/%s-%s.adx",data_root(),chapter,title?"title":"recap");}
  else {int profile=pass==1?p:0;if(pass==2&&p==0)break;snprintf(path,sizeof(path),"%s/tagteam-ui/history-prefight/%s-%d.adx",data_root(),title?"title":"recap",profile);}
  f=fopen(path,"rb");if(!f)continue;{unsigned char h[16];int valid=fread(h,1,16,f)==16&&h[0]==0x80&&h[1]==0&&h[7]==2;fclose(f);if(!valid)continue;unsigned rate=((unsigned)h[8]<<24)|((unsigned)h[9]<<16)|((unsigned)h[10]<<8)|h[11],samples=((unsigned)h[12]<<24)|((unsigned)h[13]<<16)|((unsigned)h[14]<<8)|h[15];if(rate>=8000&&rate<=192000){unsigned long long ms=(unsigned long long)samples*1000/rate;if(ms>=500&&ms<=120000)durationMs=(int)ms;}}
  for(i=0;i<sStoryTrackCount;i++)if(!strcmp(path,sStoryTracks[i].target))return 0xD48+STORY_TRACK_FIRST+i;
  if(sStoryTrackCount>=STORY_TRACK_MAX)break;i=sStoryTrackCount++;snprintf(sStoryTracks[i].target,sizeof(sStoryTracks[i].target),"%s",path);sStoryTracks[i].loop=!title;sStoryTracks[i].durationMs=durationMs;return 0xD48+STORY_TRACK_FIRST+i;
 }
 /* Native series-specific fallback when a resource has not been supplied yet. */
 return saga==5?0x10B28:saga==6?0x10B1E:0x10B1C;
}

int PortSongs_StoryDurationMs(const char*chapter,int saga,int title){int i=PortSongs_StoryBgm(chapter,saga,title)-0xD48-STORY_TRACK_FIRST;return i>=0&&i<sStoryTrackCount?sStoryTracks[i].durationMs:0;}

/* Whether `path` is the file of a song added from outside the disc (for the sound code: such a file is played
   round and round like the disc's music, though it has no loop points of its own). */
int PortSongs_IsFile(const char *path) {
    size_t pl = strlen(path), tl;
    for(int k=0;k<sStoryTrackCount;k++)if(!strcmp(path,sStoryTracks[k].target))return sStoryTracks[k].loop;
    int i;
    for (i = 0; i < sAliasCount; i++) {
        tl = strlen(sAliases[i].target);
        if (tl != 0 && tl <= pl && strcmp(path + pl - tl, sAliases[i].target) == 0) {
            return 1;
        }
    }
    return 0;
}

int PortSongs_Alias(const char *rel, char *out, unsigned n) {
    int i;
    char canonical[256];
    /* Beta8 mounts PAK3US2; the original executable mounts PZS3US2.
       Imported music aliases must work with either partition spelling. */
    if (strncmp(rel,"pak3us2/",8)==0) {
        snprintf(canonical,sizeof(canonical),"pzs3us2/%s",rel+8);
        rel=canonical;
    }
    {int entry=-1,used=0;if(sscanf(rel,"pzs3us2/%d.bin%n",&entry,&used)==1&&rel[used]==0&&entry>=STORY_TRACK_FIRST&&entry<STORY_TRACK_FIRST+sStoryTrackCount){snprintf(out,n,"%s",sStoryTracks[entry-STORY_TRACK_FIRST].target);return 1;}}
    for (i = 0; i < sAliasCount; i++) {
        if (strcmp(sAliases[i].rel, rel) == 0) {
            snprintf(out, n, "%s", sAliases[i].target);
            return 1;
        }
    }
    return 0;
}

/* BT3_TEST_BGM=<entry>: every battle is set up with that entry of the music list, whatever the menus chose
   (testing: 0x1A is the first added song). */
int Port_TestBgm(void) {
    const char *e = getenv("BT3_TEST_BGM");
    return e != NULL && e[0] != '\0' ? (int)strtol(e, NULL, 0) : -1;
}
