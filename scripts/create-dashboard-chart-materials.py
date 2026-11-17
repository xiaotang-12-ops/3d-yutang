"""主控在指定UE5.8工程运行：创建单紫段3D实验Surface与RT合成材质。
不修改既有UI候选或A资产；不是自动扩面，也不修改全局反射。-codex，2026-10-22
"""
from pathlib import Path
import unreal

content = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_content_dir())).resolve()
if str(content).casefold() != str(Path('D:/3d-research/unreal/FujianSandbox58/Content').resolve()).casefold() or not unreal.SystemLibrary.get_engine_version().startswith('5.8.'):
    raise RuntimeError('Requires designated FujianSandbox58 project and UE5.8')
lib = unreal.MaterialEditingLibrary
folder = '/Game/Dashboard'
unreal.EditorAssetLibrary.make_directory(folder)


def material(name, domain):
    path = folder + '/' + name
    result = unreal.load_asset(path)
    if result and (not isinstance(result, unreal.Material) or result.get_editor_property('material_domain') != domain):
        raise RuntimeError('Refusing incompatible asset overwrite: ' + path)
    if not result:
        result = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, folder, unreal.Material, unreal.MaterialFactoryNew())
    if not result:
        raise RuntimeError('Material creation failed: ' + path)
    result.set_editor_property('material_domain', domain)
    result.set_editor_property('blend_mode', unreal.BlendMode.BLEND_TRANSLUCENT)
    result.set_editor_property('two_sided', False)
    lib.delete_all_material_expressions(result)
    return result


def property_link(mat, expression, output, prop):
    if not lib.connect_material_property(expression, output, prop):
        raise RuntimeError('Material property connection failed: ' + str(prop))


def link(source, output, target, input_name):
    if not lib.connect_material_expressions(source, output, target, input_name):
        raise RuntimeError('Material input connection failed: ' + input_name)


surface = material('M_PondChartSurface', unreal.MaterialDomain.MD_SURFACE)
surface.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_DEFAULT_LIT)
surface.set_editor_property('translucency_lighting_mode', unreal.TranslucencyLightingMode.TLM_SURFACE_PER_PIXEL_LIGHTING)
color = lib.create_material_expression(surface, unreal.MaterialExpressionConstant3Vector, 0, 0)
# 与原紫段相同的sRGB底色换成线性颜色；材质受真实几何法线与局部灯光驱动。-codex，2026-10-22
color.set_editor_property('constant', unreal.LinearColor(0.1946, 0.1651, 0.9131, 1.0))
property_link(surface, color, '', unreal.MaterialProperty.MP_BASE_COLOR)
for prop, value in ((unreal.MaterialProperty.MP_ROUGHNESS, 0.16),
                    (unreal.MaterialProperty.MP_SPECULAR, 0.6),
                    (unreal.MaterialProperty.MP_OPACITY, 0.92)):
    scalar = lib.create_material_expression(surface, unreal.MaterialExpressionConstant, 0, 0)
    scalar.set_editor_property('r', value)
    property_link(surface, scalar, '', prop)

composite = material('M_PondChartComposite', unreal.MaterialDomain.MD_UI)
sample = lib.create_material_expression(composite, unreal.MaterialExpressionTextureSampleParameter2D, 0, 0)
sample.set_editor_property('parameter_name', 'ChartCapture')
placeholder = unreal.load_asset('/Engine/EngineResources/WhiteSquareTexture')
if not placeholder:
    raise RuntimeError('Engine white texture missing; refusing unbound texture parameter')
sample.set_editor_property('texture', placeholder)
vertex = lib.create_material_expression(composite, unreal.MaterialExpressionVertexColor, 0, 0)
combine = lib.create_material_expression(composite, unreal.MaterialExpressionCustom, 0, 0)
combine.set_editor_property('description', 'Static SceneColorHDR capture: inverse opacity, unpremultiply and fixed HDR compression; no clock')
combine.set_editor_property('output_type', unreal.CustomMaterialOutputType.CMOT_FLOAT4)
inputs = []
for name in ('SceneRGB', 'InvOpacity', 'Fade'):
    item = unreal.CustomInput()
    item.set_editor_property('input_name', name)
    inputs.append(item)
combine.set_editor_property('inputs', inputs)
combine.set_editor_property('code', '''
float coverage = saturate(1.0 - InvOpacity);
float3 straight = max(SceneRGB, 0.0) / max(coverage, 0.0001);
float3 mapped = straight / (1.0 + straight);
return float4(mapped, coverage * saturate(Fade));
''')
link(sample, 'RGB', combine, 'SceneRGB')
link(sample, 'A', combine, 'InvOpacity')
link(vertex, 'A', combine, 'Fade')
for prop, channels in ((unreal.MaterialProperty.MP_EMISSIVE_COLOR, (True, True, True, False)),
                       (unreal.MaterialProperty.MP_OPACITY, (False, False, False, True))):
    mask = lib.create_material_expression(composite, unreal.MaterialExpressionComponentMask, 0, 0)
    for name, enabled in zip(('r', 'g', 'b', 'a'), channels):
        mask.set_editor_property(name, enabled)
    link(combine, '', mask, '')
    property_link(composite, mask, '', prop)
for mat in (surface, composite):
    errors = lib.recompile_material(mat)
    if errors:
        raise RuntimeError('Compilation failed for ' + mat.get_name() + ': ' + str(list(errors)))
    if not unreal.EditorAssetLibrary.save_loaded_asset(mat):
        raise RuntimeError('Save failed: ' + mat.get_name())
unreal.log('POND_CHART_3D_ASSETS_READY: Surface ForwardShading + UI inverse-opacity composite; static 512 experiment only')
