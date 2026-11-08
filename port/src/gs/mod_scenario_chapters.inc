// Chapter presentation uses the same pure preset builder as the actual fight.
static SDL_GPUTexture *ms_art(int,bool);
static void ms_random_icon(ImDrawList*,ImVec2,float);
static void mod_scenario_chapters(int page,int cursor,int family,int variant){
 bool es=Port_Setting("mod_language",1)==1,chapters=page==MM_SCENARIOS;
 if(chapters)family=cursor;if(family<0||family>=SC_FAMILIES)return;
 int first=SC_First(family),count=SC_Count(family),route=first+(chapters?0:cursor);
 if(route<first||route>=first+count)return;
 unsigned earned=Port_AchievementMask();ImVec2 screen=ImGui::GetIO().DisplaySize;
 float k=screen.y/720.f,pad=20*k,top=104*k,bottom=screen.y-72*k,gap=16*k,leftW=(screen.x-pad*2-gap)*.43f,rightX=pad+leftW+gap,rightW=screen.x-rightX-pad;
 ImDrawList*d=ImGui::GetForegroundDrawList();ImFont*f=sModBodyFont?sModBodyFont:ImGui::GetFont();
 const ImU32 gold=IM_COL32(255,205,83,255),white=IM_COL32(249,239,229,255),muted=IM_COL32(199,180,201,255),green=IM_COL32(122,233,164,255);
 auto text=[&](const char*t,float x,float y,float fs,ImU32 col,float width,bool wrap=false){
  fs*=k;if(!wrap){float w=f->CalcTextSizeA(fs,10000,0,t).x;if(w>width)fs*=width/w;}
  d->AddText(f,fs,ImVec2(x+k,y+k),IM_COL32(0,0,0,220),t,nullptr,wrap?width:0);
  d->AddText(f,fs,ImVec2(x,y),col,t,nullptr,wrap?width:0);
 };
 auto name=[&](int id,char*out,size_t size){const char*n=id<0?(es?"Tu elección":"Your choice"):Port_CharaName(id);snprintf(out,size,"%s",n?n:"");char*p=strchr(out,'[');if(!p)p=strchr(out,',');if(p)*p=0;};
 static const int bosses[]={105,129,114,255,146,291};
 auto stageForRoute=[](int r){char key[40];snprintf(key,sizeof(key),"scenario_stage_%d",r);return Port_Setting(key,SC_DefaultStage(r));};
 if(auto*t=ms_art(stageForRoute(route),true))d->AddImage(ImTextureRef((ImTextureID)(intptr_t)t),ImVec2(0,0),screen);
 d->AddRectFilledMultiColor(ImVec2(0,0),screen,IM_COL32(29,10,39,245),IM_COL32(42,17,42,220),IM_COL32(12,14,30,245),IM_COL32(22,10,36,250));
 d->AddRectFilled(ImVec2(0,0),ImVec2(screen.x,86*k),IM_COL32(25,10,30,235));
 if(auto*t=mod_brand_texture())d->AddImage(ImTextureRef((ImTextureID)(intptr_t)t),ImVec2(pad,9*k),ImVec2(pad+160*k,66*k));
 text(es?"ESCENARIOS":"SCENARIOS",pad+180*k,17*k,27,gold,screen.x-pad*2-180*k);
 text(chapters?(es?"Elige una historia. Cada victoria tiene su recompensa.":"Choose a story. Every victory has its reward."):scFamilies[es][family],pad+180*k,53*k,16,white,screen.x-pad*2-180*k);
 d->AddLine(ImVec2(pad,85*k),ImVec2(screen.x-pad,85*k),gold,1.4f*k);
 ms_kit_sprite(d,15,ImVec2(screen.x-95*k,12*k),ImVec2(74*k,65*k),IM_COL32(255,255,255,170));
 if(chapters){
  float cw=(leftW-gap)*.5f,ch=(bottom-top-gap*2)/3;
  for(int n=0;n<SC_FAMILIES;n++){
   float x=pad+(n%2)*(cw+gap),y=top+(n/2)*(ch+gap);bool selected=n==cursor;int a=SC_First(n),nn=SC_Count(n),done=0;
   for(int j=0;j<nn;j++)if(earned&(1u<<scRoutes[a+j].achievement))done++;
   d->AddRectFilled(ImVec2(x,y),ImVec2(x+cw,y+ch),IM_COL32(55,25,59,240),7*k);
   if(auto*t=ms_art(stageForRoute(a),true))d->AddImage(ImTextureRef((ImTextureID)(intptr_t)t),ImVec2(x+2*k,y+2*k),ImVec2(x+cw-2*k,y+ch*.61f),ImVec2(0,0),ImVec2(1,1),IM_COL32(255,255,255,150));
   d->AddRectFilledMultiColor(ImVec2(x,y),ImVec2(x+cw,y+ch*.65f),IM_COL32(30,10,30,20),IM_COL32(30,10,30,20),IM_COL32(30,10,30,200),IM_COL32(30,10,30,200));
   if(auto*t=ms_art(bosses[n],false)){float side=ch*.44f;d->AddImage(ImTextureRef((ImTextureID)(intptr_t)t),ImVec2(x+cw-side-6*k,y+6*k),ImVec2(x+cw-6*k,y+side+6*k));}
   char chapter[32];snprintf(chapter,sizeof(chapter),es?"CAPÍTULO %02d":"CHAPTER %02d",n+1);text(chapter,x+8*k,y+9*k,11,white,cw-16*k);
   text(scFamilies[es][n],x+9*k,y+ch*.66f,15,selected?gold:white,cw-18*k);
   char status[64];snprintf(status,sizeof(status),es?"Trofeos %d / %d":"Trophies %d / %d",done,nn);text(status,x+9*k,y+ch-23*k,12,done?green:muted,cw-18*k);
   d->AddRect(ImVec2(x,y),ImVec2(x+cw,y+ch),selected?gold:IM_COL32(140,87,117,150),7*k,0,(selected?2.2f:1.f)*k);
   if(selected)ms_kit_sprite(d,13,ImVec2(x+cw-58*k,y+ch-26*k),ImVec2(62*k,33*k));
  }
 }else{
  text(es?"ELIGE TU DESAFÍO":"CHOOSE YOUR CHALLENGE",pad,top,16,gold,leftW);
  float rowH=(bottom-top-30*k-gap*(count-1))/count;
  for(int n=0;n<count;n++){
   int id=first+n,bit=scRoutes[id].achievement,rank=scRoutes[id].rank;float y=top+30*k+n*(rowH+gap);bool done=(earned&(1u<<bit))!=0,selected=n==cursor;
   d->AddRectFilled(ImVec2(pad,y),ImVec2(pad+leftW,y+rowH),selected?IM_COL32(116,59,54,235):IM_COL32(49,23,54,230),6*k);
   d->AddRect(ImVec2(pad,y),ImVec2(pad+leftW,y+rowH),selected?gold:IM_COL32(131,76,112,160),6*k,0,(selected?2:1)*k);
   float icon=SDL_min(48*k,rowH*.48f);if(auto*t=achievement_trophy(bit))d->AddImage(ImTextureRef((ImTextureID)(intptr_t)t),ImVec2(pad+8*k,y+10*k),ImVec2(pad+8*k+icon,y+10*k+icon));
   text(achievement_rank_name(rank,es),pad+icon+18*k,y+14*k,14,ImGui::ColorConvertFloat4ToU32(achievement_rank_colour(rank)),leftW-icon-32*k);
   text(scRoutes[id].name[es],pad+icon+18*k,y+36*k,16,selected?gold:white,leftW-icon-32*k);
   text(done?(es?"CONSEGUIDO":"EARNED"):(es?"POR CONSEGUIR":"TO EARN"),pad+12*k,y+rowH-23*k,12,done?green:muted,leftW-24*k);
  }
 }
 d->AddRectFilled(ImVec2(rightX,top),ImVec2(screen.x-pad,bottom),IM_COL32(25,13,34,230),8*k);
 d->AddRect(ImVec2(rightX,top),ImVec2(screen.x-pad,bottom),IM_COL32(161,103,97,180),8*k,0,k);
 float inset=14*k,rx=rightX+inset,rw=rightW-inset*2;
 text(chapters?scFamilies[es][family]:scRoutes[route].name[es],rx,top+13*k,22,gold,rw);
 const char*brief=scRoutes[route].brief[es];
 text(brief,rx,top+46*k,14,white,rw,true);
 MsView preview={};unsigned weak=0;SC_BuildDraft(&preview,route,variant,&weak);
 if(!chapters&&(route==14||route==16)){const ModPadHint h[]={{MP_LR,es?"Elegir protagonista":"Choose protagonist"}};mod_pad_hints(d,ImVec2(rx,top+135*k),rw,k,h,1,12);}
 auto roster=[&](int team,float y){
  text(team==0?(es?"TU EQUIPO":"YOUR TEAM"):preview.team_count>2?(team==1?"CELL":(es?"ANDROIDES - OTRO BANDO":"ANDROIDS - ANOTHER TEAM")):(es?"RIVALES":"OPPONENTS"),rx,y,15,team==0?IM_COL32(128,201,255,255):IM_COL32(255,155,129,255),rw);
  int slots[10],n=0;for(int i=0;i<preview.total;i++)if(preview.pick[i].team==team)slots[n++]=i;int cols=n>5?4:n;float cw=rw/SDL_max(1,cols),rowH=85*k;
  for(int j=0;j<n;j++){
   int slot=slots[j],cid=preview.pick[slot].chara;float x=rx+(j%cols)*cw,yy=y+25*k+(j/cols)*rowH,side=SDL_min(52*k,cw-10*k);char label[180];name(cid,label,sizeof(label));
   d->AddRectFilled(ImVec2(x+3*k,yy),ImVec2(x+cw-3*k,yy+side+25*k),IM_COL32(86,48,81,110),4*k);
   if(cid<0)ms_random_icon(d,ImVec2(x+cw*.5f,yy+side*.5f),side*.44f);
   else if(auto*t=ms_art(cid,false))d->AddImage(ImTextureRef((ImTextureID)(intptr_t)t),ImVec2(x+(cw-side)*.5f,yy),ImVec2(x+(cw+side)*.5f,yy+side));
   text(label,x+5*k,yy+side+3*k,12,white,cw-10*k);
   if(slot==0)text(es?"TÚ":"YOU",x+4*k,yy-12*k,10,gold,cw-8*k);
   else if(weak&(1u<<slot))text(es?"Sin fuerzas":"Exhausted",x+4*k,yy-12*k,10,muted,cw-8*k);
  }
 };
 if(preview.team_count>2){text(es?"Tres bandos. Tu objetivo es derrotar a Cell.":"Three teams. Your objective is to defeat Cell.",rx,top+135*k,12,gold,rw);roster(0,top+174*k);roster(1,top+290*k);roster(2,top+406*k);}else{roster(0,top+174*k);roster(1,top+315*k);}
 d->AddRectFilled(ImVec2(0,screen.y-56*k),screen,IM_COL32(21,9,28,245));d->AddLine(ImVec2(pad,screen.y-56*k),ImVec2(screen.x-pad,screen.y-56*k),gold,k);
 const ModPadHint hints[]={{MP_DPAD,es?"Elegir":"Choose"},{MP_CROSS,chapters?(es?"Ver desafíos":"View challenges"):(es?"Continuar":"Continue")},{MP_TRIANGLE,es?"Volver":"Back"},{MP_SELECT,es?"Cerrar":"Close"}};
 mod_pad_hints(d,ImVec2(pad,screen.y-39*k),screen.x-pad*2,k,hints,4,14);
}
