"""Decode the entire user reference; contact sheets retain source timestamps."""
from pathlib import Path
import cv2, json, hashlib
from PIL import Image, ImageDraw

root=Path('D:/3d-research')
out=root/'artifacts/fujian-dashboard-motion'
out.mkdir(parents=True,exist_ok=True)
source=Path('E:/WeChat/xwechat_files/wxid_hd8q3geiohjk22_48c6/msg/video/2026-10/13d71de9ce4ee0203bfe9cc27a202c5f.mp4')
cap=cv2.VideoCapture(str(source))
fps=cap.get(cv2.CAP_PROP_FPS)
frames=[]; changes=[]; previous=None; i=0
while True:
    ok,bgr=cap.read()
    if not ok: break
    small=cv2.resize(bgr,(320,180))
    if previous is not None:
        changes.append((round(i/fps,3),round(float(cv2.absdiff(small,previous).mean()),3)))
    previous=small
    if i%round(fps)==0:
        frames.append((i/fps,Image.fromarray(cv2.cvtColor(bgr,cv2.COLOR_BGR2RGB)).resize((400,225))))
    i+=1
cap.release()
for start in range(0,len(frames),24):
    group=frames[start:start+24]; sheet=Image.new('RGB',(1600,6*249),'#121827'); d=ImageDraw.Draw(sheet)
    for j,(t,img) in enumerate(group):
        x=(j%4)*400;y=(j//4)*249;sheet.paste(img,(x,y));d.text((x+8,y+228),f'{t:06.2f}s',fill='white')
    sheet.save(out/f'reference-{start:02}-{start+len(group)-1:02}.jpg',quality=95)
baseline={}
src=root/'unreal/FujianSandbox58'
for folder in ['Source','Config','Content']:
    for p in (src/folder).rglob('*'):
        if p.is_file(): baseline[str(p.relative_to(src))]=hashlib.sha256(p.read_bytes()).hexdigest()
baseline['FujianSandbox.uproject']=hashlib.sha256((src/'FujianSandbox.uproject').read_bytes()).hexdigest()
(out/'source-baseline.json').write_text(json.dumps(baseline,indent=2),encoding='utf-8')
(out/'reference-decode.json').write_text(json.dumps({'source':str(source),'fps':fps,'decoded_frames':i,'duration_decoded':i/fps,'highest_frame_differences':sorted(changes,key=lambda x:x[1],reverse=True)[:60]},indent=2),encoding='utf-8')
print(json.dumps({'frames':i,'seconds':i/fps,'sheets':len(range(0,len(frames),24)),'baseline_files':len(baseline)}))
