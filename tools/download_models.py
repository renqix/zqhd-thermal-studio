"""下载官方模型，验证SHA256后才启用；不关闭TLS证书校验。"""
from pathlib import Path
import hashlib,json,subprocess
root=Path(__file__).resolve().parents[1]/'web/models'
for item in json.loads((root/'downloads.json').read_text()):
    path=root/item['file']
    if path.exists() and hashlib.sha256(path.read_bytes()).hexdigest()==item['sha256']:continue
    temporary=path.with_suffix('.download')
    subprocess.run(['curl','--fail','--location','--retry','2',item['url'],'-o',str(temporary)],check=True)
    assert hashlib.sha256(temporary.read_bytes()).hexdigest()==item['sha256'],'模型SHA256不一致'
    temporary.replace(path);print('模型就绪：',path.name)
