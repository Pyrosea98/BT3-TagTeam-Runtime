// Chapter presentation uses the same pure preset builder as the actual fight.
static SDL_GPUTexture *ms_art(int,bool);
static void ms_random_icon(ImDrawList*,ImVec2,float);
static void mod_scenario_scene(int page,int cursor,int family,int variant){
 bool es=Port_Setting("mod_language",1)==1,chapters=page==MM_SCENARIOS;
 if(chapters)family=cursor;if(family<0||family>=SC_FAMILIES)return;
 int first=SC_First(family),count=SC_Count(family),route=first+(chapters?0:cursor);
 if(route<first||route>=first+count)return;
 unsigned earned=Port_AchievementMask();ImVec2 screen=ImGui::GetIO().DisplaySize;
 float k=screen.y/720.f,pad=20*k,top=104*k,bottom=screen.y-72*k,gap=16*k,leftW=(screen.x-pad*2-gap)*.43f,rightX=pad+leftW+gap,rightW=screen.x-rightX-pad;
 ImDrawList*d=ImGui::GetForegroundDrawList();ImFont*f=sModBodyFont?sModBodyFont:ImGui::GetFont();
 const ImU32 gold=IM_COL32(255,205,83,255),white=IM_COL32(249,239,229,255),muted=IM_COL32(199,180,201,255),green=IM_COL32(122,233,164,255);
 auto text=[&](const char*t,float x,float y,float fs,ImU32 col,float width,bool wrap=false){return story_text(d,t,x,y,fs,col,width,k,wrap);};
 auto name=[&](int id,char*out,size_t size){const char*n=id<0?(es?"Tu elección":"Your choice"):Port_CharaName(id);snprintf(out,size,"%s",n?n:"");char*p=strchr(out,'[');if(!p)p=strchr(out,',');if(p)*p=0;};
 static const int bosses[]={105,129,114,255,146,291};
 auto stageForRoute=[](int r){char key[40];snprintf(key,sizeof(key),"scenario_stage_%d",r);return Port_Setting(key,SC_DefaultStage(r));};
 story_scene(d,screen,k,ImGui::GetTime(),true);
 d->AddRectFilled(ImVec2(0,0),ImVec2(screen.x,86*k),IM_COL32(19,12,24,205));
 text(es?"ESCENARIOS":"SCENARIOS",pad,17*k,32,gold,screen.x-pad*2);
 text(chapters?(es?"Elige un escenario y descubre sus desafíos.":"Choose a scenario and discover its challenges."):scFamilies[es][family],pad,57*k,18,white,screen.x-pad*2);
 d->AddLine(ImVec2(pad,85*k),ImVec2(screen.x-pad,85*k),gold,1.4f*k);
 if(chapters){
  float cw=leftW*.48f,ch=cw*.75f,center=pad+leftW*.5f,cy=top+155*k;
  for(int pos=-1;pos<=1;pos++){
   int n=(family+pos+SC_FAMILIES)%SC_FAMILIES,r=SC_First(n);bool selected=pos==0;
   float w=cw*(selected?1.f:.68f),h=ch*(selected?1.f:.68f),x=center+pos*cw*.80f-w*.5f,y=cy-h*.5f;
   d->AddRectFilled(ImVec2(x-4*k,y-4*k),ImVec2(x+w+4*k,y+h+4*k),selected?gold:IM_COL32(77,46,64,245),5*k);
   story_image(d,ms_art(stageForRoute(r),true),ImVec2(x,y),ImVec2(x+w,y+h),selected?IM_COL32_WHITE:IM_COL32(180,155,169,190));
   float side=SDL_min(h*.45f,62*k);story_image(d,ms_art(bosses[n],false),ImVec2(x+w-side-5*k,y+5*k),ImVec2(x+w-5*k,y+side+5*k));
  }
  text(scFamilies[es][family],pad,top+272*k,26,gold,leftW);
  int done=0;for(int j=0;j<count;j++)done+=!!(earned&(1u<<scRoutes[first+j].achievement));
  story_panel(d,pad,top+321*k,leftW,150*k,k,true);
  char status[96];snprintf(status,sizeof(status),es?"Trofeos: %d / %d":"Trophies: %d / %d",done,count);
  text(status,pad+14*k,top+337*k,20,done?green:gold,leftW-28*k);
  text(es?"Cada desafío ofrece una forma distinta de vivir este combate. Revisa sus condiciones antes de empezar.":"Each challenge offers a different way to experience this battle. Check its requirements before starting.",pad+14*k,top+375*k,18,white,leftW-28*k,true);
 }else{
  text(es?"ELIGE TU DESAFÍO":"CHOOSE YOUR CHALLENGE",pad,top,16,gold,leftW);
  float rowH=(bottom-top-30*k-gap*(count-1))/count;
  for(int n=0;n<count;n++){
   int id=first+n,bit=scRoutes[id].achievement,rank=scRoutes[id].rank;float y=top+30*k+n*(rowH+gap);bool done=(earned&(1u<<bit))!=0,selected=n==cursor;
   d->AddRectFilled(ImVec2(pad,y),ImVec2(pad+leftW,y+rowH),selected?IM_COL32(99,51,40,242):IM_COL32(22,33,44,240),6*k);
   d->AddRect(ImVec2(pad,y),ImVec2(pad+leftW,y+rowH),selected?gold:IM_COL32(131,76,112,160),6*k,0,(selected?2:1)*k);
   float icon=SDL_min(48*k,rowH*.48f);if(auto*t=achievement_trophy(bit))d->AddImage(ImTextureRef((ImTextureID)(intptr_t)t),ImVec2(pad+8*k,y+10*k),ImVec2(pad+8*k+icon,y+10*k+icon));
   text(achievement_rank_name(rank,es),pad+icon+18*k,y+14*k,14,ImGui::ColorConvertFloat4ToU32(achievement_rank_colour(rank)),leftW-icon-32*k);
   text(scRoutes[id].name[es],pad+icon+18*k,y+36*k,16,selected?gold:white,leftW-icon-32*k);
   text(done?(es?"CONSEGUIDO":"EARNED"):(es?"POR CONSEGUIR":"TO EARN"),pad+12*k,y+rowH-23*k,12,done?green:muted,leftW-24*k);
  }
 }
 story_panel(d,rightX,top,rightW,bottom-top,k,true);
 float inset=14*k,rx=rightX+inset,rw=rightW-inset*2;
 text(chapters?scFamilies[es][family]:scRoutes[route].name[es],rx,top+13*k,22,gold,rw);
 const char*brief=scRoutes[route].brief[es];
 float briefH=text(brief,rx,top+46*k,18,white,rw,true);
 float rosterStart=SDL_max(top+174*k,top+46*k+briefH+38*k);
 MsView preview={};unsigned weak=0;SC_BuildDraft(&preview,route,variant,&weak);
 if(!chapters&&(route==14||route==16)){const ModPadHint h[]={{MP_LR,es?"Elegir protagonista":"Choose protagonist"}};story_hints(d,ImVec2(rx,rosterStart-29*k),rw,k,h,1,12);}
 auto roster=[&](int team,float y,float room){
  text(team==0?(es?"TU EQUIPO":"YOUR TEAM"):preview.team_count>2?(team==1?"CELL":(es?"ANDROIDES - OTRO BANDO":"ANDROIDS - ANOTHER TEAM")):(es?"RIVALES":"OPPONENTS"),rx,y,15,team==0?IM_COL32(128,201,255,255):IM_COL32(255,155,129,255),rw);
  int slots[10],n=0;for(int i=0;i<preview.total;i++)if(preview.pick[i].team==team)slots[n++]=i;int cols=n>5?4:n;int rows=(n+SDL_max(1,cols)-1)/SDL_max(1,cols);float cw=rw/SDL_max(1,cols),rowH=(room-25*k)/SDL_max(1,rows);
  for(int j=0;j<n;j++){
   int slot=slots[j],cid=preview.pick[slot].chara;float x=rx+(j%cols)*cw,yy=y+25*k+(j/cols)*rowH,side=SDL_max(20*k,SDL_min(52*k,SDL_min(cw-10*k,rowH-25*k)));char label[180];name(cid,label,sizeof(label));
   d->AddRectFilled(ImVec2(x+3*k,yy),ImVec2(x+cw-3*k,yy+side+25*k),IM_COL32(86,48,81,110),4*k);
   if(cid<0)ms_random_icon(d,ImVec2(x+cw*.5f,yy+side*.5f),side*.44f);
   else if(auto*t=ms_art(cid,false))d->AddImage(ImTextureRef((ImTextureID)(intptr_t)t),ImVec2(x+(cw-side)*.5f,yy),ImVec2(x+(cw+side)*.5f,yy+side));
   text(label,x+5*k,yy+side+3*k,12,white,cw-10*k);

  }
 };
 if(preview.team_count>2)text(es?"Tres bandos. Tu objetivo es derrotar a Cell.":"Three teams. Defeat Cell to win.",rx,rosterStart-26*k,16,gold,rw);
 float room=bottom-rosterStart-6*k;
 if(preview.team_count>2){float each=room/3;roster(0,rosterStart,each);roster(1,rosterStart+each,each);roster(2,rosterStart+each*2,each);}
 else{int rivals=0;for(int i=0;i<preview.total;i++)rivals+=preview.pick[i].team==1;float ours=room*(rivals>4?.35f:.5f);roster(0,rosterStart,ours);roster(1,rosterStart+ours,room-ours);}
 d->AddRectFilled(ImVec2(0,screen.y-56*k),screen,IM_COL32(21,9,28,245));d->AddLine(ImVec2(pad,screen.y-56*k),ImVec2(screen.x-pad,screen.y-56*k),gold,k);
 const ModPadHint hints[]={{chapters?MP_LR:MP_DPAD,es?"Elegir":"Choose"},{MP_CROSS,chapters?(es?"Ver desafíos":"View challenges"):(es?"Continuar":"Continue")},{MP_TRIANGLE,es?"Volver":"Back"},{MP_SELECT,es?"Cerrar":"Close"}};
 story_hints(d,ImVec2(pad,screen.y-39*k),screen.x-pad*2,k,hints,4,14);
}
