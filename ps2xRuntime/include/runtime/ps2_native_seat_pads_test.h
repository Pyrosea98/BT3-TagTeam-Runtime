#pragma once
#include "runtime/ps2_native_seat_pads.h"
#include <vector>
#include <cstdio>
namespace ps2_native_seats {
inline int selfTest(){
    unsigned checks=0;auto check=[&](bool ok,const char* text){++checks;if(!ok)std::fprintf(stderr,"[native-seat-test] FAIL %s\n",text);return ok;};
    bool ok=true;
    auto n=encode({});ok&=check(n.word==0&&n.lx==0&&n.ly==0&&n.padding[2]==0,"neutral ABI");
    auto all=encode({0,128,128,128,128});
    ok&=check(all.word==0xffff&&all.rx==0&&all.ry==0,"all digital PS2 buttons invert exactly");
    auto tiny=encode({0xffff,191,64,128,128});
    ok&=check((tiny.word&0xf0000)==0x80000&&tiny.lx<.5f,"quantized threshold and dominant axis");
    auto r=encode({uint16_t(0xffff^(1<<14)),255,128,128,128});
    ok&=check(r.word==0x24000&&r.lx==1&&r.ly==0,"right + cross");
    auto u=encode({0xffff,128,1,128,255});
    ok&=check(u.word==0x480000&&u.ly==-1&&u.ry==1,"Y endpoints / guest down-up mapping");
    auto d=encode({0xffff,255,255,0,0});
    ok&=check(d.word==0x840000&&std::fabs(d.lx-.70710678f)<1e-6&&std::fabs(d.ry+.70710678f)<1e-6,"circular diagonal / y dominance");
    // Native selector MENU_BITS turns nibble8 into UP, nibble4 into DOWN.
    ok&=check((u.word&0x80000)!=0&&(u.word&0x400000)!=0,"guest selector consumes expected directions");
    std::vector<uint8_t> bytes(0x08000000);Memory m{bytes.data(),bytes.size()};Service s;
    unsigned polls=0;auto poll=[&](unsigned p){++polls;return Packet{uint16_t(0xffff^(1<<p)),255,128,128,128};};
    ok&=check(s.frame(m,poll)==0&&polls==0&&m.read(0x06c1f010)==0,"absent magic has no writes/polls");
    constexpr uint32_t manager=0x100000,scene=0x200000,c=0x06c1f000;
    m.write(manager,2);m.write(0x2feb14,manager);m.write(0xd8080,1);m.write(0xd8084,6);m.write(0xd8088,manager);m.write(0xd808c,6);
    m.write(c,0x51494e31);m.write(c+4,manager);m.write(c+8,6);m.write(c+12,1);m.write(c+0x48,4);m.write(c+0x4c,0xffffffff);
    m.write(0x06c10000,0x27bdff20);m.write(0x06c10004,0xffa20000);
    ok&=check(s.frame(m,poll)==1&&polls==2&&m.read(c+16)==1&&m.read(c+28)==0,"owned match handshake");
    ok&=check(m.read(0x06c15000)==0x20004&&m.read(0x06c15020)==0,"unused P4 neutralized");
    Record expected[2]={encode({uint16_t(0xffff^4),255,128,128,128}),Record{}};
    ok&=check(std::memcmp(bytes.data()+0x06c15000,expected,64)==0,"exact two-record mailbox ABI including zero padding");
    // Reproduce the guest lease consumer: a nonzero changed sequence and a
    // clear writer flag admit the complete pair; no change rejects stale input.
    uint32_t guestLast=0;bool consumed=m.read(c+28)==0&&m.read(c+16)!=guestLast;
    guestLast=m.read(c+16);
    ok&=check(consumed&&guestLast==1&&m.read(c+16)==guestLast,"guest handshake admission and stale sequence");
    m.write(c+16,0xffffffff);s.frame(m,poll);ok&=check(m.read(c+16)==1,"sequence wraps nonzero");
    m.write(0xd8088,manager+4);unsigned before=polls;ok&=check(s.frame(m,poll)==0&&polls==before&&m.read(c+16)==1,"stale current owner rejected");m.write(0xd8088,manager);
    m.write(c+4,manager+4);m.write(manager+4,2);m.write(0x2feb14,manager+4);m.write(0xd8088,manager+4);
    ok&=check(s.frame(m,poll)==0&&m.read(c+16)==1,"owner mutation rejected until disarm");
    m.write(c+12,0);s.frame(m,poll);m.write(c+12,1);ok&=check(s.frame(m,poll)==1,"disarm permits next match ownership");m.write(c+12,0);
    constexpr uint32_t menu=0x06c3f000,assignment=0x06933000;
    m.write(scene+0x18,40);m.write(0x2ff10c,scene);m.write(0x076ff000,0x324d4e42);
    m.write(menu,0x514d4931);m.write(menu+4,scene);m.write(menu+8,2);m.write(menu+12,1);
    m.write(0x06c30000,0x102d);m.write(0x06c30004,0x182d);
    before=polls;ok&=check(s.frame(m,poll)==1&&polls-before==2&&m.read(0x06c3f100)==0x20004&&m.read(0x06c3f120)==0x20008,"P3/P4 selector publication");
    m.write(scene+0x18,41);before=polls;ok&=check(s.frame(m,poll)==0&&polls==before,"wrong scene rejected");m.write(scene+0x18,40);
    m.write(assignment,0x43415331);m.write(assignment+12,1);m.write(0x06930000,0x3c080693);m.write(0x06930004,0x35083000);
    before=polls;ok&=check(s.frame(m,poll)==2&&polls-before==4&&m.read(0x06933100)==0x20001&&m.read(0x06933120)==0x20002,"assignment compatibility publishes P1/P2");
    m.write(0x076ff000,0);m.write(0xd8080,0);before=polls;
    ok&=check(s.frame(m,poll)==0&&polls==before,"unowned menu/assignment rejected");
    std::fprintf(stdout,"[native-seat-test] %s checks=%u\n",ok?"PASS":"FAIL",checks);return ok?0:1;
}
}
