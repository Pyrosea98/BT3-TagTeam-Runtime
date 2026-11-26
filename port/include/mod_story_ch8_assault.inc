/* CH8 event4 prefix: one action sequence, then the existing Tien sacrifice. */
enum {AS_P,AS_K,AS_G,AS_T,AS_N,AS_COUNT};
static const int asCast[AS_COUNT]={3,2,4,0,8};
static Vec4 asRoot[AS_COUNT],asRot[AS_COUNT],asNHit,asKHit,asGoal,asFire,asFlee;
static float asYaw,asH;
static int st_assault_role(int actor){int i;for(i=0;i<AS_COUNT;i++)if(actor==sStRun.cast[asCast[i]])return i;return -1;}
static Vec4 as_offset(Vec4 p,float front,float side,float up){p.x+=(sinf(asYaw)*front+cosf(asYaw)*side)*asH;p.z+=(cosf(asYaw)*front-sinf(asYaw)*side)*asH;p.y-=up*asH;return p;}
static Vec4 as_lerp(Vec4 a,Vec4 b,float t){if(t<0)t=0;if(t>1)t=1;a.x+=(b.x-a.x)*t;a.y+=(b.y-a.y)*t;a.z+=(b.z-a.z)*t;return a;}
static float as_face(Vec4 a,Vec4 b){return atan2f(b.x-a.x,b.z-a.z);}
static void as_ground(Vec4*p){StgGroundHit hit;Vec4 probe=*p;probe.y-=asH*4;hit.y=3.4e38f;if(BtlStage_ProbeGround(-1,&probe,&hit)>=0&&hit.y>-1e9f&&hit.y<1e9f)p->y=hit.y;}
static int st_assault_charge_owns(int actor){return sStAssaultDone&&sStRun.active&&sStRun.chapter==6&&sStRun.set==ST_SET_EVENT0+4&&!sStEventPost&&sStAtkFrames<=0&&actor==sStRun.cast[0];}
static void st_assault_pose(int actor){int role=st_assault_role(actor),f=sStAssaultFrame,m=0,frame=0,air=0;Vec4 q,r;float t;if(role<0)return;q=asRoot[role];r=asRot[role];
 if(role==AS_P){if(f>=30&&f<=43){m=55;frame=f-30;}else if(f>=170&&f<258&&((f-170)%48)<12){m=113;frame=(f-170)%48;if(frame>9)frame=9;}if(f>=150)r.y=as_face(q,asFire);}
 if(role==AS_K){air=1;m=0x26;if(f>=81&&f<=90){m=55;frame=f-81;}else if(f>=194&&f<282&&((f-194)%48)<13){m=113;frame=(f-194)%48;}if(f>=150)r.y=as_face(q,asFire);}
 if(role==AS_G){if(f>=104&&f<134){m=241;frame=6;}else if(f>=134&&f<174){t=(f-134)/40.f;t=t*t*(3-2*t);q=as_lerp(q,asFlee,t);m=2;frame=(f-134)%13;r.y=as_face(asRoot[AS_G],asFlee);}else if(f>=174){q=asFlee;m=241;frame=6;r.y=as_face(q,asFire);}}
 if(role==AS_T&&f>=264){m=52;frame=f-264;if(frame>41)frame=41;}
 if(role==AS_N){if(f<38){q=asNHit;m=f<37?241:174;frame=f<37?6:0;}else if(f<=83){t=(f-38)/45.f;q=as_lerp(asNHit,asKHit,t);q.y-=asH*.25f*sinf(t*3.14159265f);m=195;frame=(f-38)%19;air=1;}else if(f<130){t=(f-84)/46.f;q=as_lerp(asKHit,asGoal,t);q.y-=asH*.25f*sinf(t*3.14159265f);m=f<118?195:0x26;frame=f<118?(f-84)%19:0;air=1;r.y=as_face(asKHit,asRoot[AS_G]);}else if(f<174){q=as_lerp(asGoal,asFire,(f-130)/44.f);r.y=as_face(q,asRoot[AS_G]);m=2;frame=(f-130)%13;}else{q=asFire;r.y=as_face(q,asRoot[AS_P]);if(f>=176&&f<272){int phase=(f-176)/24,df=(f-176)%24;float t=df<6?0:(df-6)/18.f;float side=sinf(t*3.14159265f)*.8f*(phase&1?-1:1);q=as_offset(q,0,side,0);m=phase&1?30:29;frame=df;if(frame>38)frame=38;}}}
 BtlCtrl_SetPos(actor,&q);BtlCtrl_SetRot(actor,&r);Port_StorySampleMotion(actor,m,frame);
 {extern void Port_StoryOffsetVisual(int,const Vec4*);Vec4 pelvis,d;BtlCharApi_GetNodePos(st_obj(actor),3,&pelvis);d=(Vec4){q.x-pelvis.x,0,q.z-pelvis.z,0};Port_StoryOffsetVisual(st_obj(actor),&d);}
 if(!air)st_ch5_ground_pose(actor,q.y);
}
static void st_assault_pair(void){int i;for(i=0;i<AS_COUNT;i++)st_assault_pose(sStRun.cast[asCast[i]]);}
static void st_assault_begin(void){int i,n=sStRun.cast[8],p=sStRun.cast[3],k=sStRun.cast[2],g=sStRun.cast[4];Vec4 q,r,hand,body;extern void Port_StoryTienArmOutfit(int);
 for(i=0;i<AS_COUNT;i++){int a=sStRun.cast[asCast[i]];Port_StoryEnterActor(a);st_scene_take(a,1);}
 if(BtlStage_GetStartPlace(1,&q,&r,0)<0){BtlCharApi_GetPos(st_obj(n),&q);BtlCharApi_GetRot(st_obj(n),&r);}BtlCtrl_SetPos(n,&q);BtlCtrl_SetRot(n,&r);Port_StoryCommitSampledRoot(n);st_scene_ground_actor(n);Port_StoryCommitSampledRoot(n);BtlCharApi_GetPos(st_obj(n),&q);asYaw=r.y;asH=st_height(n);
 asRoot[AS_N]=q;asRoot[AS_P]=as_offset(q,.95f,.2f,0);asRoot[AS_K]=as_offset(q,-2.6f,-1.7f,0);asRoot[AS_G]=as_offset(q,-.4f,2.8f,0);asRoot[AS_T]=as_offset(q,1.4f,2.2f,0);
 for(i=0;i<AS_COUNT;i++){int a=sStRun.cast[asCast[i]];BtlCtrl_SetPos(a,&asRoot[i]);Port_StorySampleMotion(a,0,0);Port_StoryCommitSampledRoot(a);st_scene_ground_actor(a);Port_StoryCommitSampledRoot(a);BtlCharApi_GetPos(st_obj(a),&asRoot[i]);asRot[i]=(Vec4){0,as_face(asRoot[i],asRoot[AS_N]),0,1};}
 asRoot[AS_K].y-=asH*.7f;asRot[AS_K].y=as_face(asRoot[AS_K],asRoot[AS_G]);
 asNHit=asRoot[AS_N];asKHit=asRoot[AS_K];asGoal=asRoot[AS_G];{float dx=asRoot[AS_K].x-asGoal.x,dz=asRoot[AS_K].z-asGoal.z,len=sqrtf(dx*dx+dz*dz);if(len>.001f){asGoal.x+=dx/len*asH*1.2f;asGoal.z+=dz/len*asH*1.2f;}}
 as_ground(&asGoal);asFire=as_lerp(asGoal,asRoot[AS_G],.25f);as_ground(&asFire);asFlee=as_offset(asRoot[AS_G],-1.7f,1.7f,0);as_ground(&asFlee);
 sStAssaultActive=1;sStAssaultFrame=37;sStAssaultShotFrame=-1;
 st_assault_pose(p);BtlCtrl_SetPos(n,&asNHit);Port_StoryCommitSampledRoot(n);Port_StorySampleMotion(n,241,6);BtlCharApi_GetNodePos(st_obj(p),21,&hand);BtlCharApi_GetNodePos(st_obj(n),17,&body);asNHit.x+=hand.x-body.x;asNHit.z+=hand.z-body.z;
 {float dx=asNHit.x-asRoot[AS_P].x,dz=asNHit.z-asRoot[AS_P].z,len=sqrtf(dx*dx+dz*dz),gap=asH*.7f;if(len<gap){float yaw=asRot[AS_P].y;asNHit.x=asRoot[AS_P].x+sinf(yaw)*gap;asNHit.z=asRoot[AS_P].z+cosf(yaw)*gap;}}
 sStAssaultFrame=83;st_assault_pose(k);BtlCtrl_SetPos(n,&asKHit);BtlCtrl_SetRot(n,&asRot[AS_N]);Port_StoryCommitSampledRoot(n);Port_StorySampleMotion(n,195,7);BtlCharApi_GetNodePos(st_obj(k),21,&hand);BtlCharApi_GetNodePos(st_obj(n),17,&body);asKHit.x+=hand.x-body.x;asKHit.y+=hand.y-body.y;asKHit.z+=hand.z-body.z;
 sStAssaultFrame=0;Port_StoryTienArmOutfit(sStRun.cast[0]);Port_Dbg3("story assault begins",p,k,g,n);
}
static void st_assault_camera(void){int f=sStAssaultFrame,who=sStRun.cast[3],kind=3;Vec4 at,eye,a,b;float h=asH;
 if(f<38){BtlCharApi_GetNodePos(st_obj(sStRun.cast[3]),17,&a);BtlCharApi_GetNodePos(st_obj(sStRun.cast[8]),17,&b);at=as_lerp(a,b,.5f);eye=as_offset(at,.25f,-1.9f,.3f);}
 else if(f<104){who=sStRun.cast[2];BtlCharApi_GetNodePos(st_obj(who),17,&a);BtlCharApi_GetNodePos(st_obj(sStRun.cast[8]),17,&b);at=as_lerp(a,b,.5f);eye=as_offset(at,-.3f,-1.9f,.3f);kind=5;}
 else if(f<134){Vec4 rot;who=sStRun.cast[4];BtlCharApi_GetNodePos(st_obj(who),46,&at);BtlCharApi_GetRot(st_obj(who),&rot);eye=at;h=st_height(who);eye.x+=(sinf(rot.y)*1.1f+cosf(rot.y)*.2f)*h;eye.z+=(cosf(rot.y)*1.1f-sinf(rot.y)*.2f)*h;kind=1;}
 else if(f<174){who=sStRun.cast[4];BtlCharApi_GetNodePos(st_obj(who),17,&a);BtlCharApi_GetNodePos(st_obj(sStRun.cast[8]),17,&b);at=as_lerp(a,b,.5f);eye=as_offset(at,.6f,1.8f,1.2f);kind=8;}
 else if(f<264){who=sStRun.cast[8];BtlCharApi_GetNodePos(st_obj(who),17,&at);eye=as_offset(at,.7f,1.8f,.25f);kind=3;}
 else{Vec4 rot;who=sStRun.cast[0];BtlCharApi_GetNodePos(st_obj(who),46,&at);BtlCharApi_GetRot(st_obj(who),&rot);eye=at;h=st_height(who);eye.x+=(sinf(rot.y)*1.1f+cosf(rot.y)*.4f)*h;eye.z+=(cosf(rot.y)*1.1f-sinf(rot.y)*.4f)*h;kind=5;}
 sStCamDrift=0;st_camera_finish(eye,at,h,who,kind);
}
static int st_assault_tick(void){int f=sStAssaultFrame,n=sStRun.cast[8];if(!st_wardrobe_ready(sStRun.cast[0]))return 0;st_assault_pair();st_assault_camera();
 if(f==37||f==83){Port_StoryBodyImpact(n);Port_StoryImpactSound(sStRun.cast[f==37?3:2],ST_SE_STRIKE);BtlFacade_ShakeCamera(.65f);Port_Dbg3("story assault contact",f,n,f==37?7:2,0);}
 if(f==43||f==89)BtlFacade_ShakeCamera(0);
 if(f==106)Port_StorySubtitle(6,ST_SET_EVENT0+6,1);if(f==174)Port_StorySubtitle(-1,0,0);if(f==286)Port_StorySubtitle(6,ST_SET_EVENT0+4,0);
 sStAssaultFrame++;if(sStAssaultFrame>=350){sStAssaultActive=0;sStAssaultDone=1;BtlFacade_ShakeCamera(0);Port_Dbg3("story assault hands to Tien sacrifice",6,4,sStRun.cast[0],n);return 1;}return 0;
}
static void st_assault_shots(void){int f=sStAssaultFrame;if(f==sStAssaultShotFrame)return;if(f==176||f==200||f==224||f==248){int actor=sStRun.cast[(f==176||f==224)?3:2],ok;Vec4 target;extern int Port_StoryVisualKiBlast(int,const Vec4*,int);BtlCharApi_GetNodePos(st_obj(sStRun.cast[8]),17,&target);ok=Port_StoryVisualKiBlast(st_obj(actor),&target,(int)(asH*.22f*256));Port_StoryImpactSound(actor,0xF);Port_Dbg3("story assault visual shot",actor,f,ok,0);sStAssaultShotFrame=f;}}

static void st_assault_handoff_camera(void){int a=sStRun.index==1?sStRun.cast[3]:sStRun.cast[0];Vec4 at,eye,r;float h=st_height(a);BtlCharApi_GetNodePos(st_obj(a),46,&at);BtlCharApi_GetRot(st_obj(a),&r);eye=at;eye.x+=(sinf(r.y)*.9f+cosf(r.y)*.3f)*h;eye.z+=(cosf(r.y)*.9f-sinf(r.y)*.3f)*h;sStCamDrift=0;st_camera_finish(eye,at,h,a,1);}
