#!/usr/bin/env bash
# gpu_check.sh - check the GPU enumerator against cvc_enum, then benchmark it
# and the old random sampler (cavacamisa_new/cavacamisa_cuda.cu, if present).
#
#   ./gpu_check.sh                  build, validate, benchmark (on a GPU server)
#   ./gpu_check.sh validate [EXE]   only the comparison; EXE defaults to ./cvc_gpu
#                                   (./cvc_gpu_cpu checks the same code without a GPU)
#   ./gpu_check.sh bench            only the benchmarks
#
# Environment: ARCH (nvcc -arch, default native), OLD_CUDA (path of the old sampler).
# The output is also appended to gpu_check_report.txt.
set -euo pipefail
cd "$(dirname "$0")"
WORK=gpu_check_runs
ARCH=${ARCH:-native}
OLD_CUDA=${OLD_CUDA:-cavacamisa_new/cavacamisa_cuda.cu}
NP=$(getconf _NPROCESSORS_ONLN)
mkdir -p $WORK
exec > >(tee -a gpu_check_report.txt) 2>&1

field() { awk -v k="$2" '$1==k {print $2}' "$1/summary.txt"; }

validate() {
  local exe=${1:-./cvc_gpu} fail=0
  echo "== validate $exe against cvc_enum (byte-identical output files)"
  make -s cvc_enum
  [ "$exe" = ./cvc_gpu ] && make -s cvc_gpu ARCH="$ARCH"
  [ "$exe" = ./cvc_gpu_cpu ] && make -s cvc_gpu_cpu
  # start of the space, middle, the mask of the 2017 infinite deal, end of the space
  for slice in "0 300" "2000000000 300" "2765932646 1" "5586853180 300"; do
    read -r start count <<< "$slice"
    for mode in "--rule=empty" "--rule=must" "--rule=empty --stop-lower"; do
      name=$(echo "${start}_${count}${mode}" | tr -c 'a-z0-9_\n' '_')
      rm -rf "$WORK/cpu_$name" "$WORK/gpu_$name"
      # shellcheck disable=SC2086
      ./cvc_enum --out="$WORK/cpu_$name" --start="$start" --count="$count" --threads="$NP" $mode \
        2> "$WORK/cpu_$name.log" || { cat "$WORK/cpu_$name.log"; return 1; }
      # small batches, so slices span several launches
      # shellcheck disable=SC2086
      $exe --out="$WORK/gpu_$name" --start="$start" --count="$count" --batch=128 --threads="$NP" $mode \
        2> "$WORK/gpu_$name.log" || { echo "$exe failed:"; cat "$WORK/gpu_$name.log"; return 1; }
      if diff -rq "$WORK/cpu_$name" "$WORK/gpu_$name" > /dev/null; then r=OK; else r=MISMATCH; fail=1; fi
      printf "  masks %-10s +%-4s %-26s %-8s games %-10s A %-10s loops %s\n" "$start" "$count" "$mode" "$r" \
        "$(field "$WORK/cpu_$name" games)" "$(field "$WORK/cpu_$name" A)" "$(field "$WORK/cpu_$name" loop)"
    done
  done
  if [ $fail = 0 ]; then echo "validation: all OK"; else echo "validation: FAILED (diff -r the folders in $WORK)"; fi
  return $fail
}

bench() {
  echo "== benchmark"
  command -v nvidia-smi > /dev/null && nvidia-smi --query-gpu=name,driver_version,memory.total --format=csv,noheader
  nvcc --version | tail -1
  make -s cvc_gpu ARCH="$ARCH"
  # 4 slices spread over the space, 65536 masks (2.3e9 games) each
  for k in 0 1 2 3; do
    start=$((k * 1396713370 + 1000003))
    rm -rf "$WORK/bench_$k"
    ./cvc_gpu --out="$WORK/bench_$k" --start=$start --count=65536 2>&1 | grep -E "^(device|masks|done)" | tail -2
  done

  if [ -f "$OLD_CUDA" ]; then
    echo "-- old random sampler ($OLD_CUDA), same settings as the 2025 runs"
    nvcc -O3 -arch="$ARCH" -o "$WORK/cavacamisa_cuda" "$OLD_CUDA"
    t0=$(date +%s.%N)
    (cd "$WORK" && ./cavacamisa_cuda 640 256 32768 12345 --step-limit=10000 --record-long=10 --print-long=0 \
      --hist=1 --cap-hist=1 > old_sampler.txt)
    t1=$(date +%s.%N)
    games=$(awk -F: '/^Total games/ {gsub(/ /,"",$2); print $2}' "$WORK/old_sampler.txt")
    steps=$(awk -F: '/^Avg steps/ {gsub(/ /,"",$2); print $2}' "$WORK/old_sampler.txt")
    awk -v g="$games" -v a="$t0" -v b="$t1" -v s="$steps" \
      'BEGIN {printf "old sampler: %.0f games in %.1f s = %.1f M games/s end-to-end, avg steps %s\n", g, b-a, g/(b-a)/1e6, s}'
  else
    echo "(old sampler not found at $OLD_CUDA, skipped)"
  fi
  echo "note: compare games/s together with avg steps (work per game); random deals average ~189 steps"
}

case "${1:-all}" in
  validate) validate "${2:-./cvc_gpu}" ;;
  bench) bench ;;
  all) validate ./cvc_gpu && bench ;;
  *) echo "usage: $0 [validate [EXE] | bench]"; exit 1 ;;
esac
