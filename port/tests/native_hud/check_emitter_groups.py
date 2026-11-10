from pathlib import Path
import subprocess,tempfile,re
r=Path.cwd()
with tempfile.TemporaryDirectory() as d:
 p=Path(d)/'emitter-groups'
 subprocess.run(['cc','-DPORT','-std=gnu99','-w','-ffunction-sections','-fdata-sections','-fsanitize=address,undefined','-Iinclude','-Iport/include','src/battle/eft_j.c','port/tests/native_hud/test_emitter_groups.c','-Wl,--gc-sections','-lm','-o',str(p)],check=True)
 subprocess.run([str(p)],check=True)

with tempfile.TemporaryDirectory() as d:
 p=Path(d)/'emitter-lifecycle'
 native=Path(d)/'eft_i.c'
 native.write_text(re.sub(r'^INCLUDE_ASM.*$', '', (r/'src/battle/eft_i.c').read_text(encoding='utf-8'), flags=re.M),encoding='utf-8')
 subprocess.run(['cc','-DPORT','-std=gnu99','-w','-ffunction-sections','-fdata-sections','-fsanitize=address,undefined','-Iinclude','-Iport/include',str(native),'port/tests/native_hud/test_emitter_lifecycle.c','-Wl,--gc-sections','-lm','-o',str(p)],check=True)
 subprocess.run([str(p)],check=True)
