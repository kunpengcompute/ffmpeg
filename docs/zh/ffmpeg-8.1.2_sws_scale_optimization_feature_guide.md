# 特性指南（FFmpeg-8.1.2）

## 特性描述

该特性针对FFmpeg-8.1.2中libswscale的bilinear缩放算法、三种缩放比例进行性能优化。主要通过减少特定缩放比例的水平缩放filter表加载开销、垂直缩放函数向量化实现等方法，提高缩放函数的性能。

## 环境要求

本文基于鲲鹏服务器和openEuler操作系统提供指导，在正式操作前请确保软硬件环境均满足要求。

表1 硬件环境要求

| 项目 | 说明                |
| :--- | :------------------ |
| CPU  | 鲲鹏920新型号处理器 |

表2 操作系统和软件环境要求

| 软件   | 版本                    |
| :----- | :---------------------- |
| OS     | openEuler 22.03 LTS SP4 |
| 编译器 | GCC 10.3.1或以上版本    |
| make   | 4.3或以上版本           |
| cmake  | 3.22.0或以上版本        |

## 编译安装

1. 克隆本仓库。

   ```bash
   git clone https://gitcode.com/boostkit/ffmpeg.git
   ```

2. 获取FFmpeg-8.1.2源码包，解压并进入源码目录。

   ```bash
   wget https://ffmpeg.org/releases/ffmpeg-8.1.2.tar.gz
   tar -zxvf ffmpeg-8.1.2.tar.gz
   cd ffmpeg-8.1.2
   ```

3. 将FFmpeg补丁合入到FFmpeg-8.1.2中。

   ```bash
   patch -p1 < /path/to/ffmpeg-8.1.2-optimize-scale.patch
   ```

4. 执行编译。安装路径用户自行指定，这里以/home/path/to/ffmpegInstall为例。

   ```bash    
   ./configure \
   --prefix=/home/path/to/ffmpegInstall \
   --enable-shared \
   --disable-debug \
   --extra-cflags="-O3 -g1 -falign-functions=64" \
   --extra-ldflags="-lm -lstdc++ -Wl,-O3"
   ```

   | 参数              | 说明                     |
   | :---------------- | :----------------------- |
   | `--prefix`        | 指定安装路径             |
   | `--enable-shared` | 指定生成动态库           |
   | `--disable-debug` | 指定关闭调试模式         |
   | `--extra-cflags`  | 指定编译时使用的编译选项 |
   | `--extra-ldflags` | 指定链接时使用的链接选项 |

5. 使用32个并行任务（线程）进行编译，可根据实际CPU核数选择。

   ```bash
   make -j32
   ```

6. 将生成的二进制文件和库文件安装到指定的路径中。

   ```bash
   make install
   ```

7. 配置环境变量，并验证FFmpeg安装是否成功。其中，lib路径是上述编译安装时指定的。

   ```bash
   export LD_LIBRARY_PATH=/home/path/to/ffmpegInstall/lib:$LD_LIBRARY_PATH
   export PATH=/home/path/to/ffmpegInstall/bin:$PATH
   ffmpeg -version
   ```
   
   如果输出了对应版本号，说明FFmpeg安装成功。
   
   ```bash
   ffmpeg version 8.1.2 Copyright (c) 2000-2026 the FFmpeg developers
   built with gcc 10.3.1 (GCC)
   ```

## 快速使用

1. 使用YUV序列进行测试。

   ```bash
   # 1920×1080 → 1280×704
   taskset -c 88 ffmpeg -f rawvideo -pix_fmt yuv420p -video_size 1920x1080 -i input_1920x1080.yuv -vf "scale=1280:704:flags=bilinear" -pix_fmt rgb24 -f rawvideo -y output_sample1.rgb
   
   # 1920×1280 → 768×512
   taskset -c 88 ffmpeg -f rawvideo -pix_fmt yuv420p -video_size 1920x1280 -i input_1920x1280.yuv -vf "scale=768:512:flags=bilinear" -pix_fmt rgb24 -f rawvideo -y output_sample2.rgb
   
   # 3840×2160 → 1280×704
   taskset -c 88 ffmpeg -f rawvideo -pix_fmt yuv420p -video_size 3840x2160 -i input_3840x2160.yuv -vf "scale=1280:704:flags=bilinear" -pix_fmt rgb24 -f rawvideo -y output_sample3.rgb
   ```

2. 验证测试是否执行成功。

   ```bash
   ls -lh output_sample1.rgb output_sample2.rgb output_sample3.rgb
   ```

   如果无错误信息且当前目录下有相应的输出文件，说明执行成功。
