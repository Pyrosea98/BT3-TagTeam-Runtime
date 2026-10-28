from pathlib import Path
import subprocess,tempfile,re
r=Path.cwd();s=(r/'src/battle/hud_c_b.c').read_text()
def function(name):
 a=re.search(r'^(?:static )?(?:void|s32) '+name+r'\([^;{]*\)\s*\{',s,re.M).start();i=s.index('{',a)+1;n=1
 while n:n+=(s[i]=='{')-(s[i]=='}');i+=1
 return s[a:i]
a=s.index('#ifdef PORT\n#define HUD_COMBO_DAMAGE_DIGITS');b=s.index('/* Combo part work',a);defines=s[a:b]
a=s.index('typedef struct HudCombo {');b=s.index('} HudCombo;',a)+len('} HudCombo;');struct=s[a:b]
head=r"""#include <cassert>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <cstdint>
#define PORT 1
typedef int32_t s32;typedef uint32_t u32;typedef uint16_t u16;typedef uint8_t u8;typedef float f32;
struct Ramp{float value=0,target=0;};struct HudCRes{};
struct HudCSprite{int u0,u1,v0,v1,x0,x1,y0,y1,hidden;};
struct HudCNode{u32 spriteCount,childCount;HudCSprite **sprites;HudCNode **children;void(*update)(HudCNode*);void(*draw)(HudCNode*);int x,y,ofsX,ofsY;};
static int paused,steps;int Port_HudSwap(){return 0;}
#define HUD_SCR(side) ((side)^Port_HudSwap())
#define HUDC_PAUSED() paused
struct HeapBlock{void *p;template<class T>operator T*(){return (T*)p;}};HeapBlock Heap_Alloc(size_t n,int,int,int){return {calloc(1,n)};}void Heap_Free(void *p){free(p);}
void HudCombo_Zero(void *p,u32 n){memset(p,0,n);}
void HudSprite_InitTex(HudCSprite*s,HudCRes*,int,int){s->u0=s->v0=0;s->u1=s->v1=128;}
void HudSprite_Show(HudCSprite*s,int show){s->hidden=!show;}
void HudSprite_SetRect(HudCSprite*s,int a,int b,int c,int d){s->x0=a;s->x1=b;s->y0=c;s->y1=d;}
void HudSprite_SetUv(HudCSprite*s,int a,int b,int c,int d){s->u0=a;s->u1=b;s->v0=c;s->v1=d;}
void HudSprite_Move(HudCSprite*s,int x,int y){s->x0+=x;s->x1+=x;s->y0+=y;s->y1+=y;}
void HudSprite_Scale(HudCSprite*,float,float){}
void HudSprite_InitPlain(HudCSprite*,int,int,int,int){}
void HudNode_Show(HudCNode*,int){}void HudNode_SetPos(HudCNode*n,int x,int y){n->x=x;n->y=y;}void HudNode_SetOfs(HudCNode*n,int x,int y){n->ofsX=x;n->ofsY=y;}
void Ramp_Start(Ramp*r,float,float from,float to){r->value=from;r->target=to;}int Ramp_Step(Ramp*r){steps++;r->value=r->target;return 1;}
int Rand_IntRange(int,int){static int n;return n++%10;}
void HudCombo_UpdateMessage(HudCNode*){}void HudCombo_UpdateText(HudCNode*){}void HudCombo_DrawSprites(HudCNode*){}void HudCombo_StoreTextPos(HudCNode*){}void HudCombo_UpdateRoot(HudCNode*){}
"""
body='\n'.join(function(n) for n in ['HudCombo_ExtraWidth','HudCombo_SetHitDigits','HudCombo_SetDamageDigits','HudCombo_UpdateDamage','HudCombo_UpdateHits','HudCombo_SetDamage','HudCombo_SetHits','HudCombo_Init','HudCombo_Term'])
foot=r"""
u32 read(HudCNode *n,int digits){u32 value=0,mul=1;for(int i=0;i<digits;i++){auto *s=n->sprites[i+1];if(!s->hidden){u32 digit=(s->v0/32)*4+(s->u0-1)/32;assert(digit<10);value+=digit*mul;}mul*=10;}return value;}
int main(){HudCNode *root;HudCRes res;HudCombo_Init(&root,&res);auto *hit=&gHudCombo->nodes[HUD_COMBO_NODE_HITS],*dmg=&gHudCombo->nodes[HUD_COMBO_NODE_DAMAGE];assert(hit->spriteCount==7&&dmg->spriteCount==10);
 for(u32 value:{0u,2u,999u,1000u,999999u,1000000u}){HudCombo_SetHits(0,value);HudCombo_SetHitDigits(hit);assert(read(hit,6)==(value>999999?999999:value));}
 for(int value:{0,1,99999,100000,199998,200000,16777217,999999999,2147483647}){HudCombo_SetDamage(0,value,1);gHudCombo->countRamp[0].value=gHudCombo->countRamp[0].target;HudCombo_SetDamageDigits(dmg);assert(read(dmg,9)==(u32)(value>999999999?999999999:value));}
 for(int side=0;side<2;side++){gHudCombo->side=side;HudCombo_SetHits(side,999999);HudCombo_SetDamage(side,999999999,1);gHudCombo->hitState[side]=gHudCombo->dmgState[side]=HUD_COMBO_ST_SHOWN;HudCombo_UpdateHits(hit);HudCombo_UpdateDamage(dmg);assert(read(hit,6)==999999&&read(dmg,9)==999999999);assert(hit->ofsX==(side?367:71));assert(dmg->ofsX==(side?367:53));for(u32 i=0;i<hit->spriteCount;i++){assert(hit->sprites[i]>=gHudCombo->sprites&&hit->sprites[i]<gHudCombo->sprites+HUD_COMBO_SPR_COUNT);}for(u32 i=0;i<dmg->spriteCount;i++){assert(dmg->sprites[i]>=gHudCombo->sprites&&dmg->sprites[i]<gHudCombo->sprites+HUD_COMBO_SPR_COUNT);}}
 paused=1;steps=0;HudCombo_SetDamageDigits(dmg);assert(!steps);HudCombo_SetHits(1,1);assert(gHudCombo->hitState[1]==HUD_COMBO_ST_OUT);HudCombo_SetDamage(1,-1,0);assert(gHudCombo->dmgState[1]==HUD_COMBO_ST_OUT);HudCombo_Term();puts("PASS actual native counters: extra sprite allocation,6/9digit boundaries, 1000hits/100000damage, exact16777217 and999999999, side layout, pause and slide-out; ASan/UBSan");}
"""
head='#include <initializer_list>\n'+head
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp);(p/'test.cpp').write_text(head+defines+struct+'\nstatic HudCombo *gHudCombo;\n'+body+foot)
 subprocess.run(['g++','-std=c++17','-fsanitize=address,undefined','-fno-omit-frame-pointer',str(p/'test.cpp'),'-o',str(p/'test')],check=True);subprocess.run([str(p/'test')],check=True)
