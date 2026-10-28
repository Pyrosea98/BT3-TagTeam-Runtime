from pathlib import Path
import subprocess,tempfile,os
r=Path.cwd()
def extract(text,name):
 a=text.index('int '+name+'(') if 'int '+name+'(' in text else text.index('void '+name+'(')
 i=text.index('{',a)+1;level=1
 while level:level+=(text[i]=='{')-(text[i]=='}');i+=1
 return text[a:i]
f=(r/'port/src/plat_fighters.c').read_text();m=(r/'port/src/plat_modes.c').read_text()
head=r"""#include <assert.h>
#include <stdio.h>
#include <string.h>
extern "C" {int Port_Setting(const char*,int);void Port_SettingSave(const char*,int);void Port_SettingsWrite(void);int Port_SettingsFlush(void);}
static int tour,sEntered;
int Port_TagTourActive(){return tour;}int Port_TagTourConfig(int){return 2;}
const char *Port_ModesName(int){return "mode";}const char *Port_ModesPlayersName(int){return "players";}
int Port_ModesChoose(int n){return n;}
static int sMatchPlayers;
"""
body='void Port_CoopReset(void){}\n'+'\n'.join(extract(f,n) for n in ['Port_LocalPlayersPreference','Port_LocalPlayersMatchSet','Port_LocalPlayers','Port_LocalPlayersSet'])+'\n'+extract(m,'Port_ModesChoosePlayers')+'\n'+extract(m,'Port_ModesLeave')
foot=r"""int main(int argc,char **argv){assert(argc==2);if(!strcmp(argv[1],"write")){
 Port_LocalPlayersSet(4);assert(Port_LocalPlayersPreference()==4);assert(Port_LocalPlayers()==4);
 Port_ModesChoosePlayers(0,1);assert(Port_LocalPlayers()==2&&Port_LocalPlayersPreference()==4);
 Port_ModesChoosePlayers(0,3);assert(Port_LocalPlayers()==3&&Port_LocalPlayersPreference()==4);
 tour=1;assert(Port_LocalPlayers()==2&&Port_LocalPlayersPreference()==4);tour=0;
 Port_ModesLeave();assert(Port_LocalPlayers()==4);assert(Port_SettingsFlush());
 }else{assert(Port_LocalPlayersPreference()==4&&Port_LocalPlayers()==4);}
 puts("PASS actual local-player preference survives match1/3/tournament2/main-menu and process restart");}
"""
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp);(p/'test.cpp').write_text(head+body+foot)
 subprocess.run(['cc','-c','-pthread',str(r/'port/src/plat_settings.c'),'-o',str(p/'settings.o')],check=True)
 subprocess.run(['g++','-pthread',str(p/'test.cpp'),str(p/'settings.o'),'-o',str(p/'test')],check=True)
 env={**os.environ,'BT3_SETTINGS':str(p/'settings.txt')}
 for mode in ['write','read']:subprocess.run([str(p/'test'),mode],env=env,check=True)
