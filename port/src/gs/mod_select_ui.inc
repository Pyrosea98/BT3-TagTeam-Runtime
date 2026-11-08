// Modern draft overlay. Real models are drawn by native ChrView underneath it.
#include "../../include/mod_select.h"
#include "../../include/b8_selection_rules.h"
extern "C" int Port_Beta8CostumeIdentity(int,int);
extern "C" const char *Port_CharaPower(int,int);
extern "C" int Port_ModernStageActive(void);
static std::atomic<unsigned> sMsSequence{0};
static std::atomic<int> sMsWords[sizeof(MsView)/sizeof(int)];
static std::atomic<int> sMsMouse{0};
extern "C" void Port_ModSelectPublish(const MsView *view){
 sMsSequence.fetch_add(1,std::memory_order_acq_rel);
 int words[sizeof(MsView)/sizeof(int)];memcpy(words,view,sizeof(*view));for(unsigned i=0;i<sizeof(MsView)/sizeof(int);i++)sMsWords[i].store(words[i],std::memory_order_relaxed);
 sMsSequence.fetch_add(1,std::memory_order_release);
}
extern "C" int Port_ModSelectActive(void){return sMsWords[0].load(std::memory_order_acquire);}
extern "C" int Port_ModSelectTakeMouse(void){return sMsMouse.exchange(0,std::memory_order_acq_rel);}
static bool ms_snapshot(MsView &v){
 for(int attempt=0;attempt<3;attempt++){
  unsigned seq=sMsSequence.load(std::memory_order_acquire);if(seq&1)continue;
  int words[sizeof(MsView)/sizeof(int)];for(unsigned i=0;i<sizeof(MsView)/sizeof(int);i++)words[i]=sMsWords[i].load(std::memory_order_relaxed);
  if(seq==sMsSequence.load(std::memory_order_acquire)){memcpy(&v,words,sizeof(v));return true;}
 }return false;
}
// Render-thread caches use original selection chips, never cropped overhead faces.
struct MsArt {int key,stamp;SDL_GPUTexture *tex;};
static MsArt sMsArt[128];static int sMsArtStamp;
static SDL_GPUTexture *ms_art(int cid,bool stage=false){
 int key=cid+1+(stage?1000:0),slot=0;char path[700];uint32_t wh[2];
 for(int i=0;i<128;i++){if(sMsArt[i].key==key){sMsArt[i].stamp=++sMsArtStamp;return sMsArt[i].tex;}if(sMsArt[i].stamp<sMsArt[slot].stamp)slot=i;}
 if(sMsArt[slot].tex)Ui_TextureFree(sMsArt[slot].tex);sMsArt[slot]={key,++sMsArtStamp,nullptr};
 snprintf(path,sizeof(path),"%s/beta8/mod-select/%s/%03d.rgba",Port_FileRoot(),stage?"stages":"chips",cid);FILE*f=fopen(path,"rb");if(!f)return nullptr;
 if(fread(wh,4,2,f)!=2||!wh[0]||!wh[1]||wh[0]>1024||wh[1]>1024){fclose(f);return nullptr;}
 size_t bytes=(size_t)wh[0]*wh[1]*4;auto *rgba=(uint8_t*)SDL_malloc(bytes);if(!rgba){fclose(f);return nullptr;}
 if(fread(rgba,1,bytes,f)!=bytes||fgetc(f)!=EOF){SDL_free(rgba);fclose(f);return nullptr;}fclose(f);
 sMsArt[slot].tex=Ui_Texture(rgba,wh[0],wh[1]);return sMsArt[slot].tex;
}
static std::atomic<unsigned> sMsStageSequence{0};static std::atomic<int> sMsStageWords[sizeof(MsStageView)/sizeof(int)];
extern "C" void Port_ModernStagePublish(const MsStageView *v){sMsStageSequence.fetch_add(1,std::memory_order_acq_rel);int w[sizeof(*v)/4];memcpy(w,v,sizeof(*v));for(unsigned i=0;i<sizeof(*v)/4;i++)sMsStageWords[i].store(w[i],std::memory_order_relaxed);sMsStageSequence.fetch_add(1,std::memory_order_release);}
extern "C" int Port_ModernStageActive(void){return sMsStageWords[0].load(std::memory_order_acquire);}
static bool ms_stage_snapshot(MsStageView &v){for(int attempt=0;attempt<3;attempt++){unsigned seq=sMsStageSequence.load(std::memory_order_acquire);if(seq&1)continue;int w[sizeof(v)/4];for(unsigned i=0;i<sizeof(v)/4;i++)w[i]=sMsStageWords[i].load(std::memory_order_relaxed);if(seq==sMsStageSequence.load(std::memory_order_acquire)){memcpy(&v,w,sizeof(v));return true;}}return false;}
extern "C" int PortSongs_Offset(int);
static const char* const sMsStageNames[]={
    "Wasteland - Noon", "Rocky Area - Noon", "Planet Namek", "Dying Namek",
    "World Tournament Stage - Noon", "Kami's Lookout", "Cell Games Arena - Evening", "Supreme Kai's World",
    "Hyperbolic Time Chamber", "City Ruins - Noon", "Mountain Road - Noon", "Islands",
    "Kame House", "Planet - Night", "Glacier", "Ruined Earth",
    "Outer Space", "Penguin Village", "Hell", "Desert - Noon",
    "King's Castle", "Muscle Tower", "Mount Paozu", "Wasteland - Evening",
    "Wasteland - Night", "Rocky Area - Evening", "Rocky Area - Night", "World Tournament Stage - Evening",
    "Cell Game - Noon", "City Ruins - Evening", "City Ruins - Night", "Desert - Evening",
    "Desert - Night", "Mountain Road - Evening", "Planet - Evening", "Random",
};
static const char* const sMsStageNamesEs[]={
 "Páramo - Mediodía", "Área rocosa - Mediodía", "Planeta Namek", "Planeta Namek agonizante",
 "Torneo de Artes Marciales", "Templo de Kamisama", "Juegos de Cell - Tarde", "Planeta Supremo",
 "Habitación del tiempo", "Ciudad en ruinas - Mediodía", "Camino de montaña - Mediodía", "Islas",
 "Kame House", "Planeta - Noche", "Glaciar", "Tierra en ruinas", "Espacio exterior", "Aldea Pingüino",
 "Infierno", "Desierto - Mediodía", "Castillo del Rey", "Torre Muscle", "Montaña Paoz", "Yermos - Tarde",
 "Yermos - Noche", "Área rocosa - Tarde", "Área rocosa - Noche", "Torneo de Artes Marciales - Tarde",
 "Juegos de Cell - Mediodía", "Ciudad en ruinas GT", "Ciudad en ruinas - Noche", "Desierto - Tarde",
 "Desierto - Noche", "Torneo Multiverse", "Planeta - Tarde", "Aleatorio"
};
static const char* const sMsSongNames[]={
    "The Meteor", "Vital Burner", "Innocent World", "After The Fire", "Sweet Vibration", "Survive", "Heat Capacity", "Overture",
    "Shine", "Power Scale", "Edge of Spirit", "Caution !", "Menace", "Hot Soul", "High and Scream", "Shootout In Meteor",
    "Dynamite Battle", "Burnin' Up", "Wild Rush", "Evolution", "********", "********", "********", "********",
};
static const char *ms_song_name(int id,char *buf,size_t n,bool es){for(int i=0;i<PortSongs_Count();i++)if(PortSongs_Offset(i)==id)return PortSongs_Name(i);if(id>=0&&id<20)return sMsSongNames[id];if(id==24)return es?"Aleatorio":"Random";if(id==25)return es?"Bloqueado":"Locked";snprintf(buf,n,es?"Pista original %02d":"Original track %02d",id+1);return buf;}
static const char *ms_stage_name(int id,char*buf,size_t n,bool es){
 // Beta 8 replaces these stages; stock English labels describe different scenery.
 if(id==39)return "Infierno AF";
 if(id==40)return es?"Ciudad del Oeste":"West City";
 if(id==41)return es?"Espacio exterior (Tierra)":"Outer Space (Earth)";
 if(id==29)return es?"Ciudad en ruinas GT":"City Ruins GT";
 if(id==33&&!es)return "Multiverse Tournament";
 if(id>=0&&id<36)return es?sMsStageNamesEs[id]:sMsStageNames[id];
 if(id>=42&&id<42+PortStages_Count())return PortStages_Name(id-42);
 snprintf(buf,n,es?"Escenario extra %02d":"Extra stage %02d",id);return buf;
}
// Resolution-independent PlayStation geometry; never relies on font coverage.
static void ms_button(ImDrawList*d,int button,float x,float y,float k){mod_pad_glyph(d,button==0?MP_CROSS:button==1?MP_TRIANGLE:button==2?MP_SQUARE:MP_DPAD,ImVec2(x,y),k);}
static void ms_random_icon(ImDrawList*d,ImVec2 c,float r){
 if(auto*t=mod_reward_art(1)){d->AddImage(ImTextureRef((ImTextureID)(intptr_t)t),ImVec2(c.x-r,c.y-r),ImVec2(c.x+r,c.y+r));return;}
 const ImU32 gold=IM_COL32(255,207,74,255),dark=IM_COL32(80,38,55,220),white=IM_COL32(255,245,222,255);
 d->AddCircleFilled(c,r,dark,40);d->AddCircle(c,r,gold,40,r*.055f);
 float l=c.x-r*.55f,rr=c.x+r*.55f,t=c.y-r*.3f,b=c.y+r*.3f;
 d->AddLine(ImVec2(l,t),ImVec2(l+r*.3f,t),white,r*.09f);d->AddLine(ImVec2(l+r*.3f,t),ImVec2(rr-r*.25f,b),white,r*.09f);d->AddLine(ImVec2(rr-r*.25f,b),ImVec2(rr,b),white,r*.09f);
 d->AddLine(ImVec2(l,b),ImVec2(l+r*.3f,b),gold,r*.09f);d->AddLine(ImVec2(l+r*.3f,b),ImVec2(rr-r*.25f,t),gold,r*.09f);d->AddLine(ImVec2(rr-r*.25f,t),ImVec2(rr,t),gold,r*.09f);
 d->AddTriangleFilled(ImVec2(rr+r*.08f,t),ImVec2(rr-r*.18f,t-r*.16f),ImVec2(rr-r*.18f,t+r*.16f),gold);
 d->AddTriangleFilled(ImVec2(rr+r*.08f,b),ImVec2(rr-r*.18f,b-r*.16f),ImVec2(rr-r*.18f,b+r*.16f),white);
}
static void mod_stage_draw(){
 MsStageView v;if(!ms_stage_snapshot(v)||!v.active)return;bool es=Port_Setting("mod_language",1)==1;
 if(getenv("BT3_STAGE_CLEAN")){int id=v.cursor>=0&&v.cursor<v.count?v.stages[v.cursor]:255;ImGui::GetForegroundDrawList()->AddRectFilled(ImVec2(0,0),ImVec2(8,8),IM_COL32(id,v.preview?255:0,255,255));return;}
 ImVec2 sz=ImGui::GetIO().DisplaySize;float k=sz.y/720.f,edge=24*k;auto*d=ImGui::GetForegroundDrawList();auto*font=Ui_FontBold();if(!font)font=ImGui::GetFont();
 const ImU32 gold=IM_COL32(255,209,83,255),white=IM_COL32(249,241,230,255),pink=IM_COL32(234,119,177,255);
 auto text=[&](const char*s,float x,float y,float fs,ImU32 c,float fit){fs*=k;float w=font->CalcTextSizeA(fs,10000,0,s).x;if(w>fit)fs*=fit/w;d->AddText(font,fs,ImVec2(x,y),c,s);};
 if(v.preview)d->AddRectFilledMultiColor(ImVec2(0,0),sz,IM_COL32(22,10,31,220),IM_COL32(22,10,31,115),IM_COL32(22,10,31,150),IM_COL32(22,10,31,230));
 else d->AddRectFilled(ImVec2(0,0),sz,IM_COL32(29,15,37,252));
 d->AddRectFilled(ImVec2(0,sz.y-66*k),sz,IM_COL32(22,10,31,205));
 text(v.music?(es?"ELIGE LA MÚSICA":"CHOOSE YOUR MUSIC"):(es?"ELIGE EL ESCENARIO":"CHOOSE YOUR STAGE"),edge,25*k,27,gold,sz.x-edge*2);
 text(v.music?(es?"La música seleccionada también sonará durante el combate.":"The selected music also plays during the match."):(es?"Elige el lugar del combate.":"Choose your battlefield."),edge,69*k,15,white,sz.x-edge*2);
 int count=v.music?v.bgmCount:v.count,cursor=v.music?v.bgmCursor:v.cursor,cols=v.music?4:6,pageSize=v.music?16:18,first=cursor/pageSize*pageSize;
 float gridW=sz.x*.66f-edge*2,gap=10*k,cw=(gridW-(cols-1)*gap)/cols,ch=v.music?100*k:145*k,top=118*k;
 char line[180];
 for(int j=0;j<pageSize&&first+j<count;j++){
  int idx=first+j,id=v.music?v.songs[idx]:v.stages[idx];float x=edge+(j%cols)*(cw+gap),y=top+(j/cols)*(ch+gap);bool selected=idx==cursor;
  d->AddRectFilled(ImVec2(x,y),ImVec2(x+cw,y+ch),selected?IM_COL32(135,73,76,225):IM_COL32(43,25,48,180),6*k);
  if(v.music){char name[160];text(ms_song_name(id,name,sizeof(name),es),x+10*k,y+15*k,16,white,cw-20*k);if(id==24)ms_random_icon(d,ImVec2(x+cw*.5f,y+ch*.65f),ch*.22f);else for(int b=0;b<9;b++){float h=(selected?18+12*sinf((float)ImGui::GetTime()*4+b):12+b%3*5)*k;d->AddRectFilled(ImVec2(x+(12+b*8)*k,y+ch-14*k-h),ImVec2(x+(16+b*8)*k,y+ch-14*k),selected?gold:pink,1*k);}}
  else {if(id==35)ms_random_icon(d,ImVec2(x+cw*.5f,y+(ch-30*k)*.5f),SDL_min(cw,ch-30*k)*.35f);else if(auto*t=ms_art(id,true)){float iw=SDL_min(cw-12*k,(ch-36*k)*16/9),ih=iw*9/16;d->AddImage(ImTextureRef((ImTextureID)(intptr_t)t),ImVec2(x+(cw-iw)*.5f,y+6*k),ImVec2(x+(cw+iw)*.5f,y+6*k+ih));}text(ms_stage_name(id,line,sizeof(line),es),x+6*k,y+ch-25*k,15,white,cw-12*k);}
  if(selected)d->AddRect(ImVec2(x,y),ImVec2(x+cw,y+ch),gold,6*k,0,2*k);
 }
 float px=sz.x*.69f,pw=sz.x-px-edge;
 if(!v.music&&cursor>=0&&cursor<v.count)text(ms_stage_name(v.stages[cursor],line,sizeof(line),es),px,170*k,27,gold,pw);
 char name[160];int song=v.bgmCursor>=0&&v.bgmCursor<v.bgmCount?v.songs[v.bgmCursor]:0;text(es?"BANDA SONORA":"SOUNDTRACK",px,490*k,16,pink,pw);text(ms_song_name(song,name,sizeof(name),es),px,520*k,20,gold,pw);
 snprintf(line,sizeof(line),es?"Página %d / %d":"Page %d / %d",cursor/pageSize+1,(count+pageSize-1)/pageSize);text(line,edge,620*k,14,white,sz.x-edge*2);
 {float y=684*k,x=edge+7*k,step=SDL_min(240*k,(sz.x-edge*2)/4);const char*labels[]={v.music?(es?"Pista":"Track"):(es?"Escenario":"Stage"),v.music?(es?"Guardar":"Save"):(es?"Combatir":"Fight"),v.music?(es?"Volver":"Back"):(es?"Equipo":"Team"),es?"Música":"Music"};int buttons[]={3,0,1,2};for(int j=0;j<(v.music?3:4);j++){ms_button(d,buttons[j],x+j*step,y,k);text(labels[j],x+j*step+15*k,y-10*k,16,white,step-26*k);}}
}
static void mod_select_result_draw(){MsView v;if(sOpen||!ms_snapshot(v)||v.active)return;
 if(v.result_team>0){char label[96];snprintf(label,sizeof(label),Port_Setting("mod_language",1)==1?"VICTORIA DEL EQUIPO %d":"TEAM %d WINS",v.result_team);ImVec2 sz=ImGui::GetIO().DisplaySize;ImFont*f=Ui_FontBold();if(!f)f=ImGui::GetFont();float fs=sz.y*.036f,w=f->CalcTextSizeA(fs,10000,0,label).x;ImGui::GetForegroundDrawList()->AddText(f,fs,ImVec2((sz.x-w)*.5f,sz.y*.12f),IM_COL32(255,209,83,255),label);}}
static void mod_select_draw(){
 MsView v;if(sOpen||!ms_snapshot(v))return;
 if(!v.active)return;
 if(v.roster_count<1||v.cursor<0||v.cursor>=v.roster_count)return;
 ++sMhStamp;
 ImVec2 sz=ImGui::GetIO().DisplaySize;float k=sz.y/720.f,edge=24*k,left=sz.x*.44f;
 bool es=Port_Setting("mod_language",1)==1;ImDrawList *dl=ImGui::GetForegroundDrawList();
 ImFont *font=Ui_FontBold();if(!font)font=ImGui::GetFont();
 auto text=[&](const char *s,float x,float y,float fs,ImU32 color,float fit){
  fs*=k;float w=font->CalcTextSizeA(fs,10000,0,s).x;if(w>fit&&w>0)fs*=fit/w;
  dl->AddText(font,fs,ImVec2(x+k,y+k),IM_COL32(0,0,0,225),s);
  dl->AddText(font,fs,ImVec2(x,y),color,s);
 };
 const ImU32 white=IM_COL32(249,241,230,255),gold=IM_COL32(255,209,83,255),pink=IM_COL32(234,119,177,255);
 dl->AddRectFilled(ImVec2(0,0),ImVec2(sz.x,82*k),IM_COL32(28,13,35,235));
 dl->AddRectFilledMultiColor(ImVec2(0,82*k),ImVec2(left,sz.y-180*k),IM_COL32(29,16,38,170),IM_COL32(29,16,38,112),IM_COL32(29,16,38,120),IM_COL32(29,16,38,185));
 dl->AddRectFilled(ImVec2(0,sz.y-180*k),sz,IM_COL32(27,13,34,200));
 dl->AddLine(ImVec2(edge,82*k),ImVec2(sz.x-edge,82*k),gold,1.3f*k);
 text(es?"ELIGE TU EQUIPO":"BUILD YOUR TEAM",edge,18*k,27,gold,left-edge);
 char line[180];snprintf(line,sizeof(line),v.mode==2?(es?"Todos contra todos  /  %d luchadores":"Free-for-All  /  %d fighters"):(es?"Tag Team  /  %d vs %d":"Tag Team  /  %d vs %d"),v.mode==2?v.total:v.a,v.total-v.a);
 if(v.mode!=2&&MS_Teams(&v)>2)snprintf(line,sizeof(line),es?"%d equipos / %d luchadores":"%d teams / %d fighters",MS_Teams(&v),v.total);
 text(line,sz.x*.48f,27*k,18,white,sz.x*.48f-edge);
 ms_kit_sprite(dl,15,ImVec2(left-74*k,84*k),ImVec2(65*k,45*k),IM_COL32(255,255,255,165));
 if(v.phase==3){
  float bx=edge,by=115*k,bw=left-edge*2;
  text(es?"CONFIRMA TU EQUIPO":"CONFIRM YOUR TEAMS",bx,by,24,gold,bw);
  text(es?"Revisa tus luchadores antes de continuar.":"Review your fighters before continuing.",bx,by+36*k,15,white,bw);
  float rowH=SDL_min(32*k,(sz.y-271*k-(by+76*k))/SDL_max(1,v.total));
  for(int i=0;i<v.total;i++){
   float y=by+76*k+i*rowH;char label[200];
   snprintf(label,sizeof(label),es?"Equipo %d: %s":"Team %d: %s",MS_Side(&v,i)+1,Port_CharaName(v.pick[i].chara));
   dl->AddRectFilled(ImVec2(bx,y),ImVec2(bx+bw,y+rowH-2*k),IM_COL32(71,35,66,130),3*k);
   if(auto*t=ms_art(Port_Beta8CostumeIdentity(v.pick[i].chara,v.pick[i].costume)))dl->AddImage(ImTextureRef((ImTextureID)(intptr_t)t),ImVec2(bx+2*k,y),ImVec2(bx+rowH,y+rowH));
   text(label,bx+rowH+5*k,y+5*k,14,white,bw-rowH-8*k);
  }
  ImVec2 a(bx,sz.y-251*k),b(left-edge,sz.y-199*k);
  bool hover=ImGui::IsMouseHoveringRect(a,b);dl->AddRectFilled(a,b,hover?IM_COL32(160,82,38,245):IM_COL32(112,57,42,230),7*k);dl->AddRect(a,b,gold,7*k,0,2*k);
  ms_kit_sprite(dl,14,ImVec2(a.x+8*k,a.y+5*k),ImVec2(42*k,42*k));
  text(es?"OK · CONTINUAR":"OK · CONTINUE",a.x+58*k,a.y+15*k,20,gold,b.x-a.x-66*k);
  if(hover&&ImGui::IsMouseClicked(0))sMsMouse.store(-1,std::memory_order_release);
 }else if(!v.phase){
  text(es?"TAMAÑO DE LOS EQUIPOS":"TEAM SIZE",edge,117*k,21,white,left-edge*2);
  if(v.mode==2){text("10 x 1",edge,173*k,40,gold,left-edge*2);}
  else{
   int teams=MS_Teams(&v),players=v.player_count>0?v.player_count:1,rows=1+teams+players;
   float step=SDL_min(31*k,(sz.y-420*k)/rows);
   for(int row=0;row<rows;row++){
    int amount=row==0?teams:row<=teams?MS_Count(&v,row-1):v.human_team[row-teams-1]+1;
    if(row==0)snprintf(line,sizeof(line),es?"Bandos":"Teams");
    else if(row<=teams)snprintf(line,sizeof(line),es?"Equipo %d":"Team %d",row);
    else snprintf(line,sizeof(line),es?"P%d · Equipo":"P%d · Team",row-teams);
    float y=151*k+row*step,h=step*.96f,px=left-edge-75*k;
    dl->AddRectFilled(ImVec2(edge,y),ImVec2(left-edge,y+step-2*k),v.cursor==row?IM_COL32(126,64,57,165):IM_COL32(59,28,53,105),4*k);
    text(line,edge+10*k,155*k+row*step,17,v.cursor==row?gold:white,left-edge*2-125*k);
    ms_kit_sprite(dl,10,ImVec2(px-30*k,y),ImVec2(h,h));
    if(!ms_kit_number(dl,amount,ImVec2(px,y),h)){snprintf(line,sizeof(line),"%d",amount);text(line,px,y,18,gold,40*k);}
    ms_kit_sprite(dl,11,ImVec2(left-edge-h-5*k,y),ImVec2(h,h));
    if(v.cursor==row)dl->AddRect(ImVec2(edge,151*k+row*step),ImVec2(left-edge,151*k+(row+1)*step),gold,3*k);
   }
   {const ModPadHint h[]={{MP_DPAD,es?"Elegir / cambiar":"Choose / change"},{MP_CROSS,es?"Personajes":"Fighters"}};mod_pad_hints(dl,ImVec2(edge,sz.y-240*k),left-edge*2,k,h,2,16);}
  }
  text(es?"Hasta 10 luchadores entre todos los bandos.":"Up to 10 fighters across all teams.",edge,sz.y-205*k,15,white,left-edge*2);

 }else{
  const int first=(v.cursor/MS_PAGE)*MS_PAGE;
  float gap=6*k,cell=SDL_min((left-edge*2-gap*(MS_COLS-1))/MS_COLS,(sz.y-318*k)/5.f-gap);
  float top=118*k,rowH=cell+gap,gridX=edge+(left-edge*2-(cell*MS_COLS+gap*(MS_COLS-1)))*.5f;
  snprintf(line,sizeof(line),es?"PAGINA %d / %d":"PAGE %d / %d",first/MS_PAGE+1,(v.roster_count+MS_PAGE-1)/MS_PAGE);
  text(line,edge,94*k,13,white,left-edge*2);
  for(int n=0;n<MS_PAGE&&first+n<v.roster_count;n++){
   int idx=first+n,cid=v.roster[idx];float x=gridX+(n%MS_COLS)*(cell+gap),y=top+(n/MS_COLS)*rowH;
   ImVec2 a(x,y),b(x+cell,y+cell);bool selected=idx==v.cursor;
   dl->AddRectFilled(a,b,selected?IM_COL32(217,141,42,120):IM_COL32(87,46,82,95),4*k);
   dl->AddRect(a,b,selected?gold:IM_COL32(197,138,82,75),4*k,0,k);
   if(cid<0)ms_random_icon(dl,ImVec2(x+cell*.5f,y+cell*.5f),cell*.36f);
   else if(SDL_GPUTexture *tex=cid>=0?ms_art(Port_Beta8CostumeIdentity(cid,selected?v.costume:0)):nullptr){
    float side=cell-6*k;float cx=(a.x+b.x)*.5f;
    dl->AddImage(ImTextureRef((ImTextureID)(intptr_t)tex),ImVec2(cx-side*.5f,y+3*k),ImVec2(cx+side*.5f,y+3*k+side));
   }else{snprintf(line,sizeof(line),cid<0?"?":"%d",cid);text(line,x+5*k,y+8*k,15,white,cell-10*k);}
   if(v.disabled[idx]){dl->AddRectFilled(a,b,IM_COL32(20,12,26,180),4*k);text(es?"EN EL EQUIPO":"IN TEAM",x+3*k,y+cell*.45f,12,pink,cell-6*k);}
   if(selected){dl->AddRect(a,b,gold,4*k,0,2*k);ms_kit_sprite(dl,13,ImVec2(x-4*k,y-9*k),ImVec2(cell*.49f,cell*.31f));}
   if(v.phase==1&&ImGui::IsMouseClicked(0)&&ImGui::IsMouseHoveringRect(a,b))sMsMouse.store(idx+1,std::memory_order_release);
  }
  int cid=v.phase==2?v.forms[v.form_cursor]:v.roster[v.cursor];const char *name=cid<0?(es?"Aleatorio":"Random"):Port_CharaName(cid);
  if(!name||!*name){snprintf(line,sizeof(line),es?"Personaje %d":"Fighter %d",cid);name=line;}
  char clean[180];snprintf(clean,sizeof(clean),"%s",name);char *cut=strchr(clean,'[');if(!cut)cut=strchr(clean,',');if(cut)*cut=0;
  text(clean,sz.x*.48f,100*k,28,gold,sz.x*.49f-edge);
  snprintf(line,sizeof(line),es?"Traje %d   /   P%d elige":"Costume %d   /   P%d choosing",v.costume+1,v.pad+1);
  text(line,sz.x*.48f,142*k,16,white,sz.x*.48f-edge);
  if(v.preset==0)snprintf(line,sizeof(line),es?"Potaras: normal":"Potaras: normal");
  else if(v.preset<=3)snprintf(line,sizeof(line),es?"Potaras: personalizado %d":"Potaras: custom set %d",v.preset);
  else snprintf(line,sizeof(line),es?"Potaras: personaje guardado %d":"Potaras: saved character %d",v.preset-3);
  text(line,sz.x*.48f,165*k,14,white,sz.x*.48f-edge);
  {
   const char *power=Port_CharaPower(cid,v.costume);
   if(power){
    snprintf(line,sizeof(line),es?"Poder: %s":"Power: %s",power);
    text(line,sz.x*.48f,189*k,19,gold,sz.x*.48f-edge);
   }
  }
  // Ownership lives next to the team roster, away from the model.
  int activeTeam=(v.slot>=0&&v.slot<v.total)?MS_Side(&v,v.slot):0;
  snprintf(line,sizeof(line),es?"Equipo %d / P%d elige":"Team %d / P%d choosing",activeTeam+1,v.pad+1);
  text(line,sz.x*.66f,sz.y-46*k,14,activeTeam?pink:gold,sz.x*.34f-edge);
  if(v.phase==2&&v.form_count>0){
   // A form reel occupies the grid column; the full model remains unobscured.
   dl->AddRectFilled(ImVec2(0,82*k),ImVec2(left,sz.y-180*k),IM_COL32(29,16,38,190));
   text(es?"ELIGE LA TRANSFORMACION":"CHOOSE YOUR FORM",edge,100*k,18,gold,left-edge*2);
   float fw=SDL_min((left-edge*2-12*k)/3,110*k),fh=fw,formX=(left-(fw*3+12*k))*.5f;
   for(int f=0;f<v.form_count;f++){
    float x=formX+(f%3)*(fw+6*k),y=142*k+(f/3)*(fh+9*k);bool selected=f==v.form_cursor;
    dl->AddRectFilled(ImVec2(x,y),ImVec2(x+fw,y+fh),selected?IM_COL32(135,73,76,235):IM_COL32(67,37,67,220),4*k);
    if(auto*t=ms_art(Port_Beta8CostumeIdentity(v.forms[f],v.costume))){float d=fw-25*k;dl->AddImage(ImTextureRef((ImTextureID)(intptr_t)t),ImVec2(x+(fw-d)*.5f,y+4*k),ImVec2(x+(fw+d)*.5f,y+4*k+d));}
    const char*fn=Port_CharaName(v.forms[f]);char formName[180];snprintf(formName,sizeof(formName),"%s",fn?fn:"");char*meta=strchr(formName,'[');if(!meta)meta=strchr(formName,',');if(meta)*meta=0;text(formName,x+5*k,y+fh-22*k,13,white,fw-10*k);
    if(v.formDisabled[f])dl->AddRectFilled(ImVec2(x,y),ImVec2(x+fw,y+fh),IM_COL32(20,12,26,180),4*k);
    if(selected)dl->AddRect(ImVec2(x,y),ImVec2(x+fw,y+fh),gold,4*k,0,2*k);
   }
  }
  if(v.loading)text(es?"Cargando modelo...":"Loading model...",sz.x*.48f,174*k,13,pink,sz.x*.48f-edge);
 }
 // Team cards share two rows; chips remain square for every legal composition.
 float base=sz.y-166*k,usable=sz.x-edge*2;int teams=MS_Teams(&v),cols=(teams+1)/2;
 for(int team=0;team<teams;team++){
  int count=MS_Count(&v,team),start=0;while(start<v.total&&MS_Side(&v,start)!=team)start++;
  float cw=usable/cols,x=edge+(team%cols)*cw,y=base+(team/cols)*63*k;
  snprintf(line,sizeof(line),es?"EQUIPO %d":"TEAM %d",team+1);text(line,x,y,12,team&1?pink:gold,cw-8*k);
  float w=SDL_min(39*k,(cw-8*k-(count-1)*3*k)/count);
  for(int j=0;j<count;j++){int slot=start+j;MsPick&p=v.pick[slot];float px=x+j*(w+3*k),py=y+17*k;
   dl->AddRectFilled(ImVec2(px,py),ImVec2(px+w,py+w),IM_COL32(80,45,74,170),3*k);
   if(p.chara>=0){if(auto*t=ms_art(Port_Beta8CostumeIdentity(p.chara,p.costume)))dl->AddImage(ImTextureRef((ImTextureID)(intptr_t)t),ImVec2(px,py),ImVec2(px+w,py+w));}
   if(v.phase&&slot==v.slot)dl->AddRect(ImVec2(px,py),ImVec2(px+w,py+w),gold,3*k,0,2*k);
   if(p.pad>=0){snprintf(line,sizeof(line),"P%d",p.pad+1);text(line,px+2*k,py+w-13*k,11,gold,w-3*k);}
  }
 }
 if(v.error==2)text(es?"Este personaje ya está en tu equipo.":"This fighter is already on your team.",edge,sz.y-44*k,15,pink,sz.x-edge*2);
 else if(v.error)text(es?"La distribucion no tiene espacio para todos los jugadores.":"This split has no space for all assigned players.",edge,sz.y-44*k,15,pink,sz.x-edge*2);
 else if(v.phase==3){const ModPadHint h[]={{MP_CROSS,es?"Confirmar":"Confirm"},{MP_TRIANGLE,es?"Volver / editar":"Back / edit"},{MP_CIRCLE,es?"Salir":"Cancel"}};mod_pad_hints(dl,ImVec2(edge,sz.y-39*k),sz.x-edge*2,k,h,3,15);}
 else {
  {const ModPadHint h[]={{MP_L1R1,es?"Página":"Page"},{MP_L2R2,es?"Traje":"Costume"},{MP_R3,es?"Potaras":"Potaras"}};mod_pad_hints(dl,ImVec2(edge,sz.y-46*k),sz.x-edge*2,k,h,3,13);}
  const char *labels[2][4]={{"Choose","Auto-fill","Back","Cancel"},{"Elegir","Completar","Atras","Salir"}};
  float yy=sz.y-17*k,step=SDL_min(145*k,(sz.x-edge*2)/4);
  for(int n=0;n<4;n++){float x=edge+n*step;ImU32 c=n==0?IM_COL32(100,174,255,255):n==1?pink:n==2?IM_COL32(92,222,145,255):IM_COL32(247,110,106,255);
   if(n==0){dl->AddLine(ImVec2(x-4*k,yy-4*k),ImVec2(x+4*k,yy+4*k),c,1.7f*k);dl->AddLine(ImVec2(x+4*k,yy-4*k),ImVec2(x-4*k,yy+4*k),c,1.7f*k);}
   else if(n==1)dl->AddRect(ImVec2(x-5*k,yy-5*k),ImVec2(x+5*k,yy+5*k),c,0,0,1.7f*k);
   else if(n==2)dl->AddTriangle(ImVec2(x,yy-6*k),ImVec2(x-6*k,yy+5*k),ImVec2(x+6*k,yy+5*k),c,1.7f*k);
   else dl->AddCircle(ImVec2(x,yy),5*k,c,16,1.7f*k);
   text(labels[es][n],x+12*k,yy-9*k,14,white,step-20*k);
  }
 }
}
