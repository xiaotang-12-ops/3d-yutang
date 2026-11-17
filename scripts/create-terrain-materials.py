"""仅在 UE 编辑器资产构建期创建地形与辉光材质；独立程序不运行 Python。
材质和高程着色图显式保存在 /Game/Terrain 并纳入 Cook。-codex，2026-10-18
"""
import unreal
import json
from pathlib import Path

lib=unreal.MaterialEditingLibrary
assets=unreal.AssetToolsHelpers.get_asset_tools()
folder='/Game/Terrain'
unreal.EditorAssetLibrary.make_directory(folder)
png=str(Path(unreal.Paths.project_content_dir())/'Data/terrain-color.png')
task=unreal.AssetImportTask()
task.filename=png
task.destination_path=folder
task.destination_name='T_TerrainColor'
task.automated=True
task.replace_existing=True
task.save=True
assets.import_asset_tasks([task])
texture=unreal.load_asset(folder+'/T_TerrainColor')
if not texture:
    raise RuntimeError('Terrain texture import failed')
# ProceduralMesh 没有静态网格的 UV 密度构建数据，默认流送曾只载入 256px。
# 本项目只用一张 2K 底图，固定常驻约 2.7MB，避免远景细节被错误降级。-codex，2026-10-18
texture.set_editor_property('never_stream',True)
unreal.EditorAssetLibrary.save_loaded_asset(texture)

# 边界在实际地形表面渲染，不再使用会穿山的悬浮折线；遮罩必须按线性值读取。
# 与地表一起固定常驻，总纹理预算约 5.4MB。-codex，2026-10-19
task=unreal.AssetImportTask()
task.filename=str(Path(unreal.Paths.project_content_dir())/'Data/terrain-borders.png')
task.destination_path=folder
task.destination_name='T_TerrainBorders'
task.automated=True
task.replace_existing=True
task.save=True
assets.import_asset_tasks([task])
borders=unreal.load_asset(folder+'/T_TerrainBorders')
if not borders:
    raise RuntimeError('Terrain border mask import failed')
borders.set_editor_property('srgb',False)
borders.set_editor_property('compression_settings',unreal.TextureCompressionSettings.TC_MASKS)
borders.set_editor_property('never_stream',True)
unreal.EditorAssetLibrary.save_loaded_asset(borders)

def material(name, unlit=False, additive=False):
    path=folder+'/'+name
    mat=unreal.load_asset(path)
    if not mat:
        mat=assets.create_asset(name,folder,unreal.Material,unreal.MaterialFactoryNew())
    lib.delete_all_material_expressions(mat)
    mat.set_editor_property('two_sided',True)
    mat.set_editor_property('shading_model',unreal.MaterialShadingModel.MSM_UNLIT if unlit else unreal.MaterialShadingModel.MSM_DEFAULT_LIT)
    mat.set_editor_property('blend_mode',unreal.BlendMode.BLEND_ADDITIVE if additive else unreal.BlendMode.BLEND_OPAQUE)
    return mat

def node(mat,kind):
    return lib.create_material_expression(mat,kind,0,0)

def scalar(mat,value):
    n=node(mat,unreal.MaterialExpressionConstant)
    n.r=value
    return n

def color_constant(mat,value):
    n=node(mat,unreal.MaterialExpressionConstant3Vector)
    n.constant=unreal.LinearColor(*value,1.)
    return n

def finish(mat):
    errors=lib.recompile_material(mat)
    # UE 5.8 返回编译错误数组，旧版返回 None；失败时不得写出成功标记。
    if errors:
        raise RuntimeError(f'Material compilation failed: {mat.get_name()}: {list(errors)}')
    unreal.EditorAssetLibrary.save_loaded_asset(mat)

def parameter(mat,name,value):
    n=node(mat,unreal.MaterialExpressionScalarParameter)
    n.set_editor_property('parameter_name',name)
    n.set_editor_property('default_value',value)
    return n

def connect(source,output,target,input_name):
    if not lib.connect_material_expressions(source,output,target,input_name):
        available=list(lib.get_material_expression_input_names(target))
        raise RuntimeError(f'Material input connection failed: {output!r} -> {target.get_class().get_name()} input={input_name!r}; engine inputs={available}')

def property_input(source,output,prop):
    if not lib.connect_material_property(source,output,prop):
        raise RuntimeError(f'Material property connection failed: {prop}')

# 地表接收现有直射光，让真实DEM法线参与山脊/沟谷明暗；不启用Lumen、额外阴影或新灯。
# 底光防止背光坡全黑；边界另走原恒亮自发光链，不受地表受光与降饱和影响。
# SurfaceLightingMix=1为本轮候选，0退回旧0.8倍自然色自发光等效基线，可同机位比较。-codex，2026-10-22
mat=material('M_Terrain')
tex=node(mat,unreal.MaterialExpressionTextureSample)
tex.texture=texture
lighting_mix=node(mat,unreal.MaterialExpressionSaturate)
# UE5.8 MaterialEditingLibrary 使用图节点短名：Input会被缩为空名，传空字符串连接第0输入。
# Desaturation同样受此转换影响；不能直接照搬C++字段名。-codex，2026-10-22
connect(parameter(mat,'SurfaceLightingMix',1.),'',lighting_mix,'')
desaturation=node(mat,unreal.MaterialExpressionDesaturation)
desaturation_fraction=node(mat,unreal.MaterialExpressionMultiply)
connect(lighting_mix,'',desaturation_fraction,'A');connect(scalar(mat,.22),'',desaturation_fraction,'B')
connect(tex,'',desaturation,'');connect(desaturation_fraction,'',desaturation,'Fraction')
surface_color=node(mat,unreal.MaterialExpressionMultiply)
connect(desaturation,'',surface_color,'A');connect(parameter(mat,'SurfaceGain',1.),'',surface_color,'B')
albedo_weight=node(mat,unreal.MaterialExpressionMultiply)
connect(lighting_mix,'',albedo_weight,'A');connect(scalar(mat,.95),'',albedo_weight,'B')
albedo=node(mat,unreal.MaterialExpressionMultiply)
connect(surface_color,'',albedo,'A');connect(albedo_weight,'',albedo,'B')
property_input(albedo,'',unreal.MaterialProperty.MP_BASE_COLOR)
rough=scalar(mat,.93)
property_input(rough,'',unreal.MaterialProperty.MP_ROUGHNESS)
# 地形表面以漫反射为主；mix=0同时关掉高光，保证旧基线不会额外收到灯光。-codex，2026-10-22
specular=node(mat,unreal.MaterialExpressionMultiply)
connect(lighting_mix,'',specular,'A');connect(scalar(mat,.08),'',specular,'B')
property_input(specular,'',unreal.MaterialProperty.MP_SPECULAR)
ambient=node(mat,unreal.MaterialExpressionMultiply)
ambient_weight=node(mat,unreal.MaterialExpressionLinearInterpolate)
connect(scalar(mat,.8),'',ambient_weight,'A');connect(scalar(mat,.35),'',ambient_weight,'B')
connect(lighting_mix,'',ambient_weight,'Alpha')
connect(surface_color,'',ambient,'A');connect(ambient_weight,'',ambient,'B')
mask=node(mat,unreal.MaterialExpressionTextureSample)
mask.texture=borders
mask.sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_MASKS
city_edge=node(mat,unreal.MaterialExpressionMultiply)
connect(mask,'R',city_edge,'A');connect(color_constant(mat,(.054,.54,.648)),'',city_edge,'B')
province_edge=node(mat,unreal.MaterialExpressionMultiply)
connect(mask,'G',province_edge,'A');connect(color_constant(mat,(.018,.52,.72)),'',province_edge,'B')
edge=node(mat,unreal.MaterialExpressionAdd)
connect(city_edge,'',edge,'A');connect(province_edge,'',edge,'B')
surface=node(mat,unreal.MaterialExpressionAdd)
edge_gain=node(mat,unreal.MaterialExpressionMultiply)
connect(edge,'',edge_gain,'A');connect(parameter(mat,'BoundaryGain',1.),'',edge_gain,'B')
connect(ambient,'',surface,'A');connect(edge_gain,'',surface,'B')
property_input(surface,'',unreal.MaterialProperty.MP_EMISSIVE_COLOR)
finish(mat)

# 底座轮廓恒亮并写入深度；取消 Time/Sine 及加法叠亮，避免主动呼吸和重叠亮度跳动。
# 顶部边界已合入 M_Terrain，M_Glow 仅服务底座线/网格。-codex，2026-10-19
mat=material('M_Glow',True)
color=node(mat,unreal.MaterialExpressionVertexColor)
strength=node(mat,unreal.MaterialExpressionScalarParameter)
strength.set_editor_property('parameter_name','Glow')
strength.set_editor_property('default_value',4.)
product=node(mat,unreal.MaterialExpressionMultiply)
connect(color,'',product,'A')
connect(strength,'',product,'B')
property_input(product,'',unreal.MaterialProperty.MP_EMISSIVE_COLOR)
finish(mat)

mat=material('M_Stage',True)
color=node(mat,unreal.MaterialExpressionVertexColor)
property_input(color,'',unreal.MaterialProperty.MP_EMISSIVE_COLOR)
finish(mat)
unreal.log('TERRAIN_ASSETS_READY: M_Terrain, M_Glow, M_Stage, T_TerrainColor, T_TerrainBorders; stable surface boundaries; DefaultLit SurfaceLightingMix=1, ambient=0.35, desaturation=0.22')

# 开发用数学地形共享基础材质。保留已有的定制省域材质。-codex，2026-11-17
source = Path(unreal.Paths.project_content_dir()) / 'Data/terrain-source.json'
if source.exists() and json.loads(source.read_text('utf-8')).get('url') == 'offline':
    for original, alias in [('M_Terrain', 'M_ProvinceTerrain'), ('M_Stage', 'M_ProvinceContext')]:
        if not unreal.EditorAssetLibrary.does_asset_exist(folder + '/' + alias):
            asset = unreal.EditorAssetLibrary.duplicate_asset(folder + '/' + original, folder + '/' + alias)
            if not asset or not unreal.EditorAssetLibrary.save_loaded_asset(asset):
                raise RuntimeError('无法保存演示材质 ' + alias)
