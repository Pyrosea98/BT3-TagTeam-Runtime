// Render-thread-only artwork, gallery and unlock presentation.
#include "../../include/mod_scenarios.h"
static int achievement_count(unsigned m){int n=0;for(int i=0;i<MA_COUNT;i++)if(m&(1u<<i))n++;return n;}
static std::atomic<int> sAchievementPick{-1};
static std::atomic<bool> sAchievementBack{false};
static SDL_GPUTexture *mod_reward_art(int index){
 static SDL_GPUTexture *tex[6];static bool tried[6];
 static const char *names[]={"dragon-trophy-v1","random-orb-v1","trophy-bronze-v2","trophy-silver-v2","trophy-gold-v2","trophy-platinum-v2"};
 if(index<0||index>5)return nullptr;if(tried[index])return tex[index];tried[index]=true;
 char path[1024];uint32_t wh[2];snprintf(path,sizeof(path),"%s/beta8/mod-select/ui/%s.rgba",Port_FileRoot(),names[index]);
 FILE*f=fopen(path,"rb");if(!f)return nullptr;
 if(fread(wh,4,2,f)!=2||wh[0]!=512||wh[1]!=512){fclose(f);return nullptr;}
 auto *pixels=(uint8_t*)SDL_malloc(512*512*4);if(!pixels){fclose(f);return nullptr;}
 bool ok=fread(pixels,1,512*512*4,f)==512*512*4&&fgetc(f)==EOF;fclose(f);
 if(ok)tex[index]=Ui_Texture(pixels,512,512);else SDL_free(pixels);return tex[index];
}
static const char *achievement_rank_name(int rank,bool es){
 static const char *names[2][MA_RANK_COUNT]={{"Bronze","Silver","Gold","Platinum"},{"Bronce","Plata","Oro","Platino"}};
 return rank>=0&&rank<MA_RANK_COUNT?names[es][rank]:"";
}
static ImVec4 achievement_rank_colour(int rank){
 static const ImVec4 c[]={ImVec4(.88f,.55f,.31f,1),ImVec4(.79f,.84f,.91f,1),ImVec4(1,.80f,.28f,1),ImVec4(.60f,.94f,1,1)};
 return c[rank>=0&&rank<MA_RANK_COUNT?rank:0];
}
static SDL_GPUTexture *achievement_trophy(int id){int rank=Port_AchievementRank(id);auto*t=rank>=0?mod_reward_art(2+rank):nullptr;return t?t:mod_reward_art(0);}
static const char *achievement_name(int i,bool es){
 if(i==20)return es?"Historia Drag\303\263n: el primer paso":"Dragon History: the first step";
 if(i==21)return es?"Historia Drag\303\263n: un verdadero guerrero":"Dragon History: a true warrior";
 if(i==22)return es?"Historia Drag\303\263n: m\303\241s all\303\241 del l\303\255mite":"Dragon History: beyond the limit";
 if(i==23)return es?"Saga Saiyajin: la Tierra resiste":"Saiyan Saga: Earth holds the line";
 if(i==24)return es?"Saga Saiyajin: defensores de la Tierra":"Saiyan Saga: defenders of Earth";
 if(i==25)return es?"Saga Saiyajin: m\303\241s fuertes que el orgullo":"Saiyan Saga: stronger than pride";
 if(i==18)return es?"Una presencia desconocida":"An unfamiliar presence";
 if(i==19)return es?"El destino puede cambiar":"Destiny can change";
 static const char *names[2][2]={{"Conqueror of the Seven Dragons","Legend of Super Saiyan 4"},{"Vencedor de los siete dragones","Leyenda del Super Saiyan 4"}};if(i<2)return names[es][i];for(int r=0;r<SC_COUNT;r++)if(scRoutes[r].achievement==i)return scRoutes[r].name[es];return "";
}
static const char *achievement_description(int i,bool es){
 if(i==20)return es?"Completa todos los cap\303\255tulos de Historia Drag\303\263n en F\303\241cil o superior. El trofeo se habilitar\303\241 cuando la campa\303\261a completa est\303\251 disponible.":"Clear every Dragon History chapter on Easy or above. This trophy becomes available when the full campaign is released.";
 if(i==21)return es?"Completa todos los cap\303\255tulos de Historia Drag\303\263n en Normal o superior. El trofeo se habilitar\303\241 cuando la campa\303\261a completa est\303\251 disponible.":"Clear every Dragon History chapter on Normal or above. This trophy becomes available when the full campaign is released.";
 if(i==22)return es?"Completa todos los cap\303\255tulos de Historia Drag\303\263n en Dif\303\255cil. El trofeo se habilitar\303\241 cuando la campa\303\261a completa est\303\251 disponible.":"Clear every Dragon History chapter on Hard. This trophy becomes available when the full campaign is released.";
 if(i==23)return es?"Gana todos los combates de la saga Saiyajin en F\303\241cil o superior. Las derrotas que la historia exige te dejan avanzar, pero el cap\303\255tulo necesita una victoria real.":"Win every Saiyan Saga battle on Easy or above. Story-required defeats let you move on, but each chapter still needs a real victory.";
 if(i==24)return es?"Gana todos los combates de la saga Saiyajin en Normal o superior.":"Win every Saiyan Saga battle on Normal or above.";
 if(i==25)return es?"Gana todos los combates de la saga Saiyajin en Dif\303\255cil.":"Win every Saiyan Saga battle on Hard.";
 if(i==18)return es?"Completa el primer cap\303\255tulo de Historia Drag\303\263n, siguiendo la historia o venciendo a Raditz.":"Clear the first Dragon History chapter, following history or defeating Raditz.";
 if(i==19)return es?"Vence a Raditz con Piccoro en el primer capítulo de Historia Dragón.":"Defeat Raditz as Piccolo in the first Dragon History chapter.";
 if(i>=2){for(int r=0;r<SC_COUNT;r++)if(scRoutes[r].achievement==i)return scRoutes[r].brief[es];return "";}
 return i==0?(es?"Gana por KO, en solitario, contra los siete dragones: uno de cada estrella. Sus transformaciones y la absorción de Omega no invalidan el reto.":"Win by KO, solo, against the seven Shadow Dragons: one of each star. Their transformations and Omega's absorption do not invalidate the challenge."):(es?"Completa el mismo reto terminando como cualquier Goku Super Saiyan 4. Puedes empezar en base y transformarte durante el combate.":"Complete the same challenge, finishing as any Super Saiyan 4 Goku. You can start in base and transform during the fight.");
}
static void achievement_gallery(int selected){
 bool es=Port_Setting("mod_language",1)==1;ImGuiIO &io=ImGui::GetIO();
 selected=SDL_clamp(selected,0,MA_COUNT-1);
 static int previous=-1;bool moved=selected!=previous;previous=selected;
 float k=SDL_max(.55f,SDL_min(1.7f,io.DisplaySize.y/800.f)),pad=24*k;
 if(ImGui::IsKeyPressed(ImGuiKey_Escape)){sAchievementBack.store(true,std::memory_order_release);return;}
 ImGui::SetNextWindowPos(ImVec2(0,0));ImGui::SetNextWindowSize(io.DisplaySize);
 ImGui::PushStyleColor(ImGuiCol_WindowBg,ImVec4(.10f,.025f,.13f,.99f));ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,ImVec2(pad,pad));
 if(ImGui::Begin("##achievement-gallery",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoNavInputs)){
  if(sModBodyFont)ImGui::PushFont(sModBodyFont,20.f);ImGui::SetWindowFontScale(k);
  if(ImGui::Button(es?"Volver":"Back"))sAchievementBack.store(true,std::memory_order_release);ImGui::SameLine();ImGui::TextColored(ImVec4(1,.8f,.3f,1),es?"SALA DE TROFEOS":"TROPHY ROOM");
  unsigned mask=Port_AchievementMask();ImGui::TextDisabled(es?"%d / %d completados  |  Perfil local":"%d / %d unlocked  |  Local profile",achievement_count(mask),MA_COUNT);ImGui::Separator();ImGui::Spacing();
  // All four ranks are visible now; unused ranks are reserved for future challenges.
  for(int rank=0;rank<MA_RANK_COUNT;rank++){
   int earned=0,total=0;for(int id=0;id<MA_COUNT;id++)if(Port_AchievementRank(id)==rank){total++;if(mask&(1u<<id))earned++;}
   if(rank)ImGui::SameLine(0,20*k);
   ImGui::BeginGroup();if(auto*t=mod_reward_art(2+rank)){ImGui::Image(ImTextureRef((ImTextureID)(intptr_t)t),ImVec2(38*k,38*k));ImGui::SameLine();}
   ImGui::TextColored(achievement_rank_colour(rank),"%s %d/%d",achievement_rank_name(rank,es),earned,total);ImGui::EndGroup();
  }ImGui::Separator();ImGui::Spacing();
  float w=ImGui::GetContentRegionAvail().x,left=SDL_min(w*.43f,480*k),h=ImGui::GetContentRegionAvail().y-65*k;
  if(ImGui::BeginChild("challenge-list",ImVec2(left,h),ImGuiChildFlags_Borders|ImGuiChildFlags_NavFlattened)){
   ImGui::TextColored(ImVec4(.92f,.5f,.72f,1),es?"RETOS DE COMBATE":"BATTLE CHALLENGES");
   for(int i=0;i<MA_COUNT;i++){
    ImGui::PushID(i);bool earned=(mask&(1u<<i))!=0;
    ImGui::PushStyleColor(ImGuiCol_Header,selected==i?ImVec4(.65f,.34f,.13f,.7f):ImVec4(.3f,.12f,.28f,.6f));
    if(ImGui::Selectable(achievement_name(i,es),selected==i,0,ImVec2(0,70*k)))sAchievementPick.store(i,std::memory_order_release);
    if(selected==i&&moved)ImGui::SetScrollHereY(.5f);
    ImGui::PopStyleColor();ImGui::TextDisabled("%s | %s",achievement_rank_name(Port_AchievementRank(i),es),earned?(es?"COMPLETADO":"UNLOCKED"):(es?"PENDIENTE":"LOCKED"));ImGui::Spacing();ImGui::PopID();
   }
   ImGui::Separator();ImGui::TextWrapped("%s",es?"Elige estos retos desde Jugar > Escenarios o Historia Drag\303\263n.":"Choose these challenges from Play > Scenarios or Dragon History.");
  }ImGui::EndChild();ImGui::SameLine();
  if(ImGui::BeginChild("trophy-detail",ImVec2(0,h),ImGuiChildFlags_Borders)){
   float tw=SDL_min(ImGui::GetContentRegionAvail().x-20*k,SDL_min(h*.47f,280*k));tw=SDL_max(40.f,tw);
   bool earned=(mask&(1u<<selected))!=0;
   ImGui::SetCursorPosX((ImGui::GetContentRegionAvail().x-tw)*.5f);
   if(auto *t=achievement_trophy(selected))ImGui::Image(ImTextureRef((ImTextureID)(intptr_t)t),ImVec2(tw,tw),ImVec2(0,0),ImVec2(1,1),earned?ImVec4(1,1,1,1):ImVec4(.52f,.52f,.61f,.9f),ImVec4(0,0,0,0));
   ImGui::TextColored(achievement_rank_colour(Port_AchievementRank(selected)),"%s | %s",achievement_rank_name(Port_AchievementRank(selected),es),achievement_name(selected,es));
   ImGui::TextWrapped("%s",achievement_description(selected,es));ImGui::Spacing();
   ImGui::TextColored(earned?ImVec4(.5f,1,.7f,1):ImVec4(.8f,.73f,.84f,1),"%s",earned?(es?"Trofeo conseguido":"Trophy earned"):(es?"Trofeo por conseguir":"Trophy not yet earned"));
   ImGui::TextWrapped("%s",es?"Combates locales. Guardado automático. Entrenamiento y repeticiones no cuentan.":"Local battles. Saved automatically. Training and replays do not count.");
   if(!Port_AchievementSaveOk())ImGui::TextWrapped("%s",es?"No se pudo guardar; se reintentará.":"Save failed; retrying automatically.");
  }ImGui::EndChild();ImGui::Separator();const ModPadHint hints[]={{MP_DPAD,es?"Elegir trofeo":"Choose trophy"},{MP_CROSS,es?"Ver":"View"},{MP_TRIANGLE,es?"Volver":"Back"}};mod_pad_hint_items(hints,3,k,16);
  if(sModBodyFont)ImGui::PopFont();
 }ImGui::End();ImGui::PopStyleVar();ImGui::PopStyleColor();
}

/* Dedicated one-shot: no battle voice, music or native result player is stolen. */
extern "C" SDL_AudioDeviceID Port_AudioDevice(void);
extern "C" int gPortSePercent;
static void achievement_chime(){
 static SDL_AudioStream*stream;static SDL_AudioSpec spec;static Uint8*pcm;static Uint32 bytes;static bool tried;
 if(!tried){tried=true;char path[1400];snprintf(path,sizeof(path),"%s/tagteam-ui/achievement-chime.wav",Port_FileRoot());
  if(!SDL_LoadWAV(path,&spec,&pcm,&bytes))fprintf(stderr,"bt3: achievement chime missing: %s\n",SDL_GetError());}
 if(!pcm||!bytes)return;
 if(!stream){auto dev=Port_AudioDevice();if(!dev)return;stream=SDL_CreateAudioStream(&spec,nullptr);if(!stream||!SDL_BindAudioStream(dev,stream)){fprintf(stderr,"bt3: achievement chime bind failed: %s\n",SDL_GetError());if(stream)SDL_DestroyAudioStream(stream);stream=nullptr;return;}}
 SDL_ClearAudioStream(stream);SDL_SetAudioStreamGain(stream,SDL_clamp(gPortSePercent,0,100)/100.f);
 bool ok=SDL_PutAudioStreamData(stream,pcm,bytes)&&SDL_FlushAudioStream(stream);
 fprintf(stderr,"bt3: achievement chime %s (%u bytes)\n",ok?"queued":"failed",bytes);
}

static void achievement_notifications(void){
 static unsigned queue;static int showing=-1;static double until;queue|=Port_AchievementUpdate();double now=ImGui::GetTime();
 if(now>=until){showing=-1;for(int i=MA_COUNT-1;i>=0;i--)if(queue&(1u<<i)){showing=i;queue&=~(1u<<i);until=now+7;achievement_chime();break;}}if(showing<0)return;
 bool es=Port_Setting("mod_language",1)==1;ImGuiIO &io=ImGui::GetIO();float k=SDL_max(.65f,SDL_min(1.5f,io.DisplaySize.y/900));
 ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x-20*k,20*k),ImGuiCond_Always,ImVec2(1,0));ImGui::SetNextWindowSize(ImVec2(SDL_min(io.DisplaySize.x-40*k,480*k),0));ImGui::SetNextWindowBgAlpha(.94f);
 if(ImGui::Begin("##achievement",nullptr,ImGuiWindowFlags_AlwaysAutoResize|ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoInputs|ImGuiWindowFlags_NoNav|ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoFocusOnAppearing)){
  if(auto*t=achievement_trophy(showing)){ImGui::Image(ImTextureRef((ImTextureID)(intptr_t)t),ImVec2(85*k,85*k));ImGui::SameLine();}
  ImGui::BeginGroup();ImGui::TextColored(achievement_rank_colour(Port_AchievementRank(showing)),"%s",es?"LOGRO DESBLOQUEADO":"ACHIEVEMENT UNLOCKED");ImGui::PushTextWrapPos(ImGui::GetCursorPosX()+300*k);ImGui::TextWrapped("%s",achievement_name(showing,es));ImGui::PopTextWrapPos();ImGui::TextDisabled("%s",achievement_rank_name(Port_AchievementRank(showing),es));ImGui::EndGroup();
 }ImGui::End();
}
