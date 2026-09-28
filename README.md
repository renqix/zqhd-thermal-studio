# ZQHD Thermal Studio · 热成像共创工作台

为 RP2040 + Heimann HTPAd 32×32 触屏热像仪开发的固件、Mac本地预览与 Tiny 蒸馏模型实验。基于 [umeiko 的开源硬件/驱动项目](https://github.com/umeiko/rpi2040-heimann-htpa32x32d-touchscreen)，保留署名和 GPL-3.0 许可，欢迎通过 Issue / Pull Request 共创。

**这是特定硬件的社区项目，并非所有 RP2040 或 MLX90640 设备通用。** 原生传感器仍是32×32；插值和模型只改善观看效果，真实温度始终取原始矩阵。公开固件替换了私有试验时使用的系统字体，采用 Noto 开源字形；算法沿用用户已验收Tiny版本，但公开字体构建尚未额外刷机验收。

## 三部分能力

|部分|功能|
|---|---|
|单片机固件|236×236等比热图、手动温区−30～300°C、当前MIN/MAX匹配、点测温、7种色板、保边去噪、L3/Tiny插值|
|实体与触摸交互|单击暂停，双击锁屏，三击当前色板/白热切换；底部上滑设置；电量颜色条、暂停/锁屏双灯|
|Mac工作台|真实屏幕镜像、原始矩阵、独立增强预览、设备参数控制、MP4录制与原始温度JSONL|
|模型实验|Mac EDSR/FSRCNN；MCU运行332参数INT8学生网络，包含权重、训练脚本和合成数据|

## 从哪里开始

- [Mac安装与使用](docs/MAC.md)
- [编译与硬件适配](docs/FIRMWARE.md)
- **[刷机、备份与还原版本指南](docs/RESTORE.md)**
- [模型能力、实验结果与局限](docs/MODEL_CARD.md)
- [项目交接与下一步](docs/HANDOFF.md)
- [贡献指南](CONTRIBUTING.md)、[第三方来源和许可证](THIRD_PARTY_NOTICES.md)
- [下载公开固件](https://github.com/renqix/zqhd-thermal-studio/releases)

## 项目结构

```text
firmware/thermal_v2/  固件、传感器驱动、Tiny整数推理
config/              LVGL与显示屏构建配置
web/                 本机网页工作台
models/tiny/         学生权重、合成训练数据与训练记录
tools/               环境准备、模型下载、训练、校验脚本
tests/               主机端逻辑测试
docs/                硬件、恢复、模型卡和交接
```

公开包不包含个人原厂整片备份、设备序列号、真实采样录像、历史访问令牌或系统字体。使用者应备份自己的设备。具体测温范围与精度取决于探头和校准，软件没有重新认证卖家标称的精度。
