# Contributing

Thanks for your interest in BT3-Recomp! This is a statically recompiled, native
PC port of *Dragon Ball Z: Budokai Tenkaichi 3* (PS2, USA, SLUS-21678). It ships
**no game content** — the game is recompiled at build time from the user's own
disc dump.

## Before you start

- Read the [README](README.md) for the build/deploy flow and the
  [repository layout](README.md#repository-layout).
- You need **your own legally obtained BT3 USA disc dump** to build and run.
- The project is **GPL-3.0**; by contributing you agree your work is licensed
  under the same terms (see [`LICENSE`](LICENSE)).

## Building

- Linux: `./scripts/build-linux.sh --iso <your.iso> --output <dir>`
  (or the Docker flow on LTS — see the README).
- Windows: `.\scripts\build-windows.ps1 -Iso "<your.iso>"`.
- macOS (experimental): `./scripts/build-macos.sh --iso <your.iso>`.

For a quick rebuild of only the runner after a source change, reuse an existing
`games/bt3/work/` tree with `--skip-setup` (Linux/macOS) or `-SkipSetup`
(Windows). See [`docs/DEPLOY.md`](docs/DEPLOY.md).

## Where things live

| Area | Path |
| --- | --- |
| Static recompiler | `ps2xRecomp/` |
| Runtime (EE/GS/VU1/scheduler) | `ps2xRuntime/src/lib/` |
| GS/GPU renderer | `ps2xRuntime/src/lib/ps2_gs_gpu_renderer.cpp`, `ps2_gs_gpu.cpp` |
| In-runtime front-end (UI, settings, install wizard) | `ps2xRuntime/src/frontend/` |
| Build pipeline | `games/bt3/setup.py`, `scripts/` |

## Making a change

1. Fork and branch from `main` (`feature/…`, `fix/…`, `docs/…`).
2. Keep the change focused; one topic per PR.
3. Match the surrounding style. Comments in this tree are dense and explain
   *why* (often with a measurement and a date) — follow that convention for
   non-obvious changes.
4. Build and, when possible, run the affected path before opening the PR.
5. Open the PR against `main` and describe: what changed, why, how it was
   tested, and any platform notes (Windows vs Linux behaviour can differ).

## Commit messages

Prefix the subject with an area, then a concise summary — matching the existing
history:

```
gs: <what changed>
runtime: <what changed>
frontend: <what changed>
iop: <what changed>
build: <what changed>
docs: <what changed>
```

Add a body when the change is non-obvious.

## Debugging & logs

- The runner writes `logs/bt3.log` (and `bt3.prev.log`) next to the deploy.
- Windows crash reports are written to `logs/wincrash.txt`.
- Useful env knobs are documented inline where they are read (grep for
  `PS2X_`); most are opt-in and default off.

## Reporting issues

Include: your OS, CPU/GPU, the deploy `logs/`, and the exact steps to reproduce.
Do **not** attach game content or your disc dump.

## License

GPL-3.0 (see [`LICENSE`](LICENSE)). Bundled third-party components and their
licenses are listed in [`THIRD-PARTY-NOTICES.md`](THIRD-PARTY-NOTICES.md).
