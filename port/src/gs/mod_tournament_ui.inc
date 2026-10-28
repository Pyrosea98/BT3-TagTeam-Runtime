static std::atomic<int> sTagTourHud[4];
extern "C" void Port_TagTourHudState(int team,int member,int owner,int cpu_match){sTagTourHud[0].store(team);sTagTourHud[1].store(member);sTagTourHud[2].store(owner);sTagTourHud[3].store(cpu_match);}
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
