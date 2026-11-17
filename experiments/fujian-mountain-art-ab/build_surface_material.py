"""复用Bridge三层成熟材质与全部既有层函数；只增加世界坐标/地理遮罩桥接。-codex，2026-10-23"""
import unreal,pathlib,json
ROOT=pathlib.Path('D:/3d-research/experiments/fujian-mountain-art-ab')
OUT=pathlib.Path('D:/3d-research/artifacts/fujian-mountain-art-ab')
lib=unreal.MaterialEditingLibrary;at=unreal.AssetToolsHelpers.get_asset_tools();ea=unreal.EditorAssetLibrary
folder='/Game/ArtAB/Materials'
ea.make_directory(folder)
parent=unreal.load_asset(folder+'/M_AB_SurfaceBlend') or ea.duplicate_asset('/Bridge/MSPresets/M_MS_SurfaceBlend_Material/M_MS_SurfaceBlend_Material',folder+'/M_AB_SurfaceBlend')
assert parent
def expr(o,cls):
    if isinstance(o,unreal.Material):return lib.create_material_expression(o,cls,-600,-300)
    return lib.create_material_expression_in_function(o,cls,-600,-300)
def connect(a,out,b,pin):
    assert lib.connect_material_expressions(a,out,b,pin), (a.get_name(),b.get_name(),pin)
def nodes(o):return list(lib.get_material_expressions(o)) if isinstance(o,unreal.Material) else list(lib.get_material_function_expressions(o))
def rewire(o,old,new,output=''):
    for dest in nodes(o):
        inputs=lib.get_inputs_for_material_expression(o,dest) if isinstance(o,unreal.Material) else lib.get_inputs_for_material_function_expression(o,dest)
        names=lib.get_material_expression_input_names(dest)
        for pin,src in zip(names,inputs):
            if src==old:connect(new,output,dest,str(pin) if str(pin)!='None' else '')
reused=[]
for layer in ['Base','Middle','Top']:
    src=f'/Bridge/MSPresets/M_MS_SurfaceBlend_Material/LayerFunctions/{layer}/MF_{layer}LayerTextures'
    f=unreal.load_asset(folder+'/MF_AB_'+layer+'Textures') or ea.duplicate_asset(src,folder+'/MF_AB_'+layer+'Textures');assert f
    coord=next(n for n in nodes(f) if isinstance(n,unreal.MaterialExpressionTextureCoordinate))
    wp=expr(f,unreal.MaterialExpressionWorldPosition)
    xy=expr(f,unreal.MaterialExpressionComponentMask);xy.set_editor_property('r',True);xy.set_editor_property('g',True);xy.set_editor_property('b',False)
    connect(wp,'',xy,'')
    div=expr(f,unreal.MaterialExpressionDivide);div.set_editor_property('const_b',100.0);connect(xy,'',div,'A')
    rewire(f,coord,div)
    lib.update_material_function(f);ea.save_loaded_asset(f)
    for n in nodes(parent):
        if isinstance(n,unreal.MaterialExpressionMaterialFunctionCall):
            current=n.get_editor_property('material_function')
            if current and current.get_name()==f'MF_{layer}LayerTextures': n.set_editor_property('material_function',f)
    reused.append({'source':src,'copy':f.get_path_name(),'adaptation':'UV0 replaced by world XY in meters; existing sampling/PBR/normal/adjustment logic retained'})
# 将原VertexColor替换成同一地理遮罩，适用于PMC与Landscape，避免B另用一套着色规则。
task=unreal.AssetImportTask();task.filename=str(ROOT/'region-mask.png');task.destination_path=folder;task.destination_name='T_AB_RegionMask';task.automated=True;task.save=True;at.import_asset_tasks([task])
mask=unreal.load_asset(folder+'/T_AB_RegionMask');mask.set_editor_property('srgb',False);mask.set_editor_property('compression_settings',unreal.TextureCompressionSettings.TC_MASKS);mask.set_editor_property('address_x',unreal.TextureAddress.TA_CLAMP);mask.set_editor_property('address_y',unreal.TextureAddress.TA_CLAMP);ea.save_loaded_asset(mask)
sampler=expr(parent,unreal.MaterialExpressionTextureSampleParameter2D);sampler.set_editor_property('parameter_name','AB Geographic Biome Mask');sampler.texture=mask;sampler.set_editor_property('sampler_type',unreal.MaterialSamplerType.SAMPLERTYPE_MASKS)
wp=expr(parent,unreal.MaterialExpressionWorldPosition)
xy=expr(parent,unreal.MaterialExpressionComponentMask);xy.set_editor_property('r',True);xy.set_editor_property('g',True);xy.set_editor_property('b',False);connect(wp,'',xy,'')
meta=json.loads((OUT/'sample-region.json').read_text());nx,ny=[v*100 for v in meta['groundMeters']]
# 纹理列=X北、行=Y东，原点西南；与R16高度图的行列约定相同。
size=expr(parent,unreal.MaterialExpressionConstant2Vector);size.set_editor_property('r',nx);size.set_editor_property('g',ny)
uv=expr(parent,unreal.MaterialExpressionDivide);connect(xy,'',uv,'A');connect(size,'',uv,'B')
offset=expr(parent,unreal.MaterialExpressionAdd);offset.set_editor_property('const_b',.5);connect(uv,'',offset,'A');connect(offset,'',sampler,'UVs')
vertex=next(n for n in nodes(parent) if isinstance(n,unreal.MaterialExpressionVertexColor))
# 输出名逐一保留R/G/B/A，避免把向量误接到标量层权重。
for dest in nodes(parent):
    names=lib.get_material_expression_input_names(dest);inputs=lib.get_inputs_for_material_expression(parent,dest)
    for pin,src in zip(names,inputs):
        if src==vertex:
            output=lib.get_input_node_output_name_for_material_expression(dest,vertex)
            connect(sampler,output or '',dest,str(pin) if str(pin)!='None' else '')
errors=lib.recompile_material(parent)
assert not errors, str(errors)
ea.save_loaded_asset(parent)
mi=at.create_asset('MI_AB_Terrain',folder,unreal.MaterialInstanceConstant,unreal.MaterialInstanceConstantFactoryNew());lib.set_material_instance_parent(mi,parent)
settings=[('Base','forest_ground_04',3.15),('Middle','rocky_terrain_02',90.0),('Top','aerial_grass_rock',15.0)]
bindings=[]
for layer,ident,meters in settings:
    assets=unreal.AssetRegistryHelpers.get_asset_registry().get_assets_by_path('/Game/ArtAB/Textures/'+ident)
    maps={}
    for a in assets:
        n=str(a.asset_name);asset=a.get_asset()
        if '_diff_' in n:maps['Albedo']=asset
        elif '_nor_' in n:maps['Normal']=asset
        elif '_arm_' in n:maps['ARD']=asset;maps['AO']=asset
    for suffix,asset in maps.items():
        assert lib.set_material_instance_texture_parameter_value(mi,f'{layer} Layer {suffix} Map',asset)
        bindings.append({'parameter':f'{layer} Layer {suffix} Map','asset':asset.get_path_name()})
    lib.set_material_instance_vector_parameter_value(mi,f'{layer} Layer Tiling/Offset',unreal.LinearColor(1/meters,1/meters,0,0))
    lib.set_material_instance_scalar_parameter_value(mi,f'{layer} Layer Roughness Min',.6)
    lib.set_material_instance_scalar_parameter_value(mi,f'{layer} Layer Normal Strength',.65)
    lib.set_material_instance_vector_parameter_value(mi,f'{layer} Layer Albedo Tint',unreal.LinearColor(.32,.49,.22,1) if layer=='Base' else unreal.LinearColor(.75,.78,.68,1))
ea.save_loaded_asset(mi)
(OUT/'surface-material-bindings.json').write_text(json.dumps({'parent':parent.get_path_name(),'instance':mi.get_path_name(),'reusedFunctions':reused,'textures':bindings,'mask':mask.get_path_name()},indent=2),encoding='utf-8')
