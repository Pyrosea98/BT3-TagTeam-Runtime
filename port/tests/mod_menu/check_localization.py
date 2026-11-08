from pathlib import Path
import re,ast
root=Path(__file__).resolve().parents[3]
pat=r'"(?:\\.|[^"\\])*"'
def text(s):
    # ast handles both literal Unicode and octal byte escapes; join UTF8 byte escapes safely.
    v=ast.literal_eval(s)
    if all(ord(c)<256 for c in v):
        try:return v.encode('latin1').decode('utf8')
        except UnicodeError:pass
    return v
cat=(root/'port/src/gs/mod_settings_l10n.inc').read_text(encoding='utf8')
pairs=re.findall(r'\{('+pat+r'),\s*('+pat+r')\}',cat)
keys={text(a):text(b)for a,b in pairs}
assert len(keys)==len(pairs),'Duplicate translation keys'
fmt=re.compile(r'%(?!%)(?:[-+ #0]*)(?:[0-9]+|\*)?(?:\.[0-9]+)?(?:ll|l|z)?[diuoxXfFeEgGcsp]')
def args(v):return [x[-1]for x in fmt.findall(re.sub(r'(?<=\d)% (?=[A-Za-z])',' percent ',v.replace('%%','')))]
for en,es in keys.items():assert args(en)==args(es),(en,es,args(en),args(es))
files=list((root/'port/src/gs').glob('mod*'))+[root/'port/src/gs/ui.cpp',root/'port/src/gs/fight_panels_ui.cpp']
count=0
for p in files:
    src=p.read_text(encoding='utf8')
    for m in re.finditer(r'mod_l10n\(\s*((?:'+pat+r'\s*)+)\)',src):
        value=''.join(text(t)for t in re.findall(pat,m[1]))
        assert value in keys,(p.name,value)
        count+=1
    for tok in re.findall(pat,src):
        value=text(tok)
        assert not any(bad in value for bad in ['Ã','Â','�','â€']),(p.name,value)
print(f'PASS localization: {len(keys)} EN/ES pairs, {count} literal lookups; formatting/UTF8/coverage')
