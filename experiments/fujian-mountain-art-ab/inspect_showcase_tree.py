import unreal,json,pathlib
asys=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
a=next(a for a in asys.get_all_level_actors() if a.get_actor_label()=='Showcase_Foliage_island_tree_02_1k')
c=a.get_components_by_class(unreal.HierarchicalInstancedStaticMeshComponent)[0];mesh=c.get_editor_property('static_mesh')
smes=unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
r={'mesh':mesh.get_path_name(),'nanite':str(smes.get_nanite_settings(mesh)),'setdoc':smes.set_nanite_settings.__doc__,'materials':[]}
for i in range(c.get_num_materials()):
 mat=c.get_material(i);r['materials'].append({'slot':i,'path':mat.get_path_name(),'parent':str(mat.get_editor_property('parent')) if isinstance(mat,unreal.MaterialInstanceConstant) else None,'overrides':str(mat.get_editor_property('base_property_overrides')) if isinstance(mat,unreal.MaterialInstanceConstant) else None})
r['overrideDoc']=unreal.MaterialInstanceBasePropertyOverrides.__doc__
pathlib.Path('D:/3d-research/artifacts/fujian-mountain-art-ab/showcase-tree-render.json').write_text(json.dumps(r,indent=2),encoding='utf-8')
