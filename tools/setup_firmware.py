"""建立项目专用Arduino环境，不覆盖使用者其它项目的库配置。"""
from pathlib import Path
import json,shutil,subprocess
root=Path(__file__).resolve().parents[1];base=root/'.toolchain';base.mkdir(exist_ok=True)
config=base/'arduino-cli.json'
config.write_text(json.dumps({'board_manager':{'additional_urls':['https://github.com/earlephilhower/arduino-pico/releases/download/global/package_rp2040_index.json']},'directories':{'data':str(base/'data'),'downloads':str(base/'downloads'),'user':str(base/'sketchbook')}},indent=2))
cli=['arduino-cli','--config-file',str(config)]
for args in [['core','update-index'],['core','install','rp2040:rp2040@3.9.5'],['lib','install','lvgl@8.3.11'],['lib','install','TFT_eSPI@2.5.43']]:subprocess.run(cli+args,check=True)
libraries=base/'sketchbook/libraries'
shutil.copy2(root/'config/lv_conf.h',libraries/'lv_conf.h');shutil.copy2(root/'config/User_Setup.h',libraries/'TFT_eSPI/User_Setup.h')
print('构建环境已准备好。运行 tools/build_firmware.sh。')
