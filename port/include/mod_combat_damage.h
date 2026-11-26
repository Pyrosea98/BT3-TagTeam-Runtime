#ifndef MOD_COMBAT_DAMAGE_H
#define MOD_COMBAT_DAMAGE_H

#ifdef PORT
int Port_CombatDamageFrom(void *victim, int amount, int flags, int attacker);
#else
#define Port_CombatDamageFrom(victim, amount, flags, attacker) BtlMember_Damage(victim, amount, flags)
#endif

#endif
