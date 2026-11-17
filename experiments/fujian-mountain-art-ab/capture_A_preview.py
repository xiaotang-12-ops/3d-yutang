import unreal
ea=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
cam=next(a for a in ea.get_all_level_actors() if a.get_actor_label()=='AB_Camera_Normal')
unreal.EditorLevelLibrary.set_level_viewport_camera_info(cam.get_actor_location(),cam.get_actor_rotation())
unreal.AutomationLibrary.take_high_res_screenshot(1920,1080,'D:/3d-research/artifacts/fujian-mountain-art-ab/A-preview.png',camera=cam,delay=0.0,force_game_view=True)
unreal.EditorLevelLibrary.editor_invalidate_viewports()
