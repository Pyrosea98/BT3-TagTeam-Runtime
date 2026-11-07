from pathlib import Path
import subprocess,tempfile
r=Path.cwd();s=(r/'port/include/mod_select_game.inc').read_text()
def extract(name):
 a=s.index('static int '+name+'(');i=s.index('{',a)+1;depth=1
 while depth:depth+=(s[i]=='{')-(s[i]=='}');i+=1
 return s[a:i]
fixture=(r/'port/tests/mod_select/owners_fixture.c').read_text().replace('/* ACTUAL_DRAFT_HELPERS */','\n'.join(extract(n)for n in ['ms_family','ms_taken','ms_random','ms_owner','ms_owners','ms_slot_for_pad','ms_fill','ms_first_empty']))
g=(r/'src/battle/btl_char_get.c').read_text();a=g.index('s32 BtlChar_GetCount(void)');i=g.index('{',a)+1;depth=1
while depth:depth+=(g[i]=='{')-(g[i]=='}');i+=1
viewer='#include <assert.h>\n#include <stddef.h>\n#define PORT 1\ntypedef int s32;\nstatic struct {int count;} roster,*gBtlChars;\n'+g[a:i]+'\nint main(void){assert(BtlChar_GetCount()==0);gBtlChars=&roster;roster.count=10;assert(BtlChar_GetCount()==10);gBtlChars=0;assert(BtlChar_GetCount()==0);return 0;}\n'
with tempfile.TemporaryDirectory()as tmp:
 for name,code in [('owners',fixture),('viewer_scope',viewer)]:
  p=Path(tmp);(p/(name+'.c')).write_text(code)
  subprocess.run(['cc','-std=c99','-Wall','-Wextra','-Wno-unused-function','-fsanitize=address,undefined','-Iport/include',str(p/(name+'.c')),'-o',str(p/name)],check=True)
  subprocess.run([str(p/name)],check=True)
print('PASS actual native roster-count getter outside/inside battle and after teardown; ASan/UBSan')
