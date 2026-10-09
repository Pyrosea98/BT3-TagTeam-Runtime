#pragma once
#include <cstdlib>

// Opt-in continuous logs for short diagnostic runs. Normal log caps remain.
inline bool ps2xContinuousTrace() {
    static const bool on = [] {
        const char *value = std::getenv("PS2X_TRACE_CONTINUOUS");
        return value && value[0] == '1';
    }();
    return on;
}
