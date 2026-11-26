/* Private review route for registered EftBlast tasks, never an arbitrary
 * type1 task cast. Geometry/resources remain owned by the native task tree. */
typedef struct PortBigBeam {
    u32 task,work,original,epoch;
    int redirected,tier,age,limit;
    /* Complete native EftHitSrc (through aimFlags+0x40, aligned to16). The
     * EftJSrc declaration is only its renderer-side partial view. */
    u8 source[0x50] __attribute__((aligned(16)));
} PortBigBeam;
static PortBigBeam sBigBeam[64];
static u32 sBigBeamEpoch=1;
int Port_BigBeamReturned(void *src){
    extern int Port_BigShotSource(void*),Port_BigDiscSource(void*),Port_BigRushSource(void*);int i;
    for(i=0;i<64;i++)if(sBigBeam[i].task&&sBigBeam[i].epoch==sBigBeamEpoch&&sBigBeam[i].redirected&&src==sBigBeam[i].source)return 1;
    return Port_BigShotSource(src)||Port_BigDiscSource(src)||Port_BigRushSource(src);
}
void Port_BigBeamReset(void){extern void Port_BigShotReset(void),Port_BigDiscReset(void),Port_BigRushReset(void);if(++sBigBeamEpoch==0)sBigBeamEpoch=1;Port_BigShotReset();Port_BigDiscReset();Port_BigRushReset();}
static PortBigBeam *big_beam_find(EftJTask *task){
    int i;for(i=0;i<64;i++)if(sBigBeam[i].task==(u32)task)return &sBigBeam[i];return NULL;
}
static void big_beam_register(EftJTask *task){
    extern int Port_BloodReviewEnabled(int);int i;PortBigBeam *b;EftBlast *w=task->work;
    if(!Port_BloodReviewEnabled(1))return;
    b=big_beam_find(task);
    if(!b){for(i=0;i<64&&sBigBeam[i].task;i++);if(i==64)return;b=&sBigBeam[i];}
    memset(b,0,sizeof(*b));b->task=(u32)task;b->work=(u32)w;b->original=(u32)w->src;b->epoch=sBigBeamEpoch;
}
int Port_BigBeamReady(void *ptr){
    PortBigBeam *b=big_beam_find(ptr);EftJTask *task=ptr;EftBlast *w;
    if(!b){extern int Port_BigShotReady(void*),Port_BigDiscReady(void*),Port_BigRushReady(void*);return Port_BigShotReady(ptr)||Port_BigDiscReady(ptr)||Port_BigRushReady(ptr);}
    if(b->redirected||b->epoch!=sBigBeamEpoch||!task||b->work!=(u32)task->work)return 0;
    w=task->work;
    return (w->flags&0x10)&&!(w->flags&3)&&w->speed>.001f&&w->speed<1000.f&&
        (u32)w->src==b->original&&w->src&&w->src->unk4>=2&&w->src->unk4<=4;
}
int Port_BigBeamTry(void *ptr,int defender,void *hit,const void *point){
    extern int Port_BigBeamAccept(int,void*,int);
    extern void BtlCharApi_GetDeflectDir(s32,EftJVec*);
    extern void BtlCharApi_GetNodePos(s32,s32,void*);
    extern void Port_TargetHit(int,int),Port_Dbg3(const char*,int,int,int,int);
    EftJTask *task=ptr;PortBigBeam *b;EftBlast *w;EftJSrc *src,*copy;EftJVec dir;float len;int tier,j,owner;
    const EftJVec *p=point;
    if(!big_beam_find(ptr)){extern int Port_BigShotTry(void*,int,void*,const void*),Port_BigDiscTry(void*,int,void*,const void*),Port_BigRushTry(void*,int,void*,const void*);
        int tier=Port_BigShotTry(ptr,defender,hit,point);if(tier)return tier;
        tier=Port_BigDiscTry(ptr,defender,hit,point);return tier?tier:Port_BigRushTry(ptr,defender,hit,point);}
    if(!Port_BigBeamReady(ptr)||!p||defender<0||defender>=16)return 0;
    if(!(p->x>=-1000000&&p->x<=1000000&&p->y>=-1000000&&p->y<=1000000&&p->z>=-1000000&&p->z<=1000000))return 0;
    b=big_beam_find(task);w=task->work;src=w->src;owner=src->objId;
    if(owner<0||owner>=16||owner==defender)return 0;
    tier=Port_BigBeamAccept(defender,hit,1)?1:Port_BigBeamAccept(defender,hit,2)?2:0;
    if(!tier)return 0;
    /* All storage and task validation precede the accepted collision/cost.
     * No allocations or fallible resource loads occur after acceptance. */
    for(j=0;j<0x50/4;j++)((u32*)b->source)[j]=((u32*)src)[j];
    copy=(EftJSrc*)b->source;copy->objId=defender;w->src=copy;
    BtlCharApi_GetDeflectDir(defender,&dir);
    if(tier==2){
        EftJVec target;BtlCharApi_GetNodePos(owner,0x11,&target);
        dir.x=target.x-p->x;dir.y=target.y-p->y;dir.z=target.z-p->z;
    }
    len=sqrtf(dir.x*dir.x+dir.y*dir.y+dir.z*dir.z);
    if(!(len>.001f)){dir.x=-w->dir.x;dir.y=-w->dir.y;dir.z=-w->dir.z;len=sqrtf(dir.x*dir.x+dir.y*dir.y+dir.z*dir.z);}
    if(len>.001f){dir.x/=len;dir.y/=len;dir.z/=len;}else{dir.x=0;dir.y=0;dir.z=-1;}
    dir.w=0;w->dir=dir;w->head=*p;w->head.w=1;w->tail=w->head;w->origin=w->head;w->muzzle=w->head;
    w->vel.x=dir.x*w->speed;w->vel.y=dir.y*w->speed;w->vel.z=dir.z*w->speed;w->vel.w=0;
    w->timer=0;w->flags&=~(1|2|4|8|0x20|0x400);task->state=2;
    /* Damage owner is the cloned source. Keep the shooter's reset tag:
     * its manager still owns the emitter resources. Extra-actor reset tags
     * are an encoded field, so combining two tags would also be invalid. */
    b->redirected=1;b->tier=tier;b->age=0;
    b->limit=src->def->life>0?src->def->life:180;if(b->limit>600)b->limit=600;
    Port_TargetHit(defender,owner);
    Port_Dbg3("big beam redirected original/new/tier/effect",owner,defender,tier,src->def->id);
    return tier;
}
static int big_beam_step(EftJTask *task){
    PortBigBeam *b=big_beam_find(task);EftBlast *w=task->work;EftJSrc *original;
    if(!b||!b->redirected)return 0;
    if(b->epoch!=sBigBeamEpoch){BtlTask_SetDead(task);return 1;}
    if(BtlScene_IsCharStopped(w->src->objId))return 1;
    original=(EftJSrc*)b->original;
    if(!(w->flags&1)){
        w->tail=w->head;
        if(b->tier==2)EftAim_Home(&w->dir,&w->head,&w->dir,w->src->objId,w->speed,
            w->src->def->homing>.05f?w->src->def->homing:.05f);
        w->vel.x=w->dir.x*w->speed;w->vel.y=w->dir.y*w->speed;w->vel.z=w->dir.z*w->speed;
        w->head.x+=w->vel.x;w->head.y+=w->vel.y;w->head.z+=w->vel.z;
        if(++b->age>=b->limit)w->flags|=1;
    }
    w->node[0]=w->head;w->node[1]=w->tail;
    /* Preserve the original technique's emitter timeline and resource owner.
     * Do not read the reflector's unrelated attack animation as this beam. */
    EftBlast_UpdateParts(original->objId,task,w->set,0);
    if(w->flags&1){float fade=w->life>0&&w->life<=120?w->life:30;if(++w->timer>=fade)BtlTask_SetDead(task);}
    else EftBlast_AddHit(task);
    return 1;
}
static int big_beam_post(EftJTask *task){
    PortBigBeam *b=big_beam_find(task);EftBlast *w=task->work;
    if(!b||!b->redirected)return 0;
    if(!BtlScene_IsCharStopped(w->src->objId)){
        EftEmit_UpdateAlive(w->set,w->emit);
        if((task->unk4&1)||(task->hit&4))w->flags|=1;
    }
    return 1;
}
static EftJSrc *big_beam_original(EftJTask *task,EftJSrc *fallback){
    PortBigBeam *b=big_beam_find(task);return b&&b->redirected?(EftJSrc*)b->original:fallback;
}
static void big_beam_forget(EftJTask *task){PortBigBeam *b=big_beam_find(task);if(b)b->task=0;}
