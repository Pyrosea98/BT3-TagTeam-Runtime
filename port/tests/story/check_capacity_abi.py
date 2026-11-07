"""The three real AI manager views must agree through actor15, not just their count macros."""
from pathlib import Path
import subprocess,tempfile
with tempfile.TemporaryDirectory() as d:
 p=Path(d);files=[]
 (p/'types.h').write_text(Path('include/types.h').read_text().replace('typedef long s64;', 'typedef long long s64;').replace('typedef unsigned long u64;', 'typedef unsigned long long u64;'))
 for tag,header,ty,field in [('a','btl_ai.h','BtlAi','work'),('b','btl_ai_act.h','AiActMgr','side'),('c','btl_ai_think.h','AiThMgr','work')]:
  f=p/(tag+'.c');f.write_text('#include <stddef.h>\n#include "battle/'+header+'"\nint '+tag+'(int k){return k==0?sizeof('+ty+'):k==1?offsetof('+ty+',frame):sizeof((('+ty+'*)0)->'+field+')/sizeof((('+ty+'*)0)->'+field+'[0]);}\n');files.append(str(f))
 f=p/'main.c';f.write_text('#include "battle/btl_obj.h"\n#include "port_body_contacts.h"\nint a(int),b(int),c(int);void emit(int n){char b[16];int z=0;do{b[z++]=48+n%10;n/=10;}while(n);for(int i=0;i<z/2;i++){char t=b[i];b[i]=b[z-1-i];b[z-1-i]=t;}b[z++]=32;__asm__ volatile("int $0x80"::"a"(4),"b"(1),"c"(b),"d"(z):"memory");}void _start(void){for(int j=0;j<3;j++){emit(a(j));emit(b(j));emit(c(j));}int bad=0;for(int k=0;k<3;k++){bad|=a(k)!=b(k);bad|=b(k)!=c(k);}bad|=a(2)!=16;bad|=BTL_RES_SLOT_MAX<14;bad|=BTL_OBJ_MAX<16;bad|=PORT_BODY_OBJECTS!=BTL_OBJ_MAX;bad|=sizeof(gPortBodyTouch)/sizeof(gPortBodyTouch[0])!=BTL_OBJ_MAX;bad|=sizeof(gPortBodyTouch[0])!=4;__asm__ volatile("int $0x80"::"a"(1),"b"(bad):"memory");__builtin_unreachable();}\n');files.append(str(f))
 subprocess.run(['cc','-m32','-nostdlib','-static','-fno-pie','-no-pie','-std=gnu99','-DPORT','-I'+str(p),'-Iinclude','-Iport/include',*files,'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
print('PASS native32 AI manager ABI: all3 views agree on size, trailing fields and16 actors; resource/object capacity covers them')
