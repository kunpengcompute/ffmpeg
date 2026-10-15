#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 4 ]]; then
    printf 'Usage: %s --base-src /path/to/ffmpeg-base --opt-src /path/to/ffmpeg-opt\n' "$0" >&2
    exit 1
fi
if [[ "$1" != --base-src || "$3" != --opt-src ]]; then
    printf 'Usage: %s --base-src /path/to/ffmpeg-base --opt-src /path/to/ffmpeg-opt\n' "$0" >&2
    exit 1
fi
base_src=$(cd -- "$2" && pwd -P)
opt_src=$(cd -- "$4" && pwd -P)
runner=$(cd -- "$(dirname -- "$0")" && pwd -P)
base_install=$base_src/ffmpeg_install
opt_install=$opt_src/ffmpeg_install

# 1. 检查两份安装；缺少时由安装脚本编译安装。
bash "$runner/build_install.sh" --base-src "$base_src" --opt-src "$opt_src"

# 2. 每次在临时目录生成新随机素材；同一次测试的两版共用这些输入。
work=$(mktemp -d /tmp/ffmpeg-cli-bench-XXXXXXXX)
staged_result=
trap 'rm -rf -- "$work"; if [[ -n "$staged_result" ]]; then rm -f -- "$staged_result"; fi' EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
python3 "$runner/generate_inputs.py" "$work"
if [[ $(wc -c < "$work/input_1920x1080.yuv") -ne 31104000 ||
      $(wc -c < "$work/input_1920x1280.yuv") -ne 36864000 ||
      $(wc -c < "$work/input_3840x2160.yuv") -ne 124416000 ]]; then
    exit 1
fi

# 3. 固定当前 Shell 的 CPU，后续 ffmpeg 子进程继承同一 CPU。
cpu=$(awk '/^Cpus_allowed_list:/ { split($2, a, /[,-]/); print a[1]; exit }' /proc/self/status)
taskset -pc "$cpu" $$ >/dev/null
printf '\nCLI benchmark: 10 frames, BILINEAR, YUV420P -> RGB24, pinned CPU %s\n' "$cpu"
saved_path=$PATH
saved_ld_library_path=${LD_LIBRARY_PATH:-}
export LC_ALL=C
TIMEFORMAT='%3R'
time_file=$work/time.txt

# 4. 优化前：只在这一版的 PATH 和 LD_LIBRARY_PATH 下执行三条缩放命令。
export PATH="$base_install/bin:$saved_path"
export LD_LIBRARY_PATH="$base_install/lib${saved_ld_library_path:+:$saved_ld_library_path}"
printf '\n[base] FFmpeg: %s\n' "$(command -v ffmpeg)"
loaded_swscale=$(ldd "$(command -v ffmpeg)" | awk '/libswscale[.]so/ { print $3 }')
if [[ "$loaded_swscale" != "$base_install/lib/"* ]]; then
    exit 1
fi
printf 'libswscale: %s\n' "$loaded_swscale"

# time 文件只接收总秒数；乘 1000 再除以 10，得到平均 ms/帧。
printf '\n[base] 1920x1080 -> 1280x704\n'
base_1=NA
if { time ffmpeg -hide_banner -loglevel error -nostdin -nostats -y \
    -threads 1 -filter_threads 1 -f rawvideo -pixel_format yuv420p -video_size 1920x1080 \
    -i "$work/input_1920x1080.yuv" \
    -vf 'scale=1280:704:flags=bilinear' \
    -frames:v 10 -threads 1 -c:v rawvideo -pix_fmt rgb24 -f rawvideo \
    "$work/base_1920x1080_to_1280x704.rgb" 2>/dev/null; } 2>"$time_file"; then
    base_1=$(awk '{ printf "%.1f", $1 * 1000 / 10 }' "$time_file")
fi
printf 'Average: %s ms/frame\n' "$base_1"

printf '\n[base] 1920x1280 -> 768x512\n'
base_2=NA
if { time ffmpeg -hide_banner -loglevel error -nostdin -nostats -y \
    -threads 1 -filter_threads 1 -f rawvideo -pixel_format yuv420p -video_size 1920x1280 \
    -i "$work/input_1920x1280.yuv" \
    -vf 'scale=768:512:flags=bilinear' \
    -frames:v 10 -threads 1 -c:v rawvideo -pix_fmt rgb24 -f rawvideo \
    "$work/base_1920x1280_to_768x512.rgb" 2>/dev/null; } 2>"$time_file"; then
    base_2=$(awk '{ printf "%.1f", $1 * 1000 / 10 }' "$time_file")
fi
printf 'Average: %s ms/frame\n' "$base_2"

printf '\n[base] 3840x2160 -> 1280x704\n'
base_3=NA
if { time ffmpeg -hide_banner -loglevel error -nostdin -nostats -y \
    -threads 1 -filter_threads 1 -f rawvideo -pixel_format yuv420p -video_size 3840x2160 \
    -i "$work/input_3840x2160.yuv" \
    -vf 'scale=1280:704:flags=bilinear' \
    -frames:v 10 -threads 1 -c:v rawvideo -pix_fmt rgb24 -f rawvideo \
    "$work/base_3840x2160_to_1280x704.rgb" 2>/dev/null; } 2>"$time_file"; then
    base_3=$(awk '{ printf "%.1f", $1 * 1000 / 10 }' "$time_file")
fi
printf 'Average: %s ms/frame\n' "$base_3"

# 5. 优化后：从原环境重新设置路径，避免带入上一版的安装目录。
export PATH="$opt_install/bin:$saved_path"
export LD_LIBRARY_PATH="$opt_install/lib${saved_ld_library_path:+:$saved_ld_library_path}"
printf '\n[opt] FFmpeg: %s\n' "$(command -v ffmpeg)"
loaded_swscale=$(ldd "$(command -v ffmpeg)" | awk '/libswscale[.]so/ { print $3 }')
if [[ "$loaded_swscale" != "$opt_install/lib/"* ]]; then
    exit 1
fi
printf 'libswscale: %s\n' "$loaded_swscale"

printf '\n[opt] 1920x1080 -> 1280x704\n'
opt_1=NA
if { time ffmpeg -hide_banner -loglevel error -nostdin -nostats -y \
    -threads 1 -filter_threads 1 -f rawvideo -pixel_format yuv420p -video_size 1920x1080 \
    -i "$work/input_1920x1080.yuv" \
    -vf 'scale=1280:704:flags=bilinear' \
    -frames:v 10 -threads 1 -c:v rawvideo -pix_fmt rgb24 -f rawvideo \
    "$work/opt_1920x1080_to_1280x704.rgb" 2>/dev/null; } 2>"$time_file"; then
    opt_1=$(awk '{ printf "%.1f", $1 * 1000 / 10 }' "$time_file")
fi
printf 'Average: %s ms/frame\n' "$opt_1"

printf '\n[opt] 1920x1280 -> 768x512\n'
opt_2=NA
if { time ffmpeg -hide_banner -loglevel error -nostdin -nostats -y \
    -threads 1 -filter_threads 1 -f rawvideo -pixel_format yuv420p -video_size 1920x1280 \
    -i "$work/input_1920x1280.yuv" \
    -vf 'scale=768:512:flags=bilinear' \
    -frames:v 10 -threads 1 -c:v rawvideo -pix_fmt rgb24 -f rawvideo \
    "$work/opt_1920x1280_to_768x512.rgb" 2>/dev/null; } 2>"$time_file"; then
    opt_2=$(awk '{ printf "%.1f", $1 * 1000 / 10 }' "$time_file")
fi
printf 'Average: %s ms/frame\n' "$opt_2"

printf '\n[opt] 3840x2160 -> 1280x704\n'
opt_3=NA
if { time ffmpeg -hide_banner -loglevel error -nostdin -nostats -y \
    -threads 1 -filter_threads 1 -f rawvideo -pixel_format yuv420p -video_size 3840x2160 \
    -i "$work/input_3840x2160.yuv" \
    -vf 'scale=1280:704:flags=bilinear' \
    -frames:v 10 -threads 1 -c:v rawvideo -pix_fmt rgb24 -f rawvideo \
    "$work/opt_3840x2160_to_1280x704.rgb" 2>/dev/null; } 2>"$time_file"; then
    opt_3=$(awk '{ printf "%.1f", $1 * 1000 / 10 }' "$time_file")
fi
printf 'Average: %s ms/frame\n' "$opt_3"

# 6. 计时结束后，检查完整的十帧输出并打印比较表。
status_1=NA
if [[ -f "$work/base_1920x1080_to_1280x704.rgb" && -f "$work/opt_1920x1080_to_1280x704.rgb" ]] &&
   [[ $(wc -c < "$work/base_1920x1080_to_1280x704.rgb") -eq 27033600 &&
      $(wc -c < "$work/opt_1920x1080_to_1280x704.rgb") -eq 27033600 ]] &&
   awk -v b="$base_1" -v o="$opt_1" 'BEGIN { exit !(b + 0 > 0 && o + 0 > 0) }' &&
   cmp -s -- "$work/base_1920x1080_to_1280x704.rgb" "$work/opt_1920x1080_to_1280x704.rgb"; then
    status_1=OK
fi

status_2=NA
if [[ -f "$work/base_1920x1280_to_768x512.rgb" && -f "$work/opt_1920x1280_to_768x512.rgb" ]] &&
   [[ $(wc -c < "$work/base_1920x1280_to_768x512.rgb") -eq 11796480 &&
      $(wc -c < "$work/opt_1920x1280_to_768x512.rgb") -eq 11796480 ]] &&
   awk -v b="$base_2" -v o="$opt_2" 'BEGIN { exit !(b + 0 > 0 && o + 0 > 0) }' &&
   cmp -s -- "$work/base_1920x1280_to_768x512.rgb" "$work/opt_1920x1280_to_768x512.rgb"; then
    status_2=OK
fi

status_3=NA
if [[ -f "$work/base_3840x2160_to_1280x704.rgb" && -f "$work/opt_3840x2160_to_1280x704.rgb" ]] &&
   [[ $(wc -c < "$work/base_3840x2160_to_1280x704.rgb") -eq 27033600 &&
      $(wc -c < "$work/opt_3840x2160_to_1280x704.rgb") -eq 27033600 ]] &&
   awk -v b="$base_3" -v o="$opt_3" 'BEGIN { exit !(b + 0 > 0 && o + 0 > 0) }' &&
   cmp -s -- "$work/base_3840x2160_to_1280x704.rgb" "$work/opt_3840x2160_to_1280x704.rgb"; then
    status_3=OK
fi

staged_result=$(mktemp "$runner/.result-cli-bench-XXXXXXXX")
awk -v b1="$base_1" -v o1="$opt_1" -v b2="$base_2" -v o2="$opt_2" -v b3="$base_3" -v o3="$opt_3" \
    -v s1="$status_1" -v s2="$status_2" -v s3="$status_3" '
function emit(scenario, base, opt, status) {
    if (status == "OK") {
        printf "%s,10,%.1f,%.1f,%.6f,OK\n", scenario, base, opt, base / opt
    } else {
        printf "%s,10,,,,NA\n", scenario
    }
}
BEGIN {
    print "scenario,frames,base_ms_per_frame,opt_ms_per_frame,speedup,status"
    emit("1920x1080_to_1280x704", b1, o1, s1)
    emit("1920x1280_to_768x512", b2, o2, s2)
    emit("3840x2160_to_1280x704", b3, o3, s3)
}' > "$staged_result"
mv -- "$staged_result" "$runner/result_cli_bench.csv"
staged_result=

awk -F, '
NR == 1 {
    printf "\n%-26s %6s %16s %16s %10s %6s\n", \
           "scenario", "frames", "base(ms/frame)", "opt(ms/frame)", "speedup", "status"
    next
}
{
    base = $6 == "OK" ? sprintf("%.1f", $3) : "-"
    opt = $6 == "OK" ? sprintf("%.1f", $4) : "-"
    speedup = $6 == "OK" ? sprintf("%.3fx", $5) : "-"
    printf "%-26s %6d %16s %16s %10s %6s\n", $1, $2, base, opt, speedup, $6
}' "$runner/result_cli_bench.csv"
printf 'Average = complete command seconds * 1000 / 10; includes startup, input, scaling and output.\n'
printf 'Result: %s/result_cli_bench.csv\n' "$runner"
if [[ "$status_1" != OK || "$status_2" != OK || "$status_3" != OK ]]; then
    exit 1
fi
