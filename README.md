# 项目介绍<a name="ZH-CN_TOPIC_0000002442319924"></a>

鲲鹏FFmpeg代码仓是基于鲲鹏服务器对开源FFmpeg进行优化的成果，存放**FFmpeg性能优化**的相关补丁。补丁功能包括：

sws\_scale函数优化：sws\_scale函数是FFmpeg框架中libswscale库的核心函数之一，主要用于图像的缩放、色彩空间转换以及像素格式转换。色彩空间转换优化补丁通过并行优化色彩空间转换函数，实现性能提升。

# 版本说明<a name="ZH-CN_TOPIC_0000002475600029"></a>

<a name="table740710612324"></a>
<table><thead align="left"><tr id="row144075603214"><th class="cellrowborder" valign="top" width="50%" id="mcps1.1.3.1.1"><p id="p84081465329"><a name="p84081465329"></a><a name="p84081465329"></a>开源软件版本</p>
</th>
<th class="cellrowborder" valign="top" width="50%" id="mcps1.1.3.1.2"><p id="p19408196163219"><a name="p19408196163219"></a><a name="p19408196163219"></a>补丁特性</p>
</th>
</tr>
</thead>
<tbody><tr id="row64089653211"><td class="cellrowborder" valign="top" width="50%" headers="mcps1.1.3.1.1 "><p id="p1040846123216"><a name="p1040846123216"></a><a name="p1040846123216"></a>FFmpeg-7.1.1</p>
</td>
<td class="cellrowborder" valign="top" width="50%" headers="mcps1.1.3.1.2 "><p id="p19408196173215"><a name="p19408196173215"></a><a name="p19408196173215"></a>sws_scale函数优化</p>
</td>
</tr>
</tbody>
</table>

# 环境部署<a name="ZH-CN_TOPIC_0000002442160060"></a>

1.  克隆本仓库。

    ```
    git clone https://gitcode.com/boostkit/ffmpeg.git
    cd ffmpeg
    ```

2.  解压FFmpeg-7.1.1源码。

    ```
    tar -zxf ffmpeg-7.1.1.tar.gz 
    ```

3.  将FFpmeg补丁拷贝到FFmpeg-7.1.1代码根目录。

    ```
    cp huawei_ffmpeg-7.1.1_sws_scale_optimize.patch ffmpeg-7.1.1/
    ```

4.  应用FFmpeg补丁。

    ```
    cd ffmpeg-7.1.1
    patch -p1 < huawei_ffmpeg-7.1.1_sws_scale_optimize.patch
    ```

5.  编译FFmpeg，需要加上openmp相应选项（如下编译命令仅供参考，请按实际需求集成相应的编码库）。

    ```
    ./configure --enable-shared --enable-pthreads --enable-gpl --extra-cflags="-fopenmp" --extra-ldflags="-fopenmp"
    make
    sudo make install
    ```

6.  验证FFmpeg是否安装成功。

    ```
    export PATH=/usr/local/bin:$PATH
    export LD_LIBRARY_PATH=/usr/local/lib:$LD_LIBRARY_PATH
    ffmpeg -version
    ```

# 贡献指南<a name="ZH-CN_TOPIC_0000002475679841"></a>

如果使用过程中有任何问题，或者需要反馈特性需求和bug报告，可以提交issue联系我们，具体贡献方法可参考[这里](https://gitcode.com/boostkit/community/blob/master/docs/contributor/contributing.md)。

# 免责声明<a name="ZH-CN_TOPIC_0000002475600033"></a>

此代码仓计划参与鲲鹏FFmpeg补丁开源，仅作FFmpeg功能扩展以及性能提升，编码风格遵照原生开源软件，继承原生开源软件安全设计，不破坏原生开源软件设计及编码风格和方式，软件的任何漏洞与安全问题，均由相应的上游社区根据其漏洞和安全响应机制解决。请密切关注上游社区发布的通知和版本更新。鲲鹏计算社区对软件的漏洞及安全问题不承担任何责任。

# 许可证书<a name="ZH-CN_TOPIC_0000002475679837"></a>

本项目采用GNU LESSER GENERAL PUBLIC LICENSE 2.1许可证。详见[LICENSE](https://gitcode.com/boostkit/ffmpeg/blob/master/LICENSE.md)文件。

