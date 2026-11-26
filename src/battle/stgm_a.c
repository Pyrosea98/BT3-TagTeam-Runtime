/*
 * Stage model drawing (0x115478..0x116B98), the last part of the stage model code that starts at 0x114C60
 * (src/battle/stg_d_b.c is the piece in front of this one).
 *
 * Twice per view and frame: StgModel_Cull(view) decides which meshes are visible and StgModel_Draw(view) queues
 * them. Callers: the battle draw (src/battle/battle.c, both the single view and the split-screen views) and
 * ChrView_Update pass 1 (src/battle/view_a_d.c).
 *
 * Nothing here is read by the fight simulation. What it writes: the visibility table (one byte per mesh, rebuilt
 * for every view), the cull result of each tree cell, the alpha of debris meshes, the w of an animated object's
 * position in the stage file (set to 1) and the current texture of animated materials (advanced once per DRAWN
 * view, not per simulated frame, and held while the battle is paused).
 */
#include "common.h"
#include "sys/dma.h"
#include "sys/gfx.h"
#include "sys/vu1_packet.h"
#include "battle/stgm_a.h"

/* Offset table of the stage file (StgOfsTable in battle/stg_a.h). */
typedef struct StgMOfsTable StgMOfsTable;

/* Local view of the battle work: only the flag word. */
typedef struct StgMWork {
    /* 0x0000 */ u8 unk0[0x19F0];
    /* 0x19F0 */ u64 flags;    /* 0x100: paused; 0x2000: stage not drawn; bit 47: no scrolling stage effect */
} StgMWork;

/* Local view of the camera view (View in battle/btl_cam.h). */
typedef struct StgMView {
    /* 0x000 */ u8 unk0[0x100];
    /* 0x100 */ f32 mtx100[16]; /* the matrix VU1 program 4 is loaded with */
} StgMView;

typedef struct StgMFrustum {
    u8 unk0[0xD0];
} StgMFrustum; /* StgFrustum in battle/stg_a.h */

extern BtlStageM *gBtlStage;
extern StgMOfsTable *gStgAnimTable;

extern StgMWork *Battle_GetWork(void);
extern s32 BtlStage_IsObjBroken(s32 idx);
extern s32 BtlStage_GetId(void);
extern void *StgOfsTable_Get(StgMOfsTable *t, s32 i);
extern s32 StgOfsTable_GetCount(StgMOfsTable *t);
extern void StgFrustum_Build(void *view, StgMFrustum *fr);
extern s32 StgFrustum_TestBox(StgMFrustum *fr, f32 x, f32 y, f32 z, f32 r);
extern s32 StgFrustum_TestPart(StgMPart *part, StgMFrustum *fr);
extern f32 Stg_FadeRatio(f32 a, f32 b);
extern f32 Stg_FadeByCamDist(Vec4 *pos, f32 value);
extern void EftStageScroll_Draw(void);
extern void Vu0Screen_StoreMtx(void *m);
extern void Vu0Clip_StoreMtx(void *m);
extern void Vu0Cur_LoadIdentity(void);
extern void Vu0Cur_RotateZXY(Vec4 *angles);
extern void Vu0Cur_Translate(Vec4 *v);
extern void Vu0Cur_StoreMtx(void *m);
extern void Vu0Cur_LoadMtx(void *m);
extern void *memset(void *, s32, u32);

/* Draws the animated stage objects: for each animation, every object that plays it, posed at its frame. */
void StgModel_DrawAnims(void) {
    f32 mtx[16];
    Vec4 pos;
    Vec4 rot;
    s32 count;
    s32 i;
    s32 j;
    s32 anim;
    s32 uploaded;
    u8 *pkt;
    StgMData *data;
    StgMObjDef *def;
    TexFile *tex;
    u32 *node;
    Vu1Node *mdl;
    Vu1Track *track;

    if (gStgAnimTable == NULL) {
        return;
    }
    if (gBtlStage->flags & 1) {
        return;
    }
    pkt = (u8 *)Vu1Pkt_LoadProg8();
    Vu0Screen_StoreMtx(pkt + 0xB0);
    Vu0Clip_StoreMtx(pkt + 0xF0);
    StgModel_SetAnimDrawEnv();
    count = StgOfsTable_GetCount(gStgAnimTable);
    data = gBtlStage->data;
    anim = 1;
    for (i = 0; i < count; i += 3, anim++) {
        uploaded = 0;
        tex = StgOfsTable_Get(gStgAnimTable, i);
        node = StgOfsTable_Get(gStgAnimTable, i + 1);
        mdl = (Vu1Node *)((u8 *)node + node[0] / 4 * 4);
        track = StgOfsTable_Get(gStgAnimTable, i + 2);
        def = data->objDefs;
        for (j = 0; j < data->objCount; j++) {
            if (def->anim != anim) {
                def++;
                continue;
            }
            if (!(def->type & 0x40000000) && BtlStage_IsObjBroken(j)) {
                def++;
                continue;
            }
            rot.x = def->rot.x * 3.14159265f / 180.0f;
            rot.y = def->rot.y * 3.14159265f / 180.0f;
            rot.z = def->rot.z * 3.14159265f / 180.0f;
            rot.w = 1.0f;
            pos.x = def->pos.x;
            pos.y = def->pos.y;
            pos.z = def->pos.z;
            pos.w = 1.0f;
            def->pos.w = 1.0f;
            Vu0Cur_LoadIdentity();
            Vu0Cur_RotateZXY(&rot);
            Vu0Cur_Translate(&pos);
            Vu0Cur_StoreMtx(mtx);
            Vu1Node_Animate(mdl, track, mtx, def->frame);
            def++;
            if (!uploaded) {
                TexFile_UploadPacked(tex, 0x3480, 0x3700);
                uploaded = 1;
            }
            Vu1Node_Draw(mdl);
        }
    }
}

/* Culls one cell of the tree and its children. vis: 0 = the parent is wholly inside (no more tests), 1 = test. */
void StgModel_CullCell(StgMCell *cell, f32 size, s32 vis, struct StgFrustum *fr) {
    u8 *tbl = gBtlStage->vis;
    f32 half = (size + size) / (f32)(1 << cell->depth);
    s32 i;
    s32 j;
    s32 k;

    if (vis == 1) {
        vis = StgFrustum_TestBox((StgMFrustum *)fr, cell->x, cell->y, cell->z, half);
    }
    cell->vis = vis;
    if (vis == 2) {
        return;
    }
    if (vis == 0) {
        StgMPart *part;

        for (i = 0; i < cell->partCount; i++) {
            part = cell->parts[i];
            if (part == NULL) {
                continue;
            }
            if (!(u16)(part->flags & 1)) {
                continue;
            }
            for (j = 0; j < part->count; j++) {
                tbl[part->meshes[j]->visIdx] = 1;
            }
        }
    } else {
        StgMPart *part;

        for (i = 0; i < cell->partCount; i++) {
            part = cell->parts[i];
            if (part == NULL) {
                continue;
            }
            if (!(u16)(part->flags & 1)) {
                continue;
            }
            if (StgFrustum_TestPart(part, (StgMFrustum *)fr) == 2) {
                continue;
            }
            for (j = 0; j < part->count; j++) {
                tbl[part->meshes[j]->visIdx] = 1;
            }
        }
    }
    for (k = 0; k < 2; k++) {
        for (j = 0; j < 2; j++) {
            for (i = 0; i < 2; i++) {
                if (cell->child[k][j][i] != NULL) {
                    StgModel_CullCell(cell->child[k][j][i], size, vis, fr);
                }
            }
        }
    }
}

/* Builds the visibility table for one view: the culling tree, the always-drawn parts, then the debris. */
void StgModel_Cull(void *view) {
    StgMFrustum fr;
    u8 *tbl;
    StgMPartList *list;
    StgMCell *root;
    StgMPart *part;
    u32 i;
    s32 j;

    if (Battle_GetWork()->flags & 0x2000) {
        return;
    }
    tbl = gBtlStage->vis;
    list = gBtlStage->always;
    root = gBtlStage->root;
    memset(tbl, 0, gBtlStage->visSize);
    if (gBtlStage->flags & 1) {
        return;
    }
    StgFrustum_Build(view, &fr);
    StgModel_CullCell(root, gBtlStage->size, 1, (struct StgFrustum *)&fr);
    if (list->count != 0) {
        StgMPart *parts = list->parts;

        for (i = 0; i < list->count; i++) {
            part = &parts[i];
            for (j = 0; j < part->count; j++) {
                tbl[part->meshes[j]->visIdx] = 1;
            }
        }
    }
    StgModel_FadeDebris();
}

/* Marks the pieces of falling objects visible, with an alpha that fades with age and camera distance. */
void StgModel_FadeDebris(void) {
    StgMObj *objs = gBtlStage->objs;
    u8 *tbl = gBtlStage->vis;
    StgMMesh *mesh;
    StgMPart *piece;
    StgMNode *node;
    u32 i;
    u32 j;
    s32 k;
    f32 ratio;
    f32 a;
    u8 alpha;

    for (i = 0; i < gBtlStage->objCount; i++) {
        if (!(objs[i].state & 2)) {
            continue;
        }
        ratio = Stg_FadeRatio((f32)objs[i].frame, (f32)objs[i].animEnd);
        for (j = 0; j < objs[i].pieceCount; j++) {
            piece = objs[i].pieces[j];
            node = piece->node;
            if (node == NULL) {
                continue;
            }
            if (node->body < 0) {
                continue;
            }
            a = Stg_FadeByCamDist(&node->pos, ratio);
            if (a < 1.0f) {
                alpha = (u32)(a * 128.0f);
            } else {
                alpha = 0x80;
            }
            if (alpha == 0) {
                continue;
            }
            for (k = 0; k < piece->count; k++) {
                mesh = piece->meshes[k];
                mesh->alpha = alpha;
                tbl[mesh->visIdx] = 1;
            }
        }
    }
}

#ifdef PORT
#include "mod_terrain_bridge.h"
#include "mod_environment_rules.h"
static int sLiquidActive;
static float sTdScarBounds[TD_MAX][4];
static unsigned char sTdVisible[8192],sTdEligible[8192];static TdState sTdVisibility;
/* Object part records are not all resolved renderer mesh lists. Compare ownership only;
   enumerate meshes exclusively through the lists consumed by the original culler. */
static int td_object_part(StgMPart*p){int i,j;for(i=0;i<gBtlStage->objCount;i++){StgMObj*o=gBtlStage->objs+i;for(j=0;j<6;j++)if(o->parts[j]==p)return !(o->state&1)||!(j&1)||p->node!=0;if(o->state&2)for(j=0;j<o->pieceCount;j++)if(o->pieces[j]==p)return 1;}return 0;}
/* Static opaque ground and its blended dirt/grass/shadow layers use the same
 * displaced sheet. Sky/backdrop, water-scroll and animated object lists stay native. */
/* Rebuild the membership index for the current view: destruction may have
   replaced material lists since the last one. Collisions fall back to the
   original range check without dereferencing an unverified pointer. */
static StgMMesh *sTdMember[16384];
static unsigned td_member_slot(StgMMesh *mesh) {
    unsigned long long key = (unsigned long long)mesh / sizeof(StgMMesh);
    return (unsigned)(key ^ (key >> 14)) & 16383;
}
static void td_index_meshes(void) {
    memset(sTdMember,0,sizeof(sTdMember));
    for (int k=STGM_GRP_MAIN;k<=STGM_GRP_BLEND_C;++k) {
        StgMGroup *group=&gBtlStage->mdl->grp[k];
        for (int i=0;i<group->count;++i) {
            StgMMat *mat=&group->mats[i];
            for (int j=0;j<mat->count;++j) {
                StgMMesh *mesh=mat->meshes+j;
                sTdMember[td_member_slot(mesh)]=mesh;
            }
        }
    }
}
static int td_main_mesh(StgMMesh*m){int i,k;if(sTdMember[td_member_slot(m)]==m)return 1;for(k=STGM_GRP_MAIN;k<=STGM_GRP_BLEND_C;k++){StgMGroup*g=&gBtlStage->mdl->grp[k];for(i=0;i<g->count;i++){unsigned long long a=(unsigned long long)m,b=(unsigned long long)g->mats[i].meshes,n=(unsigned long long)g->mats[i].count*sizeof(StgMMesh);if(a>=b&&a-b<n&&(a-b)%sizeof(StgMMesh)==0)return 1;}}return 0;}
/* A part may belong to several native culling cells. Membership is stable
   during one view, even when stage destruction changes it between views. */
static StgMPart *sTdVisited[4096];
static int sTdClosurePending;
static void td_static_part(StgMPart *p) {
    if (!p || !(p->flags & 1)) return;
    unsigned slot = ((unsigned long long)p >> 4) & 4095;
    if (sTdVisited[slot] == p) return;
    sTdVisited[slot] = p;
    int overlap = Port_TerrainPartOverlap(&sTdVisibility,sTdScarBounds,&p->x,&p->radius);
    if (td_object_part(p)) return;
    for (int j = 0; j < p->count; ++j) {
        StgMMesh *m = p->meshes[j];
        if (m && td_main_mesh(m) && m->prog == 0 && m->visIdx < 8192) {
            unsigned char *eligible = &sTdEligible[m->visIdx];
            if (overlap) *eligible = 2;
            else if (!*eligible) *eligible = 1;
            if (overlap == 2) sTdVisible[m->visIdx] = 1;
        }
    }
}
static void td_static_cell(StgMCell*c,int depth){int i,a,b,d;if(!c||depth>20)return;for(i=0;i<c->partCount;i++)td_static_part(c->parts[i]);for(a=0;a<2;a++)for(b=0;b<2;b++)for(d=0;d<2;d++)td_static_cell(c->child[a][b][d],depth+1);}
static void td_visibility_prepare(void){int i;memset(sTdVisible,0,sizeof(sTdVisible));memset(sTdEligible,0,sizeof(sTdEligible));if(!Port_TerrainSnapshot(&sTdVisibility))return;
 for (i = 0; i < sTdVisibility.count; ++i) td_bounds(&sTdVisibility.scars[i], sTdScarBounds[i]);
 td_index_meshes();
 memset(sTdVisited,0,sizeof(sTdVisited));
 sTdClosurePending=1;
 td_static_cell(gBtlStage->root,0);if(gBtlStage->always)for(i=0;i<gBtlStage->always->count;i++)td_static_part(gBtlStage->always->parts+i);
}
static void td_stage_call4(u32 chain){extern void Port_GsMarker(int);Port_GsMarker(0x7D00);Vu1Pkt_CallProg4(chain);Port_GsMarker(0x7D01);}
#else
#define td_stage_call4 Vu1Pkt_CallProg4
#endif

#ifdef PORT
/* The beach and sea at Kame House are separate, nearly coplanar materials. */
static int td_liquid_material(int stage, int texture) {
    switch (stage) {
    case 12: return texture == 27;
    case 3: return texture == 10;
    case 2: return texture == 32 || texture == 33;
    default: return 1;
    }
}
static void td_group_call4(u32 chain, unsigned index, int texture) {
    extern void Port_GsMarker(int);
    int liquid = sLiquidActive && td_liquid_material(BtlStage_GetId(), texture);
    if (liquid) Port_GsMarker(0x7D02);
    if (index < 8192 && sTdEligible[index] &&
        (sTdEligible[index] == 2 || sTdClosurePending)) {
        if (!liquid) sTdClosurePending = 0;
        td_stage_call4(chain);
    }
    else Vu1Pkt_CallProg4(chain);
    if (liquid) Port_GsMarker(0x7D03);
}
#else
#define td_group_call4(chain,index,texture) Vu1Pkt_CallProg4(chain)
#endif

/* Uploads one texture of the stage texture file to the stage's work block and selects it. */
void StgModel_SetTexture(s32 index) {
#ifdef PORT
    /* In the original Dying Namek atlas the level-60 sheet uses a dark
       distant surface; slot 19 is the horizontal glowing lava material. */
    if (index == 10 && BtlStage_GetId() == 3 && gBtlStage->tex->count == 25)
        index = 19;
#endif
    Dma_AddTexFlush();
    TexFile_UploadOne(gBtlStage->tex, index, 0x2A00, 0x32C0);
    Dma_AddTex0(gBtlStage->tex->ent[index].tex0 | 0x0006580400002A00);
}

/* Draws the debris group with VU1 program 7. Returns 0 when the group has materials, else `drawn` unchanged. */
s32 StgModel_DrawDebris(s32 drawn, u8 *vis) {
    StgMModel *mdl = gBtlStage->mdl;
    StgMMat *mats;
    StgMMat *mat;
    StgMMesh *mesh;
    s32 i;
    s32 j;
    u32 chain;

    if (mdl->grp[STGM_GRP_DEBRIS].count != 0) {
        Vu1Pkt_LoadProg7();
        StgModel_SetDrawEnv();
        StgModel_SetTestDebris();
        mats = mdl->grp[STGM_GRP_DEBRIS].mats;
        drawn = 0;
        for (i = 0; i < mdl->grp[STGM_GRP_DEBRIS].count; i++) {
            chain = 0;
            mesh = mats[i].meshes;
            for (j = 0; j < mats[i].count; j++) {
                if (vis[mesh[j].visIdx] != 0) {
                    if (chain == 0) {
                        StgModel_SetTexture(mats[i].tex);
                    }
                    chain = mesh[j].chain;
                    Vu0Cur_LoadMtx(mesh[j].mtx);
                    Vu1Pkt_CallProg7(chain, (f32)mesh[j].alpha);
                }
            }
        }
        StgModel_SetTestDefault();
    }
    return drawn;
}

/* The loop shared by the plain groups: every visible mesh of every material, texture set once per material. */
#define STGM_DRAW_GROUP(g)                                                      \
    for (i = 0; i < mdl->grp[g].count; i++) {                                   \
        chain = 0;                                                              \
        mesh = mats[i].meshes;                                                  \
        for (j = 0; j < mats[i].count; j++) {                                   \
            if (vis[mesh[j].visIdx] != 0) {                                     \
                if (chain == 0) {                                               \
                    StgModel_SetTexture(mats[i].tex);                           \
                }                                                               \
                chain = mesh[j].chain;                                          \
                td_group_call4(chain,mesh[j].visIdx,mats[i].tex);                           \
            }                                                                   \
        }                                                                       \
    }

/*
 * Draws the stage for one view. Order: group 0 (no depth writes), the scrolling stage effect, group 1, group 2
 * (the stage proper, with animated textures), the animated objects, then the blended groups 3..5 and the debris
 * (group 6). On stage 8 the debris is drawn before the blended groups; on stage 5 group 3 is skipped.
 * Not while a stage load job runs (flag 0x2000) or while the stage is not ready.
 */
void StgModel_Draw(void *view) {
    u8 *vis;
    s32 prog = -1;
    s32 blend = 0;
    void *mtx;
    StgMModel *mdl;
    StgMMat *mats;
    StgMMesh *mesh;
    StgMTexAnim *anim;
    s32 *frames;
    s32 i;
    s32 j;
    s32 k;
    u32 chain;
#ifdef PORT
    Mtx44 pod,door;
    int podStart=-1;
    extern int Port_StoryCh9PodMatrix(Mtx44 *,Mtx44 *);
#endif

    if (Battle_GetWork()->flags & 0x2000) {
        return;
    }
    if (gBtlStage->flags & 1) {
        return;
    }
    mdl = gBtlStage->mdl;
    vis = gBtlStage->vis;
#ifdef PORT
    td_visibility_prepare();
    {extern int BtlStage_GetWaterLevel(float*);extern void Port_GsMarker(int);float level;
     {float native=0;int has=BtlStage_GetWaterLevel(&native);sLiquidActive=ENV_Liquid(BtlStage_GetId(),has,native,&level)&&isfinite(level)&&fabsf(level)<1000000.f;}
     if(sLiquidActive){int g,m,j; if(BtlStage_GetId()==12)level=3.0f;Port_GsMarker(0x60000000|((int)(level*100.f)&0x0fffffff));
      /* Keep the original small liquid sheet available when a distant crater opens. */
      /* A charge on dry land must not uncull the whole ocean. */
      int needsSheet = 0;
      for (j = 0; sTdVisibility.enabled && j < sTdVisibility.count; ++j) {
          TdScar *scar = &sTdVisibility.scars[j];
          if (scar->ay < level && scar->ay + scar->depth >= level) { needsSheet = 1; break; }
      }
      int stage = BtlStage_GetId();
      if (needsSheet) {
          for (g = STGM_GRP_MAIN; g <= STGM_GRP_BLEND_C; ++g) {
              for (m = 0; m < mdl->grp[g].count; ++m) {
                  StgMMat *mat = &mdl->grp[g].mats[m];
                  if (!(mat->anim || stage == 2 || stage == 3 || stage == 12)) continue;
                  if (!td_liquid_material(stage, mat->tex)) continue;
                  for (j = 0; j < mat->count; ++j) vis[mat->meshes[j].visIdx] = 1;
              }
          }
      }
     }else Port_GsMarker(0x7D04);}
#endif
    StgModel_SetDrawEnv();

    if (mdl->grp[STGM_GRP_BACK].count != 0) {
        prog = 0;
        Vu1Pkt_LoadProg4(((StgMView *)view)->mtx100);
        mats = mdl->grp[STGM_GRP_BACK].mats;
        Dma_AddZbuf(0xE0, 1);
        STGM_DRAW_GROUP(STGM_GRP_BACK)
        Dma_AddZbuf(0xE0, 0);
    }

    if (!(Battle_GetWork()->flags & 0x0080000000000000)) {
        Dma_AddZbuf(0xE0, 1);
        EftStageScroll_Draw();
        Dma_AddZbuf(0xE0, 0);
    }

    if (mdl->grp[STGM_GRP_BACK2].count != 0) {
        if (prog != 0) {
            prog = 0;
            Vu1Pkt_LoadProg4(((StgMView *)view)->mtx100);
        }
        mats = mdl->grp[STGM_GRP_BACK2].mats;
        Dma_AddZbuf(0xE0, 0);
        STGM_DRAW_GROUP(STGM_GRP_BACK2)
    }

    StgModel_SetTestOpaque();
#ifdef PORT
    if(Port_StoryCh9PodMatrix(&pod,&door))podStart=BtlStage_GetId()==26?449:448;
#endif

    if (mdl->grp[STGM_GRP_MAIN].count != 0) {
        mtx = ((StgMView *)view)->mtx100;
        Vu1Pkt_LoadProg4(mtx);
        mats = mdl->grp[STGM_GRP_MAIN].mats;
        for (i = 0; i < mdl->grp[STGM_GRP_MAIN].count; i++) {
            chain = 0;
            mesh = mats[i].meshes;
            for (j = 0; j < mats[i].count; j++) {
#ifdef PORT
                /* During the escape the capsule is drawn at its scene position. */
                if(podStart>=0&&mesh[j].visIdx>=podStart&&mesh[j].visIdx<podStart+8)continue;
#endif
                if (vis[mesh[j].visIdx] == 0) {
#ifdef PORT
                    /* A lowered runtime floor can be visible from inside its old culling sphere. */
                    extern int Port_TerrainActive(void);
                    if(!(mesh[j].visIdx<8192&&sTdVisible[mesh[j].visIdx]))
#endif
                    continue;
                }
                if (chain == 0) {
                    anim = mats[i].anim;
                    if (anim != NULL) {
                        if ((anim->flags & 4) && !(Battle_GetWork()->flags & 0x100)) {
                            /* Step the texture animation: looping, or back and forth (bit 15 = going back). */
                            frames = anim->frames;
                            for (k = 0; k < anim->count; k++) {
                                if (frames[k] == (mats[i].cur & 0xFFFF7FFF)) {
                                    if ((u16)(anim->flags & 1)) {
                                        if (k == anim->count - 1) {
                                            mats[i].cur = frames[0];
                                        } else {
                                            mats[i].cur = frames[k + 1];
                                        }
                                    } else if (mats[i].cur & 0x8000) {
                                        if (k == 0) {
                                            mats[i].cur = frames[1];
                                        } else {
                                            mats[i].cur = (u16)frames[k - 1] | 0x8000;
                                        }
                                    } else {
                                        if (k == anim->count - 1) {
                                            mats[i].cur = (u16)frames[anim->count - 2] | 0x8000;
                                        } else {
                                            mats[i].cur = frames[k + 1];
                                        }
                                    }
                                    break;
                                }
                            }
                        }
                        StgModel_SetTexture(*(u8 *)&mats[i].cur);
                    } else {
                        StgModel_SetTexture(mats[i].tex);
                    }
                }
                chain = mesh[j].chain;
                if (mesh[j].prog != prog) {
                    switch (mesh[j].prog) {
                    case 0:
                        Vu1Pkt_LoadProg4(mtx);
                        break;
                    case 1: /* needed for the match: the program number is read again after the switch */
                        break;
                    }
                    prog = mesh[j].prog;
                }
                if (mesh[j].prog == 0) {
#ifdef PORT
                    td_group_call4(chain,mesh[j].visIdx,mats[i].tex);
#else
                    Vu1Pkt_CallProg4(chain);
#endif
                }
            }
        }
    }

#ifdef PORT
    /* Draw the capsule with its stock meshes and materials at the scene transform. */
    if(podStart>=0){
        Mtx44 *current=NULL;
        mats=mdl->grp[STGM_GRP_MAIN].mats;
        for(i=0;i<mdl->grp[STGM_GRP_MAIN].count;i++){
            int set=0;
            mesh=mats[i].meshes;
            for(j=0;j<mats[i].count;j++){
                int offset=mesh[j].visIdx-podStart;
                Mtx44 *part;
                if(mesh[j].prog!=0||offset<0||offset>=8)continue;
                /* The lens, lining and shell of the hatch share the hinge. */
                part=(offset<=1||offset>=6)?&door:&pod;
                if(part!=current){
                    Vu1Pkt_LoadProg4Prop(((StgMView*)view)->mtx100,part);
                    current=part;
                }
                if(!set){StgModel_SetTexture(mats[i].tex);set=1;}
                Vu1Pkt_CallProg4(mesh[j].chain);
            }
        }
        Vu1Pkt_LoadProg4(((StgMView*)view)->mtx100);
        prog=0;
    }
#endif
    StgModel_DrawAnims();

    if (BtlStage_GetId() == 8) {
        blend = StgModel_DrawDebris(0, vis);
    }

    if (BtlStage_GetId() != 5 && mdl->grp[STGM_GRP_BLEND_A].count != 0) {
        blend = 1;
        Vu1Pkt_LoadProg4(((StgMView *)view)->mtx100);
        Dma_AddZbuf(0xE0, 1);
        Dma_AddFillRect(0x700, 0x720, 0x200, 0x1C0, 0);
        mats = mdl->grp[STGM_GRP_BLEND_A].mats;
        StgModel_SetBlendEnv();
        Dma_AddZbuf(0xE0, 0);
        STGM_DRAW_GROUP(STGM_GRP_BLEND_A)
        Dma_AddZbuf(0xE0, 0);
    }

    if (mdl->grp[STGM_GRP_BLEND_B].count != 0) {
        blend = 1;
        Vu1Pkt_LoadProg4(((StgMView *)view)->mtx100);
        Dma_AddZbuf(0xE0, 1);
        Dma_AddFillRect(0x700, 0x720, 0x200, 0x1C0, 0);
        mats = mdl->grp[STGM_GRP_BLEND_B].mats;
        Dma_AddZbuf(0xE0, 1);
        StgModel_SetBlendEnv();
        STGM_DRAW_GROUP(STGM_GRP_BLEND_B)
        Dma_AddZbuf(0xE0, 0);
    }

    if (mdl->grp[STGM_GRP_BLEND_C].count != 0) {
        blend = 1;
        Vu1Pkt_LoadProg4(((StgMView *)view)->mtx100);
        Dma_AddZbuf(0xE0, 1);
        Dma_AddFillRect(0x700, 0x720, 0x200, 0x1C0, 0);
        mats = mdl->grp[STGM_GRP_BLEND_C].mats;
        StgModel_SetBlendEnv();
        STGM_DRAW_GROUP(STGM_GRP_BLEND_C)
        Dma_AddZbuf(0xE0, 0);
    }

    if (BtlStage_GetId() != 8) {
        blend = StgModel_DrawDebris(blend, vis);
    }
    Dma_AddZbuf(0xE0, 0);
    if (blend != 0) {
        StgModel_SetDrawEnv();
    }
}

/* Queues the stage's GS state: frame buffer of this frame, depth buffer, blend, test, filter, clamps. */
void StgModel_SetDrawEnv(void) {
    u32 pkt[40] = {
        0x10000009, 0, 0x10000000, 0x50000009,                /* DMA cnt 9 / VIF direct 9 */
        0x8008, 0x10000000, 0xE, 0,                           /* GIF tag: 8 x A+D, EOP */
        !(gGfx.frame & 1) ? 0x80070 : 0x80000, 0, 0x4C, 0,     /* FRAME_1 */
        0x310000E0, 0, 0x4E, 0,                               /* ZBUF_1 */
        0x44, 0, 0x42, 0,                                     /* ALPHA_1 */
        0x5001B, 0, 0x47, 0,                                  /* TEST_1 */
        0x60, 0, 0x14, 0,                                     /* TEX1_1 */
        0, 0, 0x4A, 0,                                        /* FBA_1 */
        0, 0, 8, 0,                                           /* CLAMP_1 */
        1, 0, 0x46, 0,                                        /* COLCLAMP */
    };

    Dma_AddData(pkt, 0xA0);
}

/* Queues the state of the blended groups: depth buffer with writes off, test, FBA. */
void StgModel_SetBlendEnv(void) {
    u32 pkt[20] = {
        0x10000004, 0, 0x10000000, 0x50000004,
        0x8003, 0x10000000, 0xE, 0,
        0x310000E0, 1, 0x4E, 0,                               /* ZBUF_1, ZMSK */
        0x54000, 0, 0x47, 0,                                  /* TEST_1 */
        1, 0, 0x4A, 0,                                        /* FBA_1 */
    };

    Dma_AddData(pkt, 0x50);
}

/* Queues TEST_1 = 0x507FB (alpha >= 0x7F, depth >=): the stage proper. */
void StgModel_SetTestOpaque(void) {
    u32 pkt[12] = {
        0x10000002, 0, 0x10000000, 0x50000002,
        0x8001, 0x10000000, 0xE, 0,
        0x507FB, 0, 0x47, 0,
    };

    Dma_AddData(pkt, 0x30);
}

/* Queues TEST_1 = 0x51809 (alpha == 0x80, otherwise colour only; depth >=): the debris. */
void StgModel_SetTestDebris(void) {
    u32 pkt[12] = {
        0x10000002, 0, 0x10000000, 0x50000002,
        0x8001, 0x10000000, 0xE, 0,
        0x51809, 0, 0x47, 0,
    };

    Dma_AddData(pkt, 0x30);
}

/* Queues TEST_1 = 0x5001B (alpha >= 1, depth >=): the default. */
void StgModel_SetTestDefault(void) {
    u32 pkt[12] = {
        0x10000002, 0, 0x10000000, 0x50000002,
        0x8001, 0x10000000, 0xE, 0,
        0x5001B, 0, 0x47, 0,
    };

    Dma_AddData(pkt, 0x30);
}

/* The same state as StgModel_SetDrawEnv with TEST_1 = 0x507FB, for the animated objects. */
void StgModel_SetAnimDrawEnv(void) {
    u32 pkt[40] = {
        0x10000009, 0, 0x10000000, 0x50000009,
        0x8008, 0x10000000, 0xE, 0,
        !(gGfx.frame & 1) ? 0x80070 : 0x80000, 0, 0x4C, 0,
        0x310000E0, 0, 0x4E, 0,
        0x44, 0, 0x42, 0,
        0x507FB, 0, 0x47, 0,
        0x60, 0, 0x14, 0,
        0, 0, 0x4A, 0,
        0, 0, 8, 0,
        1, 0, 0x46, 0,
    };

    Dma_AddData(pkt, 0xA0);
}

#ifdef PORT
#include "mod_ground_materials.h"
static TexFile*sGcProfileTex;static int sGcProfileStage=-1,sGcProfileCount;static const GcGroundProfile*sGcProfile;
void Port_GroundMaterialReset(void){sGcProfileTex=NULL;sGcProfileStage=-1;sGcProfileCount=0;sGcProfile=NULL;}
static u32 gc_material_mix(u32 h,u32 v){return (h^v)*16777619u;}
static u32 gc_material_signature(TexFile*t){u32 h=gc_material_mix(2166136261u,t->count);int i,j;
 for(i=0;i<t->count;i++){TexEntry*e=&t->ent[i];u8*p;
 h=gc_material_mix(h,e->pixSize);h=gc_material_mix(h,e->clutSize);h=gc_material_mix(h,e->tbpStep);h=gc_material_mix(h,e->cbpStep);h=gc_material_mix(h,e->pixBlt);h=gc_material_mix(h,e->clutBlt);h=gc_material_mix(h,(u32)e->tex0);h=gc_material_mix(h,(u32)(e->tex0>>32));
 if(e->clutOfs&&e->clut&&e->clutSize>=0x70){p=(u8*)e->clut+0x60;for(j=0;j<4;j++)h=gc_material_mix(h,(u32)p[j*4]|((u32)p[j*4+1]<<8)|((u32)p[j*4+2]<<16)|((u32)p[j*4+3]<<24));}
 if(e->pixOfs&&e->pix&&e->pixSize>=0x70){p=(u8*)e->pix+0x60;for(j=0;j<4;j++)h=gc_material_mix(h,(u32)p[j*4]|((u32)p[j*4+1]<<8)|((u32)p[j*4+2]<<16)|((u32)p[j*4+3]<<24));}
 }return h;
}
static const GcGroundProfile*gc_material_profile(int stage){TexFile*t;u32 sig;int i;extern void Port_GroundTrace(const char*,int,int,int,int);
 if(!gBtlStage||(gBtlStage->flags&1)||!gBtlStage->tex||!gBtlStage->tex->ent)return NULL;
 t=gBtlStage->tex;if(t->count<1||t->count>255)return NULL;
 if(sGcProfileTex==t&&sGcProfileStage==stage&&sGcProfileCount==t->count)return sGcProfile;
 sGcProfileTex=t;sGcProfileStage=stage;sGcProfileCount=t->count;sGcProfile=NULL;sig=gc_material_signature(t);
 for(i=0;i<sizeof(sGcGroundProfiles)/sizeof(sGcGroundProfiles[0]);i++){const GcGroundProfile*p=&sGcGroundProfiles[i];if(p->stage==stage&&p->count==t->count&&p->signature==sig){sGcProfile=p;break;}}
 Port_GroundTrace("profile(stage,count,signature,materialMinus1Unsupported)",stage,t->count,(int)sig,sGcProfile?sGcProfile->material:-1);return sGcProfile;
}
int Port_GroundStageMaterial(int stage){const GcGroundProfile*p=gc_material_profile(stage);return p?p->material:-1;}
void *Port_GroundStageTexture(s32 stage,s32 kind){const GcGroundProfile*p=gc_material_profile(stage);TexEntry*e;int index;
 if(!p||kind<0||kind>2)return NULL;index=p->tex[kind];if(index<0||index>=p->count)return NULL;
 e=&gBtlStage->tex->ent[index];if(!e->pix||!e->pixOfs||e->pixSize<=0||e->tbpStep<=0||e->tbpStep>1024||e->cbpStep<0||e->cbpStep>16)return NULL;return e;
}
#endif
