#pragma once
#include <cstdint>

// [memstat] Resident set size of this process in MB (0 if it cannot be read).
uint64_t ps2xRssMB();

// [loglevel] The logging level from settings.toml [logging] log_level (0 OFF, 1 Balanced, 2 Detailed, 3 Debug),
// for the runtime's own print sites: per-second lines at 1, per-frame state at 2, per-call traces at 3.
int ps2xLogLevel();
