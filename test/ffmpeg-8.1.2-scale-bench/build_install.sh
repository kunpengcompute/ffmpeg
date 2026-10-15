#!/usr/bin/env bash
set -euo pipefail

# 固定两个路径参数，按下面的顺序运行即可。
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
if [[ "$base_src" == "$opt_src" || ! -f "$base_src/configure" || ! -f "$opt_src/configure" ]]; then
    printf 'ERROR: provide two different FFmpeg source directories.\n' >&2
    exit 1
fi
if [[ "$base_src" =~ [[:space:]] || "$opt_src" =~ [[:space:]] ]]; then
    printf 'ERROR: FFmpeg source paths must not contain whitespace.\n' >&2
    exit 1
fi
if [[ "$(uname -s)" != Linux ]]; then
    printf 'ERROR: run on the target AArch64 Linux machine.\n' >&2
    exit 1
fi
case "$(uname -m)" in
    aarch64|arm64) ;;
    *) printf 'ERROR: AArch64 is required.\n' >&2; exit 1 ;;
esac

base_install=$base_src/ffmpeg_install
opt_install=$opt_src/ffmpeg_install
jobs=$(getconf _NPROCESSORS_ONLN)
if [[ "$jobs" -gt 8 ]]; then
    jobs=8
fi

# 1. 优化前：已有可用的动态库安装就复用，否则重新配置、编译、安装。
printf '\n[base] Install prefix: %s\n' "$base_install"
if [[ -x "$base_install/bin/ffmpeg" && -e "$base_install/lib/libswscale.so" ]] &&
   LD_LIBRARY_PATH="$base_install/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
       "$base_install/bin/ffmpeg" -version >/dev/null 2>&1; then
    printf 'Using existing installation.\n'
else
    cd -- "$base_src"
    if [[ -f ffbuild/config.mak ]]; then
        make distclean
    fi
    ./configure \
        --prefix="$base_install" \
        --cc="${CC:-cc}" \
        --enable-shared \
        --disable-debug \
        --extra-cflags="-O3 -g1 -falign-functions=64" \
        --extra-ldflags="-lm -lstdc++ -Wl,-O3"
    make -j "$jobs"
    make install
fi

# 2. 优化后：执行相同的编译参数，安装到它自己的源码目录。
printf '\n[opt] Install prefix: %s\n' "$opt_install"
if [[ -x "$opt_install/bin/ffmpeg" && -e "$opt_install/lib/libswscale.so" ]] &&
   LD_LIBRARY_PATH="$opt_install/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
       "$opt_install/bin/ffmpeg" -version >/dev/null 2>&1; then
    printf 'Using existing installation.\n'
else
    cd -- "$opt_src"
    if [[ -f ffbuild/config.mak ]]; then
        make distclean
    fi
    ./configure \
        --prefix="$opt_install" \
        --cc="${CC:-cc}" \
        --enable-shared \
        --disable-debug \
        --extra-cflags="-O3 -g1 -falign-functions=64" \
        --extra-ldflags="-lm -lstdc++ -Wl,-O3"
    make -j "$jobs"
    make install
fi

LD_LIBRARY_PATH="$base_install/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
    "$base_install/bin/ffmpeg" -version >/dev/null
LD_LIBRARY_PATH="$opt_install/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
    "$opt_install/bin/ffmpeg" -version >/dev/null
printf '\nPASS: both FFmpeg installations are ready.\n'
