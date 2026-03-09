# 项目介绍

鲲鹏FFmpeg代码仓是基于鲲鹏服务器对开源FFmpeg进行优化的成果，存放**FFmpeg性能优化**的相关补丁。补丁功能包括：

* sws\_scale函数优化：sws\_scale函数是FFmpeg框架中libswscale库的核心函数之一，主要用于图像的缩放、色彩空间转换以及像素格式转换。色彩空间转换优化补丁通过并行优化色彩空间转换函数，实现性能提升。

* ffmpeg_4.4.2-optimize-scale.patch: 该补丁主要是针对FFmpeg-4.4.2中的libswscale的bilinear，bicubic和lanczos三种缩放算法下进行的优化。主要通过不同场景的向量化定制改写，使能SVE向量化以及指令流水化等方法，提升当前缩放算法的性能。

# 目录结构

```bash
├──docs/                                 # 项目文档目录
│   └── zh/                               # 中文文档目录
├──patch_for_ffmpeg_4.2.2_to_support_HW265ENC/  # HW265编码器FFmpeg-4.2.2插件目录
├──patch_for_ffmpeg_7.0.1_to_support_HW265ENC/  # HW265编码器FFmpeg-7.0.1插件目录
├──ffmpeg_4.4.2-optimize-scale.patch            # FFmpeg-4.4.2缩放算法优化补丁
├──huawei_ffmpeg-7.1.1_sws_scale_optimize.patch # FFmpeg-7.1.1缩放算法优化补丁
├──LICENSE.md                                    # 许可证说明文件
├──.gitattributes                                # Git属性配置文件
├──COPYING.LGPLv2.1                              # LGPL v2.1许可证文本
├──ffmpeg-7.1.1.tar.gz                           # FFmpeg 7.1.1源码压缩包
└──README.md                                     # 项目说明文档
```

# FFmpeg 7.1.1 缩放优化特性

## 特性简介

sws\_scale函数是FFmpeg框架中libswscale库的核心函数之一，主要用于图像的缩放、色彩空间转换以及像素格式转换。色彩空间转换优化补丁通过并行优化色彩空间转换函数，实现性能提升。

## 版本说明

| 开源软件版本 | 补丁特性 |
| :--- | :--- |
| FFmpeg-7.1.1 | sws_scale函数优化 |

## 环境部署

1. 克隆本仓库。

    ```bash
    git clone https://gitcode.com/boostkit/ffmpeg.git
    cd ffmpeg
    ```

2. 解压FFmpeg-7.1.1源码。

    ```bash
    tar -zxf ffmpeg-7.1.1.tar.gz 
    ```

3. 将FFpmeg补丁拷贝到FFmpeg-7.1.1代码根目录。

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

# FFmpeg 4.4.2 缩放优化特性

## 特性简介

该特性针对FFmpeg-4.4.2中的libswscale的bilinear，bicubic和lanczos三种缩放算法下进行的优化。主要通过不同场景的向量化定制改写，使能SVE向量化以及指令流水化等方法，提升当前缩放算法的性能。

## 版本说明

| 开源软件版本 | 补丁特性 |
| :--- | :--- |
|FFmpeg-4.4.2|sws_scale函数中bilinear，bicubic和lanczos算法优化|

## 环境部署

FFmpeg-4.4.2版本的scale优化的环境部署具体参考《[FFmpeg-4.4.2_scale工具安装指南](./docs/zh/ffmpeg_4.4.2_install_guide.md)》

## 快速入门

```bash
# 使用YUV序列进行测试
taskset -c 88 /home/path/to/ffmpegInstall/bin/ffmpeg -f rawvideo -pix_fmt yuv420p -video_size 1920x1080 -i /home/path/to/video/Ca4_1920x1080.yuv -vf "scale=1280:720" -sws_flags "bilinear" -pix_fmt yuv420p -y output_1280x720.yuv
```

# License

本项目采用GNU LESSER GENERAL PUBLIC LICENSE 2.1许可证。详见[LICENSE](https://gitcode.com/boostkit/ffmpeg/blob/master/LICENSE.md)文件。

本项目的文档适用CC-BY 4.0许可证，具体请参见[LICENSE](./docs/zh/LICENSE)文件。

# 贡献声明

欢迎大家为社区做贡献，如果使用过程中有任何问题/建议，或者需要反馈特性需求和bug报告，可以提交[Issues](https://gitcode.com/boostkit/ffmpeg/issues)联系我们，具体贡献方法可参考[这里](https://gitcode.com/boostkit/community/blob/master/docs/contributor/contributing.md)。同时也欢迎大家在[讨论专区](https://gitcode.com/boostkit/community/discussions)展开讨论交流。感谢您的支持。
