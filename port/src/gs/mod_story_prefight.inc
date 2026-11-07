// Full-window pre-fight presentation. Immutable snapshot only; no game pointers or audio on render thread.
#include "../../include/mod_story_presentation.h"
static int sStpHd;
static SDL_GPUTexture*sStpTexture;static int sStpChapter=-1,sStpPhase=-1;static char sStpResource[160];
static SDL_GPUTexture*stp_art(int chapter,int phase){
 if(chapter==sStpChapter&&phase==sStpPhase)return sStpTexture;
 sStpChapter=chapter;sStpPhase=phase;sStpHd=0;char name[160],path[1600];int profile=STP_Profile(stChapters[chapter].saga);
 snprintf(name,sizeof(name),"history-prefight/%s-%s",stChapters[chapter].id,phase==1?"recap":"title");
 snprintf(path,sizeof(path),"%s/tagteam-ui/%s.rgba",Port_FileRoot(),name);FILE*f=fopen(path,"rb");
 if(f)fclose(f);else if(phase==1)snprintf(name,sizeof(name),"history-prefight/recap-%s-v1",profile==5?"gt":profile==6?"db":"z");
 else {
  snprintf(name,sizeof(name),"history-prefight/title-hd-%d-v2",profile);
  snprintf(path,sizeof(path),"%s/tagteam-ui/%s.rgba",Port_FileRoot(),name);FILE*hd=fopen(path,"rb");
  sStpHd=hd!=nullptr;if(hd)fclose(hd);else snprintf(name,sizeof(name),"history-prefight/title-%d",profile);
 }
 if(strcmp(name,sStpResource)){if(sStpTexture)Ui_TextureFree(sStpTexture);sStpTexture=mod_menu_art(name);snprintf(sStpResource,sizeof(sStpResource),"%s",name);}
 return sStpTexture;
}
static void stp_image(ImDrawList*d,SDL_GPUTexture*t,ImVec2 pos,ImVec2 size,ImVec2 uv0,ImVec2 uv1,int alpha=255){if(t)d->AddImage(ImTextureRef((ImTextureID)(intptr_t)t),pos,ImVec2(pos.x+size.x,pos.y+size.y),uv0,uv1,IM_COL32(255,255,255,alpha));}
static void stp_outline(ImDrawList*d,ImFont*f,float fs,ImVec2 pos,ImU32 color,const char*text,float width,float stroke){
 for(int n=0;n<8;n++){float a=n*0.78539816f;d->AddText(f,fs,ImVec2(pos.x+cosf(a)*stroke,pos.y+sinf(a)*stroke),IM_COL32(0,0,0,245),text,nullptr,width);}
 d->AddText(f,fs,pos,color,text,nullptr,width);
}
static void mod_story_prefight_draw(int chapter,int phase,int raw){
 if(chapter<0||chapter>=ST_COUNT||phase<1||phase>2)return;
 ImDrawList*d=ImGui::GetForegroundDrawList();ImVec2 pos((float)gUiPresentX,(float)gUiPresentY),size((float)gUiPresentW,(float)gUiPresentH);
 if(size.x<=0||size.y<=0)return;float k=size.y/720.f,p=SDL_clamp(raw/65535.f,0.f,1.f);int L=Port_Setting("mod_language",1)==1;
 const StChapter&ch=stChapters[chapter];SDL_GPUTexture*t=stp_art(chapter,phase);ImFont*f=story_font();
 d->PushClipRect(pos,ImVec2(pos.x+size.x,pos.y+size.y),true);d->AddRectFilled(pos,ImVec2(pos.x+size.x,pos.y+size.y),IM_COL32(9,12,29,255));
 if(phase==1){
  // Original custom illustration, independent from the anime title card. Gentle drift, never stretching.
  float imageAspect=1916.f/821.f,target=size.x/size.y,u=0,v=0;
  if(target<imageAspect)u=(1-target/imageAspect)*.5f;else v=(1-imageAspect/target)*.5f;
  float drift=(p-.5f)*.012f;stp_image(d,t,pos,size,ImVec2(SDL_clamp(u+drift,0.f,.49f),v),ImVec2(SDL_clamp(1-u+drift,.51f,1.f),1-v));
  d->AddRectFilled(pos,ImVec2(pos.x+size.x,pos.y+size.y),IM_COL32(8,10,29,164));
  float width=SDL_min(size.x*.82f,1080*k),x=pos.x+(size.x-width)*.5f,top=pos.y+146*k,bottom=pos.y+size.y-112*k;
  const char*body=STP_Text(chapter,L);float fs=28*k;ImVec2 extent=f->CalcTextSizeA(fs,100000,width,body);
  d->PushClipRect(ImVec2(x-5*k,top),ImVec2(x+width+5*k,bottom),true);
  float y=bottom+28*k-p*(bottom-top+extent.y+56*k);
  stp_outline(d,f,fs,ImVec2(x,y),IM_COL32(255,250,231,255),body,width,1.3f*k);d->PopClipRect();
  // Soft masking at scroll edges, without a generic plate over the art.
  d->AddRectFilledMultiColor(pos,ImVec2(pos.x+size.x,top+8*k),IM_COL32(5,8,23,245),IM_COL32(5,8,23,245),IM_COL32(5,8,23,0),IM_COL32(5,8,23,0));
  d->AddRectFilledMultiColor(ImVec2(pos.x,bottom-8*k),ImVec2(pos.x+size.x,pos.y+size.y),IM_COL32(5,8,23,0),IM_COL32(5,8,23,0),IM_COL32(5,8,23,245),IM_COL32(5,8,23,245));
  char header[96];snprintf(header,sizeof(header),L?"HISTORIA DRAG\303\223N  /  CAP\303\215TULO %02d":"DRAGON HISTORY  /  CHAPTER %02d",ch.sequence);
  story_text(d,header,x,pos.y+35*k,17,IM_COL32(255,215,115,255),width,k);
  story_text(d,ch.title[L],x,pos.y+66*k,29,IM_COL32(255,249,229,255),width,k);
 }else{
  // Native 4:3 anime art sits over an enlarged dim fill on wide monitors; no distorted characters or empty bars.
  float fw=SDL_min(size.x,size.y*4.f/3.f),fh=fw*.75f;ImVec2 front(pos.x+(size.x-fw)*.5f,pos.y+(size.y-fh)*.5f);
  if(sStpHd){
   float drift=.004f*sinf(p*6.2831853f),inset=.009f;
   float crop=size.x/size.y>4.f/3.f?(1-(size.y/(size.x*.75f)))*.5f:0;
   stp_image(d,t,pos,size,ImVec2(inset,crop),ImVec2(1-inset,1-crop),75);
   stp_image(d,t,front,ImVec2(fw,fh),ImVec2(inset+drift,inset),ImVec2(1-inset+drift,1-inset));
  }else{
  float frame=p*7.f;int a=(int)frame,b=SDL_min(7,a+1);float blend=frame-a;
  for(int layer=0;layer<2;layer++)for(int pass=0;pass<2;pass++){
   int cell=pass?b:a;float u=(cell%4)*.25f+.5f/3840.f,v=(cell/4)*.5f+.5f/1440.f;
   float u1=(cell%4+1)*.25f-.5f/3840.f,v1=(cell/4+1)*.5f-.5f/1440.f;
   int alpha=pass?(int)(blend*255):255;
   if(t){
    float crop=size.x/size.y>4.f/3.f?(1-(size.y/(size.x*.75f)))*.5f:0;
    if(layer==0)stp_image(d,t,pos,size,ImVec2(u,v+(v1-v)*crop),ImVec2(u1,v1-(v1-v)*crop),pass?(int)(blend*75):75);
    else stp_image(d,t,front,ImVec2(fw,fh),ImVec2(u,v),ImVec2(u1,v1),alpha);
   }
  }
  }
  if(!t){char cover[32];snprintf(cover,sizeof(cover),"saga-%d",ch.saga);story_image(d,story_art(cover),front,ImVec2(front.x+fw,front.y+fh));}
  // Keep the supplied artwork untouched. Episode lettering is rendered at the actual screen resolution.
  float width=SDL_min(fw*.88f,1020*k),fs=50*k,x=front.x+(fw-width)*.5f,y=front.y+fh*.70f;
  ImFont*title=sModTitleFont?sModTitleFont:f;ImVec2 extent=title->CalcTextSizeA(fs,100000,width,ch.title[L]);
  if(extent.y>120*k){fs*=.80f;extent=title->CalcTextSizeA(fs,100000,width,ch.title[L]);}
  char number[64];snprintf(number,sizeof(number),L?"CAP\303\215TULO %02d":"CHAPTER %02d",ch.sequence);
  ImVec2 numberSize=f->CalcTextSizeA(18*k,100000,0,number);
  stp_outline(d,f,18*k,ImVec2(front.x+(fw-numberSize.x)*.5f,y-34*k),IM_COL32(255,229,146,255),number,width,1.5f*k);
  stp_outline(d,title,fs,ImVec2(front.x+(fw-extent.x)*.5f,y),IM_COL32(255,252,233,255),ch.title[L],width,2.5f*k);
 }
 ModPadHint hints[]={{MP_CROSS,L?"Continuar":"Continue"},{MP_START,L?"Omitir pr\303\263logo":"Skip prologue"}};
 story_hints(d,ImVec2(pos.x+size.x*.08f,pos.y+size.y-46*k),size.x*.84f,k,hints,2,15);
 float seconds=(phase==1?STP_RecapMs(chapter):STP_TitleMs(ch.saga))/1000.f,fade=SDL_min(1.f,SDL_min(p*seconds/.5f,(1-p)*seconds/.45f));
 if(fade<1)d->AddRectFilled(pos,ImVec2(pos.x+size.x,pos.y+size.y),IM_COL32(0,0,0,(int)((1-fade)*255)));
 d->PopClipRect();
}
