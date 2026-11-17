"""按采集时间戳编码原速视频，同时导出回看索引；不伪造固定帧率。"""
import argparse, csv, json, shutil, subprocess
from pathlib import Path
import numpy as np
from PIL import Image, ImageDraw

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('take', type=Path, help='包含 capture-times.csv 和 JPEG 的 take 目录')
parser.add_argument('--output-dir', type=Path, help='任务 artifacts 根内的空目录；默认 take/encoded')
args = parser.parse_args()
take = args.take.resolve()
artifact_root = (Path(__file__).resolve().parents[2]/'artifacts/fujian-dashboard-motion').resolve()
out = (args.output_dir if args.output_dir is not None else take/'encoded').resolve()
if not out.is_relative_to(artifact_root) or out == artifact_root:
    parser.error(f'输出必须位于任务 artifacts 根的子目录内：{artifact_root}')
if out == take or out in take.parents:
    parser.error('输出目录不能是原始 take 或其父目录')
if out.exists() and (not out.is_dir() or any(out.iterdir())):
    parser.error(f'输出目录已有内容，拒绝覆盖：{out}')
ffmpeg = shutil.which('ffmpeg')
if not ffmpeg:
    parser.error('未找到 ffmpeg，请检查 PATH')
try:
    with (take/'capture-times.csv').open(encoding='utf-8-sig', newline='') as source:
        rows = list(csv.DictReader(source))
    times = np.array([float(r['time_seconds']) for r in rows])
    read_times = np.array([float(r['read_encode_ms']) for r in rows])
    frames = [int(r['frame']) for r in rows]
    if len(rows) < 3 or not np.isfinite(times).all() or not (np.diff(times)>0).all():
        raise ValueError('至少需要3帧，时间戳须为有限值且严格递增')
    if times[0] < 0 or not np.isfinite(read_times).all() or (read_times < 0).any():
        raise ValueError('时间戳和采集耗时必须有效且非负')
    if any(frame < 0 for frame in frames) or len(set(frames)) != len(frames):
        raise ValueError('帧编号必须非负且不重复')
    for frame, row in zip(frames, rows):
        if not (take/f'frame-{frame:05d}.jpg').is_file():
            raise ValueError(f'缺少原始图像：frame-{frame:05d}.jpg')
        if 'request_target_id' not in row and 'id' not in row:
            raise ValueError('时间索引缺少请求目标列（request_target_id或旧版id）')
except (OSError, ValueError, KeyError, TypeError) as error:
    parser.error(f'采集输入无效：{error}')
deltas = np.diff(times)
lines = ['ffconcat version 1.0']
for i,r in enumerate(rows):
    path = (take/f"frame-{int(r['frame']):05d}.jpg").as_posix().replace("'", "'\\''")
    lines += [f"file '{path}'",'option framerate 1000',f'duration {deltas[i] if i<len(deltas) else np.median(deltas):.6f}']
out.mkdir(parents=True, exist_ok=True)
concat = out/'realtime.ffconcat'
concat.write_text('\n'.join(lines)+'\n',encoding='utf-8')
subprocess.run([ffmpeg,'-n','-v','warning','-f','concat','-safe','0','-i',str(concat),
                '-fps_mode','vfr','-enc_time_base','1/1000','-c:v','libx264','-preset','medium',
                '-crf','18','-pix_fmt','yuv420p','-movflags','+faststart',str(out/'interaction-realtime.mp4')],check=True)
stats = {'frames':len(rows),'duration_seconds':float(times[-1]-times[0]),
         'mean_capture_fps':float((len(rows)-1)/(times[-1]-times[0])),
         'interval_p50_ms':float(np.percentile(deltas,50)*1000),
         'interval_p95_ms':float(np.percentile(deltas,95)*1000),
         'interval_max_ms':float(deltas.max()*1000),
         'read_encode_p50_ms':float(np.median([float(r['read_encode_ms']) for r in rows])),
         'timebase':'actual timestamps, VFR, no retiming','input':str(take),
         'identity_note':'request_target_id (legacy id) is the selection target at capture request, not the displayed frame identity; review labels use timestamp and frame only'}
(out/'recording-audit.json').write_text(json.dumps(stats,indent=2),encoding='utf-8')
# 每秒回看表；转场密集帧可继续按帧号读取原图。
for start in range(0,int(times[-1])+1,24):
    sheet = Image.new('RGB',(1280,6*202),'#030d1b');draw=ImageDraw.Draw(sheet)
    for slot,second in enumerate(range(start,min(start+24,int(times[-1])+1))):
        i=int(np.argmin(abs(times-second)));p=take/f"frame-{int(rows[i]['frame']):05d}.jpg"
        im=Image.open(p);im.thumbnail((320,180));x=(slot%4)*320;y=(slot//4)*202
        sheet.paste(im,(x,y));draw.text((x+6,y+182),f'{times[i]:.3f}s / frame {frames[i]}',fill='white')
    sheet.save(out/f'recording-review-{start:02d}.jpg',quality=92)
print(json.dumps(stats))
