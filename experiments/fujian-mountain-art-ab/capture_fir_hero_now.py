import unreal
AB_FIR_HERO_TASK=unreal.AutomationLibrary.take_high_res_screenshot(1280,720,'D:/3d-research/artifacts/fujian-mountain-art-ab/fir-hero-forced.png',delay=0.0)
unreal.EditorLevelLibrary.editor_invalidate_viewports()
