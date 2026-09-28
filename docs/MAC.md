# Mac本地工作台

建议Python3.12，安装Git、curl、ffmpeg（可用 `brew install ffmpeg`）。所有计算本机完成，服务只监听127.0.0.1:8766。

```sh
git clone https://github.com/renqix/zqhd-thermal-studio.git
cd zqhd-thermal-studio
python3.12 -m venv web/.venv
web/.venv/bin/pip install -r web/requirements-lock.txt
web/.venv/bin/python tools/download_models.py
web/.venv/bin/python tools/list_devices.py
export THERMAL_DEVICE_SERIAL='替换为你自己的设备序列号'
./web/start.sh
```

浏览器打开 http://127.0.0.1:8766/ 。终端Ctrl+C停止服务；刷机前先停止录制，再停止服务释放串口。没有设置登录自启。

选用“实时保边”可调降噪/细节强度与跨帧平滑；EDSR/FSRCNN为可选模型。模型下载失败不影响普通保边算法，界面会禁用不可用模型。模型使用官方固定版本并验证SHA256。

原始传感器预览与增强线程独立，增强只处理最新帧、不积压队列。默认关闭真实屏幕镜像以减少USB占用；需要看设备菜单时可开启。暂停设备画面不会暂停Mac原始流。

录像存到 `~/Downloads/thermal-recordings`，含MP4、原始温度JSONL及元信息。MP4标称10fps可能重复帧，不能视为传感器真实帧率。点击停止后才完成MP4封装。

增强设置保存到本机 `web/preview-settings.json`；设备临时参数仍不写入Flash。请勿把录像、参数文件或本地令牌提交到公共仓库。
