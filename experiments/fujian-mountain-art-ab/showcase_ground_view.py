import unreal,json,pathlib
le=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem);asys=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
cam=next(a for a in asys.get_all_level_actors() if a.get_actor_label()=='Showcase_Camera_Ground')
le.set_level_viewport_camera_info(cam.get_actor_location(),cam.get_actor_rotation(),le.get_active_viewport_config_key())
unreal.EditorLevelLibrary.editor_invalidate_viewports()
info={'camera':str(le.get_level_viewport_camera_info(le.get_active_viewport_config_key())),'viewmode':str(unreal.AutomationLibrary.get_editor_active_viewport_view_mode())}
pathlib.Path('D:/3d-research/artifacts/fujian-mountain-art-ab/showcase-viewstate.json').write_text(json.dumps(info,indent=2),encoding='utf-8')
