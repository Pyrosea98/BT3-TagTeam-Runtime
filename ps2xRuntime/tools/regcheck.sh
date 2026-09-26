#!/bin/bash
# regcheck.sh <name> [ENV=VAL ...]: run the Ultimate Training replay with the native renderer, dump game frame 2150,
# print per-band mean-abs deltas vs caps/golden (native) and, if the run has PS2X_SEAMVK_REF=1, vs the paraLLEl-GS
# target of the same frame; then fight fps / GPU. Use for EVERY performance change.
name=$1; shift
D="/home/z3/Desktop/Dragon Ball Budokai Tenkaichi 3 Recompiled"
O=/home/z3/Desktop/bt3r/caps/reg_$name; rm -rf $O; mkdir -p $O/tex; cd $O
env PS2X_EXEDIR="$D" PS2X_ASSETDIR="$D/assets" LD_LIBRARY_PATH="$D/assets/lib" PS2X_SEAMSKIP=1 PS2X_FORCE_SKIP=600 PS2X_INPLAY=/home/z3/Desktop/bt3r/caps/ultimate.inrec PS2X_SEAMVK=1 PS2X_SEAMVK_DUMPGAMEFRAME=2150 PS2X_SEAMVK_TEXDUMP=$O/tex PS2X_LOGFILE=$O/run.log "$@" /home/z3/Desktop/bt3r/BT3-Recomp/build/ps2xRuntime/ps2EntryRunner "$D/data/SLUS_216.78" > /dev/null 2>&1 &
pid=$!; sleep 2; clk=$(getconf CLK_TCK); prev=$(awk '{print $14+$15}' /proc/$pid/stat 2>/dev/null)
for ((i=0;i<80;i++)); do sleep 1; [ -d /proc/$pid ] || break; cur=$(awk '{print $14+$15}' /proc/$pid/stat 2>/dev/null); cores=$(awk -v a=$cur -v b=$prev -v c=$clk "BEGIN{printf \"%.2f\", (a-b)/c}"); prev=$cur; g=$(nvidia-smi --query-gpu=utilization.gpu --format=csv,noheader,nounits 2>/dev/null | tr -d ' '); st=$(grep -a -o "bt3state=0x[0-9a-f]*" $O/run.log 2>/dev/null | tail -1); echo "$i cores=$cores gpu=$g $st" >> $O/samples.txt; done
kill -INT $pid 2>/dev/null; sleep 2; kill -9 $pid 2>/dev/null
/home/z3/Desktop/bt3r/venv/bin/python - "$O" <<'PY'
import sys, numpy as np, os
O=sys.argv[1]
def ppm(p):
    d=open(p,'rb').read(); parts=d.split(b'\n',3); w,h=map(int,parts[1].split()); return np.frombuffer(parts[3][:w*h*3],dtype=np.uint8).reshape(h,w,3).astype(int)
cands=[O+'/tex/target_e00_1024x1024.ppm', O+'/tex/target_0_1024x1024.ppm']   # double-buffered: the frame lives in one of them
cands=[c for c in cands if os.path.exists(c)]
if not cands: print('REGCHECK: no dump at game frame 2150 (run did not reach it)'); sys.exit()
imgs=[ppm(c)[:896] for c in cands]; a=max(imgs, key=lambda x: x.mean())
gold='/home/z3/Desktop/bt3r/caps/golden/frame.ppm'
def bands(x,y):   # per band: signed mean-brightness delta (catches tone/overlay regressions) and low-res |d| (robust to particles / 1-px shifts)
    out=[]
    for b in range(4):
        r=slice(b*224,(b+1)*224); xs=x[r].reshape(14,16,64,16,3).mean(axis=(1,3)); ys=y[r].reshape(14,16,64,16,3).mean(axis=(1,3))
        out.append((x[r].mean()-y[r].mean(), np.abs(xs-ys).mean()))
    return out
if os.path.exists(gold):
    g=ppm(gold)[:896]; d=bands(a,g)
    flag='OK' if all(abs(m)<3 for m,_ in d) else 'REGRESSION?'   # lowres |d| is informational: particles and the sky differ run to run
    print('REGCHECK vs golden native (bands top..bottom): brightness delta %s | lowres |d| %s -> %s'%(' '.join('%+.1f'%m for m,_ in d), ' '.join('%.1f'%l for _,l in d), flag))
else:
    os.makedirs('/home/z3/Desktop/bt3r/caps/golden', exist_ok=True); open(gold,'wb').write(b'P6\n1024 896\n255\n'+a.astype(np.uint8).tobytes()); print('REGCHECK: golden created from this run')
ref=O+'/tex/pgs_target_0e00_512x448.ppm'
if os.path.exists(ref):
    r=ppm(ref); a2=a.reshape(448,2,512,2,3).mean(axis=(1,3)); d=[np.abs(a2[b*112:(b+1)*112]-r[b*112:(b+1)*112]).mean() for b in range(4)]
    print('REGCHECK vs paraLLEl-GS reference: bands mean|d| = %s overall %.2f'%(' '.join('%.2f'%v for v in d), np.abs(a2-r).mean()))
PY
L=$(grep -a -n -m1 "bt3state=0x2d" $O/run.log | cut -d: -f1)
E=$(grep -a -n -m1 "seamvk\] frame 2149" $O/run.log | cut -d: -f1); [ -z "$E" ] && E=$(wc -l < $O/run.log)
sed -n "${L:-1},${E}p" $O/run.log | grep -a "\[fps\]" | sed 's/.*GAME=\([0-9.]*\) guest_ms=\([0-9.]*\) wall_ms=\([0-9.]*\).*/\1 \2 \3/' | awk '{f[NR]=$1; w[NR]=$3} END{n=NR; asort(f); asort(w); printf "PERF fight: fps median %.1f (min %.1f) wall_ms %.1f [%d samples]  ", f[int(n/2)+1], f[1], w[int(n/2)+1], n}'
grep "bt3state=0x2d" $O/samples.txt | sed 's/.*cores=\([0-9.]*\) gpu=\([0-9]*\).*/\1 \2/' | awk '{c[NR]=$1; u[NR]=$2} END{n=NR; asort(c); asort(u); printf "cores %.2f gpu %s%%\n", c[int(n/2)+1], u[int(n/2)+1]}'
tail -n +${L:-1} $O/run.log | grep -a "gpu ms/frame\|\[seamvk\] per frame\|\[seamgs\] per frame" | tail -3 | cut -c1-170
