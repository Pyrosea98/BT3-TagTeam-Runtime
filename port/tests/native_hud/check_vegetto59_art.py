from pathlib import Path
import subprocess,tempfile,shutil
r=Path.cwd()
# Private game-resource fixture is deliberately not committed or packaged.
if not (r/"analysis/codex-vegetto59-art/gamedata").is_dir():
 print("SKIP Vegetto59 private asset fixture absent; no game data is shipped in source");raise SystemExit(0)
with tempfile.TemporaryDirectory()as tmp:
 p=Path(tmp);shutil.copytree(r/'analysis/codex-vegetto59-art/gamedata',p/'gamedata')
 subprocess.run(['gcc','-D_POSIX_C_SOURCE=200809L','-std=c99','-no-pie','-Iport/include','-ffunction-sections','-fdata-sections','port/tests/native_hud/check_vegetto59_art.c','-Wl,--gc-sections','-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test'),str(p/'gamedata')],check=True)
