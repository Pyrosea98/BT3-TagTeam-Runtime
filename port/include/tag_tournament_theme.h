#ifndef TAG_TOURNAMENT_THEME_H
#define TAG_TOURNAMENT_THEME_H
/* The native tournament kind is0World,1Big,2Cell,3OtherWorld,4Yamcha. */
static int TT_ThemeStage(int type,int other){switch(type){case 0:return 4;case 1:return 27;case 2:return 6;case 3:return other>=0?other:7;case 4:return 19;default:return 4;}}
static int TT_ThemeMusic(int type){switch(type){case 0:return 0x10B29;case 1:case 3:return 0x10B18;case 2:return 0x10B24;default:return 0x10B1B;}}
#endif
