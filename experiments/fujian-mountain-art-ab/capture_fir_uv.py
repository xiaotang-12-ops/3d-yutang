import unreal
AB_FIR_UV_TASK=unreal.AutomationLibrary.take_high_res_screenshot(1280,720,'D:/3d-research/artifacts/fujian-mountain-art-ab/fir-uv-corrected.png',delay=0.0)
unreal.EditorLevelLibrary.editor_invalidate_viewports()
