"""隔离实验编辑器内批处理入口；只接受本目录脚本，真实目标路径不符即拒绝。-codex，2026-10-23"""
import unreal
import pathlib
import json
import traceback
import time

AB_ROOT = pathlib.Path('D:/3d-research/experiments/fujian-mountain-art-ab').resolve()
AB_OUT = pathlib.Path('D:/3d-research/artifacts/fujian-mountain-art-ab')
assert pathlib.Path(unreal.Paths.project_dir()).resolve() == AB_ROOT, 'Wrong project: refusing experiment writes'
AB_QUEUE = AB_ROOT / 'queue'
AB_QUEUE.mkdir(exist_ok=True)
AB_GLOBALS = {'unreal':unreal, '__builtins__':__builtins__}
AB_SEEN = {p.name for p in AB_QUEUE.glob('*.json') if (AB_OUT/(p.stem+'-result.json')).exists()}
AB_BUSY = False

def ab_tick(delta):
    global AB_BUSY
    if AB_BUSY:
        return
    for job in sorted(AB_QUEUE.glob('*.json')):
        if job.name in AB_SEEN:
            continue
        AB_SEEN.add(job.name)
        AB_BUSY = True
        try:
            spec = json.loads(job.read_text(encoding='utf-8'))
            script = (AB_ROOT / spec['script']).resolve()
            assert script.is_relative_to(AB_ROOT) and script.suffix == '.py'
            AB_GLOBALS['__file__'] = str(script)
            exec(compile(script.read_text(encoding='utf-8-sig'), str(script), 'exec'), AB_GLOBALS)
            result = {'ok': True, 'job':job.name, 'script':str(script), 'time':time.time()}
        except Exception:
            result = {'ok':False, 'job':job.name, 'error':traceback.format_exc(), 'time':time.time()}
            unreal.log_error(result['error'])
        (AB_OUT / (job.stem+'-result.json')).write_text(json.dumps(result,indent=2),encoding='utf-8')
        AB_BUSY = False
        break

AB_HANDLE = unreal.register_slate_post_tick_callback(ab_tick)
(AB_OUT/'editor-bridge-ready.json').write_text(json.dumps({'project':unreal.Paths.project_dir(),'engine':unreal.SystemLibrary.get_engine_version(),'ready':True},indent=2),encoding='utf-8')
unreal.log('FUJIAN_AB_BRIDGE_READY')
