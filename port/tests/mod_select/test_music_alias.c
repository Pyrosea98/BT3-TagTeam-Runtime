#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../src/plat_songs.c"
int main(void){char path[512];sAliasCount=1;strcpy(sAliases[0].rel,"pzs3us2/73000.bin");strcpy(sAliases[0].target,"iso/pak3us2/65201.bin");
assert(PortSongs_Alias("pzs3us2/73000.bin",path,sizeof(path))&&!strcmp(path,sAliases[0].target));
assert(PortSongs_Alias("pak3us2/73000.bin",path,sizeof(path))&&!strcmp(path,sAliases[0].target));
assert(!PortSongs_Alias("pak3us2/73001.bin",path,sizeof(path)));assert(!PortSongs_Alias("pak3us1/73000.bin",path,sizeof(path)));
puts("PASS original and Beta8 partition names resolve identical soundtrack files");return 0;}
