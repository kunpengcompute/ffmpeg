# FFmpeg-4.4.2_scale工具安装指南

## 安装说明

本文主要介绍FFmpeg-4.4.2 工具的编译安装以及如何使能优化补丁。

## 环境配置

硬件环境：

|项目|说明|
| :--- | :--- |
| CPU | 鲲鹏950|

软件环境：

| 软件 | 版本 |
| :--- | :--- |
| OS | openEuler 22.03 LTS SP4 |
| 编译器 | gcc >= 10.3.1 |
| make | >= 4.3 |
| cmake | >= 3.22.0 |

其他：

sve长度需要设置为128，在openEuler 22.03 LTS SP4中可通过以下方式设置。

```shell
echo 16 > /proc/sys/abi/sve_default_vector_length
```

## 编译安装

1. 克隆本仓库，获取ffmpeg_4.4.2-optimize-scale.patch优化补丁。

    ```bash
    git clone https://gitcode.com/boostkit/ffmpeg.git
    ```

2. 获取ffmpeg-4.4.2 源码包，解压并进入源码目录。

    ```bash
    wget https://ffmpeg.org/releases/ffmpeg-4.4.2.tar.gz
    tar -zxvf ffmpeg-4.4.2.tar.gz
    cd ffmpeg-4.4.2
    ```

3. 将对应路径中的优化patch合入到ffmpeg-4.4.2中。

    ```bash
    patch -p1 < /path/to/ffmpeg_4.4.2-optimize-scale.patch
    ```

4. 执行编译。

    ```bash
    # 安装路径用户自行指定，这里以/home/path/to/ffmpegInstall为例
    ./configure \
    --prefix=/home/path/to/ffmpegInstall \
    --enable-shared \
    --disable-debug \
    --extra-cflags="-O3 -g1 -falign-functions=64" \
    --extra-ldflags="-lm -lstdc++ -Wl,-O3"
    ```

    | 参数 | 说明 |
    | :--- | :--- |
    | `--prefix` | 指定安装路径 |
    | `--enable-shared` | 指定生成动态库 |
    | `--disable-debug` | 指定关闭调试模式 |
    | `--extra-cflags` | 指定编译时使用的编译选项 |
    | `--extra-ldflags` | 指定链接时使用的链接选项 |

5. 使用32个并行任务（线程）进行编译，可根据实际CPU核数选择。

    ```bash
    make -j32
    ```

6. 将生成的二进制文件和库文件安装到指定的路径中。

    ```bash
    make install
    ```

7. 配置环境变量，并验证ffmpeg安装是否成功。

    ```bash
    # 配置环境变量，lib路径是上述编译安装时指定的
    export LD_LIBRARY_PATH=/home/path/to/ffmpegInstall/lib:$LD_LIBRARY_PATH
    /home/path/to/ffmpegInstall/bin/ffmpeg -version
    ```

    如果输出了对应版本号，说明ffmpeg安装成功。

    ```bash
    ffmpeg version 4.4.2 Copyright (c) 2000-2021 the FFmpeg developers
    built with gcc 10.3.1 (GCC)
    ```
