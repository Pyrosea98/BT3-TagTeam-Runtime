from pathlib import Path
import ast,subprocess,tempfile
r=Path.cwd();t=ast.parse((r/'port/tests/native_hud/check_native_assists.py').read_text());v={n.targets[0].id:n.value.value for n in t.body if isinstance(n,ast.Assign)and isinstance(n.value,ast.Constant)and isinstance(n.targets[0],ast.Name)}
h=v['head'].replace('[4]','[12]').replace('i<4','i<12').replace('return 4;','return 12;')
a=h.index('int Port_Setting(');b=h.index('\nint Port_FightConsumed',a)
h=h[:a]+'''int modes[2],fixedPercent[2]={250,250},beamBonus=200,beamCap=300;
int Port_Setting(const char*k,int d){
 if(!strcmp(k,"beam_assist_damage_mode"))return modes[0];if(!strcmp(k,"ultimate_boost_damage_mode"))return modes[1];
 if(!strcmp(k,"beam_assist_damage_fixed_percent"))return fixedPercent[0];if(!strcmp(k,"ultimate_boost_fixed_percent"))return fixedPercent[1];
 if(!strcmp(k,"beam_assist_damage_bonus_percent"))return beamBonus;if(!strcmp(k,"beam_assist_damage_cap_percent"))return beamCap;
 if(!strcmp(k,"assist_formation_enabled"))return 0;if(strstr(k,"enabled"))return !disabled;if(strstr(k,"_cpu"))return cpu;return d;}
'''+h[b:]
h=h.replace('void Port_AssistRushVictim(int,int,int){}','''int hits[12],dealt[12],by[12];
void Port_AssistRushVictim(int victim,int winner,int damage){assert(damage>=0&&damage<=10000);hits[victim]++;dealt[victim]+=damage;by[victim]=winner;action[victim]=0xD5;}''')
init=v['foot'].split('int main()')[0].replace('i<4','i<12')
checks=r'''void reset(){init();memset(hits,0,sizeof(hits));memset(dealt,0,sizeof(dealt));memset(by,0,sizeof(by));modes[0]=modes[1]=0;fixedPercent[0]=fixedPercent[1]=250;beamBonus=200;beamCap=300;for(int i=0;i<12;i++){team[i]=i&1;human[i]=0;}}
void confirm(int helper,int leader,int kind){sAssist[helper].target=leader+1;sAssist[helper].kind=kind;sAssist[helper].state=2;sAssist[helper].paid=1;as_credit_for(leader,kind,helper);action[helper]=7;team[helper]=team[leader];}
void decide(int w,int l,int count){*(int*)((char*)&actors[w]+0xE50)=count;Port_AssistRushOutcome(w,l);}
int main(){
 for(int kind:{1,3})for(int n=0;n<=4;n++)for(int mode=0;mode<=2;mode++){
  reset();clash=kind==1;action[0]=kind==1?304:261;action[1]=kind==1?304:11;modes[kind==1?0:1]=mode;
  for(int i=0;i<n;i++)confirm(2+2*i,0,kind);
  int expected=!n?1000:mode==1?1000*(1+n):mode==2?2500:kind==1?3000:1000+(n*500>2000?2000:n*500);
  int got=kind==1?Port_AssistBeamClashDamage(0,1000):Port_AssistDamage(0,4,1000);assert(got==expected);
  if(kind==1)assert(Port_AssistBeamClashDamage(1,1000)==1000);else assert(Port_AssistDamage(0,2,1000)==1000);
 }
 reset();assert(Port_AssistBeamClashDamage(0,3000000)==3000000);confirm(2,0,1);beamBonus=0;assert(Port_AssistBeamClashDamage(0,3000000)==3000000);modes[0]=2;fixedPercent[0]=100;assert(Port_AssistBeamClashDamage(0,3000000)==3000000);
 reset();action[0]=304;confirm(3,1,1);modes[0]=1;assert(Port_AssistBeamClashDamage(0,1000)==1000);assert(Port_AssistBeamClashDamage(1,1000)==2000);
 reset();modes[0]=1;confirm(2,0,1);as_credit_for(0,1,2);as_credit_for(0,1,2);assert(as_contributions(0,1)==1);action[2]=11;assert(as_find(2)==-1);assert(Port_AssistBeamClashDamage(0,1000)==2000);
 reset();clash=0;action[0]=261;modes[1]=1;sAssist[2].target=1;sAssist[2].kind=3;sAssist[2].state=1;assert(Port_AssistDamage(0,4,1000)==1000);
 reset();modes[0]=0;beamBonus=25;beamCap=1000;for(int i:{2,4,6,8})confirm(i,0,1);assert(Port_AssistBeamClashDamage(0,1000)==2000);assert(Port_AssistScore(0,10,1)==30);
 net=1;assert(Port_AssistBeamClashDamage(0,1000)==1000);net=0;replay=1;assert(Port_AssistBeamClashDamage(0,1000)==1000);replay=0;
 for(int unit:{60,600})for(int winner:{0,1}){
  reset();action[0]=action[1]=250;int loser=winner^1;int w0=winner?3:2,w1=winner?5:4,l0=winner?2:3,l1=winner?4:5;
  confirm(w0,winner,2);confirm(w1,winner,2);confirm(l0,loser,2);confirm(l1,loser,2);
  decide(winner,loser,10);assert(sRushOutcome[loser].participants==3);
  memset(sAssistPaidCount,0,sizeof(sAssistPaidCount));memset(sAssistPaidMask,0,sizeof(sAssistPaidMask));memset(sAssist,0,sizeof(sAssist));
  int expected=10*unit*3;if(expected>10000)expected=10000;
  assert(Port_AssistRushFinish(loser,0,unit)==expected); /* native winner counter may already be cleared */
  for(int i=0;i<12;i++)assert(!hits[i]);Port_AssistRushLaunch(loser);
  for(int i=0;i<12;i++)assert(hits[i]==(i==l0||i==l1));
  assert(dealt[l0]==expected&&dealt[l1]==expected&&by[l0]==winner&&by[l1]==winner&&action[l0]==0xD5);
  assert(Port_AssistRushFinish(loser,10,unit)==0&&hits[l0]==1);Port_AssistRushLaunch(loser);assert(hits[l0]==1);
 }
 reset();action[0]=action[1]=250;for(int i:{2,4,6,8})confirm(i,0,2);for(int i:{3,5,7,9})confirm(i,1,2);decide(0,1,2147483647);assert(Port_AssistRushFinish(1,2147483647,600)==10000);Port_AssistRushLaunch(1);for(int i:{3,5,7,9})assert(dealt[i]==10000&&hits[i]==1);assert(!hits[2]&&!hits[11]);
 reset();action[0]=action[1]=250;confirm(3,1,2);decide(0,1,500);action[3]=0xEC;assert(Port_AssistRushFinish(1,500,60)==10000&&!hits[3]);Port_AssistRushLaunch(1);assert(!hits[3]);
 reset();action[0]=action[1]=250;sAssist[3].state=1;sAssist[3].target=2;sAssist[3].kind=2;decide(0,1,10);assert(Port_AssistRushFinish(1,10,60)==600&&!hits[3]);
 reset();action[0]=action[1]=250;confirm(3,1,2);decide(0,1,1000);Port_AssistReset();assert(!sRushOutcome[1].state);net=1;assert(!Port_AssistRushLocal()&&Port_AssistRushFinish(1,1000,60)==0&&!hits[3]);
 reset();action[0]=action[1]=250;confirm(3,1,2);as_release(3);action[3]=11;decide(0,1,10);assert(sRushOutcome[1].victims&(1u<<3));assert(Port_AssistRushFinish(1,10,60)==600&&!hits[3]);Port_AssistRushLaunch(1);assert(hits[3]==1&&dealt[3]==600);
 puts("PASS damage modes: both kinds/all0..4helper counts/bonus/participants/fixed, unique paid identities, no repeat join, pending and opposing-side isolation; rush/vanish snapshot, all losing helpers, exactly-once, per-fighter10000 cap, overflow, unrelated/form actors and online/reset isolation; ASan/UBSan");}
'''
with tempfile.TemporaryDirectory()as d:
 p=Path(d);(p/'t.cpp').write_text('#include <initializer_list>\n'+h+(r/'port/src/mod_assists_native.inc').read_text()+init+checks);subprocess.run(['g++','-std=c++17','-fsanitize=address,undefined',str(p/'t.cpp'),'-o',str(p/'t')],check=True);subprocess.run([str(p/'t')],check=True)
