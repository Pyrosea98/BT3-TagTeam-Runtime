    bool visible() {
        frameState=uiStore().lifecycle.snapshot(uiNow());
        {auto& store=hudStore();std::lock_guard lock(store.mutex);hud=store.state;}
        // A successful presentation remains acknowledged throughout the match.
        // Clearing it at every frame start races guest text submission.
        if(hud.generation!=frameState.generation || (!hud.active && !hud.introStage))
            hudStore().presentedGeneration.store(0,std::memory_order_release);
        const auto now=uiNow();
        if(hud.introStage && hud.generation==frameState.generation)return true;
        if(creditsEnabled) {
            if(frameState.screen!=Screen::Hidden)creditsEnabled=false;
            else {
                if(!creditsBorn)creditsBorn=now;
                const uint64_t duration=creditsFull?4000:1500;
                if(uiStore().creditsSkip.load() && !creditsFade)creditsFade=now;
                const bool ending=(creditsFade && now-creditsFade>=334) || now-creditsBorn>=duration;
                uiStore().creditsActive.store(!ending);
                if(!ending)return true;
                creditsEnabled=false;
            }
            uiStore().creditsActive.store(false);
        }
        if((frameState.screen==Screen::Settings || frameState.screen==Screen::About) &&
           now>=frameState.heartbeatMs && now-frameState.heartbeatMs>5000) {
            uiStore().lifecycle.close(now);return false;
        }
        if(frameState.generation!=lastGeneration) {
            lastGeneration=frameState.generation;born=now;fadeOut=0;smoothProgress=progressFrom=progressTarget=0;progressBorn=now;lastStage=0;stageBorn=now;
        }
        // A verified native fight snapshot also covers a delayed controller ACK.
        if(hud.active && hud.generation==frameState.generation &&
           (frameState.phase==PreparationPhase::Ready || frameState.phase==PreparationPhase::Released)) {
            if(!fadeOut)fadeOut=now;
            return true;
        }
        if(frameState.screen==Screen::Loading) {
            if(frameState.startAccepted && now-born>=400) {
                if(!fadeOut)fadeOut=now;
                return now-fadeOut<334;
            }
            fadeOut=0;
            // Twelve 30 Hz logic frames: a quick preparation never flashes.
            return frameState.phase==PreparationPhase::Failed || now-born>=400;
        }
        if(frameState.screen==Screen::Settings || frameState.screen==Screen::About)return true;
        if(frameState.phase==PreparationPhase::Released && now-born>=400) {
            if(!fadeOut)fadeOut=now;
            return now-fadeOut<334 || (hud.active && hud.generation==frameState.generation);
        }
        return false;
    }
    Rect logical(Rect viewport,float x,float y,float width,float height) {
        float f=std::min(viewport.width/640,viewport.height/448);
        return {viewport.x+(hudEdges?0:(viewport.width-640*f)/2)+x*f,
                viewport.y+(hudEdges?0:(viewport.height-448*f)/2)+y*f,width*f,height*f};
    }
    void image(Art art,Rect vp,float x,float y,float width,float height,float alpha=1,float brightness=1,
               float u0=0,float v0=0,float u1=1,float v1=1) {
        sink.begin(vp);sink.rgba(art.handle,logical(vp,x,y,width,height),alpha,brightness,u0,v0,u1,v1);sink.end();
    }
    void text(NativeTextFont& font,std::string_view value,Palette colour,float x,float y,Rect vp,
              float maxWidth=600,TextAlign align=TextAlign::Left,float textScale=1) {
        std::array<char,1024> label{};
        if(font.measure(value)*textScale>maxWidth) {
            ++ellipsized;
            size_t n=std::min(value.size(),label.size()-4);
            while(n<value.size() && n && ((uint8_t(value[n])&0xc0)==0x80))--n;
            while(n) {
                std::memcpy(label.data(),value.data(),n);std::memcpy(label.data()+n,"\xe2\x80\xa6",3);
                if(font.measure({label.data(),n+3})*textScale<=maxWidth)break;
                --n;while(n<value.size() && n && ((uint8_t(value[n])&0xc0)==0x80))--n;
            }
            value={label.data(),n+3};
        }
        if(hudEdges){float f=std::min(vp.width/640,vp.height/448);x-=(vp.width/f-640)/2;y-=(vp.height/f-448)/2;}
        font.draw(value,colour,x,y,vp,scratch,align,textScale);
    }
    void brand(Rect vp,float x,float y,float width,float height) {
        // Fit the same transparent mark everywhere without distorting lettering.
        const float scale=std::min(width/brandLogo.width,height/brandLogo.height);
        const float w=brandLogo.width*scale,h=brandLogo.height*scale;
        image(brandLogo,vp,x+(width-w)/2,y+(height-h)/2,w,h);
    }
    void label(NativeTextFont& font,TextId id,Palette colour,float x,float y,Rect vp,
               float maxWidth=600,TextAlign align=TextAlign::Left) {
        text(font,locale.text(id),colour,x,y,vp,maxWidth,align);
    }
    void drawLoading(Rect vp) {
        const auto now=uiNow();const float age=float(now-born)*.03f;
        float fade=std::clamp((age-12)/8,0.f,1.f);
        if(frameState.phase==PreparationPhase::Failed)fade=1;
        if(fadeOut)fade=std::max(0.f,1-float(now-fadeOut)*.03f/10);
        sink.opacity=fade;
        sink.begin(vp);sink.rgba(backdrop.handle,vp);sink.end();
        float scroll=std::fmod(float(now-born)*.008f,640.f);
        image(banner,vp,-scroll,134,640,160,.3f);image(banner,vp,640-scroll,134,640,160,.3f);
        if(frameState.mode==BattleMode::FreeForAll) {
            image(teamCyan,vp,24,70,592,180);
            label(smallTitle,TextId::FreeForAll,Palette::Gold,320,62,vp,560,TextAlign::Centre);
            const unsigned total=frameState.teamOneCount+frameState.teamTwoCount;
            for(unsigned i=0;i<total;++i) {
                float x=52+112*(i%5),y=80+83*(i/5);unsigned id=frameState.fighters[i];
                image(portraitBorder,vp,x,y,60,60);image(portraits[id],vp,x+1,y+1,58,58);
                image(seatIcons[frameState.seats[i]],vp,x+30,y,30,15,1,frameState.seats[i]?1:.6f);
                text(smallBody,names[id],Palette::White,x+30,y+75,vp,100,TextAlign::Centre);
            }
        } else for(unsigned side=0;side<2;++side) {
            const unsigned count=side?frameState.teamTwoCount:frameState.teamOneCount;
            const unsigned offset=side?frameState.teamOneCount:0;
            const float left=side?344.f:24.f;
            image(side?teamOrange:teamCyan,vp,left,70,272,180);
            // Accent rails retain the source plate's slant and quiet metal edge.
            const bool coop=frameState.mode==BattleMode::Coop || frameState.mode==BattleMode::TrainingCoop;
            const bool training=frameState.mode==BattleMode::Training;
            const auto heading=coop?(side?TextId::Enemies:TextId::Allies):training?(side?TextId::PracticePartner:TextId::You):(side?TextId::TeamTwo:TextId::TeamOne);
            label(smallBody,heading,side?Palette::Yellow:Palette::Cyan,left+16,66,vp,260);
            for(unsigned i=0;i<count;++i) {
                float x=0,y=0,size=0;
                if(count==1){x=72;y=88;size=128;}
                if(count==2){x=28+116*i;y=108;size=96;}
                if(count==3){x=16+80*i;y=114;size=80;}
                if(count==4){x=44+112*(i%2);y=76+78*(i/2);size=72;}
                if(count==5){if(!i){x=18;y=100;size=80;}else{x=126+66*((i-1)%2);y=80+85*((i-1)/2);size=56;}}
                const auto id=frameState.fighters[offset+i];
                float pulse=frameState.phase==PreparationPhase::Ready && now-stageBorn<200?1.15f:1;
                image(portraitBorder,vp,left+x,y,size,size);
                image(portraits[id],vp,left+x+1,y+1,size-2,size-2,1,pulse);
                image(seatIcons[frameState.seats[offset+i]],vp,left+x+size-30,y,30,15,1,frameState.seats[offset+i]?1:.6f);
                auto& font=i==0?body:smallBody;
                auto name=std::string_view(names[id]);
                if(!i && font.measure(name)>size) {
                    size_t split=name.rfind(' ');
                    while(split!=std::string_view::npos && font.measure(name.substr(0,split))>size)
                        split=split?name.rfind(' ',split-1):std::string_view::npos;
                    if(split!=std::string_view::npos) {
                        text(font,name.substr(0,split),Palette::White,left+x+size/2,y+size+16,vp,size,TextAlign::Centre);
                        text(smallBody,name.substr(split+1),Palette::White,left+x+size/2,y+size+30,vp,size,TextAlign::Centre);
                    } else text(font,name,Palette::White,left+x+size/2,y+size+17,vp,size,TextAlign::Centre);
                } else text(font,name,Palette::White,left+x+size/2,y+size+17,vp,size,TextAlign::Centre);
            }
        }
        float vsScale=age<10?(age<5?.6f+.1f*age:1.1f-.02f*(age-5)):1;
        if(frameState.mode==BattleMode::Teams || frameState.mode==BattleMode::Coop)
            image(vs,vp,320-64*vsScale,150-40*vsScale,128*vsScale,80*vsScale);
        if(frameState.mode==BattleMode::Training || frameState.mode==BattleMode::TrainingCoop)
            label(smallBody,TextId::ModdedTraining,Palette::Gold,590,43,vp,400,TextAlign::Right);
        else if(frameState.mode==BattleMode::Teams && !frameState.humans)
            label(smallBody,TextId::Exhibition,Palette::Gold,590,43,vp,400,TextAlign::Right);
        if(frameState.stage!=lastStage){lastStage=frameState.stage;stageBorn=now;}
        float stageAge=float(now-stageBorn)*.03f;
        if(progressTarget!=float(frameState.progress)) {
            progressFrom=smoothProgress;progressTarget=float(frameState.progress);progressBorn=now;
        }
        float tween=std::clamp(float(now-progressBorn)*.03f/12,0.f,1.f);
        smoothProgress=progressFrom+(progressTarget-progressFrom)*(1-(1-tween)*(1-tween)*(1-tween));
        for(unsigned i=0;i<7;++i) {
            float scale=(i==std::min<unsigned>(frameState.stage,6) && stageAge<8)?
                (stageAge<4?1+stageAge*.0625f:1.25f-(stageAge-4)*.0625f):1;
            image(ballIcons[i],vp,88+i*66+(56-56*scale)/2,264+(56-56*scale)/2,56*scale,56*scale,
                1,i<=frameState.stage?1:.28f);
            if(i==std::min<unsigned>(frameState.stage,6) && stageAge<6) {
                image(spark,vp,88+i*66,264,56,56,.6f*(1-stageAge/6));
            }
            if(i==std::min<unsigned>(frameState.stage,6) && stageAge<10) {
                for(unsigned j=0;j<4;++j) {float a=j*1.570796f;
                    image(spark,vp,88+i*66+24+std::cos(a)*(28+stageAge*1.6f),288+std::sin(a)*(28+stageAge*1.6f),8,8,1-stageAge/10);}
            }
        }
        // Borderless energy strip; the stage balls carry the loading ornament.
        constexpr float innerX=48,innerY=346,innerW=544,innerH=18;
        image(hudGlass,vp,innerX,innerY,innerW,innerH);
        float fill=std::clamp(smoothProgress/100,0.f,1.f);
        if(fill>0) {
            // Procedural energy bands cannot sample the old sprite's scroll cap,
            // gold border or black tail. Every segment is bounded by the fill.
            const float width=innerW*fill;
            image(energyFill,vp,innerX,innerY,width,innerH);
            image(hudGold,vp,innerX,innerY+innerH-2,width,2,.6f);
            for(unsigned band=0;band<9;++band) {
                const float x=band*64.f;if(x>=width)break;
                const float shimmer=.04f+.035f*std::sin(x*.045f-float(now-born)*.003f);
                image(white,vp,innerX+x,innerY+2,std::min(64.f,width-x),innerH-4,shimmer);
            }
            image(white,vp,innerX,innerY+4,width,2,.3f,.9f);
            image(hudGold,vp,innerX+std::max(0.f,width-2),innerY, std::min(2.f,width),innerH,.9f);
        }
        constexpr TextId stages[]={TextId::PreparingTeams,TextId::LoadingFighters,TextId::CreatingFighters,
            TextId::PreparingArena,TextId::SettingUpCombat,TextId::GettingReady,TextId::Ready,TextId::Ready};
        if(frameState.phase!=PreparationPhase::Failed && !frameState.ownerExpired) {
            label(body,stages[std::min<unsigned>(frameState.stage,7)],Palette::White,320,384,vp,570,TextAlign::Centre);
            char percent[16];std::snprintf(percent,sizeof(percent),"%u%%",frameState.progress);
            text(numeric,percent,Palette::Gold,320,404,vp,100,TextAlign::Centre);
        }
        brand(vp,226,2,188,60);
        if(frameState.phase==PreparationPhase::Failed || frameState.ownerExpired) {
            image(row,vp,28,380,584,58);
            label(smallTitle,TextId::PreparationFailed,Palette::Red,320,402,vp,560,TextAlign::Centre);
            label(smallBody,TextId::CloseReopen,Palette::White,320,426,vp,560,TextAlign::Centre);
        }
        sink.opacity=1;lastFrame=now;
    }
    void drawMenu(Rect vp) {
        UiMenu menu;{std::lock_guard lock(uiStore().mutex);menu=uiStore().menu;}
        // Settings, About and mode navigation share the credits' quiet DBZ
        // background. Keep every row and text column at its existing position.
        sink.begin(vp);sink.rgba(backdrop.handle,vp);sink.end();
        for(unsigned i=0;i<28;++i){const float x=16.f+float((i*173)%608),y=12.f+float((i*79)%410);
            image(spark,vp,x,y,i%5==0?3:1,i%5==0?3:1,.2f);}
        image(hudGold,vp,0,0,640,3,.8f);image(hudGold,vp,0,445,640,3,.8f);
        brand(vp,442,5,166,55);
        image(hudGold,vp,32,81,575,1,.65f);
        text(title,menu.text.data(),Palette::Gold,48,64,vp,385);
        for(unsigned i=0;i<menu.rows;++i) {
            float y=98+i*34;
            image(hudGlass,vp,32,y,575,31,i==menu.selected?.95f:.65f);
            if(i==menu.selected){image(hudGold,vp,32,y,3,31,.9f);image(hudGold,vp,35,y+30,572,1,.45f);}
            auto name=std::string_view(menu.text.data()+128+i*192);
            auto value=std::string_view(menu.text.data()+224+i*192);
            bool chipValue=!value.empty();
            if(chipValue){image(hudGlass,vp,417,y+2,178,27,.8f);image(hudGold,vp,417,y+5,1,21,.35f);}
            const bool identifier=name.starts_with("https://") || name.starts_with("http://") || name=="Build" || name=="Version" || name=="Disc" || name=="Disco";
            text(identifier?uprightBody:body,name,i==menu.selected?Palette::Gold:Palette::White,48,y+22,vp,chipValue?350:540);
            if(chipValue)text(identifier?uprightBody:body,value,Palette::White,506,y+22,vp,158,TextAlign::Centre);
        }
        auto helpText=std::string_view(menu.text.data()+1664);
        const bool threeLines=std::count(helpText.begin(),helpText.end(),'\n')>=2;
        image(hudGlass,vp,32,390,575,threeLines?45:38,.85f);image(hudGold,vp,32,390,575,1,.5f);
        for(unsigned i=0;i<(threeLines?3u:2u) && !helpText.empty();++i) {
            auto newline=helpText.find('\n');
            text(smallBody,helpText.substr(0,newline),Palette::White,46,(threeLines?400.f:406.f)+i*17,vp,545);
            if(newline==std::string_view::npos)break;
            helpText.remove_prefix(newline+1);
        }
        const auto error=std::string_view(menu.text.data()+1920);
        if(!error.empty())text(smallBody,error,Palette::Red,48,378,vp,540);
    }
    void drawProduct(Rect vp) {
        locale.select(frameState.language==Language::Spanish?"es":"en");
        if(creditsEnabled) {drawCredits(vp);return;}
        if(hud.introStage && hud.generation==frameState.generation){
            if(hud.paused)return;
            const bool spanish=frameState.language==Language::Spanish;
            const auto label=hud.introStage==1?(spanish?"LISTOS!":"READY!"):(spanish?"A LUCHAR!":"FIGHT!");
            text(title,label,hud.introStage==1?Palette::White:Palette::Gold,320,224,vp,600,TextAlign::Centre);
            hudStore().presentedGeneration.store(hud.generation,std::memory_order_release);
            hudStore().presentedIntroStage.store(hud.introStage,std::memory_order_release);return;
        }
        if(hud.active && hud.generation==frameState.generation &&
           (frameState.phase==PreparationPhase::Ready || frameState.phase==PreparationPhase::Released)) {drawHud(vp);return;}
        if(frameState.screen==Screen::Hidden && frameState.phase==PreparationPhase::Released && fadeOut && uiNow()-fadeOut>=334) {drawHud(vp);return;}
        if(frameState.screen==Screen::Loading || (frameState.screen==Screen::Hidden && frameState.phase==PreparationPhase::Released))drawLoading(vp);
        else drawMenu(vp);
    }
    void hudBar(Rect vp,float x,float y,float width,float height,float fraction,unsigned colour,float backingAlpha=1) {
        image(hudBars[2],vp,x,y,width,height,backingAlpha);
        fraction=std::clamp(fraction,0.f,1.f);
        if(fraction>0)image(hudBars[colour],vp,x,y,width*fraction,height,1,1,0,0,fraction,1);
    }
    void overheadGlass(Rect vp,OverheadBox box,bool quiet) {
        const float alpha=std::min(hud.preferences.opacity/100.f,quiet?.35f:.45f);
        image(hudGlass,vp,box.x,box.y,box.width,box.height,alpha);
        image(white,vp,box.x,box.y,box.width,1,.7f,.65f);
        image(white,vp,box.x,box.y+box.height-1,box.width,1,.7f,.65f);
        image(white,vp,box.x,box.y,1,box.height,.7f,.65f);
        image(white,vp,box.x+box.width-1,box.y,1,box.height,.7f,.65f);
    }
    float overheadScale(const HudProjection& point,bool detailed,bool quiet) {
        const float scale=detailed?std::clamp(point.scale*hud.preferences.scale/100.f,.5f,2.f/3.f):std::clamp(point.scale,.7f,1.3f);
        return scale*(quiet?.8f:1.f);
    }
    void overheadPanel(Rect vp,unsigned physical,OverheadBox box,float scale,bool owner,unsigned seat,bool quiet,unsigned relation) {
        const auto& a=hud.actors[physical];const float x=box.x,y=box.y,w=box.width,h=box.height;
        overheadGlass(vp,box,quiet);image(relation==1?teamCyan:relation==0?teamOrange:relation==2?hudBars[0]:hudBars[3],vp,x,y,std::max(1.f,2*scale),h,.85f);
        float start=x+7*scale;
        if(hud.preferences.portraits){image(portraitBorder,vp,start,y+8*scale,36*scale,36*scale,.7f);
            image(portraits[a.character],vp,start+scale,y+9*scale,34*scale,34*scale);start+=42*scale;}
        const float width=x+w-7*scale-start,textScale=std::max(11.f/12.f,scale);
        if(hud.preferences.names && relation<2)text(smallBody,names[a.character],Palette::White,start,y+12,vp,width,TextAlign::Left,textScale);
        hudBar(vp,start,y+15,width,8*scale,float(a.hp)/std::max(1,a.maxHp),a.hp*4<a.maxHp?3:0,.7f);
        hudBar(vp,start,y+15+11*scale,width,6*scale,float(a.ki)/std::max(1,a.maxKi),1,.7f);
        if(hud.preferences.kiPips || !hud.showGameHud)for(unsigned pip=0;pip<a.stocks;++pip)image(hudGold,vp,start+pip*5*scale,y+15+19*scale,3*scale,std::max(2.f,3*scale));
        if(owner && seat<seatIcons.size())image(seatIcons[seat],vp,x+w-27*scale,y+box.height-14*scale,22*scale,11*scale);
        if(a.transforming || a.charging)text(smallBody,a.transforming?"T":"*",Palette::Cyan,x+w-16*scale,y+12,vp,12*scale,TextAlign::Left,textScale);
        if((hud.preferences.fusion || !hud.showGameHud) && a.fusionSeconds){char timer[20];std::snprintf(timer,sizeof(timer),"%us",a.fusionSeconds);
            text(smallBody,timer,a.fusionSeconds<=3?Palette::Red:Palette::Gold,x+7*scale,y+h-4*scale,vp,w/2,TextAlign::Left,textScale);}
        text(smallBody,"v",owner?Palette::Cyan:Palette::Gold,x+w/2,y+h+7*scale,vp,12,TextAlign::Centre,textScale);
    }
    void overheadArrow(Rect vp,unsigned physical,OverheadBox box,const HudProjection& point,bool owner,OverheadBox bounds) {
        overheadGlass(vp,box,hud.cinematic || hud.actors[physical].cinematic);
        auto name=std::string_view(names[hud.actors[physical].character]);size_t n=0;
        for(unsigned glyph=0;glyph<2 && n<name.size();++glyph){++n;while(n<name.size() && (uint8_t(name[n])&0xc0)==0x80)++n;}
        const char* arrow=point.x<bounds.x?"<":point.x>=bounds.x+bounds.width?">":point.y<bounds.y?"^":"v";
        text(smallBody,arrow,owner?Palette::Cyan:Palette::Gold,box.x+4,box.y+15,vp,12);
        if(hud.preferences.names)text(smallBody,name.substr(0,n),Palette::White,box.x+16,box.y+15,vp,25);
    }
    void overheadShape(Rect vp,unsigned physical,OverheadBox box,float diameter,bool owner,unsigned seat,bool quiet,unsigned relation) {
        const auto& a=hud.actors[physical];
        const float fill=std::min(hud.preferences.opacity/100.f,quiet?.35f:.45f);
        const Rect shape=logical(vp,box.x,box.y,diameter,diameter);
        sink.begin(vp);sink.gauge(hud.preferences.portraits?portraits[a.character].handle:hudGlass.handle,shape,
            hud.preferences.shape,relation,float(a.hp)/std::max(1,a.maxHp),float(a.ki)/std::max(1,a.maxKi),fill,hud.preferences.portraits,1);sink.end();
        if(owner && seat && seat<seatIcons.size())image(seatIcons[seat],vp,box.x,box.y+diameter-7,14,7);
        if(a.transforming || a.charging)text(smallBody,a.transforming?"T":"*",Palette::Cyan,box.x+diameter-8,box.y+diameter-3,vp,8,TextAlign::Left,10.f/12.f);
        float y=box.y+diameter+2;
        if(hud.preferences.kiPips || !hud.showGameHud){const float pipWidth=3.f,step=4.f,start=box.x+(diameter-a.stocks*step)/2;
            for(unsigned pip=0;pip<a.stocks;++pip)image(hudGold,vp,start+pip*step,y,pipWidth,2);y+=5;}
        if(hud.preferences.names && relation<2)text(smallBody,names[a.character],Palette::White,box.x+diameter/2,y+10,vp,diameter,TextAlign::Centre,hud.preferences.names==1?10.f/12.f:1.f);
    }
    static float fusionHeight(const HudActor& a) {
        return a.fusePrompt?14.f:(a.fusionSeconds || (a.fused && (a.showSwap || a.showFusionOwner))?34.f:0.f);
    }
    void fusionIndicators(Rect vp,const HudActor& a,OverheadBox box) {
        struct RestoreOpacity {float& value;float saved;~RestoreOpacity(){value=saved;}} restore{sink.opacity,sink.opacity};
        sink.opacity*=.45f;
        if(a.fusePrompt)sink.opacity*=.9f+.1f*std::sin(float(uiNow()%2000)*.00314159265f);
        const float w=std::min(46.f,box.width),x=box.x+(box.width-w)/2,y=box.y+box.height-fusionHeight(a);
        if(a.fusePrompt){image(hudGlass,vp,x,y,w,13,.77f);
            const auto label=frameState.language==Language::Spanish?"R3 FUSION":"R3 FUSE";
            const float scale=std::min(11.f/12.f,(w-2)/std::max(1.f,smallBody.measure(label)));
            text(smallBody,label,Palette::Gold,x+w/2,y+10,vp,w,TextAlign::Centre,scale);return;}
        auto arc=[&](float radius,float fraction,bool red){
            if(!hud.preferences.shape || hud.preferences.shape==3){
                image(red?hudBars[3]:hudGold,vp,x,y+(radius<10?6:0),w*std::clamp(fraction,0.f,1.f),2,1.f);return;}
            const unsigned segments=36;
            for(unsigned n=0;n<segments;++n){
                const float phase=float(n)/segments,angle=phase*6.2831853f-1.5707963f;
                float px=std::cos(angle)*radius,py=std::sin(angle)*radius;
                if(hud.preferences.shape==1){const unsigned edge=n/6;const float t=float(n%6)/6;
                    const float a0=float(edge)*1.04719755f-1.5707963f,a1=a0+1.04719755f;
                    px=radius*((1-t)*std::cos(a0)+t*std::cos(a1));py=radius*((1-t)*std::sin(a0)+t*std::sin(a1));}
                image(phase<fraction?(red?hudBars[3]:hudGold):hudGlass,vp,x+w/2+px,y+13+py,1.6f,1.6f,phase<fraction?1.f:.4f);
            }
        };
        if(a.fusionSeconds)arc(12,a.fusionLife>=0?a.fusionLife:float(a.fusionSeconds)/std::max(1u,a.fusionTotal),a.fusionSeconds<3);
        if(a.fusionSeconds && a.fusionFast)image(teamOrange,vp,x+w/2+14,y+12,3,2,1.f);
        if(a.showSwap)arc(8,1-float(a.swapSeconds)/std::max(1u,a.swapTotal),false);
        if(a.fusionSeconds && a.fusionSeconds<10){char number[8];std::snprintf(number,sizeof(number),"%u",a.fusionSeconds);
            text(numeric,number,a.fusionSeconds<3?Palette::Red:Palette::Gold,x+w/2,y+15,vp,w,TextAlign::Centre,.55f);}
        if(a.showFusionOwner){
            const bool split=a.attackSeat!=a.moveSeat;
            if(a.attackSeat<seatIcons.size())image(seatIcons[a.attackSeat],vp,x+(split?0:14),y+26,14,7);
            if(split && a.moveSeat<seatIcons.size())image(seatIcons[a.moveSeat],vp,x+28,y+26,14,7);
            if(split){
                // Compact attack spark and movement arrow, independent of locale.
                image(hudGold,vp,x+16,y+29,6,1,1.f);image(hudGold,vp,x+18,y+27,2,5,1.f);
                image(hudGold,vp,x+43,y+29,3,1,1.f);image(hudGold,vp,x+43,y+27,1,5,1.f);
                image(hudGold,vp,x+44,y+28,1,3,1.f);
            }
        }
    }
    void drawOverheads(Rect vp,float fullWidth,float fullHeight) {
        unsigned detailedDraws=0,simpleDraws=0,arrows=0;
        const float fit=std::min(vp.width/640,vp.height/448);
        for(unsigned index=0;index<hud.viewCount;++index){const auto& view=hud.views[index];if(!view.valid)continue;
            OverheadBox bounds{view.x*fullWidth/512,view.y*fullHeight/448,view.width*fullWidth/512,view.height*fullHeight/448};
            sink.clipped=true;sink.clip={vp.x+bounds.x*fit,vp.y+bounds.y*fit,bounds.width*fit,bounds.height*fit};
            std::array<OverheadBox,10> boxes{};std::array<bool,10> detailed{};std::array<float,10> diameters{};
            for(unsigned i=0;i<10;++i){const auto& a=hud.actors[i];const auto& point=view.points[i];if(!a.present || !a.alive || !point.valid)continue;
                const bool owner=i==view.subject,target=i==view.target;
                const bool allied=hud.mode!=BattleMode::FreeForAll && ((i&1)==(view.subject&1));
                detailed[i]=owner || (target && hud.preferences.detail>=1) || (allied && hud.preferences.detail>=2) || hud.preferences.detail==3;
                if(detailed[i] && !hud.preferences.shape && !fusionHeight(hud.actors[i]))continue;
                const bool quiet=hud.cinematic || a.cinematic;
                const float scale=overheadScale(point,detailed[i],quiet);
                const float x=point.x*fullWidth/512,y=point.y*fullHeight/448;
                float width=detailed[i]?156.f*scale:64.f*scale;
                float height=detailed[i]?std::max(50.f*scale,16.f+23.f*scale):7.f*scale;
                if(detailed[i] && hud.preferences.shape!=3){
                    width=overheadDiameter(point.scale,hud.preferences.scale,quiet);
                    if(!owner && !target)width=std::min(width,34.f*(quiet?.8f:1.f));
                    height=width+2+(hud.preferences.kiPips || !hud.showGameHud?5:0)+
                        (hud.preferences.names && (owner || target)?13:0);
                }
                if(detailed[i] && !hud.preferences.shape){width=46; height=0;}
                diameters[i]=width;if(fusionHeight(a))width=std::max(width,46.f);
                height+=fusionHeight(a);
                boxes[i]=overheadPlace(x,y,point.onScreen?width:32.f,point.onScreen?height:20.f,bounds);
            }
            if(view.subject<10 && view.target<10 && view.subject!=view.target && detailed[view.target])
                boxes[view.target]=overheadSeparate(boxes[view.target],boxes[view.subject],bounds);
            std::array<OverheadBox,10> reserved{};for(unsigned i=0;i<10;++i)if(detailed[i])reserved[i]=boxes[i];
            for(unsigned i=0;i<10;++i)if(!detailed[i] && boxes[i].width>0)boxes[i]=overheadAvoidDetails(boxes[i],reserved,bounds);
            for(unsigned pass=0;pass<2;++pass)for(unsigned i=0;i<10;++i){
                if(detailed[i]!=bool(pass))continue;
                if(detailed[i] && !hud.preferences.shape && !fusionHeight(hud.actors[i]))continue;
                const auto& a=hud.actors[i];const auto& point=view.points[i];const auto box=boxes[i];
                if(!a.present || !a.alive || !point.valid)continue;
                const bool owner=i==view.subject,target=i==view.target,allied=hud.mode!=BattleMode::FreeForAll && ((i&1)==(view.subject&1));
                const bool quiet=hud.cinematic || a.cinematic;
                sink.opacity=hud.fade*(quiet?.8f:1.f);
                if(!point.onScreen){if(owner || target){auto p=point;p.x*=fullWidth/512;p.y*=fullHeight/448;overheadArrow(vp,i,box,p,owner,bounds);++arrows;}continue;}
                const float scale=overheadScale(point,detailed[i],quiet);
                if(detailed[i]){
                    auto base=box;base.height-=fusionHeight(a);
                    if(hud.preferences.shape==3)overheadPanel(vp,i,base,scale,owner,view.seat,quiet,owner?1:target?0:allied?2:3);
                    else if(hud.preferences.shape){base.x+=(box.width-diameters[i])/2;overheadShape(vp,i,base,diameters[i],owner,view.seat,quiet,owner?1:target?0:allied?2:3);}
                    if(fusionHeight(a))fusionIndicators(vp,a,box);
                    ++detailedDraws;continue;
                }
                if((allied && !hud.preferences.friends) || (!allied && !hud.preferences.enemies))continue;
                ++simpleDraws;
                image(allied?teamCyan:teamOrange,vp,box.x-1,box.y+box.height-7*scale-1,box.width+2,9*scale,.7f);
                hudBar(vp,box.x,box.y+box.height-7*scale,box.width,7*scale,float(a.hp)/std::max(1,a.maxHp),a.hp*10<a.maxHp*3?3:0,.7f);
            }
            sink.clipped=false;
        }
        static uint64_t lastLog=0;const auto now=uiNow();
        if(now-lastLog>=1000){lastLog=now;std::fprintf(stderr,"[overhead] drawn views=%u detailed=%u simple=%u arrows=%u generation=%llu\n",hud.viewCount,detailedDraws,simpleDraws,arrows,(unsigned long long)hud.generation);}
    }
    void drawRevives(Rect vp,float fullWidth,float fullHeight){
        const float fit=std::min(vp.width/640,vp.height/448);
        const bool spanish=frameState.language==Language::Spanish;
        for(unsigned v=0;v<hud.viewCount;++v){const auto& view=hud.views[v];if(!view.valid)continue;
            OverheadBox bounds{view.x*fullWidth/512,view.y*fullHeight/448,view.width*fullWidth/512,view.height*fullHeight/448};
            sink.clipped=true;sink.clip={vp.x+bounds.x*fit,vp.y+bounds.y*fit,bounds.width*fit,bounds.height*fit};
            unsigned displayed=0;
            for(const auto& revival:hud.revives){
                if(!revival.state || revival.owner>=10 || displayed>=4 ||
                   (revival.owner&1)!=(view.subject&1) || hud.mode==BattleMode::FreeForAll)continue;
                // Hints/outcomes belong to their human viewport; active ally channels are shared.
                if(revival.state>=4 && revival.owner!=view.subject)continue;
                const unsigned anchor=revival.target<10?revival.target:revival.owner;
                const auto& point=view.points[anchor];if(!point.valid || !point.onScreen)continue;
                const float diameter=std::clamp(36.f*point.scale,26.f,40.f);
                const auto box=overheadPlace(point.x*fullWidth/512,point.y*fullHeight/448+18,diameter+16,diameter+27,bounds);
                const float cx=box.x+box.width/2,cy=box.y+diameter/2;
                const bool warning=revival.state==2 || revival.state==5 || revival.state==7;
                const auto ink=warning?Palette::Gold:revival.state==4?Palette::Cyan:Palette::White;
                sink.opacity=hud.fade*(hud.cinematic?.35f:.8f);
                image(hudGlass,vp,box.x,box.y,box.width,box.height,.35f);
                if(revival.ring && hud.preferences.shape && hud.preferences.shape!=3){
                    constexpr unsigned segments=36;
                    const float radius=diameter*.43f+(revival.state==1?revival.wave*.8f:0);
                    for(unsigned n=0;n<segments;++n){
                        const float phase=float(n)/segments,angle=phase*6.2831853f-1.5707963f;
                        float px=std::cos(angle)*radius,py=std::sin(angle)*radius;
                        if(hud.preferences.shape==1){const unsigned edge=n/6;const float t=float(n%6)/6;
                            const float a0=edge*1.04719755f-1.5707963f,a1=a0+1.04719755f;
                            px=((1-t)*std::cos(a0)+t*std::cos(a1))*radius;
                            py=((1-t)*std::sin(a0)+t*std::sin(a1))*radius;}
                        image(hudGold,vp,cx+px-1,cy+py-1,2,2,
                              (phase<revival.progress?1.f:.2f)*revival.opacity);
                    }
                }else hudBar(vp,box.x+4,cy,box.width-8,3,revival.progress,warning?1:0,.7f);
                if(revival.owner<10 && hud.actors[revival.owner].seat<seatIcons.size())
                    image(seatIcons[hud.actors[revival.owner].seat],vp,cx-7,cy-3,14,7);
                const char* chip=revival.state==1?(spanish?"REVIVIR":"REVIVE"):
                    revival.state==2?(spanish?"SIN STOCK":"NEED STOCK"):
                    revival.state==3?(spanish?"RECUPERANDO":"RECOVERING"):
                    revival.state==4?(spanish?"REVIVIDO":"REVIVED"):
                    revival.state==5?(spanish?"CANCELADO":"CANCELLED"):
                    revival.state==6?(spanish?"ACERCATE":"MOVE CLOSER"):(spanish?"ESPERA":"WAIT");
                const float textScale=std::min(.65f,(box.width+12)/std::max(1.f,smallBody.measure(chip)));
                text(smallBody,chip,ink,cx,box.y+diameter+10,vp,box.width+12,TextAlign::Centre,textScale);
                const unsigned pips=revival.cost<=10?revival.cost:1;
                const float left=cx-pips*4.f/2;
                for(unsigned pip=0;pip<pips;++pip)image(hudGold,vp,left+pip*4,box.y+diameter+15,2,2,.7f);
                if(revival.cost>10){char cost[12];std::snprintf(cost,sizeof(cost),"x%u",revival.cost);
                    text(smallBody,cost,Palette::Gold,cx+3,box.y+diameter+19,vp,24,TextAlign::Left,.55f);}
                if(revival.state==1 && revival.owner!=anchor){const auto& ownerPoint=view.points[revival.owner];
                    if(ownerPoint.valid && ownerPoint.onScreen){const auto ownerBox=overheadPlace(ownerPoint.x*fullWidth/512,ownerPoint.y*fullHeight/448+18,20,5,bounds);
                        if(revival.ring && hud.preferences.shape && hud.preferences.shape!=3){
                            for(unsigned n=0;n<18;++n){const float phase=float(n)/18,angle=phase*6.2831853f-1.5707963f;
                                image(hudGold,vp,ownerBox.x+10+std::cos(angle)*6,ownerBox.y+std::sin(angle)*6,1.5f,1.5f,phase<revival.progress?.8f:.15f);}
                        }else hudBar(vp,ownerBox.x,ownerBox.y,20,2,revival.progress,1,.6f);}}
                ++displayed;
            }
            sink.clipped=false;
        }
        sink.opacity=hud.fade;
    }
    void drawV11Prompts(Rect vp,float fullWidth,float fullHeight) {
        const float fit=std::min(vp.width/640,vp.height/448);
        for(const auto& view:hud.views){if(!view.valid || view.subject>=10)continue;
            const auto& prompt=hud.prompts[view.subject];
            OverheadBox bounds{view.x*fullWidth/512,view.y*fullHeight/448,view.width*fullWidth/512,view.height*fullHeight/448};
            sink.clipped=true;sink.clip={vp.x+bounds.x*fit,vp.y+bounds.y*fit,bounds.width*fit,bounds.height*fit};
            const float size=std::clamp(view.height/448.f,.65f,1.f)*std::clamp(hud.preferences.scale/65.f,.8f,1.4f);
            sink.opacity=hud.fade;
            if(prompt.beamText[0]){
                const float width=std::min(230.f*size,bounds.width-24),height=23.f*size;
                const float x=bounds.x+12,y=bounds.y+bounds.height-85.f*size;
                image(hudGlass,vp,x,y,width,height,std::max(.45f,hud.preferences.opacity/100.f));
                image(prompt.beamWarning?hudBars[3]:hudGold,vp,x,y,2*size,height,.9f);
                const auto seat=hud.actors[view.subject].seat;
                if(seat && seat<seatIcons.size())image(seatIcons[seat],vp,x+7*size,y+8*size,14*size,7*size);
                const float offset=seat?27.f:8.f;
                text(smallBody,{prompt.beamText.data()},prompt.beamWarning?Palette::Gold:Palette::White,
                     x+offset*size,y+16*size,vp,width-offset*size-5,TextAlign::Left,size);
            }
            // A view with incomplete native projections keeps the guest markers.
            bool coverage=true;for(unsigned i=0;i<10;++i)if(hud.actors[i].present && hud.actors[i].alive && !view.points[i].valid)coverage=false;
            if(coverage && !hud.cinematic){
                for(unsigned i=0;i<10;++i){const auto& a=hud.actors[i];const auto& point=view.points[i];
                    const bool target=prompt.target==i,threat=(prompt.threats&(1u<<i))!=0,warning=(prompt.warnings&(1u<<i))!=0;
                    if(!a.present || !a.alive || !point.valid || (!target && !threat))continue;
                    const bool gold=warning?prompt.blink:target;
                    const auto ink=gold?Palette::Gold:Palette::Red;
                    const auto art=gold?hudGold:hudBars[3];
                    const float px=point.x*fullWidth/512,py=point.y*fullHeight/448;
                    const float radius=std::clamp(22.f*point.scale,14.f,26.f)*size;
                    if(!point.onScreen){
                        float dx=px-(bounds.x+bounds.width/2),dy=py-(bounds.y+bounds.height/2);
                        const float hx=std::max(1.f,bounds.width/2-14*size),hy=std::max(1.f,bounds.height/2-35*size);
                        const float factor=std::max(std::abs(dx)/hx,std::abs(dy)/hy);
                        if(!std::isfinite(factor) || factor<=0)continue;
                        const float x=bounds.x+bounds.width/2+dx/factor,y=bounds.y+bounds.height/2+dy/factor;
                        const char* arrow=std::abs(dx)/hx>std::abs(dy)/hy?(dx<0?"<":">"):(dy<0?"^":"v");
                        image(hudGlass,vp,x-9*size,y-12*size,18*size,20*size,.7f);
                        text(body,arrow,ink,x,y+3*size,vp,18*size,TextAlign::Centre,size);
                        continue;
                    }
                    const float x=std::clamp(px,bounds.x+radius+4,bounds.x+bounds.width-radius-4);
                    const float y=std::clamp(py,bounds.y+40*size,bounds.y+bounds.height-radius-10);
                    sink.opacity=hud.fade*(warning?.95f:.75f);
                    if(target && (prompt.targetStyle&2)){
                        // The same ring/hexagon language as fusion and revival.
                        for(unsigned n=0;n<36;++n){const unsigned edge=n/6;const float t=float(n%6)/6;
                            const float a0=edge*1.04719755f-1.5707963f,a1=a0+1.04719755f;
                            float dx=radius*((1-t)*std::cos(a0)+t*std::cos(a1)),dy=radius*((1-t)*std::sin(a0)+t*std::sin(a1));
                            if(hud.preferences.shape!=1){const float angle=n*6.2831853f/36;dx=radius*std::cos(angle);dy=radius*std::sin(angle);}
                            image(hudGlass,vp,x+dx-2,y+dy-2,4*size,4*size,.8f);
                            image(art,vp,x+dx-1,y+dy-1,2*size,2*size,.9f);
                        }
                    }
                    if(target && (prompt.targetStyle&1))text(body,"v",ink,x,y-radius-6*size,vp,22*size,TextAlign::Centre,size);
                    if(threat){
                        const float markScale=size*(warning?1.3f:1.f);
                        text(body,">",ink,x-radius-7*size,y+5*markScale,vp,18*markScale,TextAlign::Centre,markScale);
                        text(body,"<",ink,x+radius+7*size,y+5*markScale,vp,18*markScale,TextAlign::Centre,markScale);
                    }
                }
            }
            sink.clipped=false;
        }
        sink.opacity=hud.fade;
    }
    void drawHud(Rect vp) {
        if(!hud.active || hud.paused || hud.generation!=frameState.generation)return;
        hudEdges=true;
        const float scale=std::min(vp.width/640,vp.height/448),fullWidth=vp.width/scale,fullHeight=vp.height/scale;
        hudStore().presentedGeneration.store(hud.generation,std::memory_order_release);
        sink.opacity=1;
        if(hud.preferences.list) {
            unsigned rowIndex=0;
            for(unsigned i=0;i<10;++i)if(hud.actors[i].present) {
                auto& a=hud.actors[i];float y=86+rowIndex++*32;
                image(hudPlate,vp,12,y,192,30);image(portraits[a.character],vp,17,y+3,24,24,1,a.alive?1:.3f);
                text(smallBody,names[a.character],a.alive?Palette::White:Palette::Grey,47,y+15,vp,150);
                hudBar(vp,47,y+22,148,5,float(a.hp)/std::max(1,a.maxHp),a.alive?(a.hp*4<a.maxHp?3:0):2);
                if(a.seat)image(seatIcons[a.seat],vp,168,y+1,28,14);
                if(a.fusionSeconds) {char timer[16];std::snprintf(timer,sizeof(timer),"%u",a.fusionSeconds);unsigned n=unsigned(std::strlen(timer)),colour=a.fusionSeconds<=3?2:a.fusionSeconds<10?1:0;
                    for(unsigned d=0;d<n;++d){auto art=timerDigits[colour][timer[d]-'0'];image(art,vp,192-(n-d)*11,y+18,float(art.width)*14/art.height,14);}}
            }
        }
        for(unsigned i=0;i<3;++i)if(hud.kills[i].active) {
            const auto& k=hud.kills[i];float y=210+i*36,x=fullWidth-230;image(hudPlate,vp,x,y,214,32);
            if(k.known)image(portraits[k.killer],vp,x+5,y+4,24,24);
            text(smallBody,k.known?killPlateName(names[k.killer]):std::string_view("?"),Palette::White,x+34,y+13,vp,130);
            text(smallBody,">",Palette::Gold,x+170,y+21,vp,18);
            image(portraits[k.victim],vp,x+185,y+4,24,24);text(smallBody,killPlateName(names[k.victim]),Palette::Grey,x+34,y+28,vp,130);
        }
        if(hud.training) {
            const float y=fullHeight-128;
            image(hudPlate,vp,16,y,300,114);
            label(smallTitle,TextId::ModdedTraining,Palette::Gold,28,y+23,vp,275);
            label(smallBody,TextId::RefillHp,Palette::White,28,y+43,vp,130);label(smallBody,hud.refill?TextId::Yes:TextId::No,Palette::Gold,158,y+43,vp,45);
            label(smallBody,TextId::IdleCpu,Palette::White,210,y+43,vp,70);label(smallBody,hud.idle?TextId::Yes:TextId::No,Palette::Gold,298,y+43,vp,40,TextAlign::Right);
            label(smallBody,TextId::ShowCounters,Palette::White,28,y+65,vp,155);label(smallBody,hud.showCounters?TextId::Yes:TextId::No,Palette::Gold,184,y+65,vp,45);
            if(hud.showCounters){char hits[24],damage[24];std::snprintf(hits,sizeof(hits),"%u",hud.hits);std::snprintf(damage,sizeof(damage),"%u",hud.damage);
                label(smallBody,TextId::Hits,Palette::White,28,y+87,vp,65);text(numeric,hits,Palette::Gold,138,y+87,vp,38,TextAlign::Right);
                label(smallBody,TextId::Damage,Palette::White,154,y+87,vp,70);text(numeric,damage,Palette::Gold,298,y+87,vp,72,TextAlign::Right);
                label(smallBody,TextId::TrainingReset,Palette::Grey,28,y+109,vp,275);}
        }
        if(hud.marker) {float f=std::min(vp.width/640,vp.height/448);sink.begin(vp);
            sink.rgba(lockMarker.handle,{vp.x+hud.markerX*vp.width/512-10*f,vp.y+hud.markerY*vp.height/448-10*f,20*f,20*f});sink.end();}
        sink.opacity=hud.fade;
        drawOverheads(vp,fullWidth,fullHeight); // fighter status has priority over auxiliary panels
        drawRevives(vp,fullWidth,fullHeight);
        drawV11Prompts(vp,fullWidth,fullHeight);
        sink.clipped=false;sink.opacity=1;hudEdges=false;
    }
    void drawCredits(Rect vp) {
        const auto now=uiNow();float age=float(now-creditsBorn);
        const float duration=creditsFull?4000.f:1500.f;
        sink.opacity=std::min(std::clamp(age/(8*1000.f/30),0.f,1.f),std::clamp((duration-age)/334.f,0.f,1.f));
        if(creditsFade)sink.opacity*=std::clamp(1-float(now-creditsFade)/334,0.f,1.f);
        if(creditsFull) {
            // Keep the boot image fully covered while credit text fades. Never
            // reuse battle status frames, whose empty portrait cap looks like
            // a character placeholder on this screen.
            const float fade=sink.opacity;sink.opacity=1;
            sink.begin(vp);sink.rgba(backdrop.handle,vp);sink.end();sink.opacity=fade;
            for(unsigned i=0;i<28;++i){const float x=16.f+float((i*173)%608),y=12.f+float((i*79)%410);
                image(spark,vp,x,y,i%5==0?3:1,i%5==0?3:1,.2f);}
            image(hudGold,vp,0,0,640,3,.8f);image(hudGold,vp,0,445,640,3,.8f);
            brand(vp,100,28,440,148);
            label(body,TextId::CreditsProject,Palette::White,320,194,vp,560,TextAlign::Centre);
            image(hudGold,vp,128,209,384,1,.65f);
            for(unsigned i=0;i<2;++i) {
                const float y=222+i*66;image(hudGlass,vp,42,y,556,60,.65f);
                image(hudGold,vp,42,y,2,60,.8f);
                text(body,i?"Tag Team: The Mufti":"Power Scale: LetsPlayBt3",Palette::Gold,320,y+24,vp,530,TextAlign::Centre);
                label(smallBody,i?TextId::TagTeamLink:TextId::PowerScaleLink,Palette::White,320,y+46,vp,530,TextAlign::Centre);
            }
            image(hudGold,vp,128,363,384,1,.5f);
            label(smallBody,TextId::SpecialThanks,Palette::White,320,387,vp,560,TextAlign::Centre);
            if(age>=1500)label(smallBody,TextId::PressAnyButton,Palette::Gold,320,412,vp,560,TextAlign::Centre);
        } else {
            image(hudGlass,vp,32,326,576,98,.85f);image(hudGold,vp,32,326,576,2,.8f);
            text(body,"Power Scale: LetsPlayBt3",Palette::Gold,320,352,vp,550,TextAlign::Centre);
            text(body,"Tag Team: The Mufti",Palette::Gold,320,378,vp,550,TextAlign::Centre);
            label(smallBody,TextId::SpecialThanks,Palette::White,320,406,vp,550,TextAlign::Centre);
        }
        sink.opacity=1;
    }
