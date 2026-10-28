/* Native larger giants: multiply the current body's own dimensions, never asset data.
   Size class4 is the live parameter byte, so transformations and body exchanges follow it. */
extern int Port_Setting(const char*,int),Port_NetActive(void),BattleReplay_IsActive(void);
float Port_GiantScale(const void*raw){
 const BobjObj*obj=(const BobjObj*)raw;int p;
 if(!obj||obj->type!=0||!obj->work||!obj->mdl.param||((const u8*)obj->mdl.param)[2]!=4)return 1;
 if(Port_NetActive()||BattleReplay_IsActive()||!Port_Setting("canonical_giants",1))return 1;
 p=Port_Setting("giant_size_percent",400);if(p<100)p=100;if(p>400)p=400;return p*.01f;
}
float Port_GiantCameraScale(const void*obj){
 int p;if(Port_GiantScale(obj)==1)return 1;p=Port_Setting("giant_camera_distance_percent",150);if(p<100)p=100;if(p>300)p=300;return p*.01f;
}
static void Port_GiantBodyDimensions(BobjObj*obj){
 f32 f;int i;if(obj->type!=0||!obj->work||!obj->mdl.model||!obj->mdl.param||((u8*)obj->mdl.param)[2]!=4)return;
 f=Port_GiantScale(obj);for(i=0;i<4;i++)obj->body.size[i]=obj->mdl.model->size[i]*f;
 obj->body.radius=(obj->mdl.param->radius>0?obj->mdl.param->radius:obj->mdl.model->size[3]*2)*f;
 if(obj->mdl.body)obj->body.unkA0=obj->mdl.body->unk04*f;
}
/* Keep unit OBB query axes and scaled half extents. World corners/bounds were
   already built by the scaled node matrix and must not be multiplied twice. */
static void Port_GiantBodyBoxes(BobjObj*obj){
 f32 f=Port_GiantScale(obj);BobjBodyPart*p;int i,j;
 if(f==1||!obj->body.parts)return;
 for(p=obj->body.parts;;p++){
  p->box.unk10.x=p->box.local[0].x*f;p->box.unk10.y=p->box.local[0].y*f;p->box.unk10.z=p->box.local[0].z*f;
  for(i=0;i<3;i++)for(j=0;j<3;j++){((f32*)&p->box.axis[i])[j]/=f;p->box.mtx.m[i][j]/=f;}
  if(p->flags&1)break;
 }
}
/* The scaled pose is visual/collision only. Preserve native root displacement
   when the manager reads it back into movement; do not accelerate locomotion. */
void Port_GiantRootPosition(const void*raw,Vec4*out,const Vec4*anchor){
 const BobjObj*obj=(const BobjObj*)raw;f32 f=Port_GiantScale(obj);int i;
 if(f==1){*out=obj->xf.rootPos;return;}
 for(i=0;i<3;i++)((f32*)out)[i]=((const f32*)anchor)[i]+(((const f32*)&obj->xf.rootPos)[i]-((const f32*)anchor)[i])/f;
 out->w=1;
}
