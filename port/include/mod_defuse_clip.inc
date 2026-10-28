/* Whether fighter object `objectId` carries motion `anim` with a plausible compressed size (the same bound the assist
   pose uses): the defuse cinematic skips a beat whose stock clip is empty instead of letting its action end at once. */
extern void *BtlObj_Get(s32 id);
int Port_DefuseClipOk(int objectId,int anim){
    BobjObj *obj;
    void *source;
    if(anim<0||anim>=BOBJ_ANIM_MAX)return 0;
    obj=(BobjObj*)BtlObj_Get(objectId);
    if(!obj)return 0;
    source=obj->mdl.anims[anim];
    return source&&*(u32*)source>=0x94&&*(u32*)source<=BOBJ_WORK_LAYER_SIZE;
}

/* The defuse cinematic's overexposure: the stock white hit flash held at `strength` (0..1 of its full 0x80 blend
   over the model's colour tables) for this frame. 0 lets a running flash expire on its own. */
void Port_DefuseWhiten(int objectId,float strength){
    BobjObj *obj=(BobjObj*)BtlObj_Get(objectId);
    BobjFlash *flash;
    if(!obj||!(strength>0.0f))return;
    if(strength>1.0f)strength=1.0f;
    flash=BtlObjFlash_Get(obj);
    if(!flash)return;
    Vec4_Set(&flash->color,255.0f,255.0f,255.0f,strength*128.0f);
    flash->time=0.2f;
}

/* Node `node` of fighter object `objectId` this frame: its world position, and its own x (right) and front axes
   (the models face their node's -z: the rest pose has no rotation). 0 when the object or node is missing. */
int Port_DefuseNodeFrame(int objectId,int node,float *pos,float *front,float *right){
    BobjObj *obj=(BobjObj*)BtlObj_Get(objectId);
    BobjPart *part;
    int i;
    if(!obj)return 0;
    part=BtlObj_GetNode(obj,node);
    if(!part||part->id!=(u32)node)return 0;
    for(i=0;i<3;i++){
        pos[i]=part->mtx.m[3][i];front[i]=-part->mtx.m[2][i];right[i]=part->mtx.m[0][i];
        if(!(pos[i]>=-1000000.0f&&pos[i]<=1000000.0f))return 0;
    }
    return 1;
}

/* ---- the crack's surface points (mod_defuse_crack.h) ------------------------------------------------------------
   The same choice as tools/defuse-previewer (buildCrack): PD_CRACK_LEVELS heights from a little below the top of the
   head down to the hips (node 2), at each the front-most vertex (smallest z) of the model near a zigzag x, read from
   the model's own parts: every node record of mdl.nodes is followed by its VIF chain (tag at +96, vertex packets of
   three quadwords: position + weight, normal, uv), exactly the layout of the model file. */
#include "mod_defuse_crack.h"
#define PD_CRACK_LEVELS 18
typedef struct PdScan {int mode,levels;float top,start,hip,tx[PD_CRACK_LEVELS+1],ty[PD_CRACK_LEVELS+1],best[PD_CRACK_LEVELS+1];
 PortCrackPt pick[PD_CRACK_LEVELS+1];int found[PD_CRACK_LEVELS+1];} PdScan;
static u32 pd_u32(const u8 *p){return (u32)p[0]|((u32)p[1]<<8)|((u32)p[2]<<16)|((u32)p[3]<<24);}
static float pd_abs(float v){return v<0.0f?-v:v;}
static void pd_vertex(PdScan *s,const float *v,BobjBone *bone,BobjBone *parent){int k;
 if(!(v[1]>=-100000.0f&&v[1]<=100000.0f))return;
 if(s->mode==0){if(v[1]<s->top)s->top=v[1];return;}
 for(k=0;k<=s->levels;k++){float dx=v[0]-s->tx[k],score;
  if(pd_abs(v[1]-s->ty[k])>0.55f||pd_abs(dx)>0.55f*1.6f)continue;
  score=v[2]+pd_abs(dx)*0.15f;if(s->found[k]&&score>=s->best[k])continue;
  s->found[k]=1;s->best[k]=score;
  {PortCrackPt *q=&s->pick[k];int i;q->node=bone->id;q->parent=parent?parent->id:bone->id;q->w=v[3]<0.0f?0.0f:v[3]>1.0f?1.0f:v[3];
   for(i=0;i<3;i++){q->p[i]=v[i];q->n[i]=v[4+i];q->origin[i]=(&bone->origin.x)[i];q->porigin[i]=parent?(&parent->origin.x)[i]:(&bone->origin.x)[i];}}}}
static int pd_scan(BobjObj *obj,PdScan *s){BobjBone *bone=obj->mdl.nodes,*stack[64];int depth=0,parts=0;
 if(!bone)return 0;
 for(;;){const u8 *rec=(const u8*)bone;u32 size=(u32)bone->next,nv=bone->unk08,pop=bone->pop;BobjBone *parent=depth?stack[depth-1]:NULL;
  if(size<64||size>0x100000)return 0;
  if(nv&&size>=128){u32 tag=pd_u32(rec+96),cur=104,end=112+(tag&0xFFFF)*16;const u8 *chunk=NULL;u32 chunkN=0;
   if(((tag>>28)&7)==6&&end+16<=size){
    while(cur+4<=end){u32 word=pd_u32(rec+cur),cmd=(word>>24)&0x7F,num=(word>>16)&0xFF,imm=word&0xFFFF;cur+=4;
     if(cmd==0)continue;
     if(cmd==0x6C){if(!num||cur+num*16>end)break;if(imm==0x8005){chunk=rec+cur;chunkN=num/3;}else if(imm!=0x8000)break;cur+=num*16;continue;}
     if(cmd==0x17){u32 i;if(chunk)for(i=0;i<chunkN;i++){float v[12];int j;for(j=0;j<12;j++){union {u32 u;float f;} bits;bits.u=pd_u32(chunk+i*48+j*4);v[j]=bits.f;}pd_vertex(s,v,bone,parent);}chunk=NULL;continue;}
     break;}}}
  if(depth<64)stack[depth++]=bone;
  if(pop>(u32)depth)return 0;depth-=pop;parts++;
  if(bone->last)break;
  bone=(BobjBone*)((u8*)bone+size);
 }
 return parts;}
/* The fused fighter's crack: up to `max` points, top to bottom; 0 when the model cannot be read. */
int Port_DefuseCrackBuild(int objectId,PortCrackPt *out,int max){
 static PdScan s;BobjObj *obj=(BobjObj*)BtlObj_Get(objectId);BobjBone *bone;float head=0.0f,hip=0.0f;int k,n=0,haveHead=0,haveHip=0;u32 seed=7;
 if(!obj||!obj->mdl.nodes)return 0;
 for(bone=obj->mdl.nodes;;bone=(BobjBone*)((u8*)bone+bone->next)){
  if(bone->id==48){head=bone->origin.y;haveHead=1;}
  if(bone->id==2){hip=bone->origin.y;haveHip=1;}
  if(bone->last||bone->next<64)break;}
 if(!haveHead||!haveHip)return 0;
 memset(&s,0,sizeof(s));s.hip=hip;s.top=1e9f;
 s.mode=0;if(!pd_scan(obj,&s)||s.top>=head)return 0;
 s.levels=PD_CRACK_LEVELS;s.start=head+(s.top-head)*0.55f;
 for(k=0;k<=s.levels;k++){float r;seed=seed*1664525u+1013904223u;r=(float)((double)seed/4294967296.0);
  s.ty[k]=s.start+(s.hip-s.start)*k/s.levels;s.tx[k]=(k%2?1.0f:-1.0f)*(0.25f+r*0.45f)*((k==0||k==s.levels)?0.0f:1.0f);}
 s.mode=1;if(!pd_scan(obj,&s))return 0;
 for(k=0;k<=s.levels&&n<max;k++){
  if(!s.found[k])continue;
  if(n&&out[n-1].p[0]==s.pick[k].p[0]&&out[n-1].p[1]==s.pick[k].p[1]&&out[n-1].p[2]==s.pick[k].p[2])continue;
  out[n++]=s.pick[k];}
 return n>=2?n:0;}
/* Where crack point `q` of fighter object `objectId` is this frame, and its normal (skinned as the game skins it). */
int Port_DefuseCrackPlace(int objectId,const PortCrackPt *q,float *pos,float *normal){
 BobjObj *obj=(BobjObj*)BtlObj_Get(objectId);BobjPart *a,*b;int i,j;float la[3],lb[3];
 if(!obj)return 0;a=BtlObj_GetNode(obj,q->node);b=BtlObj_GetNode(obj,q->parent);if(!a||!b)return 0;
 for(i=0;i<3;i++){la[i]=q->p[i]-q->origin[i];lb[i]=q->p[i]-q->porigin[i];}
 for(j=0;j<3;j++){float pa=a->mtx.m[3][j],pb=b->mtx.m[3][j],na=0.0f,nb=0.0f;
  for(i=0;i<3;i++){pa+=la[i]*a->mtx.m[i][j];pb+=lb[i]*b->mtx.m[i][j];na+=q->n[i]*a->mtx.m[i][j];nb+=q->n[i]*b->mtx.m[i][j];}
  pos[j]=pa*q->w+pb*(1.0f-q->w);normal[j]=na*q->w+nb*(1.0f-q->w);
  if(!(pos[j]>=-1000000.0f&&pos[j]<=1000000.0f))return 0;}
 return 1;}
