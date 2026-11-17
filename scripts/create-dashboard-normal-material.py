"""仅创建指定UE5.8工程的独立法线反射候选；绝不覆盖旧材质或同名资产。
主控先跑CoverageOnly验证轮廓，再看同一捕获链的环境场；不模拟物理透射/折射。-codex，2026-10-23
"""
from pathlib import Path
import unreal

content = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_content_dir())).resolve()
expected = Path('D:/3d-research/unreal/FujianSandbox58/Content').resolve()
if str(content).casefold() != str(expected).casefold() or not unreal.SystemLibrary.get_engine_version().startswith('5.8.'):
    raise RuntimeError('Requires designated FujianSandbox58 project and UE5.8')

asset_path = '/Game/Dashboard/M_PondChartNormalSurface'
if unreal.EditorAssetLibrary.does_asset_exist(asset_path):
    raise RuntimeError('Refusing existing asset overwrite: ' + asset_path)
lib = unreal.MaterialEditingLibrary
surface = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
    'M_PondChartNormalSurface', '/Game/Dashboard', unreal.Material, unreal.MaterialFactoryNew())
if not surface:
    raise RuntimeError('Normal experiment material creation failed')
surface.set_editor_property('material_domain', unreal.MaterialDomain.MD_SURFACE)
surface.set_editor_property('blend_mode', unreal.BlendMode.BLEND_OPAQUE)
surface.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
surface.set_editor_property('two_sided', False)


def connect(source, output, target, input_name):
    if not lib.connect_material_expressions(source, output, target, input_name):
        raise RuntimeError('Normal experiment connection failed: ' + input_name)


normal = lib.create_material_expression(surface, unreal.MaterialExpressionPixelNormalWS, -800, -300)
view = lib.create_material_expression(surface, unreal.MaterialExpressionCameraVectorWS, -800, -100)
world = lib.create_material_expression(surface, unreal.MaterialExpressionWorldPosition, -1000, 100)
origin = lib.create_material_expression(surface, unreal.MaterialExpressionConstant3Vector, -1000, 250)
origin.set_editor_property('constant', unreal.LinearColor(0.0, 0.0, -1000000.0, 1.0))
relative = lib.create_material_expression(surface, unreal.MaterialExpressionSubtract, -700, 150)
connect(world, '', relative, 'A')
connect(origin, '', relative, 'B')
coverage = lib.create_material_expression(surface, unreal.MaterialExpressionScalarParameter, -700, 350)
coverage.set_editor_property('parameter_name', 'CoverageOnly')
coverage.set_editor_property('default_value', 0.0)

field = lib.create_material_expression(surface, unreal.MaterialExpressionCustom, -300, 0)
field.set_editor_property('description', 'Geometric reflected rays intersect two fixed finite studio panels; no light-grid/UV/screen/time input')
field.set_editor_property('output_type', unreal.CustomMaterialOutputType.CMOT_FLOAT3)
inputs = []
for name in ('NormalWS', 'ViewWS', 'RigPosition', 'CoverageOnly'):
    entry = unreal.CustomInput()
    entry.set_editor_property('input_name', name)
    inputs.append(entry)
field.set_editor_property('inputs', inputs)

# Rig仅平移，WorldPosition-RigOrigin与世界法线同轴。若Rig以后旋转或缩放，必须先做逆变换。
# 光板固定于整个Rig，不随品种/省市/相机框选移动；空间求交让平顶也可因反射落点不同而变化。-codex，2026-10-23
field.set_editor_property('code', r'''
struct FPondNormalStudio
{
    float Panel(float3 P, float3 R, float3 Center, float3 AxisU, float3 AxisV, float2 HalfSize)
    {
        float3 PlaneNormal = cross(AxisU, AxisV);
        float Denominator = dot(R, PlaneNormal);
        if (abs(Denominator) < 0.0001) return 0.0;
        float RayDistance = dot(Center - P, PlaneNormal) / Denominator;
        if (RayDistance <= 0.0) return 0.0;
        float3 Offset = P + R * RayDistance - Center;
        float2 PanelUV = abs(float2(dot(Offset, AxisU), dot(Offset, AxisV))) / HalfSize;
        float2 Edge = 1.0 - smoothstep(float2(0.72, 0.72), float2(1.0, 1.0), PanelUV);
        return Edge.x * Edge.y;
    }
};
FPondNormalStudio Studio;
if (CoverageOnly > 0.5) return float3(0.28, 0.16, 0.65);
float3 N = normalize(NormalWS);
float3 V = normalize(ViewWS);
float3 R = normalize(-V + 2.0 * N * dot(N, V));
float MainPanel = Studio.Panel(RigPosition, R, float3(-40, 300, 240),
    float3(1,0,0), float3(0,1,0), float2(185,145));
float FrontPanel = Studio.Panel(RigPosition, R, float3(-60,-620,-180),
    float3(1,0,0), float3(0,0,1), float2(420,200));
// Fresnel与有色暗部仅美术模拟；Opaque没有透射、折射、物理吸收或环境遮挡。-codex，2026-10-23
float Fresnel = 0.06 + 0.94 * pow(1.0 - saturate(dot(N,V)), 5.0);
float3 Tint = float3(0.1946,0.1651,0.9131);
float3 Ambient = Tint * (0.06 + 0.12 * saturate(N.z * 0.5 + 0.5));
float3 ReflectionTint = lerp(Tint, float3(0.82,0.94,1.0), 0.25 + 0.65 * Fresnel);
float3 Environment = MainPanel * float3(1.2,1.45,1.8) + FrontPanel * float3(0.55,0.8,1.1);
return Ambient + ReflectionTint * Environment * (0.35 + 0.65 * Fresnel);
''')
for expression, name in ((normal, 'NormalWS'), (view, 'ViewWS'), (relative, 'RigPosition'), (coverage, 'CoverageOnly')):
    connect(expression, '', field, name)
if not lib.connect_material_property(field, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR):
    raise RuntimeError('Normal experiment emissive connection failed')
errors = lib.recompile_material(surface)
if errors:
    raise RuntimeError('Normal experiment compilation failed: ' + str(list(errors)))
if not unreal.EditorAssetLibrary.save_loaded_asset(surface):
    raise RuntimeError('Normal experiment save failed')
unreal.log('POND_CHART_NORMAL_ASSET_READY: independent Opaque Unlit; CoverageOnly default=0; finite rig-space panels; old assets untouched')
