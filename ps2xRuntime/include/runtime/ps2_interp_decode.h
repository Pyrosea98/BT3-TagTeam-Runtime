#pragma once
#include <array>
#include <cstdint>

struct Ps2DecodedInstruction {
    uint32_t op, rs, rt, rd, sa, funct, simm;
    uint16_t imm;
    Ps2DecodedInstruction(uint32_t word=0) :
        op(word>>26), rs((word>>21)&31), rt((word>>16)&31),
        rd((word>>11)&31), sa((word>>6)&31), funct(word&63),
        simm(uint32_t(int32_t(int16_t(word&65535)))), imm(uint16_t(word)) {}
};
// Fixed capacity, thread owned; no RAM pointers or register results are cached.
// Compare the freshly fetched effective word on EVERY execution. This catches
// PINE writes, guest stores, DMA, rematch replacement and boot substitution
// without depending on a partial list of memory write notification paths.
class Ps2InstructionDecodeCache {
    struct Entry { uint32_t pc=1, word=0; Ps2DecodedInstruction decoded; };
    std::array<Entry,4096> entries_{};
public:
    uint64_t hits=0, misses=0, replacements=0;
    Ps2DecodedInstruction get(uint32_t pc,uint32_t word) {
        auto& e=entries_[(pc>>2)&(entries_.size()-1)];
        if(e.pc==pc && e.word==word) { ++hits; return e.decoded; }
        ++misses; replacements+=e.pc!=1;
        e.pc=pc;e.word=word;e.decoded=Ps2DecodedInstruction(word);
        return e.decoded;
    }
};
// hasFunction() consults only the immutable generated function table. Cache
// presence, never the resolved override: dirty dispatch and lifecycle choices
// must still be checked on every call.
class Ps2GeneratedPresenceCache {
    struct Entry{uint32_t pc=1;bool present=false;};
    std::array<Entry,4096> entries_{};
public:
    uint64_t hits=0,misses=0;
    template<class Lookup> bool get(uint32_t pc,Lookup lookup){
        auto& e=entries_[(pc>>2)&4095];
        if(e.pc==pc){++hits;return e.present;}
        ++misses;e.pc=pc;e.present=lookup(pc);return e.present;
    }
};

// Straight-line blocks contain no control flow or native dispatch boundaries.
// Every cached word is re-read immediately before execution, including after
// guest stores. No register values or RAM data are cached.
inline bool ps2StraightInstruction(uint32_t word) {
    const auto d=Ps2DecodedInstruction(word);
    if(d.op==0)return d.funct!=8 && d.funct!=9 && d.funct!=12 && d.funct!=13;
    if(d.op==0x11)return d.rs!=8; // floating point, never BC1
    switch(d.op){
    case 8:case 9:case 10:case 11:case 12:case 13:case 14:case 15:
    case 0x18:case 0x19:case 0x1a:case 0x1b:case 0x1c:case 0x1e:case 0x1f:
    case 0x20:case 0x21:case 0x22:case 0x23:case 0x24:case 0x25:case 0x26:case 0x27:
    case 0x28:case 0x29:case 0x2a:case 0x2b:case 0x2c:case 0x2d:case 0x2e:case 0x2f:
    case 0x31:case 0x33:case 0x37:case 0x39:case 0x3f:return true;
    default:return false;
    }
}
class Ps2StraightBlockCache {
public:
    struct Instruction {uint32_t pc=0,word=0;Ps2DecodedInstruction decoded;};
    struct Block {uint32_t pc=1;uint8_t count=0;std::array<Instruction,32> code;};
private:
    std::array<Block,1024> blocks_{};
public:
    uint64_t hits=0,misses=0,invalidations=0;
    template<class Fetch,class Boundary> Block& get(uint32_t pc,Fetch fetch,Boundary boundary) {
        auto& b=blocks_[(pc>>2)&1023];
        if(b.pc==pc && b.count && b.code[0].word==fetch(pc)){++hits;return b;}
        ++misses;b.pc=pc;b.count=0;
        for(unsigned i=0;i<b.code.size();++i){
            uint32_t at=pc+4*i;if(i && boundary(at))break;
            auto word=fetch(at);if(!ps2StraightInstruction(word))break;
            b.code[b.count++]={at,word,Ps2DecodedInstruction(word)};
        }
        return b;
    }
    void invalidate(Block& b){b.pc=1;b.count=0;++invalidations;}
};
