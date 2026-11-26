/* Owner: eft_a.c. Render actual solid octahedra and thin surface marks in native depth-sorted world space. */
#include "mod_ground_charge.h"
#include "mod_charge_atmosphere.h"
static GcaState sChargeAtmosphere;
#include "mod_terrain_bridge.h"
#include "mod_terrain_power.h"
#include "mod_terrain_charge.h"
static TdEvent sTerrainChargePlan[GC_ACTORS];
static int sTerrainCharged[GC_ACTORS];static long long sTerrainChargePower[GC_ACTORS];
static GcState sGroundCharge;
/* EftAeTex is layout-compatible with its native effect set; image points to TexEntry. */
typedef struct {u64 tex0;void*image;int reserved;} GcTex;
static GcTex sGcCapTex[2];static u64 sGcCapTex0[2];static int sGcCapKind;static GcVec sGcCapCenter,sGcCapU,sGcCapV;static float sGcCapRadius;
static GcVec sGcKiCenter;static float sGcKiRadius;
static u64 sGcNimbusTex0;static GcVec sGcNimbusCenter;static float sGcNimbusRadius;
static GcTex sGcTex[3];static u64 sGcTex0[3];static int sGcMaterialKind=1;static GcVec sGcOrigin;static float sGcRadius=1.f;
static GcVec sGcLight={.3f,1.f,.4f},sGcLightRgb={1,1,1};static float sGcTint=1.f;
extern void*Port_GroundStageTexture(int,int);extern void Port_GroundMaterialReset(void);extern int Port_GroundStageMaterial(int);
extern u64 EftVram_AddTex(void*,int,int);
extern void EftVram_GetCursor(int*,int*);
extern void BtlStage_GetLightVecB(void*);
extern float EftStage_GetTintScale(void);extern u8*BtlStage_GetLightColors(void);
extern int Battle_GetStage(void);
extern void Port_EftGroundTriangle(s32*,s32*,s32*,EftVec*,EftVec*,EftVec*,EftVec*,EftVec*,EftVec*,s32,u64,int);
/* Once before first-view upload, after gameplay effect allocations. No allocation during per-view drawing. */
extern void Port_GroundTrace(const char*,int,int,int,int);
void Port_GroundChargePrepare(void){int k,j,before,after,c,live=0;EftVec light;float len;static int trace;
 {extern void*Port_StoryStumpTexture(int);for(k=0;k<2;k++){void*entry=Port_StoryStumpTexture(k);sGcCapTex0[k]=0;if(entry){sGcCapTex[k].image=entry;sGcCapTex[k].tex0=*(u64*)((u8*)entry+0x30);sGcCapTex0[k]=EftVram_AddTex(sGcCapTex+k,1,0);}}}
 sGcTex0[0]=sGcTex0[1]=sGcTex0[2]=0;if(!sGroundCharge.quality)return;for(k=0;k<GC_MARKS;k++)live|=sGroundCharge.marks[k].live;if(!sGroundCharge.count&&!live&&!Port_TerrainStones(0))return;
 BtlStage_GetLightVecB(&light);len=sqrtf(light.x*light.x+light.y*light.y+light.z*light.z);
 if(isfinite(len)&&len>.001f)sGcLight=(GcVec){light.x/len,light.y/len,light.z/len};
 {u8*colors=BtlStage_GetLightColors();sGcLightRgb=(GcVec){1,1,1};if(colors){sGcLightRgb=(GcVec){.65f+GC_Float(colors[8])/384.f,.65f+GC_Float(colors[9])/384.f,.65f+GC_Float(colors[10])/384.f};}}
 sGcTint=EftStage_GetTintScale();if(!isfinite(sGcTint)||sGcTint<.1f||sGcTint>1.f)sGcTint=1.f;
 for(k=0;k<3;k++){void*entry=Port_GroundStageTexture(Battle_GetStage(),k);if(!entry)continue;
 sGcTex[k].image=entry;for(j=0;j<k;j++)if(sGcTex[j].image==entry&&sGcTex0[j])break;if(j<k){sGcTex0[k]=sGcTex0[j];continue;}
 sGcTex[k].tex0=*(u64*)((u8*)entry+0x30);
 EftVram_GetCursor(&before,&c);sGcTex0[k]=EftVram_AddTex(&sGcTex[k],1,0);EftVram_GetCursor(&after,&c);
 if(after<=before)sGcTex0[k]=0;
 if(trace<4){Port_GroundTrace("texture(kind,cursorBefore,after,TEX0low)",k,before,after,(int)sGcTex0[k]);trace++;}
 }
}

extern int Port_Setting(const char*,int),Port_NetActive(void);
extern int BattleReplay_IsActive(void);
extern void Port_GroundTrace(const char*,int,int,int,int);
static int sGcTickTrace,sGcDrawTrace,sGcTriangles,sGcClipped;
extern void Vu0Cur_Push(void),Vu0Cur_Pop(void),Vu0Cur_LoadMtx(void*);
extern void Port_EftSolidTriangle(s32*,s32*,s32*,EftVec*,EftVec*,EftVec*,EftVec*,EftVec*,EftVec*,s32);
void Port_GroundChargeReset(void){GCA_Reset(&sChargeAtmosphere);Port_TerrainReset();memset(sTerrainCharged,0,sizeof(sTerrainCharged));memset(sTerrainChargePlan,0,sizeof(sTerrainChargePlan));memset(sTerrainChargePower,0,sizeof(sTerrainChargePower));GC_Reset(&sGroundCharge);Port_GroundMaterialReset();sGcTex0[0]=sGcTex0[1]=sGcTex0[2]=0;}
void Port_GroundChargeTick(void){int q=Port_Setting("ground_charge_fx",0),stopped=BtlScene_IsTimeStopped();if(Port_NetActive()||BattleReplay_IsActive())q=0;GC_Step(&sGroundCharge,q,stopped);GCA_Step(&sChargeAtmosphere,q,stopped);
 Port_TerrainConfigure(Battle_GetStage(),q>0&&Port_GroundStageMaterial(Battle_GetStage())>=0);
 Port_TerrainSetMaterial(Battle_GetStage(),Port_GroundStageMaterial(Battle_GetStage()));
 if(q>0)Port_TerrainStonesTick(stopped);
 if(!stopped){int i;for(i=0;i<GC_ACTORS;i++){GcActor*a=sGroundCharge.actors+i;if(!a->live||a->fall){sTerrainCharged[i]=0;continue;}/* Small persistent pressure scar, then a gradual twelve-step excavation.
  * Keep Claude's surface/profile and the rising rocks; never a one-frame decal. */
 {extern long long Port_CharaPowerValue(int,int);float chargeRadius=TP_ChargeRadius(sTerrainChargePower[i],Port_CharaPowerValue(6,0));
 if(chargeRadius>0){int target=a->age<8?0:1+(a->age-8)/16;if(target>12)target=12;
 if(sTerrainCharged[i]<target){TdEvent e;int result;
 if(!sTerrainCharged[i]){memset(&e,0,sizeof(e));e.owner=i;e.fx=a->tier>=5?12:a->tier>=4?6:1;e.ax=e.bx=a->p.x;e.ay=a->p.y;e.az=e.bz=a->p.z;e.r=chargeRadius;e.depth=e.r*.12f;e.sx=-a->n.x/a->n.y;e.sz=-a->n.z/a->n.y;sTerrainChargePlan[i]=e;}
 e=sTerrainChargePlan[i];result=Port_TerrainCharge(&e,target);
 sTerrainCharged[i]=result?target:12;}}}}}



 if(q>0&&sGcTickTrace++<3)Port_GroundTrace("tick(quality,paused,count,marks)",q,stopped,sGroundCharge.count,sGroundCharge.next);
}
/* Stage profiles are explicit approximations, not texture reads from collision polygons. */
static int gc_surface(int stage){return Port_GroundStageMaterial(stage);}

void Port_GroundChargeTouch(int actor,void*ground,void*normal,int eligible,float height,int stage,long long power){EftVec*p=ground,*n=normal;int before=sGroundCharge.next,surface=gc_surface(stage),tier=GC_PowerTier(power);if(!p||!n)return;
 height=GC_EffectHeight(height,tier);
 GC_Touch(&sGroundCharge,actor,(GcVec){p->x,p->y,p->z},(GcVec){n->x,n->y,n->z},eligible&&surface>=0);
 if(actor>=0&&actor<GC_ACTORS&&eligible&&surface>=0){sTerrainChargePower[actor]=power;sGroundCharge.actors[actor].height=height;sGroundCharge.actors[actor].surface=surface;sGroundCharge.actors[actor].tier=tier;}
 if(before!=sGroundCharge.next){if(actor>=0&&actor<GC_ACTORS)sTerrainCharged[actor]=0;int at=(sGroundCharge.next+GC_MARKS-1)%GC_MARKS;sGroundCharge.marks[at].height=height;sGroundCharge.marks[at].surface=surface;sGroundCharge.marks[at].tier=tier;{extern long long Port_CharaPowerValue(int,int);if(TP_ChargeRadius(power,Port_CharaPowerValue(6,0))<=0)sGroundCharge.marks[at].live=0;}Port_GroundTrace("stamp(actor,material,heightx100,mark)",actor,surface,(int)(height*100),sGroundCharge.next);}
}
/* ClipVtx native ABI is position, UV, colour (not EftGfxVert's inferred field names).
   ProjectCur returns STQ, never RGBA. Keep the two outputs separate. */
typedef struct {EftVec pos,uv,col;} GcClipVert;
static void gc_color_triangle(GcVec a,GcVec b,GcVec c,GcVec rgb,float light,float alpha){
 GcClipVert v[16];EftGfxScr scr[9];EftVec stq[9],*plane;int count=3,i;
 GcVec points[3]={a,b,c};u64 tex=0;float nx,ny,nz,len,shade;int crust;
 nx=(b.y-a.y)*(c.z-a.z)-(b.z-a.z)*(c.y-a.y);ny=(b.z-a.z)*(c.x-a.x)-(b.x-a.x)*(c.z-a.z);nz=(b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x);
 len=sqrtf(nx*nx+ny*ny+nz*nz);shade=len>.00001f?-(nx*sGcLight.x+ny*sGcLight.y+nz*sGcLight.z)/len:0;if(shade<0)shade=0;
 if(sGcMaterialKind!=-3&&sGcMaterialKind!=-4&&sGcMaterialKind!=-5&&sGcMaterialKind!=-6)light*=sGcTint*(.48f+.52f*shade);
 crust=!(rgb.x==94&&rgb.y==67&&rgb.z==42)&&!(rgb.x==129&&rgb.y==99&&rgb.z==62);
 tex=sGcMaterialKind==-4?sGcNimbusTex0:sGcMaterialKind==-2?sGcCapTex0[sGcCapKind]:sGcMaterialKind>=0&&sGcMaterialKind<3?sGcTex0[sGcMaterialKind]:0;if(tex&&sGcMaterialKind!=-4){rgb=(GcVec){128,128,128};light=sGcTint*(.82f+.18f*shade);}else if(!tex&&sGcMaterialKind!=-3&&sGcMaterialKind!=-4&&sGcMaterialKind!=-5&&sGcMaterialKind!=-6){rgb.x*=sGcLightRgb.x;rgb.y*=sGcLightRgb.y;rgb.z*=sGcLightRgb.z;}
 memset(v,0,sizeof(v));
 for(i=0;i<3;i++){v[i].pos=(EftVec){points[i].x,points[i].y,points[i].z,1};v[i].col=(EftVec){rgb.x*light,rgb.y*light,rgb.z*light,128*alpha};if(sGcMaterialKind==-6){float dx=points[i].x-sGcKiCenter.x,dy=points[i].y-sGcKiCenter.y,dz=points[i].z-sGcKiCenter.z,fade=1-sqrtf(dx*dx+dy*dy+dz*dz)/sGcKiRadius;if(fade<0)fade=0;v[i].col.w*=.18f*fade;}/* Planar triplanar choice avoids stretching vertical cliff faces. */
 if(sGcMaterialKind==-4||sGcMaterialKind==-5){float dx=(points[i].x-sGcNimbusCenter.x)/sGcNimbusRadius,dy=(points[i].y-sGcNimbusCenter.y)/(sGcNimbusRadius*.38f),dz=(points[i].z-sGcNimbusCenter.z)/sGcNimbusRadius,shade=dy<-.25f?1.f:dy<.45f?.82f:.62f;
 v[i].col.x*=shade;v[i].col.y*=shade;v[i].col.z*=shade;
 v[i].uv=(EftVec){.5f+dx*.5f,.5f+dz*.5f,1,0};
 if(sGcMaterialKind==-4){v[i].pos.x=sGcNimbusCenter.x+(points[i].x-sGcNimbusCenter.x)*1.004f;v[i].pos.y=sGcNimbusCenter.y+(points[i].y-sGcNimbusCenter.y)*1.004f;v[i].pos.z=sGcNimbusCenter.z+(points[i].z-sGcNimbusCenter.z)*1.004f;}
 }
 else if(sGcMaterialKind==-2){GcVec delta={points[i].x-sGcCapCenter.x,points[i].y-sGcCapCenter.y,points[i].z-sGcCapCenter.z};v[i].uv=(EftVec){.5f+(delta.x*sGcCapU.x+delta.y*sGcCapU.y+delta.z*sGcCapU.z)/(2*sGcCapRadius),.5f+(delta.x*sGcCapV.x+delta.y*sGcCapV.y+delta.z*sGcCapV.z)/(2*sGcCapRadius),1,0};}
 else if(sGcMaterialKind==2)v[i].uv=(EftVec){.5f+(points[i].x-sGcOrigin.x)*.25f/sGcRadius,.5f+(points[i].z-sGcOrigin.z)*.25f/sGcRadius,1,0};
 else if(fabsf(ny)>=fabsf(nx)&&fabsf(ny)>=fabsf(nz))v[i].uv=(EftVec){points[i].x*.18f,points[i].z*.18f,1,0};
 else if(fabsf(nx)>fabsf(nz))v[i].uv=(EftVec){points[i].z*.18f,points[i].y*.18f,1,0};
 else v[i].uv=(EftVec){points[i].x*.18f,points[i].y*.18f,1,0};}
 plane=EftGfx_GetClipPlanes();for(i=0;i<5&&count;i++)count=ClipPoly_ClipPlane((EftGfxVert*)v,plane++,count);
 if(count<3){sGcClipped++;return;}ClipPoly_ProjectCur((void*)scr,stq,(EftGfxVert*)v,count);
 for(i=2;i<count;i++){int z=(int)(((long long)scr[0].z+scr[i-1].z+scr[i].z)/3)>>8;
 /* The Nimbus surface is closed. Avoid drawing its hidden back faces into the
    effect order table and use opaque packets for the opaque surfaces. */
 if(sGcMaterialKind==-5){long long area=((long long)scr[i-1].x-scr[0].x)*(scr[i].y-scr[0].y)-((long long)scr[i-1].y-scr[0].y)*(scr[i].x-scr[0].x);if(area<=0)continue;}
 sGcTriangles++;Port_EftGroundTriangle((void*)scr,(void*)(scr+i-1),(void*)(scr+i),&v[0].col,&v[i-1].col,&v[i].col,stq,stq+i-1,stq+i,z,tex,sGcMaterialKind==-5&&alpha>=.999f);}
}
static void gc_triangle(GcVec a,GcVec b,GcVec c,float light,float alpha){gc_color_triangle(a,b,c,(GcVec){95,77,56},light,alpha);}
#define GC_MESH_TRI gc_color_triangle
#define GC_MATERIAL(k) (sGcMaterialKind=(k))
#define GC_DOMAIN(p,r) (sGcOrigin=(p),sGcRadius=(r))
#include "mod_ground_mesh.h"
void Port_GroundChargeAtmosphereTouch(int actor,long long power,int charging,int max,int eligible){
 extern int Port_StoryActive(void);
 static int seen[GC_ACTORS];
 if (Port_StoryActive() && power < GCA_STORY_THRESHOLD) power = 0;
 GCA_Touch(&sChargeAtmosphere,actor,power,charging,max,eligible&&GCA_StageAllowed(Battle_GetStage()));
 if(actor>=0&&actor<GC_ACTORS&&(charging||max)&&seen[actor]++<3)Port_GroundTrace("atmosphere(actor,strength,eligible,chargingMax)",actor,GCA_Power(power),eligible,charging+2*max);
}
int Port_GroundChargeSkyActive(void){if(!sGroundCharge.quality||!GCA_StageAllowed(Battle_GetStage())||Port_NetActive()||BattleReplay_IsActive())return 0;return GCA_Active(&sChargeAtmosphere);}

static void gc_story_tail_cap(void){extern int Port_StoryStumpInfo(int,float*);float q[7],len;GcVec c,u,v,n;int i,kind,old=sGcMaterialKind;
 for(kind=0;kind<2;kind++){
 if(!Port_StoryStumpInfo(kind,q))continue;c=(GcVec){q[0],q[1],q[2]};n=(GcVec){q[4],q[5],q[6]};
 u=(GcVec){n.z,0,-n.x};len=sqrtf(u.x*u.x+u.z*u.z);if(len<.01f){u=(GcVec){1,0,0};len=1;}u.x/=len;u.z/=len;
 v=(GcVec){n.y*u.z-n.z*u.y,n.z*u.x-n.x*u.z,n.x*u.y-n.y*u.x};sGcMaterialKind=-2;sGcCapKind=kind;sGcCapCenter=c;sGcCapU=u;sGcCapV=v;sGcCapRadius=q[3];
 if(kind==0){extern int Port_StoryTailCapPoint(int,float*);float center[3];
 /* Keep a short, closed fur sleeve at the native seam; the cut face has depth. */
 sGcMaterialKind=-1;
 for(i=0;i<12;i++){float aa[3],bb[3],cc[3],dd[3];GcVec a,b,c,d,color={39,18,13};int j=(i+1)%12;
 if(!Port_StoryTailCapPoint(i,aa)||!Port_StoryTailCapPoint(j,bb)||!Port_StoryTailCapPoint(13+j,cc)||!Port_StoryTailCapPoint(13+i,dd))break;
 a=(GcVec){aa[0],aa[1],aa[2]};b=(GcVec){bb[0],bb[1],bb[2]};c=(GcVec){cc[0],cc[1],cc[2]};d=(GcVec){dd[0],dd[1],dd[2]};
 gc_color_triangle(a,b,c,color,1,1);gc_color_triangle(a,c,d,color,1,1);gc_color_triangle(a,c,b,color,1,1);gc_color_triangle(a,d,c,color,1,1);}
 sGcMaterialKind=-2;if(!Port_StoryTailCapPoint(12,center))continue;c=(GcVec){center[0],center[1],center[2]};sGcCapCenter=c;for(i=0;i<12;i++){float a[3],b[3];GcVec p,r,color={83,18,19};if(!Port_StoryTailCapPoint(i,a)||!Port_StoryTailCapPoint((i+1)%12,b))break;p=(GcVec){a[0],a[1],a[2]};r=(GcVec){b[0],b[1],b[2]};gc_color_triangle(c,p,r,color,1,1);gc_color_triangle(c,r,p,color,1,1);}continue;}
 if(kind==1){extern int Port_StoryArmCapPoint(int,float*);for(i=0;i<10;i++){float a[3],b[3];GcVec p,r,color={29,48,12};if(!Port_StoryArmCapPoint(i,a)||!Port_StoryArmCapPoint((i+1)%10,b))break;p=(GcVec){a[0],a[1],a[2]};r=(GcVec){b[0],b[1],b[2]};gc_color_triangle(c,p,r,color,1,1);gc_color_triangle(c,r,p,color,1,1);}continue;}
 for(i=0;i<16;i++){float a=i*.392699082f,b=(i+1)*.392699082f;GcVec p={c.x+q[3]*(u.x*cosf(a)+v.x*sinf(a)),c.y+q[3]*(u.y*cosf(a)+v.y*sinf(a)),c.z+q[3]*(u.z*cosf(a)+v.z*sinf(a))};
 GcVec r={c.x+q[3]*(u.x*cosf(b)+v.x*sinf(b)),c.y+q[3]*(u.y*cosf(b)+v.y*sinf(b)),c.z+q[3]*(u.z*cosf(b)+v.z*sinf(b))};GcVec color=kind?(GcVec){29,48,12}:(GcVec){83,18,19};
 gc_color_triangle(c,p,r,color,1,1);gc_color_triangle(c,r,p,color,1,1);}
 }sGcMaterialKind=old;
}
/* Trace the outside of the original body lobes along each ray from the centre.
   One closed shell avoids overlapping depth-sorted faces and floating ribbons. */
static GcVec gc_nimbus_vertex(GcVec center,float radius,float cy,float sy,float a,float b,int body)
{
    static const float lobes[][4]={
        {-.65f,0,0,.65f},{.65f,0,0,.65f},{0,-.15f,.5f,.65f},
        {0,-.1f,-.5f,.7f},{-.5f,-.1f,-.45f,.5f},{.5f,-.1f,-.45f,.5f}
    };
    float x=sinf(a)*cosf(b),y=-cosf(a),z=sinf(a)*sinf(b);
    float outer=1.f;
    int i;
    for(i=0;body&&i<sizeof(lobes)/sizeof(lobes[0]);i++){
        float lx=lobes[i][0],ly=lobes[i][1]/.38f,lz=lobes[i][2],lr=lobes[i][3];
        float projection=x*lx+y*ly+z*lz;
        float discriminant=projection*projection+lr*lr-lx*lx-ly*ly-lz*lz;
        if(discriminant>0){
            float exit=projection+sqrtf(discriminant);
            if(exit>outer)outer=exit;
        }
    }
    radius*=outer;
    return (GcVec){center.x+radius*(cy*x+sy*z),center.y+radius*.38f*y,center.z+radius*(-sy*x+cy*z)};
}

static void gc_nimbus_mesh(GcVec center,float radius,float cy,float sy,int body)
{
    enum { BODY_LON=24 };
    const GcVec gold=body?(GcVec){252,205,50}:(GcVec){248,187,42};
    GcVec rings[2][BODY_LON];
    float latitude=0.f;
    float lat_step=body?3.14159265f/10:3.14159265f/5;
    float lon_step=body?6.2831853f/BODY_LON:6.2831853f/10;
    int lat=body?10:5,lon=body?BODY_LON:10;
    int row,j;

    sGcNimbusCenter=center;
    sGcNimbusRadius=radius;
    for(row=0;row<=lat;row++,latitude+=lat_step){
        GcVec *ring=rings[row&1],*prev=rings[(row+1)&1];
        float longitude=0.f;
        for(j=0;j<lon;j++,longitude+=lon_step)
            ring[j]=gc_nimbus_vertex(center,radius,cy,sy,latitude,
                row==0||row==lat?0.f:longitude,body);
        if(!row)continue;
        for(j=0;j<lon;j++){
            int next=(j+1)%lon;
            if(row>1)gc_color_triangle(prev[j],prev[next],ring[next],gold,1,1);
            if(row<lat)gc_color_triangle(prev[j],ring[next],ring[j],gold,1,1);
        }
    }
}

static void gc_story_nimbus(void)
{
    extern int Port_StoryNimbusInfo(float*),Port_StoryNimbusExhaustAge(void);
    float q[5],cy,sy,age;
    GcVec center;
    int i,tick,old=sGcMaterialKind;
    if(!Port_StoryNimbusInfo(q))return;
    center=(GcVec){q[0],q[1],q[2]};
    cy=cosf(q[4]);sy=sinf(q[4]);
    sGcMaterialKind=-5;
    gc_nimbus_mesh(center,q[3],cy,sy,1);

    tick=Port_StoryNimbusExhaustAge();
    age=(float)(tick&15)*.015625f;
    for(i=0;i<4;i++,age+=.25f){
        float back=q[3]*(1.12f+2.7f*age);
        float side=q[3]*.30f*sinf(age*5.5f)*age;
        float radius=q[3]*(.28f+.26f*age)*(1.f-age);
        GcVec puff={center.x-sy*back+cy*side,center.y-q[3]*.6f*age,
                    center.z-cy*back-sy*side};
        gc_nimbus_mesh(puff,radius,cy,sy,0);
    }
    sGcMaterialKind=old;
}
static void gc_story_kienzan(void){extern int Port_StoryKienzanInfo(float*);float q[4];int i,old=sGcMaterialKind;GcVec center;if(!Port_StoryKienzanInfo(q))return;sGcMaterialKind=-3;center=(GcVec){q[0],q[1],q[2]};
 for(i=0;i<32;i++){float a=i*.19634954f,b=(i+1)*.19634954f;GcVec p={q[0]+q[3]*cosf(a),q[1],q[2]+q[3]*sinf(a)},r={q[0]+q[3]*cosf(b),q[1],q[2]+q[3]*sinf(b)},color={255,250,165};GcVec u={q[0]+q[3]*1.13f*cosf(a),q[1],q[2]+q[3]*1.13f*sinf(a)},v={q[0]+q[3]*1.13f*cosf(b),q[1],q[2]+q[3]*1.13f*sinf(b)},gold={255,205,20};
 gc_color_triangle(center,p,r,color,1,1);gc_color_triangle(center,r,p,color,1,1);gc_color_triangle(p,u,v,gold,1,.8f);gc_color_triangle(p,v,r,gold,1,.8f);gc_color_triangle(p,v,u,gold,1,.8f);gc_color_triangle(p,r,v,gold,1,.8f);
 }sGcMaterialKind=old;}
static int gc_draw_impact_stones(void){
 TvaStone stones[TVA_CAPACITY];
 int i,n=Port_TerrainStones(stones),drawn=0;
 int surface=gc_surface(Battle_GetStage());
 if(surface<0)return 0;
 for(i=0;i<n&&drawn<GC_ROCKS;i++){
  float p[5];GcRock rock;
  TVA_Pose(stones+i,p);
  if(p[4]<=.005f)continue;
  rock=(GcRock){{p[0],p[1],p[2]},p[3],1.f,p[4],stones[i].seed,surface};
  GC_RockMesh(&rock,1);drawn++;
 }
 return drawn;
}
void Port_GroundChargeDraw(void){int i,j,shown=0,maxMarks=sGroundCharge.quality==1?2:4;
 if(!gBtlCamView)return;
 sGcTriangles=sGcClipped=0;Vu0Cur_Push();Vu0Cur_LoadMtx(gBtlCamView+0x140);
 gc_story_kienzan();gc_story_nimbus();gc_story_tail_cap();if(!sGroundCharge.quality){Vu0Cur_Pop();return;}
 {int impacts=gc_draw_impact_stones();
  for(i=0;i<sGroundCharge.count&&i+impacts<GC_ROCKS;i++)
   if(sGroundCharge.rocks[i].alpha>.005f)GC_RockMesh(sGroundCharge.rocks+i,sGroundCharge.quality);
 }
 for(j=0;j<GC_MARKS&&shown<maxMarks;j++){i=(sGroundCharge.next+GC_MARKS-1-j)%GC_MARKS;if(sGroundCharge.marks[i].live&&!Port_TerrainEnabled()&&!Port_TerrainHasScar(&sGroundCharge.marks[i].p)){GC_MarkMesh(sGroundCharge.marks+i,sGroundCharge.quality);shown++;}}
 Vu0Cur_Pop();if(sGroundCharge.count>0&&sGcDrawTrace++<3)Port_GroundTrace("draw(rocks,triangles,clipped,marks)",sGroundCharge.count,sGcTriangles,sGcClipped,shown);
}

int Port_GroundChargeSkyLevel(void){if(!sGroundCharge.quality||!GCA_StageAllowed(Battle_GetStage())||Port_NetActive()||BattleReplay_IsActive())return 0;return sChargeAtmosphere.level;}

int Port_GroundChargeStormLevel(void){if(!sGroundCharge.quality||!GCA_StageAllowed(Battle_GetStage())||Port_NetActive()||BattleReplay_IsActive())return 0;return GCA_Storm(&sChargeAtmosphere);}
