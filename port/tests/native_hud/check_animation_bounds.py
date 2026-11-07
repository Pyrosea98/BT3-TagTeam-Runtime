from pathlib import Path
import subprocess,tempfile
r=Path.cwd();s=(r/'src/battle/bobj_a.c').read_text()
a=s[s.index('static int Port_AnimValidateChannels('):s.index('#endif',s.index('static int Port_AnimValidateChannels('))]
b=s[s.index('BobjAnim *BtlObjAnim_Load('):s.index('/* Initial player state',s.index('BobjAnim *BtlObjAnim_Load('))]
head=r"""
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <stdio.h>
typedef uint8_t u8;typedef uint16_t u16;typedef uint32_t u32;typedef int32_t s32;
#define PORT 1
#define BOBJ_NODE_MAX 71
#define BOBJ_ANIM_MAX 414
#define BOBJ_WORK_LAYER_SIZE 0xC000
#define BOBJ_TRACK_ROT_ONLY(t) ((t)->flags&1)
typedef struct {u8 unk00,eventCount;u16 length,unk04,track[71];} BobjAnim;
typedef struct {u16 flags,count;} BobjTrack;
#define TRACK_AT(a,o) ((BobjTrack*)((u8*)(a)+(o)*4))
typedef struct {struct{void *anims[414];}mdl;}BobjObj;
static int decodes;
void Bpe_Decode(void *p,void *out,int *size){decodes++;*size=*(u32*)p;memcpy(out,(u8*)p+8,*size);}
"""
test=r"""
int main(int argc,char**argv){
 u8 a[512]={0},copy[512],buf[0xC000],packed[520];BobjAnim *m=(BobjAnim*)a;BobjObj o={0};int size;
 m->length=62;m->track[0]=40;BobjTrack*t=(BobjTrack*)(a+160);t->flags=1;t->count=2;
 memcpy(copy,a,512);assert(Port_AnimValidateChannels(m,184));assert(!memcmp(a,copy,512));
 assert(Port_AnimValidateChannels(m,183));assert(!m->track[0]);
 m->track[0]=40;t->count=0;assert(Port_AnimValidateChannels(m,184)&&!m->track[0]);
 m->track[0]=1;assert(Port_AnimValidateChannels(m,184)&&!m->track[0]);
 m->track[0]=65535;assert(Port_AnimValidateChannels(m,184)&&!m->track[0]);
 m->track[0]=40;t->flags=0;t->count=65535;assert(Port_AnimValidateChannels(m,512)&&!m->track[0]);
 assert(!Port_AnimValidateChannels(m,147));assert(!Port_AnimValidateChannels(m,0xC001));
 m->eventCount=1;assert(!Port_AnimValidateChannels(m,148));m->eventCount=0;
 *(u32*)packed=512;memcpy(packed+8,a,512);o.mdl.anims[0]=packed;
 assert(BtlObjAnim_Load(&o,buf,0,&size)==(BobjAnim*)buf&&decodes==1);
 *(u32*)packed=0xC001;assert(!BtlObjAnim_Load(&o,buf,0,&size)&&decodes==1);
 *(u32*)packed=147;assert(!BtlObjAnim_Load(&o,buf,0,&size)&&decodes==1);
 FILE*f=fopen(argv[1],"rb");assert(f);size=fread(buf,1,sizeof(buf),f);fclose(f);
 u8 original[0xC000];memcpy(original,buf,size);m=(BobjAnim*)buf;
 assert(m->track[64]&&m->track[66]);assert(Port_AnimValidateChannels(m,size));
 assert(!m->track[64]&&!m->track[66]);
 ((BobjAnim*)original)->track[64]=0;((BobjAnim*)original)->track[66]=0;
 assert(!memcmp(original,buf,size));
 puts("PASS real Gohan idle: malformed channels64/66 repaired, all valid tracks/events bytes unchanged; truncated/oversize BPE rejected before decode; ASan/UBSan");
}
"""
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.c').write_text(head+a+b+test)
 subprocess.run(['gcc','-std=gnu99','-O1','-fsanitize=address,undefined','-fno-omit-frame-pointer',str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test'),str(r/'port/tests/native_hud/fixtures/gohan-idle-malformed.bin')],check=True)
