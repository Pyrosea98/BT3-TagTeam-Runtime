#pragma once

// [injected] Function registry for guest code we bring ourselves: the recompiler's tables only
// cover the addresses that exist in the game's binaries, so a CLONE of a game module (e.g. the
// Duel menu copied to a new address range, see netmenutest_functions.cpp) has no slot anywhere.
//
// Registering here makes PS2Runtime::lookupFunction() dispatch those addresses to our C++ bodies.
// Consulted AFTER the generated tables and BEFORE the isCodeAddress() check, so real game code is
// never shadowed.

#include <cstdint>

struct R5900Context;
class PS2Runtime;

namespace ps2x_injected
{
    using Fn = void (*)(uint8_t *, R5900Context *, PS2Runtime *);

    // Idempotent per address; logs when it would shadow a generated function.
    void add(uint32_t addr, Fn fn);

    // nullptr when the address is not injected.
    Fn find(uint32_t addr);

    uint32_t count();
}
