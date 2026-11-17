import unreal,json,pathlib
ml=unreal.MaterialEditingLibrary
mi=unreal.load_asset('/Game/ArtAB/Materials/MI_AB_Terrain')
ml.update_material_instance(mi)
ml.recompile_material(unreal.load_asset('/Game/ArtAB/Materials/M_AB_SurfaceBlend'))
unreal.EditorAssetLibrary.save_loaded_asset(mi)
info={'switches':{str(n):str(ml.get_material_instance_static_switch_parameter_value(mi,n)) for n in ml.get_static_switch_parameter_names(mi)},'scalars':{str(n):str(ml.get_material_instance_scalar_parameter_value(mi,n)) for n in ml.get_scalar_parameter_names(mi)},'vectors':{str(n):str(ml.get_material_instance_vector_parameter_value(mi,n)) for n in ml.get_vector_parameter_names(mi)},'stats':str(ml.get_statistics(unreal.load_asset('/Game/ArtAB/Materials/M_AB_SurfaceBlend')))}
pathlib.Path('D:/3d-research/artifacts/fujian-mountain-art-ab/surface-actual-params.json').write_text(json.dumps(info,indent=2),encoding='utf-8')
exec(compile(pathlib.Path('D:/3d-research/experiments/fujian-mountain-art-ab/capture_A_preview.py').read_text(),'<preview>','exec'))
