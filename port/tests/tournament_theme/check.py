from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[3]
s=(root/'src/sys/gfxm_c.c').read_text();start=s.index('void Port_TournamentDraw(');end=s.index('\n}\n',start)+3
function=s[start:end]
harness=r"""
#include <cassert>
#include <cstring>
typedef int s32;typedef unsigned u32;
struct FlashCxform{float mul[4];short add[4];};
struct FlashShape{FlashCxform cx;};
struct FlashSlot{FlashShape *shape;};
struct Tl{FlashSlot *slots;unsigned depths;};
struct FlashClip{char name[64];Tl tl;};
struct Clips{FlashClip *items;unsigned count;};
struct Flash{Clips *clips;};
static FlashShape *panel,*text,*face;static FlashCxform original;
void Flash_Draw(Flash*){
 assert(panel->cx.mul[0]<original.mul[0]);assert(panel->cx.add[0]>original.add[0]);
 assert(!memcmp(&text->cx,&original,sizeof(original)));
 assert(!memcmp(&face->cx,&original,sizeof(original)));
}
"""
main=r"""
int main(){
 original={{1,1,1,0.5f},{0,0,0,0}};
 FlashShape p={original},t={original},f={original};panel=&p;text=&t;face=&f;
 FlashSlot slots[]={{&p},{&t},{&f}};
 FlashClip c[]={ {"mc_menu_plate_01",{slots,1}}, {"mc_menu_text_on",{slots+1,1}}, {"mc_face_plate",{slots+2,1}} };
 Clips clips={c,3};Flash flash={&clips};
 for(int i=0;i<100;i++){
  Port_TournamentDraw(&flash,i%2?"mc_menu_plate_01":nullptr);
  assert(!memcmp(&p.cx,&original,sizeof(original)));
  assert(!memcmp(&t.cx,&original,sizeof(original)));
  assert(!memcmp(&f.cx,&original,sizeof(original)));
 }
}
"""
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'theme.cpp').write_text(harness+function+main)
 subprocess.run(['c++','-Wall','-Wextra','-Werror',str(p/'theme.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
print('PASS tournament draw-only styling: labels/portraits unchanged; transforms restored over100 draws')
