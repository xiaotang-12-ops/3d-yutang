import unreal,json,pathlib
ea=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
info={'instances':[],'graph':[]}
for a in ea.get_all_level_actors():
 for c in a.get_components_by_class(unreal.InstancedStaticMeshComponent):
  info['instances'].append({'actor':a.get_actor_label(),'mesh':c.static_mesh.get_path_name() if c.static_mesh else None,'count':c.get_instance_count()})
 for c in a.get_components_by_class(unreal.PCGComponent):info['graph'].append({'actor':a.get_actor_label(),'generated':c.generated,'bounds':str(a.get_actor_bounds(False))})
ml=unreal.MaterialEditingLibrary;p=unreal.load_asset('/Game/ArtAB/Materials/M_AB_SurfaceBlend');root=ml.get_material_property_input_node(p,unreal.MaterialProperty.MP_MATERIAL_ATTRIBUTES)
info['root']={'name':root.get_name(),'parameter':str(root.get_editor_property('parameter_name')),'inputnames':[str(n) for n in ml.get_material_expression_input_names(root)],'inputs':[str(n) for n in ml.get_inputs_for_material_expression(p,root)]}
info['automationmethods']=[n for n in dir(unreal.AutomationLibrary) if 'view' in n or 'mode' in n]
pathlib.Path('D:/3d-research/artifacts/fujian-mountain-art-ab/pcg-generated-and-root.json').write_text(json.dumps(info,indent=2),encoding='utf-8')
exec(compile(pathlib.Path('D:/3d-research/experiments/fujian-mountain-art-ab/capture_B_preview.py').read_text(),'<preview>','exec'))
