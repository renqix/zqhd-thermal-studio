"""只运行主机逻辑测试，不刷写设备。"""
from pathlib import Path
import subprocess,tempfile,shutil
root=Path(__file__).resolve().parents[1];compiler=shutil.which('clang++') or shutil.which('g++')
assert compiler,'需要C++编译器'
with tempfile.TemporaryDirectory() as temporary:
    for source in sorted((root/'tests').glob('*.cpp')):
        out=Path(temporary)/source.stem
        objects=[]
        if source.stem=='one_euro_test':
            obj=Path(temporary)/'reference.o'
            subprocess.run([shutil.which('clang') or 'cc','-c',str(root/'tests/reference/SF1eFilter.c'),'-o',str(obj)],check=True);objects=[str(obj)]
        subprocess.run([compiler,'-std=c++17','-O2','-fsanitize=address,undefined',str(source),*objects,'-o',str(out)],check=True)
        subprocess.run([str(out)],check=True)
print('全部主机测试通过')
