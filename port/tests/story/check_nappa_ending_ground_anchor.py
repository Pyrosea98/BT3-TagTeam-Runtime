"""Exercise CH7 staging/release and ending anchors through the actual Story reducer."""
from pathlib import Path
import subprocess,tempfile
with tempfile.TemporaryDirectory() as d:
    exe=Path(d)/'ch7-scenes'
    subprocess.run(['cc','-w','-std=gnu99','-fsanitize=address,undefined','-DPORT','-Iinclude','-Iport/include','-Iport/src','port/tests/story/test_ch7_scene_boundaries.c','-lm','-o',str(exe)],check=True)
    result=subprocess.run([str(exe)],text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
    if result.returncode: print(result.stdout)
    result.check_returncode()
    print(result.stdout.strip().splitlines()[-1])
