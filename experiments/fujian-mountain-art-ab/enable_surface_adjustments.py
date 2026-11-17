import unreal,pathlib
ml=unreal.MaterialEditingLibrary
mi=unreal.load_asset('/Game/ArtAB/Materials/MI_AB_Terrain')
for layer in ['Base','Middle','Top']:
 ml.set_material_instance_static_switch_parameter_value(mi,'Use '+layer+' Layer Adjustments',True)
ml.update_material_instance(mi)
unreal.EditorAssetLibrary.save_loaded_asset(mi)
exec(compile(pathlib.Path('D:/3d-research/experiments/fujian-mountain-art-ab/capture_A_preview.py').read_text(),'<preview>','exec'))
