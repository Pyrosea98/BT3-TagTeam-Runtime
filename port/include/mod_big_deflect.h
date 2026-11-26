#ifndef MOD_BIG_DEFLECT_H
#define MOD_BIG_DEFLECT_H
/* The request is bound to the incoming owner and expires with the native
 * defence action. No gauge is spent until a collision is accepted. */
int Port_BigDeflectScan(int actor);
int Port_BigDeflectMatch(int actor, int owner);
void Port_BigDeflectReset(void);
void Port_BigDeflectClear(int actor);
#endif
