#ifndef MOD_GOGETTO_VOICE_H
#define MOD_GOGETTO_VOICE_H
/* Streamed voices have only the original roster's 100-line banks. SSJ2's
 * model donor166 otherwise addresses the soundtrack range in alternate audio.
 * This does not change the independently loaded character sound-effect banks. */
static int GV_StreamDonor(int logical,int original){switch(logical){
 case 234:return 51;
 case 235:case 238:case 239:case 241:return 52;
 default:return original;}}
#endif
