import unreal,json,pathlib
le=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
ea=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
assert le.new_level_from_template('/Game/ArtAB/B_Landscape','/Game/ArtAB/Original')
for a in ea.get_all_level_actors():
 if a.get_components_by_class(unreal.ProceduralMeshComponent):ea.destroy_actor(a)
w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
unreal.SystemLibrary.execute_console_command(w,'AB.ImportLandscape')
ls=[a for a in ea.get_all_level_actors() if isinstance(a,unreal.Landscape)]
assert len(ls)==1,ls
l=ls[0]
l.set_editor_property('landscape_material',unreal.load_asset('/Game/ArtAB/Materials/MI_AB_Terrain'))
l.set_editor_property('tags',['ABTerrain'])
assert le.save_current_level()
info={'path':l.get_path_name(),'class':l.get_class().get_path_name(),'bounds':str(l.get_actor_bounds(False)),'location':str(l.get_actor_location()),'scale':str(l.get_actor_scale3d()),'components':len(l.get_components_by_class(unreal.LandscapeComponent)),'material':l.get_editor_property('landscape_material').get_path_name()}
pathlib.Path('D:/3d-research/artifacts/fujian-mountain-art-ab/landscape-import.json').write_text(json.dumps(info,indent=2),encoding='utf-8')
