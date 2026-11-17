"""A保留原版程序网格，只绑定成熟材质实例；同一机位与光照随模板复制。-codex，2026-10-23"""
import unreal,pathlib,json
ls=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
ls.eject_pilot_level_actor()
assert ls.new_level_from_template('/Game/ArtAB/A_Procedural','/Game/ArtAB/Original')
es=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
allactors=es.get_all_level_actors()
terrain=next(a for a in allactors if a.get_actor_label()=='Original_CPP_ProceduralMesh_SameData')
pm=terrain.get_component_by_class(unreal.ProceduralMeshComponent)
assert pm and pm.get_num_sections()==1, 'Original PMC did not survive save/reopen'
terrain.set_actor_label('A_CPP_ProceduralMesh_SameData')
pm.set_material(0,unreal.load_asset('/Game/ArtAB/Materials/MI_AB_Terrain'))
assert ls.save_current_level()
unreal.EditorLevelLibrary.editor_invalidate_viewports()
pathlib.Path('D:/3d-research/artifacts/fujian-mountain-art-ab/A-initial-bindings.json').write_text(json.dumps({'terrain':terrain.get_path_name(),'component':pm.get_path_name(),'material':pm.get_material(0).get_path_name(),'sections':pm.get_num_sections()},indent=2),encoding='utf-8')
