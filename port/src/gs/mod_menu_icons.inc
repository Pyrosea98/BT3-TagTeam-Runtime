#include <math.h>
// Scalable menu symbols. Each icon owns its geometry: no atlas cells or crop guesses.
enum {MI_VIDEO,MI_EFFECTS,MI_AUDIO,MI_INTERFACE,MI_CONTROLS,MI_CHEATS,MI_SYSTEM,MI_EXTRAS,MI_HUD,MI_FUSION,MI_PLAY,MI_CREDITS,MI_RETURN,MI_TEAM,MI_COOP,MI_FFA,MI_TRAINING,MI_TROPHY,MI_STORY,MI_STRUGGLES};
// Approved previous-port DBZ artwork, exact whole atlas cells with a half-texel inset.
static SDL_GPUTexture *mod_parity_art(const char *name){
 char path[1000];snprintf(path,sizeof(path),"%s/tagteam-ui/%s.rgba",Port_FileRoot(),name);FILE *f=fopen(path,"rb");if(!f)return nullptr;
 unsigned shape[2];if(fread(shape,4,2,f)!=2||!shape[0]||!shape[1]||shape[0]>4096||shape[1]>4096){fclose(f);return nullptr;}
 size_t n=(size_t)shape[0]*shape[1]*4;auto *data=(unsigned char*)malloc(n);if(!data||fread(data,1,n,f)!=n){free(data);fclose(f);return nullptr;}fclose(f);return Ui_Texture(data,shape[0],shape[1]);
}
// One texture per symbol: UV0..1 can never sample a neighbouring atlas icon.
static bool mod_dbz_icon(ImDrawList *d,int kind,ImVec2 c,float r){
 static SDL_GPUTexture *textures[20];static bool tried[20];
 static const char *names[]={"video","effects","audio","interface","controls","cheats","system","extra-fighters","battle-hud","fusion","play","credits","return","team-battle","coop-battle","free-for-all","cpu-training","tournament","scenarios","struggles"};
 if(kind<0||kind>=20)return false;
 if(!tried[kind]){tried[kind]=true;char path[100];snprintf(path,sizeof(path),"menu-hd/%s",names[kind]);textures[kind]=mod_parity_art(path);}
 if(!textures[kind])return false;
 d->AddImage(ImTextureRef((ImTextureID)(intptr_t)textures[kind]),ImVec2(c.x-r,c.y-r),ImVec2(c.x+r,c.y+r),ImVec2(0,0),ImVec2(1,1));return true;
}

static void mod_icon_draw(ImDrawList *d,int kind,ImVec2 c,float r,ImU32 col){
 if(mod_dbz_icon(d,kind,c,r))return;
 auto p=[&](float x,float y){return ImVec2(c.x+x*r,c.y+y*r);};float w=SDL_max(1.f,r*.10f);
 auto line=[&](float x,float y,float a,float b){d->AddLine(p(x,y),p(a,b),col,w);};
 auto circle=[&](float x,float y,float rad){d->AddCircle(p(x,y),rad*r,col,24,w);};
 auto box=[&](float x,float y,float a,float b){d->AddRect(p(x,y),p(a,b),col,r*.1f,0,w);};
 auto star=[&](float x,float y,float rad){ImVec2 pts[10];for(int n=0;n<10;n++){float a=-1.570796f+n*.6283185f,z=n%2?rad*.43f:rad;pts[n]=p(x+cosf(a)*z,y+sinf(a)*z);}d->AddPolyline(pts,10,col,ImDrawFlags_Closed,w*.8f);};
 switch(kind){
 case MI_VIDEO:box(-.92f,-.65f,.92f,.55f);line(0,.55f,0,.9f);line(-.5f,.9f,.5f,.9f);break;
 case MI_STRUGGLES:case MI_EFFECTS:{ImVec2 pts[]={p(.1f,-1),p(-.65f,.12f),p(-.07f,.12f),p(-.23f,1),p(.7f,-.25f),p(.1f,-.25f)};d->AddConcavePolyFilled(pts,6,col);break;}
 case MI_AUDIO:line(-.55f,.5f,-.55f,-.7f);line(-.55f,-.7f,.6f,-.95f);line(.6f,-.95f,.6f,.3f);circle(-.75f,.6f,.25f);circle(.4f,.4f,.25f);break;
 case MI_INTERFACE:box(-.9f,-.8f,.9f,.8f);line(-.9f,-.25f,.9f,-.25f);line(-.25f,-.25f,-.25f,.8f);break;
 case MI_CONTROLS:box(-.95f,-.5f,.95f,.65f);line(-.7f,0,-.2f,0);line(-.45f,-.25f,-.45f,.25f);circle(.4f,-.1f,.12f);circle(.65f,.2f,.12f);break;
 case MI_CHEATS:circle(0,0,.9f);star(0,0,.65f);break;
 case MI_SYSTEM:circle(0,0,.43f);circle(0,0,.8f);for(int i=0;i<8;i++){float a=i*.785398f;line(cosf(a)*.8f,sinf(a)*.8f,cosf(a),sinf(a));}break;
 case MI_EXTRAS:case MI_COOP:case MI_TEAM:circle(-.4f,-.45f,.25f);circle(.45f,-.45f,.25f);box(-.8f,0,-.03f,.7f);box(.07f,0,.85f,.7f);if(kind==MI_EXTRAS){line(.1f,.4f,.65f,.4f);line(.38f,.12f,.38f,.68f);}if(kind==MI_TEAM)line(-.85f,.95f,.9f,.95f);break;
 case MI_HUD:circle(0,0,.85f);circle(0,0,.62f);line(-.55f,.95f,.55f,.95f);break;
 case MI_FUSION:line(-.95f,-.6f,-.15f,0);line(-.95f,.6f,-.15f,0);line(.95f,-.6f,.15f,0);line(.95f,.6f,.15f,0);star(0,0,.5f);break;
 case MI_PLAY:circle(0,0,.94f);star(0,0,.55f);break;
 case MI_CREDITS:circle(0,0,.9f);circle(0,-.45f,.07f);line(0,-.08f,0,.55f);break;
 case MI_RETURN:box(-.3f,-.85f,.85f,.85f);line(-1,0,.35f,0);line(-1,0,-.5f,-.5f);line(-1,0,-.5f,.5f);break;
 case MI_FFA:for(int i=0;i<3;i++){float a=-1.570796f+i*2.094395f;circle(cosf(a)*.6f,sinf(a)*.6f,.27f);line(cosf(a)*.3f,sinf(a)*.3f,0,0);}break;
 case MI_TRAINING:line(-.8f,-.75f,.8f,.75f);line(-.8f,.75f,.8f,-.75f);circle(0,0,.5f);break;
 case MI_TROPHY:box(-.55f,-.85f,.55f,.25f);line(0,.25f,0,.85f);line(-.55f,.85f,.55f,.85f);line(-.55f,-.5f,-.95f,-.5f);line(-.95f,-.5f,-.75f,.1f);line(-.75f,.1f,-.55f,.1f);line(.55f,-.5f,.95f,-.5f);line(.95f,-.5f,.75f,.1f);line(.75f,.1f,.55f,.1f);break;
 case MI_STORY:box(-.95f,-.75f,.95f,.75f);line(0,-.75f,0,.75f);line(-.65f,-.35f,-.25f,-.35f);line(.25f,-.35f,.65f,-.35f);break;
 }
}
