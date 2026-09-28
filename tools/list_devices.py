"""只列出本机串口；不会写入或重启设备。"""
from serial.tools import list_ports
for p in list_ports.comports():print(f'{p.device}\t序列号={p.serial_number or "未知"}\t{p.description}')
