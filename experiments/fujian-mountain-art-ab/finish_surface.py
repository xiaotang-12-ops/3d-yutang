"""通过参数回读核验成熟材质实例，避免把API返回空值误当作写入失败。-codex，2026-10-23"""
import unreal,json,pathlib
OUT=pathlib.Path('D:/3d-research/artifacts/fujian-mountain-art-ab')
lib=unreal.MaterialEditingLibrary;ea=unreal.EditorAssetLibrary
parent=unreal.load_asset('/Game/ArtAB/Materials/M_AB_SurfaceBlend')
mi=unreal.load_asset('/Game/ArtAB/Materials/MI_AB_Terrain');assert mi
lib.set_material_instance_parent(mi,parent)
records={'textureNames':[str(x) for x in lib.get_texture_parameter_names(mi)],'textures':[],'functions':[]}
for n in lib.get_material_expressions(parent):
    if isinstance(n,unreal.MaterialExpressionMaterialFunctionCall):
        records['functions'].append(n.get_editor_property('material_function').get_path_name())
for layer,ident,meters in [('Base','forest_ground_04',3.15),('Middle','rocky_terrain_02',90.0),('Top','aerial_grass_rock',15.0)]:
    maps={}
    for a in unreal.AssetRegistryHelpers.get_asset_registry().get_assets_by_path('/Game/ArtAB/Textures/'+ident):
        n=str(a.asset_name)
        if '_diff_' in n:maps['Albedo']=a.get_asset()
        elif '_nor_' in n:maps['Normal']=a.get_asset()
        elif '_arm_' in n:maps['ARD']=a.get_asset();maps['AO']=a.get_asset()
    for suffix,asset in maps.items():
        name=f'{layer} Layer {suffix} Map';ret=lib.set_material_instance_texture_parameter_value(mi,name,asset)
        actual=lib.get_material_instance_texture_parameter_value(mi,name)
        records['textures'].append({'parameter':name,'requested':asset.get_path_name(),'actual':actual.get_path_name() if actual else None,'setterResult':str(ret)})
    lib.set_material_instance_vector_parameter_value(mi,f'{layer} Layer Tiling/Offset',unreal.LinearColor(r=1/meters,g=1/meters,b=0,a=0))
    lib.set_material_instance_scalar_parameter_value(mi,f'{layer} Layer Roughness Min',.6)
    lib.set_material_instance_scalar_parameter_value(mi,f'{layer} Layer Normal Strength',.65)
    lib.set_material_instance_vector_parameter_value(mi,f'{layer} Layer Albedo Tint',unreal.LinearColor(r=.32,g=.49,b=.22,a=1) if layer=='Base' else unreal.LinearColor(r=.75,g=.78,b=.68,a=1))
ea.save_loaded_asset(mi)
(OUT/'surface-material-bindings.json').write_text(json.dumps(records,indent=2),encoding='utf-8')
assert all(x['requested']==x['actual'] for x in records['textures']),records
