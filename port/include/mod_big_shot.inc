/* Registered native EftBlastObj only. Managers keep owning their resources;
 * reset tags remain original while the private source carries damage owner. */
typedef struct PortBigShot {
    u32 task,work,original,epoch;
    int redirected,tier,age,limit,ending;
    Vec4 dir;
    u8 source[0x50] __attribute__((aligned(16)));
} PortBigShot;
static PortBigShot sBigShot[60];
static u32 sBigShotEpoch=1;
int Port_BigShotSource(void *src){int i;for(i=0;i<60;i++)if(sBigShot[i].task&&sBigShot[i].epoch==sBigShotEpoch&&sBigShot[i].redirected&&src==sBigShot[i].source)return 1;return 0;}
void Port_BigShotReset(void){if(++sBigShotEpoch==0)sBigShotEpoch=1;}
static PortBigShot *big_shot_find(void *ptr){int i;for(i=0;i<60;i++)if(sBigShot[i].task==(u32)ptr)return &sBigShot[i];return NULL;}
static void big_shot_register(EftOTask *task){
    extern int Port_BloodReviewEnabled(int);int i;PortBigShot *b;EftBlastObj *w=task->work;
    if(!Port_BloodReviewEnabled(2))return;
    b=big_shot_find(task);if(!b){for(i=0;i<60&&sBigShot[i].task;i++);if(i==60)return;b=&sBigShot[i];}
    memset(b,0,sizeof(*b));b->task=(u32)task;b->work=(u32)w;b->original=(u32)w->src;b->epoch=sBigShotEpoch;
}
int Port_BigShotReady(void *ptr){
    PortBigShot *b=big_shot_find(ptr);EftOTask *task=ptr;EftBlastObj *w;int slot;
    if(!b||!task||b->redirected||b->epoch!=sBigShotEpoch||b->work!=(u32)task->work)return 0;
    w=task->work;if(!w->src||(u32)w->src!=b->original)return 0;
    slot=((s32*)w->src)[1];
    return (w->flags&EFT_BLASTOBJ_ALIVE)&&!(w->flags&(EFT_BLASTOBJ_STOP|EFT_BLASTOBJ_DYING|EFT_BLASTOBJ_KILLED|EFT_BLASTOBJ_NO_HIT|EFT_BLASTOBJ_HELD))&&
        w->delay<=0&&w->speed>.001f&&w->speed<1000.f&&slot>=2&&slot<=4;
}
int Port_BigShotTry(void *ptr,int defender,void *hit,const void *point){
    extern int Port_BigBeamAccept(int,void*,int);
    extern void BtlCharApi_GetDeflectDir(s32,void*),Port_TargetHit(int,int),Port_Dbg3(const char*,int,int,int,int);
    EftOTask *task=ptr;PortBigShot *b;EftBlastObj *w;EftOSrc *src;Vec4 dir,target;float len;int owner,tier,j;
    const Vec4 *p=point;
    if(!Port_BigShotReady(ptr)||!p||defender<0||defender>=16)return 0;
    if(!(p->x>=-1000000&&p->x<=1000000&&p->y>=-1000000&&p->y<=1000000&&p->z>=-1000000&&p->z<=1000000))return 0;
    b=big_shot_find(task);w=task->work;src=w->src;owner=src->objId;
    if(owner<0||owner>=16||owner==defender)return 0;
    tier=Port_BigBeamAccept(defender,hit,1)?1:Port_BigBeamAccept(defender,hit,2)?2:0;
    if(!tier)return 0;
    for(j=0;j<0x50/4;j++)((u32*)b->source)[j]=((u32*)src)[j];
    w->src=(EftOSrc*)b->source;w->src->objId=defender;
    BtlCharApi_GetDeflectDir(defender,&dir);
    if(tier==2){BtlCharApi_GetNodePos(owner,0x11,&target);dir.x=target.x-p->x;dir.y=target.y-p->y;dir.z=target.z-p->z;}
    len=sqrtf(dir.x*dir.x+dir.y*dir.y+dir.z*dir.z);
    if(!(len>.001f)){dir.x=-w->dir.x;dir.y=-w->dir.y;dir.z=-w->dir.z;len=sqrtf(dir.x*dir.x+dir.y*dir.y+dir.z*dir.z);}
    if(len>.001f){dir.x/=len;dir.y/=len;dir.z/=len;}else{dir.x=dir.y=0;dir.z=-1;}
    dir.w=0;b->dir=dir;Vec4_Copy(&w->dir,&dir);
    w->pose.start=*p;w->pose.pos=*p;w->pose.prev=*p;w->delay=0;
    w->pose.vel.x=dir.x*w->speed;w->pose.vel.y=dir.y*w->speed;w->pose.vel.z=dir.z*w->speed;w->pose.vel.w=0;
    b->redirected=1;b->tier=tier;b->age=b->ending=0;
    b->limit=w->life>0?w->life*30:180;if(b->limit>600)b->limit=600;
    Port_TargetHit(defender,owner);
    Port_Dbg3("big shot redirected original/new/tier/effect",owner,defender,tier,src->def->id);
    return tier;
}
static int big_shot_step(EftOTask *task){
    PortBigShot *b=big_shot_find(task);EftBlastObj *w=task->work;EftOSrc *original;int i,j;
    if(!b||!b->redirected)return 0;
    if(b->epoch!=sBigShotEpoch){BtlTask_SetDead(task);return 1;}
    if(BtlScene_IsCharStopped(w->src->objId))return 1;
    original=(EftOSrc*)b->original;
    /* Original volley steering/END can no longer recapture the returned
     * object. Native reset still retires it before its resources are freed. */
    w->flags&=~(EFT_BLASTOBJ_STOP|EFT_BLASTOBJ_DYING|EFT_BLASTOBJ_STUCK|EFT_BLASTOBJ_FROZEN|EFT_BLASTOBJ_NO_HIT|EFT_BLASTOBJ_HELD|EFT_BLASTOBJ_PLACED);
    Vec4_Copy(&w->dir,&b->dir);w->pose.prev=w->pose.pos;
    if(!b->ending){
        if(b->tier==2)EftAim_Home(&w->dir,&w->pose.pos,&w->dir,w->src->objId,w->speed,w->src->def->homing>.05f?w->src->def->homing:.05f);
        Vec4_Copy(&b->dir,&w->dir);
        w->pose.vel.x=b->dir.x*w->speed;w->pose.vel.y=b->dir.y*w->speed;w->pose.vel.z=b->dir.z*w->speed;
        Vec3_Add(&w->pose.pos,&w->pose.pos,&w->pose.vel);
        if(++b->age>=b->limit)b->ending=1;
    }
    for(i=0;i<6;i++){for(j=0;j<2;j++)w->nodes.n[i][j].pos=w->pose.pos;w->nodes.c[i].pos=w->pose.pos;}
    if(w->flags&EFT_BLASTOBJ_MODEL)EftBlastObj_UpdateModel(task);
    if(b->ending)w->flags|=EFT_BLASTOBJ_STOP;
    EftBlastObj_UpdateParts(original->objId,task,w->set,0);
    if(b->ending){BtlTask_SetDead(task);return 1;}
    EftEmit_UpdateTrailWidth(w->set,w->state);EftBlastObj_AddHit(task);return 1;
}
static int big_shot_post(EftOTask *task){
    PortBigShot *b=big_shot_find(task);EftBlastObj *w=task->work;
    if(!b||!b->redirected)return 0;
    if(!BtlScene_IsCharStopped(w->src->objId)){
        EftEmit_UpdateAlive(w->set,w->state);
        if((task->result&4)||(task->flags&1))b->ending=1;
    }
    return 1;
}
static void big_shot_forget(void *task){PortBigShot *b=big_shot_find(task);if(b)b->task=0;}
