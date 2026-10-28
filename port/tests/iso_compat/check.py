from pathlib import Path
import argparse,json,os,struct,subprocess,tempfile
r=Path(__file__).resolve().parents[3]
p=argparse.ArgumentParser();p.add_argument('--profile',type=Path);args=p.parse_args()
s=(r/'src/battle/battle_load.c').read_text();a=s.index('static s32 Port_BattleVoiceFile(');b=s.index('\n}',a)+2;voice=s[a:b]
a=s.index('s32 BtlLoad_StepObject(');a=s.index('    case 0:',a)+len('    case 0:');b=s.index('    case 1:',a);obj=s[a:b]
a=s.index('s32 BattleSide_IsCharaUsable(');b=s.index('\n}',a)+2;usable=s[a:b]
head=r"""
#include <cassert>
#include <cstdio>
#include <cstring>
#include <cstdlib>
extern "C" {
#include "plat_beta8.h"
#include "plat_songs.h"
int PortExtras_Dir(const char*,const char*,char*,unsigned){return 0;}
int PortExtras_Scan(const char*,const char*,char (*)[128],int){return 0;}
void PortExtras_NameFromFile(const char*,char*,unsigned){}
void PortSongs_Mp3Start(const char*){}int PortSongs_Mp3Made(){return 0;}
int Port_NetSession(){return 0;}
int Port_ModelUsable(int,int,int);
int Port_FilePath(int volume,int entry,const char*,char *out,int size){snprintf(out,size,"%s/pak3us%d/%05d.bin",getenv("BT3_DATA"),volume,entry);return 1;}
}
using s32=int;using u64=unsigned long long;
struct UsableSide{struct{u64 bits[3];}charaBits;} usableSides[2];
UsableSide *Side(int n){return &usableSides[n];}
struct Save{int flags;} save,*gSaveData=&save;
#define SAVE_FLAG_VOICE 1
#define BTL_FILE_VOICE 0xBDA
#define BTL_FILE_VOICE_ALT 0xC7B
#define BTL_FILE_OBJECT 0xC1C
#define BTL_FILE_CHARA 0x590
#define BTL_JOB_KIND_OBJECT 2
#define BATTLE_FLAG_LOAD_OBJECT 1
#define PORT 1
struct Work{int flags;} work;
Work *Battle_GetWork(){return &work;}
struct BtlJob{int kind,chara,animChara,unk1C,state;};
int gBtlLoadHandle,requested;
int BtlRes_Request(int,int id,int,int){requested=id;return 1;}
"""
with tempfile.TemporaryDirectory() as d:
 t=Path(d);root=args.profile/'gamedata' if args.profile else t/'data'
 if not args.profile:
  (root/'beta8').mkdir(parents=True);(root/'pak3us2').mkdir()
  (root/'beta8/bundle-map.bin').write_bytes(struct.pack('<4sIII8I',b'B8RM',1,1,32,166,2,0,0x0400002c,0x0400002d,0x0400002e,0,0))
  params=bytearray(256);params[2]=4;(root/'beta8/btlparam.bin').write_bytes(params)
  (root/'beta8/voice-map.bin').write_bytes(struct.pack('<4sIII4I',b'B8VC',1,1,16,166,1,0x04000059,324352))
  (root/'beta8/iso-songs.tsv').write_text('65201\tISO test song\n../x\tInvalid path\n')
  (root/'pak3us2/65201.bin').write_bytes(bytes.fromhex('80000024031204020000bb80002bf20000000000'))
 tests="""int main(){save.flags=0;assert(Port_BattleVoiceFile(3)==0xBDD);save.flags=1;assert(Port_BattleVoiceFile(3)==0xC7E);\n"""
 if args.profile:
  rows=json.loads((root/'beta8/bundle-map.json').read_text())['rows']
  for row in rows:
   for kind,part in enumerate(row['bundle']):tests+=f'assert(Port_Beta8FileId({row["character"]},{row["costume_internal"]},{int(row["damaged"])},{kind})=={0x40000000|part["transport_id"]});\n'
  for row in rows:tests+=f'assert(Port_Beta8CharacterForModel({0x40000000|row["bundle"][0]["transport_id"]})=={row["character"]});\n'
  audio=json.loads((root/'beta8/iso-audio-import.json').read_text())
  for v in audio['voices']:tests+=f'assert(Port_Beta8VoiceFileId({v["character"]},{v["language"]})=={0x44000000|v["entry"]});\n'
  for row in rows:tests+=f'assert(Port_ModelUsable({row["character"]},{row["costume_internal"]},{int(row["damaged"])}));\n'
  for cid in sorted({row['character'] for row in rows if row['character']>=165}):tests+=f'assert(BattleSide_IsCharaUsable(0,{cid}));\n'
  portraits=json.loads((root/'beta8/portrait-map.json').read_text())['rows']
  for row in portraits:tests+=f'assert(Port_Beta8PortraitFileId({row["character"]},{row["costume"]})=={0x40000000|row["transport"]});\n'
  for row in audio.get('member_portraits',[]):tests+=f'assert(Port_Beta8MemberFileId({row["character"]},{row["side"]})=={0x44000000|row["entry"]});\n'
  small=json.loads((root/'beta8/small-portraits/manifest.json').read_text())['rows']
  for row in (x for x in small if x['character']>=165):tests+=f'{{void *p=Port_Beta8LoadSmallPortrait({row["character"]},{row["costume"]});assert(p);Port_Beta8FreeSmallPortrait(p);}}\n'
  expected=len(audio['songs'])
 else:
  expected=1
  tests+='assert(Port_Beta8SizeClass(166,2)==4);assert(Port_Beta8SizeClass(166,0)==-1);assert(Port_Beta8SizeClass(300,0)==-1);\n'
 tests+="""usableSides[0].charaBits.bits[0]=8;assert(BattleSide_IsCharaUsable(0,3));assert(!BattleSide_IsCharaUsable(0,161));assert(!BattleSide_IsCharaUsable(0,291));assert(!BattleSide_IsCharaUsable(0,-1));assert(Port_Beta8VoiceFileId(166,0)==0x44000059);save.flags=0;assert(Port_BattleVoiceFile(166)==0x44000059);
 BtlJob j={2,166,2,0,0};load_object(&j);assert(requested==Port_Beta8FileId(166,2,0,0));
 j={0,256,0,0,0};load_object(&j);assert(requested==0xD1C);
 j={2,3,2,1,0};load_object(&j);assert(requested==Port_Beta8ResolveFile(3,2,1,0,0x5B4));
 PortSongs_Init();"""
 tests+=f'assert(PortSongs_Count()=={expected});\n'
 tests+="""for(int i=0;i<PortSongs_Count();i++){char rel[64],out[512];snprintf(rel,sizeof(rel),"pzs3us2/%05d.bin",73000+i);assert(PortSongs_Alias(rel,out,sizeof(out)));FILE *f=fopen(out,"rb");assert(f);fclose(f);assert(PortSongs_IsFile(out));} }
"""
 (t/'test.cpp').write_text(head+voice+'\n'+usable+'\nint load_object(BtlJob *job){int id;'+obj+'}\n'+tests)
 objs=[]
 for name in ['plat_beta8','plat_songs','plat_fighters']:
  o=t/(name+'.o');subprocess.run(['gcc','-std=gnu99','-w','-ffunction-sections','-fdata-sections','-I'+str(r/'port/src'),'-I'+str(r/'port/include'),'-c',str(r/'port/src'/f'{name}.c'),'-o',str(o)],check=True);objs.append(str(o))
 subprocess.run(['c++','-std=c++17','-I'+str(r/'port/include'),str(t/'test.cpp')]+objs+['-Wl,--gc-sections','-o',str(t/'test')],check=True)
 env=dict(os.environ,BT3_DATA=str(root));subprocess.run([str(t/'test')],env=env,check=True)
print('PASS actual ISO model/voice/music consumers, fusion-object route, stock/prop isolation; catalogue profile='+str(bool(args.profile)))
