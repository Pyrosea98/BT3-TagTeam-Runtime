/* One point of the defuse cinematic's crack: a vertex of the fused model's own surface, kept as the game skins it
   (blended between its part's node and that node's parent by w), so the crack moves with the body. Built by
   Port_DefuseCrackBuild (mod_defuse_clip.inc), placed every frame by Port_DefuseCrackPlace. */
#ifndef MOD_DEFUSE_CRACK_H
#define MOD_DEFUSE_CRACK_H
#define PD_CRACK_POINTS 24
typedef struct PortCrackPt {
    int node, parent;           /* model node ids */
    float w;                    /* weight of `node` (1 - w: `parent`) */
    float p[3], n[3];           /* rest position and normal, model space */
    float origin[3], porigin[3]; /* the two nodes' rest positions */
} PortCrackPt;
#endif
