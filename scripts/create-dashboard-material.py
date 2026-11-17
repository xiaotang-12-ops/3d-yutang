"""仅由主控在UE5.8资产生成阶段运行：创建B屏紫段受控光学候选。
材质属于UI域，不读取世界、场景颜色或Time；不覆盖Terrain/A屏资产。-codex，2026-10-22
"""
import unreal
from pathlib import Path

# 此脚本只属于5.8候选，拒绝误用于旧工程或其他项目。-codex，2026-10-22
project_content = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_content_dir())).resolve()
expected_content = Path('D:/3d-research/unreal/FujianSandbox58/Content').resolve()
if str(project_content).casefold() != str(expected_content).casefold() or not unreal.SystemLibrary.get_engine_version().startswith('5.8.'):
    raise RuntimeError('PondGlass generation requires the designated FujianSandbox58 project and UE5.8')

lib = unreal.MaterialEditingLibrary
folder = '/Game/Dashboard'
asset_path = folder + '/M_PondGlass'
unreal.EditorAssetLibrary.make_directory(folder)
mat = unreal.load_asset(asset_path)
if mat and (not isinstance(mat, unreal.Material) or mat.get_editor_property('material_domain') != unreal.MaterialDomain.MD_UI):
    raise RuntimeError('Refusing to replace a non-UI/non-Material asset: ' + asset_path)
if not mat:
    mat = unreal.AssetToolsHelpers.get_asset_tools().create_asset('M_PondGlass', folder, unreal.Material, unreal.MaterialFactoryNew())
if not mat:
    raise RuntimeError('Failed to create ' + asset_path)
mat.set_editor_property('material_domain', unreal.MaterialDomain.MD_UI)
mat.set_editor_property('blend_mode', unreal.BlendMode.BLEND_TRANSLUCENT)
mat.set_editor_property('two_sided', True)
lib.delete_all_material_expressions(mat)


def node(kind):
    return lib.create_material_expression(mat, kind, 0, 0)


def connect(source, output_name, target, input_name):
    if not lib.connect_material_expressions(source, output_name, target, input_name):
        raise RuntimeError('Material connection failed: ' + output_name + ' -> ' + input_name)


# MakeCustomVerts走ST_Custom，FillTexCoords直接返回GetSlateUVs：通道0=xy，通道1=zw。
# 普通元素在NUM_MATERIAL_TEXCOORDS=3下的1/2重排位于return之后，不能套用到Custom。-codex，2026-10-22
uv = node(unreal.MaterialExpressionTextureCoordinate)
uv.set_editor_property('coordinate_index', 0)
surface = node(unreal.MaterialExpressionTextureCoordinate)
surface.set_editor_property('coordinate_index', 1)
if (uv.get_editor_property('coordinate_index'), surface.get_editor_property('coordinate_index')) != (0, 1):
    raise RuntimeError('PondGlass Custom UV wiring mismatch: expected plane=0, face/thickness=1')
vertex = node(unreal.MaterialExpressionVertexColor)
optical = node(unreal.MaterialExpressionCustom)
optical.set_editor_property('description', 'PondGlass wiring-v2 UV0=plane UV1=face/thickness: surface normals, dielectric Fresnel, finite softbox and thickness absorption; no scene refraction')
optical.set_editor_property('output_type', unreal.CustomMaterialOutputType.CMOT_FLOAT4)
inputs = []
for input_name in ('UV', 'Surface', 'Tint', 'Fade'):
    custom_input = unreal.CustomInput()
    custom_input.set_editor_property('input_name', input_name)
    inputs.append(custom_input)
optical.set_editor_property('inputs', inputs)
optical.set_editor_property('code', r'''
// UI optical illustration, not physical scene refraction. -codex, 2026-10-22
float radius = max(length(UV), 0.0001);
float2 radial = UV / radius;
float face = Surface.x;
float height = saturate(Surface.y);
float innerBevel = 1.0 - smoothstep(0.680, 0.698, radius);
float outerBevel = smoothstep(0.982, 1.000, radius);
float edge = max(innerBevel, outerBevel);
float3 N = normalize(float3(radial * 0.34 * (outerBevel-innerBevel), 1.0));
float path = 0.22 + edge * 0.38;
float alphaBody = 0.54 + edge * 0.26;
if (face > 0.5)
{
    float2 wallNormal = radial;
    if (face > 1.5 && face < 2.5) wallNormal = -radial;
    if (face > 2.5 && face < 3.5) wallNormal = float2(radial.y, -radial.x);
    if (face > 3.5) wallNormal = float2(-radial.y, radial.x);
    float lip = 1.0 - smoothstep(0.0, 0.10, height);
    N = normalize(float3(wallNormal, lip * 0.30));
    edge = max(1.0-smoothstep(0.0,0.10,height), smoothstep(0.86,1.0,height));
    path = 0.55 + height * 0.32;
    alphaBody = 0.72 + edge * 0.14;
}
float3 V = normalize(float3(0.0, 0.88, 0.475));
float fresnel = 0.04 + 0.96 * pow(1.0-saturate(abs(dot(N,V))), 5.0);
float3 R = reflect(-V,N);
// A finite rectangular environment panel is projected by the surface normal.
// Unlike a position-only Gaussian stripe, walls/cuts/bevels reflect different parts.
float2 panelUV = R.xy / max(abs(R.z),0.25) + UV * 0.65 - float2(-0.15,-1.50);
float panelDistance = max(abs(panelUV.x)-0.17,abs(panelUV.y)-0.29);
float panelAA = max(fwidth(panelDistance)*1.5,0.018);
float panel = 1.0-smoothstep(-panelAA,panelAA,panelDistance);
float2 echoUV = panelUV + float2(0.055,0.10) * (1.0+height);
float echoDistance = max(abs(echoUV.x)-0.17,abs(echoUV.y)-0.29);
float echo = (1.0-smoothstep(-0.035,0.035,echoDistance)) * (1.0-panel);
float3 extinction = (1.0-saturate(Tint))*1.8 + 0.12;
float3 transmission = exp(-extinction*path);
float3 body = Tint * (0.58+0.42*transmission);
float panelEnergy = panel*(0.15+0.64*fresnel) + echo*0.065*(1.0-fresnel);
float3 reflected = float3(0.64,0.84,1.0) * (panelEnergy+edge*fresnel*0.30);
float3 finalColor = body*(1.0-fresnel*0.32) + reflected;
float finalAlpha = saturate(alphaBody+panelEnergy*0.22+fresnel*0.08)*saturate(Fade);
return float4(saturate(finalColor),finalAlpha);
''')
connect(uv, '', optical, 'UV')
connect(surface, '', optical, 'Surface')
connect(vertex, '', optical, 'Tint')
connect(vertex, 'A', optical, 'Fade')

color_mask = node(unreal.MaterialExpressionComponentMask)
for channel, enabled in (('r', True), ('g', True), ('b', True), ('a', False)):
    color_mask.set_editor_property(channel, enabled)
alpha_mask = node(unreal.MaterialExpressionComponentMask)
for channel, enabled in (('r', False), ('g', False), ('b', False), ('a', True)):
    alpha_mask.set_editor_property(channel, enabled)
connect(optical, '', color_mask, '')
connect(optical, '', alpha_mask, '')
if not lib.connect_material_property(color_mask, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR):
    raise RuntimeError('Failed to connect UI Final Color')
if not lib.connect_material_property(alpha_mask, '', unreal.MaterialProperty.MP_OPACITY):
    raise RuntimeError('Failed to connect UI Opacity')
errors = lib.recompile_material(mat)
if errors:
    raise RuntimeError('PondGlass compilation failed: ' + str(list(errors)))
if not unreal.EditorAssetLibrary.save_loaded_asset(mat):
    raise RuntimeError('Failed to save ' + asset_path)
unreal.log('POND_GLASS_ASSET_READY: /Game/Dashboard/M_PondGlass; UI/Translucent; purple sector only; wiring-v2 UV0=plane UV1=face/thickness; no Time or MID')
