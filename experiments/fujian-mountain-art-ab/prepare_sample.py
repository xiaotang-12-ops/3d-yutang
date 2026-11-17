"""只裁取原福州三角面并做统一单位换算；Landscape采样同一面，不制造高程细节。-codex，2026-10-23"""
import pathlib,json,math,hashlib
import numpy as np
import matplotlib.tri as mtri
from PIL import Image

ROOT=pathlib.Path(__file__).resolve().parent
OUT=ROOT.parents[1]/'artifacts/fujian-mountain-art-ab'
raw=np.load(ROOT/'fuzhou-original.npz'); v=raw['vertices']; tri=raw['indices'].reshape(-1,3)
lon0,lat0=118.2,26.05
cos=math.cos(math.radians(lat0))
# 此框在福州西部山地；尺寸由真实地理范围决定，不按路线改变。-codex，2026-10-23
west,east,south,north=118.82,118.98,26.17,26.33
x0,x1=(south-lat0)*1000,(north-lat0)*1000
y0,y1=(west-lon0)*1000*cos,(east-lon0)*1000*cos
cx,cy=(x0+x1)/2,(y0+y1)/2
factor=11132.0
# 保留接触框的全部原始面，机位取景留边；不对A重三角化。
tpts=v[tri,:3]
keep=(tpts[:,:,0].max(1)>=x0)&(tpts[:,:,0].min(1)<=x1)&(tpts[:,:,1].max(1)>=y0)&(tpts[:,:,1].min(1)<=y1)
ids=np.unique(tri[keep]); remap=np.full(len(v),-1);remap[ids]=np.arange(len(ids)); selected=v[ids].copy()
pos=(selected[:,:3]-np.array([cx,cy,140]))*factor
spec={'boundsWGS84':[west,south,east,north],'originWGS84':[(west+east)/2,(south+north)/2],
      'axes':{'X':'north','Y':'east','Z':'up'},'verticalExaggeration':6,
      'uniformSourceToCm':factor,'sourceOriginUE':[cx,cy,140],
      'groundMeters':[(x1-x0)*factor/100,(y1-y0)*factor/100],
      'sourceMeshSha256':hashlib.sha256((ROOT/'Content/Data/terrain.bin').read_bytes()).hexdigest(),
      'source':'Mapzen Terrarium z9; original terrain.bin triangle surface',
      'vertices':pos.tolist(),'normals':selected[:,3:6].tolist(),'uv':selected[:,6:8].tolist(),
      'triangles':remap[tri[keep]].ravel().tolist()}
(ROOT/'sample.json').write_text(json.dumps(spec,separators=(',',':')),encoding='utf-8')
# Landscape 4×4组件、每组件63×63四边形，共253样点；这里只做格式适配。
n=253; xs=np.linspace(x0,x1,n);ys=np.linspace(y0,y1,n);xx,yy=np.meshgrid(xs,ys,indexing='xy')
interp=mtri.LinearTriInterpolator(mtri.Triangulation(v[:,0],v[:,1],tri),v[:,2])
zz=interp(xx,yy)
assert not np.any(np.ma.getmaskarray(zz)), 'Sample rectangle crosses source city boundary'
world=(np.asarray(zz)-140)*factor
zscale=3200.0
raw_encoding=np.rint(world*128/zscale+32768)
assert raw_encoding.min()>=0 and raw_encoding.max()<=65535, 'R16 range exceeded'
encoded=raw_encoding.astype('<u2')
(ROOT/'sample.r16').write_bytes(encoded.tobytes())
Image.fromarray(encoded).save(ROOT/'sample-height.png')
quant=(encoded.astype(float)-32768)*zscale/128
summary={k:val for k,val in spec.items() if k not in ['vertices','normals','uv','triangles']}
summary.update({'meshVertices':len(ids),'meshTriangles':int(keep.sum()),'landscapeSamples':n,
 'landscapeScale':[float((x1-x0)*factor/(n-1)),float((y1-y0)*factor/(n-1)),zscale],
 'landscapeLocation':[float((x0-cx)*factor),float((y0-cy)*factor),0],
 'landscapeEncoding':'little-endian R16; rows increase east, columns increase north',
 'heightQuantizationMaxCm':float(np.max(abs(quant-world))), 'heightRangeCm':[float(world.min()),float(world.max())]})
(OUT/'sample-region.json').write_text(json.dumps(summary,indent=2),encoding='utf-8')
print(json.dumps(summary,ensure_ascii=False,indent=2))
