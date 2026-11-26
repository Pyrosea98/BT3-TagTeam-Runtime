#ifndef MOD_TERRAIN_BRIDGE_H
#define MOD_TERRAIN_BRIDGE_H
#include "mod_terrain_dynamic.h"
void Port_TerrainReset(void);
void Port_TerrainConfigure(int,int);
int Port_TerrainSnapshotNearBox(void*,const float*,const float*,float*);
int Port_TerrainPartOverlap(const void*,const float[][4],const float*,const float*);
int Port_TerrainSnapshot(void*);
int Port_TerrainActive(void);
int Port_TerrainEnabled(void);
void Port_TerrainSetMaterial(int,int);
int Port_TerrainGetMaterial(int);
int Port_TerrainAdd(void*);
int Port_TerrainImpact(void*);
int Port_TerrainImpactVisual(void*);
void Port_TerrainStonesTick(int paused);
int Port_TerrainStones(void *out);
int Port_TerrainCharge(void*,int);
int Port_TerrainHasScar(void*);
int Port_TerrainRemeshBox(void*,const void*,const void*,void (*)(const float*,void*),void*);
int Port_TerrainRemesh(void*,const void*,void (*)(const float*,void*),void*);
void Port_TerrainCollectTriangle(void*,void*,const void*);
int Port_TerrainWalls(void*,const void*,void (*)(const float*,void*),void*);
#endif
