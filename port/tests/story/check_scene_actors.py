"""A parked scene-only actor can never affect the fight: it is reported as consumed by both predicates every system
already uses for an absorbed fusion partner, and those systems keep it out. Checks the shipped sources, then runs the
real Port_FusionConsumedActor / Port_FightConsumed bodies against the parked predicate."""
import os
import pathlib
import subprocess
import tempfile

r = pathlib.Path(__file__).resolve().parents[3]
read = lambda p: (r / p).read_text()
fighters = read("port/src/plat_fighters.c")
seq = read("src/battle/btl_seq.c")

# the two predicates include parked scene actors
assert "int Port_FusionConsumedActor(int who){return who>=0&&who<16&&(sFusionOwner[who]!=0||Port_StorySceneParked(who));}" in fighters
assert "sFight[k].consumed || Port_StorySceneParked(k + 2)" in fighters
# ... and every fight system uses them (the exclusions a fusion partner already gets)
uses = {
    "battle end (alive teams)": (seq, "!Port_FusionConsumedActor(i) && (u32)Port_Team(i) < PORT_SEQ_TEAMS"),
    "time-up team judge": (seq, "BtlCharApi_GetHp(i) > 0 && !Port_FusionConsumedActor(i)"),
    "frozen: no AI / actions, untargetable": (read("src/battle/btl_char_get.c"), "if(Port_FusionConsumedActor(chr->player))return 1;"),
    "targeting skips frozen": (read("src/battle/btl_char_flag_opp.c"), "if(BtlChar_IsFrozen(c)||ChrCam_IsCutActive(c))return -1;"),
    "hit-stop / melee detection": (read("src/battle/btl_char_mgr.c"), "if(Port_FusionConsumedActor(i))continue;"),
    "HUD panels": (read("port/src/fight_panels.c"), "Port_FightConsumed(i-2)"),
    "native HUD": (read("src/battle/hud_b.c"), "Port_FightConsumed(i - 2)"),
    "revival": (read("port/src/mod_revive_native.inc"), "Port_FightConsumed(i-2)"),
    "assists": (read("port/src/mod_assists_native.inc"), "static int as_consumed(int i){return i>=2&&Port_FightConsumed(i-2);}"),
    "CPU fusion partner": (read("port/include/mod_cpu_tactics_native.inc"), "Port_FusionConsumedActor(partner)"),
    "parking: hidden, health 0, below the stage": (fighters, "if(o)BtlObj_SetPartVisible(o,0,0);if(m)*(int*)((unsigned char*)m+0x40)=0;if(p){p[0]=0;p[1]=-100000;p[2]=0;}"),
}
for what, (text, needle) in uses.items():
    assert needle in text, what
# scene ownership is by actor, not by side (three or more fighters share a side)
assert "Port_StorySceneActorShown(chr->player)" in read("src/battle/btl_act_j.c")
assert "Port_StorySceneActorShown(chr->player)" in read("src/battle/btl_char_action.c")
assert "Port_StorySceneActorShown(chr->side)" not in read("src/battle/btl_act_j.c") + read("src/battle/btl_char_action.c")

# the real predicate bodies over stub state
def body(name, end="}"):
    i = fighters.index(name)
    return fighters[i:fighters.index(end, i) + 1]
consumed = body("int Port_FusionConsumedActor(int who)")
fight = fighters[fighters.index("int Port_FightConsumed(int k) {"):]
fight = fight[:fight.index("\n}\n") + 3]
test = r"""
#include <assert.h>
#include <stdio.h>
static int sFusionOwner[16], sFightCount = 3; static struct { int consumed; } sFight[10];
static int parked[16]; int Port_StorySceneParked(int a) { return a >= 0 && a < 16 && parked[a]; }
""" + consumed + "\n" + fight + r"""
int main(void) {
    assert(!Port_FusionConsumedActor(3) && !Port_FightConsumed(1));
    parked[3] = 1; assert(Port_FusionConsumedActor(3) && Port_FightConsumed(1));   /* parked: out of every system */
    parked[3] = 0; assert(!Port_FusionConsumedActor(3) && !Port_FightConsumed(1)); /* in a cutscene: an actor of it */
    sFusionOwner[4] = 1; sFight[2].consumed = 1; assert(Port_FusionConsumedActor(4) && Port_FightConsumed(2)); /* fusion unchanged */
    assert(!Port_FusionConsumedActor(16) && !Port_FightConsumed(-1) && !Port_FightConsumed(5));
    puts("predicates ok"); return 0;
}
"""
with tempfile.TemporaryDirectory() as t:
    src = os.path.join(t, "p.c"); pathlib.Path(src).write_text(test); exe = os.path.join(t, "p")
    subprocess.run(["cc", "-std=gnu99", "-Wall", "-Wextra", "-fsanitize=address,undefined", src, "-o", exe], check=True)
    subprocess.run([exe], check=True)
print("PASS scene-only actors cannot affect the fight: consumed predicates cover parked actors; battle end, time-up judge, AI/actions, targeting, hit-stop, both HUDs, revival, assists and CPU fusion use them; parking hides, zeroes and removes; scene ownership by actor")
