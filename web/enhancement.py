"""电脑端显示增强：保持原始测温矩阵独立，所有输出仅为灰度预览。"""
from pathlib import Path
import cv2
import numpy as np

DEFAULTS={'method':'faithful','denoise':60,'detail':25,'temporal':True}
METHODS=('none','bilateral','lanczos','fsrcnn','faithful','edsr')

def validate_options(update,current=None):
    if not isinstance(update,dict) or not update or set(update)-set(DEFAULTS):raise ValueError('无效增强参数')
    result=dict(current or DEFAULTS)
    for key,value in update.items():
        if key=='method':
            if value not in METHODS:raise ValueError('未知增强模式')
        elif key=='temporal':
            if type(value)!=bool:raise ValueError('稳定开关需要布尔值')
        elif type(value)!=int or not 0<=value<=100:raise ValueError('增强强度需要0到100之间的整数')
        result[key]=value
    return result

class PreviewEnhancer:
    def __init__(self,models_dir,load_models=True):
        self.models={};self.model_info={};self.previous=None;self.last_sequence=None;self.last_stamp=0.;self.last_key=None
        if load_models:
            for name,filename in [('fsrcnn','FSRCNN_x2.pb'),('edsr','EDSR_x2.pb')]:
                try:
                    model=cv2.dnn_superres.DnnSuperResImpl_create();model.readModel(str(Path(models_dir)/filename));model.setModel(name,2)
                    model.upsample(np.zeros((32,32,3),np.uint8));self.models[name]=model;self.model_info[name]={'ready':True,'scale':2,'error':''}
                except Exception as error:self.model_info[name]={'ready':False,'scale':2,'error':str(error)}

    def temporal_filter(self,raw,sequence,stamp,strength,enabled,key):
        # 参数变化、重连和长时间断流重置历史，重复同一帧不会叠加滤波。
        reset=self.previous is None or key!=self.last_key or stamp-self.last_stamp>.6 or (self.last_sequence is not None and sequence<self.last_sequence)
        if reset or not enabled or strength==0:self.previous=raw.copy()
        elif sequence!=self.last_sequence:
            difference=np.abs(raw-self.previous)
            if np.median(difference)>1.5:self.previous=raw.copy()
            else:
                floor=1-.85*strength;gain=floor+(1-floor)*np.minimum(difference/.8,1)**2
                self.previous+=gain*(raw-self.previous)
        self.last_sequence=sequence;self.last_stamp=stamp;self.last_key=key
        return self.previous.copy()

    @staticmethod
    def normalize(data,low,high):
        if high<=low:return np.where(data<low,0,np.where(data>high,1,.5)).astype(np.float32)
        return np.clip((data-low)/(high-low),0,1).astype(np.float32)

    def render(self,raw,low,high,options,sequence,stamp):
        raw=np.asarray(raw,dtype=np.float32).reshape(32,32)
        if not np.isfinite(raw).all():raise ValueError('无效温度帧')
        method=options['method'];strength=options['denoise']/100;detail=options['detail']/100
        if method=='none':
            return np.clip(cv2.resize(np.flipud(self.normalize(raw,low,high)),(384,384),interpolation=cv2.INTER_CUBIC),0,1)
        key=(method,options['denoise'],options['temporal'])
        filtered=self.temporal_filter(raw,sequence,stamp,strength,options['temporal'],key)
        if strength:
            if method in ('faithful','edsr'):
                # 温度单位下的引导滤波：使用自身结构作为引导，保留明显冷热边界。
                filtered=cv2.ximgproc.guidedFilter(filtered,filtered,1,(.05+.5*strength)**2)
            else:filtered=cv2.bilateralFilter(filtered,5,.05+.5*strength,1.0)
        source=np.ascontiguousarray(np.flipud(self.normalize(filtered,low,high)))
        interpolation=cv2.INTER_LANCZOS4 if method in ('faithful','edsr','lanczos') else cv2.INTER_CUBIC
        result=cv2.resize(source,(384,384),interpolation=interpolation)
        if method in ('fsrcnn','edsr'):
            if method not in self.models:raise ValueError('所选模型未加载成功')
            model_input=cv2.cvtColor(np.rint(source*255).astype(np.uint8),cv2.COLOR_GRAY2BGR)
            predicted=self.models[method].upsample(model_input)
            predicted=cv2.cvtColor(predicted,cv2.COLOR_BGR2GRAY).astype(np.float32)/255
            predicted=cv2.resize(predicted,(384,384),interpolation=interpolation)
            # 与同帧常规插值混合，降低自然图像模型对热像结构的过度改写。
            result=.25*result+.75*predicted
        if detail and method in ('faithful','edsr'):
            residual=result-cv2.GaussianBlur(result,(0,0),1.4)
            result+=np.clip(residual*detail*1.5,-.015,.015)
        # 限制在原采样邻域的亮度范围内，抑制亮暗光环和异常模型输出。
        kernel=np.ones((3,3),np.uint8)
        minimum=cv2.resize(cv2.erode(source,kernel),(384,384),interpolation=cv2.INTER_NEAREST)
        maximum=cv2.resize(cv2.dilate(source,kernel),(384,384),interpolation=cv2.INTER_NEAREST)
        return np.clip(result,minimum,maximum)
