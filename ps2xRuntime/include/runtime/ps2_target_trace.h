#pragma once
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace ps2x::tagteam {
// Read-only, bounded observer. Frame = installed lock-on update counter, not
// renderer frame or getter-call count. Only event changes enter the ring.
class TargetTrace {
    struct Human {
        bool known=false,blocked=false,reported=false,edge=false;
        uint32_t actor=0,target=0,blockedAt=0,edgeFrame=0,edgeTarget=0,edgeSwitches=0;
    };
    std::array<Human,12> humans{};
    std::array<std::array<char,640>,256> ring{};
    uint32_t cursor=0,used=0,manager=0,lastFrame=0;
    bool active=false,haveSample=false;
    uint32_t sampledFrame=0;
    struct Reader { uint32_t site=0,physical=0,frame=0; };
    std::array<Reader,128> readers{};
    std::array<Reader,128> resolvedReaders{};
    void (*sink)(const char *);
    const uint8_t *ram=nullptr;
    size_t size=0;
    uint32_t read(uint32_t at) const {
        uint32_t value=0;
        if(uint64_t(at)+4<=size)std::memcpy(&value,ram+at,4);
        return value;
    }
    bool actorValid(uint32_t at) const { return at>=0x100000 && uint64_t(at)+0x1280<=size; }
    uint32_t actor(uint32_t physical) const { return physical<12?read(0xD8040+4*physical):0; }
    uint32_t row(uint32_t at) const {
        const uint32_t slot=read(at+0x994);
        return actorValid(at)&&slot<5?at+0x9A4+slot*0xA4:0;
    }
    bool paired(uint32_t action) const {
        return (action>=301&&action<304)||(action>=313&&action<316)||(action>=183&&action<188);
    }
    const char *cameraGate(uint32_t a) const {
        const uint32_t c=read(0x2FEBCC);
        if(!c)return "none";
        if(c<0x100000||uint64_t(c)+816>size)return "invalid-owner";
        if(!read(c+812)&&(!read(c+704)||(read(c+776)&3)!=1))return "inactive";
        const uint32_t id=read(a+12),model=id<12?read(0x31C640+4*id):0;
        if(!model)return "missing-model";
        return read(c+768)==model||read(c+772)==model?"human-model-bound":"other-model";
    }
    void event(const char *kind,uint32_t frame,uint32_t physical,uint32_t oldTarget,uint32_t target,const char *detail) {
        const uint32_t a=actor(physical),t=actor(target),r=row(t);
        auto &line=ring[cursor];
        std::snprintf(line.data(),line.size(),
            "[targettrace] event=%s frame=%u manager=0x%x physical=%u target=%u->%u target_actor=0x%x hp=%u present=%u target_action=%u human_action=%u pending=%u/%u/%u pair_alias=%u %s",
            kind,frame,manager,physical,oldTarget,target,t,r?read(r+64):0,r?read(r+8):0,
            actorValid(t)?read(t+0x948):0,read(a+0x948),read(a+3480),read(a+3500),read(a+3512),read(0xC4004),detail);
        cursor=(cursor+1)%ring.size();if(used<ring.size())++used;
        // Emit event-only records immediately so an external process termination
        // cannot discard all recent evidence. The last256 stay in bounded RAM.
        if(sink)sink(line.data());
    }
    bool attach(const uint8_t *memory,size_t length) {
        ram=memory;size=length;
        const uint32_t m=read(0xD8088),count=read(0xD8084);
        const bool valid=read(0xD8080)==1&&count>=4&&count<=10&&read(0xD808C)==count&&
            actorValid(m)&&read(0x2FEB14)==m&&read(m)==2&&read(0x073D6800)!=0&&read(0x073D6804)==m;
        if(!valid){active=false;haveSample=false;return false;}
        const uint32_t frame=read(0x073D680C);
        if(!active||manager!=m||frame<lastFrame){humans={};readers={};resolvedReaders={};haveSample=false;}
        active=true;manager=m;lastFrame=frame;return true;
    }
public:
    explicit TargetTrace(void (*output)(const char *)):sink(output){}
    uint32_t retainedEvents() const { return used; }
    void sample(const uint8_t *memory,size_t length) {
        if(!attach(memory,length))return;
        const uint32_t frame=read(0x073D680C),count=read(0xD8084);
        if(haveSample&&sampledFrame==frame)return;
        haveSample=true;sampledFrame=frame;
        for(uint32_t i=0;i<count;++i) {
            const uint32_t a=actor(i);
            if(!actorValid(a)||read(a+0x1278)!=0){humans[i]={};continue;} // zero = human
            auto &h=humans[i];const uint32_t target=read(0xD8000+4*i);
            if(h.actor!=a){h={};h.actor=a;}
            const uint32_t switches=read(0x073D6860+4*i);
            if(h.known&&target!=h.target)event("target-change",frame,i,h.target,target,"");
            if(h.edge&&frame!=h.edgeFrame) {
                char detail[160];std::snprintf(detail,sizeof(detail),
                    "press_frame=%u per_actor_switches=%u->%u outcome=%s",
                    h.edgeFrame,h.edgeSwitches,switches,switches!=h.edgeSwitches?"switch-counter-advanced":"no-switch-observed-yet");
                event("press-outcome",frame,i,h.edgeTarget,target,detail);h.edge=false;
            }
            h.known=true;h.target=target;
            if(frame%60==0) {
                char detail[250];
                std::snprintf(detail,sizeof(detail),"queue_wide_pending=%u held=%u timeout=%u slot=%u paired_ids=%u/%u",
                    read(0x073D6900+4*i),read(0x073D6930+4*i),read(0x073D6850),read(a+0x994),read(a+3732),read(a+3736));
                event("periodic",frame,i,target,target,detail);
            }
            const bool blocked=paired(read(a+0x948))||read(a+3480)||read(a+3500)||read(a+3512);
            if(blocked&&!h.blocked){h.blocked=true;h.reported=false;h.blockedAt=frame;}
            if(blocked&&!h.reported&&uint32_t(frame-h.blockedAt)>30) {
                event("blocker-over-30-updates",frame,i,target,target,"observed paired/pending gate; not proof of a queued request");h.reported=true;
            }
            if(!blocked&&h.blocked){
                if(h.reported)event("blocker-cleared",frame,i,target,target,"");
                h.blocked=false;h.reported=false;
            }
        }
    }
    // Called just before the real LW t4,328(t1) in either switch body. The
    // guest has already resolved the actual pad (including co-op/quad routing).
    void input(const uint8_t *memory,size_t length,uint32_t physical,uint32_t a,uint32_t pad,uint32_t previousAt,uint32_t pc=0) {
        if(!attach(memory,length)||physical>=read(0xD8084)||a!=actor(physical)||
           !actorValid(a)||read(a+0x1278)!=0||uint64_t(pad)+332>size||uint64_t(previousAt)+4>size)return;
        const uint32_t raw=read(pad+328),previous=read(previousAt),button=read(0x073D6808);
        const bool l3=(raw&2)&&!(previous&2),bound=(raw&button)&&!(previous&button);
        if(!l3&&!bound)return;
        auto &h=humans[physical];h.actor=a;
        const uint32_t frame=read(0x073D680C),target=read(0xD8000+4*physical);
        char detail[360];
        std::snprintf(detail,sizeof(detail),"source_pc=0x%x raw=0x%x previous=0x%x bound_button=0x%x l3_edge=%u bound_edge=%u observed_gate=%s camera_gate=%s hold_updates=%u queued=%u expired=%u cancelled=%u",
            pc,
            raw,previous,button,unsigned(l3),unsigned(bound),
            paired(read(a+0x948))?"paired":(read(a+3480)||read(a+3500)||read(a+3512))?"pending-damage":"no-paired/pending-blocker",
            cameraGate(a),read(0x073D6858),read(0x073D6814),read(0x073D6818),read(0x073D681C));
        event("press-edge",frame,physical,target,target,detail);
        char queue[200];std::snprintf(queue,sizeof(queue),
            "wide_pending=%u wide_held=%u legacy_pending=%u legacy_held=%u timeout=%u; layout not assumed",
            read(0x073D6900+4*physical),read(0x073D6930+4*physical),read(0x073D6880+4*physical),read(0x073D68A0+4*physical),read(0x073D6850));
        event("queue-at-press",frame,physical,target,target,queue);
        for(uint32_t candidate=0;candidate<read(0xD8084);++candidate) {
            const uint32_t pointer=actor(candidate),r=row(pointer);
            const char *reason=(candidate&1)==(physical&1)?"same-side":!pointer?"null-pointer":
                !r?"invalid-pointer-or-slot":int32_t(read(r+64))<=0?"hp<=0":"living-candidate";
            char candidateDetail[200];std::snprintf(candidateDetail,sizeof(candidateDetail),
                "candidate=%u slot=%u reason=%s present=%u; snapshot, not consumer branch proof",
                candidate,actorValid(pointer)?read(pointer+0x994):0xffffffffu,reason,r?read(r+8):0);
            event("candidate-snapshot",frame,physical,target,candidate,candidateDetail);
        }
        if(bound){h.edge=true;h.edgeFrame=frame;h.edgeTarget=target;h.edgeSwitches=read(0x073D6860+4*physical);}
    }
    void consumer(const uint8_t *memory,size_t length,uint32_t physical,uint32_t a,uint32_t address,uint32_t pc) {
        if(!attach(memory,length)||physical>=read(0xD8084)||a!=actor(physical)||!actorValid(a)||read(a+0x1278))return;
        if(address!=0x073D6900+4*physical&&address!=0x073D6880+4*physical)return;
        const uint32_t pending=read(address);
        if(!pending)return;
        // Once at the beginning of each request; countdown/unchanged requests
        // are covered by periodic snapshots. Read the actual emitted address.
        auto &h=humans[physical];
        if(pending==read(0x073D6850)||h.edge) {
            char detail[180];std::snprintf(detail,sizeof(detail),"source_pc=0x%x actual_pending_address=0x%x pending=%u timeout=%u",pc,address,pending,read(0x073D6850));
            event("queue-consumer",read(0x073D680C),physical,read(0xD8000+4*physical),read(0xD8000+4*physical),detail);
        }
    }
    void candidate(const uint8_t *memory,size_t length,uint32_t physical,uint32_t candidateId,uint32_t pointer,uint32_t pc,bool hpRead) {
        if(!attach(memory,length)||physical>=read(0xD8084)||candidateId>=read(0xD8084)||
           pointer!=actor(candidateId)||!actorValid(actor(physical))||read(actor(physical)+0x1278))return;
        const uint32_t r=row(pointer);char detail[180];
        std::snprintf(detail,sizeof(detail),"source_pc=0x%x candidate=%u slot=%u reason=%s",pc,candidateId,
            actorValid(pointer)?read(pointer+0x994):0xffffffffu,
            !pointer?"null-pointer":!r?"invalid-slot-or-pointer":int32_t(read(r+64))<=0?"hp<=0":"hp-positive");
        event(hpRead?"candidate-hp-read":"candidate-pointer-read",read(0x073D680C),physical,read(0xD8000+4*physical),candidateId,detail);
    }
    void nativePhysicalRead(const uint8_t *memory,size_t length,uint32_t site,uint32_t argument,uint32_t result) {
        if(!attach(memory,length)||argument>=2||read(0xC4004)||result!=actor(argument))return;
        const uint32_t frame=read(0x073D680C);
        // Gate repeated hot getter calls BEFORE scanning human state. Bounded
        // open-address lookup; overflow skips diagnostics, never gameplay.
        Reader *record=nullptr;
        const unsigned bucket=((site>>2)^(argument<<4))%readers.size();
        for(unsigned probe=0;probe<8;++probe) {
            auto &r=readers[(bucket+probe)%readers.size()];
            if(r.site==site&&r.physical==argument){
                if(uint32_t(frame-r.frame)<60)return;
                record=&r;break;
            }
            if(!r.site){record=&r;break;}
        }
        if(!record)return;
        *record={site,argument,frame};
        for(uint32_t i=0;i<read(0xD8084);++i) {
            const uint32_t a=actor(i),target=read(0xD8000+4*i);
            if(!actorValid(a)||read(a+0x1278)||target==argument||(i&1)==argument)continue;
            char detail[180];std::snprintf(detail,sizeof(detail),"caller_site=0x%x getter_argument=%u returned_actor=0x%x; reader candidate, NOT proof of aim use",site,argument,result);
            event("native-leader-getter-read",frame,i,target,argument,detail);
        }
    }
    void resolverRead(const uint8_t *memory,size_t length,uint32_t site,uint32_t source,uint32_t result,uint32_t pc) {
        if(!attach(memory,length))return;
        const uint32_t count=read(0xD8084),frame=read(0x073D680C);
        uint32_t physical=12,resolved=12;
        for(uint32_t i=0;i<count;++i){if(actor(i)==source)physical=i;if(actor(i)==result)resolved=i;}
        if(physical>=count||!actorValid(source)||read(source+0x1278))return;
        const unsigned bucket=((site>>2)^physical)%resolvedReaders.size();Reader *record=nullptr;
        for(unsigned probe=0;probe<8;++probe) {
            auto &r=resolvedReaders[(bucket+probe)%resolvedReaders.size()];
            if(r.site==site&&r.physical==resolved){if(uint32_t(frame-r.frame)<60)return;record=&r;break;}
            if(!r.site||r.site==site){record=&r;break;}
        }
        if(!record)return;*record={site,resolved,frame};
        char detail[200];std::snprintf(detail,sizeof(detail),"caller_site=0x%x return_pc=0x%x returned_actor=0x%x table_match=%u contact_context=%u",
            site,pc,result,unsigned(resolved==read(0xD8000+4*physical)),read(0x071AF020));
        event("resolved-target-read",frame,physical,read(0xD8000+4*physical),resolved,detail);
    }
};
}
