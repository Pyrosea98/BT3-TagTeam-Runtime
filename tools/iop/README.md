# Native IOP (R3000) — status

Subsystem for running the IOP modules (`data/IRX/*.IRX`) **recompiled to native** instead of
emulating/HLE-ing them. All of this is on the development branch
(`texpack-pak-texcache`), **not** in the product PR.

## Pipeline

1. **Recompile**: `ps2xRecomp` with `arch="r3000"` (MIPS I backend; rejects MMI/VU/FPU/COP1-2/64-bit).
   - Function map: `tools/iop/irx_functions.py` (inputs = export fptrs, JAL, aligned
     `lui+ori/addiu` pointers ≥0x100, and import stubs).
   - IRX imports → `runtime->iopImport(rdram, ctx, "<mod>", <ordinal>)` (kernel HLE).
   - Internal calls → `runtime->dispatchIopBranch(...)`; registration via
     `extern "C" void ps2x_register_<mod>()`.
2. **Integrate**: the generated files go to `ps2xRuntime/src/iop_native/<mod>/` and are listed in
   `ps2xRuntime/CMakeLists.txt` (`target_sources(ps2EntryRunner ...)`).
3. **Run**: `SifLoadModule` maps the IRX into **IOP RAM** (the same sparse mapping with `rdram`
   shifted by base) and calls its native `entry`.

## Runtime (ps2_runtime.cpp)

- **IOP RAM**: sparse mapping (`mmap MAP_NORESERVE`, Linux) covering RAM + the
  `0x1F80xxxx`/`0xBF80xxxx` registers; each module gets its **own base** (the module's real size).
- **Cooperative IOP scheduler**: threads yielding via exception (`IopYield`); resume through
  `ctx->pc` (recompiler resume labels). `WaitSema`/`WaitEventFlag`/`Sleep/DelayThread`
  park; `SignalSema`/`SetEventFlag`/`WakeupThread` wake.
- **Kernel HLE** (`iopImport`): `loadcore`, `stdio`, `sysclib`, `dmacman`, `thbase`, `thevent`,
  `thsemap`, `intrman`, `sysmem`, `sifman`, `sifcmd` with real state (threads/semaphores/events).
- **SIF RPC**: `sifcmd` registers servers (`sceSifRegisterRpc`); `ps2xInvokeIopRpc` delivers
  EE RPCs to the native handler (cross-space copy + `(command,data,size)` ABI).
- **Loader**: `loadAndRunIopModule` — guard: invalid header `text==0` → HLE.

## Flags

| env | effect |
|---|---|
| `PS2X_IOP_DIR` | IRX folder (default `data/IRX/`) |
| `PS2X_IOP_MODULES` | list of stems to run natively, or `all` (default: validated set) |
| `PS2X_IOP_SCHED=0` | disables the scheduler |
| `PS2X_IOP_NATIVE_DBCMAN=0` | disables RPC routing from pad to native DBCMAN |
| `PS2X_IOP_NOENTRY=<substr>` | loads the module without running its `entry` (diagnostic) |
| `PS2X_IOP_WATCHDOG=<sec>` | SIGALRM + backtrace if an entry hangs (diagnostic) |
| `PS2X_IOP_XCALL_RESOLVE_ONLY=1` | resolves cross calls without executing the callee |

Validated set (default): `SIO2MAN,SIO2D,DBCMAN,LIBSD,SDRDRV,CDVDSTM,MCMAN`. **Native pad by
default** via DBCMAN.

## Modules

### Working natives (run in-game, do not break boot)
| module | notes |
|---|---|
| `SIO2MAN` | entry 0x634; its thread waits on the SIO2 interrupt (event flag) |
| `DBCMAN` | **native pad** (registers 4 RPC servers; game RPCs → native handlers) |
| `SIO2D` | entry 0xe14 |
| `LIBSD` | library (registers no RPC) |
| `SDRDRV` | entry 0x3a0 |
| `CDVDSTM` | entry 0x15dc |
| `MCMAN` | memory card manager (entry 0x178) |
| `MCSERV` | memory card server (entry 0x40) |
| `DS2U_D` | DualShock 2 driver (entry 0x0) |
| `SOUNDS` | sound driver |
| `MODHSYN` | synthesis |
| `MODSESQ2` | sequencer |

Default: `SIO2MAN,SIO2D,DBCMAN,LIBSD,SDRDRV,CDVDSTM,MCMAN,MCSERV,SOUNDS,MODHSYN,MODSESQ2,DS2U_D`.

### Broken / blocked
All 17 integrated IRX modules run natively. The **re-entrancy deadlock** bug (`iopImport`
held a non-recursive mutex and re-entered through the cross call) was what caused the
`MCMAN`/`MCSERV`/`DS2U_D`/`CRI_ADXI` stalls. Fixed with `std::recursive_mutex`.

`DS2O_D`, `MODMIDI`, `MODSEIN`, `MODSESQ` are integrated and enabled, but the game **does not load
them at boot** (they load later or via `IOPRP300.IMG`).

Probably functional (they return and boot): `SOUNDS`, `MODHSYN`, `MODSESQ2` — still needs visual
validation that the game progresses to FIGHT.

## Bugs already fixed
- **IOP header**: read 34 bytes from `p_offset` (several declare `p_filesz` < 34, e.g. MCMAN → 32).
- **Module bases**: reserve the real size (text+data+bss); fixed 64 KB made MCMAN
  (~74 KB) overwrite the next module.
- **Start at 0** valid for IRX (code at vaddr 0).
- **Exports with interior 0 slots**: do not truncate at the first 0.

## Next steps
1. **CRI_ADXI**: isolate which write corrupts (watchpoints / region bounds).
2. **DS2*/MOD***: model **SIO2 hardware/IRQ** (registers + interrupt) so the pad/sequencer
   driver waits resolve.
3. **MC (MCMAN+MCSERV)**: attack them together; they need real SIF (EE↔IOP) and/or SIO2 plus the
   `ioman`/`secrman`/`cdvdman` chain.
4. `IOPRP300.IMG`: IOP kernel modules still untouched.

## IOPRP300.IMG (IOP kernel + residents)

`tools/iop/ioprp.py` lists/extracts the image (ROMDIR): 16 ELF modules:
`SYSMEM LOADCORE SIFCMD SIFMAN THREADMAN IOMAN MODLOAD FILEIO CDVDMAN CDVDFSV LOADFILE TIMEMANI
ROMDRV EESYNC SYSCLIB STDIO`. Our **kernel HLE** covers the kernel ones
(sysmem/loadcore/sifcmd/sifman/threadman/intrman/…).

**Missing providers** (imported by the game's IRX, neither HLE'd nor present as a module):
- Resident in IOPRP but not HLE'd: **`ioman`, `cdvdman`, `modload`**.
- Do not exist as a module: **`secrman`, `heaplib`, `thmsgbx`, `timrman`, `vblank`**.

Today those calls return 0 (the game still boots and reaches FIGHT). For full fidelity
(MC, CD, timers, vblank) they need HLE-ing or recompiling the IOPRP residents.

### Confirmation by ordinal
Almost all imports of `ioman`/`cdvdman`/`modload`/`fileio`/… are **between IOPRP modules**
(CDVDFSV↔CDVDMAN↔FILEIO↔…), not from the game. The **game** modules use:

| game module | import | ordinal | semantics | "healthy" value |
|---|---|---|---|---|
| `MCMAN`,`CDVDSTM` | `ioman` | 20/21 | `AddDrv`/`DelDrv` | **0** (success) |
| `MCMAN`,`SIO2D` | `secrman` | 4/5/6 | Set handlers / `SecrAuthCard` | **0** (ok) |
| `MCMAN` | `modload` | 13 | module load | 0 |
| `MCMAN` | `cdvdman` | 24 | CD | 0 |
| `SOUNDS` | `timrman` | 4/6/20/22/23/24 | timers | id/0 |
| `CRI_ADXI`,`SIO2D` | `vblank` | 4/5 | Wait* | 0 |

The default (0 for anything unhandled) **already matches** those success values, which is why
boot does not break. Only `timrman#4` (AllocHardTimer) returns a new id.

**Added complements**: `ioman` (device registry + all `ops` routed to the native
drivers), `cdvdman` (sync/break/nop→1, disktype→DVD, readclock), `modload`→0, `secrman`→0 (success),
and **SIO2/PIO**: a write to CTRL sets STAT "ready" (+ initial STAT on read) so the drivers that
poll SIO2 transfers exit.
