#!/bin/bash
# Compile the seamvk shaders to SPIR-V word arrays (src/lib/seamvk/*.inc). ALWAYS with -O: the unoptimised SPIR-V the
# headers were first generated with made the target decodes (rtdecode.frag) cost 15 ms of GPU per fight frame; -O: 1.6 ms.
set -e; cd "$(dirname "$0")/../src/lib/seamvk"
c() { glslc -O -mfmt=num "$@"; }
c -fshader-stage=vert seam.vert -o seam.vert.inc
c -fshader-stage=vert gs.vert -o gs.vert.inc
c -fshader-stage=vert present.vert -o present.vert.inc
c -fshader-stage=vert rtdecode.vert -o rtdecode.vert.inc
c -fshader-stage=frag gs.frag -o gs.frag.inc
c -fshader-stage=frag -DNOPERSP gs.frag -o gs_np.frag.inc
c -fshader-stage=frag -DSTENCIL_EXPORT gs.frag -o gs_st.frag.inc
c -fshader-stage=frag -DNOPERSP -DSTENCIL_EXPORT gs.frag -o gs_np_st.frag.inc
c -fshader-stage=frag present.frag -o present.frag.inc
c -fshader-stage=frag rtdecode.frag -o rtdecode.frag.inc
c -fshader-stage=frag -DFROM_VRAM rtdecode.frag -o rtdecode_vram.frag.inc
c -fshader-stage=frag alias16.frag -o alias16.frag.inc
c -fshader-stage=frag depthmask.frag -o depthmask.frag.inc
c -fshader-stage=frag ztop.frag -o ztop.frag.inc
c -fshader-stage=frag clutpass.frag -o clutpass.frag.inc
c -fshader-stage=frag glowdown.frag -o glowdown.frag.inc
c -fshader-stage=frag glowcomp.frag -o glowcomp.frag.inc
c -fshader-stage=frag outline.frag -o outline.frag.inc
c -fshader-stage=frag outline_h.frag -o outline_h.frag.inc
echo "shaders compiled (-O)"
