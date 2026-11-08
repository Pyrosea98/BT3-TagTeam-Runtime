// Dragon History scene: original layered guides/covers and authored animation, restored landscape.
// All game state arrives through snapshots. No game pointers or file reads per frame.
#include "mod_story_animation.inc"
#include "mod_story_guide.h"
#include "mod_story_commentary.h"
extern "C" const char *Port_CharaPower(int,int);
static SDL_GPUTexture *story_art(const char *name){
 struct Item{char name[48];SDL_GPUTexture*t;};static Item cache[128];static int count;
 for(int i=0;i<count;i++)if(!strcmp(cache[i].name,name))return cache[i].t;
 if(count>=128)return nullptr;Item&i=cache[count++];snprintf(i.name,sizeof(i.name),"%s",name);
 char path[96];snprintf(path,sizeof(path),"history-v2/%s",name);return i.t=mod_menu_art(path);
}
static void story_image(ImDrawList*d,SDL_GPUTexture*t,ImVec2 a,ImVec2 b,ImU32 tint=IM_COL32_WHITE){
 if(t)d->AddImage(ImTextureRef((ImTextureID)(intptr_t)t),a,b,ImVec2(0,0),ImVec2(1,1),tint);
}
static const char *storySagaNames[2][8]={
 {"Saiyan Saga","Frieza Saga","Android Saga","Majin Buu Saga","Special Saga","GT Saga","Dragon Ball Saga","What If Saga"},
 {"Saga Saiyan","Saga de Freezer","Saga de los Androides","Saga de Majin Buu","Historias especiales","Saga GT","Saga Dragon Ball","Historias alternativas"}};
static const char *storySagaGuides[8]={"Piccolo","Vegeta","Trunks","Mr. Satan","Videl","Pan","Goku","Kaioshin"};
static const char *storySagaRecaps[2][8]={
 {"This is where it all started! I never expected a brother from space. Raditz sure gave us a tough fight!","Frieza really pushed us to our limits! Come on, let's see how everyone fought to protect Namek.","Trunks came all the way from the future to warn us. We had a lot of training to do before the androids arrived!","Buu was one tough opponent! Strength alone wasn't enough. Good thing I had friends I could count on!","We've met some amazing fighters on these adventures! Ready to take them on again?","What a journey that was! With Pan and the others around, things never stayed quiet for long!","Look how little I was! Meeting Bulma was the start of an adventure I could never have imagined.","Hey, what if things had gone differently? I'd love to try some of these battles myself!"},
 {"\302\241Aqu\303\255 empez\303\263 todo! Nunca imagin\303\251 que ten\303\255a un hermano en el espacio. \302\241Raditz s\303\255 que nos hizo sudar!","\302\241Freezer nos llev\303\263 al l\303\255mite! Vamos a recordar c\303\263mo luchamos todos por proteger Namek.","Trunks vino desde el futuro para avisarnos. \302\241Hab\303\255a que entrenar mucho antes de que llegaran los androides!","\302\241Buu era dur\303\255simo! No bastaba con ser fuerte. \302\241Menos mal que ten\303\255a amigos con los que pod\303\255a contar!","\302\241Menudos rivales encontramos en estas aventuras! \302\277Qu\303\251 dices, volvemos a enfrentarnos a ellos?","\302\241Vaya viaje! \302\241Con Pan y los dem\303\241s, era imposible que las cosas estuvieran tranquilas mucho tiempo!","\302\241Mira qu\303\251 peque\303\261o era! Cuando conoc\303\255 a Bulma, no imaginaba la aventura que me esperaba.","Oye, \302\277y si todo hubiera ocurrido de otra forma? \302\241Me encantar\303\255a probar algunos de estos combates!"}};
static ImFont *story_font(){return sStoryReadableFont?sStoryReadableFont:ImGui::GetIO().Fonts->Fonts[0];}
static float story_text(ImDrawList*d,const char*t,float x,float y,float fs,ImU32 col,float width,float k,bool wrap=false){
 ImFont*f=story_font();float size=SDL_max(fs,16.f)*k;
 if(!wrap){float w=f->CalcTextSizeA(size,100000,0,t).x;if(w>width)size*=width/w;}
 int brightness=((col&255)+((col>>8)&255)+((col>>16)&255))/3;
 if(brightness>140)d->AddText(f,size,ImVec2(x+k,y+k),IM_COL32(11,18,29,225),t,nullptr,wrap?width:0);
 d->AddText(f,size,ImVec2(x,y),col,t,nullptr,wrap?width:0);
 return wrap?f->CalcTextSizeA(size,100000,width,t).y:size;
}
static void story_hints(ImDrawList*d,ImVec2 pos,float width,float k,const ModPadHint*items,int count,float fs=16){
 fs=SDL_max(fs,16.f);ImFont*f=story_font();float x=pos.x,y=pos.y;
 for(int i=0;i<count;i++){float icon=mod_pad_width(items[i].button)*k,tw=f->CalcTextSizeA(fs*k,100000,0,items[i].label).x,w=icon+tw+30*k;
  if(x>pos.x&&x+w>pos.x+width){x=pos.x;y+=(fs+9)*k;}
  mod_pad_glyph(d,items[i].button,ImVec2(x+icon*.5f,y+fs*k*.55f),k);
  story_text(d,items[i].label,x+icon+8*k,y,fs,IM_COL32(255,250,232,255),tw+2*k,k);x+=w;
 }
}
static void story_panel(ImDrawList*d,float x,float y,float w,float h,float k,bool dark){
 d->AddRectFilled(ImVec2(x,y),ImVec2(x+w,y+h),dark?IM_COL32(17,39,54,242):IM_COL32(253,239,191,250),8*k);
 d->AddRect(ImVec2(x,y),ImVec2(x+w,y+h),IM_COL32(86,31,79,255),8*k,0,4*k);
 d->AddRect(ImVec2(x+3*k,y+3*k),ImVec2(x+w-3*k,y+h-3*k),IM_COL32(251,223,132,245),6*k,0,1.5f*k);
}
static void story_guide(ImDrawList*d,int guide,float x,float y,float scale,double time,bool talking){
 guide=SDL_clamp(guide,0,8);char name[40];
 snprintf(name,sizeof(name),"guide-%d-0",guide);story_image(d,story_art(name),ImVec2(x,y),ImVec2(x+256*scale,y+512*scale));
 // The body already contains the fully open eyes. Frame1 is the half-blink,
 // not the idle face; keeping it visible made every guide look sleepy.
 double blink=SDL_fmod(time+guide*.47,4.6);
 int frames[2]={blink<4.38?-1:blink<4.43?1:blink<4.53?2:1,talking&&((int)(time*7)&1)?3:4};
 for(int p=0;p<2;p++){
  int frame=frames[p];snprintf(name,sizeof(name),"guide-%d-%d",guide,frame);
  if(frame<0)continue;
  // Original image sizes: eyes64 except Mr.Satan32; mouths32 except Mr.Satan64.
  float size=p==0?(guide==4?32:64):(guide==4?64:32);
  float px=x+storyGuidePatches[guide][p*2]*scale,py=y+storyGuidePatches[guide][p*2+1]*scale;
  story_image(d,story_art(name),ImVec2(px,py),ImVec2(px+size*scale,py+size*scale));
 }
}
static void story_scene(ImDrawList*d,ImVec2 screen,float k,double time,bool ruined=false){
 float ratio=screen.x/screen.y,span=SDL_min(1.f,ratio/3.f),u0=SDL_clamp(.65f-span*.5f,0.f,1.f-span);
 if(auto*t=story_art(ruined?"lava-world-v1":"world-v1"))d->AddImage(ImTextureRef((ImTextureID)(intptr_t)t),ImVec2(0,0),screen,ImVec2(u0,0),ImVec2(u0+span,1));
 else d->AddRectFilled(ImVec2(0,0),screen,ruined?IM_COL32(61,28,33,255):IM_COL32(66,165,207,255));
 auto xy=[&](float u,float v){return ImVec2((u-u0)/span*screen.x,v*screen.y);};
 // The cascade itself uses original moving water frames, not only a splash at its base.
 char name[32];snprintf(name,sizeof(name),"fall-%d",(int)(time*9)%5);
 if(auto*t=story_art(name))d->AddImageQuad(ImTextureRef((ImTextureID)(intptr_t)t),xy(.799f,.03f),xy(.824f,.03f),xy(.781f,.70f),xy(.730f,.70f),ImVec2(0,0),ImVec2(1,0),ImVec2(1,1),ImVec2(0,1),ruined?IM_COL32(255,105,25,145):IM_COL32(235,255,255,155));
 for(int i=0;i<2;i++){snprintf(name,sizeof(name),"cloud-%d",i);float x=screen.x*(.20f+.43f*i)+(float)SDL_sin(time*.10+i)*24*k;story_image(d,story_art(name),ImVec2(x,86*k+i*22*k),ImVec2(x+160*k,166*k+i*22*k),ruined?IM_COL32(111,65,71,95):IM_COL32(255,255,255,130));}
 ImVec2 base=xy(.76f,.73f);snprintf(name,sizeof(name),"water-%d",(int)(time*12)%16);
 story_image(d,story_art(name),ImVec2(base.x-65*k,base.y-34*k),ImVec2(base.x+65*k,base.y+40*k),ruined?IM_COL32(255,113,32,160):IM_COL32(255,255,255,130));
 for(int i=0;i<9;i++){float u=.25f+i*.047f+(float)SDL_sin(time*.5+i)*.018f,v=.69f+i*.012f;ImVec2 a=xy(u,v);d->AddLine(a,ImVec2(a.x+(32+i*3)*k,a.y),ruined?IM_COL32(255,190,51,28):IM_COL32(235,255,255,28),1.4f*k);}
 if(ruined){for(int i=0;i<16;i++){float x=(float)SDL_fmod(i*.173+time*.009,1.0)*screen.x,y=(1.f-(float)SDL_fmod(i*.137+time*(.025+i*.001),1.0))*screen.y;d->AddCircleFilled(ImVec2(x,y),(i%3+1)*.6f*k,IM_COL32(255,135,42,110),6);}return;}
 int frame=(int)(time*30)%110;float scale=.50f*k,ox=screen.x-180*k,oy=28*k;
 for(int i=0;i<7;i++){const float*q=storyBubblesFrames[frame][i];snprintf(name,sizeof(name),"bubbles-%d",(int)q[0]);if(auto*t=story_art(name))d->AddImageQuad(ImTextureRef((ImTextureID)(intptr_t)t),ImVec2(ox+q[1]*scale,oy+q[2]*scale),ImVec2(ox+q[3]*scale,oy+q[4]*scale),ImVec2(ox+q[7]*scale,oy+q[8]*scale),ImVec2(ox+q[5]*scale,oy+q[6]*scale));}
}
static const char*storyLockedSaga[2][8]={
 {"Our adventure has only just begun!", "First, let's finish the Saiyan saga. One adventure at a time!", "We still have a journey to finish before facing this chapter of our lives.", "Let's finish the android saga first. We cannot rush into every battle!", "There are other adventures waiting for us. Let's finish the last saga first!", "We still have plenty to do before this new journey begins!", "Let's finish the previous adventure before looking further back!", "A different destiny can wait. Let's finish our current adventure first!"},
 {"\302\241Nuestra aventura apenas comienza!", "Primero terminemos la saga de los Saiyajin. \302\241Una aventura a la vez!", "Todav\303\255a nos queda un viaje por terminar antes de llegar aqu\303\255.", "Terminemos primero la saga de los androides. \302\241No podemos adelantarnos a todas las peleas!", "Nos esperan otras aventuras. \302\241Terminemos primero la saga anterior!", "A\303\272n nos queda mucho por hacer antes de emprender este nuevo viaje!", "Terminemos la aventura anterior antes de mirar m\303\241s atr\303\241s!", "Otro destino puede esperar. \302\241Terminemos primero nuestra aventura!"}
};
static const char*storyLockedIdle[2][3]={{"A little training never hurts while we wait!","Let's keep going. Our current adventure is not over yet!","We will get there together. No skipping our training!"},{"Un poco de entrenamiento nunca viene mal mientras esperamos!","Sigamos adelante. Nuestra aventura todav\303\255a no ha terminado!","Llegaremos juntos. \302\241Nada de saltarse el entrenamiento!"}};
static void mod_story_page(unsigned view,int page,int cursor){
 const int chapterIndex=SDL_clamp(sStoryChapter.load(),0,ST_COUNT-1);const bool chapterOpen=(sStoryUnlocked.load()>>chapterIndex)&1u;
 const StChapter&c=stChapters[chapterIndex];int outcome=view&15,zeni=(int)(view>>8);unsigned progress=(view>>4)&15;
 char chapterTitle[2][128];for(int l=0;l<2;l++)snprintf(chapterTitle[l],sizeof(chapterTitle[l]),"%02d  %s",c.sequence,c.title[l]);
 bool es=Port_Setting("mod_language",1)==1;int L=es?1:0;bool sagas=page==MM_STORY_SAGAS,result=page==MM_STORY_RESULT;
 ImVec2 screen=ImGui::GetIO().DisplaySize;float k=screen.y/720.f,margin=28*k;double time=ImGui::GetTime();
 ImDrawList*d=ImGui::GetForegroundDrawList();ImFont*f=sModBodyFont?sModBodyFont:ImGui::GetFont();
 ImU32 gold=IM_COL32(255,218,85,255),white=IM_COL32(255,249,230,255),ink=IM_COL32(67,27,66,255),green=IM_COL32(165,255,151,255);
 auto text=[&](const char*t,float x,float y,float fs,ImU32 col,float width,bool wrap=false){return story_text(d,t,x,y,fs,col,width,k,wrap);};
 auto panel=[&](float x,float y,float w,float h,bool blue){story_panel(d,x,y,w,h,k,blue);};
 story_scene(d,screen,k,time);
 d->AddRectFilledMultiColor(ImVec2(0,0),ImVec2(screen.x,108*k),IM_COL32(31,72,88,125),IM_COL32(24,80,100,25),0,IM_COL32(24,50,70,30));
 if(es){float titleW=SDL_min(520*k,screen.x-240*k);story_image(d,story_art("history-title-es"),ImVec2(margin,9*k),ImVec2(margin+titleW,9*k+titleW/8));}
 else text("Dragon History",margin,16*k,42,IM_COL32(255,174,60,255),screen.x-240*k);
 text(sagas?(es?"Elige una saga y revive la aventura":"Choose a saga and relive the adventure"):storySagaNames[L][SDL_clamp(c.saga,0,7)],margin,72*k,18,white,screen.x-250*k);
 float leftW=screen.x*.66f-margin*1.5f,guideX=screen.x*.68f,dialogY=screen.y-146*k;
 // Restrict the host to a separate right-hand stage; never stretch its source canvas.
 float hostScale=SDL_min(1.12f*k,(screen.x-guideX-8*k)/190.f);
 int selected=sagas?SDL_clamp(cursor,0,7):SDL_clamp(c.saga,0,7);
 unsigned sagaState=sStorySagaState[selected].load();bool sagaOpen=(sagaState&2)!=0,sagaAuthored=(sagaState&1)!=0;
 int host=sagas?0:selected+1;
 const StGuideChapter*comment=STG_Chapter(c.id);
 const char*opening=sagas?(!sagaOpen?storyLockedSaga[L][selected]:!sagaAuthored?(es?"Esta aventura llegar\303\241 m\303\241s adelante. Mientras tanto, \302\241sigamos entrenando!":"This adventure will arrive later. Meanwhile, let's keep training!"):storySagaRecaps[L][selected]):STG_ChapterOpening(comment,L,result?outcome:ST_NONE);
 if(!sagas&&!result&&!chapterOpen)opening=es?"Cada encuentro guarda una sorpresa. Sigue la aventura para descubrirla.":"Every encounter holds a surprise. Continue your adventure to discover it.";
 if(!opening)opening=result&&outcome==ST_FAIL?stFailText[0][L]:c.recap[L];
 const char*const*idle=sagas&&!sagaOpen?storyLockedIdle[L]:!sagas&&chapterOpen&&comment?comment->idle[L]:stGuideIdle[host][L];
 static StGuideTalk speech={};
 int focus=(page<<20)|(host<<16)|(chapterIndex<<8)|(selected<<4)|(outcome<<1)|L;
 int activity=sagas?cursor:result?0:sStoryDifficulty.load();
 bool talking=STG_Advance(&speech,time,ImGui::GetFrameCount(),focus,activity,opening,idle)!=0;
 const char*caption=speech.line>=0?idle[speech.line]:opening;
 // A visiting saga guide speaks from the left, then leaves Goku alone.
 static unsigned welcomed[8]={};static int visitor=-1;static double visitorStart=-100;static int visitorFrame=-1;
 if(!sagas||visitorFrame+1!=ImGui::GetFrameCount()||(visitor>=0&&selected!=visitor))visitor=-1;visitorFrame=ImGui::GetFrameCount();
 unsigned welcomeKey=sStoryWelcomeRequest[selected].load();
 if(sagas&&sagaOpen&&sagaAuthored&&welcomeKey&&welcomed[selected]!=welcomeKey){welcomed[selected]=welcomeKey;visitor=selected;visitorStart=time;sStoryWelcomeAck[selected].store(welcomeKey);}
 bool visiting=sagas&&sagaOpen&&sagaAuthored&&visitor==selected&&time-visitorStart<18.0;
 int visitLine=(int)((time-visitorStart)/6.0);bool guestSpeaking=visiting&&visitLine!=1;
 const char*visitorName=ST_SpeakerName(storySagaGuides[selected],L);
 if(visiting){
  if(selected==0){
   bool later=(sStoryUnlocked.load()&2u)!=0;
   const char*lines[2][2][3]={
    {{"Goku, I met a warrior from space. My attack barely scratched him.","Someone that strong? Then we should hear what he wants!","Be careful. He is looking for you, and he knows more than he should."},
     {"Raditz has reached Kame House. This is no ordinary visit, Goku.","He knows my name... but why does he keep calling me Kakarot?","Stay close to Gohan. I do not trust that man."}},
    {{"Goku, me encontr\303\251 con un guerrero del espacio. Mi ataque apenas le hizo da\303\261o.","\302\277Tan fuerte es? \302\241Entonces tendremos que averiguar qu\303\251 busca!","Ten cuidado. Te est\303\241 buscando y sabe demasiado sobre ti."},
     {"Raditz ha llegado a Kame House. Esta no es una visita cualquiera, Goku.","Sabe mi nombre... pero \302\277por qu\303\251 insiste en llamarme Kakarot?","No pierdas de vista a Gohan. No me f\303\255o de ese hombre."}}};
   caption=lines[L][later?1:0][SDL_clamp(visitLine,0,2)];
  }else caption=visitLine==1?storySagaRecaps[L][selected]:stGuideIdle[selected+1][L][visitLine==2?1:0];
  // Later unlocks use their own authored chapter guide, never chapter2's greeting.
  if(selected!=0||welcomed[selected]>2){
   int remaining=(int)welcomed[selected],latest=ST_SagaFirst(selected);
   for(int ch=0;ch<ST_COUNT;ch++)if(stChapters[ch].saga==selected&&--remaining==0){latest=ch;break;}
   if(latest>=0){const StGuideChapter*arrival=STG_Chapter(stChapters[latest].id);
    caption=visitLine==1?(es?"Pues vamos juntos. \302\241Todav\303\255a nos quedan muchas aventuras!":"Then let's go together. We still have plenty of adventures ahead!"):
      arrival?(visitLine==0?arrival->opening[L]:arrival->idle[L][0]):stChapters[latest].recap[L];
   }
  }
  talking=!guestSpeaking;
 }

 static double farewellStart=-1;static bool farewellWasActive=false;
 bool farewell=result&&sStoryNewSaga.load()==1;
 if(farewell&&!farewellWasActive)farewellStart=time;farewellWasActive=farewell;
 int farewellLine=SDL_clamp((int)((time-farewellStart)/6.0),0,3),farewellGuide=farewellLine<2?1:2;
 if(farewell){
  static const char*lines[2][4]={
   {"Piccolo... we'll find Namek's Dragon Balls. We're bringing you and the others back!","Don't make Gohan wait forever, Goku. Take care of him until I return.","Kakarot, you have no idea who else is heading to Namek. Frieza will be waiting.","Then that's where we're going! Rest up, everyone. Our next adventure is on Namek!"},
   {"Piccoro... encontraremos las esferas de Namek. \302\241Los traeremos de vuelta a todos!","No hagas esperar tanto a Gohan, Goku. Cuida de \303\251l hasta que vuelva.","Kakarotto, no sabes qui\303\251n m\303\241s va rumbo a Namek. Freezer estar\303\241 all\303\255.","\302\241Pues hacia all\303\241 vamos! Descansen, amigos. \302\241La pr\303\263xima aventura nos espera en Namek!"}};
  caption=lines[L][farewellLine];host=0;guestSpeaking=farewellLine==1||farewellLine==2;talking=!guestSpeaking;
  if(time-farewellStart>=24)sStoryFarewellDone.store(true);
 }

 // Place the painted cutoff behind the dialogue, rather than exposing a flat crop.
 story_guide(d,host,guideX,dialogY-400*hostScale+32*k,hostScale,time,talking);
 if(sagas){
  // Three illustrated covers form a native-style carousel; only Saiyan is playable so far.
  float cy=257*k,cw=SDL_min(196*k,leftW*.34f),ch=cw,center=margin+leftW*.50f;
  for(int slot=-1;slot<=1;slot++){
   int saga=(selected+slot+8)%8;bool active=slot==0;float size=cw*(active?1.f:.72f),x=center+slot*cw*.85f-size*.5f,y=cy-size*.5f+(active?(float)SDL_sin(time*1.8)*2*k:8*k);
   unsigned state=sStorySagaState[saga].load();bool open=(state&2)!=0,authored=(state&1)!=0;
   d->AddRectFilled(ImVec2(x-6*k,y-6*k),ImVec2(x+size+6*k,y+size+6*k),active&&open?IM_COL32(255,215,86,255):IM_COL32(76,64,83,235),6*k);
   if(open&&authored){char name[32];snprintf(name,sizeof(name),"saga-%d",saga);story_image(d,story_art(name),ImVec2(x,y),ImVec2(x+size,y+size),active?IM_COL32_WHITE:IM_COL32(213,207,218,175));}
   else{
    d->AddRectFilled(ImVec2(x,y),ImVec2(x+size,y+size),IM_COL32(39,34,46,255),4*k);
    ImU32 dim=IM_COL32(143,131,151,255);float cx=x+size*.5f;
    d->AddCircle(ImVec2(cx,y+size*.39f),size*.13f,dim,24,3*k);
    d->AddRectFilled(ImVec2(cx-size*.19f,y+size*.39f),ImVec2(cx+size*.19f,y+size*.67f),dim,3*k);
    d->AddCircleFilled(ImVec2(cx,y+size*.51f),size*.025f,IM_COL32(39,34,46,255),12);
    d->AddLine(ImVec2(cx,y+size*.51f),ImVec2(cx,y+size*.58f),IM_COL32(39,34,46,255),3*k);
   }
   if(active){const char*stateText=!open?(es?"BLOQUEADA":"LOCKED"):!authored?(es?"PR\303\223XIMAMENTE":"COMING SOON"):(state&4)?(es?"COMPLETADA":"COMPLETE"):(es?"DISPONIBLE":"AVAILABLE");text(stateText,x,y+size+12*k,14,open?gold:IM_COL32(200,188,208,255),size);}
  }
  text(storySagaNames[L][selected],margin,390*k,36,sagaOpen?gold:IM_COL32(191,181,201,255),leftW);
  panel(margin,441*k,leftW,75*k,false);
  char available[128];int first=ST_SagaFirst(selected);
  if(!sagaOpen)snprintf(available,sizeof(available),es?"Próximamente. Completa %s para descubrir esta aventura.":"Coming soon. Complete %s to discover this adventure.",storySagaNames[L][selected-1]);
  else if(sagaAuthored)snprintf(available,sizeof(available),es?"%d cap\303\255tulos disponibles. La saga sigue creciendo.":"%d chapters available. More of this saga is on the way.",ST_SagaChapterCount(selected));
  else snprintf(available,sizeof(available),"%s",es?"Esta saga a\303\272n est\303\241 en preparaci\303\263n.":"This saga is still in preparation.");
  text(!sagaOpen?(es?"Una aventura por descubrir":"An adventure to discover"):sagaAuthored&&first>=0?stChapters[first].title[L]:(es?"La aventura continuar\303\241...":"The adventure will continue..."),margin+18*k,454*k,22,ink,leftW-36*k);
  text(available,margin+18*k,486*k,13,ink,leftW-36*k);

  text(es?"Tu gu\303\255a":"Your guide",guideX,dialogY-33*k,13,gold,screen.x-guideX-margin);
  text("Goku",guideX+62*k,dialogY-33*k,16,white,screen.x-guideX-margin-62*k);
 }else if(!result){
  panel(margin,119*k,leftW,445*k,false);
  float x=margin+18*k,w=leftW-36*k;
  text(chapterOpen?chapterTitle[L]:(es?"???  Un encuentro por descubrir":"???  An encounter to discover"),x,132*k,24,ink,w);
  if(ST_COUNT>1)text(es?"Arriba/abajo: cap\303\255tulo":"Up/down: chapter",x+w*.62f,138*k,13,ink,w*.38f);
  if(!chapterOpen){
   d->AddRectFilled(ImVec2(x,173*k),ImVec2(x+w,505*k),IM_COL32(45,32,54,235),12*k);
   text("?",x+w*.43f,211*k,92,IM_COL32(145,130,151,255),w*.2f);
   text(es?"UNA NUEVA HISTORIA TE ESPERA":"A NEW STORY AWAITS",x+24*k,350*k,24,IM_COL32(173,156,181,255),w-48*k);
   text(es?"Completa el encuentro anterior para descubrir este capitulo.":"Complete the previous encounter to reveal this chapter.",x+24*k,410*k,18,white,w-48*k,true);
  }else{
  float shotW=w*.32f,shotH=73*k;
  story_image(d,ms_art(c.stage,true),ImVec2(x,174*k),ImVec2(x+shotW,174*k+shotH));
  text(c.recap[L],x+shotW+12*k,174*k,15,ink,w-shotW-12*k,true);
  const StCast&cast=stCasts[chapterIndex];int slot=0,shown[ST_ACTORS],counts[ST_ACTORS],items=0;
  for(int n=0;n<cast.count;n++){
   const StActor&a=cast.actor[n];if((a.flags&ST_ACTOR_SCENE)&&!(a.flags&ST_ACTOR_RESERVE))continue;
   int prev=-1;for(int j=0;j<items;j++)if(cast.actor[shown[j]].chara==a.chara&&cast.actor[shown[j]].team==a.team&&chapterIndex!=5)prev=j;
   if(prev>=0){counts[prev]++;continue;}shown[items]=n;counts[items++]=1;
  }
  int columns=items>6?4:items>2?3:2;float cardW=w/columns,side=34*k;
  for(int j=0;j<items;j++){
   int n=shown[j];const StActor&a=cast.actor[n];float px=x+(slot%columns)*cardW,py=260*k+(slot/columns)*51*k;slot++;
   story_image(d,ms_art(a.chara,false),ImVec2(px,py),ImVec2(px+side,py+side));
   char label[112];snprintf(label,sizeof(label),"%s%s",Port_CharaName(a.chara),counts[j]>1?"":"");char*cut=strchr(label,'[');if(cut)*cut=0;
   if(chapterIndex==5&&n>=5){char clean[96];snprintf(clean,sizeof(clean),"%s",label);snprintf(label,sizeof(label),es?"Doble de %s":"%s's double",clean);}
   text(label,px+side+5*k,py,11,ink,cardW-side-8*k);
   int power=stChapterPower[chapterIndex][n];
   if(power>0)snprintf(label,sizeof(label),es?"Poder: %d":"Power: %d",power);
   else snprintf(label,sizeof(label),"%s",es?"Poder variable":"Variable power");
   text(label,px+side+5*k,py+18*k,11,ink,cardW-side-8*k);
   if(counts[j]>1){snprintf(label,sizeof(label),"×%d",counts[j]);text(label,px+side-4*k,py+side-7*k,12,ink,28*k);}
  }
  const char*rule=c.objective[L];
  text(rule,x,417*k,15,ink,w,true);
  text(sStoryDensity.load()?(es?"Batallas completas":"Full battles"):(es?"Batallas reducidas":"Reduced battles"),x,444*k,13,ink,w);
  text(es?"Dificultad":"Difficulty",x,467*k,14,ink,w);
  const char*levels[2][3]={{"Easy","Normal","Hard"},{"F\303\241cil","Normal","Dif\303\255cil"}};
  static SDL_GPUTexture*plaque=nullptr;static bool tried=false;if(!tried){tried=true;plaque=mod_menu_art("menu-hd/story-difficulty-plaque-v1");}
  float cw=(w-14*k)/3,hh=SDL_min(37*k,cw/3);int diff=SDL_clamp(sStoryDifficulty.load(),0,2);
  int rank=SDL_clamp(sStoryClearRank.load(),0,3);
  for(int n=0;n<3;n++){
   float px=x+n*(cw+7*k);bool cleared=rank>n;
   story_image(d,plaque,ImVec2(px,485*k),ImVec2(px+cw,485*k+hh),n==diff?IM_COL32_WHITE:IM_COL32(157,142,170,180));
   text(levels[L][n],px+cw*.12f,485*k+hh*.32f,17,n==diff?ink:IM_COL32(80,49,92,255),cw*.49f);
   // Reuse our DBZ trophy art at its native aspect; cleared ranks remain lit
   // even when another difficulty is selected. Text makes the state unambiguous.
   float medal=SDL_min(31*k,hh*.78f),mx=px+cw*.79f,my=485*k+(hh-medal)*.5f;
   story_image(d,mod_reward_art(MA_SILVER+n+2),ImVec2(mx-medal*.5f,my),ImVec2(mx+medal*.5f,my+medal),cleared?IM_COL32_WHITE:IM_COL32(80,72,85,100));
   text(cleared?(es?"Superado":"Cleared"):(es?"Pendiente":"Pending"),px+cw*.1f,488*k+hh,13,cleared?IM_COL32(32,110,44,255):ink,cw*.8f);
   if(n==diff)d->AddLine(ImVec2(px+cw*.15f,485*k+hh),ImVec2(px+cw*.85f,485*k+hh),IM_COL32(189,72,33,255),2*k);
  }
  const char*record=rank==3?(es?"R\303\251cord: Dif\303\255cil. Las tres dificultades superadas.":"Best clear: Hard. All three difficulties cleared."):rank==2?(es?"R\303\251cord: Normal. F\303\241cil y Normal superados.":"Best clear: Normal. Easy and Normal cleared."):rank==1?(es?"R\303\251cord: F\303\241cil.":"Best clear: Easy."):progress?(es?"Historia completada. Gana el combate para obtener un trofeo.":"Story passed. Win the fight to earn a trophy."):(es?"Sin completar":"Not cleared");
  if(progress){d->AddLine(ImVec2(x,552*k),ImVec2(x+5*k,557*k),IM_COL32(42,121,55,255),2*k);d->AddLine(ImVec2(x+5*k,557*k),ImVec2(x+15*k,544*k),IM_COL32(42,121,55,255),2*k);}
  text(record,x+(progress?22*k:0),545*k,13,rank?IM_COL32(42,121,55,255):ink,w);
  }
 }else{
  bool clear=outcome==ST_CANON||outcome==ST_ALT;
  panel(margin,132*k,leftW,420*k,false);float x=margin+22*k,w=leftW-44*k;
  if(clear){d->AddLine(ImVec2(x+w-28*k,160*k),ImVec2(x+w-18*k,171*k),IM_COL32(42,121,55,255),3*k);d->AddLine(ImVec2(x+w-18*k,171*k),ImVec2(x+w,145*k),IM_COL32(42,121,55,255),3*k);}
  text(clear?(es?"CAP\303\215TULO COMPLETADO":"CHAPTER CLEAR"):(es?"CAP\303\215TULO FALLIDO":"CHAPTER FAILED"),x,153*k,36,clear?IM_COL32(255,171,36,255):IM_COL32(222,73,54,255),w);
  text(c.title[L],x,205*k,22,ink,w);
  text(outcome==ST_ALT?(es?"El destino puede cambiar":"Destiny can change"):outcome==ST_CANON?(es?"La historia contin\303\272a...":"History continues..."):stFailText[0][L],x,246*k,19,ink,w,true);
  if(clear&&sStoryClearRank.load()>0){int rank=MA_SILVER+sStoryClearRank.load()-1;story_image(d,mod_reward_art(rank+2),ImVec2(x+w-85*k,282*k),ImVec2(x+w,367*k));}
  int ball=sStoryGrantedBall.load(),next=sStoryNewChapter.load();char label[256];
  if(ball>=0&&ball<7)snprintf(label,sizeof(label),es?(ball==0?"\302\241Esfera de %d estrella obtenida!":"\302\241Esfera de %d estrellas obtenida!"):"%d-star Dragon Ball obtained!",ball+1);
  else snprintf(label,sizeof(label),"%s",es?"Esfera del drag\303\263n: no obtenida":"Dragon Ball: not obtained");
  text(label,x,285*k,18,ball>=0?IM_COL32(147,83,12,255):ink,w-95*k,true);
  if(sStoryNewSaga.load()==1)snprintf(label,sizeof(label),"%s",es?"Nueva saga desbloqueada: Saga de Freezer (pr\303\263ximamente)":"New saga unlocked: Frieza Saga (coming soon)");
  else if(next>=0&&next<ST_COUNT)snprintf(label,sizeof(label),es?"Nuevo cap\303\255tulo: %02d  %s":"New chapter unlocked: %02d  %s",stChapters[next].sequence,stChapters[next].title[L]);
  else snprintf(label,sizeof(label),"%s",clear&&chapterIndex==ST_COUNT-1&&!ST_CAMPAIGN_FINALIZED?(es?"La aventura continuar\303\241...":"The adventure will continue..."):(es?"Sin nuevos cap\303\255tulos desbloqueados":"No new chapters unlocked"));
  text(label,x,322*k,18,next>=0?IM_COL32(36,112,51,255):ink,w-95*k,true);
  snprintf(label,sizeof(label),es?"Zeni obtenidos   +%d":"Zeni earned   +%d",zeni);text(label,x,379*k,32,ink,w);
  snprintf(label,sizeof(label),es?"Saldo anterior: %d":"Previous balance: %d",sStoryBalance[0].load());text(label,x,425*k,17,ink,w);
  snprintf(label,sizeof(label),es?"Saldo actual: %d":"Current balance: %d",sStoryBalance[1].load());text(label,x,454*k,26,ink,w);
  if(!clear&&sStoryCheckpoint.load())text(es?"Punto de control disponible durante esta sesión":"Checkpoint available for this session",x,479*k,14,ink,w);
  text(clear?(sStoryClearRank.load()>0?(es?"Victoria registrada en esta dificultad y las inferiores":"Victory recorded for this difficulty and below"):(es?"Historia completada: sin trofeo de victoria":"Story passed: no victory trophy")):(es?"Vuelve a intentarlo":"Try again"),x,505*k,15,ink,w);

 }
 if(farewell){
  double phase=time-farewellStart;float alpha=phase<12?SDL_min(1.f,(float)(12-phase)*2):SDL_min(1.f,(float)(phase-12)*2);
  story_guide(d,farewellGuide,margin-(1-SDL_max(0.f,alpha))*230*k,dialogY-400*hostScale+32*k,hostScale,time,guestSpeaking);
 }
 if(visiting){
  float slide=SDL_min(1.f,(float)(time-visitorStart)*3.f)*SDL_min(1.f,(float)(18.0-(time-visitorStart))*3.f);
  story_guide(d,selected+1,margin-(1-slide)*220*k,dialogY-400*hostScale+32*k,hostScale,time,guestSpeaking);
 }
 // A shared story dialogue plate replaces the old full-screen dark settings panels.
 panel(margin,dialogY,screen.x-margin*2,91*k,true);
 text(farewell?(guestSpeaking?ST_SpeakerName(storySagaGuides[farewellGuide-1],L):"Goku"):visiting?(guestSpeaking?visitorName:"Goku"):sagas?"Goku":ST_SpeakerName(storySagaGuides[selected],L),margin+18*k,dialogY+9*k,14,gold,screen.x-margin*2-36*k);
 text(caption,margin+18*k,dialogY+34*k,19,white,screen.x-margin*2-36*k,true);
 const ModPadHint browse[]={{MP_LR,es?"Saga":"Saga"},{MP_CROSS,es?"Cap\303\255tulos":"Chapters"},{MP_TRIANGLE,es?"Volver":"Back"}};
 const ModPadHint chapter[]={{MP_LR,es?"Dificultad":"Difficulty"},{MP_CROSS,es?"Comenzar":"Start chapter"},{MP_TRIANGLE,es?"Sagas":"Sagas"},{MP_SQUARE,es?"Completa/reducida (guardado)":"Full/reduced (saved)"}};
 const ModPadHint ending[]={{MP_CROSS,es?"Volver a Historia Drag\303\263n":"Return to Dragon History"},{MP_SQUARE,es?"Reintentar punto de control":"Retry checkpoint"},{MP_START,es?"Reiniciar cap\303\255tulo":"Restart chapter"}};
 story_hints(d,ImVec2(margin,screen.y-35*k),screen.x-margin*2,k,result?ending:sagas?browse:chapter,result?(sStoryCheckpoint.load()?3:1):sagas?3:4,14);
}
