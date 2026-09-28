"""本机热成像控制台。串口只由一个线程读写；视频增强不改变原始温度。"""
from __future__ import annotations
import shutil
import atexit, base64, json, os, queue, re, secrets, signal, subprocess, threading, time, uuid
from collections import deque
from pathlib import Path
import cv2
import numpy as np
import serial
from serial.tools import list_ports
from flask import Flask, Response, jsonify, request, send_from_directory
from enhancement import PreviewEnhancer, DEFAULTS, validate_options

ROOT = Path(__file__).resolve().parent
TARGET = os.environ.get('THERMAL_DEVICE_SERIAL', '').upper()
RECORDINGS = Path.home() / 'Downloads' / 'thermal-recordings'
RECORDINGS.mkdir(parents=True, exist_ok=True)
cv2.setNumThreads(2)
app = Flask(__name__, static_folder='static')
TOKEN = secrets.token_urlsafe(32)


def load_palettes():
    text = (ROOT.parent/'firmware/thermal_v2/palettes.h').read_text()
    result = []
    for name in ['greys_r','greys','classic','hot','turbo','inferno','viridis']:
        block = re.search(rf'{name}\[180\]\s*=\s*\{{(.*?)\}}', text, re.S).group(1)
        values = np.array([int(x,16) for x in re.findall(r'0x[0-9a-fA-F]+',block)],dtype=np.uint16)
        result.append(np.stack([(values&31)*255//31,((values>>5)&63)*255//63,((values>>11)&31)*255//31],axis=1).astype(np.uint8))
    return result


PALETTES = load_palettes()


class Recorder:
    def __init__(self, bridge):
        self.bridge=bridge; self.lock=threading.RLock(); self.active=False
        self.started=0.; self.name=''; self.kind='enhanced'; self.error=''; self.process=None
        self.stop_event=threading.Event(); self.thread=None; self.data=None

    def start(self, kind):
        with self.lock:
            if self.active: raise ValueError('已经在录制')
            if kind not in ('screen','raw','enhanced'): raise ValueError('录制画面无效')
            if not self.bridge.frames.get(kind): raise ValueError('还没有可录制的实时画面')
            ffmpeg=shutil.which('ffmpeg')
            if not ffmpeg:raise ValueError('未找到ffmpeg，请先安装后再录制')
            self.kind=kind; self.name=time.strftime('%Y%m%d-%H%M%S')+'-'+uuid.uuid4().hex[:6]
            self.path=RECORDINGS/(self.name+'.part.mp4'); self.error=''
            self.log=open(RECORDINGS/(self.name+'.ffmpeg.log'),'wb')
            self.process=subprocess.Popen([ffmpeg,'-hide_banner','-loglevel','error','-y',
                '-f','rawvideo','-pixel_format','bgr24','-video_size','640x520','-framerate','10','-i','pipe:0',
                '-an','-c:v','libx264','-preset','veryfast','-crf','18','-pix_fmt','yuv420p','-movflags','+faststart',str(self.path)],stdin=subprocess.PIPE,stderr=self.log)
            self.data=open(RECORDINGS/(self.name+'.jsonl'),'w'); self.started=time.time(); self.active=True
            self.stop_event.clear(); self.thread=threading.Thread(target=self.run,daemon=True); self.thread.start()
            return self.status()

    def run(self):
        deadline=time.monotonic()
        try:
            while not self.stop_event.is_set():
                with self.bridge.lock: item=self.bridge.frames.get(self.kind)
                if item:
                    image=cv2.imdecode(np.frombuffer(item[1],np.uint8),cv2.IMREAD_COLOR)
                    scale=min(640/image.shape[1],480/image.shape[0])
                    image=cv2.resize(image,None,fx=scale,fy=scale,interpolation=cv2.INTER_NEAREST if self.kind!='enhanced' else cv2.INTER_AREA)
                    canvas=np.zeros((520,640,3),np.uint8); h,w=image.shape[:2]
                    canvas[(480-h)//2:(480-h)//2+h,(640-w)//2:(640-w)//2+w]=image
                    caption=f'{self.kind.upper()} | '+time.strftime('%Y-%m-%d %H:%M:%S')
                    if self.kind=='enhanced': caption+=' | PREVIEW ONLY'
                    if not self.bridge.connected: caption+=' | DISCONNECTED'
                    cv2.putText(canvas,caption,(12,504),cv2.FONT_HERSHEY_SIMPLEX,.48,(220,225,230),1,cv2.LINE_AA)
                    self.process.stdin.write(canvas.tobytes())
                deadline+=.1
                self.stop_event.wait(max(0,deadline-time.monotonic()))
                if deadline<time.monotonic()-1: deadline=time.monotonic()
        except Exception as error: self.error=str(error)
        finally:
            try:
                self.process.stdin.close(); code=self.process.wait(timeout=15)
                if code: self.error=self.error or '视频编码失败，请查看录制日志'
                elif self.path.exists(): self.path.rename(RECORDINGS/(self.name+'.mp4'))
            except Exception as error: self.error=str(error)
            with self.lock:
                self.active=False
                if self.data:self.data.close(); self.data=None
                self.log.close()
                (RECORDINGS/(self.name+'.json')).write_text(json.dumps({'view':self.kind,'started':self.started,'ended':time.time(),'error':self.error,'device':TARGET,'raw_file':self.name+'.jsonl'},ensure_ascii=False,indent=2))

    def raw(self, timestamp, sequence, temperatures, device):
        with self.lock:
            if self.active and self.data:
                self.data.write(json.dumps({'time':timestamp,'seq':sequence,'width':32,'height':32,'temperatures':temperatures.tolist(),'device':device},separators=(',',':'))+'\n')

    def stop(self):
        with self.lock: self.stop_event.set(); thread=self.thread
        if thread: thread.join(timeout=20)
        return self.status()

    def status(self):
        with self.lock:return {'active':self.active,'seconds':round(time.time()-self.started,1) if self.active else 0,'name':self.name,'kind':self.kind,'error':self.error}


class Bridge:
    def __init__(self):
        self.lock=threading.RLock(); self.connected=False; self.error='等待设备连接' if TARGET else '请先设置 THERMAL_DEVICE_SERIAL，再启动工作台'; self.port=''
        self.device={}; self.frames={}; self.raw=None; self.raw_seq=0; self.raw_time=0; self.state_time=0
        self.commands=queue.Queue(); self.pending={}; self.counter=10; self.stop_event=threading.Event()
        self.times=deque(maxlen=30); self.image_event=threading.Event();self.enhance_event=threading.Event()
        self.preview_options=dict(DEFAULTS)
        try:self.preview_options=validate_options(json.loads((ROOT/'preview-settings.json').read_text()))
        except (OSError,ValueError,TypeError):pass
        self.enhance=self.preview_options['method'];self.enhance_ms=0;self.enhance_error='';self.enhance_source_time=0;self.enhance_sequence=0;self.enhance_times=deque(maxlen=30)
        self.processor=PreviewEnhancer(ROOT/'models');self.model_ready=self.processor.model_info.get('fsrcnn',{}).get('ready',False);self.model_error=''
        self.recorder=Recorder(self)
        self.reader=threading.Thread(target=self.read_loop,daemon=True)
        self.worker=threading.Thread(target=self.image_loop,daemon=True)
        self.enhancer=threading.Thread(target=self.enhance_loop,daemon=True)

    def start(self):self.reader.start();self.worker.start();self.enhancer.start()

    def send(self,name,*values,wait=True):
        if not self.connected:raise ValueError('设备未连接')
        with self.lock:
            self.counter+=1; key=self.counter
            ticket={'event':threading.Event(),'ok':False};self.pending[key]=ticket
        self.commands.put((key,name,values))
        if not wait:return
        if not ticket['event'].wait(4):
            with self.lock:self.pending.pop(key,None)
            raise ValueError('设备没有确认参数，请检查连接')
        if not ticket['ok']:raise ValueError('设备拒绝此参数，原值保持不变')

    def exact(self,port,count):
        data=bytearray(); until=time.monotonic()+3
        while len(data)<count and time.monotonic()<until:
            data.extend(port.read(count-len(data)))
        if len(data)!=count:raise serial.SerialException('数据帧不完整，重新连接')
        return data

    def read_loop(self):
        while not self.stop_event.is_set():
            port=None
            try:
                matches=[p for p in list_ports.comports() if TARGET and (p.serial_number or '').upper()==TARGET]
                if len(matches)!=1:self.stop_event.wait(1);continue
                port=serial.Serial(matches[0].device,115200,timeout=.25,write_timeout=2,exclusive=True)
                port.reset_input_buffer(); self.port=matches[0].device
                screen=np.zeros((240,280,3),np.uint8); coverage=np.zeros((240,280),bool)
                # 握手获得STATE后才标记可用，避免将别的固件当成兼容协议。
                port.write(b'@1 stream 1\n@2 mirror 0\n'); handshake=time.monotonic(); last_packet=handshake
                while not self.stop_event.is_set():
                    while not self.commands.empty():
                        key,name,values=self.commands.get_nowait()
                        port.write(('@'+str(key)+' '+name+' '+' '.join(map(str,values))+'\n').encode())
                    line=port.readline()
                    if line.startswith(b'#ACK '):
                        _,key,ok=line.split();key=int(key)
                        with self.lock:ticket=self.pending.pop(key,None)
                        if ticket:ticket['ok']=bool(int(ok));ticket['event'].set()
                    elif line.startswith(b'#STATE '):
                        state=json.loads(line[7:]);assert state.get('protocol')==1
                        with self.lock:self.device=state;self.connected=True;self.error='';self.state_time=time.time()
                        last_packet=time.monotonic()
                    elif line.startswith(b'#FRAME '):
                        _,sequence,millis,length=line.split();assert int(length)==4096
                        data=np.frombuffer(self.exact(port,4096),dtype='<f4').copy()
                        if not np.isfinite(data).all():continue
                        stamp=time.time()
                        with self.lock:
                            self.raw=data;self.raw_seq=int(sequence);self.raw_time=stamp;self.times.append(stamp);device=dict(self.device)
                        self.recorder.raw(stamp,int(sequence),data,device);self.image_event.set();last_packet=time.monotonic()
                    elif line.startswith((b'#RECT ',b'#RLE ',b'#PAL ')):
                        fields=line.split();x,y,w,h=map(int,fields[1:5])
                        if not (0<=x<280 and 0<=y<240 and w>0 and h>0 and x+w<=280 and y+h<=240):raise ValueError('无效屏幕区域')
                        if fields[0]==b'#PAL':
                            colors,packed,length=map(int,fields[5:8])
                            if not 1<=colors<=256 or packed not in (0,1) or not 0<length<=w*h*2:raise ValueError('无效索引块')
                            palette=np.frombuffer(self.exact(port,colors*2),dtype='<u2')
                            indices=np.frombuffer(self.exact(port,length),dtype=np.uint8)
                            if packed:
                                if length%2:raise ValueError('无效索引游程')
                                pairs=indices.reshape(-1,2)
                                if int(pairs[:,0].sum())!=w*h:raise ValueError('无效索引像素数')
                                indices=np.repeat(pairs[:,1],pairs[:,0])
                            if indices.size!=w*h or int(indices.max())>=colors:raise ValueError('索引越界')
                            rgb=palette[indices].reshape(h,w)
                        elif fields[0]==b'#RLE':
                            length=int(fields[5])
                            if length<=0 or length>w*h*4 or length%4:raise ValueError('无效压缩长度')
                            pairs=np.frombuffer(self.exact(port,length),dtype='<u2').reshape(-1,2)
                            if int(pairs[:,0].sum())!=w*h:raise ValueError('无效压缩像素数')
                            rgb=np.repeat(pairs[:,1],pairs[:,0]).reshape(h,w)
                        else:rgb=np.frombuffer(self.exact(port,w*h*2),dtype='<u2').reshape(h,w)
                        screen[y:y+h,x:x+w]=np.stack([(rgb&31)*255//31,((rgb>>5)&63)*255//63,((rgb>>11)&31)*255//31],axis=-1).astype(np.uint8)
                        coverage[y:y+h,x:x+w]=True
                    elif line.startswith(b'#END'):
                        if coverage.all():self.publish('screen',screen)
                        coverage[:]=False
                    if time.monotonic()-last_packet>6:raise serial.SerialException('设备未返回兼容的网页数据流')
            except Exception as error:
                with self.lock:self.error=str(error)
            finally:
                with self.lock:
                    self.connected=False
                    for ticket in self.pending.values():ticket['event'].set()
                    self.pending.clear()
                while not self.commands.empty():
                    try:self.commands.get_nowait()
                    except queue.Empty:break
                if port:
                    try:port.write(b'@3 stream 0\n@4 mirror 0\n');port.close()
                    except Exception:pass
            self.stop_event.wait(1)

    def publish(self,kind,image):
        ok,jpeg=cv2.imencode('.jpg',image,[cv2.IMWRITE_JPEG_QUALITY,95])
        if ok:
            with self.lock:self.frames[kind]=(time.time(),jpeg.tobytes())

    def image_loop(self):
        # 原始画面独立刷新，慢模型不会拖住原始测温流或设备控制。
        while not self.stop_event.is_set():
            if not self.image_event.wait(.5):continue
            self.image_event.clear()
            with self.lock:
                if self.raw is None:continue
                raw=self.raw.copy().reshape(32,32);state=dict(self.device)
            low=state.get('lower',20) if state.get('custom') else float(raw.min());high=state.get('upper',40) if state.get('custom') else float(raw.max())
            native=np.flipud(self.processor.normalize(raw,low,high));palette=PALETTES[min(6,max(0,int(state.get('palette',4))))]
            self.publish('raw',cv2.resize(palette[np.rint(native*179).astype(np.uint8)],(384,384),interpolation=cv2.INTER_NEAREST))
            self.enhance_event.set()

    def enhance_loop(self):
        # 只处理最新帧，不排队积压；模型耗时高时优先保持低延迟。
        while not self.stop_event.is_set():
            if not self.enhance_event.wait(.5):continue
            self.enhance_event.clear()
            with self.lock:
                if self.raw is None:continue
                raw=self.raw.copy().reshape(32,32);state=dict(self.device);options=dict(self.preview_options);seq=self.raw_seq;stamp=self.raw_time
            try:
                start=time.monotonic();low=state.get('lower',20) if state.get('custom') else float(raw.min());high=state.get('upper',40) if state.get('custom') else float(raw.max())
                gray=self.processor.render(raw,low,high,options,seq,stamp)
                palette=PALETTES[min(6,max(0,int(state.get('palette',4))))]
                with self.lock:
                    if options!=self.preview_options:continue
                    self.publish('enhanced',palette[np.rint(gray*179).astype(np.uint8)])
                    self.enhance_ms=round((time.monotonic()-start)*1000,1);self.enhance_source_time=stamp;self.enhance_sequence=seq;self.enhance_times.append(time.time());self.enhance_error=''
            except Exception as error:self.enhance_error=str(error)

    def status(self):
        with self.lock:
            now=time.time();fps=(len(self.times)-1)/(self.times[-1]-self.times[0]) if len(self.times)>1 and self.times[-1]>self.times[0] else 0
            stats={'min':float(self.raw.min()),'max':float(self.raw.max()),'center':float(self.raw[16*32+16])} if self.raw is not None else {}
            return {'connected':self.connected and now-self.state_time<5,'error':self.error,'port':self.port,'device':self.device,'fps':round(fps,2) if now-self.raw_time<3 else 0,'age':round(now-self.raw_time,1) if self.raw_time else None,'seq':self.raw_seq,'stats':stats,'enhance':self.enhance,'enhance_ms':self.enhance_ms,'preview_options':self.preview_options,'models':self.processor.model_info,'enhance_error':self.enhance_error,'enhance_age':round(now-self.enhance_source_time,2) if self.enhance_source_time else None,'enhance_seq':self.enhance_sequence,'enhance_fps':round((len(self.enhance_times)-1)/(self.enhance_times[-1]-self.enhance_times[0]),1) if len(self.enhance_times)>1 and now-self.enhance_times[-1]<3 else 0,'model_ready':self.model_ready,'model_error':self.model_error,'recording':self.recorder.status()}

    def close(self):
        self.recorder.stop();self.stop_event.set()
        if self.reader.is_alive():self.reader.join(timeout=4)


bridge=Bridge()

@app.before_request
def local_guard():
    if request.host not in ('127.0.0.1:8766','localhost:8766'):return jsonify(error='仅允许本机访问'),403
    if request.method=='POST' and request.headers.get('X-Thermal-Token')!=TOKEN:return jsonify(error='请求校验失败，请刷新页面'),403

@app.get('/')
def index():return send_from_directory(ROOT/'static','index.html')

@app.get('/api/state')
def state():return jsonify(**bridge.status(),token=TOKEN)

@app.get('/api/live')
def live():
    # 三路画面共用一个短请求，避免浏览器长期连接被MJPEG占满。
    with bridge.lock:
        frames={kind:{'time':item[0],'image':base64.b64encode(item[1]).decode()} for kind,item in bridge.frames.items() if request.args.get(kind)!=str(item[0])}
    return jsonify(frames=frames)

@app.get('/api/snapshot/<kind>')
def snapshot(kind):
    with bridge.lock:item=bridge.frames.get(kind)
    if not item:return '画面尚未就绪',404
    return Response(item[1],mimetype='image/jpeg',headers={'Cache-Control':'no-store'})

@app.get('/api/matrix')
def matrix():
    with bridge.lock:return jsonify(width=32,height=32,seq=bridge.raw_seq,time=bridge.raw_time,temperatures=bridge.raw.tolist() if bridge.raw is not None else [])

@app.post('/api/params')
def params():
    try:
        data=request.get_json(); allowed={'lower','upper','custom','palette','filter','interpolation','emissivity','brightness','paused','locked','mirror','activation','match','point'}
        if not isinstance(data,dict) or not data or set(data)-allowed:raise ValueError('无效参数')
        commands=[]
        if 'lower' in data or 'upper' in data:
            if not {'lower','upper'}<=data.keys():raise ValueError('上下限必须一起提交')
            a,b=data['lower'],data['upper']
            if type(a)!=int or type(b)!=int or not -30<=a<=b<=300:raise ValueError('需要 -30 ≤ 最低 ≤ 最高 ≤ 300')
            commands.append(('range',a,b))
        bounds={'custom':(0,1),'palette':(0,6),'filter':(0,3),'interpolation':(0,4),'emissivity':(10,100),'brightness':(5,100),'paused':(0,1),'locked':(0,1),'mirror':(0,1),'activation':(0,1),'match':(0,2)}
        for key,value in data.items():
            if key in ('lower','upper'):continue
            if key=='point':
                if not isinstance(value,list) or len(value)!=2 or any(type(v)!=int or not 0<=v<32 for v in value):raise ValueError('测温点超出范围')
                commands.append(('point',*value))
            else:
                lo,hi=bounds[key]
                if type(value)!=int or not lo<=value<=hi:raise ValueError('参数范围错误：'+key)
                commands.append((key,value))
        for command in commands:bridge.send(*command)
        return jsonify(ok=True)
    except (ValueError,TypeError,KeyError) as error:return jsonify(error=str(error)),400

@app.post('/api/enhance')
def enhance():
    try:
        with bridge.lock:
            options=validate_options(request.get_json(),bridge.preview_options);method=options['method']
            if method in ('fsrcnn','edsr') and method not in bridge.processor.models:raise ValueError('所选本地模型未加载成功')
            temporary=ROOT/'preview-settings.json.tmp';temporary.write_text(json.dumps(options,ensure_ascii=False,indent=2));temporary.replace(ROOT/'preview-settings.json')
            bridge.preview_options=options;bridge.enhance=method
        bridge.enhance_event.set();return jsonify(ok=True,options=options)
    except (ValueError,TypeError) as error:return jsonify(error=str(error)),400

@app.get('/stream/<kind>')
def stream(kind):
    if kind not in ('screen','raw','enhanced'):return '未知画面',404
    def generate():
        last=0
        while True:
            with bridge.lock:item=bridge.frames.get(kind)
            if item and item[0]!=last:
                last=item[0];yield b'--frame\r\nContent-Type: image/jpeg\r\nContent-Length: '+str(len(item[1])).encode()+b'\r\n\r\n'+item[1]+b'\r\n'
            time.sleep(.035)
    return Response(generate(),mimetype='multipart/x-mixed-replace; boundary=frame',headers={'Cache-Control':'no-store'})

@app.post('/api/record')
def record():
    try:
        data=request.get_json() or {}
        if data.get('action')=='start':
            if data.get('kind')=='screen':bridge.send('mirror',1)
            result=bridge.recorder.start(data.get('kind','enhanced'))
        elif data.get('action')=='stop':result=bridge.recorder.stop()
        else:raise ValueError('未知录制操作')
        return jsonify(result)
    except ValueError as error:return jsonify(error=str(error)),400

@app.get('/api/recordings')
def recordings():
    return jsonify(files=[{'name':p.name,'size':p.stat().st_size,'modified':p.stat().st_mtime} for p in sorted(RECORDINGS.glob('*.mp4'),key=lambda p:p.stat().st_mtime,reverse=True) if '.part.' not in p.name])

@app.get('/recordings/<path:name>')
def download(name):
    if Path(name).name!=name or Path(name).suffix not in ('.mp4','.jsonl','.json'):return '无效文件',400
    return send_from_directory(RECORDINGS,name,as_attachment=request.args.get('download')=='1')

if __name__=='__main__':
    bridge.start();atexit.register(bridge.close)
    signal.signal(signal.SIGTERM,lambda *_:(_ for _ in ()).throw(SystemExit(0)))
    app.run(host='127.0.0.1',port=8766,threaded=True,use_reloader=False)
