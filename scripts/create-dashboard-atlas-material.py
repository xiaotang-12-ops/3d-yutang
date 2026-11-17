"""主控导入真实Atlas纹理后，仅新建独立B屏顶面UI候选；拒绝覆盖任何同名材质。
全环角度/径向采样保留真实份额与程序厚度，纹理仅是静态美术细节，不是物理反射。-codex，2026-10-23
"""
from pathlib import Path
import unreal

content = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_content_dir())).resolve()
expected = Path('D:/3d-research/unreal/FujianSandbox58/Content').resolve()
if str(content).casefold() != str(expected).casefold() or not unreal.SystemLibrary.get_engine_version().startswith('5.8.'):
    raise RuntimeError('PondAtlas requires designated FujianSandbox58 project and UE5.8')
asset_path = '/Game/Dashboard/M_PondGlassAtlas'
if unreal.EditorAssetLibrary.does_asset_exist(asset_path):
    raise RuntimeError('Refusing existing material overwrite: ' + asset_path)
atlas = unreal.load_asset('/Game/Dashboard/T_PondRingSurfaceAtlas')
if not isinstance(atlas, unreal.Texture2D):
    raise RuntimeError('Import real /Game/Dashboard/T_PondRingSurfaceAtlas Texture2D first; no placeholder will be created')
if atlas.get_editor_property('srgb') or atlas.get_editor_property('address_x') != unreal.TextureAddress.TA_WRAP or atlas.get_editor_property('address_y') != unreal.TextureAddress.TA_CLAMP:
    raise RuntimeError('Atlas import contract: SRGB=false, AddressX=Wrap, AddressY=Clamp; texture will not be rewritten')
if atlas.get_editor_property('virtual_texture_streaming'):
    raise RuntimeError('Atlas must use regular Texture2D sampling, not virtual texture streaming')
compression = atlas.get_editor_property('compression_settings')
samplers = {
    unreal.TextureCompressionSettings.TC_DEFAULT: unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR,
    unreal.TextureCompressionSettings.TC_EDITOR_ICON: unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR,
    unreal.TextureCompressionSettings.TC_GRAYSCALE: unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_GRAYSCALE,
    unreal.TextureCompressionSettings.TC_MASKS: unreal.MaterialSamplerType.SAMPLERTYPE_MASKS,
}
if compression not in samplers:
    raise RuntimeError('Atlas requires Default/UI/Grayscale/Masks compression; texture will not be rewritten')

lib = unreal.MaterialEditingLibrary
mat = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
    'M_PondGlassAtlas', '/Game/Dashboard', unreal.Material, unreal.MaterialFactoryNew())
if not mat:
    raise RuntimeError('PondAtlas material creation failed')
mat.set_editor_property('material_domain', unreal.MaterialDomain.MD_UI)
mat.set_editor_property('blend_mode', unreal.BlendMode.BLEND_TRANSLUCENT)
mat.set_editor_property('two_sided', True)


def node(kind):
    return lib.create_material_expression(mat, kind, 0, 0)


def connect(source, output, target, input_name):
    if not lib.connect_material_expressions(source, output, target, input_name):
        raise RuntimeError('PondAtlas connection failed: ' + input_name)


def custom(inputs, output_type, code, description):
    result = node(unreal.MaterialExpressionCustom)
    entries = []
    for name in inputs:
        entry = unreal.CustomInput()
        entry.set_editor_property('input_name', name)
        entries.append(entry)
    result.set_editor_property('inputs', entries)
    result.set_editor_property('output_type', output_type)
    result.set_editor_property('description', description)
    result.set_editor_property('code', code)
    return result


# ST_Custom的UV0保留归一环平面xy；逐像素atan2不受分段份额或顶点角度接缝插值影响。-codex，2026-10-23
plane = node(unreal.MaterialExpressionTextureCoordinate)
plane.set_editor_property('coordinate_index', 0)
mapping = custom(('Plane',), unreal.CustomMaterialOutputType.CMOT_FLOAT2, r'''
float Angle = atan2(Plane.y, Plane.x);
float U = frac(Angle * 0.15915494309189535 + 0.5);
float V = saturate((length(Plane) - 0.68) / 0.32);
return float2(U,V);
''', 'Whole-circle angle: -PI..PI -> 0..1; radius inner .68 -> 0, outer 1 -> 1')
connect(plane, '', mapping, 'Plane')
sample = node(unreal.MaterialExpressionTextureSampleParameter2D)
sample.set_editor_property('parameter_name', 'RingSurfaceAtlas')
sample.set_editor_property('texture', atlas)
sample.set_editor_property('sampler_type', samplers[compression])
connect(mapping, '', sample, 'UVs')
vertex = node(unreal.MaterialExpressionVertexColor)
shade = custom(('AtlasValue', 'AtlasUV', 'Tint', 'Fade'), unreal.CustomMaterialOutputType.CMOT_FLOAT4, r'''
// 接缝两侧各4%退回同一中性底色，只是遮缝处理，不代表原素材已经无缝。-codex，2026-10-23
float SeamWeight = smoothstep(0.0,0.04,min(AtlasUV.x,1.0-AtlasUV.x));
float Detail = lerp(0.5,saturate(AtlasValue),SeamWeight);
float Highlight = pow(saturate((Detail-0.55)/0.45),2.0) * 0.18;
float3 Body = saturate(Tint) * (0.56 + 0.32 * Detail);
float3 FinalColor = lerp(Body,float3(0.65,0.84,0.95),Highlight);
return float4(saturate(FinalColor),0.91*saturate(Fade));
''', 'Neutral atlas detail preserves species tint; bounded highlight; single vertex Fade; no time/physical reflection')
# 原图有轻微偏色，只读R作为中性细节，避免把素材色相带入品种颜色。-codex，2026-10-23
connect(sample, 'R', shade, 'AtlasValue')
connect(mapping, '', shade, 'AtlasUV')
connect(vertex, '', shade, 'Tint')
connect(vertex, 'A', shade, 'Fade')
for prop, channels in ((unreal.MaterialProperty.MP_EMISSIVE_COLOR, (True, True, True, False)),
                       (unreal.MaterialProperty.MP_OPACITY, (False, False, False, True))):
    mask = node(unreal.MaterialExpressionComponentMask)
    for name, enabled in zip(('r', 'g', 'b', 'a'), channels):
        mask.set_editor_property(name, enabled)
    connect(shade, '', mask, '')
    if not lib.connect_material_property(mask, '', prop):
        raise RuntimeError('PondAtlas output connection failed: ' + str(prop))
errors = lib.recompile_material(mat)
if errors:
    raise RuntimeError('PondAtlas material compilation failed: ' + str(list(errors)))
if not unreal.EditorAssetLibrary.save_loaded_asset(mat):
    raise RuntimeError('PondAtlas material save failed')
unreal.log('POND_ATLAS_MATERIAL_READY: UI/Translucent; purple top only; UV0 global atan2/radius; seam fallback <=4% each end; Fade once; old assets untouched')
