import unreal
asys=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
unreal.SystemLibrary.execute_console_command(w,'r.AntiAliasingMethod 4')
unreal.SystemLibrary.execute_console_command(w,'r.ScreenPercentage 100')
for a in asys.get_all_level_actors():
 if isinstance(a,unreal.ExponentialHeightFog):a.get_components_by_class(unreal.ExponentialHeightFogComponent)[0].set_editor_property('fog_inscattering_luminance',unreal.LinearColor(r=.4,g=.55,b=.75,a=1))
unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level();unreal.EditorLevelLibrary.editor_invalidate_viewports()
