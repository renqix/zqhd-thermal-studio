# 公开版验证记录

- Arduino-Pico3.9.5 / LVGL8.3.11 / TFT_eSPI2.5.43，250MHz/O3构建通过。
- 公开版Flash509312字节，静态RAM213712字节，剩余48432字节。
- 按钮/布局/OneEuro参考一致性/色板/处理/跟踪/UI/Tiny共8个C++测试通过地址与未定义行为检测。
- 2项Python测试通过：本地主机与令牌校验、参数拒绝/持久化、平场保持与原始输入不变。
- Python及浏览器JavaScript语法检查通过。
- 仓库文本与UF2隐私检查通过，无开发者绝对路径、固定设备序列号或系统字体。
- Noto生成字形预览已人工检查。公开版UF2尚未实机刷写；原本地Tiny版本经过用户实机确认。
- 模型原始69组/282624点的整数等价性记录见tiny-checks.json，合成PSNR见MODEL_CARD.md。

预编译UF2的SHA256：`c093239ca36dbb6db76e0098fca17810d057beeb15bfccd81502d8ec8af80d7f`。
