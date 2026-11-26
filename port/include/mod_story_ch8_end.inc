/* CH8 bounded pickup -> airborne native blast. No gameplay throw requests. */
static Vec4 ceV,ceN,ceVR,ceNR,ceRelease;static int ceReady,ceReleased;
static void st_ch8_end_reset(void){ceReady=ceReleased=0;}
static void st_ch8_end_pair(void){
 int v=sStRun.cast[9],n=sStRun.cast[8],f=sStRun.frames;Vec4 p,hand,foot,nrot;float h=st_height(v),t;
 extern int Port_StorySampleOwnMotionQ8(int,int,int);extern void Port_StoryCommitSampledRoot(int);
 if(!ceReady||sStRun.index!=1)return;
 BtlCtrl_SetPos(v,&ceV);BtlCtrl_SetRot(v,&ceVR);
 if(f<64){int frame=f<24?f*16/24:f<48?16+(f-24)*4/24:20; /* retain the lift; omit the native downward slam */Port_StorySampleOwnMotionQ8(v,0x188,frame*256);st_ch5_ground_pose(v,ceV.y);}
 else if(f<100){Port_StorySampleOwnMotionQ8(v,0,0);st_ch5_ground_pose(v,ceV.y);}
 if(sStEndingFall>=24)return;
 p=ceN;
 if(f<64){
  BtlCtrl_SetPos(n,&p);BtlCtrl_SetRot(n,&ceNR);Port_StorySampleOwnMotionQ8(n,0xD6,20*256);Port_StoryCommitSampledRoot(n);
  if(f>=24){BtlCharApi_GetNodePos(st_obj(v),21,&hand);BtlCharApi_GetNodePos(st_obj(n),11,&foot);p.x+=hand.x-foot.x;p.y+=hand.y-foot.y;p.z+=hand.z-foot.z;}
  
 }else if(ceReleased){t=(f-64)/36.f;if(t>1)t=1;p=ceRelease;p.x+=sinf(ceVR.y)*h*1.6f*t;p.z+=cosf(ceVR.y)*h*1.6f*t;p.y-=h*(4.f*t-1.2f*t*t);}
 nrot=ceNR;if(f>=64){float turn=(f-64)/36.f;if(turn>1)turn=1;turn=turn*turn*(3-2*turn);nrot.y+=3.14159265f*turn;} /* face the caster while keeping the accepted airborne pose */
 BtlCtrl_SetPos(n,&p);BtlCtrl_SetRot(n,&nrot);Port_StoryCommitSampledRoot(n);
 if(f>=64&&f<74){extern void Port_StoryBlendSampledPose(void*,int,int);void*o=BtlObj_Get(st_obj(n));Port_StorySampleOwnMotionQ8(n,0xD6,20*256);Port_StoryBlendSampledPose(o,1,4096);Port_StorySampleOwnMotionQ8(n,0xC7,((f-64)/2)*256);Port_StoryBlendSampledPose(o,0,(74-f)*4096/10);}
 else Port_StorySampleOwnMotionQ8(n,f<64?0xD6:0xC7,(f<64?20:f<100?(f-64)/2:18)*256);
 if(f<24){st_ch5_ground_pose(n,ceN.y);BtlCharApi_GetPos(st_obj(n),&p);}
 sStEndingDrawAnchor[0]=ceV;sStEndingDrawAnchor[1]=p;
}
static void st_ch8_end_tick(void){
 int n=sStRun.cast[8],v=sStRun.cast[9],f=sStRun.frames;
 if(f==1){extern int Port_StoryVoicePrepare(int);ceReady=1;ceReleased=0;sStEndingExecutionHit=sStEndingFall=0;if(st_hp(n)<=0)Port_StoryReviveActor(n,1);if(st_hp(v)<=0)Port_StoryReviveActor(v,Port_StoryReserveHealth(v));Port_StoryVoicePrepare(80);
  ceV=sStEndingDrawAnchor[0];ceN=sStEndingDrawAnchor[1];BtlCtrl_SetPos(v,&ceV);BtlCtrl_SetPos(n,&ceN);st_face(v,n);BtlCharApi_GetRot(st_obj(v),&ceVR);ceNR=ceVR;st_ch8_end_pair();Port_Dbg3("story ending pickup begins",v,n,392,0);}
 if(f==64){ceRelease=sStEndingDrawAnchor[1];ceReleased=1;}
 st_ch8_end_pair();
 if(f==24){Port_StoryImpactSound(v,ST_SE_STRIKE);Port_Dbg3("story ending ankle grip",v,n,21,11);}
 if(f==64){Port_StoryImpactSound(v,ST_SE_DASH);Port_Dbg3("story ending upward release",v,n,64,0);}
 if(f==100){sStRun.actors&=~(1u<<v);BtlCtrl_StopMotion(v);st_face(v,n);gPortTarget[v]=n;Port_StoryUseTechnique(v,2);Port_Dbg3("story ending native blast",v,n,2,0);}
 if(sStEndingExecutionHit&&sStEndingFall<24){sStEndingFall++;if(sStEndingFall==24){st_set_hp(n,0);sStGoneActors|=1u<<n;sStDownActors&=~(1u<<n);sStRun.actors&=~(1u<<n);Port_StoryRemoveActor(n);Port_Dbg3("story ending Nappa disappears",v,n,f,0);}}
}
static void st_ch8_end_camera(void){
 int v=sStRun.cast[9],n=sStRun.cast[8],f=sStRun.frames;Vec4 at,eye,a,b,r;float h=st_height(v);
 if(!ceReady)return;if(sStEndingFall>=24){BtlFacade_SetFixedCamera(&sStRun.exitEye,&sStRun.exitRot);return;}
 if(f<64){BtlCharApi_GetNodePos(st_obj(v),17,&a);BtlCharApi_GetNodePos(st_obj(n),3,&b);at=as_lerp(a,b,.5f);eye=at;eye.x+=(sinf(ceVR.y)*1.5f+cosf(ceVR.y)*2.1f)*h;eye.z+=(cosf(ceVR.y)*1.5f-sinf(ceVR.y)*2.1f)*h;eye.y-=h*.6f;}
 else{Vec4 forward;h=st_height(n);if(!Port_StoryMouthPose(n,&at,&forward)){BtlCharApi_GetNodePos(st_obj(n),46,&at);forward=(Vec4){sinf(ceNR.y),0,cosf(ceNR.y),0};}eye=at;eye.x+=forward.x*h*1.1f+cosf(ceNR.y)*h*.3f;eye.y+=forward.y*h*1.1f;eye.z+=forward.z*h*1.1f-sinf(ceNR.y)*h*.3f;}
 sStCamDrift=0;st_camera_finish(eye,at,h,f<64?v:n,f<64?4:1);
}
