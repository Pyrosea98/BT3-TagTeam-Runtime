static std::atomic<int> sTagTourHud[4];
extern "C" void Port_TagTourHudState(int team,int member,int owner,int cpu_match){sTagTourHud[0].store(team);sTagTourHud[1].store(member);sTagTourHud[2].store(owner);sTagTourHud[3].store(cpu_match);}
#include "../../include/tag_tournament_view.h"
extern "C" int Port_Beta8CostumeIdentity(int,int);
static SDL_GPUTexture *ms_art(int,bool);
static std::atomic<unsigned> sTtBracketSequence{0};
static std::atomic<int> sTtBracketWords[sizeof(TTBracketView)/sizeof(int)];
extern "C" void Port_TagTourBracketPublish(const TTBracketView*v){
 sTtBracketSequence.fetch_add(1,std::memory_order_acq_rel);int w[sizeof(*v)/4];memcpy(w,v,sizeof(*v));
 for(unsigned i=0;i<sizeof(*v)/4;i++)sTtBracketWords[i].store(w[i],std::memory_order_relaxed);
 sTtBracketSequence.fetch_add(1,std::memory_order_release);
}
static bool tournament_bracket_snapshot(TTBracketView&v){
 for(int n=0;n<3;n++){unsigned seq=sTtBracketSequence.load(std::memory_order_acquire);if(seq&1)continue;int w[sizeof(v)/4];for(unsigned i=0;i<sizeof(v)/4;i++)w[i]=sTtBracketWords[i].load(std::memory_order_relaxed);if(seq==sTtBracketSequence.load(std::memory_order_acquire)){memcpy(&v,w,sizeof(v));return true;}}return false;
}
static void tournament_bracket_draw(){
 TTBracketView v;if(!tournament_bracket_snapshot(v)||!v.active)return;
 if(TT_BracketIntro(v.sequence))return;
 static int priorMatch=-99,priorMode=-99;static double begun=0;
 int motion=(v.flags&0x40)?1:(v.flags&0x20)?2:(v.sequence>=1&&v.sequence<=0xB)?3:0;
 if(v.match!=priorMatch||motion!=priorMode){begun=ImGui::GetTime();priorMatch=v.match;priorMode=motion;}
 float progress=SDL_clamp(float((ImGui::GetTime()-begun)/.75),0.f,1.f);
 float ease=progress*progress*(3.f-2.f*progress);
 auto*d=ImGui::GetForegroundDrawList();ImVec2 sz=ImGui::GetIO().DisplaySize;float k=sz.y/720.f,pad=20*k,top=100*k,span=sz.y*.47f,col=(sz.x-pad*2)/5.f,step=span/16.f;
 bool es=Port_Setting("mod_language",1)==1;auto*font=Ui_FontBold();if(!font)font=ImGui::GetFont();
 ImU32 gold=IM_COL32(255,207,78,255),cream=IM_COL32(255,243,215,255),muted=IM_COL32(185,165,161,220);
 auto text=[&](const char*t,float x,float y,float fs,ImU32 c,float fit){fs*=k;float w=font->CalcTextSizeA(fs,10000,0,t).x;if(w>fit&&w>0)fs*=fit/w;d->AddText(font,fs,ImVec2(x+k,y+k),IM_COL32(0,0,0,210),t);d->AddText(font,fs,ImVec2(x,y),c,t);};
 d->AddRectFilledMultiColor(ImVec2(0,0),ImVec2(sz.x,top+span+27*k),IM_COL32(29,20,33,235),IM_COL32(39,24,38,220),IM_COL32(32,24,38,142),IM_COL32(32,24,38,168));
 text(es?"TORNEO TAG TEAM":"TAG TEAM TOURNAMENT",pad,20*k,27,gold,sz.x*.68f);
 char title[110];snprintf(title,sizeof(title),es?"Ronda %d  /  Combate %d":"Round %d  /  Match %d",v.round+1,v.match+1);text(title,pad,55*k,15,cream,sz.x*.5f);
 const char*labels[2][5]={{"Teams","Quarter-finals","Semi-finals","Final","Champion"},{"Equipos","Cuartos de final","Semifinales","Final","Campeón"}};
 for(int c=0;c<5;c++)text(labels[es][c],pad+c*col,top-24*k,12,c==v.round?gold:cream,col-12*k);
 int nodes[5][16];for(auto&row:nodes)for(int&i:row)i=-1;for(int i=0;i<16;i++)nodes[0][i]=i;
 for(int c=1;c<5;c++){int start=c==1?0:c==2?8:c==3?12:14;for(int i=0;i<(16>>c);i++)nodes[c][i]=TT_BracketWinner(&v,start+i);}
 float width=col-24*k;
 auto card=[&](int e,float x,float cy,bool special){
  float h=step-2*k;bool known=e>=0&&e<17&&v.count[e]>0,lost=known&&(v.entry_flags[e]&2),current=known&&v.match>=0&&v.match<16&&(v.entrant[v.match][0]==e||v.entrant[v.match][1]==e);
  ImU32 border=current?gold:known&&v.human[e]?IM_COL32(105,211,246,230):IM_COL32(162,132,103,140);
  d->AddRectFilled(ImVec2(x,cy-h*.5f),ImVec2(x+width,cy+h*.5f),lost?IM_COL32(23,23,28,125):IM_COL32(61,37,54,177),3*k);d->AddRect(ImVec2(x,cy-h*.5f),ImVec2(x+width,cy+h*.5f),border,3*k,0,current?1.5f*k:k*.7f);
  if(!known){text("...",x+width*.45f,cy-h*.44f,12,muted,width*.5f);return;}
  char n[16];snprintf(n,sizeof(n),"%d",v.team[e]+1);text(n,x+3*k,cy-h*.4f,11,lost?muted:cream,19*k);
  float chip=SDL_min(h-2*k,(width-24*k)/SDL_max(1,v.count[e]));
  for(int j=0;j<v.count[e]&&j<5;j++)if(auto*t=ms_art(Port_Beta8CostumeIdentity(v.character[e][j],v.costume[e][j]),false))d->AddImage(ImTextureRef((ImTextureID)(intptr_t)t),ImVec2(x+23*k+j*chip,cy-chip*.5f),ImVec2(x+23*k+(j+1)*chip,cy+chip*.5f),ImVec2(0,0),ImVec2(1,1),lost?IM_COL32(180,180,180,95):IM_COL32(255,255,255,255));
  if(ImGui::IsMouseHoveringRect(ImVec2(x,cy-h*.5f),ImVec2(x+width,cy+h*.5f))){ImGui::BeginTooltip();ImGui::Text(es?"Equipo %d":"Team %d",v.team[e]+1);for(int j=0;j<v.count[e]&&j<5;j++)ImGui::TextUnformatted(Port_CharaName(v.character[e][j]));ImGui::EndTooltip();}
 };
 for(int c=0;c<5;c++){int n=16>>c;for(int i=0;i<n;i++){
  float y=top+(i+.5f)*span/n,x=pad+c*col;
  float reveal=SDL_clamp((float((ImGui::GetTime()-begun)*2.5)-c*.12f),0.f,1.f);
  float drawX=x-(1.f-reveal)*28*k;
  card(nodes[c][i],drawX,y,false);
  if(c<4){float nx=x+col,ny=top+((i/2)+.5f)*span/(n/2),joint=x+width+(col-width)*.5f;ImU32 line=nodes[c][i]>=0?IM_COL32(225,184,92,165):IM_COL32(149,130,124,95);d->AddLine(ImVec2(x+width,y),ImVec2(joint,y),line,k);d->AddLine(ImVec2(joint,y),ImVec2(joint,ny),line,k);d->AddLine(ImVec2(joint,ny),ImVec2(nx,ny),line,k);}
 }}
 /* The selected teams slide toward each other, then the winner advances
    along its own branch. No native single-character placeholder clips. */
 if(motion&&v.match>=0&&v.match<16){
  float cy=top+span*.5f,center=sz.x*.5f;
  float cw=SDL_min(sz.x*.38f,430*k),ch=108*k;
  int left=v.entrant[v.match][0],right=v.entrant[v.match][1];
  float travel=(1.f-ease)*sz.x*.28f;
  auto focus=[&](int e,float x,float y,float scale){
   if(e<0||e>=17||v.count[e]<=0)return;
   float w=cw*scale,h=ch*scale;
   d->AddRectFilled(ImVec2(x,y-h*.5f),ImVec2(x+w,y+h*.5f),IM_COL32(39,23,42,243),8*k*scale);
   d->AddRect(ImVec2(x,y-h*.5f),ImVec2(x+w,y+h*.5f),gold,8*k*scale,0,2*k);
   char team[40];snprintf(team,sizeof(team),es?"EQUIPO %d":"TEAM %d",v.team[e]+1);
   text(team,x+12*k*scale,y-h*.5f+7*k*scale,18*scale,gold,w-24*k*scale);
   float chip=SDL_min(68*k*scale,(w-24*k*scale)/v.count[e]);
   float sx=x+(w-chip*v.count[e])*.5f;
   for(int j=0;j<v.count[e]&&j<5;j++)if(auto*t=ms_art(Port_Beta8CostumeIdentity(v.character[e][j],v.costume[e][j]),false))
    d->AddImage(ImTextureRef((ImTextureID)(intptr_t)t),ImVec2(sx+j*chip,y-h*.5f+33*k*scale),ImVec2(sx+(j+1)*chip,y-h*.5f+33*k*scale+chip));
  };
  if(motion==2){int winner=TT_BracketWinner(&v,v.match);if(winner>=0){
   float start=winner==left?center-cw-35*k:center+35*k;
   float finish=pad+SDL_min(v.round+1,4)*col;
   float shrink=1.f-.73f*ease;
   focus(winner,start+(finish-start)*ease,cy,shrink);
  }}else{
   focus(left,center-cw-35*k-travel,cy,1.f);focus(right,center+35*k+travel,cy,1.f);
   text("VS",center-20*k,cy-18*k,29,gold,45*k);
  }
 }
 float bossY=top+span*.12f;card(16,pad+4*col,bossY,true);text(es?"Reto final":"Final challenge",pad+4*col,bossY-27*k,11,gold,width);
 if(v.match==15){int winner=TT_BracketWinner(&v,15);if(winner>=0){card(winner,pad+4*col,top+span*.8f,true);text(es?"¡Campeón del torneo!":"Tournament champion!",pad+4*col,top+span*.8f+16*k,12,gold,width);}}
 d->AddLine(ImVec2(pad,top+span+18*k),ImVec2(sz.x-pad,top+span+18*k),IM_COL32(224,178,75,150),k);
}

// Native tournament screens publish a compact visual context; no input/gameplay writes.
static std::atomic<unsigned> sTournamentView{0};
static std::atomic<Uint64> sTournamentStamp{0};
extern "C" void Port_TournamentFrame(int stage,int level,int cursor,int round){
 unsigned v=(unsigned)(stage&255)|((unsigned)(level&255)<<8)|((unsigned)(cursor&255)<<16)|((unsigned)(round&255)<<24);
 sTournamentStamp.store(SDL_GetTicks(),std::memory_order_release);
 sTournamentView.store(v,std::memory_order_release);
}
extern "C" int Port_TournamentActive(void){return (sTournamentView.load(std::memory_order_acquire)&255)!=0;}
static bool tournament_visible(){return Port_TournamentActive()&&SDL_GetTicks()-sTournamentStamp.load(std::memory_order_acquire)<150;}
static void tournament_draw(){
 if(sOpen||!tournament_visible())return;
 tournament_bracket_draw();
 unsigned v=sTournamentView.load(std::memory_order_acquire);int stage=v&255,round=(v>>24)&255;
 bool es=Port_Setting("mod_language",1)==1;ImVec2 screen=ImGui::GetIO().DisplaySize;float k=screen.y/448.f;
 ImDrawList *dl=ImGui::GetForegroundDrawList();float y=screen.y-21*k;
 dl->AddRectFilled(ImVec2(0,y),screen,IM_COL32(67,30,58,220));
 dl->AddLine(ImVec2(10*k,y),ImVec2(screen.x-10*k,y),IM_COL32(241,192,65,220),k);
 ImFont *font=Ui_FontBold();if(!font)font=ImGui::GetFont();
 auto label=[&](const char *t,float x,ImU32 colour,float fit){
  float fs=11*k;float w=font->CalcTextSizeA(fs,10000,0,t).x;if(w>fit)fs*=fit/w;
  dl->AddText(font,fs,ImVec2(x+k,y+6*k),IM_COL32(0,0,0,230),t);dl->AddText(font,fs,ImVec2(x,y+5*k),colour,t);
 };
 if(SDL_GPUTexture *brand=mod_brand_texture())dl->AddImage(ImTextureRef((ImTextureID)(intptr_t)brand),ImVec2(8*k,y+2*k),ImVec2(62*k,screen.y-1*k));
 float x=78*k,cy=y+11*k;
 dl->AddLine(ImVec2(x-4*k,cy-4*k),ImVec2(x+4*k,cy+4*k),IM_COL32(100,170,255,255),1.8f*k);
 dl->AddLine(ImVec2(x+4*k,cy-4*k),ImVec2(x-4*k,cy+4*k),IM_COL32(100,170,255,255),1.8f*k);
 label(stage==3&&sTagTourHud[3].load()?(es?"Ver":"Watch"):stage==2?(es?"Elegir":"Choose"):(es?"Continuar":"Continue"),x+11*k,IM_COL32(255,245,226,255),74*k);
 x=177*k;dl->AddTriangle(ImVec2(x,cy-5*k),ImVec2(x-5*k,cy+4*k),ImVec2(x+5*k,cy+4*k),IM_COL32(84,224,145,255),1.6f*k);
 label(es?"Volver":"Back",x+12*k,IM_COL32(255,245,226,255),65*k);
 if(stage==3&&sTagTourHud[3].load()){
  float xx=260*k;dl->AddRect(ImVec2(xx,cy-4*k),ImVec2(xx+8*k,cy+4*k),IM_COL32(240,132,200,255),0,0,1.5f*k);
  label(es?"Omitir CPU vs CPU":"Skip CPU vs CPU",xx+13*k,IM_COL32(255,245,226,255),145*k);
 }
 char title[96];if(stage==2&&sTagTourHud[0].load()>=0)snprintf(title,sizeof(title),es?"Equipo %d - P%d - Miembro %d":"Team %d - P%d - Member %d",sTagTourHud[0].load()+1,sTagTourHud[2].load()+1,sTagTourHud[1].load()+1);
 else if(stage==3)snprintf(title,sizeof(title),es?"Torneo - Ronda %d":"Tournament - Round %d",round+1);
 else snprintf(title,sizeof(title),"%s",stage==2?(es?"Torneo - Participantes":"Tournament - Entrants"):(es?"Torneo - Ajustes":"Tournament - Setup"));
 label(title,screen.x-185*k,IM_COL32(255,213,91,255),172*k);
}

// Invitation images share stock IDs with borrowed extra-stage previews. Read the
// original same-ISO resources by dedicated name; never trust a preview as BPE here.
extern "C" int Port_TournamentInvitationLoad(int invite,void *buffer){
 if(invite<0||invite>=5||buffer==NULL)return 0;
 char path[1400];snprintf(path,sizeof(path),"%s/tagteam-ui/tournament-invite-%d.bpe",Port_FileRoot(),invite);
 FILE *f=fopen(path,"rb");if(!f){fprintf(stderr,"bt3: tournament invitation missing: %s\n",path);return 0;}
 if(fseek(f,0,SEEK_END)!=0){fclose(f);return 0;}long size=ftell(f);rewind(f);
 uint32_t h[2];
 if(size<8||size>0xF000||fread(h,sizeof(h),1,f)!=1||h[0]!=264864||h[0]>0x41000||h[1]>(unsigned long)size-8){fclose(f);return 0;}
 rewind(f);memset(buffer,0,0xF000);bool ok=fread(buffer,1,(size_t)size,f)==(size_t)size;fclose(f);
 if(ok)fprintf(stderr,"bt3: tournament invitation %d loaded from dedicated original asset (%ld bytes)\n",invite,size);
 return ok?1:0;
}
