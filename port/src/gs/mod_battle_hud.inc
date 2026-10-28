// Port of our ring/hexagon/compact battle gauges. Native-resolution vector drawing;
// portraits are exact original costume pixels. No gameplay writes on this thread.
#include "fight_panels.h"
#include <math.h>
#include <chrono>
struct MhTiming {
 bool on;std::chrono::steady_clock::time_point start;
 MhTiming(){static bool enabled=getenv("BT3_HUD_PROFILE")!=nullptr;on=enabled;if(on)start=std::chrono::steady_clock::now();}
 ~MhTiming(){if(!on)return;static unsigned count;static long long total,peak;
  auto ns=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count();
  total+=ns;if(ns>peak)peak=ns;if(++count==240){fprintf(stderr,"bt3: hud-profile draw-build avg_us=%lld peak_us=%lld samples=%u (CPU only)\n",total/count/1000,peak/1000,count);count=0;total=peak=0;}}
};
struct MhConfig {float background;bool portraits,owner,countdown,timer,es;int controls;};
static MhConfig sMhConfig;
struct MhPortrait {int key,stamp;SDL_GPUTexture *tex;bool stock;};
static MhPortrait sMhPortrait[48];static int sMhStamp;
static SDL_GPUTexture *mh_portrait(int cid,int costume,bool *stock=nullptr){
 int key=cid*256+(costume&255)+1,slot=0;char path[700];uint32_t shape[2];FILE *f;uint8_t *rgba;
 for(int i=0;i<48;i++){if(sMhPortrait[i].key==key){sMhPortrait[i].stamp=sMhStamp;if(stock)*stock=sMhPortrait[i].stock;return sMhPortrait[i].tex;}if(sMhPortrait[i].stamp<sMhPortrait[slot].stamp)slot=i;}
 if(sMhPortrait[slot].tex)Ui_TextureFree(sMhPortrait[slot].tex);
 sMhPortrait[slot]={key,sMhStamp,NULL,false};
 for(int k=0;k<3;k++){
  if(k==0&&cid==59&&costume!=0)continue;
  if(k==0)snprintf(path,sizeof(path),"%s/beta8/custom-small-portraits/%03d.rgba",Port_FileRoot(),cid);
  else snprintf(path,sizeof(path),"%s/beta8/small-portraits/%03d-%d.rgba",Port_FileRoot(),cid,k==2?0:costume);
  f=fopen(path,"rb");if(!f)continue;
  if(fread(shape,4,2,f)!=2||shape[0]!=64||shape[1]!=64){fclose(f);break;}
  rgba=(uint8_t*)SDL_malloc(64*64*4);if(!rgba){fclose(f);break;}
  if(fread(rgba,1,64*64*4,f)!=64*64*4||fgetc(f)!=EOF){SDL_free(rgba);fclose(f);break;}
  fclose(f);sMhPortrait[slot].tex=Ui_Texture(rgba,64,64);sMhPortrait[slot].stock=k>=1;break;
 }
 if(stock)*stock=sMhPortrait[slot].stock;return sMhPortrait[slot].tex;
}
static float mh_clamp(float x,float a=0,float b=1){return x<a?a:x>b?b:x;}
static ImVec2 mh_mix(ImVec2 a,ImVec2 b,float t){return ImVec2(a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t);}
static ImU32 mh_health(int layers,int alpha=242){int n=layers+1;if(n<2)return IM_COL32(245,41,31,alpha);switch((n-2)%4){case 0:return IM_COL32(255,148,41,alpha);case 1:return IM_COL32(245,214,46,alpha);case 2:return IM_COL32(51,232,97,alpha);default:return IM_COL32(46,199,240,alpha);}}

static void mh_arc(ImDrawList *d,ImVec2 c,float r,float a,float b,ImU32 col,float thick){if(b<=a)return;int segments=(int)ceilf((b-a)*sqrtf(fmaxf(r,1.f))*.65f);if(segments<6)segments=6;if(segments>36)segments=36;d->PathArcTo(c,r,a,b,segments);d->PathStroke(col,0,thick);}
static void mh_line_track(ImDrawList *d,const ImVec2 *p,int n,float a,float b,ImU32 col,float width){
 for(int i=0;i<n-1;i++){float lo=(float)i/(n-1),hi=(float)(i+1)/(n-1);float x=mh_clamp((a-lo)/(hi-lo)),y=mh_clamp((b-lo)/(hi-lo));if(y>x)d->AddLine(mh_mix(p[i],p[i+1],x),mh_mix(p[i],p[i+1],y),col,width);}
}
static void mh_text(ImDrawList *d,ImVec2 p,float size,ImU32 col,const char *text,bool centre=false,float maxW=0){
 if(mod_legacy_text(d,text,p,size,col,maxW,centre))return;
 ImFont *font=Ui_FontBold();if(!font)font=ImGui::GetFont();ImVec2 t=font->CalcTextSizeA(size,10000,0,text);
 if(maxW>0&&t.x>maxW){size*=maxW/t.x;t=font->CalcTextSizeA(size,10000,0,text);}if(centre)p.x-=t.x*.5f;
 d->AddText(font,size,ImVec2(p.x+1,p.y+1),IM_COL32(0,5,15,220),text);d->AddText(font,size,p,col,text);
}
// Small inward brackets, kept clear of the portrait, health and Ki tracks.
static void mh_target(ImDrawList *d,ImVec2 c,float r){
 float k=r/17, gap=6*k, arm=3*k, half=3.5f*k;
 for(int side=-1;side<=1;side+=2){
  float tip=c.x+side*(r+gap),outer=tip+side*arm;
  ImVec2 a(outer,c.y-half),b(tip,c.y),e(outer,c.y+half);
  d->AddLine(a,b,IM_COL32(5,16,28,160),2.5f*k);
  d->AddLine(b,e,IM_COL32(5,16,28,160),2.5f*k);
  d->AddLine(a,b,IM_COL32(255,219,120,210),1.1f*k);
  d->AddLine(b,e,IM_COL32(255,219,120,210),1.1f*k);
 }
}
static void mh_gauge(ImDrawList *d,const FpActor &a,ImVec2 c,float r,bool hex,bool layered,float t,bool effects,bool label){
 float hp=layered?(a.hp>0?(float)((a.hp-1)%10000+1)/10000:0):a.hpMax>0?(float)a.hp/a.hpMax:0;
 int layers=a.hp>0?(a.hp-1)/10000:0;float ki=a.kiMax>0?(float)a.ki/a.kiMax:0;
 float k=r/17;const float pi=3.14159265f;
 d->AddCircleFilled(ImVec2(c.x+1*k,c.y+2*k),r+3*k,IM_COL32(0,4,14,(int)(105*sMhConfig.background)),48);
 d->AddCircleFilled(c,r-2*k,IM_COL32(18,31,46,(int)(115*sMhConfig.background)),48);
 bool stockFace=false;
 if(SDL_GPUTexture *tex=sMhConfig.portraits?mh_portrait(a.chara,a.costume,&stockFace):nullptr)d->AddImageRounded(ImTextureRef((ImTextureID)(intptr_t)tex),ImVec2(c.x-r+5*k,c.y-r+5*k),ImVec2(c.x+r-5*k,c.y+r-5*k),stockFace?ImVec2(8.f/64,3.f/64):ImVec2(0,0),stockFace?ImVec2(45.f/64,40.f/64):ImVec2(1,1),IM_COL32_WHITE,(r-5*k)*.75f);
 if(!hex){
  // Original previous-port shader: layered = opposing health/Ki semicircles;
  // non-layered = two 270-degree tracks with the characteristic bottom gap.
  float start=layered?pi:pi*.75f,span=layered?pi:pi*1.5f;
  ImU32 hpColor=layered?mh_health(layers):(hp<.3f?IM_COL32(245,41,31,242):IM_COL32(51,232,97,242));
  mh_arc(d,c,r,start,start+span,layered&&layers?mh_health(layers-1,115):IM_COL32(145,171,189,115),2.6f*k);
  mh_arc(d,c,r,start,start+span*mh_clamp(hp),hpColor,2.6f*k);
  int segments=layered?5:1;float kr=layered?r:r-3.8f*k;
  for(int i=0;i<segments;i++){float lo=(float)i/segments,hi=(float)(i+1)/segments,part=mh_clamp(ki*segments-i);
   float ka=layered?pi*(1-hi):start+lo*span,kb=layered?pi*(1-lo):start+hi*span;
   float gap=layered?.025f:0;mh_arc(d,c,kr,ka+gap,kb-gap,IM_COL32(145,171,189,115),2.6f*k);
   if(layered)mh_arc(d,c,kr,kb-gap-(kb-ka-2*gap)*part,kb-gap,IM_COL32(41,168,255,242),2.6f*k);
   else mh_arc(d,c,kr,ka,ka+(kb-ka)*part,IM_COL32(41,168,255,242),2.6f*k);
  }
 }else{
  ImVec2 up[4]={ImVec2(c.x-r,c.y),ImVec2(c.x-r*.5f,c.y-r*.866f),ImVec2(c.x+r*.5f,c.y-r*.866f),ImVec2(c.x+r,c.y)};
  ImVec2 down[4]={up[0],ImVec2(c.x-r*.5f,c.y+r*.866f),ImVec2(c.x+r*.5f,c.y+r*.866f),up[3]};
  mh_line_track(d,up,4,0,1,IM_COL32(18,34,48,230),4*k);mh_line_track(d,down,4,0,1,IM_COL32(18,34,48,230),4*k);
  if(layered&&layers>0)mh_line_track(d,up,4,0,1,mh_health(layers-1,75),3*k);
  mh_line_track(d,up,4,0,mh_clamp(hp),mh_health(layers),2.6f*k);
  int segments=layered?5:1;for(int i=0;i<segments;i++){float lo=(float)i/segments+.008f,hi=(float)(i+1)/segments-.008f;mh_line_track(d,down,4,lo,lo+(hi-lo)*mh_clamp(ki*segments-i),IM_COL32(88,202,255,235),2.5f*k);}
 }
 // Prior HUD stock ticks, including one partially filled next stock; no new long bar.
 int stocks=a.blast/100000,limit=a.blastMax/100000;float charge=stocks<limit?(a.blast%100000)/100000.f:0;int slots=stocks+(stocks<limit);float step=4*k,left=c.x-slots*step*.5f;
 for(int i=0;i<slots;i++){ImVec2 lo(left+i*step,c.y+r+4*k),hi(lo.x+step-k,lo.y+2*k);d->AddRectFilled(lo,hi,IM_COL32(19,35,50,160));float amount=i<stocks?1:charge;if(amount>0)d->AddRectFilled(lo,ImVec2(lo.x+(hi.x-lo.x)*amount,hi.y),IM_COL32(242,199,80,242));}
 char str[80];float infoY=c.y+r+(a.fusionSeconds>0&&sMhConfig.timer?35:27)*k;
 if(layered){snprintf(str,sizeof(str),"%d",a.hp>0?layers+1:0);mh_text(d,ImVec2(c.x,c.y-r-8*k),7*k,IM_COL32(237,249,255,255),str,true);}
 if(label){if(a.player>0)snprintf(str,sizeof(str),"P%d",a.player);else snprintf(str,sizeof(str),"COM");mh_text(d,ImVec2(c.x,c.y+r+(a.fusionSeconds>0&&sMhConfig.timer?17:8)*k),6*k,IM_COL32(221,238,250,240),str,true);}
 if(a.fusionMove>=0&&sMhConfig.owner){if(a.fusionMove!=a.fusionAttack)snprintf(str,sizeof(str),"Move P%d / Attack P%d",a.fusionMove+1,a.fusionAttack+1);else snprintf(str,sizeof(str),"P%d",a.fusionAttack+1);mh_text(d,ImVec2(c.x,infoY),6*k,IM_COL32(101,218,255,255),str,true,80*k);infoY+=9*k;
 if(sMhConfig.countdown&&sMhConfig.controls!=4&&sMhConfig.controls!=5){snprintf(str,sizeof(str),"%ds",a.fusionSwap);mh_text(d,ImVec2(c.x,infoY),6*k,IM_COL32(255,213,104,255),str,true);infoY+=9*k;}}
 if(a.fusionSeconds>0&&sMhConfig.timer){
  float remaining=mh_clamp(a.fusionProgress/1000.f),outer=r+12*k;const int pieces=24;
  for(int j=0;j<pieces;j++){float lo=j/(float)pieces,hi=(j+1.f)/pieces,part=mh_clamp((remaining-lo)*pieces);ImU32 color=remaining<.2f?IM_COL32(255,102,70,245):IM_COL32(255,215,105,235);
   if(!hex){float start=-pi*.5f+lo*2*pi,end=-pi*.5f+hi*2*pi;mh_arc(d,c,outer,start+.025f,end-.025f,IM_COL32(120,140,160,70),2*k);if(part>0)mh_arc(d,c,outer,start+.025f,start+.025f+(end-start-.05f)*part,color,2*k);}
   else {ImVec2 track[7];for(int v=0;v<7;v++){float angle=pi/3*v;track[v]=ImVec2(c.x+cosf(angle)*outer,c.y+sinf(angle)*outer);}mh_line_track(d,track,7,lo+.003f,hi-.003f,IM_COL32(120,140,160,70),2*k);if(part>0)mh_line_track(d,track,7,lo+.003f,lo+.003f+(hi-lo-.006f)*part,color,2*k);}
  }
 }
 if(a.fusionAccept){
  ImVec2 button(c.x,c.y-r-21*k);d->AddCircleFilled(button,8*k,IM_COL32(12,22,38,220),20);d->AddCircle(button,8*k,IM_COL32(255,218,120,245),20,1.2f*k);
  mh_text(d,ImVec2(button.x,button.y-3*k),6*k,IM_COL32(255,240,195,255),"R3",true);
  mh_text(d,ImVec2(button.x,button.y-18*k),7*k,IM_COL32(255,224,145,245),sMhConfig.es?"Aceptar fusión":"Accept fusion",true,110*k);
 }
 if(a.assistKind&&(a.assistState||a.assistHelpers)){
  bool es=sMhConfig.es;char msg[96];const char*kind=a.assistKind==1?(es?"Rayo":"Beam"):a.assistKind==2?(es?"Acometida":"Rush"):(es?"Definitivo":"Ultimate");
  if(a.assistState==1)snprintf(msg,sizeof(msg),es?"R3: ayudar (%s)":"R3: assist (%s)",kind);
  else if(a.assistState)snprintf(msg,sizeof(msg),es?"%s: %s":"%s: %s",kind,a.assistState==2?(es?"uniéndose":"joining"):(es?"asistiendo":"assisting"));
  else snprintf(msg,sizeof(msg),es?"%s +%d aliados":"%s +%d helpers",kind,a.assistHelpers);
  mh_text(d,ImVec2(c.x,infoY),9*k,IM_COL32(255,221,135,245),msg,true,145*k);
 }
 if(a.reviveProgress>0||a.reviveInsufficient||a.reviveRecovering){
  bool es=sMhConfig.es;
  const char *hint=a.reviveRecovering?(es?"Recuperándose":"Recovering"):a.reviveInsufficient?(es?"Faltan reservas":"Need stocks"):(es?"Reanimando":"Reviving");
  mh_text(d,ImVec2(c.x,c.y-r-28*k),7*k,IM_COL32(124,255,192,255),hint,true,100*k);
  if(!a.reviveRecovering){ImVec2 lo(c.x-22*k,c.y-r-19*k);d->AddRectFilled(lo,ImVec2(lo.x+44*k,lo.y+2*k),IM_COL32(12,24,35,190));d->AddRectFilled(lo,ImVec2(lo.x+44*k*mh_clamp(a.reviveProgress/1000.f),lo.y+2*k),IM_COL32(124,255,192,240));}
 }
 if(effects&&a.poweredUp){
  static SDL_GPUTexture *bolts;static bool tried;if(!tried){tried=true;bolts=mod_parity_art("sparking-lightning-v2");}
  float pulse=.5f+.5f*sinf(t*8);mh_arc(d,c,r+2*k,-pi*.5f,pi*1.5f,IM_COL32(255,216,80,(int)(45+55*pulse)),4*k);
  if(bolts)for(int j=0;j<2;j++){int frame=((int)(t*12)+a.chara+j*2)&3;float u=(frame&1)*.5f,v=(frame>>1)*.5f,angle=j?-.38f:2.76f,sz=r*1.04f;ImVec2 pos(c.x+cosf(angle)*r*1.1f-sz*.5f,c.y+sinf(angle)*r*1.1f-sz*.5f);d->AddImage(ImTextureRef((ImTextureID)(intptr_t)bolts),pos,ImVec2(pos.x+sz,pos.y+sz),ImVec2(j?u+.5f:u,v),ImVec2(j?u:u+.5f,v+.5f),IM_COL32(255,255,255,240));}

 }
}
// The defuse cinematic's crack: lightning down the front of the body, three glow layers as in tools/defuse-previewer.
static bool mh_defuse_any(const FpState &st){for(int i=0;i<FP_CRACKS;i++)if(st.defuse[i].active)return true;return false;}
static void mh_defuse_lights(ImDrawList *d,const FpState &st,float sx,float sy){
 for(int q=0;q<st.viewCount;q++){
  const FpView &v=st.view[q];ImVec2 lo(gUiPresentX+v.x*sx,gUiPresentY+v.y*sy),hi(lo.x+v.w*sx,lo.y+v.h*sy);d->PushClipRect(lo,hi,true);
  for(int i=0;i<FP_CRACKS;i++){
   const FpDefuseLight &l=st.defuse[i];if(!l.active||q>=FP_VIEWS)continue;
   static const struct {float width,alpha;int r,g,b;} layer[3]={{4.f,.18f,255,219,115},{1.8f,.5f,255,242,191},{.6f,1.f,255,255,255}};
   for(int k=0;k<3;k++)for(int j=0;j<l.count[q];j++){const FpCrackSeg &s=l.seg[q][j];
    ImVec2 a(gUiPresentX+s.x0/16.f*sx,gUiPresentY+s.y0/16.f*sy),b(gUiPresentX+s.x1/16.f*sx,gUiPresentY+s.y1/16.f*sy);
    d->AddLine(a,b,IM_COL32(layer[k].r,layer[k].g,layer[k].b,(int)(s.alpha*layer[k].alpha)),fmaxf(1.f,s.width/16.f*sy*layer[k].width));}
  }
  d->PopClipRect();
 }
}
static bool mod_battle_hud_visible(){static FpState st;if(!PortPanels_Read(&st)||!st.battle)return false;if(mh_defuse_any(st))return true;if(!st.gauges)return false;if(Port_Setting("mod_hud_style",1))return true;for(int q=0;q<st.viewCount;q++)if(st.view[q].takeover)return true;for(int i=0;i<st.actorCount;i++)if(st.reviveRing[i].active)return true;for(int i=0;i<st.actorCount;i++)if(st.actor[i].present&&(st.actor[i].fusionAccept||st.actor[i].fusionSeconds>0||st.actor[i].assistState||st.actor[i].assistHelpers||st.actor[i].reviveProgress||st.actor[i].reviveInsufficient||st.actor[i].reviveRecovering))return true;return false;}
static void mod_battle_hud_draw(){
 MhTiming timing;
 static FpState st;static float phase;int mode=Port_Setting("mod_hud_style",1);if(!PortPanels_Read(&st)||!st.battle||gUiPresentW<=0||gUiPresentH<=0)return;
 if(mh_defuse_any(st))mh_defuse_lights(ImGui::GetBackgroundDrawList(),st,gUiPresentW/512.f,gUiPresentH/448.f);
 if(!st.gauges)return;
 sMhConfig={mh_clamp(Port_Setting("mod_hud_background_percent",100)/100.f),Port_Setting("show_hud_portraits",1)!=0,Port_Setting("show_fusion_control_owner",1)!=0,Port_Setting("show_fusion_control_countdown",1)!=0,Port_Setting("show_fusion_timer",1)!=0,Port_Setting("mod_language",1)==1,Port_Setting("coop_fusion_controls",0)};
 float opacity=mh_clamp(Port_Setting("mod_hud_opacity_percent",100)/100.f);if(mode!=0&&opacity<=0)return;
 float sx=gUiPresentW/512.f,sy=gUiPresentH/448.f;bool layered=Port_Setting("hud_layered_gauges",1)!=0,fx=Port_Setting("show_hud_sparking_effects",1)!=0,names=Port_Setting("mod_hud_names",1)!=0;
 if(!st.paused)phase=(float)ImGui::GetTime();float scale=mh_clamp(Port_Setting("mod_hud_scale_percent",100)/100.f,.5f,2.2f);bool hex=mode==2||mode==5,over=mode==1||mode==2||mode==4||mode==5,panels=mode>=3;
 ImDrawList *d=ImGui::GetBackgroundDrawList();++sMhStamp;
 for(int q=0;q<st.viewCount;q++){
  const FpView &v=st.view[q];ImVec2 lo(gUiPresentX+v.x*sx,gUiPresentY+v.y*sy),hi(lo.x+v.w*sx,lo.y+v.h*sy);d->PushClipRect(lo,hi,true);int vertexStart=d->VtxBuffer.Size;
  if(Port_Setting("show_revive_ring",1))for(int i=0;i<st.actorCount;i++){
   const FpReviveRing&ring=st.reviveRing[i];if(!ring.active||v.me<0||v.me>=st.actorCount||ring.team!=st.actor[v.me].team)continue;
   int opacity=Port_Setting("revive_ring_opacity_percent",45);opacity=opacity<0?0:opacity>100?100:opacity;
   for(int j=0;j<24;j++)if(ring.visible[q][j]&&ring.visible[q][j+1]){
    ImVec2 a(gUiPresentX+ring.x[q][j]*sx,gUiPresentY+ring.y[q][j]*sy),b(gUiPresentX+ring.x[q][j+1]*sx,gUiPresentY+ring.y[q][j+1]*sy);
    d->AddLine(a,b,IM_COL32(126,255,185,opacity*255/100),fmaxf(1.f,sy*1.4f));
   }
  }
  if(over)for(int i=0;i<st.actorCount;i++){
   const FpActor &a=st.actor[i];if(!a.present||!a.visible[q])continue;
   float r=fminf(23*sy*scale*(st.viewCount>2?.85f:1.f),fminf(hi.x-lo.x,hi.y-lo.y)*.12f);
   ImVec2 head(gUiPresentX+a.x[q]*sx,gUiPresentY+a.y[q]*sy);
   // Do not pin an off-screen actor to an unrelated cinematic position.
   if(head.x<lo.x||head.x>hi.x||head.y<lo.y||head.y>hi.y)continue;
   float k=r/17,extra=a.fusionSeconds>0&&sMhConfig.timer?1:0;
   float below=r+(names?(extra?31:23):(extra?23:14))*k,info=r+(extra?35:27)*k;
   if(a.fusionMove>=0&&sMhConfig.owner){below=fmaxf(below,info+6*k);info+=9*k;if(sMhConfig.countdown&&sMhConfig.controls!=4&&sMhConfig.controls!=5){below=fmaxf(below,info+6*k);info+=9*k;}}
   if(a.assistKind&&(a.assistState||a.assistHelpers))below=fmaxf(below,info+9*k);
   float above=r+(a.fusionAccept?40:a.reviveProgress||a.reviveInsufficient||a.reviveRecovering?29:extra?13:9)*k;
   // Anchor the lowest label above the head, never clamp it down onto the model.
   ImVec2 c(roundf(mh_clamp(head.x,lo.x+r*1.8f,hi.x-r*1.8f)),roundf(head.y-below-22*sy*scale));
   if(c.y-above<lo.y||c.y+below>hi.y)continue;
   mh_gauge(d,a,c,r,hex,layered,phase,fx,true);
   if(i==v.target&&a.hp>0)mh_target(d,c,r);
   if(names)mh_text(d,ImVec2(c.x,c.y+r+(a.fusionSeconds>0&&sMhConfig.timer?25:17)*(r/17)),6*(r/17),IM_COL32(232,244,255,245),a.name,true,110*(r/17));
  }
  if(panels)for(int role=0;role<2;role++){
   int who=role?v.target:v.me;if(who<0||who>=st.actorCount||!st.actor[who].present)continue;const FpActor &a=st.actor[who];
   float k=sy*scale*(st.viewCount>2?.65f:.85f),w=fminf(118*k,(hi.x-lo.x)*.46f),h=50*k,x=role?hi.x-w-5*k:lo.x+5*k,y=lo.y+6*k;
   d->AddRectFilled(ImVec2(x,y),ImVec2(x+w,y+h),IM_COL32(10,24,42,(int)(118*sMhConfig.background)),8*k);d->AddLine(ImVec2(x+6*k,y+h),ImVec2(x+w-6*k,y+h),IM_COL32(225,182,80,165),k);
   mh_gauge(d,a,ImVec2(x+24*k,y+24*k),17*k,hex,layered,phase,fx,false);
   if(names)mh_text(d,ImVec2(x+46*k,y+8*k),8*k,IM_COL32(237,246,255,255),a.name,false,w-50*k);
   char value[80];snprintf(value,sizeof(value),"%d / %d",a.hp,a.hpMax);mh_text(d,ImVec2(x+46*k,y+21*k),7*k,IM_COL32(123,241,164,255),value,false,w-50*k);
   snprintf(value,sizeof(value),"Ki %.1f",a.ki/20000.f);mh_text(d,ImVec2(x+46*k,y+32*k),7*k,IM_COL32(112,211,255,255),value);
  }
  if(mode==0&&v.me>=0&&v.me<st.actorCount){const FpActor&a=st.actor[v.me];if(a.assistKind&&(a.assistState||a.assistHelpers)){
   bool es=sMhConfig.es;char hint[96];const char*kind=a.assistKind==1?(es?"rayo":"beam"):a.assistKind==2?(es?"acometida":"rush"):(es?"definitivo":"ultimate");
   if(a.assistState==1)snprintf(hint,sizeof(hint),es?"R3: ayudar (%s)":"R3: assist (%s)",kind);
   else if(a.assistState)snprintf(hint,sizeof(hint),es?"Asistencia: %s":"Assisting: %s",kind);
   else snprintf(hint,sizeof(hint),es?"%s: %d ayudantes":"%s: %d helpers",kind,a.assistHelpers);
   mh_text(d,ImVec2(lo.x+8*sy,hi.y-42*sy),10*sy,IM_COL32(255,221,135,255),hint,false,hi.x-lo.x-16*sy);
  }}
  if(mode==0&&v.me>=0&&v.me<st.actorCount){const FpActor &a=st.actor[v.me];
   if(a.reviveProgress>0||a.reviveInsufficient||a.reviveRecovering){bool es=sMhConfig.es;char hint[96];
    if(a.reviveRecovering)snprintf(hint,sizeof(hint),"%s",es?"Recuperándose":"Recovering");
    else if(a.reviveInsufficient)snprintf(hint,sizeof(hint),"%s",es?"Reanimación: faltan reservas":"Revival: need stocks");
    else snprintf(hint,sizeof(hint),es?"Reanimando %d%%":"Reviving %d%%",a.reviveProgress/10);
    mh_text(d,ImVec2(lo.x+8*sy,hi.y-26*sy),10*sy,IM_COL32(124,255,192,255),hint,false,hi.x-lo.x-16*sy);
   }
  }
  if(v.takeover){bool es=sMhConfig.es;
   const char*hint=v.takeover==2?(es?"Control del aliado activado":"Teammate control activated"):(es?"L2 + Cuadrado: controlar aliado":"L2 + Square: take over teammate");
   mh_text(d,ImVec2(lo.x+(hi.x-lo.x)*.5f,hi.y-22*sy),10*sy,IM_COL32(255,221,135,255),hint,true,hi.x-lo.x-16*sy);
  }
  if(mode==0)for(int i=0;i<st.actorCount;i++){const FpActor&a=st.actor[i];if(a.present&&a.fusionAccept&&a.visible[q]){
   float x=gUiPresentX+a.x[q]*sx,y=gUiPresentY+a.y[q]*sy;ImVec2 c(x,y-24*sy);d->AddCircleFilled(c,9*sy,IM_COL32(12,22,38,220),20);d->AddCircle(c,9*sy,IM_COL32(255,218,120,245),20,sy);mh_text(d,ImVec2(c.x,c.y-4*sy),8*sy,IM_COL32(255,240,195,255),"R3",true);
  }}
  // Normal-HUD players still see a readable fusion countdown, independent of replacement gauges.
  if(mode==0&&sMhConfig.timer)for(int role=0;role<2;role++){
   int who=role?v.target:v.me;if(who<0||who>=st.actorCount||!st.actor[who].present||st.actor[who].fusionSeconds<=0)continue;
   char text[48];int secs=st.actor[who].fusionSeconds;snprintf(text,sizeof(text),"Fusion %d:%02d",secs/60,secs%60);
   mh_text(d,ImVec2(role?hi.x-75*sy:lo.x+6*sy,lo.y+56*sy),9*sy,IM_COL32(255,215,104,255),text);
  }
  // Apply opacity to this viewport's vertices only, including portraits/text.
  if(mode!=0&&opacity<1){
   for(int j=vertexStart;j<d->VtxBuffer.Size;j++){ImU32 &col=d->VtxBuffer[j].col;int alpha=(col>>IM_COL32_A_SHIFT)&255;col=(col&~IM_COL32_A_MASK)|((ImU32)(alpha*opacity)<<IM_COL32_A_SHIFT);}}
  d->PopClipRect();
 }
}
