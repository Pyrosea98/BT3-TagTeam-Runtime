/* Native EftDisc class, technique pieces only. Keep its original simulation,
 * model, emitters and reset ownership; only clone and redirect its source. */
typedef struct PortBigDisc {
    u32 task,work,epoch;
    u8 source[0x50] __attribute__((aligned(16)));
} PortBigDisc;
static PortBigDisc sBigDisc[128];
static u32 sBigDiscEpoch=1;
int Port_BigDiscSource(void *src){int i;for(i=0;i<128;i++)if(sBigDisc[i].task&&sBigDisc[i].epoch==sBigDiscEpoch&&src==sBigDisc[i].source)return 1;return 0;}
void Port_BigDiscReset(void){if(++sBigDiscEpoch==0)sBigDiscEpoch=1;}
static PortBigDisc *big_disc_find(void *ptr){int i;for(i=0;i<128;i++)if(sBigDisc[i].task==(u32)ptr)return &sBigDisc[i];return NULL;}
void Port_BigDiscForget(void *ptr){PortBigDisc *b=big_disc_find(ptr);if(b)b->task=0;}
int Port_BigDiscReady(void *ptr){
    extern int Port_BloodReviewEnabled(int);EftPTask *task=ptr;EftDisc *w;int slot;
    if(!Port_BloodReviewEnabled(2)||!task||!task->cls||*task->cls!=(void*)EftDisc_Update||!task->work)return 0;
    if(big_disc_find(ptr))return 0;
    w=task->work;
    if((w->flags&(EFT_DISC_ALIVE|EFT_DISC_FLYING|EFT_DISC_PIECE))!=(EFT_DISC_ALIVE|EFT_DISC_FLYING|EFT_DISC_PIECE)||
       (w->flags&(EFT_DISC_END|EFT_DISC_DEAD|EFT_DISC_HELD))||!w->arg.src||w->arg.kind<2)return 0;
    slot=((s32*)w->arg.src)[1];
    return slot>=2&&slot<=4&&w->arg.speed>.001f&&w->arg.speed<1000.f;
}
int Port_BigDiscTry(void *ptr,int defender,void *hit,const void *point){
    extern int Port_BigBeamAccept(int,void*,int);
    extern void BtlCharApi_GetDeflectDir(s32,void*),Port_TargetHit(int,int),Port_Dbg3(const char*,int,int,int,int);
    EftPTask *task=ptr;EftDisc *w;EftPSrc *src;PortBigDisc *b;EftPVec dir,target;float len;int i,j,owner,tier;
    const EftPVec *p=point;
    if(!Port_BigDiscReady(ptr)||!p||defender<0||defender>=16)return 0;
    if(!(p->x>=-1000000&&p->x<=1000000&&p->y>=-1000000&&p->y<=1000000&&p->z>=-1000000&&p->z<=1000000))return 0;
    for(i=0;i<128&&sBigDisc[i].task;i++);if(i==128)return 0;b=&sBigDisc[i];
    w=task->work;src=w->arg.src;owner=src->objId;
    if(owner<0||owner>=16||owner==defender||w->arg.objId!=owner)return 0;
    tier=Port_BigBeamAccept(defender,hit,1)?1:Port_BigBeamAccept(defender,hit,2)?2:0;
    if(!tier)return 0;
    b->task=(u32)task;b->work=(u32)w;b->epoch=sBigDiscEpoch;
    for(j=0;j<0x50/4;j++)((u32*)b->source)[j]=((u32*)src)[j];
    w->arg.src=(EftPSrc*)b->source;w->arg.src->objId=defender;w->arg.objId=defender;
    BtlCharApi_GetDeflectDir(defender,&dir);
    if(tier==2){BtlCharApi_GetNodePos(owner,0x11,&target);dir.x=target.x-p->x;dir.y=target.y-p->y;dir.z=target.z-p->z;}
    len=sqrtf(dir.x*dir.x+dir.y*dir.y+dir.z*dir.z);
    if(!(len>.001f)){dir.x=-w->arg.dir.x;dir.y=-w->arg.dir.y;dir.z=-w->arg.dir.z;len=sqrtf(dir.x*dir.x+dir.y*dir.y+dir.z*dir.z);}
    if(len>.001f){dir.x/=len;dir.y/=len;dir.z/=len;}else{dir.x=dir.y=0;dir.z=-1;}
    dir.w=0;w->arg.dir=dir;w->dir=dir;w->arg.pos=*p;w->prev=*p;w->prev2=*p;
    w->arg.turn=tier==1?0:(w->arg.turn>.05f?w->arg.turn:.05f);
    w->age=0;w->life=w->life>0?w->life:180;if(w->life>600)w->life=600;
    if(!(w->fadeTime>0))w->fadeTime=30;w->fadeLeft=w->fadeTime;
    w->flags|=EFT_DISC_MOVED;
    Port_TargetHit(defender,owner);
    Port_Dbg3("big disc redirected original/new/tier/effect",owner,defender,tier,src->def->id);
    return tier;
}
