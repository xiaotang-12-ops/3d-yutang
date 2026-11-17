"""福建公开高程离线处理：统一采样后裁切九市、生成原生网格与地表着色。
支持高程着色或离线 Blue Marble 自然色；倍率和资料来源写入元数据。-codex，2026-10-22
"""
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import hashlib
import argparse
import io
import json
import math
import struct
import time
import requests
import numpy as np
from PIL import Image, ImageDraw
from shapely.geometry import shape, box, Polygon, MultiPolygon
from shapely.ops import transform, triangulate, unary_union
from shapely.prepared import prep

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description='离线生成福建地形，显式选择目标工程。')
parser.add_argument('--project-dir', type=Path, default=ROOT / 'unreal/FujianSandbox')
parser.add_argument('--demo', action='store_true', help='用数学曲面生成开发用地形，不下载任何瓦片')
parser.add_argument('--surface-source', choices=['elevation','bluemarble'], default='elevation')
parser.add_argument('--mesh-step', type=float, default=.9,
                    help='投影平面网格步长：默认0.9约1km，0.45约500m；不改变源DEM精度。')
parser.add_argument('--shape-weight', type=float, default=0.,
                    help='受限山形残差权重，默认0保持原几何；本批允许0至0.35，不改变DEM/网格密度/颜色。')
args = parser.parse_args()
if args.demo and args.surface_source != 'elevation':
    parser.error('--demo 只使用离线数学曲面，不支持遥感影像')
if not math.isfinite(args.mesh_step) or args.mesh_step <= 0:
    parser.error('--mesh-step 必须是有限正数')
if not math.isfinite(args.shape_weight) or not 0 <= args.shape_weight <= .35:
    parser.error('--shape-weight 必须是0至0.35的有限数；扩大试验范围须另行确认')
OUT = args.project_dir.resolve() / 'Content/Data'
CACHE = ROOT / 'shared/terrain/tiles'
ZOOM = 9
# 默认保留旧网格；仅显式参数改变几何密度，法线/色图仍复用原算法。-codex，2026-10-22
STEP = args.mesh_step
ORIGIN_LON, ORIGIN_LAT, SCALE = 118.2, 26.05, 100
COS = math.cos(math.radians(ORIGIN_LAT))
# 展示倍率用于保留省市尺度的山体层次，不代表测绘比例；运行时以元数据标示。-codex，2026-10-22
EXAGGERATION = 6
HEIGHT_SCALE = 1000 / 111320 * EXAGGERATION
BASE = 140.
OUT.mkdir(parents=True, exist_ok=True)
CACHE.mkdir(parents=True, exist_ok=True)
geo = json.loads((ROOT / 'shared/fujian.geojson').read_text('utf-8'))
regions = [transform(lambda x, y, z=None: ((np.asarray(x)-ORIGIN_LON)*SCALE*COS, (np.asarray(y)-ORIGIN_LAT)*SCALE), shape(f['geometry'])) for f in geo['features']]
province = unary_union(regions)
west, south, east, north = province.bounds
pad = 65
bounds = [west-pad, south-pad, east+pad, north+pad]

def tile_xy(lon, lat):
    return ((np.asarray(lon)+180)/360*(2**ZOOM), (1-np.arcsinh(np.tan(np.radians(lat)))/math.pi)/2*(2**ZOOM))

left, top = tile_xy(bounds[0]/(SCALE*COS)+ORIGIN_LON, bounds[3]/SCALE+ORIGIN_LAT)
right, bottom = tile_xy(bounds[2]/(SCALE*COS)+ORIGIN_LON, bounds[1]/SCALE+ORIGIN_LAT)
tx0, ty0, tx1, ty1 = math.floor(left), math.floor(top), math.floor(right), math.floor(bottom)
tiles = [(x,y) for y in range(ty0,ty1+1) for x in range(tx0,tx1+1)]

def download(item):
    x,y=item
    if args.demo:
        row,col=np.indices((256,256))
        return x,y,150+90*np.sin((256*x+col)/160)*np.cos((256*y+row)/130)
    path=CACHE/f'{ZOOM}-{x}-{y}.png'
    if not path.exists():
        url=f'https://s3.amazonaws.com/elevation-tiles-prod/terrarium/{ZOOM}/{x}/{y}.png'
        response=requests.get(url, timeout=45)
        response.raise_for_status()
        # 校验后写入，避免中断后缓存半张图片被误用。-codex，2026-10-18
        image=Image.open(io.BytesIO(response.content)).convert('RGB')
        if image.size != (256,256):
            raise ValueError(f'Invalid tile {item}: {image.size}')
        path.write_bytes(response.content)
    a=np.asarray(Image.open(path).convert('RGB'), dtype=np.float32)
    if a.shape != (256,256,3):
        raise ValueError(f'Invalid cached tile {item}')
    return x,y,a[:,:,0]*256+a[:,:,1]+a[:,:,2]/256-32768

print(f'Downloading/reading {len(tiles)} elevation tiles, zoom {ZOOM}',flush=True)
mosaic=np.zeros(((ty1-ty0+1)*256,(tx1-tx0+1)*256),dtype=np.float32)
with ThreadPoolExecutor(max_workers=4) as pool:
    for x,y,a in pool.map(download, tiles):
        mosaic[(y-ty0)*256:(y-ty0+1)*256,(x-tx0)*256:(x-tx0+1)*256]=a

# 只对全省拼接DEM求同一个小尺度残差，避免分市滤波产生接缝；不补造高程细节。
# sigma/截幅/低地门限是本批固定保护，唯一美术变量为shape-weight。-codex，2026-10-23
shape_spec={'weight':args.shape_weight,'sigmaPixels':.65,'radiusPixels':2,
            'residualClipMeters':40.,'gateMeters':[0.,80.],
            'interpretation':'Bounded display approximation, not survey elevation or water classification.'}
shape_mosaic=None
if args.shape_weight:
    offsets=np.arange(-2,3,dtype=np.float64)
    kernel=np.exp(-.5*(offsets/.65)**2)
    kernel/=kernel.sum()
    shape_mosaic=mosaic.astype(np.float64)
    for axis in (1,0):
        padding=[(0,0),(0,0)]; padding[axis]=(2,2)
        padded=np.pad(shape_mosaic,padding,mode='edge')
        filtered=np.zeros_like(shape_mosaic)
        for offset,weight in enumerate(kernel):
            section=[slice(None),slice(None)]
            section[axis]=slice(offset,offset+shape_mosaic.shape[axis])
            filtered+=weight*padded[tuple(section)]
        shape_mosaic=filtered

def sample_elevation(x,y,grid):
    tx,ty=tile_xy(np.asarray(x)/(SCALE*COS)+ORIGIN_LON,np.asarray(y)/SCALE+ORIGIN_LAT)
    px=np.clip((tx-tx0)*256-.5,0,grid.shape[1]-1.001)
    py=np.clip((ty-ty0)*256-.5,0,grid.shape[0]-1.001)
    ix,iy=px.astype(np.int32),py.astype(np.int32)
    dx,dy=px-ix,py-iy
    return (grid[iy,ix]*(1-dx)*(1-dy)+grid[iy,ix+1]*dx*(1-dy)+grid[iy+1,ix]*(1-dx)*dy+grid[iy+1,ix+1]*dx*dy)

def elevation(x,y):
    # 色图仍使用原DEM，几何试验不叠加颜色变量。-codex，2026-10-23
    return sample_elevation(x,y,mosaic)

def shaped_elevation(x,y):
    h=np.maximum(0,elevation(x,y))
    if not args.shape_weight:
        return h
    hb=np.maximum(0,sample_elevation(x,y,shape_mosaic))
    t=np.clip(h/80.,0,1)
    gate=t*t*(3-2*t)
    return h+args.shape_weight*gate*np.clip(hb-h,-40.,40.)

def height(x,y):
    return BASE+shaped_elevation(x,y)*HEIGHT_SCALE

def normal(x,y):
    d=.35
    gx=(height(x+d,y)-height(x-d,y))/(20*d)
    gy=(height(x,y+d)-height(x,y-d))/(20*d)
    # 原生 X=北、Y=东，所有城市用同一个高度函数求导，避免分市明暗缝。
    n=np.stack([-gy,-gx,np.ones_like(gx)],axis=-1)
    return n/np.linalg.norm(n,axis=-1,keepdims=True)

def polygons(g):
    if isinstance(g,Polygon): return [g]
    if hasattr(g,'geoms'): return [p for p in g.geoms if isinstance(p,Polygon)]
    return []

def rings_of(g):
    result=[]
    for polygon in polygons(g):
        for ring in [polygon.exterior,*polygon.interiors]:
            coords=np.asarray(ring.coords)
            # 细分长直线，使边界贴地而非从山体中穿过。
            samples=[]
            for a,b in zip(coords[:-1],coords[1:]):
                count=max(1,int(np.ceil(np.linalg.norm(b-a)/.7)))
                samples.extend(a+(b-a)*t/count for t in range(count))
            samples.append(coords[-1])
            a=np.asarray(samples)
            result.append(np.column_stack([a[:,1]*10,a[:,0]*10,height(a[:,0],a[:,1])+5]).round(3).tolist())
    return result

def make_mesh(region, step):
    region_prep=prep(region)
    minx,miny,maxx,maxy=region.bounds
    vertices=[]; indices=[]; cache={}
    def vertex(x,y):
        key=(round(x,7),round(y,7))
        if key not in cache:
            cache[key]=len(vertices)
            vertices.append(key)
        return cache[key]
    for iy in range(math.floor(miny/step),math.ceil(maxy/step)):
        for ix in range(math.floor(minx/step),math.ceil(maxx/step)):
            x,y=ix*step,iy*step
            cell=box(x,y,x+step,y+step)
            if not region_prep.intersects(cell): continue
            if region_prep.covers(cell):
                points=[(x,y),(x+step,y),(x+step,y+step),(x,y+step)]
                ids=[vertex(*p) for p in points]
                indices.extend([ids[0],ids[1],ids[2],ids[0],ids[2],ids[3]])
            else:
                for p in polygons(region.intersection(cell)):
                    for tri in triangulate(p):
                        if not p.covers(tri): continue
                        pts=list(tri.exterior.coords)[:3]
                        # Shapely 三角面统一为 shared 东北平面的逆时针；交换 XY 后符合 UE 正面。
                        if (pts[1][0]-pts[0][0])*(pts[2][1]-pts[0][1])-(pts[1][1]-pts[0][1])*(pts[2][0]-pts[0][0])<0:
                            pts[1],pts[2]=pts[2],pts[1]
                        indices.extend(vertex(*q) for q in pts)
    a=np.asarray(vertices,dtype=np.float64)
    x,y=a[:,0],a[:,1]
    positions=np.column_stack([y*10,x*10,height(x,y)])
    normals=normal(x,y)
    uv=np.column_stack([(x-bounds[0])/(bounds[2]-bounds[0]),(bounds[3]-y)/(bounds[3]-bounds[1])])
    records=np.column_stack([positions,normals,uv]).astype('<f4')
    return records,np.asarray(indices,dtype='<u4')

metadata={'version':1,'zoom':ZOOM,'stepShared':STEP,'verticalExaggeration':EXAGGERATION,'heightScale':HEIGHT_SCALE,'base':BASE,'bounds':bounds,'tiles':len(tiles),'cities':[],'provinceRings':rings_of(province)}
metadata['shape']=shape_spec
binary=OUT/'terrain.bin'
with binary.open('wb') as stream:
    stream.write(struct.pack('<III',0x52544A46,1,len(regions)))
    for feature,region in sorted(zip(geo['features'],regions),key=lambda p:p[0]['properties']['adcode']):
        code=feature['properties']['adcode']
        vertices,indices=make_mesh(region,STEP)
        stream.write(struct.pack('<III',code,len(vertices),len(indices)))
        stream.write(vertices.tobytes()); stream.write(indices.tobytes())
        center=feature['properties'].get('centroid',feature['properties']['center'])
        cx,cy=(center[0]-ORIGIN_LON)*SCALE*COS,(center[1]-ORIGIN_LAT)*SCALE
        item={'code':code,'vertices':len(vertices),'triangles':len(indices)//3,'rings':rings_of(region),'centerHeight':float(height(np.asarray(cx),np.asarray(cy)))}
        metadata['cities'].append(item)
        print(f'{code}: {len(vertices)} vertices, {len(indices)//3} triangles',flush=True)

# 表面只依据高程、坡度与真实 DEM 的沟谷明暗着色；没有道路、农田或卫星照片来源。
size=2048
xx,yy=np.meshgrid(np.linspace(bounds[0],bounds[2],size),np.linspace(bounds[3],bounds[1],size))
e=np.maximum(0,elevation(xx,yy))
gx=(elevation(xx+.12,yy)-elevation(xx-.12,yy))/267
gy=(elevation(xx,yy+.12)-elevation(xx,yy-.12))/267
shade=np.clip((.58-.52*gx+.64*gy)/np.sqrt(1+gx*gx+gy*gy),.12,1.)
t=np.clip(e/1600,0,1)[...,None]
# 色阶与岩色权重服务视觉展示，弱化碎纹与灰岩感，不代表实际植被或岩性分类。-codex，2026-10-22
low=np.array([30,66,55]); high=np.array([101,119,86])
base_color=low*(1-t)+high*t
shade_gain=.62+shade*.65
rock=np.clip((np.sqrt(gx*gx+gy*gy)-.2)*1.0,0,.20)[...,None]
if args.surface_source=='elevation':
    from shapely import intersects_xy
    # 按实际省域地形覆盖求均值，0m低地仍算陆地；不以高程猜水域、不逐市归一化。-codex，2026-10-23
    land_mask=intersects_xy(province,xx,yy)
    if not np.any(land_mask):
        raise ValueError('Elevation color has no covered terrain samples')
    land_base=base_color[land_mask]
    land_weight=(1-rock)[land_mask]
    land_rock=(np.array([126,132,111])*rock)[land_mask]
    land_gain=shade_gain[land_mask]
    def land_luminance(gain):
        # 与最终8位sRGB纹理一致；在线性光中守住Rec.709平均亮度，避免把提亮误认成细节。-codex，2026-10-23
        encoded=np.uint8(np.clip((land_base*gain[:,None])*land_weight+land_rock,0,255))/255.
        linear=np.where(encoded<=.04045,encoded/12.92,((encoded+.055)/1.055)**2.4)
        return float(np.mean(linear@np.array([.2126,.7152,.0722])))
    target_luminance=land_luminance(land_gain)
    low_gain,high_gain=float(land_gain.min()),float(land_gain.max())
    # 只有对比系数.45是美术变量；中性增益由亮度约束二分求解，保留岩色及颜色端点。-codex，2026-10-23
    for _ in range(28):
        neutral_gain=(low_gain+high_gain)*.5
        if land_luminance(neutral_gain+.45*(land_gain-neutral_gain))<target_luminance:
            low_gain=neutral_gain
        else:
            high_gain=neutral_gain
    neutral_gain=(low_gain+high_gain)*.5
    shade_gain=neutral_gain+.45*(shade_gain-neutral_gain)
rgb=(base_color*shade_gain[...,None])*(1-rock)+np.array([126,132,111])*rock
# 低海拔沿用连续地形色阶；高程阈值不能识别真实水域，禁止据此硬涂水色。-codex，2026-10-22
surface_source={'type':'elevation','description':'Terrain-derived color; not satellite imagery.'}
if args.surface_source=='bluemarble':
    # 静态公共自然色影像仅在构建期使用；成品不依赖网络。2004 年 500m 资料不代表现状或测绘。
    bbox=[bounds[0]/(SCALE*COS)+ORIGIN_LON,bounds[1]/SCALE+ORIGIN_LAT,
          bounds[2]/(SCALE*COS)+ORIGIN_LON,bounds[3]/SCALE+ORIGIN_LAT]
    params={'SERVICE':'WMS','REQUEST':'GetMap','VERSION':'1.1.1','LAYERS':'BlueMarble_NextGeneration',
            'STYLES':'','SRS':'EPSG:4326','BBOX':','.join(map(str,bbox)),
            'WIDTH':size,'HEIGHT':size,'FORMAT':'image/png'}
    service='https://gibs.earthdata.nasa.gov/wms/epsg4326/best/wms.cgi'
    request_url=requests.Request('GET',service,params=params).prepare().url
    key=hashlib.sha256(request_url.encode()).hexdigest()[:20]
    imagery_cache=ROOT/'shared/terrain/imagery'
    imagery_cache.mkdir(parents=True,exist_ok=True)
    cached=imagery_cache/f'bluemarble-{key}.png'
    if not cached.exists():
        response=requests.get(service,params=params,timeout=90)
        response.raise_for_status()
        check=Image.open(io.BytesIO(response.content)).convert('RGB')
        if check.size!=(size,size):raise ValueError(f'Invalid Blue Marble image: {check.size}')
        cached.write_bytes(response.content)
    natural=Image.open(cached).convert('RGB')
    if natural.size!=(size,size):raise ValueError('Invalid cached Blue Marble dimensions')
    natural=np.asarray(natural,dtype=np.float32)
    # 温和提亮和沟谷阴影保留自然地表颜色，避免把纹理再次烘成硬质碎石。
    rgb=natural*np.array([1.18,1.20,1.16])*(.82+.30*shade[...,None])
    surface_source={'type':'BlueMarble_NextGeneration','provider':'NASA Earth Observatory / GIBS',
        'collectionYear':2004,'collectionResolutionMeters':500,'month':'unspecified static GIBS layer',
        'url':request_url,'bboxLonLat':bbox,'imageSize':size,'sourceSha256':hashlib.sha256(cached.read_bytes()).hexdigest(),
        'description':'Historical natural-color reference, clipped and shaded; not current imagery or a survey.'}
Image.fromarray(np.uint8(np.clip(rgb,0,255))).save(OUT/'terrain-color.png')
# 边界与地表使用同一 UV：在材质上着色，避免独立 DEM 折线穿入实际三角面。
# 重复城市线写入同一遮罩取并集，不累计亮度；4× 超采样后保留纹理 mip 抗锯齿。
# R=城市，G=省轮廓。2K 适用于当前省/市观察，更近镜头需提高分辨率。-codex，2026-10-19
supersample=4
def boundary_mask(geometries,width):
    mask=Image.new('L',(size*supersample,size*supersample))
    draw=ImageDraw.Draw(mask)
    for geometry in geometries:
        for polygon in polygons(geometry):
            for ring in [polygon.exterior,*polygon.interiors]:
                points=[((x-bounds[0])/(bounds[2]-bounds[0])*(size-1)*supersample,
                         (bounds[3]-y)/(bounds[3]-bounds[1])*(size-1)*supersample) for x,y in ring.coords]
                draw.line(points,fill=255,width=round(width*supersample),joint='curve')
    return mask.resize((size,size),Image.Resampling.LANCZOS)
city_mask=boundary_mask(regions,.9)
province_mask=boundary_mask([province],1.35)
Image.merge('RGB',(city_mask,province_mask,Image.new('L',(size,size)))).save(OUT/'terrain-borders.png')
metadata['borderTexture']={'file':'terrain-borders.png','size':size,'channels':{'R':'cities','G':'province'},'supersample':supersample}
metadata['meshSha256']=hashlib.sha256(binary.read_bytes()).hexdigest()
metadata['meshBytes']=binary.stat().st_size
metadata['surfaceSource']=surface_source
(OUT/'terrain.json').write_text(json.dumps(metadata,separators=(',',':')),'utf-8')
source={'accessedAt':time.strftime('%Y-%m-%dT%H:%M:%S%z'),'service':'Mapzen Terrain Tiles on AWS','url':'https://registry.opendata.aws/terrain-tiles/','format':'https://github.com/tilezen/joerd/blob/master/docs/formats.md','attribution':'https://github.com/tilezen/joerd/blob/master/docs/attribution.md','credit':'Mapzen; SRTM and GMTED2010 terrain data courtesy of the U.S. Geological Survey; ETOPO1: DOC/NOAA/NESDIS/NCEI','modifications':f'Reprojected, resampled, clipped to existing demonstration boundaries, {EXAGGERATION}x vertical exaggeration, terrain-derived color and hillshade; not satellite imagery or a survey product.','zoom':ZOOM,'tiles':tiles,'meshSha256':metadata['meshSha256']}
source['surfaceSource']=surface_source
if args.demo:
    source={'service':'Synthetic mathematical terrain','url':'offline','meshSha256':metadata['meshSha256']}
source['shape']=shape_spec
if args.surface_source=='bluemarble':
    source['modifications']=f'Reprojected, resampled, clipped, {EXAGGERATION}x vertical exaggeration; NASA Blue Marble historical natural color with mild hillshade. Not current imagery or a survey product.'
(OUT/'terrain-source.json').write_text(json.dumps(source,indent=2),'utf-8')
print(json.dumps({'meshBytes':metadata['meshBytes'],'vertices':sum(c['vertices'] for c in metadata['cities']),'triangles':sum(c['triangles'] for c in metadata['cities']),'source':source['url']}),flush=True)
