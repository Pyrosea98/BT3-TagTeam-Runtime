#pragma once
// Host-only seat publication. No guest pad ports and no transport-thread writes.
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace ps2_native_seats {
struct Packet { uint16_t buttons=0xffff; uint8_t lx=128,ly=128,rx=128,ry=128; };
struct Record { uint32_t word=0; float lx=0,ly=0,rx=0,ry=0; uint32_t padding[3]{}; };
static_assert(sizeof(Record)==32,"private seat ABI");
inline Record encode(Packet p) {
    Record out; out.word=uint32_t(~p.buttons)&0xffff;
    auto axis=[](uint8_t b){float v=(int(b)-128)/127.f;return v<-1?-1.f:v;};
    out.lx=axis(p.lx);out.ly=axis(p.ly);out.rx=axis(p.rx);out.ry=axis(p.ry);
    // PadConfig already applies each configured binding's deadzone. Do not
    // apply the old SDL 18% deadzone a second time to the configured PS2 pad.
    auto stick=[&](float &x,float &y,unsigned shift){
        float n=std::sqrt(x*x+y*y);if(n>1){x/=n;y/=n;}
        unsigned bits=std::fabs(y)>=std::fabs(x)?(y>.5f?4:y<-.5f?8:0):(x>.5f?2:x<-.5f?1:0);
        out.word|=bits<<shift;
    };
    // Direct Y matches actual quad_controller.encode_pad and MENU_BITS.
    stick(out.lx,out.ly,16);stick(out.rx,out.ry,20);return out;
}
struct Memory {
    uint8_t *bytes;size_t size;
    bool fits(uint32_t a,size_t n=4)const{return a<=size && n<=size-a;}
    uint32_t read(uint32_t a)const{uint32_t v=0;if(fits(a))std::memcpy(&v,bytes+a,4);return v;}
    void write(uint32_t a,uint32_t v)const{std::memcpy(bytes+a,&v,4);}
};
struct Service {
    struct Owner { bool seen=false,changed=false;uint32_t a=0,b=0;
        void reset(){*this=Owner{};}
        bool accept(uint32_t x,uint32_t y){if(!seen){seen=true;a=x;b=y;}else if(a!=x||b!=y)changed=true;return !changed;}
    };
    std::array<Owner,3> owners{};
    void reset(){owners={};}
    static bool pointer(uint32_t p){return p>=0x100000 && p<0x2000000 && !(p&3);}
    static bool match(Memory m,uint32_t manager,uint32_t count){
        return pointer(manager)&&m.fits(manager)&&count>=4&&count<=10&&!(count&1)&&
            m.read(manager)==2&&m.read(0x2feb14)==manager&&m.read(0xd8080)==1&&
            m.read(0xd8084)==count&&m.read(0xd8088)==manager&&m.read(0xd808c)==count;
    }
    static bool selector(Memory m,uint32_t scene){
        return pointer(scene)&&m.fits(scene+0x18)&&m.read(0x2ff10c)==scene&&
            m.read(scene+0x18)==40&&m.read(0x076ff000)==0x324d4e42;
    }
    template<class Poll> unsigned frame(Memory m,Poll poll){
        constexpr uint32_t controls[]={0x06c1f000,0x06c3f000,0x06933000};
        constexpr uint32_t boxes[]={0x06c15000,0x06c3f100,0x06933100};
        constexpr uint32_t magics[]={0x51494e31,0x514d4931,0x43415331};
        bool active[3]{};unsigned published=0;
        for(unsigned i=0;i<3;i++){
            uint32_t c=controls[i];if(!m.fits(c,0x80)||!m.fits(boxes[i],64))continue;
            if(m.read(c)!=magics[i]||!m.read(c+12)){owners[i].reset();continue;}
            uint32_t a=m.read(c+4),b=m.read(c+8);
            if(i==0){active[i]=match(m,a,b)&&m.read(0x06c10000)==0x27bdff20&&m.read(0x06c10004)==0xffa20000;}
            if(i==1){active[i]=b==2&&selector(m,a)&&m.read(0x06c30000)==0x0000102d&&m.read(0x06c30004)==0x0000182d;}
            if(i==2){ // Compatibility assignment uses P1/P2; normally disabled.
                uint32_t scene=m.read(0x2ff10c),manager=m.read(0x2feb14),count=m.read(0xd8084);
                active[i]=a==0&&b==0&&(selector(m,scene)||match(m,manager,count))&&
                    m.read(0x06930000)==0x3c080693&&m.read(0x06930004)==0x35083000;
                a=selector(m,scene)?scene:manager;b=selector(m,scene)?40:count;
            }
            if(active[i])active[i]=owners[i].accept(a,b);
        }
        std::array<Record,4> records{};
        if(active[0]||active[1]){records[2]=encode(poll(2));records[3]=encode(poll(3));}
        if(active[2]){records[0]=encode(poll(0));records[1]=encode(poll(1));}
        for(unsigned i=0;i<3;i++)if(active[i]){
            Record pair[2]={records[i==2?0:2],records[i==2?1:3]};
            if(i==0)for(unsigned seat=0;seat<2;seat++)if(m.read(controls[i]+0x48+seat*4)>=m.read(controls[i]+8))pair[seat]=Record{};
            uint32_t sequence=m.read(controls[i]+16)+1;if(!sequence)sequence=1;
            m.write(controls[i]+28,1);std::memcpy(m.bytes+boxes[i],pair,64);
            m.write(controls[i]+16,sequence);m.write(controls[i]+28,0);++published;
        }
        return published;
    }
};
}
