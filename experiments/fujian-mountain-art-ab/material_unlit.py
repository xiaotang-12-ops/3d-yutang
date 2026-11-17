import unreal,pathlib,json
before=unreal.AutomationLibrary.get_editor_active_viewport_view_mode()
unreal.AutomationLibrary.set_editor_viewport_view_mode(unreal.ViewModeIndex.VMI_UNLIT)
pathlib.Path('D:/3d-research/artifacts/fujian-mountain-art-ab/view-mode-before.json').write_text(json.dumps({'before':str(before)}),encoding='utf-8')
c=next(a for a in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors() if a.get_actor_label()=='AB_Camera_Normal')
unreal.AutomationLibrary.take_high_res_screenshot(1920,1080,'D:/3d-research/artifacts/fujian-mountain-art-ab/B-unlit.png',camera=c,delay=0)
unreal.EditorLevelLibrary.editor_invalidate_viewports()
