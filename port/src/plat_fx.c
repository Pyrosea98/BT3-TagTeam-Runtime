/*
 * plat_fx.c - efectos propios del port (recursos nuestros, no del disco de PS2).
 *
 * Primer uso: sangre. Las texturas viven en `assets/fx/` y se leen con SDL
 * (SDL_LoadPNG), igual que el resto de los recursos del port. Nada de
 * identidades de textura de PCSX2: este codigo no toca GS ni TEX0.
 *
 * El renderer provee UNA sola call (port/src/gs/ui.cpp):
 *
 *     void Port_Fx_DrawSprite(const unsigned char *rgba, int w, int h,
 *                             float x, float y, float size, int alpha);
 *
 * (x, y) y `size` en la pagina 512 x 448 del juego; el renderer los pasa a
 * pixeles de la ventana.
 *
 * Tiempo: todo lo que envejece lo hace en `Port_Fx_Tick`, que corre una vez por
 * cuadro de simulacion (Battle_Update). El dibujo no cambia la vida de nada, asi
 * que los FPS, las imagenes intermedias o un menu abierto no la alteran.
 *
 * Encendido: variable de entorno BT3_BLOOD=1 (apagado por defecto).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL3/SDL.h>
#include "plat_fx.h"

typedef struct FxImage {
    unsigned char *rgba;
    int w, h;
    float u0,v0,u1,v1,anchorV;
} FxImage;

typedef struct FxBurst {
    int used;
    const FxImage *img;
    float x, y;      /* pagina 512 x 448 */
    float vx, vy;    /* deriva por cuadro de simulacion */
    float size0, size1;
    int life, life0; /* en cuadros de simulacion */
    int alpha0;
    unsigned char r, g, b; /* color de la sangre al nacer (no cambia si despues sangra otro) */
} FxBurst;

typedef struct FxPending {
    int used;
    int objId;
    int kind;
    float scale;
    int spit;
    int ticks;       /* cuantos cuadros de simulacion espera el punto de impacto */
} FxPending;

/* Small ballistic drops in world space, projected again for every drawn view.
   No screen splash or screen-space drift is used for a story mouth spit. */
typedef struct {float x,y,z,w;} FxWorldVec;
typedef struct {FxWorldVec p,v;float radius,gravity,floor;int life,green;unsigned char r,g,b;} FxWorldDrop;
/* Color de la PROXIMA sangre que nazca (tinte del sprite gris). Cada gota/chorro/estallido copia este color al
   nacer y lo conserva: si despues sangra otro personaje, la sangre que ya esta en pantalla no cambia de color.
   El golpe lo fija justo antes de emitir y lo devuelve al rojo despues (Port_Fx_ResetTint), asi la sangre
   autorada de la Historia sigue roja. */
#define FX_TINT_R 172
#define FX_TINT_G 22
#define FX_TINT_B 28
static int sTintR = FX_TINT_R, sTintG = FX_TINT_G, sTintB = FX_TINT_B;
void Port_Fx_SetTint(int r, int g, int b) {
    sTintR = r < 0 ? 0 : r > 255 ? 255 : r;
    sTintG = g < 0 ? 0 : g > 255 ? 255 : g;
    sTintB = b < 0 ? 0 : b > 255 ? 255 : b;
}
void Port_Fx_ResetTint(void) {
    sTintR = FX_TINT_R; sTintG = FX_TINT_G; sTintB = FX_TINT_B;
}
static FxWorldDrop sWorldDrops[64];static int sEmitGreen;static unsigned char*sGreenDrop;
static float sFxAgeAccumulator;
extern int Port_StoryFxRateQ8(void);
typedef struct {FxWorldVec p,d;float h;float age;int life;unsigned char r,g,b;} FxWorldJet;
static FxWorldJet sWorldJets[4];
extern int Port_ModHudProject(int,void*,int*,int*);
static void fx_world_emit(const FxWorldVec *mouth,const FxWorldVec *direction,int heightQ8,int mouthStyle){
 int i,n=mouthStyle?6:12;float h=heightQ8/256.f;
 if(!mouth||!direction||h<=0||!Port_Fx_Enabled())return;
 if(mouthStyle){int j,best=0;for(j=0;j<4;j++){if(sWorldJets[j].life<=0){best=j;break;}if(sWorldJets[j].life<sWorldJets[best].life)best=j;}
 FxWorldJet*b=&sWorldJets[best];b->p=*mouth;b->d=*direction;b->h=h;b->age=0;b->life=7;b->r=(unsigned char)sTintR;b->g=(unsigned char)sTintG;b->b=(unsigned char)sTintB;}
 if(getenv("BT3_STORY_SCENE_LOG"))fprintf(stderr,"bt3: world blood spawn style%d h%.3f mouth(%.3f %.3f %.3f) dir(%.3f %.3f %.3f) floor%.3f\n",mouthStyle,h,mouth->x,mouth->y,mouth->z,direction->x,direction->y,direction->z,direction->w);
 for(i=0;i<n;i++){int j,best=0;for(j=0;j<64;j++){if(sWorldDrops[j].life<=0){best=j;break;}if(sWorldDrops[j].life<sWorldDrops[best].life)best=j;}FxWorldDrop*b=&sWorldDrops[best];float spread=(i%5-2)*(mouthStyle?.004f:.009f),speed=(mouthStyle?.046f:.032f)+.003f*i;
  b->green=sEmitGreen;b->r=(unsigned char)sTintR;b->g=(unsigned char)sTintG;b->b=(unsigned char)sTintB;b->p=*mouth;b->v.x=(direction->x*speed+direction->z*spread)*h;
  b->v.z=(direction->z*speed-direction->x*spread)*h;
  b->v.y=(direction->y*speed-.012f+(i%4)*.004f)*h;
  b->radius=h*(mouthStyle?(.004f+.001f*(i%4)):(.008f+.002f*(i%3)));
  b->gravity=h*.002f;b->floor=direction->w;b->life=(mouthStyle?22:16)+i%5;
 }
}
void Port_Fx_WorldSpit(const FxWorldVec *p,const FxWorldVec*d,int h){fx_world_emit(p,d,h,0);}
void Port_Fx_WorldGreenBurst(const FxWorldVec*p,const FxWorldVec*d,int h){sEmitGreen=1;fx_world_emit(p,d,h,0);sEmitGreen=0;}
void Port_Fx_WorldMouthSpit(const FxWorldVec*p,const FxWorldVec*d,int h){fx_world_emit(p,d,h,1);}
#define FX_MAX 24
#define FX_PENDING 12
#define FX_PENDING_TICKS 3

extern void Port_Fx_SetDrawTint(int r, int g, int b); /* gs/ui.cpp */
static FxImage sSplash, sSpray, sDrop;
typedef struct { FxWorldVec a,b;int used;unsigned char r,g,bloodB; } FxWound;
static FxWound sWounds[16][14];
static unsigned long long sSurfaceState[16];
void Port_Fx_SurfaceState(int actor,int level,int profile,int r,int g,int b) {
    unsigned long long value;
    if(actor<0||actor>=16)return;
    value=(level&3)|((profile&255)<<8)|((unsigned long long)(r&255)<<16)|((unsigned long long)(g&255)<<24)|((unsigned long long)(b&255)<<32);
    __atomic_store_n(&sSurfaceState[actor],value,__ATOMIC_RELEASE);
}
unsigned long long Port_Fx_GetSurfaceState(int actor) {
    if(actor<0||actor>=16||!Port_Fx_Enabled())return 0;
    return __atomic_load_n(&sSurfaceState[actor],__ATOMIC_ACQUIRE);
}
/* Verified surface/beam routes use normal saved settings. Explicit overrides
 * remain useful for private negative tests. Auxiliary task classes stay off. */
int Port_BloodReviewEnabled(int deflect) {
    const char *v=getenv(deflect==2?"BT3_BIG_AUX_REVIEW":deflect?"BT3_BIG_DEFLECT_REVIEW":"BT3_BLOOD_PROGRESS_REVIEW");
    return v?strcmp(v,"1")==0:deflect!=2;
}
int Port_BeamFixtureSlot(void) {
    const char *v=getenv("BT3_BIG_BEAM_FIXTURE");int a,b,slot;
    return v&&sscanf(v,"%d,%d,%d",&a,&b,&slot)==3&&slot>=2&&slot<=4?slot:0;
}
int Port_BeamFixtureMode(void) {
    const char *v=getenv("BT3_BIG_BEAM_FIXTURE_MODE");return v&&strcmp(v,"0")==0?0:6;
}
int Port_BeamFixtureStock(void) {
    const char *v=getenv("BT3_BIG_BEAM_FIXTURE_STOCK");long n=v?strtol(v,NULL,10):300000;
    return n>=0&&n<=700000?(int)n:300000;
}
int Port_BloodFixtureLevel(int frame) {
    const char *v=getenv("BT3_BLOOD_SURFACE_FIXTURE_LEVEL");int level=v?atoi(v):0;
    if(v&&strcmp(v,"progressive")==0)return frame<430?1:frame<450?2:3;
    return level>=1&&level<=3?level:0;
}
int Port_BloodFixtureDamageAt(int frame) {
    const char *value=getenv("BT3_BLOOD_SURFACE_FIXTURE_DAMAGE_AT");
    return value&&frame==atoi(value);
}
void Port_Fx_WoundSegment(int actor,int mark,const void *a,const void *b,int r,int g,int blue) {
    FxWound *w;if(actor<0||actor>=16||mark<0||mark>=14)return;
    w=&sWounds[actor][mark];w->used=a&&b;
    {const char *v=getenv("BT3_BLOOD_SURFACE_REVIEW");if(v&&strcmp(v,"1")==0)w->used=0;}
    if(w->used){w->a=*(const FxWorldVec*)a;w->b=*(const FxWorldVec*)b;w->r=r;w->g=g;w->bloodB=blue;}
}
static int sLoaded, sEnabled, sInit, sDropFragment;
static FxBurst sBursts[FX_MAX];
static FxPending sPending[FX_PENDING];

/* el renderer lo provee (ver la cabecera del archivo) */
extern void Port_Fx_DrawSprite(const unsigned char *rgba, int w, int h,
                               float x, float y, float size, int alpha);

extern void Port_Fx_DrawFluid(const unsigned char*,int,int,float,float,float,float,float,float,float,float,float,int);
void Port_Fx_Init(void);

/* El modulo se arranca solo la primera vez que alguien pregunta: asi no hace falta tocar el arranque del port. */
extern int Port_Setting(const char*,int);
static int fx_wanted(void){const char*v=getenv("BT3_BLOOD");return v&&*v?strcmp(v,"0")!=0:Port_Setting("blood_effects",0)!=0;}
static void fx_lazy_init(void) {
 static int previous=-1;int wanted=fx_wanted();
 if(!sInit||previous!=wanted){sInit=1;previous=wanted;if(wanted&&!sLoaded)Port_Fx_Init();else sEnabled=wanted;}
}

static int load_image(FxImage *out, const char *path) {
    SDL_Surface *png = SDL_LoadPNG(path);
    SDL_Surface *conv;
    int y;

    if (png == NULL) {
        return 0;
    }
    conv = SDL_ConvertSurface(png, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(png);
    if (conv == NULL || conv->w <= 0 || conv->h <= 0) {
        SDL_DestroySurface(conv);
        return 0;
    }
    out->rgba = SDL_malloc((size_t)conv->w * conv->h * 4);
    if (out->rgba == NULL) {
        SDL_DestroySurface(conv);
        return 0;
    }
    for (y = 0; y < conv->h; y++) {
        memcpy(out->rgba + (size_t)y * conv->w * 4,
               (const unsigned char *)conv->pixels + (size_t)y * conv->pitch,
               (size_t)conv->w * 4);
    }
    /* El sprite se guarda en GRIS (luminancia maxima por pixel): el color lo pone el tinte por personaje
       (Port_Fx_SetTint / mod_blood.h). Asi un mismo PNG sirve para sangre roja, violeta o rosa. */
    {
        int i, n = conv->w * conv->h;
        for (i = 0; i < n; i++) {
            unsigned char *p = out->rgba + (size_t)i * 4;
            unsigned char v = p[0] > p[1] ? (p[0] > p[2] ? p[0] : p[2]) : (p[1] > p[2] ? p[1] : p[2]);
            p[0] = p[1] = p[2] = v;
        }
    }
    out->w = conv->w;
    out->h = conv->h;
    {int x,y,left=conv->w,top=conv->h,right=-1,bottom=-1;float anchor=0,weight=0;
     for(y=0;y<conv->h;y++)for(x=0;x<conv->w;x++)if(out->rgba[(y*conv->w+x)*4+3]>32){if(x<left)left=x;if(x>right)right=x;if(y<top)top=y;if(y>bottom)bottom=y;}
     if(right>=left){for(y=top;y<=bottom;y++)for(x=left;x<=left+3&&x<=right;x++){int a=out->rgba[(y*conv->w+x)*4+3];if(a>32){anchor+=y*a;weight+=a;}}
      out->u0=(float)left/conv->w;out->v0=(float)top/conv->h;out->u1=(float)(right+1)/conv->w;out->v1=(float)(bottom+1)/conv->h;out->anchorV=weight?anchor/weight/conv->h:(out->v0+out->v1)*.5f;
     }else{out->u1=out->v1=1;out->anchorV=.5f;}}

    SDL_DestroySurface(conv);
    return 1;
}

void Port_Fx_Init(void) {
    const char *dir = "assets/fx";
    char path[512];

    sEnabled = fx_wanted();
    if (!sEnabled) {
        return;
    }
    snprintf(path, sizeof(path), "%s/blood_splash_128.png", dir);
    load_image(&sSplash, path);
    snprintf(path, sizeof(path), "%s/blood_spit_v2.png", dir);
    if(!load_image(&sSpray, path)){
        snprintf(path, sizeof(path), "%s/blood_spray_64x64.png", dir);
        load_image(&sSpray, path);
    }
    snprintf(path, sizeof(path), "%s/blood_fragment_v3.png", dir);
    sDropFragment=load_image(&sDrop,path);
    if(!sDropFragment){snprintf(path, sizeof(path), "%s/blood_droplet_32.png", dir);load_image(&sDrop,path);}
    sLoaded = sSplash.rgba != NULL;
    if (!sLoaded) {
        fprintf(stderr, "bt3: fx: no encontre assets/fx/blood_splash_128.png, sangre apagada\n");
    }
}

int Port_Fx_Enabled(void) {
    fx_lazy_init();
    return sEnabled && sLoaded;
}

/* ---- pedidos pendientes: golpe decidido, punto de impacto todavia no ---- */

void Port_Fx_Arm(int objId, int kind, int scaleQ8, int spit) {
    float scale = (float)(scaleQ8 > 384 ? 384 : scaleQ8) / 256.0f;
    int i;

    if (!Port_Fx_Enabled() || scale <= 0.0f) {
        return;
    }
    for (i = 0; i < FX_PENDING; i++) {
        if (sPending[i].used && sPending[i].objId == objId) {
            /* varios golpes en el mismo cuadro: uno solo, con el tamano mayor */
            if (scale > sPending[i].scale) {
                sPending[i].scale = scale;
            }
            sPending[i].kind = kind;
            sPending[i].spit = spit;
            sPending[i].ticks = FX_PENDING_TICKS;
            return;
        }
    }
    for (i = 0; i < FX_PENDING; i++) {
        if (!sPending[i].used) {
            sPending[i].used = 1;
            sPending[i].objId = objId;
            sPending[i].kind = kind;
            sPending[i].scale = scale;
            sPending[i].spit = spit;
            sPending[i].ticks = FX_PENDING_TICKS;
            return;
        }
    }
}

int Port_Fx_Take(int objId, int *kind, float *scale, int *spit) {
    int i;

    if (!Port_Fx_Enabled()) {
        return 0;
    }
    for (i = 0; i < FX_PENDING; i++) {
        if (sPending[i].used && sPending[i].objId == objId) {
            if (kind != NULL) {
                *kind = sPending[i].kind;
            }
            if (scale != NULL) {
                *scale = sPending[i].scale;
            }
            if (spit != NULL) {
                *spit = sPending[i].spit;
            }
            sPending[i].used = 0;
            return 1;
        }
    }
    return 0;
}

/* ---- estallidos ---- */

/* kind 0 splash (impacto), 1 spray (escupir), 2 gota. (x, y) en la pagina 512 x 448. */
static void burst(float x, float y, int kind, float scale) {
    int i;

    if (!(scale > 0.0f && scale <= 1.5f)) {
        return;
    }
    for (i = 0; i < FX_MAX; i++) {
        if (!sBursts[i].used) {
            FxBurst *b = &sBursts[i];
            const FxImage *img = kind == 1 ? (&sSpray) : kind == 2 ? (&sDrop) : (&sSplash);
            if (img->rgba == NULL) {
                img = &sSplash;
            }
            b->used = 1;
            b->img = img;
            b->x = x;
            b->y = y;
            b->vx = 0.0f;
            b->vx = kind==1 ? 1.8f*scale : kind==2 ? .9f*scale : 0.f;
            b->vy = kind==0 ? -.3f*scale : -.65f*scale;
            b->size0 = (kind==2 ? 3.f : kind==1 ? 32.f : 20.f) * scale;
            b->size1 = (kind==2 ? 2.f : kind==1 ? 40.f : 26.f) * scale;
            b->life0 = b->life = kind == 2 ? 14 : 7;
            b->alpha0 = 220;
            b->r = (unsigned char)sTintR;
            b->g = (unsigned char)sTintG;
            b->b = (unsigned char)sTintB;
            return;
        }
    }
}

/* El golpe ya entro y ya se sabe donde: (x, y) en la pagina 512 x 448. */
void Port_Fx_BloodAt(int x, int y, int kind, int scaleQ8, int spit) {
    float scale = (float)(scaleQ8 > 384 ? 384 : scaleQ8) / 256.0f;
    if (!Port_Fx_Enabled()) {
        return;
    }
    burst((float)x, (float)y, kind, scale);
    if(kind==1){int k;for(k=0;k<4;k++)burst((float)x+2*k,(float)y+(k%2?1.f:-1.f),2,scale*(.6f+.12f*k));}
    if (spit) {
        burst((float)x + 6.0f, (float)y - 12.0f, 1, scale * 0.9f);
    }
}

/* ---- tiempo: cuadros de simulacion ---- */

void Port_Fx_Tick(void) {
    static int sSelfTest = -1;
    static int sSelfTick;
    int i,age;float dt;

    fx_lazy_init();
    if (!sEnabled) {
        return;
    }
    dt=Port_StoryFxRateQ8()/256.f;if(dt<.01f||dt>1.f)dt=1.f;
    sFxAgeAccumulator+=dt;age=sFxAgeAccumulator>=.999f;if(age)sFxAgeAccumulator-=1.f;
    /* Prueba de dibujo sin depender de nada mas: BT3_BLOOD_SELFTEST=1 deja un estallido en el centro de la pagina
       cada 45 cuadros de simulacion, con todas las demas superposiciones apagadas. Sirve para verificar la
       admision del cuadro, el dibujo y el tiempo en un caso aislado. */
    if (sSelfTest < 0) {
        const char *v = getenv("BT3_BLOOD_SELFTEST");
        sSelfTest = (v != NULL && v[0] != '\0' && strcmp(v, "0") != 0) ? 1 : 0;
    }
    if (sSelfTest) {
        sSelfTick++;
        if ((sSelfTick % 45) == 1) {
            burst(256.0f, 224.0f, 0, 1.0f);
        }
    }
    for(i=0;i<4;i++)if(sWorldJets[i].life>0){sWorldJets[i].life-=age;sWorldJets[i].age+=dt;}
    for(i=0;i<64;i++){FxWorldDrop*b=&sWorldDrops[i];if(b->life<=0)continue;
        b->p.x+=b->v.x*dt;b->p.y+=b->v.y*dt;b->p.z+=b->v.z*dt;b->v.y+=b->gravity*dt;
        if((b->life-=age)<=0||b->p.y>=b->floor)b->life=0;}
    for (i = 0; i < FX_PENDING; i++) {
        if (sPending[i].used && (sPending[i].ticks-=age) <= 0) {
            sPending[i].used = 0; /* el efecto nunca se pidio: no hubo impacto */
        }
    }
    for (i = 0; i < FX_MAX; i++) {
        FxBurst *b = &sBursts[i];
        if (!b->used) {
            continue;
        }
        b->x += b->vx*dt;
        b->y += b->vy*dt;
        b->vy += 0.12f*dt;
        if ((b->life-=age) <= 0) {
            b->used = 0;
        }
    }
}

void Port_Fx_Clear(void) {
    int actor;for(actor=0;actor<16;actor++)Port_Fx_SurfaceState(actor,0,0,0,0,0);
    extern void Port_BloodProgressClear(void),Port_BigDeflectReset(void),Port_BigBeamReset(void);
    Port_BloodProgressClear();Port_BigDeflectReset();Port_BigBeamReset();
    memset(sWounds,0,sizeof(sWounds));
    sFxAgeAccumulator=0;
    Port_Fx_ResetTint();
    memset(sWorldJets,0,sizeof(sWorldJets));
    memset(sWorldDrops,0,sizeof(sWorldDrops));
    memset(sBursts, 0, sizeof(sBursts));
    memset(sPending, 0, sizeof(sPending));
}

int Port_Fx_Live(void) {
    int i;

    if (!Port_Fx_Enabled()) {
        return 0;
    }
    for(i=0;i<16*14;i++)if(sWounds[i/14][i%14].used)return 1;
    for(i=0;i<4;i++)if(sWorldJets[i].life>0)return 1;
    for(i=0;i<64;i++)if(sWorldDrops[i].life>0)return 1;
    for (i = 0; i < FX_MAX; i++) {
        if (sBursts[i].used) {
            return 1;
        }
    }
    for (i = 0; i < FX_PENDING; i++) {
        if (sPending[i].used) {
            return 1;
        }
    }
    return 0;
}

/* ---- dibujo ---- */

void Port_Fx_Draw(void) {
    int i;

    if (!Port_Fx_Enabled()) {
        return;
    }
    for(i=0;i<16*14;i++){
      FxWound *w=&sWounds[i/14][i%14];int ax,ay,bx,by;
      if(!w->used||!sDrop.rgba)continue;
      if(!Port_ModHudProject(0,&w->a,&ax,&ay)||!Port_ModHudProject(0,&w->b,&bx,&by))continue;
      Port_Fx_SetDrawTint(w->r,w->g,w->bloodB);
      Port_Fx_DrawFluid(sDrop.rgba,sDrop.w,sDrop.h,sDrop.u0,sDrop.v0,sDrop.u1,sDrop.v1,sDrop.anchorV,ax,ay,bx,by,205);
    }
    for(i=0;i<4;i++)if(sWorldJets[i].life>0&&sSpray.rgba){
      FxWorldJet *jet=&sWorldJets[i];
      static FxWorldVec p,tip;static int x0,y0,x1,y1;
      extern void Port_Fx_DrawFluid(const unsigned char*,int,int,float,float,float,float,float,float,float,float,float,int);
      float travel=jet->h*.025f*jet->age,length=jet->h*(.22f+.065f*jet->age);
      p=jet->p;p.x+=jet->d.x*travel;p.y+=jet->d.y*travel;p.z+=jet->d.z*travel;
      tip=p;tip.x+=jet->d.x*length;tip.y+=jet->d.y*length+jet->h*.025f;tip.z+=jet->d.z*length;
      Port_Fx_SetDrawTint(jet->r,jet->g,jet->b); /* el color con que nacio este chorro */
      if(Port_ModHudProject(0,&p,&x0,&y0)&&Port_ModHudProject(0,&tip,&x1,&y1))
       Port_Fx_DrawFluid(sSpray.rgba,sSpray.w,sSpray.h,sSpray.u0,sSpray.v0,sSpray.u1,sSpray.v1,sSpray.anchorV,(float)x0,(float)y0,(float)x1,(float)y1,jet->life<3?jet->life*90:230);
    }
    for(i=0;i<64;i++){FxWorldDrop*b=&sWorldDrops[i];static FxWorldVec edge;static int x,y,ex,ey;float size;
      if(b->life<=0||!sDrop.rgba||!Port_ModHudProject(0,&b->p,&x,&y))continue;
      if(b->green&&!sGreenDrop){int j;sGreenDrop=SDL_malloc(sDrop.w*sDrop.h*4);if(sGreenDrop)for(j=0;j<sDrop.w*sDrop.h;j++){int l=sDrop.rgba[j*4];sGreenDrop[j*4]=12+l/8;sGreenDrop[j*4+1]=55+l/2;sGreenDrop[j*4+2]=8+l/10;sGreenDrop[j*4+3]=sDrop.rgba[j*4+3];}}
      /* La gota verde (estallido autorado de la Historia) ya trae su color: se dibuja sin tinte. */
      if(b->green&&sGreenDrop)Port_Fx_SetDrawTint(255,255,255);else Port_Fx_SetDrawTint(b->r,b->g,b->b);
      edge=b->p;edge.y-=b->radius*2;
      if(!Port_ModHudProject(0,&edge,&ex,&ey))continue;size=(float)abs(ey-y);
      if(size<1)size=1;if(size>24)size=24;
      if(i==0&&getenv("BT3_STORY_SCENE_LOG")){static int trace; if(trace++<12)fprintf(stderr,"bt3: world blood draw life%d page(%d %d) diameter%.3f\n",b->life,x,y,size);}
      if(sDropFragment){
       static FxWorldVec trail;static int tx,ty;float dx,dy,len,stretch=size*(1.4f+.2f*(i%4));
       trail=b->p;trail.x+=b->v.x;trail.y+=b->v.y;trail.z+=b->v.z;
       if(!Port_ModHudProject(0,&trail,&tx,&ty)){tx=x+1;ty=y+1;}
       dx=tx-x;dy=ty-y;len=sqrtf(dx*dx+dy*dy);if(len<1){dx=1;dy=0;len=1;}
       Port_Fx_DrawFluid(b->green&&sGreenDrop?sGreenDrop:sDrop.rgba,sDrop.w,sDrop.h,sDrop.u0,sDrop.v0,sDrop.u1,sDrop.v1,sDrop.anchorV,(float)x,(float)y,x+dx/len*stretch,y+dy/len*stretch,b->life<5?b->life*45:220);
      }else Port_Fx_DrawSprite(b->green&&sGreenDrop?sGreenDrop:sDrop.rgba,sDrop.w,sDrop.h,(float)x,(float)y,size,b->life<5?b->life*45:220);
    }
    for (i = 0; i < FX_MAX; i++) {
        FxBurst *b = &sBursts[i];
        float t, size;
        int alpha;

        if (!b->used || b->img == NULL || b->img->rgba == NULL) {
            continue;
        }
        t = b->life0 > 0 ? 1.0f - (float)b->life / (float)b->life0 : 1.0f;
        size = b->size0 + (b->size1 - b->size0) * t;
        alpha = (int)((float)b->alpha0 * (1.0f - t));
        if (alpha < 1) {
            continue;
        }
        Port_Fx_SetDrawTint(b->r, b->g, b->b);
        Port_Fx_DrawSprite(b->img->rgba, b->img->w, b->img->h,
                           b->x, b->y, size, alpha);
    }
    Port_Fx_SetDrawTint(255, 255, 255); /* nada mas queda tintado despues de la sangre */
}

void Port_Fx_Term(void) {
    SDL_free(sSplash.rgba);
    SDL_free(sSpray.rgba);
    SDL_free(sDrop.rgba);SDL_free(sGreenDrop);sGreenDrop=NULL;
    sSplash.rgba = sSpray.rgba = sDrop.rgba = NULL;
    sLoaded = 0;
    Port_Fx_Clear();
}

/* World-scaled fluid spray for authored wounds; normal drop size limits stay unchanged. */
void Port_Fx_WorldWoundSpit(const FxWorldVec*p,const FxWorldVec*d,int h){fx_world_emit(p,d,h,1);}
