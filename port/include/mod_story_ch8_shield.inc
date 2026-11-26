static void st_vnode(int,int,Vec4*);
/* CH8 Kienzan prefix, joined to the existing shield and Masenko events. */
static Vec4 ksPos[5],ksRot[5],ksDiscFrom,ksDiscTo;
static float ksH,ksYaw;
static Vec4 ksShieldTarget;
static Vec4 sKsDiscPos;static float sKsDiscRadius;static int sKsDiscVisible;
int Port_StoryDiscPlace(int obj,const Vec4*p,float scale){(void)obj;if(!p)return 0;extern int Port_StoryNativeDiscPlace(int,const Vec4*,float,int),Port_StoryDiscPalette(int);sKsDiscPos=*p;sKsDiscRadius=ksH*.7f*(scale/.5f);sKsDiscVisible=1;return Port_StoryNativeDiscPlace(obj,p,scale,Port_StoryDiscPalette(sStRun.cast[2]));}
void Port_StoryDiscEnd(void){extern void Port_StoryNativeDiscEnd(void);Port_StoryNativeDiscEnd();sKsDiscVisible=0;}
int Port_StoryKienzanInfo(float*q){return 0; /* Native game mesh draws the disc. */ if(!sStKienActive||!sKsDiscVisible)return 0;q[0]=sKsDiscPos.x;q[1]=sKsDiscPos.y;q[2]=sKsDiscPos.z;q[3]=sKsDiscRadius;return 1;}

static const int ksCast[5]={2,8,9,3,4};
static Vec4 ks_offset(Vec4 p,float f,float side){p.x+=(sinf(ksYaw)*f+cosf(ksYaw)*side)*ksH;p.z+=(cosf(ksYaw)*f-sinf(ksYaw)*side)*ksH;return p;}
static int st_kien_role(int a){int i;for(i=0;i<5;i++)if(a==sStRun.cast[ksCast[i]])return i;return -1;}
static void st_kien_pose(int a){int i=st_kien_role(a),f=sStKienFrame;Vec4 p,rot;int m=0,fr=0;if(i<0)return;p=ksPos[i];rot=ksRot[i];
 if(i==0){BtlCtrl_SetPos(a,&p);BtlCtrl_SetRot(a,&rot);if(f<100)st_ch8_clip(a,1016,f<17?329:330,(f<17?f:6)*256);else st_ch8_clip(a,1016,331,(f-100<96?f-100:96)*256);}
 else {if(i==3&&f>=184){
  float t=(f-184)/28.f,u,b,yaw,dy;void*o=BtlObj_Get(st_obj(a));
  extern void Port_StoryBlendSampledPose(void*,int,int);
  if(t>1)t=1;u=t*t*t*(10.f+t*(-15.f+6.f*t)); /* bounded acceleration and braking */
  p=as_lerp(ksPos[3],ksShieldTarget,u);p.y-=st_height(a)*.12f*sinf(t*3.14159265f);
  yaw=atan2f(ksShieldTarget.x-ksPos[3].x,ksShieldTarget.z-ksPos[3].z);
  b=(f-198)/14.f;if(b<0)b=0;if(b>1)b=1;b=b*b*(3.f-2.f*b);
  dy=atan2f(sinf(ksRot[4].y-yaw),cosf(ksRot[4].y-yaw));rot.y=yaw+dy*b;
  BtlCtrl_SetPos(a,&p);BtlCtrl_SetRot(a,&rot);
  if(f<212){
   Port_StoryLoopMotion(a,ST_MOTION_FLIGHT,(f-184)*2);
   if(f>=198){Port_StoryBlendSampledPose(o,1,4096);st_ch8_clip(a,2752,285,28*256);Port_StoryBlendSampledPose(o,0,(212-f)*4096/14);}
  }else st_ch8_clip(a,2752,285,28*256);
  st_ch5_ground_pose(a,p.y);return;
 }if(i==1&&f>=117){float t=(f-117)/11.f;if(t>1)t=1;p=ks_offset(p,0,1.45f*t*(2-t));m=30;fr=(f-117)*3;if(fr>38)fr=38;} /* Juan: clear of the disc before it arrives (~f134) */if(i==2&&f>=112){Vec4 nb=ksPos[1];rot.y=atan2f(nb.x-p.x,nb.z-p.z);} /* Vegeta watches Nappa and the disc */BtlCtrl_SetPos(a,&p);BtlCtrl_SetRot(a,&rot);Port_StorySampleMotion(a,m,fr);}
 st_ch5_ground_pose(a,p.y);
}
static void st_kien_pair(void){int i;for(i=0;i<5;i++)st_kien_pose(sStRun.cast[ksCast[i]]);}
static void st_kien_begin(void){int i,k=sStRun.cast[2],n=sStRun.cast[8];Vec4 p,rot;
 for(i=0;i<5;i++){int a=sStRun.cast[ksCast[i]];Port_StoryEnterActor(a);st_scene_take(a,1);}
 if(BtlStage_GetStartPlace(1,&p,&rot,0)<0){BtlCharApi_GetPos(st_obj(n),&p);BtlCharApi_GetRot(st_obj(n),&rot);}ksYaw=rot.y;ksH=st_height(n);
 ksPos[1]=p;ksPos[0]=ks_offset(p,5,0);ksPos[2]=ks_offset(p,-1,3);ksPos[3]=ks_offset(ksPos[0],-2.2f,-2.7f);ksPos[4]=ks_offset(ksPos[0],-2.4f,2.7f); /* Juan: Krillin not on top of Piccolo/Gohan */
 for(i=0;i<5;i++){int a=sStRun.cast[ksCast[i]];BtlCtrl_SetPos(a,&ksPos[i]);Port_StoryCommitSampledRoot(a);Port_StorySampleMotion(a,0,0);st_scene_ground_actor(a);Port_StoryCommitSampledRoot(a);BtlCharApi_GetPos(st_obj(a),&ksPos[i]);ksRot[i]=(Vec4){0,atan2f(ksPos[i?0:1].x-ksPos[i].x,ksPos[i?0:1].z-ksPos[i].z),0,1};}
 ksRot[4].y=atan2f(ksPos[1].x-ksPos[4].x,ksPos[1].z-ksPos[4].z);ksShieldTarget=ksPos[4];ksShieldTarget.x+=sinf(ksRot[4].y)*st_height(sStRun.cast[4])*1.8f;ksShieldTarget.z+=cosf(ksRot[4].y)*st_height(sStRun.cast[4])*1.8f;
 sStKienFrame=0;sStKienActive=1;Port_Dbg3("story Kienzan prefix begins",k,n,1016,331);
}
static void st_kien_camera(void){int f=sStKienFrame,a=sStRun.cast[f>=184?3:f>=65&&f<100?9:f<120?2:8];Vec4 at,eye,rot;float h=st_height(a);BtlCharApi_GetNodePos(st_obj(a),f<120?46:17,&at);BtlCharApi_GetRot(st_obj(a),&rot);if(f>=184)rot.y=ksRot[4].y; /* keep the accepted lens while Piccolo turns into the block */eye=at;eye.x+=(sinf(rot.y)*1.65f+cosf(rot.y)*.55f)*h;eye.z+=(cosf(rot.y)*1.65f-sinf(rot.y)*.55f)*h;eye.y-=h*.15f;sStCamDrift=0;st_camera_finish(eye,at,h,a,3);}
static void st_kien_geometry(void){int f=sStKienFrame,k=sStRun.cast[2],n=sStRun.cast[8];Vec4 p;extern int Port_StoryDiscPlace(int,const Vec4*,float);extern void Port_StoryDiscEnd(void);st_kien_pair();
 if(f<112){BtlCharApi_GetNodePos(st_obj(k),21,&p);p.y-=ksH*.2f;ksDiscFrom=p;Port_StoryDiscPlace(st_obj(k),&p,(f<25?f/25.f:1)*.5f);}
 else if(f<158){float t=(f-112)/35.f;if(f==112){BtlCharApi_GetNodePos(st_obj(n),17,&ksDiscTo);ksDiscTo=ks_offset(ksDiscTo,-3,0);}p=as_lerp(ksDiscFrom,ksDiscTo,t);Port_StoryDiscPlace(st_obj(k),&p,.5f);}else Port_StoryDiscEnd();
}
static int st_kien_tick(void){int f=sStKienFrame;st_kien_pair();st_kien_camera();if(f==0){extern int Port_StoryVoicePrepare(int);Port_StoryVoicePrepare(22);BtlCtrl_SetAuraOn(sStRun.cast[2]);}if(f==65)Port_StorySubtitle(6,ST_SET_KIENZAN_WARNING,0);if(f==133)Port_StorySubtitle(-1,0,0);if(f==112){BtlCtrl_SetAuraOff(sStRun.cast[2]);Port_StoryImpactSound(sStRun.cast[2],0xF);Port_Dbg3("story Kienzan released",f,sStRun.cast[2],sStRun.cast[8],0);}if(f==117)Port_StoryImpactSound(sStRun.cast[8],ST_SE_DASH);if(f==184){BtlCtrl_SetAuraOn(sStRun.cast[3]);Port_StoryImpactSound(sStRun.cast[3],ST_SE_DASH);Port_Dbg3("story shield arrival begins",6,7,sStRun.cast[3],0);}if(f==212)BtlCtrl_SetAuraOff(sStRun.cast[3]);sStKienFrame++;if(sStKienFrame>=230){extern void Port_StoryDiscEnd(void);Port_StoryDiscEnd();sStKienActive=0;sStKienDone=1;Port_Dbg3("story Kienzan hands to shield",6,7,0,0);return 1;}return 0;}

/* Native ISO285 death performance, not a generic falling/get-up loop. */
static Vec4 spRoot[2],spRot[2];
static int st_shield_post_owned(int actor){return sStRun.active&&sStRun.chapter==6&&sStEventPost&&sStRun.set==ST_SET_EVENT0+7&&(actor<0||actor==sStRun.cast[3]||actor==sStRun.cast[4]);}
static void st_shield_post_pose(int actor){int p=sStRun.cast[3],g=sStRun.cast[4],i=actor==g;Vec4 q;extern int Port_StoryScriptDamage(int);
 if(!sStShieldPostReady){int j;for(j=0;j<2;j++){int a=j?g:p;BtlCtrl_SetPos(a,&ksPos[j?4:3]);BtlCtrl_SetRot(a,&ksRot[j?4:3]);Port_StoryCommitSampledRoot(a);BtlCharApi_GetPos(st_obj(a),&spRoot[j]);BtlCharApi_GetRot(st_obj(a),&spRot[j]);Port_StoryCommitSampledRoot(a);st_scene_ground_actor(a);Port_StoryCommitSampledRoot(a);BtlCharApi_GetPos(st_obj(a),&spRoot[j]);}
  {float hp=st_height(p),dx,dz,len,sx,sz;Vec4 head,hip;
   /* Anchor to the evaluated lying head, never the object's waist/root. */
   Port_StorySampleMotion(p,213,20);st_ch5_ground_pose(p,spRoot[0].y);
   BtlCharApi_GetNodePos(st_obj(p),46,&head);BtlCharApi_GetNodePos(st_obj(p),3,&hip);
   dx=head.x-hip.x;dz=head.z-hip.z;len=sqrtf(dx*dx+dz*dz);
   if(len>.01f){dx/=len;dz/=len;}else{dx=sinf(spRot[0].y);dz=cosf(spRot[0].y);}
   sx=dz;sz=-dx;if(sx*cosf(spRot[0].y)-sz*sinf(spRot[0].y)<0){sx=-sx;sz=-sz;}
   spRoot[1]=spRoot[0];spRoot[1].x=head.x+(sx*.42f+dx*.1f)*hp;spRoot[1].z=head.z+(sz*.42f+dz*.1f)*hp;
   spRot[1]=spRot[0];spRot[1].y=atan2f(head.x-spRoot[1].x,head.z-spRoot[1].z);}
  st_wardrobe_queue(p);sStShieldPostReady=1;
  {int j;for(j=0;j<3;j++){int a=sStRun.cast[j==0?2:j==1?8:9];Vec4 q=j==0?spRoot[0]:ksPos[j==1?1:2];
   if(j==0){q.x+=cosf(spRot[0].y)*st_height(p)*2.6f;q.z-=sinf(spRot[0].y)*st_height(p)*2.6f;} /* Juan: Krillin further from Piccolo/Gohan */
   Port_StoryEnterActor(a);st_scene_take(a,1);BtlCtrl_SetPos(a,&q);Port_StoryCommitSampledRoot(a);st_scene_ground_actor(a);Port_StoryCommitSampledRoot(a);BtlCharApi_GetPos(st_obj(a),&ksPos[j==0?0:j==1?1:2]);st_face(a,p);BtlCharApi_GetRot(st_obj(a),&ksRot[j==0?0:j==1?1:2]);}}
}
 q=spRoot[i];BtlCtrl_SetPos(actor,&q);BtlCtrl_SetRot(actor,&spRot[i]);if(i){extern int Port_StorySampleOwnMotionQ8(int,int,int);int fr=sStShieldPostAge<30?sStShieldPostAge*2:60;{extern void Port_StoryKneelUpper(void*,int);void*o=BtlObj_Get(st_obj(actor));Port_StorySampleOwnMotionQ8(actor,388,(fr<24?fr:24)*256);Port_StoryKneelUpper(o,1);Port_StorySampleOwnMotionQ8(actor,388,fr*256);Port_StoryKneelUpper(o,0);}}else Port_StorySampleMotion(actor,213,20);st_ch5_ground_pose(actor,q.y); /* Juan: face up, Gohan kneeling beside */
}
static void st_shield_post_camera(void){
 int a=sStRun.index==1||sStRun.index==3?sStRun.cast[3]:sStRun.cast[4];Vec4 at,eye,r;float h=st_height(a);
 BtlCharApi_GetNodePos(st_obj(a),46,&at);BtlCharApi_GetRot(st_obj(a),&r);eye=at;
 if(a==sStRun.cast[4]){ /* low front three-quarter: see his eyes beneath the fringe */
  eye.x+=(sinf(r.y)*.8f+cosf(r.y)*.25f)*h;eye.z+=(cosf(r.y)*.8f-sinf(r.y)*.25f)*h;eye.y-=h*.03f; /* near-frontal rather than exposing the eyelid edge in profile */
  if(eye.y>spRoot[1].y-h*.4f)eye.y=spRoot[1].y-h*.4f;
 }else{eye.x+=(sinf(r.y)*1.1f+cosf(r.y)*.55f)*h;eye.z+=(cosf(r.y)*1.1f-sinf(r.y)*.55f)*h;eye.y-=h*.2f;if(eye.y>spRoot[0].y-h*.45f)eye.y=spRoot[0].y-h*.45f;}
 sStCamDrift=0;st_camera_finish(eye,at,h,a,1);
}

static Vec4 rsRoot[3],rsRot[3];Vec4 rsCloud;
static float rsH;
static int rsExhaustAge;
static const int rsCast[3]={4,8,6};
static int st_rescue_role(int a){int i;for(i=0;i<3;i++)if(a==sStRun.cast[rsCast[i]])return i;return -1;}
int Port_StoryNimbusInfo(float*q){if(!((sStRescueActive&&sStRescueFrame>=30)||(sStRescueDone&&sStRun.active&&sStRun.chapter==6&&sStRun.set==ST_SET_EVENT0+8&&sStEventPost)))return 0; /* stays under Gohan to the end */q[0]=rsCloud.x;q[1]=rsCloud.y;q[2]=rsCloud.z;q[3]=rsH*.7f;q[4]=rsRot[0].y+1.570796327f;return 1;}
static void st_rescue_pose(int a){int i=st_rescue_role(a),f=sStRescueFrame,m=0,fr=0;float t;Vec4 p,r;if(i<0)return;p=rsRoot[i];r=rsRot[i];
 if(sStRescueDone&&i==0){Vec4 pelvis,d;extern void Port_StoryOffsetVisual(int,const Vec4*);p=rsCloud;p.y-=rsH*.15f;BtlCtrl_SetPos(a,&p);BtlCtrl_SetRot(a,&r);st_ch8_clip(a,536,227,10*256);
  BtlCharApi_GetNodePos(st_obj(a),3,&pelvis);d=(Vec4){p.x-pelvis.x,p.y-rsH*.35f-pelvis.y,p.z-pelvis.z,0};Port_StoryOffsetVisual(st_obj(a),&d);return;}
 if(sStRescueDone){if(i==1){p=as_lerp(rsRoot[1],rsRoot[0],.82f);r.y=atan2f(rsCloud.x-p.x,rsCloud.z-p.z);}BtlCtrl_SetPos(a,&p);BtlCtrl_SetRot(a,&r);Port_StorySampleMotion(a,0,0);st_ch5_ground_pose(a,p.y);return;}
 if(i==1){t=f/70.f;if(t>1)t=1;t=t*t;p=as_lerp(rsRoot[1],rsRoot[0],t*.82f);m=f<72?ST_MOTION_FLIGHT:0;fr=f<72?f:0;if(f>=72){p=as_lerp(rsRoot[1],rsRoot[0],.82f);r.y=atan2f(rsCloud.x-p.x,rsCloud.z-p.z);m=241;fr=6;}} /* dash, charge in hand (FX hook) */
 if(i==0&&f<77){m=241;fr=6;} /* paralysed by fear as Nappa comes */
 if(i==0&&f>=77){p=rsCloud;p.y-=rsH*.15f;m=226;fr=40;} /* on the cloud on hands and knees, face down */
 if(i==2){t=(f-90)/52.f;if(t<0)t=0;if(t>1)t=1;p.y-=rsH*5*(1-t)*(1-t);m=f<142?0x2F:0;}
 BtlCtrl_SetPos(a,&p);BtlCtrl_SetRot(a,&r);if(i==0&&f>=77)st_ch8_clip(a,536,227,10*256);else if(i==1){extern int Port_StorySampleOwnMotionQ8(int,int,int);extern void Port_StoryBlendSampledPose(void*,int,int);void*o=BtlObj_Get(st_obj(a));if(f<72)Port_StorySampleOwnMotionQ8(a,0x85,(f%30)*256);else{if(f<90){Port_StorySampleOwnMotionQ8(a,0x85,20*256);Port_StoryBlendSampledPose(o,1,4096);}Port_StorySampleOwnMotionQ8(a,0,0);if(f<90)Port_StoryBlendSampledPose(o,0,(90-f)*4096/18);}}else Port_StorySampleMotion(a,m,fr);if(i==0&&f>=77){Vec4 pelvis,d;extern void Port_StoryOffsetVisual(int,const Vec4*);BtlCharApi_GetNodePos(st_obj(a),3,&pelvis);d=(Vec4){p.x-pelvis.x,p.y-rsH*.35f-pelvis.y,p.z-pelvis.z,0};Port_StoryOffsetVisual(st_obj(a),&d);}if((i==0&&f<77)||(i==2&&f>=142))st_ch5_ground_pose(a,p.y);
}
static void st_rescue_pair(void){int i,f=sStRescueFrame;float t=(f-30)/70.f;if(t<0)t=0;if(t>1)t=1;rsCloud=rsRoot[0];rsCloud.x+=cosf(rsRot[0].y)*rsH*(-4+6*t);rsCloud.z-=sinf(rsRot[0].y)*rsH*(-4+6*t);rsCloud.y-=rsH*(.1f+.8f*t);for(i=0;i<3;i++)st_rescue_pose(sStRun.cast[rsCast[i]]);}

static void st_rescue_begin(void){int i,g=sStRun.cast[4],n=sStRun.cast[8],hero=sStRun.cast[6];Vec4 p,r;rsH=st_height(g);Port_StoryEnterActor(hero);st_scene_take(hero,1);
 p=spRoot[1];r=spRot[0];BtlCtrl_SetPos(g,&p);BtlCtrl_SetRot(g,&r);Port_StoryCommitSampledRoot(g);Port_StorySampleMotion(g,0,0);st_scene_ground_actor(g);Port_StoryCommitSampledRoot(g);BtlCharApi_GetPos(st_obj(g),&p);rsRoot[0]=p;rsRot[0]=r;
 BtlCtrl_SetPos(n,&ksPos[1]);Port_StoryCommitSampledRoot(n);st_face(n,g);st_scene_ground_actor(n);Port_StoryCommitSampledRoot(n);BtlCharApi_GetPos(st_obj(n),&rsRoot[1]);BtlCharApi_GetRot(st_obj(n),&rsRot[1]);
 st_place_near(hero,g,-2.5f,-3.0f,0);Port_StoryCommitSampledRoot(hero);st_scene_ground_actor(hero);Port_StoryCommitSampledRoot(hero);st_face(hero,n);BtlCharApi_GetPos(st_obj(hero),&rsRoot[2]);BtlCharApi_GetRot(st_obj(hero),&rsRot[2]);
 for(i=0;i<3;i++)st_scene_take(sStRun.cast[rsCast[i]],1);sStRescueFrame=rsExhaustAge=0;sStRescueActive=1;Port_Dbg3("story Nimbus rescue begins",g,n,hero,0);}
static void st_rescue_camera(void){int f=sStRescueFrame,a=sStRun.cast[f<90?4:6];Vec4 at,eye;float h=st_height(a);BtlCharApi_GetNodePos(st_obj(a),17,&at);eye=at;eye.x+=h*2.1f;eye.z+=h*1.4f;eye.y-=h*.3f;sStCamDrift=0;st_camera_finish(eye,at,h,a,5);}
static int st_rescue_tick(void){int f=sStRescueFrame;st_rescue_pair();st_rescue_camera();if(f==40)Port_StoryImpactSound(sStRun.cast[4],ST_SE_DASH);if(f==142)Port_StoryBodyImpact(sStRun.cast[6]);sStRescueFrame++;if(sStRescueFrame>=160){int i;sStRescueActive=0;sStRescueDone=1;for(i=0;i<3;i++)BtlCtrl_StopMotion(sStRun.cast[rsCast[i]]);Port_Dbg3("story Nimbus rescue complete",6,8,sStRun.cast[6],0);return 1;}return 0;}

/* Stable observers, outside the performance's actor-pair placement hooks. */
static void st_shield_observers(void){int j;if(!sStShieldPostReady)return;
 if(!(st_shield_post_owned(-1)||(sStRun.active&&sStRun.chapter==6&&sStEventPost&&sStRun.set==ST_SET_EVENT0+8)))return;
 for(j=0;j<3;j++){int idx=j==0?0:j==1?1:2,a=sStRun.cast[j==0?2:j==1?8:9];
  if(j==1&&sStRescueDone)continue;st_scene_take(a,1);BtlCtrl_SetPos(a,&ksPos[idx]);BtlCtrl_SetRot(a,&ksRot[idx]);Port_StorySampleMotion(a,0,0);st_ch5_ground_pose(a,ksPos[idx].y);}
}
static void st_shield_reaction(int actor){int age=sStShieldHitAge;Vec4 d;
 st_ch8_clip(actor,2752,285,(28+(age>0&&age<65?(age/4)%4:0))*256);
 if(age>0){extern void Port_StoryFaceFrame(int,int),Port_StoryShieldRecoil(void*,int);Port_StoryShieldRecoil(BtlObj_Get(st_obj(actor)),age);Port_StoryFaceFrame(actor,10);}
 if(age>0&&age<65){extern void Port_StoryOffsetVisual(int,const Vec4*);float h=st_height(actor);d=(Vec4){sinf(age*1.7f)*h*.008f,0,cosf(age*1.3f)*h*.008f,0};Port_StoryOffsetVisual(st_obj(actor),&d);}
}
int Port_StoryChargeDim(void){if(st_chapter()!=6)return 0;if(sStKienActive&&sStKienFrame<126){int f=sStKienFrame;return f<30?f:f<112?30:(126-f)*30/14;}if(sStPowerActive)return 25;return 0;}
static int st_rescue_hand_charge(int actor)
{
    return sStRun.active && st_chapter()==6 && sStRescueActive &&
           sStRescueFrame<72 && actor==sStRun.cast[8];
}

int Port_StoryChargeHandActive(int obj)
{
    if(st_chapter()!=6)return 0;
    if(sStKienActive&&sStKienFrame<112&&obj==st_obj(sStRun.cast[2]))return 1;
    return st_rescue_hand_charge(sStRun.cast[8])&&obj==st_obj(sStRun.cast[8]);
}

/* This clock keeps moving through the dialogue after the cloud stops. */
int Port_StoryNimbusExhaustAge(void){return rsExhaustAge;}

static void st_rescue_exhaust(void)
{
    if(sStRescueFrame>=30&&++rsExhaustAge>=64)rsExhaustAge=0;
}
int Port_StoryRescueAura(int obj){return st_chapter()==6&&sStRescueActive&&obj==st_obj(sStRun.cast[8]);}
static void st_power_tick(void){int g=sStRun.cast[6],v=sStRun.cast[9],f=sStPowerFrame++;Vec4 at,eye,rot;float h=st_height(g);
 Port_StorySampleMotion(g,52,f<40?f:40);Port_StoryCommitSampledRoot(g);st_scene_ground_actor(g);BtlCharApi_GetRot(st_obj(g),&rot);BtlCharApi_GetNodePos(st_obj(g),17,&at);eye=at;eye.x+=(sinf(rot.y)*2.2f+cosf(rot.y)*.7f)*h;eye.z+=(cosf(rot.y)*2.2f-sinf(rot.y)*.7f)*h;eye.y-=h*.7f;sStCamDrift=0;st_camera_finish(eye,at,h,g,5);
 if(f==0){BtlCtrl_SetAuraOn(g);Port_Dbg3("story CH8 over8000 begins",g,9,0,0);}
 if(f<2){int j,al[2]={sStRun.cast[2],sStRun.cast[4]};for(j=0;j<2;j++)if(al[j]>=0){st_place_near(al[j],g,-3.2f,j?2.4f:-2.4f,0);Port_StoryCommitSampledRoot(al[j]);st_scene_ground_actor(al[j]);Port_StoryCommitSampledRoot(al[j]);st_face(al[j],g);}} /* Juan: Gohan and Krillin well back from the charge */
 if(f==55){TdEvent scar;Vec4 p;BtlCharApi_GetPos(st_obj(g),&p);memset(&scar,0,sizeof(scar));scar.owner=g;scar.fx=1;scar.ax=scar.bx=p.x;scar.az=scar.bz=p.z;scar.ay=BtlCharApi_GetGroundY(st_obj(g));scar.r=h*.95f;scar.depth=h*.13f;Port_TerrainImpact(&scar);BtlFacade_ShakeCamera(.35f);}
 /* f70 power subtitle removed: Vegeta's line is now the event line itself (Juan 2026-11-18) */
 if(f==145){BtlCtrl_SetAuraOff(g);BtlFacade_ShakeCamera(0);sStPowerActive=0;sStPowerDone=1;Port_StorySubtitle(-1,0,0);Port_Dbg3("story CH8 over8000 complete",g,9,0,0);}
 (void)v;
}
