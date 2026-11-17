import unreal,json,pathlib
asys=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
a=next(a for a in asys.get_all_level_actors() if a.get_actor_label()=='Showcase_Foliage_island_tree_02_1k')
c=a.get_components_by_class(unreal.HierarchicalInstancedStaticMeshComponent)[0];m=c.get_editor_property('static_mesh')
m.set_material(1,unreal.load_asset('/Game/ArtAB/Materials/MI_Showcase_Leaves'))
sm=unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem);s=sm.get_nanite_settings(m)
et=type(s.get_editor_property('fallback_target'));en=next(n for n in dir(et) if 'PERCENT' in n)
s.set_editor_property('fallback_target',getattr(et,en));s.set_editor_property('fallback_percent_triangles',.06);s.set_editor_property('fallback_relative_error',0.0)
sm.set_nanite_settings(m,s,True)
unreal.EditorAssetLibrary.save_loaded_asset(m)
r={'settings':str(sm.get_nanite_settings(m)),'verticesLOD0':sm.get_number_verts(m,0)}
# 显式白昼环境贴图填补动态阴影；避免等待未更新的实时天空捕获。
for a in asys.get_all_level_actors():
 if isinstance(a,unreal.SkyLight):
  sc=a.light_component
  sc.set_editor_property('source_type',unreal.SkyLightSourceType.SLS_SPECIFIED_CUBEMAP)
  sc.set_editor_property('cubemap',unreal.load_asset('/Engine/MapTemplates/Sky/DaylightAmbientCubemap'))
  sc.set_editor_property('intensity',3000.0);sc.recapture_sky()
pathlib.Path('D:/3d-research/artifacts/fujian-mountain-art-ab/showcase-fallback-rebuild.json').write_text(json.dumps(r,indent=2),encoding='utf-8')
unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
