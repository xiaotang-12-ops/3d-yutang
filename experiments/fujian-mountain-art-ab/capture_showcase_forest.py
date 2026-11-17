import unreal
asys=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
assert unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world().get_path_name().startswith('/Game/ArtAB/Mountain_Showcase.')
asys.clear_actor_selection_set()
c=next(a for a in asys.get_all_level_actors() if a.get_actor_label()=='Showcase_Camera_Ground')
AB_SHOWCASE_TASK=unreal.AutomationLibrary.take_high_res_screenshot(1920,1080,'D:/3d-research/artifacts/fujian-mountain-art-ab/showcase-forest-ground.png',camera=c,delay=0.0)
unreal.EditorLevelLibrary.editor_invalidate_viewports()
