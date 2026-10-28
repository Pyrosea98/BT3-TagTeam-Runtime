// Native Select menu: game owns navigation; render thread receives one packed snapshot.
#include "mod_menu_model.h"
#include "../../include/tag_tournament_port.h"
static std::atomic<int> sTourSetupView[10];
static std::atomic<int> sCoopSetupView[5];
static ModMenuState sModMenuGame={0,0,0}; // touched only by MainMenu's game thread
static std::atomic<unsigned> sModMenuView{0};
static int sModLastPlay=-1,sModResumePlay=-1; // game thread only
static std::atomic<int> sModMenuSettings{-1};
extern "C" int Port_ModMenuActive(void){return (sModMenuView.load(std::memory_order_acquire)&255)!=0;}
extern "C" void Port_ModMenuReset(void){sModMenuGame={0,0,0};sModMenuView.store(0,std::memory_order_release);sModMenuSettings.store(-1);}
extern "C" int Port_ModMenuStep(unsigned pressed,unsigned repeat,int *players,int *sound){
 int from=(sModMenuGame.page==MM_PLAYERS||sModMenuGame.page==MM_COOPSETUP)?sModMenuGame.pick:sModMenuGame.cursor;
 int r=mm_step(&sModMenuGame,pressed,repeat,players,sound);
 if(r>=100&&r<112){sModMenuSettings.store(r-100,std::memory_order_release);r=0;}
 sModMenuView.store((unsigned)sModMenuGame.page|((unsigned)sModMenuGame.cursor<<8)|((unsigned)sModMenuGame.pick<<16),std::memory_order_release);
 if(sModMenuGame.page==MM_TOURSETUP){const TTSetup &t=sModMenuGame.tour;
  sTourSetupView[0].store(t.config.players);sTourSetupView[1].store(t.config.team_size);sTourSetupView[2].store(t.config.type);sTourSetupView[3].store(t.config.difficulty);
  for(int n=0;n<4;n++)sTourSetupView[4+n].store(t.config.player_team[n]);sTourSetupView[8].store(t.error);}
 if(sModMenuGame.page==MM_COOPSETUP){sCoopSetupView[0].store(sModMenuGame.coop.players);for(int n=0;n<4;n++)sCoopSetupView[n+1].store(sModMenuGame.coop.team[n]);}
 if(r>=1&&r<=3){
  if(from==1||from==6){const CoopSetup &c=sModMenuGame.coop;Port_CoopConfigure(c.players,c.team[0],c.team[1],c.team[2],c.team[3],from==6);}
  else Port_CoopReset();
 }
 if(r==30&&!Port_TagTourBegin(&sModMenuGame.tour.config))return -1;
 if((r>=1&&r<=3)||r==30){sModLastPlay=r==30?4:from;Port_ModMenuReset();return r;}
 return sModMenuGame.page||r==10?-1:0;
}
extern "C" int Port_ModMenuReturnFromSelect(void){
 if(Port_ModesFight()==PORT_MODE_NONE||sModLastPlay<0)return 0;
 sModResumePlay=sModLastPlay;return 1;
}
extern "C" void Port_ModMenuReturnFromTournament(void){sModResumePlay=4;}
extern "C" void Port_ModMenuEnterMain(void){
 Port_TagTourCancel();
 Port_ModMenuReset();
 if(sModResumePlay>=0){mm_resume(&sModMenuGame,sModResumePlay);sModResumePlay=-1;
  sModMenuView.store((unsigned)sModMenuGame.page|((unsigned)sModMenuGame.cursor<<8),std::memory_order_release);}
}
static SDL_GPUTexture *sModNav,*sModNimbus,*sModCategory,*sModSections,*sModPlayers[4];static bool sModArtTried;
static SDL_GPUTexture *mod_menu_art(const char *name){
 char path[1400];snprintf(path,sizeof(path),"%s/tagteam-ui/%s.rgba",Port_FileRoot(),name);
 FILE *f=fopen(path,"rb");if(!f)return NULL;uint32_t h[2];
 if(fread(h,sizeof(h),1,f)!=1||!h[0]||!h[1]||h[0]>4096||h[1]>4096){fclose(f);return NULL;}
 size_t n=(size_t)h[0]*h[1]*4;uint8_t *p=(uint8_t*)malloc(n);
 if(!p||fread(p,1,n,f)!=n||fgetc(f)!=EOF){free(p);fclose(f);return NULL;}fclose(f);return Ui_Texture(p,h[0],h[1]);
}
static void mod_menu_draw(){
 unsigned view=sModMenuView.load(std::memory_order_acquire);int page=view&255,cursor=(view>>8)&255,pick=(view>>16)&255;
 if(!page||sOpen)return;
 if(!sModArtTried){sModArtTried=true;sModNimbus=mod_menu_art("menu-nimbus");for(int n=0;n<4;n++){char name[40];snprintf(name,sizeof(name),"menu-player-%d",n+1);sModPlayers[n]=mod_menu_art(name);}}
 bool es=Port_Setting("mod_language",1)==1;
 static const char *root[2][4]={{"Play","Settings","About / Credits","Return to game"},{"Jugar","Ajustes","Acerca de / Creditos","Volver al juego"}};
 static const char *play[2][7]={{"Team Battle","Co-op Battle","Free-for-All","Training","Tag Team Tournament","Scenarios (coming soon)","Co-op Training"},{"Batalla en equipo","Batalla cooperativa","Todos contra todos","Entrenamiento","Torneo Tag Team","Escenarios (proximamente)","Entrenamiento cooperativo"}};
 static const char *settings[2][12]={{"Video","Effects","Audio","Interface","Controls","Cheats","System","Extra fighters","Battle HUD","Fusion","CPU / Training","Struggles"},{"Video","Efectos","Audio","Interfaz","Controles","Trucos","Sistema","Luchadores extra","HUD de combate","Fusion","CPU / Entrenamiento","Choques"}};
 static const char *help[2][4]={{"Choose a mode and prepare your next battle.","Configure the native game and your controllers.","Creators and contributors.","Close this menu and return to the original menu."},{"Elige un modo y prepara la siguiente batalla.","Configura el juego y tus controles.","Creadores y colaboradores.","Cierra este menu y vuelve al menu original."}};
 static const char *playHelp[2][7]={{"One player with CPU teammates.","Two to four local players. Choose the player count next.","Every fighter for themselves. One to four local players.","One-player practice with a CPU opponent.","Draft your team and enter the Tag Team Tournament.","Coming soon: story scenarios are not implemented yet.","Two to four players. Assign teams, then choose every fighter."},{"Un jugador con companeros CPU.","De dos a cuatro jugadores locales. Elige cuantos al continuar.","Todos contra todos. De uno a cuatro jugadores locales.","Practica de un jugador contra la CPU.","Elige tus equipos y compite en el Torneo Tag Team.","Proximamente: el modo de escenarios aun no esta implementado.","De dos a cuatro jugadores. Asigna equipos y elige los luchadores."}};
 ImVec2 size=ImGui::GetIO().DisplaySize;float k=size.y/448.f,margin=20*k;
 // Use wide-screen space while preserving readable, height-based text sizes.
 float left=margin,right=size.x-margin,content=(right-left)*.67f;
 if(size.x/size.y<1.5f)content=right-left;
 ImDrawList *dl=ImGui::GetForegroundDrawList();ImFont *font=Ui_FontBold();if(!font)font=ImGui::GetFont();
 auto text=[&](const char *t,float x,float y,float fs,ImU32 col,float fit){
  if(mod_legacy_text(dl,t,ImVec2(x,y),fs*k,col,fit,false,fs>=20))return;
  fs*=k;float width=font->CalcTextSizeA(fs,10000,0,t).x;if(width>fit)fs*=fit/width;
  dl->AddText(font,fs,ImVec2(x,y),IM_COL32(0,0,0,220),t);dl->AddText(font,fs,ImVec2(x-k,y-k),col,t);
 };
 if(SDL_GPUTexture *brand=mod_brand_texture())dl->AddImage(ImTextureRef((ImTextureID)(intptr_t)brand),ImVec2(left,8*k),ImVec2(left+185*k,70*k));
 const char *title=page==MM_ROOT?(es?"Modos modificados":"Modded Modes"):page==MM_PLAY?(es?"Jugar":"Play"):page==MM_COOPSETUP?(es?"Equipos de jugadores":"Player teams"):page==MM_PLAYERS?(es?"Jugadores":"Players"):page==MM_SETTINGS?(es?"Ajustes":"Settings"):page==MM_TOURSETUP?(es?"Torneo Tag Team":"Tag Team Tournament"):(es?"Acerca de / Creditos":"About / Credits");
 text(title,left,76*k,23,IM_COL32(255,182,55,255),content);dl->AddLine(ImVec2(left,105*k),ImVec2(left+content,105*k),IM_COL32(249,197,71,210),1.5f*k);
 int count=page==MM_PLAYERS?((pick==1||pick==6)?3:4):mm_count(page);
 if(page==MM_COOPSETUP){
  int humans=sCoopSetupView[0].load();
  for(int n=0;n<=humans;n++){char label[60],value[50]="";float y=(133+39*n)*k;
   if(n<humans){snprintf(label,sizeof(label),es?"Equipo de P%d":"P%d team",n+1);snprintf(value,sizeof(value),es?"Equipo %d":"Team %d",sCoopSetupView[n+1].load()+1);}
   else snprintf(label,sizeof(label),es?"Elegir luchadores":"Choose fighters");
   ImU32 color=n==cursor?IM_COL32(255,220,80,255):IM_COL32(255,255,255,255);
   if(n==cursor)dl->AddLine(ImVec2(left,y+25*k),ImVec2(left+content,y+25*k),IM_COL32(255,218,77,240),1.7f*k);
   text(label,left+8*k,y,17,color,content*.58f);text(value,left+content*.62f,y,17,color,content*.36f);
  }
 }else if(page==MM_TOURSETUP){
  const char *labels[2][9]={{"Players","Fighters per team","Tournament","CPU difficulty","P1 team","P2 team","P3 team","P4 team","Start team draft"},{"Jugadores","Luchadores por equipo","Torneo","Dificultad CPU","Equipo P1","Equipo P2","Equipo P3","Equipo P4","Elegir equipos"}};
  const char *types[2][5]={{"World Tournament","Big Tournament","Cell Games","Otherworld","Yamcha Games"},{"Torneo Mundial","Gran Torneo","Juegos de Cell","Otro Mundo","Juegos de Yamcha"}};
  const char *levels[2][3]={{"Easy","Normal","Hard"},{"Facil","Normal","Dificil"}};
  for(int n=0;n<9;n++){char value[80]="";int v=n<8?sTourSetupView[n].load():0;float y=(119+26*n)*k;
   if(n==0)snprintf(value,sizeof(value),v?"%d":"COM",v);
   else if(n==1)snprintf(value,sizeof(value),"%d",v);
   else if(n==2)snprintf(value,sizeof(value),"%s",types[es][v>=0&&v<5?v:0]);
   else if(n==3)snprintf(value,sizeof(value),"%s",levels[es][v>=0&&v<3?v:1]);
   else if(n<8)snprintf(value,sizeof(value),n-4<sTourSetupView[0].load()?"%d":"--",v+1);
   if(n==cursor)dl->AddLine(ImVec2(left,y+21*k),ImVec2(left+content,y+21*k),IM_COL32(255,218,77,240),1.7f*k);
   ImU32 color=n==cursor?IM_COL32(255,220,80,255):IM_COL32(255,255,255,255);
   text(labels[es][n],left+8*k,y,15,color,content*.48f);text(value,left+content*.52f,y,15,color,content*.46f);
  }
 }else if(page==MM_CREDITS){
  text("Tag Team Mod - Real Power Scale",left+12*k,142*k,20,IM_COL32(255,235,204,255),content-24*k);
  for(int i=0;i<3;i++){
   char title[160];snprintf(title,sizeof(title),"%s: %s",es?kModCreditRolesEs[i]:kModCreditRoles[i],kModCreditNames[i]);
   text(title,left+12*k,(177+43*i)*k,16,IM_COL32(255,220,80,255),content-24*k);
   text(kModCreditUrls[i],left+12*k,(196+43*i)*k,10,IM_COL32(255,235,204,255),content-24*k);
  }
  text(kModThanks[es?1:0],left+12*k,308*k,12,IM_COL32(255,235,204,255),content-24*k);
  text("Tenkaichi3Decomp - z3xox and contributors",left+12*k,342*k,12,IM_COL32(255,235,204,255),content-24*k);
 }else{
  int rows=(count+1)/2;float gap=14*k,w=(content-gap)*.5f,h=(235*k-gap*(rows-1))/rows;
  for(int i=0;i<count;i++){
   float x=left+(i%2)*(w+gap),y=122*k+(i/2)*(h+gap);bool selected=i==cursor;bool compact=page==MM_SETTINGS||rows>=4;
   // Beta8 style: floating labels and a Nimbus underline, no opaque card backs.
   if(selected){dl->AddRectFilled(ImVec2(x,y),ImVec2(x+w,y+h),IM_COL32(255,208,73,22),4*k);dl->AddLine(ImVec2(x,y+h-3*k),ImVec2(x+w,y+h-3*k),IM_COL32(255,218,77,240),1.7f*k);}
   float bob=selected?(float)SDL_sin(SDL_GetTicks()*.004)*2*k:0;
   int symbol=MI_PLAY;
   if(page==MM_ROOT){static const int ids[]={MI_PLAY,MI_SYSTEM,MI_CREDITS,MI_RETURN};symbol=ids[i];}
   else if(page==MM_PLAY){static const int ids[]={MI_TEAM,MI_COOP,MI_FFA,MI_TRAINING,MI_TROPHY,MI_STORY,MI_TRAINING};symbol=ids[i];}
   else if(page==MM_SETTINGS)symbol=i==10?MI_TRAINING:i==11?MI_STRUGGLES:MI_VIDEO+i;
   else if(page==MM_PLAYERS)symbol=MI_COOP;
   if(page==MM_PLAYERS&&i+((pick==1||pick==6)?2:1)>=1&&i+((pick==1||pick==6)?2:1)<=4&&sModPlayers[i+((pick==1||pick==6)?2:1)-1]){int humans=i+((pick==1||pick==6)?2:1);dl->AddImage(ImTextureRef((ImTextureID)(intptr_t)sModPlayers[humans-1]),ImVec2(x+7*k,y+7*k),ImVec2(x+63*k,y+35*k));}
   else mod_icon_draw(dl,symbol,ImVec2(x+23*k,y+(compact?h*.5f:22*k)+bob),(compact?SDL_min(15*k,h*.43f):15*k),selected?IM_COL32(255,215,87,255):IM_COL32(255,239,210,255));
   char player[40];snprintf(player,sizeof(player),es?"%d jugadores":"%d players",i+((pick==1||pick==6)?2:1));if(page==MM_PLAYERS&&i==0&&pick!=1&&pick!=6)snprintf(player,sizeof(player),es?"1 jugador":"1 player");
   const char *label=page==MM_ROOT?root[es][i]:page==MM_PLAY?play[es][i]:page==MM_SETTINGS?settings[es][i]:player;
   text(label,x+(compact?46:8)*k,y+(compact?h*.5f-7*k:h-30*k),compact?14:17,selected?IM_COL32(255,220,80,255):IM_COL32(255,255,255,255),w-(compact?54:18)*k);
   if(selected&&sModNimbus)dl->AddImage(ImTextureRef((ImTextureID)(intptr_t)sModNimbus),ImVec2(x+w-41*k,y+h-22*k+bob),ImVec2(x+w,y+h+bob));
  }
 }
 float banner=374*k;dl->AddRectFilled(ImVec2(0,banner),ImVec2(size.x,426*k),IM_COL32(120,68,27,145));dl->AddLine(ImVec2(margin,banner),ImVec2(right,banner),IM_COL32(246,191,57,230),k);
 const char *description=page==MM_COOPSETUP?(es?"Izquierda/derecha: equipo. Cada jugador elige su luchador; el lider elige los CPU.":"Left/right: team. Each player picks their fighter; the team leader picks CPU partners."):page==MM_TOURSETUP?(sTourSetupView[8].load()?(es?"Demasiados jugadores en un equipo. Aumenta su tamano o separalos.":"Too many players on one team. Increase team size or split the players."):(es?"Arriba/abajo: opcion. Izquierda/derecha: cambiar. Cada jugador elige su luchador.":"Up/down: option. Left/right: change. Each player drafts their own fighter.")):page==MM_ROOT?help[es][cursor]:page==MM_PLAY?playHelp[es][cursor]:page==MM_SETTINGS?(es?"Configura esta categoria y guarda tus ajustes.":"Configure this category and save your settings."):page==MM_PLAYERS?(es?"Elige cuantos jugadores humanos participan.":"Choose how many human players participate."):(es?"Se conservan los creditos originales al iniciar el juego.":"Original creator credits remain in the full startup sequence.");
 text(description,margin+8*k,388*k,15,IM_COL32(255,248,230,255),right-margin-16*k);
 // Controller shapes are drawn geometrically: no missing-font glyphs.
 float yy=438*k;dl->AddLine(ImVec2(left,yy-4*k),ImVec2(left+8*k,yy+4*k),IM_COL32(104,174,255,255),2*k);dl->AddLine(ImVec2(left+8*k,yy-4*k),ImVec2(left,yy+4*k),IM_COL32(104,174,255,255),2*k);
 text(es?"Abrir":"Open",left+15*k,431*k,12,IM_COL32(255,255,255,255),80*k);
 float tx=left+105*k;dl->AddTriangle(ImVec2(tx,yy-6*k),ImVec2(tx-6*k,yy+5*k),ImVec2(tx+6*k,yy+5*k),IM_COL32(81,226,150,255),1.5f*k);
 text(es?"Volver":"Back",tx+13*k,431*k,12,IM_COL32(255,255,255,255),80*k);
 text(es?"Direcciones: elegir    SELECT: cerrar":"Directions: choose    SELECT: close",left+211*k,431*k,12,IM_COL32(255,255,255,255),right-left-211*k);
}
