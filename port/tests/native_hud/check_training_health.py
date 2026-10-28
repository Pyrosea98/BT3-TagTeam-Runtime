from pathlib import Path
import ast,subprocess,tempfile
r=Path.cwd();tree=ast.parse((r/'port/tests/native_hud/check_native_assists.py').read_text());data={}
for n in tree.body:
 if isinstance(n,ast.Assign) and isinstance(n.value,ast.Constant):
  for name in n.targets:
   if isinstance(name,ast.Name):data[name.id]=n.value.value
head=data['head'];init=data['foot'].split('int main()')[0]
head=head.replace('int Port_Setting(', 'static int heal=1,refillKi,refillStock,practice=3;int Battle_GetMode(){return 0;}int Port_ModesFight(){return practice;}int Port_Setting(')
head=head.replace('if(strstr(k,"enabled"))','if(!strcmp(k,"training_refill_health"))return heal;if(!strcmp(k,"training_refill_ki"))return refillKi;if(!strcmp(k,"training_refill_stocks"))return refillStock;if(strstr(k,"enabled"))')
foot=r"""int main(){init();Port_TrainingHealthReset();members[0][0x40/4]=5000;for(int i=0;i<60;i++)Port_TrainingHealthTick();assert(members[0][0x40/4]==5000);Port_TrainingHealthTick();assert(members[0][0x40/4]==10000&&Port_TrainingNoKO(0));members[0][0x40/4]=4000;Port_TrainingHealthTick();for(int i=0;i<30;i++)Port_TrainingHealthTick();members[0][0x40/4]=3000;Port_TrainingHealthTick();for(int i=0;i<30;i++)Port_TrainingHealthTick();assert(members[0][0x40/4]==3000);pause=1;int remaining=sPracticeDelay[0];Port_TrainingHealthTick();assert(sPracticeDelay[0]==remaining);pause=0;
 heal=0;assert(!Port_TrainingNoKO(0));refillKi=refillStock=1;members[0][0x4c/4]=0;members[0][0x54/4]=0;members[0][0x58/4]=300000;Port_TrainingHealthTick();assert(members[0][0x4c/4]==100000&&members[0][0x54/4]==300000&&members[0][0x40/4]==3000);
 net=1;assert(!Port_TrainingNoKO(0));net=0;replay=1;assert(!Port_TrainingNoKO(0));replay=0;practice=0;assert(!Port_TrainingNoKO(0));practice=3;heal=1;members[0][0x40/4]=0;assert(!Port_TrainingNoKO(0));Port_TrainingHealthTick();assert(members[0][0x40/4]==0);Port_TrainingHealthReset();assert(!sPracticeHp[0]&&!sPracticeDelay[0]);puts("PASS actual native training health: delayed all-player refill, damage resets, no dead revival, pause, Ki/stocks, no-KO admission, online/replay/nonpractice/reset isolation; ASan/UBSan");}
"""
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp);(p/'test.cpp').write_text('#include <initializer_list>\n'+head+(r/'port/src/mod_assists_native.inc').read_text()+(r/'port/src/mod_training_health_native.inc').read_text()+init+foot)
 subprocess.run(['g++','-std=c++17','-fsanitize=address,undefined','-fno-omit-frame-pointer',str(p/'test.cpp'),'-o',str(p/'test')],check=True);subprocess.run([str(p/'test')],check=True)
assert '|| Port_TrainingNoKO(chr->side)' in (r/'src/battle/btl_char_member.c').read_text()
