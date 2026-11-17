"""仅由主控在指定5.8工程执行；本批5个新材质，不改旧候选和纹理。
薄透明为艺术化晶体而非物理实体玻璃；几何/照明/曝光仍由Capture持有。-codex，2026-10-23
"""
from pathlib import Path
import unreal

ROOT = Path('D:/3d-research/unreal/FujianSandbox58/Content').resolve()
content = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_content_dir())).resolve()
if str(content).casefold() != str(ROOT).casefold() or not unreal.SystemLibrary.get_engine_version().startswith('5.8.'):
    raise RuntimeError('Requires designated FujianSandbox58 and UE5.8.x')
lib = unreal.MaterialEditingLibrary
folder = '/Game/Dashboard'
names = ('M_PondChartRGBSurface', 'M_PondChartRGBComposite', 'M_PondChartRGBBackground',
         'M_PondChartRGBStage', 'M_PondChartRGBEdge')
marker = 'POND_RGB_STATIC_20261023_V1'
# 类/枚举由本机5.8反射确认，缺失即停止，不能把默认Lit悄悄冒充ThinTranslucent。
for name in ('MaterialExpressionThinTranslucentMaterialOutput', 'MaterialExpressionCustom', 'CustomInput'):
    if not hasattr(unreal, name):
        raise RuntimeError('Required UE5.8 reflected class unavailable: ' + name)
if not hasattr(unreal.MaterialShadingModel, 'MSM_THIN_TRANSLUCENT'):
    raise RuntimeError('ThinTranslucent enum unavailable')
if not hasattr(unreal, 'RefractionMode') or not hasattr(unreal.RefractionMode, 'RM_INDEX_OF_REFRACTION'):
    raise RuntimeError('Index-of-refraction enum unavailable')
for name in names:
    old = unreal.load_asset(folder + '/' + name)
    if old and (not isinstance(old, unreal.Material) or unreal.EditorAssetLibrary.get_metadata_tag(old, 'PondRGBBatch') != marker):
        raise RuntimeError('Refusing existing asset outside this batch: ' + name)
stage_texture = unreal.load_asset('/Game/Dashboard/T_PondStageEnvironment')
if not isinstance(stage_texture, unreal.Texture2D):
    raise RuntimeError('Existing stage texture required; import it with approved existing importer first')


def mat(name, ui=False, translucent=False):
    result = unreal.load_asset(folder + '/' + name)
    if not result:
        result = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, folder, unreal.Material, unreal.MaterialFactoryNew())
    if not result:
        raise RuntimeError('Create failed: ' + name)
    unreal.EditorAssetLibrary.set_metadata_tag(result, 'PondRGBBatch', marker)
    result.set_editor_property('material_domain', unreal.MaterialDomain.MD_UI if ui else unreal.MaterialDomain.MD_SURFACE)
    result.set_editor_property('blend_mode', unreal.BlendMode.BLEND_TRANSLUCENT if translucent else unreal.BlendMode.BLEND_OPAQUE)
    result.set_editor_property('two_sided', False)
    lib.delete_all_material_expressions(result)
    return result


def node(m, cls):
    return lib.create_material_expression(m, cls, 0, 0)


def output_name(expression, requested):
    available = [str(name) for name in lib.get_material_expression_output_names(expression)]
    if not available:
        raise RuntimeError('Expression has no reflected outputs: ' + expression.get_class().get_name())
    # MaterialEditingLibrary空名明确选择第0输出；VertexColor的RGB掩码输出名为空，
    # 而TextureSample显式命名RGB/A。只对已核源码且反射布局吻合的VertexColor解析此别名。-codex，2026-10-23
    if requested == '':
        return available[0]
    if available.count(requested) == 1:
        return requested
    if requested == 'RGB' and isinstance(expression, unreal.MaterialExpressionVertexColor) and available == ['', 'R', 'G', 'B', 'A']:
        return available[0]
    raise RuntimeError('Required reflected output unavailable: ' + requested + ' in ' + str(available)
                       + ' on ' + expression.get_class().get_name())


def link(a, out, b, pin):
    out = output_name(a, out)
    if pin:
        available = list(lib.get_material_expression_input_names(b))
        matches = [str(p) for p in available if str(p).replace(' ', '').casefold() == pin.replace(' ', '').casefold()]
        if len(matches) != 1:
            raise RuntimeError('Required reflected input unavailable: ' + pin + ' in ' + str(available))
        pin = matches[0]
    if not lib.connect_material_expressions(a, out, b, pin):
        raise RuntimeError('Connection failed: ' + pin)


def prop(m, a, out, p):
    out = output_name(a, out)
    if not lib.connect_material_property(a, out, p):
        raise RuntimeError('Property connection failed: ' + str(p))


def scalar(m, value):
    n = node(m, unreal.MaterialExpressionConstant)
    n.set_editor_property('r', value)
    return n


def custom(m, code, inputs, vector=True):
    n = node(m, unreal.MaterialExpressionCustom)
    n.set_editor_property('code', code)
    n.set_editor_property('output_type', unreal.CustomMaterialOutputType.CMOT_FLOAT3 if vector else unreal.CustomMaterialOutputType.CMOT_FLOAT1)
    pins = []
    for name, source, out in inputs:
        pin = unreal.CustomInput()
        pin.set_editor_property('input_name', name)
        pins.append(pin)
    n.set_editor_property('inputs', pins)
    for name, source, out in inputs:
        link(source, out, n, name)
    return n


def sample(m, name, texture):
    n = node(m, unreal.MaterialExpressionTextureSampleParameter2D)
    n.set_editor_property('parameter_name', name)
    n.set_editor_property('texture', texture)
    return n


surface = mat(names[0], translucent=True)
edge = mat(names[4], translucent=True)
for m, edge_factor in ((surface, 0.0), (edge, 1.0)):
    m.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_THIN_TRANSLUCENT)
    m.set_editor_property('translucency_lighting_mode', unreal.TranslucencyLightingMode.TLM_SURFACE_PER_PIXEL_LIGHTING)
    m.set_editor_property('refraction_method', unreal.RefractionMode.RM_INDEX_OF_REFRACTION)
    vertex = node(m, unreal.MaterialExpressionVertexColor)
    prop(m, vertex, 'RGB', unreal.MaterialProperty.MP_BASE_COLOR)
    thin = node(m, unreal.MaterialExpressionThinTranslucentMaterialOutput)
    # 只把颜色作为吸收，不以.92实色opacity盖住背景；薄透明模型不计算真实厚体的第二出口。
    absorption = custom(m, 'return lerp(float3(0.18,0.22,0.30), sqrt(saturate(C)), 0.78);', [('C', vertex, 'RGB')])
    link(absorption, '', thin, 'TransmittanceColor')
    link(scalar(m, 1.0), '', thin, 'SurfaceCoverage')
    prop(m, scalar(m, 0.08 if edge_factor else 0.025), '', unreal.MaterialProperty.MP_OPACITY)
    prop(m, scalar(m, 0.12 if edge_factor else 0.20), '', unreal.MaterialProperty.MP_ROUGHNESS)
    prop(m, scalar(m, 0.5), '', unreal.MaterialProperty.MP_SPECULAR)
    prop(m, scalar(m, 1.035), '', unreal.MaterialProperty.MP_REFRACTION)
    emission = node(m, unreal.MaterialExpressionScalarParameter)
    emission.set_editor_property('parameter_name', 'EdgeEmission')
    # ThinTranslucentCommon.ush把Emissive乘TopMaterialCoverage(.08)，7.5对应最多.6艺术化边能量。
    emission.set_editor_property('default_value', 7.5 if edge_factor else 0.0)
    glow = custom(m, 'return C * E;', [('C', vertex, 'RGB'), ('E', emission, '')])
    prop(m, glow, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)

composite = mat(names[1], ui=True)
capture = sample(composite, 'ChartCapture', unreal.load_asset('/Engine/EngineResources/WhiteSquareTexture'))
# FinalColorLDR的BGRA8 sRGB RT由纹理采样解码；UI仅输出线性RGB，不再反alpha/除alpha/Reinhard。
prop(composite, capture, 'RGB', unreal.MaterialProperty.MP_EMISSIVE_COLOR)

background = mat(names[2])
background.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
uv = node(background, unreal.MaterialExpressionTextureCoordinate)
navy = custom(background, 'float y=saturate(UV.y); float3 c=lerp(float3(.00152,.00913,.02843),float3(.00061,.00368,.01096),y); float p=exp(-pow((UV.x-.5)*3.0,2)-pow((UV.y-.46)*5,2)); return c+float3(.001,.006,.012)*p;', [('UV', uv, '')])
prop(background, navy, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)

stage = mat(names[3])
stage.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
tex = sample(stage, 'StageTexture', stage_texture)
stage_uv = node(stage, unreal.MaterialExpressionTextureCoordinate)
# 先把PNG透明边与同一解析深蓝底合为opaque，避免同一procedural mesh中透明舞台与晶体批次排序互盖。
stage_rgb = custom(stage, 'float2 p=float2((260+UV.x*1400)/1920,(320+UV.y*(1400.0*721/2182))/1080); float3 c=lerp(float3(.00152,.00913,.02843),float3(.00061,.00368,.01096),saturate(p.y)); float g=exp(-pow((p.x-.5)*3.0,2)-pow((p.y-.46)*5,2)); c+=float3(.001,.006,.012)*g; return lerp(c,T,saturate(A));', [('UV', stage_uv, ''), ('T', tex, 'RGB'), ('A', tex, 'A')])
prop(stage, stage_rgb, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
for m in (surface, composite, background, stage, edge):
    errors = lib.recompile_material(m)
    if errors:
        raise RuntimeError('Compile failed: ' + m.get_name() + ': ' + str(list(errors)))
    if not unreal.EditorAssetLibrary.save_loaded_asset(m):
        raise RuntimeError('Save failed: ' + m.get_name())
unreal.log('POND_RGB_ASSETS_READY: five batch-only materials; ThinTranslucent art crystal; native UI remains direct Slate')
