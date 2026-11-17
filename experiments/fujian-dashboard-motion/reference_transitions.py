from pathlib import Path
import cv2
from PIL import Image,ImageDraw
source='E:/WeChat/xwechat_files/wxid_hd8q3geiohjk22_48c6/msg/video/2026-10/13d71de9ce4ee0203bfe9cc27a202c5f.mp4'
out=Path('D:/3d-research/artifacts/fujian-dashboard-motion')
cap=cv2.VideoCapture(source)
for start in [7.4,15.0,28.4,37.0,47.0,56.6,62.4]:
    sheet=Image.new('RGB',(1600,4*249),'#121827');d=ImageDraw.Draw(sheet)
    for j in range(16):
        t=start+j*.15;cap.set(cv2.CAP_PROP_POS_MSEC,t*1000);ok,bgr=cap.read()
        if not ok: continue
        img=Image.fromarray(cv2.cvtColor(bgr,cv2.COLOR_BGR2RGB)).resize((400,225))
        x=j%4*400;y=j//4*249;sheet.paste(img,(x,y));d.text((x+5,y+228),f'{t:.2f}s',fill='white')
    sheet.save(out/f'transition-{start:.1f}.jpg',quality=95)
cap.release()
