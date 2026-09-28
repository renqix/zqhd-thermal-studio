# 来源与许可

项目整体按GPL-3.0发布；原作者版权声明保留。下面第三方文件保留其原有许可，不宣称由本项目原创。

|组件|来源|许可/备注|
|---|---|---|
|硬件、HTPAd驱动与色表基础|[umeiko/rpi2040-heimann-htpa32x32d-touchscreen](https://github.com/umeiko/rpi2040-heimann-htpa32x32d-touchscreen)，参考commit 327e4077d6b3974e00447d86d6b50dd48fdd057a|GPL-3.0；驱动保留Umeko/Heimann原署名|
|CST816T触摸驱动|umeiko上游携带的Atsushi Sasaki实现（文件头声明MIT）|MIT，见licenses/CST816T-MIT.txt|
|MLX90640备用驱动|源文件内Melexis署名|Apache-2.0；本设备实际使用Heimann|
|One Euro|[casiez/OneEuroFilter](https://github.com/casiez/OneEuroFilter)|MIT，完整声明在one_euro_pixel.h|
|EDSR与FSRCNN教师/预训练模型|[Saafke/EDSR_Tensorflow](https://github.com/Saafke/EDSR_Tensorflow)、[FSRCNN_Tensorflow](https://github.com/Saafke/FSRCNN_Tensorflow)|保留web/models内Apache-2.0许可证；固定版本及散列见downloads.json|
|Noto Sans CJK字形|[notofonts/noto-cjk](https://github.com/notofonts/noto-cjk)|SIL OFL1.1，见licenses/Noto-OFL.txt；只带字形子集|
|LVGL8.3.11 / TFT_eSPI2.5.43|[lvgl/lvgl](https://github.com/lvgl/lvgl)、[Bodmer/TFT_eSPI](https://github.com/Bodmer/TFT_eSPI)|MIT/FreeBSD等原许可见licenses，构建时由Arduino安装|
|Arduino-Pico3.9.5|[earlephilhower/arduino-pico](https://github.com/earlephilhower/arduino-pico)|核心及依赖按上游各自许可，构建时安装|

Tiny学生模型、合成热场数据、训练脚本和本项目新增代码按GPL-3.0发布。字体子集与第三方文件仍按各自原许可。下载官方大模型不会把私人传感器数据上传。
