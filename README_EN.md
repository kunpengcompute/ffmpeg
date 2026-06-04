# Introduction to FFmpeg

## What's New

* Optimized scaling algorithms (Bilinear, Bicubic, and Lanczos) within the libswscale library of FFmpeg-4.4.2 based on Kunpeng servers.
* Optimized the `sws_scale` function in the libswscale library of FFmpeg-7.1.1 based on Kunpeng servers.

## Overview

Kunpeng FFmpeg optimizes FFmpeg based on Kunpeng servers. It stores patches for FFmpeg performance optimization. The patch provides the following functions:

* `sws_scale` optimization: The `sws_scale` function is one of the core functions of the libswscale library in the FFmpeg framework. It is mainly used for image scaling, color space conversion (CSC), and pixel format conversion. The CSC optimization patch improves performance by parallelizing CSC functions.

* `ffmpeg_4.4.2-optimize-scale.patch`: is used to optimize scaling algorithms (Bilinear, Bicubic, and Lanczos) within the libswscale library of FFmpeg-4.4.2. The vectorization customization and rewriting in different scenarios enable SVE vectorization and instruction pipelining, improving the performance of current scaling algorithms.

## Directory Structure

```text
├──docs/                                         # Project document directory
│   ├── en/                                      # English document directory
│   │   ├── ffmpeg_4.4.2_install_guide.md        # FFmpeg-4.4.2_scale installation guide
│   │   ├── LICENSE                              # Document license
├──patch_for_ffmpeg_4.2.2_to_support_HW265ENC/   # Directory of the HW265 encoder FFmpeg-4.2.2 plugin
├──patch_for_ffmpeg_7.0.1_to_support_HW265ENC/   # Directory of the HW265 encoder FFmpeg-7.0.1 plugin
├──ffmpeg_4.4.2-optimize-scale.patch             # FFmpeg-4.4.2 scaling algorithm optimization patch
├──huawei_ffmpeg-7.1.1_sws_scale_optimize.patch  # FFmpeg-7.1.1 scaling algorithm optimization patch
├──LICENSE                                       # Open-source license
├──.gitattributes                                # Git attribute configuration file
├──COPYING.LGPLv2.1                              # LGPL v2.1 license text
├──ffmpeg-7.1.1.tar.gz                           # FFmpeg 7.1.1 source code package
└── README.md                                    # Project description document
```

## FFmpeg 7.1.1 Scaling Optimization

### Feature Description

The `sws_scale` function is one of the core functions of the libswscale library in the FFmpeg framework. It is mainly used for image scaling, color space conversion (CSC), and pixel format conversion. The CSC optimization patch improves performance by parallelizing CSC functions.

### Version Description

| Open-Source Software Version| Patch Feature|
| :--- | :--- |
| FFmpeg-7.1.1 | sws_scale function optimization|

### Environment Deployment

1. Clone this repository.

    ```bash
    git clone https://gitcode.com/boostkit/ffmpeg.git
    cd ffmpeg
    ```

2. Decompress the FFmpeg-7.1.1 source code.

    ```bash
    tar -zxf ffmpeg-7.1.1.tar.gz 
    ```

3. Copy the FFmpeg patch to the root directory of the FFmpeg-7.1.1 code.

    ```bash
    cp huawei_ffmpeg-7.1.1_sws_scale_optimize.patch ffmpeg-7.1.1/
    ```

4. Apply the FFmpeg patch.

    ```bash
    cd ffmpeg-7.1.1
    patch -p1 < huawei_ffmpeg-7.1.1_sws_scale_optimize.patch
    ```

5. Compile FFmpeg. You need to add the corresponding OpenMP options. (The following commands are for reference only. Integrate the corresponding encoding library as required.)

    ```bash
    ./configure --enable-shared --enable-pthreads --enable-gpl --extra-cflags="-fopenmp" --extra-ldflags="-fopenmp"
    make
    sudo make install
    ```

6. Check whether FFmpeg is installed.

    ```bash
    export PATH=/usr/local/bin:$PATH
    export LD_LIBRARY_PATH=/usr/local/lib:$LD_LIBRARY_PATH
    ffmpeg -version
    ```

## FFmpeg 4.4.2 Scaling Optimization

### Feature Description

This feature optimizes scaling algorithms (Bilinear, Bicubic, and Lanczos) within the libswscale library of FFmpeg-4.4.2. The vectorization customization and rewriting in different scenarios enable SVE vectorization and instruction pipelining, improving the performance of current scaling algorithms.

### Version Description

| Open-Source Software Version| Patch Feature|
| :--- | :--- |
|FFmpeg-4.4.2|Optimization of the Bilinear, Bicubic, and Lanczos algorithms in the sws_scale function|

### Environment Deployment

For details about how to deploy the environment for scaling optimization in FFmpeg-4.4.2, see [FFmpeg-4.4.2_scale Installation Guide](./docs/en/ffmpeg_4.4.2_install_guide.md).

### Quick Start

```bash
# Use the YUV sequence for testing.
taskset -c 88 /home/path/to/ffmpegInstall/bin/ffmpeg -f rawvideo -pix_fmt yuv420p -video_size 1920x1080 -i /home/path/to/video/Ca4_1920x1080.yuv -vf "scale=1280:720" -sws_flags "bilinear" -pix_fmt yuv420p -y output_1280x720.yuv
```

## License

This project is licensed under GNU LESSER GENERAL PUBLIC LICENSE 2.1. For details, see [LICENSE](https://gitcode.com/boostkit/ffmpeg/blob/master/LICENSE).

The documents of this project are licensed under CC-BY 4.0. For details, see [LICENSE](./docs/en/LICENSE).

## Contribution Statement

We welcome your contributions to the community. If you have any questions/suggestions or want to provide feedback on feature requirements and bug reports, you can submit [issues](https://gitcode.com/boostkit/ffmpeg/issues). For details, see the [contribution guideline](https://gitcode.com/boostkit/community/blob/master/docs/contributor/contributing.md). You are also welcome to share insights in [Discussions](https://gitcode.com/boostkit/community/discussions). Thank you for your support.
