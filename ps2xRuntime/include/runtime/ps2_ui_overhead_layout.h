#pragma once
#include <algorithm>
#include <span>
#include <limits>
namespace ps2x::ui {
struct OverheadBox {float x=0,y=0,width=0,height=0;};
inline bool overheadOverlap(OverheadBox a,OverheadBox b){return a.width>0 && b.width>0 && a.height>0 && b.height>0 && a.x<b.x+b.width && a.x+a.width>b.x && a.y<b.y+b.height && a.y+a.height>b.y;}
inline OverheadBox overheadPlace(float x,float y,float width,float height,OverheadBox bounds){
    width=std::min(width,std::max(1.f,bounds.width-8));height=std::min(height,std::max(1.f,bounds.height-8));
    return {std::clamp(x-width/2,bounds.x+4,bounds.x+bounds.width-width-4),
            std::clamp(y-height-12,bounds.y+4,bounds.y+bounds.height-height-4),width,height};
}
inline OverheadBox overheadSeparate(OverheadBox target,OverheadBox owner,OverheadBox bounds){
    if(!overheadOverlap(target,owner))return target;
    if(owner.y-target.height-5>=bounds.y+4)target.y=owner.y-target.height-5;
    else if(owner.y+owner.height+5+target.height<=bounds.y+bounds.height-4)target.y=owner.y+owner.height+5;
    else if(owner.x+owner.width+5+target.width<=bounds.x+bounds.width-4)target.x=owner.x+owner.width+5;
    else if(owner.x-target.width-5>=bounds.x+4)target.x=owner.x-target.width-5;
    return target;
}
inline OverheadBox overheadAvoidDetails(OverheadBox bar,std::span<const OverheadBox> reserved,OverheadBox bounds){
    auto free=[&](OverheadBox c){if(c.x<bounds.x+4 || c.y<bounds.y+4 || c.x+c.width>bounds.x+bounds.width-4 || c.y+c.height>bounds.y+bounds.height-4)return false;
        for(auto other:reserved)if(overheadOverlap(c,other))return false;return true;};
    if(free(bar))return bar;
    OverheadBox best=bar;float distance=std::numeric_limits<float>::max();
    for(auto other:reserved)if(other.width>0){
        const OverheadBox options[]={ {bar.x,other.y-bar.height-4,bar.width,bar.height},
            {bar.x,other.y+other.height+4,bar.width,bar.height},
            {other.x-bar.width-4,bar.y,bar.width,bar.height}, {other.x+other.width+4,bar.y,bar.width,bar.height} };
        for(auto candidate:options)if(free(candidate)){const float dx=candidate.x-bar.x,dy=candidate.y-bar.y,d=dx*dx+dy*dy;if(d<distance){best=candidate;distance=d;}}
    }
    return best;
}

}
