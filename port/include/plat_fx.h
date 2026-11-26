#ifndef PORT_PLAT_FX_H
#define PORT_PLAT_FX_H

/*
 * Efectos propios del port (sangre y lo que venga despues).
 * Recursos: `assets/fx/*.png` (nuestros, SDL_LoadPNG). Nada del disco de PS2.
 *
 * Apagado por defecto: BT3_BLOOD=1 lo enciende. Si el PNG falta, queda apagado
 * y lo dice una vez por stderr.
 *
 * El renderer provee `Port_Fx_DrawSprite(rgba, w, h, x, y, size, alpha)`:
 * (x, y) en la pagina 512 x 448 del juego, centro del sprite; `size` el lado en
 * esa misma pagina; `alpha` 0..255. Es el unico punto de integracion.
 *
 * Tiempo: `Port_Fx_Tick` una vez por cuadro de SIMULACION (Battle_Update), no por
 * cuadro dibujado: la vida de los estallidos no depende de los FPS ni de las
 * imagenes intermedias. `Port_Fx_Draw` solo dibuja.
 *
 * Decision y posicion estan separadas a proposito: el golpe se decide donde el
 * juego ya resolvio la reaccion del rival (BtlHit_ApplyHit), y el punto de
 * impacto se conoce despues, cuando el juego pide el efecto (BtlChar_SpawnHitFx).
 * `Port_Fx_Arm` deja el pedido, `Port_Fx_Take` lo consume en el lugar del impacto.
 */

void Port_Fx_Init(void);        /* al arrancar (lo llama el modulo la primera vez) */
void Port_Fx_Term(void);
int  Port_Fx_Enabled(void);

/* Un golpe que entro: queda pendiente para el objeto `objId` hasta que se sepa donde pego. */
/* EE software-float -> host ABI: scale is Q8 (256 = 1.0), never a float argument. */
void Port_Fx_Arm(int objId, int kind, int scaleQ8, int spit);
/* Se lleva el pedido pendiente de `objId` (1 si habia). */
int  Port_Fx_Take(int objId, int *kind, float *scale, int *spit);
/* El impacto, ya con posicion: (x, y) en la pagina 512 x 448; `spit` agrega la escupida. */
void Port_Fx_BloodAt(int x, int y, int kind, int scaleQ8, int spit);

/* Color de la sangre: el sprite esta en gris y se tinta. SetTint fija el color de la PROXIMA sangre que nazca
   (cada gota guarda el suyo); ResetTint lo devuelve al rojo por defecto. Llamar Reset despues de emitir. */
void Port_Fx_SetTint(int r, int g, int b);
void Port_Fx_ResetTint(void);
/* Evaluated game-space endpoints, reprojected for every drawn view. */
void Port_Fx_WoundSegment(int actor,int mark,const void *start,const void *end,int r,int g,int b);
void Port_Fx_SurfaceState(int actor,int level,int profile,int r,int g,int b);

void Port_Fx_Tick(void);        /* una vez por cuadro de simulacion */
void Port_Fx_Draw(void);        /* una vez por cuadro dibujado, encima de la escena */
void Port_Fx_Clear(void);       /* arranque / fin de pelea: no queda nada vivo */
/* 1 si hay algo que dibujar ahora (un estallido vivo o un golpe esperando su punto de impacto). El overlay lo usa
   como admision: sin esto, un cuadro sin ninguna otra superposicion no se construye y la sangre no se veria. */
int  Port_Fx_Live(void);

#endif
