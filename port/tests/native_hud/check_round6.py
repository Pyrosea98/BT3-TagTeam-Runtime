from pathlib import Path
import subprocess,tempfile
r=Path.cwd()
with tempfile.TemporaryDirectory()as tmp:
 p=Path(tmp)
 for name in ['routes','trace']:
  exe=p/name
  subprocess.run(['gcc','-std=c99','-no-pie','-I'+str(r/'port/include'),'-ffunction-sections','-fdata-sections',str(r/f'port/tests/native_hud/check_round6_{name}.c'),'-Wl,--gc-sections','-lm','-o',str(exe)],check=True)
  subprocess.run([str(exe)],cwd=p,check=True)
