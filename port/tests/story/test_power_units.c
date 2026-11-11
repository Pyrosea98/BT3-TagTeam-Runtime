#include <assert.h>
#include <string.h>
#include "mod_power_units.h"
#include "mod_power_armor.h"
#include "mod_damage_safe.h"
#include "mod_melee_profiles.h"
int main(void){char s[96];long long g=PU_Parse("2.25B",PU_BUnit(53,0)),bu=PU_Parse("132.000M",PU_BUnit(116,0));
 assert(g==2250000000000LL&&bu==132000000000LL&&PA_Tier(g,bu)==0&&PA_Tier(bu,g)==1);
 assert(PU_Parse("5B",PU_BUnit(52,0))==50*PU_Parse("100.000M",PU_BUnit(51,0)));
 assert(g==50*PU_Parse("45.000M",PU_BUnit(110,0)));
 assert(PU_Parse("4.5B",PU_BUnit(249,0))==g*2);
 assert(PU_Parse("18B",PU_BUnit(172,0))==g*8);
 assert(PU_Parse("10B",PU_BUnit(166,0))==10000000000000LL);
 assert(PU_Parse("40B",PU_BUnit(149,0))==40000000000000LL);
 assert(PU_Parse("40.000B",PU_BUnit(54,0))==40000000000000000LL);
 assert(PU_Parse("80.000B",PU_BUnit(59,0))==80000000000000000LL);
 assert(PU_Parse("5T",1000000000LL)==5000000000000000000LL);
 assert(PU_Parse("300Mil",1)==300000&&PU_Parse("104MM",1)==104000000000LL);
 assert(PU_Parse("2,25B",PU_BUnit(0,0))==g&&PU_Parse("MAX",1)==PU_MAX);
 assert(PU_Parse("9999999999999999999999999",1)==0&&PU_Parse("999999999999999999B",1000000000000LL)==0);
 assert(PU_Parse("nonsense",1)==0&&PU_Parse("5Q",1)==0&&PU_Parse(0,1)==0);
 PU_Format(g,s,sizeof(s));assert(!strcmp(s,"2.25B"));PU_Format(bu,s,sizeof(s));assert(!strcmp(s,"132MM"));
 assert(sMdPeaks[MD_ProfileIndex(53,0)]==2250000&&sMdPeaks[MD_ProfileIndex(52,0)]==5000000);
 assert(sMdPeaks[MD_ProfileIndex(149,0)]==40000000&&sMdPeaks[MD_ProfileIndex(54,0)]==40000000);
 assert(PU_Parse("80B",PU_BUnit(147,0))==80000000000000LL);
 assert(PU_Parse("6B",PU_BUnit(216,0))==50*PU_Parse("120.000M",PU_BUnit(214,0)));
 assert(PU_Parse("232.000B",PU_BUnit(230,0))==232000000000000000LL);
 assert(PU_Parse("9223372036854775807",1)==PU_MAX);
 assert(PU_Parse("9223372036854775808",1)==0);
 assert(PU_Parse("10T",1)==0);
 assert(PU_AtLeastPercent(5000000000000000000LL,PU_MAX,90)==0);
 assert(PU_AtLeastPercent(PU_MAX,5000000000000000000LL,150)==1);
 assert(PU_AtLeastPercent(PU_MAX,PU_MAX,150)==0);
 assert(PU_AtLeastPercent(90,100,90)&&!PU_AtLeastPercent(89,100,90));
 assert(PU_AtLeastPercent(150,100,150)&&!PU_AtLeastPercent(149,100,150));
 PU_Format(5000000000000000000LL,s,sizeof(s));assert(!strcmp(s,"5T"));
 puts("PASS fusion x50/x2/x4 progression, Gogeta above Buuhan, long-scale SSJ4 values, signed-64 MAX, T/B/MM/M parsing, normalized labels, bounds and matching melee profiles");
}