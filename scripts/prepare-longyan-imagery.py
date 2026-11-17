"""龙岩真实遥感离线贴图：匿名读取 Sentinel-2 COG，逐像素屏蔽云与缺测后拼接。
仅写 longyan 专属产物；输出为北朝上的线性经纬图，绝不以旧底图补缺测。-codex，2026-10-28
局部依赖：python -m pip install --target .tools/geo-python rasterio==1.5.2
"""
from pathlib import Path
from concurrent.futures import ThreadPoolExecutor
import argparse
import math
import shutil
import tempfile
import hashlib
import json
import os
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / '.tools/geo-python'))
import numpy as np
import requests
from PIL import Image, ImageFilter
import rasterio
from rasterio.vrt import WarpedVRT
from rasterio.enums import Resampling
from rasterio.transform import from_bounds
from rasterio.windows import Window

OUT = ROOT / 'unreal/FujianSandbox58/Content/Province'
CACHE = OUT / 'longyan-cache'
PROOF = ROOT / 'artifacts/pond-v7-a-proof/longyan-detail/source'
BBOX = [115.65, 24.15, 118.05, 26.25]
SHARED = [-229.094844045467, -190., -13.476167296793, 20.]
SIZE = 8192
GRID_VERSION = 2
CRS = 'EPSG:4326'
FROZEN = ROOT / 'artifacts/farm-orbit-20261030/before-08-texture/unreal/FujianSandbox58/Content/Province'
TRANSFORM = from_bounds(*BBOX, SIZE, SIZE)
API = 'https://earth-search.aws.element84.com/v1/search'
CLEAR = [2, 4, 5, 6, 7]
os.environ['GDAL_DISABLE_READDIR_ON_OPEN'] = 'EMPTY_DIR'
os.environ['CPL_VSIL_CURL_ALLOWED_EXTENSIONS'] = '.tif'
os.environ['GDAL_HTTP_TIMEOUT'] = '90'
os.environ['GDAL_HTTP_CONNECTTIMEOUT'] = '20'
os.environ['GDAL_HTTP_MAX_RETRY'] = '3'
os.environ['GDAL_HTTP_RETRY_DELAY'] = '2'
os.environ['GDAL_CACHEMAX'] = '256'


def configure(size):
    global SIZE, CACHE, PROOF, TRANSFORM
    SIZE = size
    # 仅接受CLI允许的本地已知栅格尺寸，支持16K回读而不关闭Pillow尺寸保护。-codex，2026-10-30
    Image.MAX_IMAGE_PIXELS = 16384**2
    CACHE = OUT / ('longyan-cache' if size == 8192 else 'longyan-cache-16384-v2')
    PROOF = ROOT / 'artifacts/pond-v7-a-proof/longyan-detail' / ('source' if size == 8192 else 'source-16384')
    TRANSFORM = from_bounds(*BBOX, SIZE, SIZE)


def grid_identity():
    return {'bbox': BBOX, 'size': SIZE, 'crs': CRS,
            'transform': list(TRANSFORM), 'gridVersion': GRID_VERSION}


def missing_summary(covered, city_bbox):
    # 先分块计数再收集有限样例，避免故障时数亿像素转成Python对象。-codex，2026-10-30
    total = city_total = 0
    samples = []
    lon = BBOX[0] + (np.arange(SIZE)+.5)/SIZE*(BBOX[2]-BBOX[0])
    in_x = (lon >= city_bbox[0]) & (lon <= city_bbox[2])
    for y in range(0, SIZE, 128):
        missing = ~covered[y:y+128]
        total += int(np.count_nonzero(missing))
        lat = BBOX[3]-(np.arange(y,y+missing.shape[0])+.5)/SIZE*(BBOX[3]-BBOX[1])
        in_y = (lat >= city_bbox[1]) & (lat <= city_bbox[3])
        city_total += int(np.count_nonzero(missing[in_y][:,in_x]))
        if len(samples) < 64:
            # 每行仅取所需样例；不为全图缺测构建坐标数组。-codex，2026-10-30
            for row in range(missing.shape[0]):
                for x in np.flatnonzero(missing[row])[:64-len(samples)]:
                    samples.append({'x':int(x),'y':y+row,'lon':float(lon[x]),'lat':float(lat[row])})
                if len(samples) == 64:
                    break
    return total, city_total, samples


def validate_baseline():
    names = ['longyan-raw.png','longyan-detail.png','longyan-detail-source.json']
    hashes = {name: sha(OUT/name) for name in names}
    current = json.loads((OUT/names[-1]).read_text('utf-8'))
    if current['rawSha256'] != hashes[names[0]] or current['outputSha256'] != hashes[names[1]]:
        raise RuntimeError('Current imagery does not match its manifest')
    if current['textureSize'] != SIZE:
        frozen = json.loads((FROZEN/names[-1]).read_text('utf-8'))
        if current['textureSize'] != 8192 or frozen['textureSize'] != 8192:
            raise RuntimeError('Only frozen 8K to 16K migration is authorized')
        for name in names:
            if hashes[name] != sha(FROZEN/name):
                raise RuntimeError('Frozen 8K baseline mismatch: '+name)
    return hashes


def publish(staging, baseline):
    # PNG与清单全部完成后才替换；清单最后发布，普通异常恢复原三文件。
    # 多文件不具备单次原子性，进程强杀时应从本次staging/previous恢复。-codex，2026-10-30
    for name, digest in baseline.items():
        if sha(OUT/name) != digest:
            raise RuntimeError('Imagery changed during generation: '+name)
    previous = staging/'previous'
    previous.mkdir()
    for name in baseline:
        shutil.copy2(OUT/name, previous/name)
    replaced = []
    try:
        for name in baseline:
            (staging/name).replace(OUT/name)
            replaced.append(name)
    except Exception:
        for name in reversed(replaced):
            shutil.copy2(previous/name, OUT/name)
        raise


def sha(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for part in iter(lambda: f.read(1024 * 1024), b''):
            h.update(part)
    return h.hexdigest()


def retry(action, label):
    for attempt in range(3):
        try:
            return action()
        except Exception as exc:
            print(f'{label}: attempt {attempt + 1} failed: {exc}', flush=True)
            if attempt == 2:
                raise
            time.sleep(2 * (attempt + 1))


def discover(start, end):
    key = f'catalog-{start}-{end}.json'
    path = CACHE / key
    if path.exists():
        return json.loads(path.read_text('utf-8'))

    def fetch():
        r = requests.get(API, params={'collections': 'sentinel-2-l2a',
                         'bbox': ','.join(map(str, BBOX)),
                         'datetime': f'{start}T00:00:00Z/{end}T23:59:59Z',
                         'limit': 100}, timeout=60)
        r.raise_for_status()
        data = r.json()
        features = data['features']
        for _ in range(10):
            link = next((link for link in data.get('links', []) if link.get('rel') == 'next'), None)
            if not link or not data['features']:
                return features
            r = requests.get(link['href'], timeout=60)
            r.raise_for_status()
            data = r.json()
            features.extend(data['features'])
        raise RuntimeError('Catalog exceeds bounded 10-page retrieval')
    features = retry(fetch, 'STAC')
    path.write_text(json.dumps(features, indent=2), encoding='utf-8')
    return features


def window_for(feature):
    west, south, east, north = feature['bbox']
    x0 = max(0, int(np.floor((west-BBOX[0])/(BBOX[2]-BBOX[0])*SIZE))-2)
    x1 = min(SIZE, int(np.ceil((east-BBOX[0])/(BBOX[2]-BBOX[0])*SIZE))+2)
    y0 = max(0, int(np.floor((BBOX[3]-north)/(BBOX[3]-BBOX[1])*SIZE))-2)
    y1 = min(SIZE, int(np.ceil((BBOX[3]-south)/(BBOX[3]-BBOX[1])*SIZE))+2)
    return x0, y0, x1, y1


def read_feature(feature, restrict_bbox=None):
    suffix = '-cloudroi-v1.npz' if restrict_bbox else '-patch-v1.npz'
    if restrict_bbox and SIZE != 8192:
        # 同一景可读取薄云区与单像素缺口；矩形参与缓存键，避免不同ROI误复用。-codex，2026-10-30
        key = hashlib.sha256(json.dumps(restrict_bbox,separators=(',',':')).encode()).hexdigest()[:16]
        suffix = '-roi-'+key+'-v2.npz'
    path = CACHE / (feature['id'] + suffix)
    box = window_for({'bbox':restrict_bbox}) if restrict_bbox else window_for(feature)
    if path.exists():
        with np.load(path) as z:
            if z['box'].tolist() != list(box) or (SIZE != 8192 and ('grid' not in z or json.loads(str(z['grid'].item())) != grid_identity())):
                raise RuntimeError('Cached grid does not match fixed patch')
            rgb, scl = z['rgb'], z['scl']
            if rgb.shape != (3,box[3]-box[1],box[2]-box[0]) or scl.shape != rgb.shape[1:]:
                raise RuntimeError('Cached raster shape does not match grid window')
            return rgb, scl, box, sha(path)
    x0, y0, x1, y1 = box
    win = Window(x0, y0, x1-x0, y1-y0)

    def load_asset(name, method):
        url = feature['assets'][name]['href']
        local = CACHE / (feature['id'] + '-' + name + '.tif')
        def warp(location):
            with rasterio.open(location) as src:
                with WarpedVRT(src, crs='EPSG:4326', transform=TRANSFORM,
                               width=SIZE, height=SIZE, resampling=method,
                               src_nodata=0, nodata=0, warp_mem_limit=128) as vrt:
                    return vrt.read(window=win)
        if local.exists():
            return warp(local)
        try:
            if SIZE == 8192 and feature['id'] == 'S2A_50RNN_20241228_0_L2A':
                raise rasterio.errors.RasterioIOError('Known GDAL transport failure: use requests cache directly')
            return warp(url)
        except rasterio.errors.RasterioIOError as exc:
            if restrict_bbox or SIZE != 8192:
                raise RuntimeError('ROI-only retrieval failed; full-scene fallback is not permitted for this operation') from exc
            print(f'{feature["id"]} {name}: GDAL transport failed; bounded requests cache: {exc}',flush=True)
            # Windows GDAL TLS失败不代表源数据损坏；requests独立链只为失败源缓存，硬限500MB。-codex，2026-10-28
            response = requests.get(url, stream=True, timeout=(20,90))
            response.raise_for_status()
            length = int(response.headers.get('Content-Length',0))
            if length <= 0 or length > 500_000_000:
                raise RuntimeError(f'Unbounded/oversized source {length}')
            temporary = local.with_suffix('.partial.tif')
            total = 0
            with open(temporary,'wb') as f:
                for block in response.iter_content(1024*1024):
                    total += len(block)
                    if total > length:
                        raise RuntimeError('Response exceeds declared size')
                    f.write(block)
            if total != length:
                raise RuntimeError('Incomplete cached TIFF')
            with rasterio.open(temporary) as check:
                if check.count not in [1,3] or check.width != 10980 // (2 if name=='scl' else 1):
                    raise RuntimeError('Unexpected source raster layout')
            temporary.replace(local)
            return warp(local)
    rgb = retry(lambda: load_asset('visual', Resampling.bilinear), feature['id']+' RGB')
    scl = retry(lambda: load_asset('scl', Resampling.nearest), feature['id']+' SCL')[0]
    temp = path.with_suffix('.partial.npz')
    np.savez_compressed(temp, rgb=rgb, scl=scl, box=box, grid=json.dumps(grid_identity(),sort_keys=True))
    temp.replace(path)
    return rgb, scl, box, sha(path)


def crop(im, name, lon, lat):
    x = round((lon-BBOX[0])/(BBOX[2]-BBOX[0])*SIZE)
    y = round((BBOX[3]-lat)/(BBOX[3]-BBOX[1])*SIZE)
    im.crop((x-512, y-512, x+512, y+512)).save(PROOF / f'{name}.png')


def fill_city_gaps(raw, covered, provenance, records, missing, city_bbox):
    """16K新增采样中心可能暴露原8K未见的小缺口；仅用真实清晰源补最多4个市内缺测点。
    不重采已有像素、不扩大容许缺测范围；小窗读取失败或SCL不合格仍由市内零缺测门禁拒绝。
    """
    gaps = [p for p in missing if city_bbox[0] <= p['lon'] <= city_bbox[2] and city_bbox[1] <= p['lat'] <= city_bbox[3]]
    if not gaps or len(gaps)>4:
        return []
    candidates = sorted(discover('2025-01-20','2025-01-20'),key=lambda f:(f['properties'].get('eo:cloud_cover',100),f['id']))
    repairs = []
    for point in gaps:
        x,y,lon,lat = point['x'],point['y'],point['lon'],point['lat']
        if covered[y,x]:
            continue
        dx,dy = (BBOX[2]-BBOX[0])/SIZE*16,(BBOX[3]-BBOX[1])/SIZE*16
        region = [max(BBOX[0],lon-dx),max(BBOX[1],lat-dy),min(BBOX[2],lon+dx),min(BBOX[3],lat+dy)]
        for feature in candidates:
            w,s,e,n = feature['bbox']
            if not (w<=lon<=e and s<=lat<=n):
                continue
            rgb,scl,box,digest = read_feature(feature,region)
            lx,ly = x-box[0],y-box[1]
            value = rgb[:,ly,lx]
            if int(scl[ly,lx]) not in CLEAR or not np.any(value):
                continue
            if len(records)>=255:
                raise RuntimeError('Source index capacity exceeded')
            # 唯一写入是经有效掩码证明的缺测像素；邻域仅用于真实源的小窗读取，不参与混色。
            raw[y,x] = value
            covered[y,x] = True
            provenance[y,x] = len(records)+1
            record = {'id':feature['id'],'datetime':feature['properties']['datetime'],
                      'bbox':region,'visualUrl':feature['assets']['visual']['href'],'sclUrl':feature['assets']['scl']['href'],
                      'nativeResolutionM':10,'sclResolutionM':20,'sceneCloudPercent':feature['properties']['eo:cloud_cover'],
                      'cachedReprojectedSubsetSha256':digest,'acceptedPixels':1,
                      'gapOnlyPixel':{'x':x,'y':y,'lon':lon,'lat':lat,'scl':int(scl[ly,lx]),'rgb':value.tolist()},
                      'method':'Only previously missing city pixel; clear SCL and nonzero real RGB; no interpolation from existing map'}
            records.append(record)
            repairs.append(record)
            print('CITY_GAP_FILLED '+json.dumps(record),flush=True)
            break
    return repairs


def outside_digest(array, box):
    """局部换源只能改变授权矩形内像素；不把整图重新调色伪装成局部修复。-codex，2026-10-28"""
    ax,ay,bx,by = box
    h = hashlib.sha256()
    for part in [array[:ay],array[by:],array[ay:by,:ax],array[ay:by,bx:]]:
        h.update(np.ascontiguousarray(part).tobytes())
    return h.hexdigest()


def main():
    CACHE.mkdir(parents=True, exist_ok=True)
    PROOF.mkdir(parents=True, exist_ok=True)
    baseline = validate_baseline()
    staging = Path(tempfile.mkdtemp(prefix='publish-',dir=CACHE))
    base = OUT / 'province-surface.png'
    source = json.loads((OUT / 'province-source.json').read_text('utf-8'))
    if sha(base) != source['outputs']['province-surface.png']:
        raise RuntimeError('Province source changed: inspect baseline before color matching')
    raw = np.zeros((SIZE, SIZE, 3), dtype=np.uint8)
    covered = np.zeros((SIZE, SIZE), dtype=bool)
    provenance = np.zeros((SIZE, SIZE), dtype=np.uint8)
    records = []
    blend_records = []
    seen = set()
    # 同日优先保证接缝季相一致；仅在有效掩码证明缺口后才取邻近日期。-codex，2026-10-28
    for start, end in [('2024-12-31', '2024-12-31'), ('2024-12-28', '2024-12-30'),
                       ('2025-01-01', '2025-01-03'), ('2025-01-08', '2025-01-10')]:
        features = sorted(discover(start, end), key=lambda f:
                          (f['id'].split('_')[1] != '50RMN', f['properties'].get('eo:cloud_cover', 100)))
        pending = []
        for feature in features:
            x0,y0,x1,y1 = window_for(feature)
            if feature['id'] not in seen and x1>x0 and y1>y0 and not covered[y0:y1,x0:x1].all():
                pending.append(feature)
        # 三路匿名分块读取，限制服务器负载与并发内存；缓存完成后原子发布。-codex，2026-10-28
        pool = ThreadPoolExecutor(max_workers=3) if SIZE == 8192 else None
        # 16K按景读取，不预取整批Future结果；单景数组也约为8K的四倍。-codex，2026-10-30
        downloads = {f['id']:pool.submit(read_feature, f) for f in pending} if pool else {}
        for feature in pending:
            if feature['id'] in seen:
                continue
            seen.add(feature['id'])
            x0, y0, x1, y1 = window_for(feature)
            if x1 <= x0 or y1 <= y0 or covered[y0:y1, x0:x1].all():
                continue
            print('Reading '+feature['id'], flush=True)
            rgb, scl, box, digest = downloads.pop(feature['id']).result() if pool else read_feature(feature)
            valid = np.isin(scl, CLEAR) & np.any(rgb != 0, axis=0)
            fill = valid & ~covered[y0:y1, x0:x1]
            existing = raw[y0:y1,x0:x1]
            incoming = rgb.transpose(1,2,0)
            overlap = valid & covered[y0:y1,x0:x1] & (scl == 4)
            overlap &= (incoming.min(axis=2)>12) & (incoming.max(axis=2)<150)
            overlap &= (existing.min(axis=2)>12) & (existing.max(axis=2)<150)
            gain = np.ones(3)
            if overlap.sum()>1000:
                sample = overlap[::4,::4]
                if sample.any():
                    gain = np.median((existing[::4,::4][sample].astype(np.float32)+1)/
                                     (incoming[::4,::4][sample].astype(np.float32)+1),axis=0)
                    gain = np.clip(gain,.70,1.40)
            if not np.isfinite(gain).all():
                raise RuntimeError('Non-finite scene color gain')
            incoming = np.uint8(np.clip(incoming.astype(np.float32)*gain,0,255))
            # 只羽化新区域的合成权重，不滤波影像本身；同地重叠森林中位数校正场景色差。-codex，2026-10-28
            feather = np.asarray(Image.fromarray(np.uint8(fill)*255).filter(ImageFilter.BoxBlur(32*SIZE//8192)),dtype=np.float32)/127.5
            alpha = np.minimum(feather,1)*valid
            alpha[fill] = 1
            existing[:] = np.uint8(existing.astype(np.float32)*(1-alpha[...,None])+incoming.astype(np.float32)*alpha[...,None])
            blend_records.append({'id':feature['id'],'overlapForestPixels':int(overlap.sum()),
                                  'rgbGain':gain.tolist(),'maskFeatherPixels':32*SIZE//8192})
            records.append({'id': feature['id'], 'datetime': feature['properties']['datetime'],
                            'bbox': feature['bbox'], 'visualUrl': feature['assets']['visual']['href'],
                            'sclUrl': feature['assets']['scl']['href'], 'nativeResolutionM': 10,
                            'sclResolutionM': 20, 'sceneCloudPercent': feature['properties']['eo:cloud_cover'],
                            'cachedReprojectedSubsetSha256': digest,
                            'sclHistogram': {str(v): int(n) for v,n in zip(*np.unique(scl, return_counts=True))},
                            'acceptedPixels': int(fill.sum()), 'nodataPixels': int((scl == 0).sum()),
                            'cloudShadowRejectedPixels': int(np.isin(scl, [3,8,9,10,11]).sum())})
            covered[y0:y1, x0:x1] |= valid
            provenance[y0:y1, x0:x1][fill] = len(records)
            print(f'Coverage {covered.mean():.8%}; added {fill.sum()}', flush=True)
            # 中心首景先回传，黑区明确属于尚未拼完而非交付纹理。-codex，2026-10-28
            if len(records) == 1:
                crop(Image.fromarray(raw), 'longyan-center-first-source', 117.03, 25.08)
                print('EARLY_CROP '+str(PROOF / 'longyan-center-first-source.png'), flush=True)
        if pool:
            pool.shutdown(wait=True)
        if covered.all():
            break
    city_bbox = [115.853363,24.369851,117.748258,26.033059]
    missing_count, city_missing_count, missing = missing_summary(covered, city_bbox)
    gap_repairs = []
    if SIZE == 16384 and 0<city_missing_count<=4 and missing_count<=64:
        gap_repairs = fill_city_gaps(raw,covered,provenance,records,missing,city_bbox)
        missing_count, city_missing_count, missing = missing_summary(covered, city_bbox)
    Image.fromarray(np.uint8(covered)*255).save(PROOF / 'longyan-coverage.png')
    Image.fromarray(provenance).save(PROOF / 'longyan-source-index.png')
    (PROOF / 'longyan-source-audit.json').write_text(json.dumps(records, indent=2), encoding='utf-8')
    missing_limit = 5*(SIZE//8192)**2
    print(f'COVERAGE total={SIZE*SIZE} missing={missing_count} cityMissing={city_missing_count} outsideLimit={missing_limit}',flush=True)
    if missing_count > missing_limit or city_missing_count:
        raise RuntimeError(f'Coverage rejected: {missing_count} missing; {city_missing_count} inside city bbox; samples={missing}')
    # SCL会漏掉薄云；显式取得已人工对比批准的清晰日期，不依赖补洞循环碰巧访问此源。-codex，2026-10-28
    cloud_roi = [117.28,25.45,117.50,25.72]
    cloud_id = 'S2A_50RNP_20250120_0_L2A'
    cloud_feature = next((f for f in discover('2025-01-20','2025-01-20') if f['id']==cloud_id),None)
    if cloud_feature is None:
        raise RuntimeError('Approved clear replacement scene is absent from catalog')
    cloud_rgb, cloud_scl, cloud_box, cloud_hash = read_feature(cloud_feature,cloud_roi)
    x0,y0,x1,y1 = cloud_box
    ax = max(x0,int((cloud_roi[0]-BBOX[0])/(BBOX[2]-BBOX[0])*SIZE))
    bx = min(x1,int((cloud_roi[2]-BBOX[0])/(BBOX[2]-BBOX[0])*SIZE))
    ay = max(y0,int((BBOX[3]-cloud_roi[3])/(BBOX[3]-BBOX[1])*SIZE))
    by = min(y1,int((BBOX[3]-cloud_roi[1])/(BBOX[3]-BBOX[1])*SIZE))
    other = cloud_rgb[:,ay-y0:by-y0,ax-x0:bx-x0].transpose(1,2,0).astype(np.float32)
    scl = cloud_scl[ay-y0:by-y0,ax-x0:bx-x0]
    region = raw[ay:by,ax:bx]
    valid = np.isin(scl,CLEAR) & np.any(other!=0,axis=2)
    forest = valid & (scl==4) & (other.min(axis=2)>12) & (region.min(axis=2)>12)
    # 排除旧影像明显白云再匹配色调，避免把清晰山林整体提亮成旧云色。-codex，2026-10-28
    forest &= (region.mean(axis=2)<other.mean(axis=2)*1.30+8)
    correction = np.ones(3)
    if forest.sum()>=1000:
        correction = np.clip(np.median((region[forest].astype(np.float32)+1)/(other[forest]+1),axis=0),.7,1.4)
    if not np.isfinite(correction).all():
        raise RuntimeError('Non-finite replacement color gain')
    other *= correction
    ys,xs = np.indices(valid.shape)
    border = np.minimum(np.minimum(xs,valid.shape[1]-1-xs),np.minimum(ys,valid.shape[0]-1-ys))
    alpha = np.minimum(border/(24.*SIZE/8192),1)*valid
    cloud_replaced = int((alpha>0).sum())
    region[:] = np.uint8(np.clip(region*(1-alpha[...,None])+other*alpha[...,None],0,255))
    roi_box = (ax,ay,bx,by)
    raw_outside = outside_digest(raw,roi_box)
    if (OUT/'longyan-raw.png').exists() and Image.open(OUT/'longyan-raw.png').size == (SIZE,SIZE):
        if raw_outside != outside_digest(np.asarray(Image.open(OUT/'longyan-raw.png')),roi_box):
            raise RuntimeError('Unexpected raw changes outside approved cloud ROI')
    raw_image = Image.fromarray(raw)
    raw_image.save(staging / 'longyan-raw.png')
    raw_image.resize((1600,1600), Image.Resampling.LANCZOS).save(PROOF / 'longyan-raw-overview.png')
    for name, lon, lat in [('center',117.03,25.08), ('west',116.1,25.0),
                           ('north',116.6,25.8), ('east',117.55,25.3)]:
        crop(raw_image, 'longyan-raw-'+name, lon, lat)
    print('RAW_STAGED '+str(staging / 'longyan-raw.png'), flush=True)
    # 只调色，不平滑真实纹理。RGB亮度/边缘全部来自遥感，旧底图仅提供整体色彩目标。-codex，2026-10-28
    old = Image.open(base).convert('RGB')
    b = source['boundsShared']
    box = ((SHARED[0]-b[0])/(b[2]-b[0])*old.width,
           (b[3]-SHARED[3])/(b[3]-b[1])*old.height,
           (SHARED[2]-b[0])/(b[2]-b[0])*old.width,
           (b[3]-SHARED[1])/(b[3]-b[1])*old.height)
    target_mean = np.asarray(old.crop(box)).mean(axis=(0,1))
    actual_mean = raw.mean(axis=(0,1))
    # 固定主代理已批准的R15色彩校准，局部换源不触发全省/全patch色调漂移。-codex，2026-10-28
    gain = np.array([0.8961120151442088,1.0167961451441834,1.15])
    detail = np.empty_like(raw)
    for y in range(0, SIZE, 128):
        a = raw[y:y+128].astype(np.float32)
        # 85% 保留真实色彩变化，15%降低饱和度，保持田块/城区的强度差。-codex，2026-10-28
        lum = a @ np.array([.2126,.7152,.0722], dtype=np.float32)
        a = .85*a+.15*lum[...,None]
        detail[y:y+128] = np.uint8(np.clip(a*gain, 0,255))
    detail_image = Image.fromarray(detail).convert('RGBA')
    detail_image.putalpha(Image.fromarray(np.uint8(covered)*255))
    detail_outside = outside_digest(np.asarray(detail_image),roi_box)
    if (OUT/'longyan-detail.png').exists() and Image.open(OUT/'longyan-detail.png').size == (SIZE,SIZE):
        if detail_outside != outside_digest(np.asarray(Image.open(OUT/'longyan-detail.png')),roi_box):
            raise RuntimeError('Unexpected styled changes outside approved cloud ROI')
    detail_image.save(staging / 'longyan-detail.png')
    detail_image.resize((1600,1600),Image.Resampling.LANCZOS).save(PROOF / 'longyan-detail-overview.png')
    crop(detail_image,'longyan-detail-center',117.03,25.08)
    crop(detail_image,'longyan-cloud-corrected',117.39,25.57)
    manifest = {'textureSize': SIZE, 'bbox': BBOX, 'boundsShared': SHARED,
                'orientation': 'north-up; linear longitude x, linear latitude y; EPSG:4326',
                'baseSha256': sha(base), 'rawSha256': sha(staging/'longyan-raw.png'),
                'outputSha256': sha(staging/'longyan-detail.png'), 'nativeResolutionM': 10,
                'outputApproxMetersPerPixel': [(BBOX[2]-BBOX[0])*111320*math.cos(math.radians((BBOX[1]+BBOX[3])/2))/SIZE,(BBOX[3]-BBOX[1])*111320/SIZE],
                'grid':grid_identity(),
                'credit': 'Contains modified Copernicus Sentinel data '+ '/'.join(sorted({r['datetime'][:4] for r in records})) + '; ESA/Copernicus via Element 84 Earth Search and AWS Open Data.',
                'runtimeVersions': {'rasterio': rasterio.__version__, 'numpy': np.__version__},
                'downloadedSourceFiles': [{'file':p.name,'sha256':sha(p),'bytes':p.stat().st_size} for p in CACHE.glob('*.tif') if '.partial.' not in p.name],
                'license': 'https://cds.climate.copernicus.eu/licences/ec-sentinel',
                'coverageFraction': float(covered.mean()), 'unfilledPixels': missing_count, 'outsideCityMissingLimit':missing_limit, 'missingSampleLimit':64,
                'missingPixels': missing, 'cityBbox':city_bbox,'cityBboxUnfilledPixels':city_missing_count,
                'alphaSemantics':'0 for recorded invalid pixels outside Longyan city bbox; otherwise 255. Renderer must multiply patch blend by texture alpha.',
                'acceptedSclClasses': CLEAR, 'inputs': records,
                'sceneColorMatching':blend_records,
                'cloudEdgeCorrection':{'bbox':cloud_roi,'replacementSource':cloud_id,'replacedPixels':cloud_replaced,
                    'visualUrl':cloud_feature['assets']['visual']['href'],'sclUrl':cloud_feature['assets']['scl']['href'],
                    'datetime':cloud_feature['properties']['datetime'],'cachedReprojectedSubsetSha256':cloud_hash,
                    'forestMatchingPixels':int(forest.sum()),'rgbGain':correction.tolist(),
                    'replacementClearPixelFraction':float(valid.mean()),
                    'outsideRoiRawSha256':raw_outside,'outsideRoiDetailSha256':detail_outside,
                    'featherPixels':24*SIZE//8192, 'method':'Explicit approved 2025-01-20 clear ROI; SCL/nodata validation, RGB gain from non-cloud forest, resolution-scaled boundary mask feather; no image smoothing'},
                'colorTreatment': {'gain': gain.tolist(), 'targetMean':target_mean.tolist(),
                                   'rawMean':actual_mean.tolist(), 'desaturation':.15, 'spatialSmoothing':False},
                'limits': '10m source resampled to the recorded output grid; historical imagery, not current surveyed ground truth. SCL validation does not prove absence of every thin cloud.',
                'sourceIndexSemantics':'Initial fill source only; cloudEdgeCorrection separately records final ROI replacement; overlap feather can mix sources.'}
    manifest['boundedCityGapFill'] = gap_repairs
    (staging/'longyan-detail-source.json').write_text(json.dumps(manifest,indent=2),encoding='utf-8')
    publish(staging,baseline)
    print('DONE '+json.dumps({k:v for k,v in manifest.items() if k!='inputs'}), flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--size',type=int,choices=[8192,16384],default=8192)
    configure(parser.parse_args().size)
    main()
