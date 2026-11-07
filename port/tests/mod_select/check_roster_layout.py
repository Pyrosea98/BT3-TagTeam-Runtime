from pathlib import Path
import subprocess,tempfile,math
r=Path.cwd();s=(r/'port/include/mod_select_game.inc').read_text()
def extract(name):
 a=s.index('static '+('int 'if name=='ms_roster_key' else 'void ')+name+'(');i=s.index('{',a)+1;depth=1
 while depth:depth+=(s[i]=='{')-(s[i]=='}');i+=1
 return s[a:i]
code='''#include <assert.h>
#include <string.h>
#include <stdio.h>
#include "mod_select.h"
typedef struct {int id,formCount,form[7];}TsCell;
#include "mod_select_catalog.h"
static TsCell sMsCatalog[MS_ROSTER];
static int Port_Beta8StockAlias(int id){(void)id;return -1;}
/* FUNCTIONS */
int main(void){MsView v;int ids[]={29,66,11,3,0,51,234,214,213,146,170,34,231},i;memset(&v,0,sizeof(v));v.roster_count=sizeof(ids)/sizeof(ids[0]);for(i=0;i<v.roster_count;i++){v.roster[i]=ids[i];sMsCatalog[i].id=ids[i];sMsCatalog[i].form[0]=1000+ids[i];}
ms_sort_catalog(&v);assert(v.roster[0]==11&&v.roster[1]==0&&v.roster[2]==231&&v.roster[3]==3);assert(ms_roster_key(29)<ms_roster_key(34)&&ms_roster_key(34)<ms_roster_key(122));assert(ms_roster_key(261)<ms_roster_key(275)&&ms_roster_key(276)<ms_roster_key(146));for(i=0;i<v.roster_count;i++){assert(sMsCatalog[i].id==v.roster[i]&&sMsCatalog[i].form[0]==1000+v.roster[i]);if(i)assert(ms_roster_key(v.roster[i-1])<=ms_roster_key(v.roster[i]));}MsView saved=v;ms_sort_catalog(&v);assert(!memcmp(&saved,&v,sizeof(v)));assert(MS_COLS==7&&MS_PAGE==35);puts("PASS family ordering, sibling stability and form associations");return 0;}
'''
code=code.replace('/* FUNCTIONS */','\n'.join(extract(n)for n in ['ms_roster_key','ms_sort_catalog']))
with tempfile.TemporaryDirectory()as t:
 p=Path(t);(p/'test.c').write_text(code);subprocess.run(['cc','-std=c99','-w','-fsanitize=address,undefined','-Iport/include',str(p/'test.c'),'-o',str(p/'test')],check=True);subprocess.run([str(p/'test')],check=True)
for aspect in [4/3,16/9,21/9]:
 h=720;w=h*aspect;k=h/720;edge=24*k;left=w*.44;gap=6*k;cell=min((left-edge*2-gap*6)/7,(h-318*k)/5-gap);x=edge+(left-edge*2-(cell*7+gap*6))*.5
 assert cell>0 and x>=edge-1e-6 and x+cell*7+gap*6<=left-edge+1e-6 and 118*k+5*cell+4*gap<=h-180*k
print('PASS square seven-column grid bounds at 4:3, 16:9 and 21:9')
