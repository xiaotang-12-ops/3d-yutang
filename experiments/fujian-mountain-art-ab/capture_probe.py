"""原生截图时序核对。-codex，2026-10-23"""
import unreal,pathlib,json
probe={'index':AB_CAP_INDEX,'task':str(AB_CAP_TASK),'time':time.time(),'started':AB_CAP_START}
pathlib.Path('D:/3d-research/artifacts/fujian-mountain-art-ab/capture-probe.json').write_text(json.dumps(probe),encoding='utf-8')
AB_SCREEN_TASK=unreal.AutomationLibrary.take_high_res_screenshot(1920,1080,'D:/3d-research/artifacts/fujian-mountain-art-ab/Original-Normal.png',camera=cams['Normal'],delay=0.0)
unreal.EditorLevelLibrary.editor_invalidate_viewports()
