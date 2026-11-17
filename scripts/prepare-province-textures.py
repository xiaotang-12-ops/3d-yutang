"""派生省览地貌/连续周边底图，不写terrain.bin、不生成几何。
优先原缓存，只在构建期补齐同源周边数据；着色是地貌展示而非地物分类。-codex，2026-10-27
"""
from pathlib import Path
import argparse
import hashlib
import json
import math
import io
from concurrent.futures import ThreadPoolExecutor
import numpy as np
import requests
from PIL import Image, ImageDraw
from shapely.geometry import shape, Polygon
from shapely.ops import unary_union

ROOT = Path(__file__).resolve().parents[1]
PROJECT = ROOT / 'unreal/FujianSandbox58'
OUT = PROJECT / 'Content/Province'
META = json.loads((PROJECT / 'Content/Data/terrain.json').read_text('utf-8'))
SOURCE = json.loads((PROJECT / 'Content/Data/terrain-source.json').read_text('utf-8'))
BOUNDS = np.asarray(META['bounds'], dtype=np.float64)
SIZE = 4096
COS = math.cos(math.radians(26.05))
CONTEXT_BOUNDS = np.asarray([-650., -470., 650., 720.])
CACHE = OUT / 'cache'


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--context-only',action='store_true',help='只生成省外背景，校验并保留省内纹理')
    context_only=parser.parse_args().context_only
    # 用户认可R7省内；周边调校必须能够完全避开省内源图写入。-codex，2026-10-27
    locked={}
    if context_only:
        previous=json.loads((OUT/'province-source.json').read_text('utf-8'))
        locked={name:previous['outputs'][name] for name in ['province-surface.png','province-borders.png']}
        for name,expected in locked.items():
            if digest(OUT/name)!=expected:raise RuntimeError(f'Locked province texture changed: {name}')
    original_mesh = digest(PROJECT / 'Content/Data/terrain.bin')
    CACHE.mkdir(parents=True, exist_ok=True)
    def tile_xy(x,y):
        lon,lat=x/(100*COS)+118.2,y/100+26.05
        return (lon+180)/360*512,(1-np.arcsinh(np.tan(np.radians(lat)))/math.pi)/2*512
    tx0,ty1=tile_xy(CONTEXT_BOUNDS[0]-12,CONTEXT_BOUNDS[1]-12)
    tx1,ty0=tile_xy(CONTEXT_BOUNDS[2]+12,CONTEXT_BOUNDS[3]+12)
    tiles=[(x,y) for y in range(math.floor(ty0),math.floor(ty1)+1) for x in range(math.floor(tx0),math.floor(tx1)+1)]
    def cached_tile(pair):
        x,y=pair
        original=ROOT/f'shared/terrain/tiles/9-{x}-{y}.png'
        path=original if original.exists() else CACHE/f'9-{x}-{y}.png'
        if not path.exists():
            response=requests.get(f'https://s3.amazonaws.com/elevation-tiles-prod/terrarium/9/{x}/{y}.png',timeout=60)
            response.raise_for_status()
            check=Image.open(io.BytesIO(response.content))
            if check.size!=(256,256):raise ValueError(f'Invalid DEM response {pair}')
            path.write_bytes(response.content)
        return pair,path
    with ThreadPoolExecutor(max_workers=8) as pool:
        tile_paths=dict(pool.map(cached_tile,tiles))
    print(f'DEM cached: {len(tiles)} tiles',flush=True)
    x0, y0 = min(t[0] for t in tiles), min(t[1] for t in tiles)
    x1, y1 = max(t[0] for t in tiles), max(t[1] for t in tiles)
    mosaic = np.full(((y1-y0+1)*256, (x1-x0+1)*256), np.nan, dtype=np.float32)
    inputs = {}
    for x, y in tiles:
        path = tile_paths[x,y]
        a = np.asarray(Image.open(path).convert('RGB'), dtype=np.float32)
        if a.shape != (256, 256, 3):
            raise ValueError(f'Invalid source tile: {path}')
        mosaic[(y-y0)*256:(y-y0+1)*256, (x-x0)*256:(x-x0+1)*256] = a[..., 0]*256+a[..., 1]+a[..., 2]/256-32768
        inputs[str(path.relative_to(ROOT))] = digest(path)
    if not np.isfinite(mosaic).all():
        raise ValueError('Incomplete cached DEM coverage; no zero filling allowed')

    def sample(x, y):
        lon, lat = x/(100*COS)+118.2, y/100+26.05
        px = ((lon+180)/360*512-x0)*256-.5
        py = ((1-np.arcsinh(np.tan(np.radians(lat)))/math.pi)/2*512-y0)*256-.5
        if px.min()<0 or py.min()<0 or px.max()>=mosaic.shape[1]-1 or py.max()>=mosaic.shape[0]-1:
            raise ValueError('Sampling exceeds the actual cached DEM footprint')
        ix, iy = px.astype(np.int32), py.astype(np.int32)
        dx, dy = px-ix, py-iy
        return mosaic[iy, ix]*(1-dx)*(1-dy)+mosaic[iy, ix+1]*dx*(1-dy)+mosaic[iy+1, ix]*(1-dx)*dy+mosaic[iy+1, ix+1]*dx*dy

    imagery_path = ROOT / 'shared/terrain/imagery/bluemarble-71523389dcbd8682f2eb.png'
    # 同原生成器的完整bounds映射；不以高程0m猜水域，海陆颜色来自既有自然色。
    # 影像约500m/2004年，只承担上下文，不冒充当前高分辨率遥感。-codex，2026-10-27
    natural = np.asarray(Image.open(imagery_path).convert('RGB').resize((SIZE,SIZE), Image.Resampling.BICUBIC), dtype=np.float32)
    # 周边单独扩大真实采样范围，避免将省域纹理Clamp拉伸或用矩形黑边遮掩缺图。
    bbox=[CONTEXT_BOUNDS[0]/(100*COS)+118.2,CONTEXT_BOUNDS[1]/100+26.05,
          CONTEXT_BOUNDS[2]/(100*COS)+118.2,CONTEXT_BOUNDS[3]/100+26.05]
    params={'SERVICE':'WMS','REQUEST':'GetMap','VERSION':'1.1.1','LAYERS':'BlueMarble_NextGeneration',
            'STYLES':'','SRS':'EPSG:4326','BBOX':','.join(map(str,bbox)),
            'WIDTH':SIZE,'HEIGHT':SIZE,'FORMAT':'image/png'}
    url=requests.Request('GET','https://gibs.earthdata.nasa.gov/wms/epsg4326/best/wms.cgi',params=params).prepare().url
    photo_path=CACHE/('bluemarble-'+hashlib.sha256(url.encode()).hexdigest()[:20]+'.png')
    if not photo_path.exists():
        response=requests.get(url,timeout=120);response.raise_for_status()
        check=Image.open(io.BytesIO(response.content))
        if check.size!=(SIZE,SIZE):raise ValueError('Invalid context imagery response')
        photo_path.write_bytes(response.content)
    context_photo=np.asarray(Image.open(photo_path).convert('RGB'),dtype=np.float32)
    if context_photo.shape!=(SIZE,SIZE,3):raise ValueError('Invalid cached context imagery')

    def render(bounds,photograph,is_context=False):
        result=np.zeros((SIZE,SIZE,3),dtype=np.uint8)
        x=np.linspace(bounds[0],bounds[2],SIZE)[None,:]
        ys=np.linspace(bounds[3],bounds[1],SIZE)
        for start in range(0,SIZE,96):
            end=min(start+96,SIZE);y=ys[start:end,None]
            raw=sample(x,y);e=np.maximum(0,raw);photo=photograph[start:end]
            # 三个真实空间尺度的山体受光，弱化逐像素砂点、突出连续山系；不改变网格。
            # 光向西北，6倍展示坡度与原网格一致。-codex，2026-10-27
            key=np.zeros_like(e);slope=None
            scales=[(.24,.04),(1.2,.26),(3.5,.70)] if is_context else [(.24,.10),(1.2,.30),(3.5,.60)]
            for distance,weight in scales:
                gx=(sample(x+distance,y)-sample(x-distance,y))/(2*distance*1113.2)
                gy=(sample(x,y+distance)-sample(x,y-distance))/(2*distance*1113.2)
                n=np.sqrt(1+36*(gx*gx+gy*gy))
                key+=weight*np.clip((.50+3.3*gx-3.9*gy)/n,0,1)
                if distance==1.2:slope=np.sqrt(gx*gx+gy*gy)
            neighbors=[sample(x+2.,y),sample(x-2.,y),sample(x,y+2.),sample(x,y-2.)]
            neighborhood=sum(neighbors)*.25
            local=e-neighborhood
            occlusion=np.clip(1+local/850,.68,1.08)
            # 远处山脊的遮蔽只来自DEM的实际高程，不用噪声绘制假山脉。
            visibility=np.ones_like(e)
            for distance in [.6,1.4,2.8,5.6]:
                blocker=sample(x-distance*.646,y+distance*.763)-e
                horizon=blocker*6-distance*1113.2*.58
                visibility=np.minimum(visibility,np.clip(1-horizon/650,.22,1))
            light=key*visibility
            high=np.clip(e/1600,0,1)[...,None]
            base=np.asarray([42.,61.,52.])*(1-high)+np.asarray([117.,123.,108.])*high
            rock=(np.clip((slope-.07)/.35,0,.55)*np.clip(.65+local/650,.15,1.15))[...,None]
            base=base*(1-rock)+np.asarray([167.,169.,151.])*rock
            # 低海拔且平缓才进入青灰低地色阶；不是把所有低处涂成海或城市。
            plain=(np.exp(-e/180)*np.exp(-slope/.10)*.28)[...,None]
            base=base*(1-plain)+np.asarray([88.,107.,99.])*plain
            luminance=photo@np.asarray([.2126,.7152,.0722],dtype=np.float32)
            # 历史影像中真实的明亮谷地/低地纹理参与色彩组织，打破全省同绿。
            # 这是连续反射率风格化，不把亮像素宣称为现状城镇或裸岩。-codex，2026-10-27
            open_ground=np.clip((luminance-43)/74,0,1)[...,None]
            mix=open_ground*.48
            base=base*(.70+.30*open_ground)*(1-mix)+np.asarray([150.,154.,140.])*mix
            natural_detail=np.clip((luminance-65)/120,-.3,.4)
            illumination=np.asarray([.29,.38,.46])+light[...,None]*np.asarray([1.26,1.16,.99])
            rgb=base*illumination*occlusion[...,None]*(1+natural_detail[...,None]*.25)
            if is_context:
                # 影像的蓝色倾向是连续色彩混合，不声称是测绘海陆分类；无高程0m硬切。
                water=np.clip((photo[...,2]-photo[...,0]-2)/26,0,1)
                water=np.maximum(water,np.clip((24-luminance)/20,0,.75))
                # 周边原先抬暗部、压亮脊而显成蓝黑碎纹；恢复灰冷亮坡和暗谷，省内完全不动。
                # 远景按地理北向渐入低对比空气色，不添加虚构河流或新地形。-codex，2026-10-27
                land=rgb*np.asarray([.50,.60,.72])+np.asarray([2.,7.,13.])
                distance_haze=np.clip((y-180)/400,0,.28)
                land=land*(1-distance_haze[...,None])+np.asarray([15.,27.,39.])*distance_haze[...,None]
                depth=np.maximum(0,-raw)
                shallow=np.exp(-depth/100)
                sea=np.asarray([7.,26.,45.])+photo*np.asarray([.05,.10,.14])+shallow[...,None]*np.asarray([3.,21.,30.])
                sea+=np.exp(-depth/28)[...,None]*np.asarray([2.,13.,18.])
                # 海中细线来自同源ETOPO实际等深值，仅作历史地理展示，非航海测深。
                # 浅海25m、深海100m间隔，避免250m间隔在海峡中几乎不可见。-codex，2026-10-27
                shelf_line=np.exp(-(np.minimum(depth%25,25-depth%25)/.9)**2)
                deep_line=np.exp(-(np.minimum(depth%100,100-depth%100)/3.)**2)
                deep_mix=np.clip((depth-175)/50,0,1)
                contour=(shelf_line*(1-deep_mix)+deep_line*deep_mix)*(depth>8)*water
                sea+=contour[...,None]*np.asarray([2.,10.,16.])
                # 只在影像蓝色水域且邻近真实正高程岸地处柔化近岸颜色。
                # 不沿全部省界铺光圈，西侧行政边界不会被误画成海岸。-codex，2026-10-27
                coastal=np.clip((np.maximum.reduce(neighbors)-e)/160,0,1)*np.exp(-depth/200)*water
                sea+=coastal[...,None]*np.asarray([5.,35.,48.])
                rgb=land*(1-water[...,None])+sea*water[...,None]
            result[start:end]=np.uint8(np.clip(rgb,0,255))
        return result
    surface=None if context_only else render(BOUNDS,natural)
    context=render(CONTEXT_BOUNDS,context_photo,True)
    OUT.mkdir(exist_ok=True)
    if not context_only:Image.fromarray(surface).save(OUT/'province-surface.png')
    Image.fromarray(context).save(OUT/'province-context.png')
    # 同一真实行政轮廓只重采样成更高分辨率描边；省界约2px、内部界约1px的远景权重。
    # 不修改GeoJSON/网格边界，也不从效果图描摹新轮廓。-codex，2026-10-27
    geo=json.loads((ROOT/'shared/fujian.geojson').read_text('utf-8'))
    shapes=[shape(f['geometry']) for f in geo['features']]
    def border_mask(geometries,width):
        mask=Image.new('L',(SIZE*2,SIZE*2));draw=ImageDraw.Draw(mask)
        for geometry in geometries:
            polygons=[geometry] if isinstance(geometry,Polygon) else list(geometry.geoms)
            for polygon in polygons:
                for ring in [polygon.exterior,*polygon.interiors]:
                    a=np.asarray(ring.coords)
                    x=(a[:,0]-118.2)*100*COS;y=(a[:,1]-26.05)*100
                    pts=list(zip((x-BOUNDS[0])/(BOUNDS[2]-BOUNDS[0])*(SIZE-1)*2,(BOUNDS[3]-y)/(BOUNDS[3]-BOUNDS[1])*(SIZE-1)*2))
                    draw.line(pts,fill=255,width=round(width*2),joint='curve')
        return mask.resize((SIZE,SIZE),Image.Resampling.LANCZOS)
    if not context_only:
        Image.merge('RGB',(border_mask(shapes,2.2),border_mask([unary_union(shapes)],7.),Image.new('L',(SIZE,SIZE)))).save(OUT/'province-borders.png')
    for name,expected in locked.items():
        if digest(OUT/name)!=expected:raise RuntimeError(f'Locked province texture changed: {name}')
    manifest={'boundsShared':BOUNDS.tolist(),'contextBoundsShared':CONTEXT_BOUNDS.tolist(),'worldAxes':'UE X=north, Y=east; 10 world units/shared unit','textureSize':SIZE,
        'meshUnchanged':original_mesh==digest(PROJECT/'Content/Data/terrain.bin'),'meshSha256':original_mesh,
        'credit':SOURCE['credit']+'; NASA Earth Observatory / GIBS Blue Marble Next Generation, 2004, 500m.',
        'interpretation':'Cartographic display shading derived from cached DEM and historical imagery, not new terrain, current imagery or surveyed land-cover classification.',
        'imagerySha256':digest(imagery_path),'inputs':inputs,
        'contextImagery':{'url':url,'sha256':digest(photo_path),'bboxLonLat':bbox},
        'boundarySourceSha256':digest(ROOT/'shared/fujian.geojson'),
        'outputs':{p.name:digest(p) for p in [OUT/'province-surface.png',OUT/'province-context.png',OUT/'province-borders.png']}}
    # 保留已导入的氛围源及生成提示，不把艺术薄雾标注为地理/气象数据。-codex，2026-10-27
    existing=json.loads((OUT/'province-source.json').read_text('utf-8')) if (OUT/'province-source.json').exists() else {}
    if (OUT/'province-atmosphere.png').exists():
        manifest['outputs']['province-atmosphere.png']=digest(OUT/'province-atmosphere.png')
        manifest['atmosphereSource']=existing.get('atmosphereSource',{'type':'art overlay, not meteorological data'})
    (OUT/'province-source.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding='utf-8')
    if not manifest['meshUnchanged']:
        raise RuntimeError('Source geometry changed during texture generation')
    print(json.dumps({'generated':['province-context.png'] if context_only else ['province-surface.png','province-context.png','province-borders.png'],'size':SIZE,'meshUnchanged':True,'provinceTexturesPreserved':bool(context_only)}))


if __name__=='__main__':
    main()
