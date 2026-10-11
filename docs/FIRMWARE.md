# 单片机固件与构建

已验证硬件：RP2040、2MB Flash；Heimann HTPAd32×32（非MLX90640）；ST7789逻辑280×240；CST816T触摸；实体功能按钮GPIO24。探头0x1A、EEPROM0x50、触摸0x15。触摸SDA/SCL GPIO2/3；屏幕MOSI7/SCLK6/CS9/DC8；背光GPIO4；电池ADC26。其它探头/面板/PCB版本需要单独适配。

第12版采用264MHz/O3，不提高核心电压。本地同硬件样机经过短时检查；不同板卡不能保证相同稳定性，公开Noto字体构建也尚未额外刷机。原公开第3版250MHz文件仍保存在旧Release，可按恢复指南回退。不写探头EEPROM，模型不修改原始测温。

安装Arduino CLI后：

```sh
python3 tools/setup_firmware.py
./tools/build_firmware.sh
```

固定Arduino-Pico3.9.5、LVGL8.3.11、TFT_eSPI2.5.43；环境隔离在项目 `.toolchain/`。输出 `dist/zqhd-tiny-public.uf2`。构建不会自动刷机。目录 `config/` 保存必要的LVGL/显示配置。

源代码内已带Noto开源字形子集；字体来源、许可、生成方式见licenses与tools/make_font.py。公开构建与本地验收固件的字形不同，SHA不同，不应混称为同一个二进制。

默认Tiny插值，可在底部上滑→图像中切换原始/Tiny，并开启轮廓。滤波档位简化为0、1、3；插值档位简化为0、4。底部上滑→降噪可调整0～100背景降噪，默认35。温区、色板、滤波、亮度等只保存在RAM；重新上电恢复固件默认值。单击暂停，双击锁屏/解锁，三击切换白热/之前模式。

主机测试：`python3 tools/run_tests.py`。它不接触设备，不能替代触摸、屏幕和时延的实机验证。
