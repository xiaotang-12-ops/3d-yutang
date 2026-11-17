import unreal,json,pathlib
asys=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
a=next(a for a in asys.get_all_level_actors() if a.get_actor_label()=='Showcase_Mature_Fir_Forest')
c=a.get_components_by_class(unreal.HierarchicalInstancedStaticMeshComponent)[0]
m=c.get_editor_property('static_mesh');sm=unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
r={'visible':c.is_visible(),'bounds':str(c.get_local_bounds()),'nanite':str(sm.get_nanite_settings(m)),'instances':c.get_instance_count(),'transforms':[str(c.get_instance_transform(i,True)) for i in [0,100,200,300]],'materials':[]}
for i in range(c.get_num_materials()):
 mat=c.get_material(i);r['materials'].append({'name':mat.get_path_name(),'parent':str(mat.get_editor_property('parent')),'textures':str(mat.get_editor_property('texture_parameter_values')),'scalars':str(mat.get_editor_property('scalar_parameter_values')),'overrides':str(mat.get_editor_property('base_property_overrides'))})
pathlib.Path('D:/3d-research/artifacts/fujian-mountain-art-ab/fir-native-state.json').write_text(json.dumps(r,indent=2),encoding='utf-8')
