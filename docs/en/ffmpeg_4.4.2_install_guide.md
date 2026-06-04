# FFmpeg-4.4.2_scale Installation Guide

## Description

This document describes how to compile and install the FFmpeg-4.4.2 tool and how to enable the optimization patch.

## Environment Configuration

**Hardware Environment**

|Item|Description|
| :--- | :--- |
| CPU | Kunpeng 950|

**Software Environment**

| Software| Version|
| :--- | :--- |
| OS | openEuler 22.03 LTS SP4 |
| Compiler| GCC ≥ 10.3.1|
| make | ≥ 4.3|
| cmake | ≥ 3.22.0|

Others:

The SVE length must be set to 128. You can set it in openEuler 22.03 LTS SP4 as follows:

```shell
echo 16 > /proc/sys/abi/sve_default_vector_length
```

## Compilation and Installation

1. Clone this repository to obtain the `ffmpeg_4.4.2-optimize-scale.patch` optimization patch.

    ```bash
    git clone https://gitcode.com/boostkit/ffmpeg.git
    ```

2. Obtain the FFmpeg-4.4.2 source package, decompress it, and go to the source code directory.

    ```bash
    wget https://ffmpeg.org/releases/ffmpeg-4.4.2.tar.gz
    tar -zxvf ffmpeg-4.4.2.tar.gz
    cd ffmpeg-4.4.2
    ```

3. Integrate the optimization patch in the corresponding path into FFmpeg-4.4.2.

    ```bash
    patch -p1 < /path/to/ffmpeg_4.4.2-optimize-scale.patch
    ```

4. Perform the compilation.

    ```bash
    # The installation path is user-defined. The following uses `/home/path/to/ffmpegInstall` as an example.
    ./configure \
    --prefix=/home/path/to/ffmpegInstall \
    --enable-shared \
    --disable-debug \
    --extra-cflags="-O3 -g1 -falign-functions=64" \
    --extra-ldflags="-lm -lstdc++ -Wl,-O3"
    ```

    | Parameter| Description|
    | :--- | :--- |
    | `--prefix` | Specifies the installation path.|
    | `--enable-shared` | Specifies the dynamic library to be generated.|
    | `--disable-debug` | Disables the debugging mode.|
    | `--extra-cflags` | Specifies the compilation options used during compilation.|
    | `--extra-ldflags` | Specifies the link options used during linking.|

5. Use 32 parallel tasks (threads) for compilation. You can select the number of tasks based on the actual number of CPU cores.

    ```bash
    make -j32
    ```

6. Install the generated binary and library files to the specified path.

    ```bash
    make install
    ```

7. Configure environment variables and check whether FFmpeg is installed.

    ```bash
    # Configure environment variables. The lib path is specified during compilation and installation.
    export LD_LIBRARY_PATH=/home/path/to/ffmpegInstall/lib:$LD_LIBRARY_PATH
    /home/path/to/ffmpegInstall/bin/ffmpeg -version
    ```

    If the version number is displayed, FFmpeg is installed.

    ```bash
    ffmpeg version 4.4.2 Copyright (c) 2000-2021 the FFmpeg developers
    built with gcc 10.3.1 (GCC)
    ```
