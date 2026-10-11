"""在本机蒸馏极小热像显示网络；不修改测温数据，不操作设备固件。"""
from pathlib import Path
import json,time,urllib.request
import cv2
import numpy as np

PROJECT=Path(__file__).resolve().parents[1]
ROOT=PROJECT/"models/tiny/legacy-v3"
WEB=PROJECT/"web"
rng=np.random.default_rng(92817)
cv2.setNumThreads(2)
teacher=cv2.dnn_superres.DnnSuperResImpl_create()
teacher.readModel(str(WEB/'models/EDSR_x2.pb'));teacher.setModel('edsr',2)

def patches(a):
    padded=np.pad(a,1,mode='edge')
    return np.lib.stride_tricks.sliding_window_view(padded,(3,3)).reshape(-1,9)-a.reshape(-1,1)

def phases(a):return a.reshape(32,2,32,2).transpose(0,2,1,3).reshape(-1,4)
def unphases(a):return a.reshape(32,32,2,2).transpose(0,2,1,3).reshape(64,64)
def baseline(a):return cv2.resize(a,(64,64),interpolation=cv2.INTER_LINEAR)
def teach(a):
    image=np.repeat(np.rint(a*255).astype(np.uint8)[...,None],3,axis=2)
    return cv2.cvtColor(teacher.upsample(image),cv2.COLOR_BGR2GRAY).astype(np.float32)/255

def synthetic():
    y,x=np.mgrid[0:64,0:64].astype(np.float32)/63
    a=np.full((64,64),rng.uniform(.2,.5),np.float32)
    a+=rng.uniform(-.25,.25)*x+rng.uniform(-.25,.25)*y
    for _ in range(rng.integers(2,6)):
        cx,cy=rng.uniform(-.1,1.1,2);sx,sy=rng.uniform(.04,.35,2)
        a+=rng.uniform(-.5,.5)*np.exp(-((x-cx)**2/sx**2+(y-cy)**2/sy**2)/2)
    if rng.random()<.65:
        angle=rng.uniform(0,6.28);d=x*np.cos(angle)+y*np.sin(angle)-rng.uniform(-.5,.7)
        a+=rng.uniform(-.35,.35)/(1+np.exp(np.clip(d/rng.uniform(.008,.04),-30,30)))
    return np.clip(a,.02,.98).astype(np.float32)

def build_data():
    # 按整幅场景拆分，避免同一场景的相邻小块同时进入训练和测试。
    arrays={};sources=[]
    for split,count in [('train',360),('valid',50),('test',80)]:
        inputs=[];targets=[];truth=[];teachers=[]
        for index in range(count):
            clean=synthetic();small=cv2.resize(clean,(32,32),interpolation=cv2.INTER_AREA)
            noisy=np.clip(small+rng.normal(0,rng.uniform(.002,.015),small.shape),0,1)
            noisy=(np.rint(noisy*255)/255).astype(np.float32)
            taught=teach(small)
            # 大模型只占目标的25%，其余为已知合成真值，减少自然图像假纹理迁移。
            target=.25*taught+.75*clean
            inputs.append(noisy);targets.append(target);truth.append(clean);teachers.append(taught)
            if index%100==0:print('教师样本',split,index,flush=True)
        for key,value in [('input',inputs),('target',targets),('truth',truth),('teacher',teachers)]:arrays[split+'_'+key]=np.array(value,np.float32)
    np.savez_compressed(ROOT/'dataset.npz',**arrays)
    return arrays

data=dict(np.load(ROOT/'dataset.npz')) if (ROOT/'dataset.npz').exists() else build_data()
X=np.concatenate([patches(a) for a in data['train_input']])
Y=np.concatenate([phases(np.clip(b-baseline(a),-.15,.15)) for a,b in zip(data['train_input'],data['train_target'])])
V=np.concatenate([patches(a) for a in data['valid_input']])
T=np.concatenate([phases(np.clip(b-baseline(a),-.15,.15)) for a,b in zip(data['valid_input'],data['valid_target'])])
params=[(rng.standard_normal((9,16))*np.sqrt(2/9)).astype(np.float32),np.zeros(16,np.float32),(rng.standard_normal((16,8))*np.sqrt(2/16)).astype(np.float32),np.zeros(8,np.float32),np.zeros((8,4),np.float32),np.zeros(4,np.float32)]
mom=[np.zeros_like(a) for a in params];vel=[np.zeros_like(a) for a in params]
def forward(x,p):
    h=np.maximum(x@p[0]+p[1],0);k=np.maximum(h@p[2]+p[3],0)
    return k@p[4]+p[5],h,k
best=1e9;bestp=None;iteration=0;history=[]
for epoch in range(35):
    order=rng.permutation(len(X));losses=[];rate=.003*(.96**epoch)
    for pos in range(0,len(X),2048):
        ids=order[pos:pos+2048];x=X[ids];y=Y[ids];out,h,k=forward(x,params)
        error=out-y;losses.append(float(np.mean(error**2)));g=2*error/error.size
        gk=(g@params[4].T)*(k>0);gh=(gk@params[2].T)*(h>0)
        grads=[x.T@gh,gh.sum(0),h.T@gk,gk.sum(0),k.T@g,g.sum(0)]
        iteration+=1
        for i,(p,grad) in enumerate(zip(params,grads)):
            if i%2==0:grad+=1e-6*p
            mom[i]=.9*mom[i]+.1*grad;vel[i]=.999*vel[i]+.001*grad*grad
            p-=rate*(mom[i]/(1-.9**iteration))/(np.sqrt(vel[i]/(1-.999**iteration))+1e-8)
    vl=float(np.mean((forward(V,params)[0]-T)**2));history.append({'epoch':epoch,'train_loss':float(np.mean(losses)),'valid_loss':vl})
    if vl<best:best=vl;bestp=[p.copy() for p in params]
    if epoch%5==0:print('蒸馏训练',history[-1],flush=True)
params=bestp;np.savez(ROOT/'student-float.npz',**{f'p{i}':a for i,a in enumerate(params)})
(ROOT/'training.json').write_text(json.dumps(history,indent=2))

# 校准整数运算范围。权重INT8，偏置INT32，激活INT8，缩放采用Q16。
_,h,k=forward(X,params);scales=[1/127,max(float(h.max()),1e-3)/127,max(float(k.max()),1e-3)/127,1/4096]
quant=[]
for i in range(3):
    weight,bias=params[2*i:2*i+2];ws=max(float(np.abs(weight).max()),1e-6)/127
    q=np.rint(weight/ws).clip(-127,127).astype(np.int8);b=np.rint(bias/(scales[i]*ws)).astype(np.int32)
    multiplier=round(scales[i]*ws/scales[i+1]*65536)
    quant.append({'weight':q,'bias':b,'multiplier':multiplier})

def quant_forward(a):
    # 输入先转8位显示强度，温度矩阵始终不被模型覆盖。
    a=np.rint(a*255).clip(0,255).astype(np.float32)/255
    value=np.rint(patches(a)*127).astype(np.int32)
    for i,layer in enumerate(quant):
        value=value@layer['weight'].astype(np.int32)+layer['bias']
        value=((value.astype(np.int64)*layer['multiplier']+32768)>>16).astype(np.int32)
        if i<2:value=value.clip(0,127)
    residual=unphases(value.astype(np.float32)/4096).clip(-.15,.15)
    result=baseline(a)+residual
    lo=cv2.resize(cv2.erode(a,np.ones((3,3),np.uint8)),(64,64),interpolation=cv2.INTER_NEAREST)
    hi=cv2.resize(cv2.dilate(a,np.ones((3,3),np.uint8)),(64,64),interpolation=cv2.INTER_NEAREST)
    return np.clip(result,lo,hi)

def psnr(a,b):return float(-10*np.log10(max(float(np.mean((a-b)**2)),1e-12)))
scores={method:[] for method in ['bilinear','cubic','lanczos','edsr','student_float','student_int8']}
for a,b,t in zip(data['test_input'],data['test_truth'],data['test_teacher']):
    outputs={'bilinear':baseline(a),'cubic':cv2.resize(a,(64,64),interpolation=cv2.INTER_CUBIC),'lanczos':cv2.resize(a,(64,64),interpolation=cv2.INTER_LANCZOS4),'edsr':teach(a),'student_float':np.clip(baseline(a)+unphases(forward(patches(a),params)[0]).clip(-.15,.15),0,1),'student_int8':quant_forward(a)}
    for name,out in outputs.items():scores[name].append(psnr(np.clip(out,0,1),b))
weight_bytes=sum(a['weight'].nbytes+a['bias'].nbytes+4 for a in quant)
report={'training_scenes':360,'validation_scenes':50,'test_scenes':80,'real_frames_check_only':0,'architecture':'9→16 ReLU→8 ReLU→4 residual subpixels','parameters':sum(p.size for p in params),'mac_per_32x32_frame':(9*16+16*8+8*4)*1024,'packed_model_bytes':weight_bytes,'test_psnr_db':{name:round(float(np.mean(v)),3) for name,v in scores.items()},'limitation':'合成场景指标；不能据此证明真实分辨率提高。本次重训结果需重新核验和实机测时。'}
(ROOT/'report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2))
np.savez(ROOT/'student-int8.npz',**{f'{key}{i}':value for i,l in enumerate(quant) for key,value in l.items()})
header=['// 本机实验训练生成；仅用于显示副本，不替代测温。','#pragma once','#include <stdint.h>']
for i,l in enumerate(quant):
    for name,ctype in [('weight','int8_t'),('bias','int32_t')]:
        a=l[name];header.append(f'static const {ctype} tiny_{name}{i}[{a.size}] = '+'{'+','.join(map(str,a.ravel()))+'};')
    header.append(f'static const int32_t tiny_multiplier{i} = {l["multiplier"]};')
(ROOT/'tiny_weights.h').write_text('\n'.join(header)+'\n')
print(json.dumps(report,ensure_ascii=False,indent=2),flush=True)
