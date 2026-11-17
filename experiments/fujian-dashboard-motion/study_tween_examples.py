"""Inspect original author's public GIF examples, not UE compatibility proof."""
from pathlib import Path
import urllib.request, io, json
from PIL import Image,ImageSequence,ImageDraw
out=Path('D:/3d-research/artifacts/fujian-dashboard-motion/resource-study');out.mkdir(parents=True,exist_ok=True)
results=[]
for name in ['bob_demo.gif','outback_demo.gif','elastic_scale_demo.gif','cubic_demo.gif']:
    url='https://raw.githubusercontent.com/jdcook/fresh_cooked_tweens/main/images/'+name
    raw=urllib.request.urlopen(url,timeout=25).read();im=Image.open(io.BytesIO(raw));frames=[f.convert('RGB') for f in ImageSequence.Iterator(im)]
    sheet=Image.new('RGB',(1200,260),'#121827');d=ImageDraw.Draw(sheet)
    for j in range(6):
        index=int((len(frames)-1)*j/5);frame=frames[index];frame.thumbnail((200,220));sheet.paste(frame,(j*200,0));d.text((j*200+4,230),f'{name} frame {index}',fill='white')
    sheet.save(out/(name+'.jpg'),quality=95);results.append({'source':url,'frame_count':len(frames),'kind':'original author demo, only visual reference; no plugin installed'})
(out/'sources.json').write_text(json.dumps(results,indent=2),encoding='utf-8');print(results)
