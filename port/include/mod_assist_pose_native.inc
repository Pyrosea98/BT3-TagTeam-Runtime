/* Presentation-only assist stance. The fighter keeps action 7 and its idle
   event stream; these sampled rotations cannot launch an attack or restore Ki.
   Dedicated caches leave both native animation layers untouched. */
extern int Port_AssistVisual(int objectId,int *ticks);
extern int Port_AssistRushMotion(int objectId);
extern int Port_AssistQteSample(int objectId,int *anim,float *ratio);
typedef struct PortAssistPoseCache {
    void *source;
    int kind,valid;
    u8 data[BOBJ_WORK_LAYER_SIZE];
} PortAssistPoseCache;
static PortAssistPoseCache sAssistPose[12];

static int Port_AssistPoseValid(BobjAnim *a,u32 size) {
    int i,j;u32 off,bytes,previous,frame;
    if(size<0x94||size>BOBJ_WORK_LAYER_SIZE||!a->length)return 0;
    for(i=0;i<BOBJ_NODE_MAX;i++){
        off=(u32)a->track[i]*4;if(!off)continue;
        if(off>size-4)return 0;
        {BobjTrack*t=TRACK_AT(a,a->track[i]);
         if(!t->count)return 0;
         bytes=4+(u32)t->count*(BOBJ_TRACK_ROT_ONLY(t)?10:24);
         if(bytes>size-off)return 0;
         previous=0;
         for(j=0;j<t->count;j++){
             frame=BOBJ_TRACK_ROT_ONLY(t)?((u16*)((u8*)t+4+t->count*8))[j]:t->u.key[j].frame;
             if(frame>a->length||(j&&frame<=previous))return 0;
             previous=frame;
         }
        }
    }
    return 1;
}

static void Port_AssistPose(BobjObj *obj) {
    int id,kind,ticks=0,size=0,anim,qte=0;float ratio=0;u32 decoded;float frame,weight;
    PortAssistPoseCache*c;BobjAnim*a;BobjBone*bone;void*source;
    if(obj->type!=0||!obj->work)return;
    id=*(s32*)((u8*)obj+0x10);if(id<0||id>=12)return;
    c=&sAssistPose[id];kind=Port_AssistVisual(id,&ticks);
    if(!kind){c->source=NULL;c->valid=0;return;}
    anim=kind==2?Port_AssistRushMotion(id):364;
    if(kind==2)qte=Port_AssistQteSample(id,&anim,&ratio);
    source=obj->mdl.anims[anim];
    if(!source||*(u32*)source<0x94||*(u32*)source>BOBJ_WORK_LAYER_SIZE){anim=53;source=obj->mdl.anims[anim];}
    if(!source)return;
    if(c->source!=source||c->kind!=kind){
        c->source=source;c->kind=kind;c->valid=0;
        decoded=*(u32*)source;
        if(decoded<0x94||decoded>sizeof(c->data))return;
        Bpe_Decode(source,c->data,&size);
        if(size!=(int)decoded||!Port_AssistPoseValid((BobjAnim*)c->data,decoded))return;
        c->valid=1;
    }
    if(!c->valid)return;
    a=(BobjAnim*)c->data;frame=qte?ratio*a->length:(float)(ticks%a->length);
    weight=qte?1.0f:ticks<5?ticks*.2f:1.0f;
    bone=obj->mdl.nodes;
    while(1){
        /* Rush includes legs/torso for punches and kicks. World root, tail and
           all translations stay native; beam/ultimate remain upper body only. */
        if(bone->id>=(kind==2?1:16)&&bone->id<=48&&a->track[bone->id]){
            BobjTrack*t=TRACK_AT(a,a->track[bone->id]);
            Quat q;Vec4 unused;u16 cursor=0;BobjPart*p=BtlObj_GetNode(obj,bone->id);
            if(BOBJ_TRACK_ROT_ONLY(t))BtlObjAnim_SampleRot(t,&q,&cursor,frame);
            else BtlObjAnim_SamplePosRot(t,&unused,&q,&cursor,frame);
            Quat_Slerp(&p->rot,&p->rot,&q,weight);
        }
        if(bone->last)break;
        bone=(BobjBone*)((u8*)bone+bone->next);
    }
}
