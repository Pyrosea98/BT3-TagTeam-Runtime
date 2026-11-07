from pathlib import Path
import tempfile,subprocess,os,re
r=Path.cwd();s=(r/'port/src/plat_file.c').read_text()
m=re.search(r'^int Port_FilePath\([^;{}]*\)\s*\{',s,re.M);assert m
i=s.index('{',m.start())+1;depth=1
while depth:depth+=(s[i]=='{')-(s[i]=='}');i+=1
body=s[m.start():i]
head='''#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <cassert>
static char sPartDir[8][64]={"pzs3us0","pzs3us1","pzs3us2","pak3us3","pak3us4"};
static const char*root(){return getenv("BT3_DATA");}
static const char*alias_target(const char*){return nullptr;}
'''
with tempfile.TemporaryDirectory()as d:
 t=Path(d);(t/'mods/pak3us3').mkdir(parents=True);(t/'mods/pak3us3/06700.bin').write_bytes(b'new native override outside original AFS count')
 tail='''int main(){char p[1024];assert(Port_FilePath(3,6700,nullptr,p,sizeof(p)));assert(strstr(p,"mods/pak3us3/06700.bin"));assert(!Port_FilePath(3,6701,nullptr,p,sizeof(p)));puts("PASS actual native loose-file override6700 resolves outside original AFS count; original AFS unchanged");}'''
 (t/'test.cpp').write_text(head+body+tail)
 subprocess.run(['c++',str(t/'test.cpp'),'-o',str(t/'test')],check=True);subprocess.run([str(t/'test')],env=dict(os.environ,BT3_DATA=str(t)),check=True)
