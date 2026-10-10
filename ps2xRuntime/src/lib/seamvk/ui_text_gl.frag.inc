R"GLSL(#version 330
uniform vec4 options; uniform vec4 gauges; uniform vec4 detail;
uniform sampler2D texture0;
#define source texture0
uniform sampler2D palette;
in vec2 fragTexCoord;
#define texcoord fragTexCoord
out vec4 finalColor;
#define color finalColor
vec2 edge(vec2 p,vec2 a,vec2 b) {
    float t=clamp(dot(p-a,b-a)/dot(b-a,b-a),0.0,1.0);
    return vec2(length(p-mix(a,b,t)),t);
}
void frameGauge() {
    vec2 p=texcoord*2.0-1.0;
    float radius=length(p),distance=10.0,progress=0.0,health=1.0;
    float inside=0.0;
    if(options.x<2.5) {
        // Top and bottom three-edge paths both progress left to right.
        for(int side=0;side<2;++side) {
            float y=side==0?-0.8660254:0.8660254;
            vec2 v[4]=vec2[4](vec2(-1,0),vec2(-0.5,y),vec2(0.5,y),vec2(1,0));
            for(int i=0;i<3;++i) {
                vec2 hit=edge(p,v[i],v[i+1]);
                if(hit.x<distance){distance=hit.x;progress=(float(i)+hit.y)/3.0;health=side==0?1.0:0.0;}
            }
        }
        inside=max(abs(p.y)/0.8660254,abs(p.x)+abs(p.y)*0.5773503)<0.77?1.0:0.0;
    } else {
        float angle=mod(atan(p.y,p.x)-2.3561945+6.2831853,6.2831853);
        if(angle<=4.712389) {
            float outer=abs(radius-0.91),inner=abs(radius-0.73);
            health=outer<inner?1.0:0.0;distance=min(outer,inner);progress=angle/4.712389;
        }
        inside=radius<0.61?1.0:0.0;
    }
    vec3 accent=options.z>2.5?vec3(0.95,0.3,0.3):options.z>1.5?vec3(0.2,0.85,0.62):options.z>0.5?vec3(0.23,0.8,0.96):vec3(1.0,0.56,0.18);
    color=vec4(0);
    if(inside>0.5) {
        vec4 portrait=texture(source,clamp(p/(options.x<2.5?0.77:0.61)*0.5+0.5,0.0,1.0));
        color=vec4(mix(vec3(0.07,0.12,0.18),portrait.rgb,detail.x),min(detail.y,0.45)*portrait.a);
    }
    float thickness=detail.z;
    float coverage=1.0-smoothstep(thickness-0.015,thickness+0.015,distance);
    if(coverage>0.0) {
        float amount=health>0.5?gauges.x:gauges.y;
        bool filled=progress<amount;
        vec3 hp=gauges.x<0.3?vec3(0.96,0.16,0.12):vec3(0.2,0.91,0.38);
        vec3 gauge=health>0.5?hp:vec3(0.16,0.66,1.0);
        color=vec4(filled?gauge:vec3(0.57,0.67,0.74),coverage*(filled?0.95:0.45));
    }
    // Small relation accent at the lower tip/gap, not a second HUD shape.
    if(abs(p.x)<0.11 && p.y>0.87 && p.y<0.99)color=vec4(accent,0.7);
    color.a*=options.y;
}
void main() {
    if(options.x>1.5){frameGauge();return;}
    if(options.x>0.5) {
        int index=int(round(texture(source,texcoord).r*255.0));
        color=texelFetch(palette,ivec2(index,0),0);
    } else color=texture(source,texcoord);
    color.a*=options.y;
    color.rgb*=options.z;
}
)GLSL"
