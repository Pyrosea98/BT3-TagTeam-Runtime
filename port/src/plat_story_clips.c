/* Explicit story-only clip resources. Never redirects normal game files or changes fighter profiles. */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
extern const char *Port_FileRoot(void); extern void *Port_LowAlloc(size_t); extern void Port_LowFree(void*);
const void *Port_StoryClipBytes(int bank,int motion){
 static struct{int bank,motion;void *data;} cache[64];static int used;char path[768];FILE*f;long n;void*p;uint32_t decoded;int i;
 if(bank<0||bank>99999||motion<0||motion>=440)return NULL;
 for(i=0;i<used;i++)if(cache[i].bank==bank&&cache[i].motion==motion)return cache[i].data;
 if(used>=64)return NULL;
 snprintf(path,sizeof(path),"%s/story-clips/ch8/%05d-%03d.bin",Port_FileRoot(),bank,motion);
 f=fopen(path,"rb");if(!f)return NULL;
 if(fseek(f,0,SEEK_END)||(n=ftell(f))<8||n>65536||fseek(f,0,SEEK_SET)){fclose(f);return NULL;}
 p=Port_LowAlloc((size_t)n);if(!p){fclose(f);return NULL;}
 if(fread(p,1,(size_t)n,f)!=(size_t)n){Port_LowFree(p);fclose(f);return NULL;}fclose(f);
 decoded=*(uint32_t*)p;if(decoded<0x94||decoded>49152){Port_LowFree(p);return NULL;}
 cache[used].bank=bank;cache[used].motion=motion;cache[used++].data=p;return p;
}

/* CH2/CH3 manual techniques use the shipped base bank. Cinematic overrides stay intact. */
static FILE* story_base_file(int entry){char p[768];snprintf(p,sizeof(p),"%s/pak3us3/%05d.bin",Port_FileRoot(),entry);return fopen(p,"rb");}
const void*Port_StoryGameplayClipBytes(int bank,int motion){
 static struct{int bank,motion;void*data;}cache[192];static int n;int i;FILE*f;uint32_t a,b,decoded;void*p=0;
 if((bank!=896&&bank!=897&&bank!=1016)||motion<0||motion>=414)return 0;
 for(i=0;i<n;i++)if(cache[i].bank==bank&&cache[i].motion==motion)return cache[i].data;
 if(n>=192)return 0;f=0;
 if(bank==897){char path[768];snprintf(path,sizeof(path),"%s/mods/pak3us3/%05d.bin",Port_FileRoot(),bank);f=fopen(path,"rb");}
 if(!f)f=story_base_file(bank);
 if(f){if(!fseek(f,(motion+1)*4,SEEK_SET)&&fread(&a,4,1,f)==1&&fread(&b,4,1,f)==1&&b>a&&b-a<=65536&&!fseek(f,a,SEEK_SET)){
  p=Port_LowAlloc(b-a);if(p&&(fread(p,1,b-a,f)!=b-a||*(uint32_t*)p<0x94||*(uint32_t*)p>49152)){Port_LowFree(p);p=0;}}
 fclose(f);}cache[n].bank=bank;cache[n].motion=motion;cache[n++].data=p;
 if(!p)fprintf(stderr,"bt3: missing Story gameplay clip bank%d motion%d\n",bank,motion);return p;
}
/* Read one entry from a stock donor bundle, separately from the loaded model. */
static const void *story_ultimate_entry(int file, int entry, size_t min, size_t max)
{
    FILE *f = story_base_file(file);
    uint32_t offset[2];
    size_t size;
    void *data = NULL;

    if (!f) return NULL;
    if (!fseek(f, entry * 4, SEEK_SET) && fread(offset, 4, 2, f) == 2) {
        offset[0] &= ~3u;
        offset[1] &= ~3u;
        size = offset[1] > offset[0] ? offset[1] - offset[0] : 0;
        if (size >= min && size <= max && !fseek(f, offset[0], SEEK_SET)) {
            data = Port_LowAlloc(size);
            if (data && fread(data, 1, size, f) != size) {
                Port_LowFree(data);
                data = NULL;
            }
        }
    }
    fclose(f);
    return data;
}

const void *Port_StoryGameplaySuper(int profile)
{
    static const void *table[2];
    int i = profile == 880 ? 0 : profile == 1000 ? 1 : -1;
    if (i < 0) return NULL;
    if (!table[i]) table[i] = story_ultimate_entry(profile, 24, 640, 640);
    return table[i];
}

const void *Port_StoryUltimateEffectPack(int profile)
{
    static const void *pack[2];
    int i = profile == 880 ? 0 : profile == 1000 ? 1 : -1;
    if (i < 0) return NULL;
    if (!pack[i]) pack[i] = story_ultimate_entry(i ? 1024 : 904, 5, 32, 262144);
    return pack[i];
}

const void *Port_StoryUltimateFallbackText(int profile)
{
    static const void *text[2];
    int i = profile == 880 ? 0 : profile == 1000 ? 1 : -1;
    if (i < 0) return NULL;
    /* Some legacy bundles have only the base Latin-American script. */
    if (!text[i]) text[i] = story_ultimate_entry(i ? 1001 : 880, 45, 2, 16384);
    return text[i];
}

void Port_StoryCopyUltimate(void *dst, const void *src)
{
    /* Three-slot property arrays, skipping alignment padding. Copy only slot 4. */
    static const unsigned int fields[][3] = {
        {0,0x18,4}, {0x18,0x24,2}, {0x24,0x90,4}, {0x90,0x16b,1},
        {0x16c,0x208,4}, {0x208,0x214,2}, {0x214,0x22c,1}, {0x22c,0x280,4}
    };
    unsigned int r, j;
    for (r = 0; r < sizeof(fields) / sizeof(fields[0]); ++r) {
        unsigned int width = fields[r][2];
        for (j = fields[r][0] + 2 * width; j < fields[r][1]; j += 3 * width) {
            memcpy((unsigned char *)dst + j, (const unsigned char *)src + j, width);
        }
    }
}
