# 刷机后如何还原版本

## 先分清要还原哪一种

|目标|需要的文件|
|---|---|
|恢复自己买来时的原厂功能|**自己这台设备在改动前保存的整片Flash备份**|
|回到本项目某一版本|对应GitHub Release的UF2及SHA256清单|
|恢复临时温区/色板设置|固件恢复后重新设置；UF2不等于完整的运行参数快照|

本仓库不提供某位使用者的原厂整片备份。其它设备的整片镜像可能含不同板卡配置，不应当成通用原厂包。没有自己的原厂备份时，请向卖家/原项目确认准确的原厂固件。

## 首次刷写前：留好自己的恢复文件

1. 先记录PCB/探头/屏幕型号、接线、原始功能和当前固件；Mac安装picotool及本项目Python环境。
2. 停止网页录制，并Ctrl+C关闭工作台，释放串口。
3. 根据**具体板卡文档**进入RP2040 BOOTSEL。原生Pico通常按住BOOTSEL连接USB；成品的外部功能按钮不一定是BOOTSEL，不要猜测或盲目短接引脚。应查上游硬件图、卖家说明或板上明确标注的BOOT/RESET入口。
4. `picotool info -a`确认芯片/Flash容量，保存设备自己的Flash ID。以下 `YOUR_FLASH_ID` 必须替换为实际值，仅连接这一台目标设备。

```sh
mkdir -p backups/my-device
picotool info -a --ser YOUR_FLASH_ID
picotool save -a -v backups/my-device/original-full-flash.uf2 --ser YOUR_FLASH_ID
picotool save -a -v backups/my-device/original-full-flash.bin --ser YOUR_FLASH_ID
shasum -a 256 backups/my-device/original-full-flash.* > backups/my-device/SHA256SUMS
picotool reboot --ser YOUR_FLASH_ID
```

把备份和SHA256再复制到独立存储位置。`backups/`已被Git忽略。备份时设备上运行什么，保存的就是什么；已经刷过Tiny后新备份不会自动变成“原厂”。

## A. 设备还能正常连接USB

停止录制和工作台。兼容Arduino-Pico固件可通过1200波特串口触发BOOTSEL：

```sh
web/.venv/bin/python tools/list_devices.py
web/.venv/bin/python -c 'import serial,sys; p=serial.Serial(sys.argv[1],1200,timeout=.2); p.dtr=False; p.close()' /dev/cu.替换为实际串口
```

等设备重新枚举，再用 `picotool info -a --ser YOUR_FLASH_ID` 核对。如果固件不支持此方式，请使用B，不反复盲写。

## B. 黑屏、USB串口无响应

停止软件连接。设备带电池，拔USB不等于断电：先拨到关机，等待约10秒，然后按该板卡文档进入BOOTSEL。下载模式时黑屏本身正常；以 `picotool info -a` 能识别RP2040为准。若出现RPI-RP2盘，也说明进入ROM下载模式。

仍识别不到时检查可传数据的USB线、端口、供电和正确BOOT入口。先恢复连接再刷写；不要用上游不同板型的按键猜测，也不要继续加频。

## C. 写入选定版本并校验

从Release下载UF2及匹配的manifest.json，先核对SHA：

```sh
python3 tools/check_firmware.py /path/to/zqhd-tiny-public.uf2 --manifest /path/to/manifest.json
picotool info -a --ser YOUR_FLASH_ID
picotool load -v /path/to/zqhd-tiny-public.uf2 --ser YOUR_FLASH_ID
picotool verify /path/to/zqhd-tiny-public.uf2 --ser YOUR_FLASH_ID
picotool reboot --ser YOUR_FLASH_ID
```

**回原厂**时，将以上UF2换成自己的 `original-full-flash.uf2`，用保存时的SHA256清单核对它；不要拿本项目发布清单去验证原厂文件。优先恢复UF2，避免裸BIN装载地址错误。

BOOTSEL盘拖入匹配UF2也是常见方式，但复制成功不等于实机功能验收；用picotool流程可明确得到回读校验。过程中保持电源和数据线稳定。

## D. 重启后的验收

检查热图、触摸、底部上滑菜单、色板、实体单/双/三击、实时温度与电池提示；再启动Mac工作台，确认设备序列号、所选版本的CPU频率（第12版264MHz、原第3版250MHz）和数据序号持续增加。重启后临时温区需要重新设置。

若新版本异常，按同样步骤恢复上一份已验证的UF2。需要报告问题时附硬件版本、固件SHA、复现步骤和脱敏日志；不要公开整片备份、序列号或私人录像。
