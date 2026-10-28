#ifndef MOD_KEYBOARD_OWNER_H
#define MOD_KEYBOARD_OWNER_H
/* 0 Auto,1..4 manual player,5 original per-player bindings only. */
static int MK_Owner(int policy,unsigned active,unsigned controllers){
 int p;active&=15;if(!active)active=1;
 if(policy==5)return -1;
 if(policy>=1&&policy<=4&&(active&(1u<<(policy-1))))return policy-1;
 if(policy==0){unsigned missing=active&~controllers;if(missing)active=missing;}
 for(p=0;p<4;p++)if(active&(1u<<p))return p;
 return 0;
}
#endif
