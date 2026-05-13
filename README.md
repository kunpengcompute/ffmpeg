# FFmpeg 介绍

## 最新消息
* [2026.03.30]：基于鲲鹏服务器对FFmpeg-4.4.2中libswscale库的bilinear、bicubic和lanczos三种缩放算法进行性能优化。
* [2025.09.30]：基于鲲鹏服务器对FFmpeg-7.1.1中libswscale库的sws\_scale函数进行性能优化。

## 简介

鲲鹏FFmpeg代码仓是基于鲲鹏服务器对开源FFmpeg进行优化的成果，存放**FFmpeg性能优化**的相关补丁。补丁功能包括：

* FFmpeg-7.1.1 sws\_scale函数优化：sws\_scale函数是FFmpeg框架中libswscale库的核心函数之一，主要用于图像的缩放、色彩空间转换以及像素格式转换。色彩空间转换优化补丁通过并行优化色彩空间转换函数，实现性能提升。

* FFmpeg-4.4.2 缩放算法优化：主要针对FFmpeg-4.4.2中libswscale的bilinear、bicubic和lanczos三种缩放算法进行的优化。主要通过不同场景的向量化定制改写，使能SVE向量化以及指令流水化等方法，提升当前缩放算法的性能。

## 目录结构

```bash
├──docs/                                                            # 项目文档目录
│   ├── zh/                                                         # 中文文档目录
│   │   ├── ffmpeg_4.4.2_scale_optimization_feature_guide.md        # 特性指南（FFmpeg-4.4.2）
|   |   ├── fffmpeg_7.1.1_sws_scale_optimization_feature_guide.md   # 特性指南（FFmpeg-7.1.1）
│   │   ├── LICENSE                                                 # 文档许可证
├──ffmpeg_4.4.2-optimize-scale.patch                                # FFmpeg-4.4.2 缩放算法优化补丁
├──huawei_ffmpeg-7.1.1_sws_scale_optimize.patch                     # FFmpeg-7.1.1 sws_scale函数优化补丁
├──LICENSE                                                          # 开源许可证
├──.gitattributes                                                   # Git属性配置文件
├──COPYING.LGPLv2.1                                                 # LGPL v2.1许可证文本
├──ffmpeg-7.1.1.tar.gz                                              # FFmpeg 7.1.1源码压缩包
└──README.md                                                        # 项目说明文档
```

## FFmpeg 特性介绍

|特性|特性说明|链接|
| :--- | :--- | :--- | 
|FFmpeg-7.1.1 sws\_scale函数优化|针对FFmpeg-7.1.1中的sws\_scale函数进行优化|[FFmpeg-7.1.1 特性指南](./docs/zh/ffmpeg_7.1.1_sws_scale_optimization_feature_guide.md)|
|FFmpge-4.4.2 缩放算法优化|针对FFmpeg-4.2.2中的bilinear、bicubic和lanczos三种缩放算法进行优化|[FFmpeg-4.4.2 特性指南](./docs/zh/ffmpeg_4.4.2_scale_optimization_feature_guide.md)|

## License

本项目采用GNU LESSER GENERAL PUBLIC LICENSE 2.1许可证。详见[LICENSE](https://gitcode.com/boostkit/ffmpeg/blob/master/LICENSE.md)文件。

本项目的文档适用CC-BY 4.0许可证，具体请参见[LICENSE](./docs/zh/LICENSE)文件。

## 贡献声明

欢迎大家为社区做贡献，如果使用过程中有任何问题/建议，或者需要反馈特性需求和bug报告，可以提交[Issues](https://gitcode.com/boostkit/ffmpeg/issues)联系我们，具体贡献方法可参考[这里](https://gitcode.com/boostkit/community/blob/master/docs/contributor/contributing.md)。同时也欢迎大家在[讨论专区](https://gitcode.com/boostkit/community/discussions)展开讨论交流。感谢您的支持。
