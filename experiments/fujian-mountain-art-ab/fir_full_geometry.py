import unreal,json,pathlib
asys=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
a=next(a for a in asys.get_all_level_actors() if a.get_actor_label()=='Showcase_Fir_Hero')
src=a.static_mesh_component.get_editor_property('static_mesh')
m=unreal.EditorAssetLibrary.duplicate_asset(src.get_path_name(),'/Game/ArtAB/Nature/SM_Showcase_Fir_Full')
assert m
sm=unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem);s=sm.get_nanite_settings(m);s.set_editor_property('enabled',False);sm.set_nanite_settings(m,s,True)
a.static_mesh_component.set_static_mesh(m);unreal.EditorAssetLibrary.save_loaded_asset(m)
unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level();unreal.EditorLevelLibrary.editor_invalidate_viewports()
pathlib.Path('D:/3d-research/artifacts/fujian-mountain-art-ab/fir-full-geometry.json').write_text(json.dumps({'mesh':m.get_path_name(),'vertices':sm.get_number_verts(m,0),'nanite':str(sm.get_nanite_settings(m))}),encoding='utf-8')
