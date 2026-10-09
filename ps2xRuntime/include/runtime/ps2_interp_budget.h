#pragma once
#include <cstdint>

// Limit an uninterrupted interpreted stretch, never the lifetime of the game.
// Native dispatch is the progress boundary. Entry execution must be tracked
// separately: resetting this budget must not re-enable executeEntry semantics.
struct Ps2InterpreterBudget {
    uint64_t uninterrupted = 0;
    uint64_t total = 0;
    uint64_t nativeDispatches = 0;
    uint64_t limit;
    explicit Ps2InterpreterBudget(uint64_t maximum = 2000000000ull) : limit(maximum) {}
    bool instruction() { ++total; return ++uninterrupted <= limit; }
    void nativeProgress() { uninterrupted = 0; ++nativeDispatches; }
};
