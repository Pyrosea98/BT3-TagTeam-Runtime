#ifndef MOD_POWER_DISPLAY_H
#define MOD_POWER_DISPLAY_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif
const char *Port_CharaPower(int chara, int costume);
int Port_CharaPowerText(int chara, int costume, unsigned short *out, int cap);
void Port_FormatPowerInteger(long long value, char *out, size_t cap);
#ifdef __cplusplus
}
#endif

#endif
