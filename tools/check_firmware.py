"""刷机前验证下载文件；本脚本不接触硬件。"""
import argparse,hashlib,json
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('firmware',type=Path);p.add_argument('--manifest',type=Path,default=Path(__file__).resolve().parents[1]/'dist/manifest.json');a=p.parse_args()
m=json.loads(a.manifest.read_text());digest=hashlib.sha256(a.firmware.read_bytes()).hexdigest()
if digest!=m['firmware_sha256']:raise SystemExit('SHA256不一致：请勿刷写此文件')
print('校验通过：',a.firmware.name,'\nSHA256:',digest)
