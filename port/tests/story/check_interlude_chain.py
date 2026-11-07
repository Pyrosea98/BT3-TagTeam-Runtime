from pathlib import Path
import subprocess,tempfile
r=Path.cwd();assert "if(!Port_StoryInterludeAdvance())break" in (r/"src/menu/menu_a_b.c").read_text(), "Progress_Main must complete BOTH synchronous scenes before consuming result";s=(r/'port/tests/story/test_chapter1.c').read_text();s=s[:s.index('int main(')];s=s.replace('../../src/gs/mod_menu_model.h','gs/mod_menu_model.h')
s+=r"""
int main(){int before,zeni,ch,o;unsigned rank;
 Port_StoryChoose(4);Port_StoryFightStarted();Port_StoryResult(0,1,1);o=Port_StoryLastOutcome(&ch,&zeni);before=Save_GetMoney();rank=Port_StoryClearRank(4);
 assert((o==ST_CANON||o==ST_ALT)&&ch==4);
 assert(Port_StoryInterludeAdvance()&&Port_StoryChapter()==17);Port_StoryFightStarted();Port_StoryResult(0,1,1);
 assert(Port_StoryInterludeAdvance()&&Port_StoryChapter()==18);Port_StoryFightStarted();Port_StoryResult(0,1,1);
 assert(!Port_StoryInterludeAdvance());assert(Port_StoryLastOutcome(&ch,0)==o&&ch==4);assert(Save_GetMoney()==before&&Port_StoryClearRank(4)==rank);
 assert(Port_StoryTakeReturn());Port_StoryAcknowledge();assert(!Port_StoryInterludeAdvance());
 Port_StoryChoose(5);Port_StoryFightStarted();Port_StoryResult(0,1,1);o=Port_StoryLastOutcome(&ch,&zeni);before=Save_GetMoney();
 assert(Port_StoryInterludeAdvance()&&Port_StoryChapter()==19);Port_StoryFightStarted();Port_StoryResult(0,1,1);
 assert(Port_StoryWishPending()&&!Port_StoryWishPending());
 assert(Port_StoryInterludeAdvance()&&Port_StoryChapter()==20);Port_StoryFightStarted();Port_StoryResult(0,1,1);
 assert(!Port_StoryInterludeAdvance());assert(Port_StoryLastOutcome(&ch,0)==o&&ch==5&&Save_GetMoney()==before);
 puts("PASS CH6 KameHouse wish -> native Shenlong ONCE -> Kaio hurry -> ONE CH6 result; actual runtime scene chain 5 -> Lookout -> King Kai -> ONE chapter5 result; no duplicate rewards/ranks or click-triggered replay");}
"""
with tempfile.TemporaryDirectory() as td:
 p=Path(td)/'chain.c';p.write_text(s);subprocess.run(['cc','-w','-Iinclude','-Iport/include','-Iport/src','-fsanitize=address,undefined',str(p),'port/src/gs/tag_tournament_core.c','-o',str(p.with_suffix(''))],check=True);subprocess.run([str(p.with_suffix(''))],check=True)
