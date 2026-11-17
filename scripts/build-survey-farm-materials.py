"""为A原地图的局部鱼塘构建水/土材质，不改原地形与B运行资产。
复用仓库现有水纹及土石源图，哈希固定；只覆盖本脚本所有权资产。
在指定UE5.8工程的PythonScript commandlet内执行。-codex，2026-10-30
"""
import hashlib
import json
from pathlib import Path

import unreal as u

ROOT = Path(__file__).resolve().parent.parent
CONTENT = Path(u.Paths.convert_relative_path_to_full(u.Paths.project_content_dir())).resolve()
if CONTENT != (ROOT / 'unreal/FujianSandbox58/Content').resolve():
    raise RuntimeError('只允许在FujianSandbox58工程构建调查场址材质')
if not u.SystemLibrary.get_engine_version().startswith('5.8.'):
    raise RuntimeError('要求UE5.8')

DEST = '/Game/Terrain'
OWNER = 'xinluo-survey-farms-20261030'
TAG = 'SurveyFarmOwner'
A = u.EditorAssetLibrary
E = u.MaterialEditingLibrary
TOOLS = u.AssetToolsHelpers.get_asset_tools()
STAGE = ROOT / 'experiments/fujian-dashboard-motion/SourceAssets/Stage'
SOURCES = {
    'Water': (STAGE / 'water-caustics-v1.png',
              'T_SurveyWaterPattern', 'sha256', 'bc6733405b0fe4e9eb952cac2ad6c9947ed5cfeca81687db43f7708cb4e4e6fa'),
    'Soil': (STAGE / 'rock_ground_02_diff_2k.jpg', 'T_SurveySoilDiffuse', 'md5', 'f36c1afc75b856f80d85732c2ba03da1'),
    'Normal': (STAGE / 'rock_ground_02_nor_dx_2k.jpg', 'T_SurveySoilNormal', 'md5', '9298a0590766c4cad7d7e3c9269bc147'),
    'Rough': (STAGE / 'rock_ground_02_rough_2k.jpg', 'T_SurveySoilRough', 'md5', 'f4f0b587df9090a8df75f0166806d2eb'),
}


def owned(name):
    path = f'{DEST}/{name}'
    if not A.does_asset_exist(path):
        return None
    asset = A.load_asset(path)
    if not asset or A.get_metadata_tag(asset, TAG) != OWNER:
        raise RuntimeError('拒绝覆盖非本轮资产：' + path)
    return asset


def save(asset):
    A.set_metadata_tag(asset, TAG, OWNER)
    if not A.save_loaded_asset(asset, False):
        raise RuntimeError('资产保存失败：' + asset.get_path_name())


def load_textures():
    result = {}
    for key, (path, name, algorithm, expected) in SOURCES.items():
        if not path.is_file() or hashlib.new(algorithm, path.read_bytes()).hexdigest() != expected:
            raise RuntimeError('源贴图缺失或哈希不符：' + str(path))
        tex = owned(name)
        if tex is not None and (not isinstance(tex, u.Texture2D) or A.get_metadata_tag(tex, 'SourceHash') != expected):
            raise RuntimeError('已有纹理类型或来源不符：' + name)
        if tex is None:
            task = u.AssetImportTask()
            for prop, value in dict(filename=str(path), destination_path=DEST, destination_name=name,
                                    automated=True, replace_existing=False, save=False).items():
                task.set_editor_property(prop, value)
            TOOLS.import_asset_tasks([task])
            tex = A.load_asset(f'{DEST}/{name}')
        if not isinstance(tex, u.Texture2D):
            raise RuntimeError('纹理导入失败：' + name)
        tex.set_editor_property('srgb', key in ('Water', 'Soil'))
        tex.set_editor_property('compression_settings', u.TextureCompressionSettings.TC_NORMALMAP if key == 'Normal' else u.TextureCompressionSettings.TC_DEFAULT)
        # 程序网格没有静态UV密度；四张小型纹理固定常驻，避免近景仍采最低mip。
        tex.set_editor_property('never_stream', True)
        tex.set_editor_property('address_x', u.TextureAddress.TA_CLAMP if key == 'Water' else u.TextureAddress.TA_WRAP)
        tex.set_editor_property('address_y', u.TextureAddress.TA_CLAMP if key == 'Water' else u.TextureAddress.TA_WRAP)
        A.set_metadata_tag(tex, 'SourceHash', expected)
        save(tex)
        result[key] = tex
        u.log(f'SURVEY_FARM_TEXTURE_READY {name} {algorithm}={expected}')
    return result


def node(mat, cls, **props):
    value = E.create_material_expression(mat, cls)
    if not value:
        raise RuntimeError('创建材质节点失败：' + str(cls))
    for key, item in props.items():
        value.set_editor_property(key, item)
    return value


def custom(mat, names, code, vector=True):
    pins = []
    for name in names:
        item = u.CustomInput()
        item.set_editor_property('input_name', name)
        pins.append(item)
    return node(mat, u.MaterialExpressionCustom, inputs=pins, code=code,
                output_type=u.CustomMaterialOutputType.CMOT_FLOAT3 if vector else u.CustomMaterialOutputType.CMOT_FLOAT2)


def wire(source, target, pin, output=''):
    if not E.connect_material_expressions(source, output, target, pin):
        raise RuntimeError('材质连线失败：' + pin)


def prop(source, name, output=''):
    if not E.connect_material_property(source, output, getattr(u.MaterialProperty, name)):
        raise RuntimeError('材质属性连线失败：' + name)


def constant(mat, value):
    return node(mat, u.MaterialExpressionConstant, r=value)


def material(name):
    mat = owned(name)
    if mat is not None and not isinstance(mat, u.Material):
        raise RuntimeError('已有材质类型不符：' + name)
    if mat is None:
        mat = TOOLS.create_asset(name, DEST, u.Material, u.MaterialFactoryNew())
    if not mat:
        raise RuntimeError('材质创建失败：' + name)
    E.delete_all_material_expressions(mat)
    mat.set_editor_property('material_domain', u.MaterialDomain.MD_SURFACE)
    mat.set_editor_property('blend_mode', u.BlendMode.BLEND_OPAQUE)
    mat.set_editor_property('shading_model', u.MaterialShadingModel.MSM_DEFAULT_LIT)
    mat.set_editor_property('two_sided', False)
    return mat


def finish(mat):
    # 此API不返回GPU编译错误；NullRHI只负责资产构建，实际SM5着色器必须另查运行日志。
    E.recompile_material(mat)
    save(mat)
    u.log('SURVEY_FARM_MATERIAL_WRITTEN ' + mat.get_path_name() + ' target_rhi_validation_required=1')


def build():
    textures = load_textures()
    water = material('M_SurveyWater')
    uv = node(water, u.MaterialExpressionTextureCoordinate)
    time = node(water, u.MaterialExpressionTime)
    moving = custom(water, ('UV', 'T'),
                    'return UV*.80+.10+float2(.012*sin(T*.17),.012*cos(T*.13));', False)
    wire(uv, moving, 'UV'); wire(time, moving, 'T')
    sample = node(water, u.MaterialExpressionTextureSample, texture=textures['Water'])
    wire(moving, sample, 'UVs')
    # 水纹只贡献轻微明暗，避免高对比焦散把塘面做成发光贴纸。-codex，2026-10-30
    color = custom(water, ('C', 'UV'), '''
float shade=dot(C,float3(.2126,.7152,.0722));
float edge=smoothstep(.30,.50,max(abs(UV.x-.5),abs(UV.y-.5)));
return float3(.028,.105,.098)*(0.92+shade*.12)*(1.0-edge*.18);
''')
    wire(sample, color, 'C', 'RGB'); wire(uv, color, 'UV')
    prop(color, 'MP_BASE_COLOR')
    ambient = node(water, u.MaterialExpressionMultiply)
    wire(color, ambient, 'A'); wire(constant(water, .16), ambient, 'B')
    prop(ambient, 'MP_EMISSIVE_COLOR')
    waves = custom(water, ('UV', 'T'), '''
float2 p=UV+.017*float2(sin(UV.y*17+T*.23),sin(UV.x*21-T*.19));
float2 slope=float2(.016,.009)*cos(dot(p,float2(43,17))+T*.22)
 +float2(-.007,.014)*cos(dot(p,float2(-21,57))-T*.29);
return normalize(float3(-slope,1.0));
''')
    wire(uv, waves, 'UV'); wire(time, waves, 'T'); prop(waves, 'MP_NORMAL')
    prop(constant(water, .48), 'MP_ROUGHNESS'); prop(constant(water, .22), 'MP_SPECULAR')
    finish(water)

    soil = material('M_SurveyBank')
    uv = node(soil, u.MaterialExpressionTextureCoordinate)
    tiled = node(soil, u.MaterialExpressionMultiply)
    wire(uv, tiled, 'A'); wire(constant(soil, 2.), tiled, 'B')
    samples = {}
    for key, sampler in (('Soil', u.MaterialSamplerType.SAMPLERTYPE_COLOR),
                         ('Normal', u.MaterialSamplerType.SAMPLERTYPE_NORMAL),
                         ('Rough', u.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)):
        samples[key] = node(soil, u.MaterialExpressionTextureSample, texture=textures[key], sampler_type=sampler)
        wire(tiled, samples[key], 'UVs')
    tint = custom(soil, ('C',), 'float l=dot(C,float3(.2126,.7152,.0722)); return lerp(C,l.xxx,.68)*float3(.43,.44,.34);')
    wire(samples['Soil'], tint, 'C', 'RGB')
    # 外土肩按原地理影像过渡；只读复用既有纹理，不修改原地形材质。-codex，2026-10-30
    detail = A.load_asset(DEST + '/T_LongyanDetail')
    if not isinstance(detail, u.Texture2D):
        raise RuntimeError('缺少原龙岩地表纹理，不能构建连续土肩')
    meta = json.loads((CONTENT / 'Province/longyan-detail-source.json').read_text('utf-8'))
    west, south, east, north = meta['boundsShared']
    world = node(soil, u.MaterialExpressionWorldPosition)
    # 西界为负数；常量必须括起，防止生成P.y--2290而被HLSL解释成自减。-codex，2026-10-30
    map_uv = custom(soil, ('P',), f'return float2((P.y-({west * 10:.12f}))/{(east-west)*10:.12f},(({north*10:.12f})-P.x)/{(north-south)*10:.12f});', False)
    wire(world, map_uv, 'P')
    satellite = node(soil, u.MaterialExpressionTextureSample, texture=detail)
    wire(map_uv, satellite, 'UVs')
    vertex = node(soil, u.MaterialExpressionVertexColor)
    diffuse = node(soil, u.MaterialExpressionMultiply)
    wire(tint, diffuse, 'A'); wire(vertex, diffuse, 'B', 'A'); prop(diffuse, 'MP_BASE_COLOR')
    ambient = node(soil, u.MaterialExpressionMultiply)
    wire(tint, ambient, 'A'); wire(constant(soil, .16), ambient, 'B')
    original = node(soil, u.MaterialExpressionMultiply)
    wire(satellite, original, 'A', 'RGB'); wire(constant(soil, .90), original, 'B')
    edge_blend = node(soil, u.MaterialExpressionLinearInterpolate)
    wire(original, edge_blend, 'A'); wire(ambient, edge_blend, 'B'); wire(vertex, edge_blend, 'Alpha', 'A')
    prop(edge_blend, 'MP_EMISSIVE_COLOR')
    prop(samples['Normal'], 'MP_NORMAL', 'RGB')
    rough = node(soil, u.MaterialExpressionLinearInterpolate)
    wire(constant(soil, .72), rough, 'A'); wire(constant(soil, .98), rough, 'B')
    wire(samples['Rough'], rough, 'Alpha', 'R'); prop(rough, 'MP_ROUGHNESS')
    prop(constant(soil, .07), 'MP_SPECULAR')
    finish(soil)
    u.log('SURVEY_FARM_ASSETS_READY textures=4 materials=2 runtime_dependency=A_only')


build()
