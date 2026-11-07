from pathlib import Path
import subprocess,tempfile
s=Path('src/menu/menu_c_c.c').read_text();a=s.index('void HistResult_UpdateSe(void)');b=s.index('\n/* The result screen',a)
code='#include <assert.h>\n#include <stdio.h>\n#define PORT 1\nstruct R{int seState;} result,*gHistResult=&result;int status,played;int StreamSe_GetStat(int i){return status;}void Bgm_Play(int n){played++;}\n'+s[a:b]+'\nint main(){result.seState=1;HistResult_UpdateSe();assert(result.seState==2);status=3;HistResult_UpdateSe();assert(result.seState==2);status=5;HistResult_UpdateSe();assert(result.seState==3);HistResult_UpdateSe();assert(result.seState==0&&played==1);result.seState=2;status=0;HistResult_UpdateSe();assert(result.seState==3);puts("PASS actual result audio gate: playing waits; complete/stopped release");}\n'
with tempfile.TemporaryDirectory()as d:
 p=Path(d);(p/'gate.c').write_text(code);subprocess.run(['cc','-fsanitize=address,undefined',str(p/'gate.c'),'-o',str(p/'gate')],check=True);subprocess.run([str(p/'gate')],check=True)
