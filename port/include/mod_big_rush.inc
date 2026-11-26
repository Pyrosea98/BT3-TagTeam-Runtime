/* Native flying rush-shot projectile. Reflection never starts a paired
 * cinematic from the reflector's unrelated attack animation. */
typedef struct PortBigRush {
    u32 task,work,original,epoch;
    int redirected,tier,age,limit,ending;
    u8 source[0x50] __attribute__((aligned(16)));
} PortBigRush;
static PortBigRush sBigRush[64];
static u32 sBigRushEpoch=1;
void Port_BigRushReset(void){if(++sBigRushEpoch==0)sBigRushEpoch=1;}
static PortBigRush *big_rush_find(void *ptr){int i;for(i=0;i<64;i++)if(sBigRush[i].task==(u32)ptr)return &sBigRush[i];return NULL;}
static void big_rush_register(EftTask *task){
    extern int Port_BloodReviewEnabled(int);int i;PortBigRush *b;EftRushShotWork *w=task->work;
    if(!Port_BloodReviewEnabled(1))return;
    b=big_rush_find(task);if(!b){for(i=0;i<64&&sBigRush[i].task;i++);if(i==64)return;b=&sBigRush[i];}
    memset(b,0,sizeof(*b));b->task=(u32)task;b->work=(u32)w;b->original=(u32)w->arg;b->epoch=sBigRushEpoch;
}
int Port_BigRushReady(void *ptr){
    PortBigRush *b=big_rush_find(ptr);EftTask *task=ptr;EftRushShotWork *w;
    if(!b||!task||b->redirected||b->epoch!=sBigRushEpoch||b->work!=(u32)task->work)return 0;
    w=task->work;
    return (w->flags&EFT_RUSHSHOT_FIRED)&&!(w->flags&(EFT_RUSHSHOT_CONNECTED|EFT_RUSHSHOT_ATTACHED|EFT_RUSHSHOT_SENT|EFT_RUSHSHOT_END|EFT_RUSHSHOT_KILL))&&
        w->speed>.001f&&w->speed<1000.f&&w->arg&&(u32)w->arg==b->original&&w->arg->slot>=2&&w->arg->slot<=4;
}
int Port_BigRushSource(void *src){int i;for(i=0;i<64;i++)if(sBigRush[i].task&&sBigRush[i].epoch==sBigRushEpoch&&sBigRush[i].redirected&&src==sBigRush[i].source)return 1;return 0;}
int Port_BigRushTry(void *ptr,int defender,void *hit,const void *point){
    extern int Port_BigBeamAccept(int,void*,int);
    extern void BtlCharApi_GetDeflectDir(s32,Vec4*),Port_TargetHit(int,int),Port_Dbg3(const char*,int,int,int,int);
    EftTask *task=ptr;EftRushShotWork *w;PortBigRush *b;EftTechArg *src;Vec4 dir,target;float len;int owner,tier,j;
    const Vec4 *p=point;
    if(!Port_BigRushReady(ptr)||!p||defender<0||defender>=16)return 0;
    if(!(p->x>=-1000000&&p->x<=1000000&&p->y>=-1000000&&p->y<=1000000&&p->z>=-1000000&&p->z<=1000000))return 0;
    b=big_rush_find(task);w=task->work;src=w->arg;owner=src->objId;
    if(owner<0||owner>=16||owner==defender)return 0;
    tier=Port_BigBeamAccept(defender,hit,1)?1:Port_BigBeamAccept(defender,hit,2)?2:0;if(!tier)return 0;
    for(j=0;j<0x50/4;j++)((u32*)b->source)[j]=((u32*)src)[j];
    w->arg=(EftTechArg*)b->source;w->arg->objId=defender;
    BtlCharApi_GetDeflectDir(defender,&dir);
    if(tier==2){BtlCharApi_GetNodePos(owner,0x11,&target);dir.x=target.x-p->x;dir.y=target.y-p->y;dir.z=target.z-p->z;}
    len=sqrtf(dir.x*dir.x+dir.y*dir.y+dir.z*dir.z);
    if(!(len>.001f)){dir.x=-w->dir.x;dir.y=-w->dir.y;dir.z=-w->dir.z;len=sqrtf(dir.x*dir.x+dir.y*dir.y+dir.z*dir.z);}
    if(len>.001f){dir.x/=len;dir.y/=len;dir.z/=len;}else{dir.x=dir.y=0;dir.z=-1;}
    dir.w=0;w->dir=dir;w->start=w->pos=w->prev=w->unk390=*p;
    w->vel=(Vec4){dir.x*w->speed,dir.y*w->speed,dir.z*w->speed,0};
    w->flags&=~(EFT_RUSHSHOT_HIT_MOVED|EFT_RUSHSHOT_ENDED|EFT_RUSHSHOT_FAST_END|EFT_RUSHSHOT_COUNT);
    w->timer=0;b->redirected=1;b->tier=tier;b->age=b->ending=0;
    b->limit=src->def->unk28>0?src->def->unk28:180;if(b->limit>600)b->limit=600;
    Port_TargetHit(defender,owner);Port_Dbg3("big rush redirected original/new/tier/effect",owner,defender,tier,src->def->id);return tier;
}
static int big_rush_step(EftTask *task){
    PortBigRush *b=big_rush_find(task);EftRushShotWork *w=task->work;EftTechArg *original;
    if(!b||!b->redirected)return 0;
    if(b->epoch!=sBigRushEpoch){BtlTask_SetDead(task);return 1;}
    if(BtlScene_IsCharStopped(w->arg->objId))return 1;
    original=(EftTechArg*)b->original;w->prev=w->pos;
    if(!b->ending){
        if(b->tier==2)EftAim_Home(&w->dir,&w->pos,&w->dir,w->arg->objId,w->speed,w->arg->def->unk38>.05f?w->arg->def->unk38:.05f);
        w->vel=(Vec4){w->dir.x*w->speed,w->dir.y*w->speed,w->dir.z*w->speed,0};
        Vec3_Add(&w->pos,&w->pos,&w->vel);if(++b->age>=b->limit)b->ending=1;
    }
    if(w->subCount>0)EftRushShot_UpdateModels(task);
    if(b->ending)w->flags|=EFT_RUSHSHOT_END;
    EftRushShot_Draw(original->objId,task,w->model);
    if(b->ending){float fade=w->life>0&&w->life<=120?w->life:30;if(++w->timer>=fade)BtlTask_SetDead(task);}
    else {EftEmit_UpdateTrailWidth(w->model,&w->inst);EftRushShot_AddHitRecord(task);}
    return 1;
}
static int big_rush_post(EftTask *task){
    PortBigRush *b=big_rush_find(task);EftRushShotWork *w=task->work;
    if(!b||!b->redirected)return 0;
    if(!BtlScene_IsCharStopped(w->arg->objId)){
        EftEmit_UpdateAlive(w->model,&w->inst);if((task->flags&7)||(task->hit&4))b->ending=1;
    }
    return 1;
}
static void big_rush_restore(EftTask *task){PortBigRush *b=big_rush_find(task);if(b){if(b->redirected)((EftRushShotWork*)task->work)->arg=(EftTechArg*)b->original;b->task=0;}}
