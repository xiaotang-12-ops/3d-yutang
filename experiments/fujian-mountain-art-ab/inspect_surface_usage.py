import unreal,pathlib,json
p=unreal.load_asset('/Game/ArtAB/Materials/M_AB_SurfaceBlend')
info={'auto':p.get_editor_property('automatically_set_usage_in_editor'),'enums':[n for n in dir(unreal.MaterialUsage) if n.startswith('MATUSAGE')],'props':{n:str(getattr(p,n)) for n in dir(p) if any(k in n for k in ['usage','used_with','material_attributes','domain'])},'root':str(unreal.MaterialEditingLibrary.get_material_property_input_node(p,unreal.MaterialProperty.MP_MATERIAL_ATTRIBUTES))}
pathlib.Path('D:/3d-research/artifacts/fujian-mountain-art-ab/material-usage.json').write_text(json.dumps(info,indent=2),encoding='utf-8')
# PCG生成是异步原生任务，下一脚本读取实例数与资源。
for a in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors():
 if isinstance(a,unreal.PCGVolume):a.get_components_by_class(unreal.PCGComponent)[0].generate(True)
