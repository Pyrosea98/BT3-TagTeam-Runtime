from pathlib import Path
import re,json,tempfile,subprocess
r=Path.cwd();s=(r/'port/src/gs/mod_settings_l10n.inc').read_text();rows=re.findall(r' \{("(?:\\.|[^"\\])*")\,("(?:\\.|[^"\\])*")\}',s)
assert len(rows)>=140
for a,b in rows:
 a,b=json.loads(a),json.loads(b);assert b and '\ufffd' not in b
 assert re.findall(r'%(?:\d+\$)?[0-9.*+-]*[a-zA-Z%]',a)==re.findall(r'%(?:\d+\$)?[0-9.*+-]*[a-zA-Z%]',b),(a,b)
head='#include <assert.h>\n#include <string.h>\nstatic int language;int Port_Setting(const char*,int){return language;}\n'
foot='int main(){for(auto &row:sModTranslations){language=0;assert(!strcmp(mod_l10n(row.en),row.en));language=1;assert(!strcmp(mod_l10n(row.en),row.es));language=0;assert(!strcmp(mod_l10n(row.en),row.en));}assert(!strcmp(mod_l10n("unknown"),"unknown"));}'
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp);(p/'test.cpp').write_text(head+s+foot);subprocess.run(['g++',str(p/'test.cpp'),'-o',str(p/'test')],check=True);subprocess.run([str(p/'test')],check=True)
print('PASS translation catalogue:',len(rows),'entries, format placeholders and English/Spanish/English live switching')
