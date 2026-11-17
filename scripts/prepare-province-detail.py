"""派生近景地貌颜色：保留R15源图，以同源细DEM补坡面细节并抑制低地粗块。
不写原图、背景、边界或地形网格；运行时无网络，素材来源与哈希随输出记录。-codex，2026-10-28
"""
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import hashlib
import io
import json
import math
import time
import numpy as np
import requests
from PIL import Image, ImageFilter

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'unreal/FujianSandbox58/Content/Province'
CACHE = OUT / 'cache'
SIZE = 8192
ZOOM = 11
COS = math.cos(math.radians(26.05))


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def tile_xy(x, y, zoom):
    lon, lat = x / (100 * COS) + 118.2, y / 100 + 26.05
    return (lon + 180) / 360 * 2**zoom, (1 - np.arcsinh(np.tan(np.radians(lat))) / math.pi) / 2 * 2**zoom


def main():
    source = json.loads((OUT / 'province-source.json').read_text('utf-8'))
    base_path = OUT / 'province-surface.png'
    if sha(base_path) != source['outputs']['province-surface.png']:
        raise RuntimeError('R15 source changed; inspect before deriving detail')
    bounds = np.asarray(source['boundsShared'])
    inputs = {}

    def mosaic(zoom):
        left, bottom = tile_xy(bounds[0]-2, bounds[1]-2, zoom)
        right, top = tile_xy(bounds[2]+2, bounds[3]+2, zoom)
        x0, y0, x1, y1 = math.floor(left), math.floor(top), math.floor(right), math.floor(bottom)
        tiles = [(x, y) for y in range(y0, y1+1) for x in range(x0, x1+1)]

        def fetch(pair):
            x, y = pair
            old = ROOT / f'shared/terrain/tiles/{zoom}-{x}-{y}.png'
            path = old if old.exists() else CACHE / f'{zoom}-{x}-{y}.png'
            if not path.exists():
                url = f'https://s3.amazonaws.com/elevation-tiles-prod/terrarium/{zoom}/{x}/{y}.png'
                for attempt in range(3):
                    try:
                        response = requests.get(url, timeout=45)
                        response.raise_for_status()
                        with Image.open(io.BytesIO(response.content)) as im:
                            if im.size != (256, 256):
                                raise ValueError(f'Invalid DEM tile {pair}')
                            im.load()
                        path.write_bytes(response.content)
                        break
                    except (requests.RequestException, OSError):
                        if attempt == 2:
                            raise
                        time.sleep(attempt + 1)
            return pair, path

        result = np.full(((y1-y0+1)*256, (x1-x0+1)*256), np.nan, dtype=np.float32)
        with ThreadPoolExecutor(max_workers=12) as pool:
            for index, ((x, y), path) in enumerate(pool.map(fetch, tiles)):
                a = np.asarray(Image.open(path).convert('RGB'), dtype=np.float32)
                if a.shape != (256, 256, 3):
                    raise ValueError(f'Invalid cached DEM {path}')
                result[(y-y0)*256:(y-y0+1)*256, (x-x0)*256:(x-x0+1)*256] = a[..., 0]*256+a[..., 1]+a[..., 2]/256-32768
                inputs[str(path.relative_to(ROOT))] = sha(path)
                if index % 100 == 0:
                    print(f'DEM z{zoom}: {index+1}/{len(tiles)}', flush=True)
        if not np.isfinite(result).all():
            raise ValueError('Missing elevation coverage')

        def sample(x, y):
            tx, ty = tile_xy(x, y, zoom)
            px, py = (tx-x0)*256-.5, (ty-y0)*256-.5
            if px.min() < 0 or py.min() < 0 or px.max() >= result.shape[1]-1 or py.max() >= result.shape[0]-1:
                raise ValueError('Sampling outside cached elevation')
            ix, iy = px.astype(np.int32), py.astype(np.int32)
            dx, dy = px-ix, py-iy
            return result[iy, ix]*(1-dx)*(1-dy)+result[iy, ix+1]*dx*(1-dy)+result[iy+1, ix]*(1-dx)*dy+result[iy+1, ix+1]*dx*dy
        return sample

    coarse, fine = mosaic(9), mosaic(ZOOM)
    original = Image.open(base_path).convert('RGB')
    # 低地只减弱原影像采样块，不把平原伪装成建筑或山地；边界在另一张遮罩中，不参与柔化。
    soft = np.asarray(original.filter(ImageFilter.GaussianBlur(3)).resize((SIZE, SIZE), Image.Resampling.BICUBIC))
    base = np.asarray(original.resize((SIZE, SIZE), Image.Resampling.BICUBIC))
    output = np.empty_like(base)
    x = np.linspace(bounds[0], bounds[2], SIZE)[None, :]
    ys = np.linspace(bounds[3], bounds[1], SIZE)
    residual_sum, count = 0., 0
    for start in range(0, SIZE, 64):
        end = min(start+64, SIZE)
        y = ys[start:end, None]
        elevation = np.maximum(0, fine(x, y))
        def shade(sample):
            # 西北向与原图一致，约180m邻域避免细DEM在斜视下呈现砂点；不改变现有几何。
            d = .16
            gx = (sample(x+d, y)-sample(x-d, y))/(2*d*1113.2)
            gy = (sample(x, y+d)-sample(x, y-d))/(2*d*1113.2)
            light = (.50+3.3*gx-3.9*gy)/np.sqrt(1+36*(gx*gx+gy*gy))
            return light, np.sqrt(gx*gx+gy*gy)
        small, slope = shade(fine)
        large, _ = shade(coarse)
        residual = np.clip(small-large, -.30, .30)
        residual_sum += float(np.abs(residual).sum()); count += residual.size
        flat = np.exp(-elevation/180)*np.exp(-slope/.075)
        rgb = base[start:end].astype(np.float32)
        rgb += (soft[start:end].astype(np.float32)-rgb)*(flat*.90)[..., None]
        # 平原主要修复粗块，不用微高程噪声制造褶皱；山坡也保留原明暗的主导地位。
        rgb *= (1+residual*.55*(1-flat*.85))[..., None]
        output[start:end] = np.uint8(np.clip(rgb, 0, 255))
    target = OUT / 'province-detail.png'
    Image.fromarray(output).save(target)
    manifest = {
        'textureSize': SIZE, 'zoom': ZOOM, 'boundsShared': bounds.tolist(),
        'baseSha256': sha(base_path), 'outputSha256': sha(target), 'inputs': inputs,
        'meanAbsoluteFineShadeResidual': residual_sum/count,
        'source': 'https://registry.opendata.aws/terrain-tiles/',
        'credit': 'Mapzen; SRTM and GMTED2010 terrain data courtesy of USGS; ETOPO1 DOC/NOAA/NESDIS/NCEI. Base colors retain NASA Blue Marble 2004 reference.',
        'interpretation': 'Near-view cartographic shading, not current imagery or new geometry. Fine DEM shade residual plus attenuation of coarse lowland color blocks.',
    }
    (OUT / 'province-detail-source.json').write_text(json.dumps(manifest, indent=2), encoding='utf-8')
    print(json.dumps({k:v for k,v in manifest.items() if k != 'inputs'}), flush=True)


if __name__ == '__main__':
    main()
