from pathlib import Path
import subprocess,tempfile
r=Path.cwd()
with tempfile.TemporaryDirectory()as d:
 p=Path(d)/'body-change'
 subprocess.run(['cc','-DPORT','-std=gnu99','-w','-ffunction-sections','-fdata-sections','-fsanitize=address,undefined','-Iinclude','-Iport/include','src/battle/btl_char_action.c','src/battle/btl_char_ctl_b.c','port/src/plat_body_change.c','port/tests/native_hud/test_body_change.c','-Wl,--gc-sections','-lm','-o',str(p)],check=True)
 subprocess.run([str(p)],check=True)

source=(r/'src/battle/btl_char_mgr.c').read_text()
start=source.index('void BtlChar_OnModelLoaded(');end=source.index('void BtlChar_OnStageLoaded(',start);body=source[start:end]
assert body.index('Port_BodyChangeBeforeLoaded(chr)')<body.index('BtlChar_BindObject(chr)')
assert body.index('Port_BodyChangeLoaded(chr)')>body.index('Port_PatchFighterSkillData(chr)')
assert 'Port_BodyChangeLoaded(chr)'not in source[:start]
print('PASS actual native model-reload callback boundary: pre-bind identity and post-bind body gauges; no initial-setup completion hook')
