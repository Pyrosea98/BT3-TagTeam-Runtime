/* Local power-gap reaction: 5x physical armor, 50x all-contact armor. */
#ifndef MOD_POWER_ARMOR_H
#define MOD_POWER_ARMOR_H
static int PA_Physical(int kind,int action,int reaction){
 if(kind<0||action<0||action>=0xEC)return 0;
 /* Grabs, throws, Ki-charge bursts and technique/script-only records are not fists. */
 if(kind==0x55||kind==0x56||kind==0x57||kind==0x66)return 0;
 if(reaction==1||reaction==9||reaction==0xE||reaction==0x1D||reaction==0x1E||reaction==0x22||reaction==0x29)return 0;
 return kind<=0x54||(kind>=0x59&&kind<=0x5E)||(kind>=0x67&&kind<=0x6B)||(kind>=0x7E&&kind<=0x86);
}
static int PA_Outclassed(long long attacker,long long defender){
 /* Division avoids overflow even for MAX / imported high-power identities. */
 return attacker>0&&defender>0&&attacker<=defender/5;
}
static int PA_Tier(long long attacker,long long defender){
 if(attacker<=0||defender<=0)return 0;
 return attacker<=defender/50?2:PA_Outclassed(attacker,defender)?1:0;
}
#endif
