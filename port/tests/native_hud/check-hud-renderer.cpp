#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <math.h>
#include "imgui.h"
#include "fight_panels.h"
struct SDL_GPUTexture {};
#define SDL_malloc malloc
#define SDL_free free
static FpState fixture;static int mode;static int opacity=100,backing=100,hudSize=100;static int texId=10;static char root[512];
int gUiPresentW,gUiPresentH,gUiPresentX,gUiPresentY;
const char *Port_FileRoot(){return root;}int Port_Setting(const char *s,int def){return !strcmp(s,"mod_hud_style")?mode:!strcmp(s,"mod_hud_opacity_percent")?opacity:!strcmp(s,"mod_hud_background_percent")?backing:!strcmp(s,"mod_hud_scale_percent")?hudSize:def;}
int PortPanels_Read(FpState *out){*out=fixture;return 1;}
SDL_GPUTexture *Ui_Texture(uint8_t *p,uint32_t,uint32_t){free(p);return (SDL_GPUTexture*)(intptr_t)texId++;}
void Ui_TextureFree(SDL_GPUTexture*){}
static ImFont *sModBodyFont,*sModTitleFont;
ImFont *Ui_FontBold(){return ImGui::GetFont();}
#define SDL_max(a,b) ((a)>(b)?(a):(b))
#include "mod_menu_icons.inc"
#include "mod_legacy_font.inc"
#include "mod_battle_hud.inc"
int main(int argc,char **argv){
 snprintf(root,sizeof(root),"%s",argc>1?argv[1]:"/profile/gamedata");ImGui::CreateContext();auto &io=ImGui::GetIO();io.IniFilename=NULL;io.BackendFlags|=ImGuiBackendFlags_RendererHasVtxOffset;io.Fonts->AddFontDefault();char fp[700];snprintf(fp,sizeof(fp),"%s/tagteam-ui/menu-body.ttf",root);FILE *fontFile=fopen(fp,"rb");if(fontFile){fclose(fontFile);sModBodyFont=io.Fonts->AddFontFromFileTTF(fp,48.f);}snprintf(fp,sizeof(fp),"%s/tagteam-ui/menu-title.ttf",root);fontFile=fopen(fp,"rb");if(fontFile){fclose(fontFile);sModTitleFont=io.Fonts->AddFontFromFileTTF(fp,64.f);}unsigned char *pix;int fw,fh;io.Fonts->GetTexDataAsRGBA32(&pix,&fw,&fh);io.Fonts->SetTexID(1);
 int cases=0;int widths[]={1440,1920,2520};
 for(int wi=0;wi<3;wi++)for(int views=1;views<=4;views++)for(mode=0;mode<6;mode++)for(int extreme=0;extreme<2;extreme++){
  opacity=extreme?25:100;backing=extreme?0:100;hudSize=extreme?160:65;
  gUiPresentW=widths[wi];gUiPresentH=1080;gUiPresentX=16;gUiPresentY=8;io.DisplaySize=ImVec2(gUiPresentW+32,1096);io.DeltaTime=1.f/60;
  memset(&fixture,0,sizeof(fixture));fixture.battle=fixture.gauges=1;fixture.actorCount=2;fixture.viewCount=views;
  for(int q=0;q<views;q++){
   auto &v=fixture.view[q];v.x=views==1?0:(q&1)*256;v.y=views>2&&q>=2?224:0;v.w=views==1||(views==3&&q==2)?512:256;v.h=views>2?224:448;v.me=0;v.target=1;
   for(int i=0;i<2;i++){fixture.actor[i].visible[q]=1;fixture.actor[i].x[q]=v.x+v.w*(i?3:1)/4;fixture.actor[i].y[q]=v.y+v.h/2;}
  }
  for(int i=0;i<2;i++){auto &a=fixture.actor[i];a.present=1;a.fusionMove=a.fusionAttack=-1;a.chara=i?53:52;a.costume=0;a.hp=i?10001:130000;a.hpMax=150000;a.ki=75900;a.kiMax=100000;a.blast=285000;a.blastMax=400000;a.poweredUp=1;a.player=i+1;a.fusionSeconds=75;a.fusionProgress=extreme?120:750;a.fusionAccept=i==1;snprintf(a.name,sizeof(a.name),"Long costume-specific fighter name %d",i);}
  fixture.reviveRing[1].active=1;fixture.reviveRing[1].team=0;
  for(int q=0;q<views;q++){fixture.view[q].takeover=extreme?2:1;for(int j=0;j<25;j++){fixture.reviveRing[1].visible[q][j]=1;fixture.reviveRing[1].x[q][j]=fixture.view[q].x+fixture.view[q].w/2+(int)(20*cosf(j*6.2831853f/24));fixture.reviveRing[1].y[q][j]=fixture.view[q].y+fixture.view[q].h/2+(int)(10*sinf(j*6.2831853f/24));}}
  fixture.actor[0].assistKind=extreme?3:1;fixture.actor[0].assistState=extreme?3:1;
  ImGui::NewFrame();assert(mod_battle_hud_visible());mod_battle_hud_draw();ImGui::Render();auto *data=ImGui::GetDrawData();assert(data->TotalVtxCount>0);
  for(int l=0;l<data->CmdListsCount;l++){
   auto *dl=data->CmdLists[l];for(auto &v:dl->VtxBuffer){assert(isfinite(v.pos.x)&&isfinite(v.pos.y)&&isfinite(v.uv.x)&&isfinite(v.uv.y));}
   for(auto &cmd:dl->CmdBuffer)if(cmd.ElemCount){assert(cmd.ClipRect.x>=gUiPresentX-.01f&&cmd.ClipRect.y>=gUiPresentY-.01f);assert(cmd.ClipRect.z<=gUiPresentX+gUiPresentW+.01f&&cmd.ClipRect.w<=gUiPresentY+gUiPresentH+.01f);assert(cmd.IdxOffset+cmd.ElemCount<=(unsigned)dl->IdxBuffer.Size);for(unsigned j=0;j<cmd.ElemCount;j++)assert(cmd.VtxOffset+dl->IdxBuffer[cmd.IdxOffset+j]<(unsigned)dl->VtxBuffer.Size);}
  }
  cases++;
 }
 {char manifest[700];snprintf(manifest,sizeof(manifest),"%s/tagteam-ui/menu-hd/manifest.json",root);FILE *f=fopen(manifest,"rb");if(f){fclose(f);ImGui::NewFrame();auto *d=ImGui::GetForegroundDrawList();for(int i=0;i<=MI_STRUGGLES;i++)assert(mod_dbz_icon(d,i,ImVec2(100,100),30));assert(sModBodyFont&&sModTitleFont);ImGui::Render();puts("PASS all20independentHDicon C++loads and original display-resolution font families");}}
 ImGui::DestroyContext();printf("PASS actual HUD renderer: %d cases, opacity/background/size extremes x all6styles x4view layouts x4:3/16:9/21:9, finite geometry and viewport clipping\n",cases);
}
