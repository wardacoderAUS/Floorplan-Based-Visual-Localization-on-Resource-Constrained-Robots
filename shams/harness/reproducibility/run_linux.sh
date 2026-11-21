#!/usr/bin/env bash
# run_linux.sh - reproduce the harness experiment on any Linux machine
# (Raspberry Pi 5, Jetson Orin Nano, a laptop, or Linux inside Docker).
#
#   usage:  bash reproducibility/run_linux.sh <name> [runs]
#   e.g.:   bash reproducibility/run_linux.sh raspberry-pi-5 3
#
# It records the machine's conditions, builds the harness and the validation program,
# profiles the program <runs> times (default 3), and saves everything in
#   experiments/<date>-<name>/  (system.txt, output.txt)
# Run it from the harness/ folder. Set OUT=<folder> to choose the output folder.
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
CODE="$HERE/../pmu_harness"
NAME=${1:?"give a name for this machine, e.g. raspberry-pi-5"}
RUNS=${2:-3}
OUT=${OUT:-"$HERE/../experiments/$(date +%Y%m%d-%H%M)-$NAME"}
mkdir -p "$OUT"

# 1. the conditions of the run: everything needed to compare or repeat it
temps() { cat /sys/class/thermal/thermal_zone*/temp 2>/dev/null | tr '\n' ' '; echo; }
{
  echo "== date";               date
  echo "== kernel";             uname -a
  echo "== os";                 cat /etc/os-release 2>/dev/null
  cat /etc/nv_tegra_release 2>/dev/null                       # Jetson only
  echo "== cpu";                lscpu 2>/dev/null || cat /proc/cpuinfo
  echo "== cores";              nproc
  echo "== frequency governor"; cat /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor 2>/dev/null | sort | uniq -c
  echo "== perf_event_paranoid";cat /proc/sys/kernel/perf_event_paranoid 2>/dev/null
  echo "== counter devices";    ls /sys/bus/event_source/devices/ 2>/dev/null
  echo "== ARM events listed";  ls /sys/bus/event_source/devices/armv8*/events 2>/dev/null | tr '\n' ' '; echo
  echo "== compiler";           gcc --version | head -1
  echo "== temperatures before (milli-degC)"; temps
} > "$OUT/system.txt" 2>&1

# 2. build (same flags on every machine)
gcc -O2 "$CODE/pmu.c" "$CODE/pmurun.c" -o "$OUT/pmurun" -pthread || exit 1
gcc -O2 "$CODE/workload.c" -o "$OUT/workload" -pthread || exit 1

# 3. profile the validation program, RUNS times
for i in $(seq 1 "$RUNS"); do
  echo "########## run $i of $RUNS ##########"
  "$OUT/pmurun" "$OUT/workload"
done 2>&1 | tee "$OUT/output.txt"

echo "== temperatures after (milli-degC)" >> "$OUT/system.txt"; temps >> "$OUT/system.txt"
rm -f "$OUT/pmurun" "$OUT/workload"
echo "saved in $OUT"
