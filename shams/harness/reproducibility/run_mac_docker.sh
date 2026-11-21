#!/usr/bin/env bash
# run_mac_docker.sh - reproduce the laptop experiment on a Mac (Linux inside Docker).
#
#   usage:  bash reproducibility/run_mac_docker.sh <name> [runs]
#   e.g.:   bash reproducibility/run_mac_docker.sh macbook-air 3
#
# Needs Docker Desktop running. Records the Mac's details, then runs run_linux.sh inside
# a Linux container (image gcc:14). Results go to experiments/<date>-<name>/.
# Run it from the harness/ folder.
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
HARNESS=$(cd "$HERE/.." && pwd)
NAME=${1:?"give a name for this machine, e.g. macbook-air"}
RUNS=${2:-3}
REL="experiments/$(date +%Y%m%d-%H%M)-$NAME"
mkdir -p "$HARNESS/$REL"

{                                                  # the host: what Docker runs on
  echo "== date";   date
  echo "== macOS";  sw_vers
  echo "== chip";   sysctl -n machdep.cpu.brand_string
  echo "== cores";  sysctl -n hw.ncpu
  echo "== memory"; sysctl -n hw.memsize
  echo "== docker"; docker --version
  docker info --format 'Docker VM: {{.NCPU}} CPUs, {{.MemTotal}} bytes' 2>/dev/null
} > "$HARNESS/$REL/host.txt" 2>&1

docker run --rm --privileged -v "$HARNESS":/h -w /h -e OUT="/h/$REL" gcc:14 \
  bash reproducibility/run_linux.sh "$NAME" "$RUNS"
