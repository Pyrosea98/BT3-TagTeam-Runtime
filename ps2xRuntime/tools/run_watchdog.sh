#!/bin/bash
# Unattended fight run with a hang watchdog: the runner is started UNDER gdb (attaching later is not permitted on this
# system); the log's [fps] lines are polled, and once the game has been at 0 fps for 5 samples after having run, the
# runner gets SIGUSR1, gdb stops on it and writes every thread's backtrace to <log>.hang.txt, then everything is killed.
#   PS2X_LOGFILE=<log> tools/run_watchdog.sh <max seconds> <runner> <elf>
max=$1; runner=$2; elf=$3; log=$PS2X_LOGFILE
gdb -q -batch -ex "handle SIGUSR1 stop print nopass" -ex "handle SIGSEGV SIGBUS stop print" -ex run -ex "set pagination off" -ex "thread apply all bt 14" --args "$runner" "$elf" > "$log.hang.txt" 2>&1 &
gpid=$!
t=0; ran=0
while kill -0 $gpid 2>/dev/null && [ $t -lt $max ]; do
  sleep 5; t=$((t+5))
  last=$(grep "\[fps\] GAME=[0-9.]* guest" "$log" 2>/dev/null | tail -5 | sed -E 's/.*GAME=([0-9.]+).*/\1/' | tr '\n' ' ')
  set -- $last
  if [ "$#" -ge 5 ]; then
    nz=0; z=0; for v in "$@"; do if [ "${v%.*}" -gt 20 ] 2>/dev/null; then nz=$((nz+1)); else z=$((z+1)); fi; done
    if [ $nz -ge 3 ]; then ran=1; fi
    if { [ $ran -eq 1 ] && [ $z -ge 5 ]; } || { [ $ran -eq 0 ] && [ $t -ge 90 ]; }; then
      echo "[watchdog] hang detected at t=$t s: signalling for a thread dump" >> "$log"
      cpid=$(pgrep -x ps2EntryRunner | head -1); [ -n "$cpid" ] && kill -USR1 $cpid
      sleep 20; break
    fi
  fi
done
pkill -9 -x ps2EntryRunner 2>/dev/null; sleep 1; kill -9 $gpid 2>/dev/null; wait $gpid 2>/dev/null; echo "[watchdog] done" >> "$log"; exit 0
