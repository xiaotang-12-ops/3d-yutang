import unreal,json,pathlib
ml=unreal.MaterialEditingLibrary
mi=unreal.load_asset('/Game/ArtAB/Materials/MI_AB_Terrain')
r={}
for n in ['Use Base Layer Adjustments','Use Middle Layer Adjustments','Use Top Layer Adjustments']:
 r[n]={'before':ml.get_material_instance_static_switch_parameter_value(mi,n)}
 ml.set_material_instance_static_switch_parameter_value(mi,n,True)
 r[n]['after']=ml.get_material_instance_static_switch_parameter_value(mi,n)
ml.set_material_instance_vector_parameter_value(mi,'Base Layer Albedo Tint',unreal.LinearColor(r=.08,g=.5,b=.06,a=1))
ml.set_material_instance_vector_parameter_value(mi,'Top Layer Albedo Tint',unreal.LinearColor(r=.15,g=.6,b=.08,a=1))
ml.update_material_instance(mi)
unreal.EditorAssetLibrary.save_loaded_asset(mi)
unreal.AutomationLibrary.set_editor_viewport_view_mode(unreal.ViewModeIndex.VMI_LIT)
r['methods']=[x for x in dir(unreal.AutomationLibrary) if any(k in x for k in ['buffer','view_mode'])]
pathlib.Path('D:/3d-research/artifacts/fujian-mountain-art-ab/switch-actual.json').write_text(json.dumps(r,indent=2),encoding='utf-8')
