// [seamvk] per-draw push constants shared by every stage (112 bytes)
layout(push_constant) uniform PC
{
    vec4 view;      // x, y: XYOFFSET in pixels (seam draws); z, w: target logical size in pixels
    vec4 texInfo;   // x, y: texture size in texels
    ivec4 fA;       // x: flags (1 tme, 2 fst, 8 bilinear, 16 ate, 32 fba, 128 tcc, 256 fog, 512 date, 1024 datm); y: tfx; z: wms | wmt<<2; w: atst | aref<<3 | afail<<11
    ivec4 fB;       // region clamp: minu, maxu, minv, maxv
    vec4 fogcol;
    ivec4 blend;    // [shaderblend] abe, aA | aB<<2 | aC<<4 | aD<<6, fix, colclamp (used when flag 65536 is set)
    ivec4 misc;     // [rtnative] x: the texture image's native factor over the GS texture size (1 = GS-res texture)
} pc;
