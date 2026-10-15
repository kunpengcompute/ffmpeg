#!/usr/bin/env bash
set -eo pipefail

usage()
{
    printf 'Usage: %s --base-src /path/to/ffmpeg-base --opt-src /path/to/ffmpeg-opt\n' "$0"
}

die()
{
    printf 'ERROR: %s\n' "$*" >&2
    exit 1
}

base_src=
opt_src=
while [[ $# -gt 0 ]]; do
    case "$1" in
        --base-src)
            [[ $# -ge 2 && -n "$2" && "$2" != --* ]] || die "--base-src needs a directory"
            [[ -z "$base_src" ]] || die "duplicate --base-src"
            base_src=$2
            shift 2
            ;;
        --opt-src)
            [[ $# -ge 2 && -n "$2" && "$2" != --* ]] || die "--opt-src needs a directory"
            [[ -z "$opt_src" ]] || die "duplicate --opt-src"
            opt_src=$2
            shift 2
            ;;
        --help|-h)
            usage
            exit 0
            ;;
        *)
            usage >&2
            die "unknown argument: $1"
            ;;
    esac
done
[[ -n "$base_src" && -n "$opt_src" ]] || { usage >&2; die "both source directories are required"; }
base_src=$(cd -- "$base_src" && pwd -P) || die "cannot open baseline source directory"
opt_src=$(cd -- "$opt_src" && pwd -P) || die "cannot open optimized source directory"
[[ "$base_src" != "$opt_src" ]] || die "baseline and optimized sources must be different directories"
runner=$(cd -- "$(dirname -- "$0")" && pwd -P)
for directory in "$runner" "$base_src" "$opt_src"; do
    [[ ! "$directory" =~ [[:space:]] ]] || die "FFmpeg make rules require paths without whitespace: $directory"
done
for source in "$base_src" "$opt_src"; do
    [[ -f "$source/configure" && -f "$source/Makefile" &&
       -f "$source/libswscale/swscale_internal.h" ]] || die "not an FFmpeg source directory: $source"
done
[[ "$(uname -s)" == Linux ]] || die "run this script on the target AArch64 Linux machine"
case "$(uname -m)" in
    aarch64|arm64) ;;
    *) die "AArch64 is required; a host or emulated benchmark is not a target performance result" ;;
esac
for tool in make awk sort taskset getconf mktemp tar tail mv rm cat; do
    command -v "$tool" >/dev/null || die "missing required tool: $tool"
done
compiler=$(printenv CC || true)
[[ -n "$compiler" ]] || compiler=cc
command -v "$compiler" >/dev/null || die "C compiler not found: $compiler"
jobs=$(getconf _NPROCESSORS_ONLN)
[[ "$jobs" =~ ^[0-9]+$ && "$jobs" -gt 0 ]] || jobs=1
[[ "$jobs" -le 8 ]] || jobs=8
cpu=$(awk '/^Cpus_allowed_list:/ { split($2, a, /[,-]/); print a[1]; exit }' /proc/self/status)
[[ "$cpu" =~ ^[0-9]+$ ]] || die "cannot identify an allowed CPU"

work=$(mktemp -d /tmp/ffmpeg-8.1.2-scale-bench-XXXXXXXX)
staged_result=
cleanup()
{
    local status=$?
    trap - EXIT
    if [[ -n "$staged_result" ]]; then
        rm -f -- "$staged_result"
    fi
    rm -rf -- "$work"
    exit "$status"
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
printf '================================================================================\n'
printf 'FFmpeg 8.1.2 scale optimization benchmark\n'
printf '================================================================================\n'
printf 'Baseline source : %s\n' "$base_src"
printf 'Optimized source: %s\n' "$opt_src"
printf 'Compiler        : %s\n' "$compiler"
printf 'Pinned CPU      : %s (logical CPU, both versions)\n' "$cpu"
printf 'Scaling         : BILINEAR, YUV420P -> RGB24, BT.709 limited -> full, single thread\n'
printf 'Measurement     : 3 paired rounds; 9 batches/item/round, calibration >=20 ms\n'
printf 'Units           : ns/call (function_replay); ns/frame (sws_scale)\n'

cat > "$work/bench.mk" <<'MAKEFILE'
# Read after the selected FFmpeg Makefile, so both versions use their own
# configured compiler, include paths, optimization flags, and static dependencies.
.PHONY: ffmpeg-8.1.2-scale-bench
ffmpeg-8.1.2-scale-bench: $(SWS_BENCH_OUT)

$(SWS_BENCH_OUT): $(SWS_BENCH_SRC) libswscale/libswscale.a libavutil/libavutil.a
	$(CC) $(CPPFLAGS) $(CFLAGS) -DHAVE_AV_CONFIG_H -DBUILDING_swscale \
	    -DBENCH_OPTIMIZED=$(SWS_BENCH_OPTIMIZED) $(SWS_BENCH_EXTRA_CFLAGS) \
	    -c $(SWS_BENCH_SRC) -o $(SWS_BENCH_OUT).o
	$(LD) $(LDFLAGS) $(LDEXEFLAGS) $(LD_O) $(SWS_BENCH_OUT).o \
	    libswscale/libswscale.a libavutil/libavutil.a \
	    $(EXTRALIBS-swscale) $(EXTRALIBS-avutil) $(EXTRALIBS) $(CCLIBS)
MAKEFILE

cat > "$work/compare.awk" <<'AWK'
BEGIN {
    FS = OFS = ","
}

function reject(message) {
    print "ERROR: " message > "/dev/stderr"
    failed = 1
    exit 1
}

function median3(a, b, c, temporary) {
    if (a > b) {
        temporary = a; a = b; b = temporary
    }
    if (b > c) {
        temporary = b; b = c; c = temporary
    }
    if (a > b) {
        b = a
    }
    return b
}

FNR == 1 {
    if ($1 != "version" || NF != 21 || $21 != "checksum") {
        reject("invalid benchmark CSV header: " FILENAME)
    }
    next
}

{
    if (NF != 21 || ($1 != "base" && $1 != "opt") || $7 != "OK" ||
        ($6 != "function_replay" && $6 != "sws_scale")) {
        reject("invalid benchmark CSV row: " FILENAME ":" FNR)
    }
    key = $2 OFS $3 OFS $5 OFS $6
    slot = key SUBSEP $1
    if (!seen[key]++) {
        keys++
        calls[key] = $16
        width[key] = $12
    } else if (calls[key] != $16 || width[key] != $12) {
        reject("status, row width, or real call counts differ: " key)
    }
    if (actual[slot] && actual[slot] != $4) {
        reject("dispatch changed between rounds: " key)
    }
    actual[slot] = $4
    round = ++count[slot]
    if (round > 3) {
        reject("duplicate benchmark row: " key)
    }
    if ($17 + 0 <= 0) {
        reject("invalid timing: " key)
    }
    samples[slot SUBSEP round] = $17 + 0
    if (hash[slot] && hash[slot] != "h" $21) {
        reject("output checksum changed between rounds: " key)
    }
    hash[slot] = "h" $21
}

END {
    if (failed) {
        exit 1
    }
    if (keys != 15) {
        reject("expected 15 distinct measurements, found " keys)
    }
    for (key in seen) {
        base = key SUBSEP "base"
        opt = key SUBSEP "opt"
        if (count[base] != 3 || count[opt] != 3) {
            reject("missing paired rounds: " key)
        }
        if (hash[base] != hash[opt]) {
            reject("baseline and optimized replay checksums differ: " key)
        }
        b = median3(samples[base SUBSEP 1], samples[base SUBSEP 2], samples[base SUBSEP 3])
        o = median3(samples[opt SUBSEP 1], samples[opt SUBSEP 2], samples[opt SUBSEP 3])
        printf "%s,OK,%s,%s,%d,%.3f,%.3f,%.6f,%s,%s\n",
               key, actual[base], actual[opt], calls[key], b, o, b / o, substr(hash[base], 2), substr(hash[opt], 2)
    }
}
AWK

configure_flags=(
    --disable-programs --disable-doc --disable-debug --disable-network --disable-autodetect
    --disable-everything --disable-avcodec --disable-avformat --disable-avfilter
    --disable-avdevice --disable-swresample --enable-shared --enable-static
    --enable-avutil --enable-swscale
    "--extra-cflags=-O3 -g1 -falign-functions=64"
    "--extra-ldflags=-lm -lstdc++ -Wl,-O3"
)

build_version()
{
    local label=$1
    local source=$2
    local optimized=$3
    local build=$work/build-$label
    local snapshot=$work/source-$label
    local excludes=()
    case "$work" in
        "$source"/*) excludes=("--exclude=./${work#"$source"/}") ;;
    esac
    mkdir "$snapshot"
    # Copy source contents, including local code changes, into a private clean tree.
    # Existing generated files and binaries must not affect either build.
    tar -C "$source" "${excludes[@]}" --exclude='./.git' --exclude='./config.h' --exclude='./config.asm' \
        --exclude='./config_components.h' --exclude='./ffbuild/config.*' \
        --exclude='./libavutil/avconfig.h' --exclude='./libavutil/ffversion.h' \
        --exclude='*.o' --exclude='*.a' --exclude='*.so' --exclude='*.so.*' \
        --exclude='*.dll' --exclude='*.exe' --exclude='*.d' \
        -cf - . | tar -C "$snapshot" -xf -
    mkdir "$build"
    printf '  Building %-4s ... ' "$label"
    (
        cd "$build"
        bash "$snapshot/configure" "--cc=$compiler" "${configure_flags[@]}"
        make -j "$jobs" -f "$snapshot/Makefile" -f "$work/bench.mk" \
            SWS_BENCH_SRC="$runner/bench.c" SWS_BENCH_OUT="$work/bench_$label" \
            SWS_BENCH_OPTIMIZED="$optimized" ffmpeg-8.1.2-scale-bench
    ) > "$work/build-$label.log" 2>&1 || {
        printf 'FAIL\n' >&2
        tail -n 40 "$work/build-$label.log" >&2
        die "$label build failed"
    }
    printf 'PASS\n'
}

printf '\n[1/4] Build\n'
build_version base "$base_src" 0
build_version opt "$opt_src" 1

printf '\n[2/4] Dispatch and correctness checks\n'
taskset -c "$cpu" "$work/bench_base" --reference-write "$work/reference.bin" 2>&1
taskset -c "$cpu" "$work/bench_opt" --reference-read "$work/reference.bin" 2>&1

printf '\n[3/4] Paired performance measurements\n'
for round in 1 2 3; do
    if [[ "$round" == 2 ]]; then
        order=(opt base)
    else
        order=(base opt)
    fi
    for label in "${order[@]}"; do
        printf '  Round %s/3: %-4s ... ' "$round" "$label"
        taskset -c "$cpu" "$work/bench_$label" --csv "$work/$label-$round.csv" 2>&1
        printf 'PASS\n'
    done
done

awk -f "$work/compare.awk" \
    "$work/base-1.csv" "$work/opt-1.csv" \
    "$work/base-2.csv" "$work/opt-2.csv" \
    "$work/base-3.csv" "$work/opt-3.csv" > "$work/comparison-body.csv"
LC_ALL=C sort "$work/comparison-body.csv" > "$work/comparison-sorted.csv"

# Keep replay checksums in the terminal report, without adding columns to result_bench.csv.
awk -F, '
{
    if (scenario != $1) {
        scenario = $1
        printf "\n================================================================================\n"
        printf "Scenario: %s\n", scenario
        target = ""
    }
    if (target != $2) {
        target = $2
        printf "\n  Target: %s\n", target
        printf "  Actual: base=%s; opt=%s\n", $6, $7
        printf "  %-5s %-15s %11s %12s %12s %9s %-5s %16s %16s\n",
               "plane", "mode", "calls/frame", "base(ns)", "opt(ns)", "speedup", "test", "base checksum", "opt checksum"
    }
    printf "  %-5s %-15s %11d %12.3f %12.3f %8.3fx %-5s %16s %16s\n",
           $3, $4, $8, $9, $10, $11, "PASS", $12, $13
}' "$work/comparison-sorted.csv" > "$work/report.txt"

staged_result=$(mktemp "$runner/.result-bench-XXXXXXXX")
{
    printf 'scenario,target,plane,workload,status,base_actual,opt_actual,calls_per_frame,'
    printf 'base_median_ns,opt_median_ns,speedup\n'
    awk -F, 'BEGIN { OFS = "," } { print $1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11 }' \
        "$work/comparison-sorted.csv"
} > "$staged_result"
mv -- "$staged_result" "$runner/result_bench.csv"
staged_result=

printf '\n[4/4] Performance comparison and output checksums\n'
cat "$work/report.txt"
printf '\n================================================================================\n'
printf 'PASS: dispatch, byte-exact comparisons, replay and paired checksums passed.\n'
printf 'Result: %s/result_bench.csv\n' "$runner"
printf '15 measurements; speedup = base time / opt time (>1 means faster).\n'
