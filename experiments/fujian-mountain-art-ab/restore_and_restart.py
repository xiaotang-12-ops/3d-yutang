import unreal
ea=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
a=next(a for a in ea.get_all_level_actors() if a.get_components_by_class(unreal.ProceduralMeshComponent))
pm=a.get_components_by_class(unreal.ProceduralMeshComponent)[0]
pm.set_material(0,unreal.load_asset('/Game/ArtAB/Materials/MI_AB_Terrain'))
unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True,True)
unreal.SystemLibrary.quit_editor()
