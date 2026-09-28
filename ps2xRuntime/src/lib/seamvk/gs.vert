#version 450
// [seamvk] GS draws: vertices already in screen space (after XYOFFSET), depth normalised.
layout(location = 0) in vec4 inPos;   // x, y (GS pixels), z (0..1), q
layout(location = 1) in vec4 inTex;   // s, t, u, v
layout(location = 2) in vec4 inCol;   // rgba bytes / 255
layout(location = 3) in float inFog;
layout(location = 4) in uvec2 inRect;   // [spriterect] packed texel rect of the sprite (0 = none)
#include "pc.glsl"
layout(location = 0) out vec4 vColor;
layout(location = 1) out vec3 vTex;   // (s, t, q) or (u, v, 1)
layout(location = 2) out float vLit;
layout(location = 3) out float vFog;
layout(location = 4) flat out vec4 vRect;   // [spriterect] texel rect (umin, vmin, umax, vmax), x < 0 = none
void main()
{
    // Attributes are evaluated at pixel centres (x + 0.5): the GS convention. The post chain relies on it: its outline pass
    // draws the depth ramp shifted by half a pixel with NEAREST filtering and subtracts, which only differs from the
    // unshifted draw when the sample point is the centre (floor(y + 1) vs floor(y + 0.5)); its read-back sprites carry
    // +0.5 texel UVs with LINEAR filtering, which lands exactly on texel x. (paraLLEl-GS snaps 1:1 sprites to the integer
    // position, so its depth-mask pass is one row off from this; nothing visible depends on that row.)
    vec2 ndc = vec2(inPos.x / pc.view.z * 2.0 - 1.0, inPos.y / pc.view.w * 2.0 - 1.0);
    gl_Position = vec4(ndc, inPos.z, 1.0);
    vColor = inCol;
    vTex = ((pc.fA.x & 2) != 0) ? vec3(inTex.z, inTex.w, 1.0) : vec3(inTex.x, inTex.y, inPos.w);
    vLit = 1.0;
    vFog = inFog;
    {   // [spriterect]
        vec4 r = vec4(float(inRect.x & 0xFFFFu), float(inRect.x >> 16), float(inRect.y & 0xFFFFu), float(inRect.y >> 16));
        if ((pc.fA.x & 2) != 0) r /= 16.0; else r = r / 4096.0 * vec4(pc.texInfo.xy, pc.texInfo.xy);
        vRect = (inRect.x == 0u && inRect.y == 0u) ? vec4(-1.0) : r;
    }
}
