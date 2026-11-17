import unreal,json,pathlib
ml=unreal.MaterialEditingLibrary;ea=unreal.EditorAssetLibrary;asys=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
mi=unreal.load_asset('/Game/ArtAB/Materials/MI_Mountain_Showcase')
t=unreal.AssetImportTask();t.filename='D:/3d-research/experiments/fujian-mountain-art-ab/showcase-biome-mask.png';t.destination_path='/Game/ArtAB/Materials';t.destination_name='T_Showcase_BiomeMask';t.automated=True;t.save=True
unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([t])
mask=unreal.load_asset('/Game/ArtAB/Materials/T_Showcase_BiomeMask');mask.set_editor_property('srgb',False);mask.set_editor_property('compression_settings',unreal.TextureCompressionSettings.TC_MASKS);mask.set_editor_property('address_x',unreal.TextureAddress.TA_CLAMP);mask.set_editor_property('address_y',unreal.TextureAddress.TA_CLAMP)
ml.set_material_instance_texture_parameter_value(mi,'AB Geographic Biome Mask',mask)
ml.set_material_instance_vector_parameter_value(mi,'Top Layer Albedo Tint',unreal.LinearColor(r=.38,g=.85,b=.28,a=1))
ml.set_material_instance_vector_parameter_value(mi,'Base Layer Albedo Tint',unreal.LinearColor(r=.8,g=.95,b=.7,a=1))
ml.update_material_instance(mi);ea.save_loaded_asset(mask);ea.save_loaded_asset(mi)
for a in asys.get_all_level_actors():
 if isinstance(a,unreal.SkyLight):a.light_component.set_editor_property('intensity',20000.0)
 if isinstance(a,unreal.DirectionalLight):a.light_component.set_editor_property('temperature',6500.0)
if not any(isinstance(a,unreal.ExponentialHeightFog) for a in asys.get_all_level_actors()):
 fog=asys.spawn_actor_from_class(unreal.ExponentialHeightFog,unreal.Vector(0,0,7000));fog.set_actor_label('Showcase_Valley_DistanceHaze')
 c=fog.get_components_by_class(unreal.ExponentialHeightFogComponent)[0];c.set_editor_property('fog_density',.0015);c.set_editor_property('fog_height_falloff',.15);c.set_editor_property('start_distance',20000.0);c.set_editor_property('fog_max_opacity',.45)
r={};parent=unreal.load_asset('/Game/ArtAB/Materials/M_AB_SurfaceBlend')
for n in ml.get_material_expressions(parent):
 if isinstance(n,unreal.MaterialExpressionMaterialFunctionCall) and 'MaterialBlend' in n.get_editor_property('material_function').get_name():
  r[n.get_name()]=[{'pin':str(p),'src':s.get_name() if s else None,'output':ml.get_input_node_output_name_for_material_expression(n,s) if s else None} for p,s in zip(ml.get_material_expression_input_names(n),ml.get_inputs_for_material_expression(parent,n))]
pathlib.Path('D:/3d-research/artifacts/fujian-mountain-art-ab/showcase-biome-bindings.json').write_text(json.dumps(r,indent=2),encoding='utf-8')
unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
