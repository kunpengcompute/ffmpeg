# 特性指南（FFmpeg-7.1.1）

## 特性描述

sws\_scale函数是FFmpeg框架中libswscale库的核心函数之一，主要用于图像的缩放、色彩空间转换以及像素格式转换。色彩空间转换优化补丁通过并行优化色彩空间转换函数，实现性能提升。

## 环境要求
本文基于鲲鹏服务器和openEuler操作系统提供指导，在正式操作前请确保软硬件环境均满足要求。

硬件环境：

|项目|说明|
| :--- | :--- |
| CPU | 鲲鹏920新型号处理器|

软件环境：

| 软件 | 版本 |
| :--- | :--- |
| OS | openEuler 22.03 LTS SP4 |
| 编译器 | gcc >= 10.3.1 |
| make | >= 4.3 |
| cmake | >= 3.10.0 |

## 编译安装

1. 克隆本仓库。

    ```bash
    git clone https://gitcode.com/boostkit/ffmpeg.git
    cd ffmpeg
    ```

2. 解压FFmpeg-7.1.1源码。

    ```bash
    tar -zxf ffmpeg-7.1.1.tar.gz 
    ```

3. 将FFmpeg补丁拷贝到FFmpeg-7.1.1代码根目录。

    ```bash
    cp huawei_ffmpeg-7.1.1_sws_scale_optimize.patch ffmpeg-7.1.1/
    ```

4. 应用FFmpeg补丁。

    ```bash
    cd ffmpeg-7.1.1
    patch -p1 < huawei_ffmpeg-7.1.1_sws_scale_optimize.patch
    ```

5. 编译FFmpeg，需要加上openmp相应选项（如下编译命令仅供参考，请按实际需求集成相应的编码库）。

    ```bash
    ./configure --enable-shared --enable-pthreads --enable-gpl --extra-cflags="-fopenmp" --extra-ldflags="-fopenmp"
    make
    sudo make install
    ```

6. 验证FFmpeg是否安装成功。

    ```bash
    export PATH=/usr/local/bin:$PATH
    export LD_LIBRARY_PATH=/usr/local/lib:$LD_LIBRARY_PATH
    ffmpeg -version
    ```
    如果输出了对应版本号，说明FFmpeg安装成功。
    ```bash
    ffmpeg version 7.1.1 Copyright (c) 2000-2025 the FFmpeg developers
    built with gcc 10.3.1 (GCC)
    ```
## 快速使用

```bash
# 使用YUV序列进行测试
ffmpeg -s 640x480 -pix_fmt yuv420p -i input.yuv -pix_fmt rgb24 output.rgb

# 在当前目录检查是否有输出文件
ls -lh output.rgb
```
执行测试命令，无错误信息且当前目录下有输出文件output.rgb，说明执行成功。