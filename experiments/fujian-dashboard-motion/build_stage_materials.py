"""为隔离实验生成六个受光舞台材质，仅更新带本脚本所有权标记的资产。
Rock/Bed 的 UV 由几何按每200cm重复提供；Water/Cut 使用0..1 UV。
在 UE5.8 PythonScript commandlet 内执行，失败向外抛出令命令返回非零。-codex，2026-10-23
"""
import hashlib
import re
import traceback
from pathlib import Path

import unreal as u

ROOT = Path(__file__).resolve().parent
DEST = '/Game/Motion'
OWNER = 'fujian-dashboard-motion-stage-20261023'
TAG = 'MotionStageOwner'
E = u.MaterialEditingLibrary
A = u.EditorAssetLibrary
NULL_RHI = re.search(r'(?:^|\s)-nullrhi(?=\s|$)', u.SystemLibrary.get_command_line(), re.IGNORECASE) is not None
TEXTURES = {
    'Diffuse': ('rock_ground_02_diff_2k.jpg', 'T_StageRockDiffuse', 'f36c1afc75b856f80d85732c2ba03da1'),
    'Normal': ('rock_ground_02_nor_dx_2k.jpg', 'T_StageRockNormal', '9298a0590766c4cad7d7e3c9269bc147'),
    'Rough': ('rock_ground_02_rough_2k.jpg', 'T_StageRockRough', 'f4f0b587df9090a8df75f0166806d2eb'),
}
NAMES = ['M_StageRock', 'M_StageWater', 'M_StageBed', 'M_StageCut', 'M_StagePlant', 'M_StageFloor']
PIN_SCHEMAS = set()
CAUSTICS_SOURCE = ROOT/'SourceAssets'/'Stage'/'water-caustics-v1.png'
CAUSTICS_SHA256 = 'bc6733405b0fe4e9eb952cac2ad6c9947ed5cfeca81687db43f7708cb4e4e6fa'
CAUSTICS_NAME = 'T_StageWaterCaustics'


def selected_materials(command_line):
    allowed = ('Rock', 'Water', 'Bed', 'Cut', 'Plant', 'Floor')
    values = re.findall(r'(?:^|\s)-StageMaterials(?:=("[^"]*"|\S*))?(?=\s|$)', command_line, re.IGNORECASE)
    if not values:
        return allowed
    if len(values) != 1:
        raise RuntimeError('StageMaterials只能指定一次')
    selected = tuple(values[0].strip('"').split(','))
    if not selected or len(set(selected)) != len(selected) or any(name not in allowed for name in selected):
        raise RuntimeError(f'StageMaterials必须为不重复的严格白名单：{allowed}；收到={selected}')
    return selected


def log_pins(expression, direction, names):
    schema = (expression.get_class().get_name(), direction, tuple(names))
    if schema not in PIN_SCHEMAS:
        u.log(f'MOTION_STAGE_PINS {schema[0]} {direction}={names!r}')
        PIN_SCHEMAS.add(schema)


def check_owned(name):
    path = f'{DEST}/{name}'
    if A.does_asset_exist(path):
        asset = A.load_asset(path)
        if not asset or A.get_metadata_tag(asset, TAG) != OWNER:
            raise RuntimeError(f'拒绝覆盖非本轮资产：{path}')
        return asset
    return None


def node(material, cls, **properties):
    result = E.create_material_expression(material, cls)
    if not result:
        raise RuntimeError(f'无法创建材质节点：{cls}')
    for key, value in properties.items():
        result.set_editor_property(key, value)
    return result


def checked_output(source, output):
    names = [str(name) for name in E.get_material_expression_output_names(source)]
    log_pins(source, 'outputs', names)
    if not names or (output and output not in names):
        raise RuntimeError(f'输出不存在：{source.get_name()} 请求={output!r} 实际={names!r}')
    # 空名按引擎GetExpressionOutputIndexByName的约定选择首输出，不猜测RGB别名。
    return output


def wire(source, target, pin, output=''):
    names = [str(name) for name in E.get_material_expression_input_names(target)]
    log_pins(target, 'inputs', names)
    output = checked_output(source, output)
    if pin not in names:
        if pin == 'Input' and len(names) == 1:
            u.log(f'MOTION_STAGE_PIN_RESOLVED {target.get_name()} Input -> {names[0]!r}')
            pin = names[0]
        else:
            raise RuntimeError(f'输入不存在：{target.get_name()} 请求={pin!r} 实际={names!r}')
    if not E.connect_material_expressions(source, output, target, pin):
        raise RuntimeError(f'节点连接失败：{source.get_name()}.{output} -> {target.get_name()}.{pin}; 实际输入={names!r}')


def prop(source, name, output=''):
    output = checked_output(source, output)
    if not E.connect_material_property(source, output, getattr(u.MaterialProperty, name)):
        raise RuntimeError(f'材质属性连接失败：{name}')


def scalar(m, value):
    return node(m, u.MaterialExpressionConstant, r=value)


def color(m, rgb):
    return node(m, u.MaterialExpressionConstant3Vector, constant=u.LinearColor(*rgb, 1.0))


def multiply(m, a, b, output_a='', output_b=''):
    result = node(m, u.MaterialExpressionMultiply)
    wire(a, result, 'A', output_a)
    wire(b, result, 'B', output_b)
    return result


def waterlight(m, base, emission, kind):
    # 仅数据页动态实例开关，默认0严格还原旧图；三种表面共享世界坐标中心和椭圆范围。
    # 不增加素材/资产，保留原纹理细节，暖石岸只影响上部，避免整块外壳自发光。-codex，2026-10-26
    strength = node(m, u.MaterialExpressionScalarParameter, parameter_name='WaterlightStrength', default_value=0.)
    phase = node(m, u.MaterialExpressionScalarParameter, parameter_name='WaterlightPhase', default_value=0.)
    center = node(m, u.MaterialExpressionVectorParameter, parameter_name='WaterlightCenter', default_value=u.LinearColor(0., 0., 0., 0.))
    radius = node(m, u.MaterialExpressionVectorParameter, parameter_name='WaterlightRadius', default_value=u.LinearColor(230., 170., 100., 0.))
    position = node(m, u.MaterialExpressionWorldPosition)
    mask = node(m, u.MaterialExpressionCustom, output_type=u.CustomMaterialOutputType.CMOT_FLOAT1,
                code='''float3 p=(P-C)/max(R,float3(1,1,1));
float d=length(p.xy);
float envelope=1-smoothstep(.18,1.0,d);
float ripple=.62+.38*pow(.5+.5*sin(p.x*12+p.y*8+sin(p.y*7-T)),2);
return saturate(S)*envelope*ripple;''',
                inputs=[custom_input(n) for n in ('P', 'C', 'R', 'S', 'T')])
    for source, pin in ((position, 'P'), (center, 'C'), (radius, 'R'), (strength, 'S'), (phase, 'T')):
        wire(source, mask, pin)
    if kind == 'Rock':
        upper = node(m, u.MaterialExpressionCustom, output_type=u.CustomMaterialOutputType.CMOT_FLOAT1,
                     code='return smoothstep(-.45,.2,(P.z-C.z)/max(R.z,1.));',
                     inputs=[custom_input(n) for n in ('P', 'C', 'R')])
        for source, pin in ((position, 'P'), (center, 'C'), (radius, 'R')):
            wire(source, upper, pin)
        mask = multiply(m, mask, upper)
    elif kind == 'Bed':
        vc = node(m, u.MaterialExpressionVertexColor)
        top = node(m, u.MaterialExpressionCustom, output_type=u.CustomMaterialOutputType.CMOT_FLOAT1,
                   code='return smoothstep(.55,.64,VC.g);', inputs=[custom_input('VC')])
        wire(vc, top, 'VC')
        mask = multiply(m, mask, top)
    # 乘原有基色使暗纹仍暗，避免白色贴片；水面低幅，塘底较强以读出水深。-codex，2026-10-26
    gain = {'Rock': (1.05, .66, .28), 'Water': (.24, .36, .27), 'Bed': (.85, 1.15, .70)}[kind]
    accent = multiply(m, multiply(m, base, color(m, gain)), mask)
    base_sum = node(m, u.MaterialExpressionAdd)
    wire(base, base_sum, 'A')
    wire(accent, base_sum, 'B')
    prop(base_sum, 'MP_BASE_COLOR')
    emit_sum = node(m, u.MaterialExpressionAdd)
    wire(emission, emit_sum, 'A')
    wire(multiply(m, accent, scalar(m, .55 if kind == 'Rock' else .75)), emit_sum, 'B')
    prop(emit_sum, 'MP_EMISSIVE_COLOR')


def begin(name, translucent=False):
    m = check_owned(name)
    if not m:
        m = u.AssetToolsHelpers.get_asset_tools().create_asset(name, DEST, u.Material, u.MaterialFactoryNew())
    if not isinstance(m, u.Material):
        raise RuntimeError(f'资产不是 Material：{name}')
    A.set_metadata_tag(m, TAG, OWNER)
    E.delete_all_material_expressions(m)
    m.set_editor_property('blend_mode', u.BlendMode.BLEND_TRANSLUCENT if translucent else u.BlendMode.BLEND_OPAQUE)
    m.set_editor_property('two_sided', True)
    # Surface ForwardShading 允许动态直射光产生水面高光，不依赖 Lumen/SSR。
    # 环境反射未提供时不能承诺设计图中的天空倒影。-codex，2026-10-23
    if translucent:
        m.set_editor_property('translucency_lighting_mode', u.TranslucencyLightingMode.TLM_SURFACE_PER_PIXEL_LIGHTING)
    return m


def finish(m):
    E.layout_material_expressions(m)
    # NullRHI只保存图，避免RecompileMaterial附带的编辑器调试视图/UV密度编译。
    # 着色器正确性后移至真实-game SM5加载；图保存成功不能表述为渲染通过。-codex，2026-10-23
    if not NULL_RHI:
        errors = E.recompile_material(m)
        if errors:
            raise RuntimeError(f'{m.get_name()} 编译失败：{list(errors)}')
    if not A.save_loaded_asset(m, False):
        raise RuntimeError(f'保存失败：{m.get_path_name()}')
    status = 'GRAPH_SAVED SHADER_NOT_VALIDATED' if NULL_RHI else 'MATERIAL_OK'
    u.log(f'MOTION_STAGE_{status} {m.get_path_name()}')


def import_textures():
    result = {}
    for channel, (filename, name, expected) in TEXTURES.items():
        source = ROOT/'SourceAssets'/'Stage'/filename
        if not source.is_file() or hashlib.md5(source.read_bytes()).hexdigest() != expected:
            raise RuntimeError(f'贴图缺失或与官方MD5不符：{source}')
        existing = check_owned(name)
        if existing:
            compression = u.TextureCompressionSettings.TC_NORMALMAP if channel == 'Normal' else u.TextureCompressionSettings.TC_DEFAULT
            if not isinstance(existing, u.Texture2D) or existing.get_editor_property('srgb') != (channel == 'Diffuse') or existing.get_editor_property('compression_settings') != compression:
                raise RuntimeError(f'已有贴图类型或采样设置不符，拒绝静默复用：{name}')
            result[channel] = existing
            u.log(f'MOTION_STAGE_TEXTURE_REUSED {name} source_md5={expected}')
            continue
        task = u.AssetImportTask()
        for key, value in dict(filename=str(source), destination_path=DEST, destination_name=name,
                               automated=True, replace_existing=True, save=False).items():
            task.set_editor_property(key, value)
        u.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
        texture = A.load_asset(f'{DEST}/{name}')
        if not isinstance(texture, u.Texture2D):
            raise RuntimeError(f'贴图导入失败：{name}')
        A.set_metadata_tag(texture, TAG, OWNER)
        texture.set_editor_property('srgb', channel == 'Diffuse')
        texture.set_editor_property('compression_settings', u.TextureCompressionSettings.TC_NORMALMAP if channel == 'Normal' else u.TextureCompressionSettings.TC_DEFAULT)
        if not A.save_loaded_asset(texture, False):
            raise RuntimeError(f'贴图保存失败：{name}')
        result[channel] = texture
    return result


def rock_graph(m, textures, tint):
    uv = node(m, u.MaterialExpressionTextureCoordinate)
    samples = {}
    for channel in TEXTURES:
        texture = textures[channel]
        sample = node(m, u.MaterialExpressionTextureSample, texture=texture,
                      sampler_type={'Diffuse': u.MaterialSamplerType.SAMPLERTYPE_COLOR,
                                    'Normal': u.MaterialSamplerType.SAMPLERTYPE_NORMAL,
                                    'Rough': u.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR}[channel])
        wire(uv, sample, 'UVs')
        samples[channel] = sample
    vc = node(m, u.MaterialExpressionVertexColor)
    # UE5.8 VertexColor 的首个RGB输出无名字；TextureSample才命名为RGB。
    # 使用空名字选择首输出，不能把编辑器显示通道名当作脚本pin名。-codex，2026-10-23
    base = multiply(m, multiply(m, samples['Diffuse'], vc, 'RGB', ''), color(m, tint))
    waterlight(m, base, color(m, (0., 0., 0.)), 'Rock')
    prop(samples['Normal'], 'MP_NORMAL', 'RGB')
    prop(multiply(m, samples['Rough'], scalar(m, .73), 'R'), 'MP_ROUGHNESS')
    prop(scalar(m, .34), 'MP_SPECULAR')


def rock(name, textures, tint):
    m = begin(name)
    rock_graph(m, textures, tint)
    finish(m)


def custom_input(name):
    # CustomInput仅EditAnywhere字段，不生成带字段参数的Python构造函数。
    # 空构造后走结构体set_editor_property，遵循本机PyWrapperStruct绑定。-codex，2026-10-23
    result = u.CustomInput()
    result.set_editor_property('input_name', name)
    if str(result.get_editor_property('input_name')) != name:
        raise RuntimeError(f'CustomInput字段回读不符：{name}')
    return result


def import_caustics():
    if not CAUSTICS_SOURCE.is_file() or hashlib.sha256(CAUSTICS_SOURCE.read_bytes()).hexdigest() != CAUSTICS_SHA256:
        raise RuntimeError('焦散素材缺失或SHA256不符')
    texture = check_owned(CAUSTICS_NAME)
    if texture is not None and (not isinstance(texture, u.Texture2D) or A.get_metadata_tag(texture, 'SourceSHA256') != CAUSTICS_SHA256):
        raise RuntimeError('已有焦散资产类型或来源不符，拒绝静默覆盖')
    if texture is None:
        task = u.AssetImportTask()
        for key, value in dict(filename=str(CAUSTICS_SOURCE), destination_path=DEST, destination_name=CAUSTICS_NAME,
                               automated=True, replace_existing=False, save=False).items():
            task.set_editor_property(key, value)
        u.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
        texture = A.load_asset(f'{DEST}/{CAUSTICS_NAME}')
    if not isinstance(texture, u.Texture2D):
        raise RuntimeError('焦散贴图导入失败')
    texture.set_editor_property('srgb', True)
    texture.set_editor_property('compression_settings', u.TextureCompressionSettings.TC_DEFAULT)
    texture.set_editor_property('address_x', u.TextureAddress.TA_CLAMP)
    texture.set_editor_property('address_y', u.TextureAddress.TA_CLAMP)
    A.set_metadata_tag(texture, TAG, OWNER)
    A.set_metadata_tag(texture, 'SourceSHA256', CAUSTICS_SHA256)
    if not A.save_loaded_asset(texture, False):
        raise RuntimeError('焦散贴图保存失败')
    u.log(f'MOTION_STAGE_CAUSTICS_READY {texture.get_path_name()} source_sha256={CAUSTICS_SHA256}')
    return texture


def caustics_sample(m, texture, uv, time, offset=0.):
    # 使用素材内部区域轻微往返漂移，不假定生成图片可平铺；运行法线仍独立更新。-codex，2026-10-24
    moving = node(m, u.MaterialExpressionCustom, output_type=u.CustomMaterialOutputType.CMOT_FLOAT2,
                  code=f'return UV*.84+.08+float2({offset}+.008*sin(T*.13),.008*cos(T*.11));',
                  inputs=[custom_input('UV'), custom_input('T')])
    wire(uv, moving, 'UV')
    wire(time, moving, 'T')
    sample = node(m, u.MaterialExpressionTextureSample, texture=texture, sampler_type=u.MaterialSamplerType.SAMPLERTYPE_COLOR)
    wire(moving, sample, 'UVs')
    return sample


def water_graph(m, texture):
    prop(scalar(m, .28), 'MP_ROUGHNESS')
    prop(scalar(m, .08), 'MP_SPECULAR')
    uv = node(m, u.MaterialExpressionTextureCoordinate)
    time = node(m, u.MaterialExpressionTime)
    sample = caustics_sample(m, texture, uv, time)
    # round6用已核验独立水纹素材建立清晰焦散层次；仍保留受光和动态法线，不是静态整塘图。-codex，2026-10-24
    absorption = node(m, u.MaterialExpressionCustom, output_type=u.CustomMaterialOutputType.CMOT_FLOAT3,
                      code='''float2 p=abs((UV-.5)/float2(.37,.35));
float shore=smoothstep(.36,.94,pow(pow(p.x,4)+pow(p.y,4),.25));
return C*.32*lerp(.70,1.0,shore);''',
                      inputs=[custom_input('UV'), custom_input('C')])
    wire(uv, absorption, 'UV')
    wire(sample, absorption, 'C', 'RGB')
    waterlight(m, absorption, multiply(m, sample, scalar(m, .30), 'RGB'), 'Water')
    opacity = node(m, u.MaterialExpressionCustom, output_type=u.CustomMaterialOutputType.CMOT_FLOAT1,
                   code='float2 p=abs((UV-.5)/float2(.37,.35)); float shore=smoothstep(.36,.94,pow(pow(p.x,4)+pow(p.y,4),.25)); return lerp(.78,.72,shore);',
                   inputs=[custom_input('UV')])
    wire(uv, opacity, 'UV')
    prop(opacity, 'MP_OPACITY')
    # 三组不同方向的解析坡度构成连续水纹法线；避免把岩石法线平移成水。
    # 不做顶点位移/折射，避免透明排序和切面裂缝，低角度高光由真实灯光提供。-codex，2026-10-23
    waves = node(m, u.MaterialExpressionCustom, output_type=u.CustomMaterialOutputType.CMOT_FLOAT3,
                 code='float2 p=UV+0.025*float2(sin(UV.y*17+T*.31),sin(UV.x*21-T*.27)); float a=dot(p,float2(43,17))+T*.62; float b=dot(p,float2(-21,57))-T*.79; float c=dot(p,float2(93,-52))+T*1.1; float2 slope=float2(.10,.038)*cos(a)+float2(-.035,.09)*cos(b)+float2(.035,-.022)*cos(c); return normalize(float3(-slope,1.0));',
                 inputs=[custom_input('UV'), custom_input('T')])
    wire(uv, waves, 'UV')
    wire(time, waves, 'T')
    prop(waves, 'MP_NORMAL')


def bed_graph(m, textures):
    # round4近景岩石法线透过水面，读成干燥粗糙塘底；底层只保留平滑暗底和动态聚光斑。
    # 外岸仍由M_StageRock承担岩石质感，不能将该法线再复用到水底。-codex，2026-10-24
    prop(color(m, (0., 0., 1.)), 'MP_NORMAL')
    prop(scalar(m, .88), 'MP_ROUGHNESS')
    prop(scalar(m, .10), 'MP_SPECULAR')
    uv = node(m, u.MaterialExpressionTextureCoordinate)
    time = node(m, u.MaterialExpressionTime)
    vc = node(m, u.MaterialExpressionVertexColor)
    sample = caustics_sample(m, textures['Caustics'], uv, time, .025)
    # 复用真实低于水面117cm的Bed；宽窄不等的聚光斑替代细胞边线。
    # 主底面改用归一UV，唇边/裙部通过原顶点色隔离，不把底部焦散贴到外壳。-codex，2026-10-24
    bed = node(m, u.MaterialExpressionCustom, output_type=u.CustomMaterialOutputType.CMOT_FLOAT3,
               code='''float2 q=abs((UV-.5)/float2(.37,.35));
float shallow=smoothstep(.32,.96,pow(pow(q.x,4)+pow(q.y,4),.25));
float3 bed=C*.28*lerp(.55,1.0,shallow);
return lerp(float3(.022,.029,.021),bed,smoothstep(.55,.64,VC.g));''',
               inputs=[custom_input('UV'), custom_input('C'), custom_input('VC')])
    wire(uv, bed, 'UV')
    wire(sample, bed, 'C', 'RGB')
    wire(vc, bed, 'VC')
    waterlight(m, bed, multiply(m, bed, scalar(m, .30)), 'Bed')


def bed(textures):
    m = begin('M_StageBed')
    bed_graph(m, textures)
    finish(m)


def cut_graph(m):
    # 切面采用低亮度Unlit透明示意，不模拟物理吸收；顶部水面仍保持受光PBR。
    # 若真实帧仍呈棕色，应查绑定/遮挡/排序，不继续加亮掩盖原因。-codex，2026-10-23
    m.set_editor_property('shading_model', u.MaterialShadingModel.MSM_UNLIT)
    if m.get_editor_property('shading_model') != u.MaterialShadingModel.MSM_UNLIT:
        raise RuntimeError('Cut shading_model回读未确认为MSM_UNLIT')
    u.log(f'MOTION_STAGE_CUT_SHADING_MODEL_OK {m.get_path_name()} MSM_UNLIT')
    uv = node(m, u.MaterialExpressionTextureCoordinate)
    v = node(m, u.MaterialExpressionComponentMask, r=False, g=True, b=False, a=False)
    wire(uv, v, 'Input')
    tint = node(m, u.MaterialExpressionLinearInterpolate)
    wire(color(m, (.010, .055, .038)), tint, 'A')
    wire(color(m, (.020, .12, .080)), tint, 'B')
    wire(v, tint, 'Alpha')
    prop(tint, 'MP_EMISSIVE_COLOR')
    opacity = node(m, u.MaterialExpressionAdd, const_b=.65)
    wire(multiply(m, v, scalar(m, -.15)), opacity, 'A')
    prop(opacity, 'MP_OPACITY')


def preflight_api():
    # transient材质不创建Content包；实际构造全部六套图先验证绑定和连线。
    # 任一失败由顶层抛出，尚不会导入贴图、清空既有图或保存资产。-codex，2026-10-23
    textures = {}
    for channel, (_, name, _) in TEXTURES.items():
        texture = A.load_asset(f'{DEST}/{name}') if A.does_asset_exist(f'{DEST}/{name}') else None
        if texture is None:
            texture = u.new_object(u.Texture2D)
            texture.set_editor_property('srgb', channel == 'Diffuse')
            texture.set_editor_property('compression_settings', u.TextureCompressionSettings.TC_NORMALMAP if channel == 'Normal' else u.TextureCompressionSettings.TC_DEFAULT)
        textures[channel] = texture
    textures['Caustics'] = u.new_object(u.Texture2D)
    graphs = [
        ('Rock', lambda m: rock_graph(m, textures, (.90, .85, .72)), False),
        ('Bed', lambda m: bed_graph(m, textures), False),
        ('Water', lambda m: water_graph(m, textures['Caustics']), True),
        ('Cut', cut_graph, True),
        ('Plant', lambda m: simple_graph(m, (.025, .08, .018), .86), False),
        ('Floor', lambda m: simple_graph(m, (.014, .019, .023), .82), False),
    ]
    for name, graph, translucent in graphs:
        transient = u.new_object(u.Material)
        transient.set_editor_property('blend_mode', u.BlendMode.BLEND_TRANSLUCENT if translucent else u.BlendMode.BLEND_OPAQUE)
        transient.set_editor_property('two_sided', True)
        if translucent:
            transient.set_editor_property('translucency_lighting_mode', u.TranslucencyLightingMode.TLM_SURFACE_PER_PIXEL_LIGHTING)
        graph(transient)
        u.log(f'MOTION_STAGE_GRAPH_PREFLIGHT_OK {name}')
    u.log('MOTION_STAGE_API_PREFLIGHT_OK graphs=6')


def water(texture):
    m = begin('M_StageWater', True)
    water_graph(m, texture)
    finish(m)


def cut():
    m = begin('M_StageCut', True)
    cut_graph(m)
    finish(m)


def simple_graph(m, tint, roughness):
    vc = node(m, u.MaterialExpressionVertexColor)
    prop(multiply(m, vc, color(m, tint)), 'MP_BASE_COLOR')
    prop(scalar(m, roughness), 'MP_ROUGHNESS')
    prop(scalar(m, .18), 'MP_SPECULAR')


def simple(name, tint, roughness):
    m = begin(name)
    simple_graph(m, tint, roughness)
    finish(m)


def main():
    selected = selected_materials(u.SystemLibrary.get_command_line())
    u.log(f'MOTION_STAGE_SELECTED {",".join(selected)}')
    preflight_api()
    # 先检查全部目标归属，避免处理一半才发现同名用户资产。-codex，2026-10-23
    for name in NAMES + [record[1] for record in TEXTURES.values()]:
        check_owned(name)
    check_owned(CAUSTICS_NAME)
    textures = import_textures()
    if 'Water' in selected or 'Bed' in selected:
        textures['Caustics'] = import_caustics()
    builders = {
        'Rock': lambda: rock('M_StageRock', textures, (.90, .85, .72)),
        'Bed': lambda: bed(textures),
        'Water': lambda: water(textures['Caustics']),
        'Cut': cut,
        'Plant': lambda: simple('M_StagePlant', (.025, .08, .018), .86),
        'Floor': lambda: simple('M_StageFloor', (.014, .019, .023), .82),
    }
    for name in selected:
        builders[name]()
    if NULL_RHI:
        u.log(f'MOTION_STAGE_GRAPH_SAVE_SUCCESS materials={len(selected)} textures={len(textures)} SHADER_NOT_VALIDATED')
    else:
        u.log(f'MOTION_STAGE_BUILD_SUCCESS materials={len(selected)} textures={len(textures)}')


try:
    main()
except Exception:
    u.log_error('MOTION_STAGE_BUILD_FAILED\n' + traceback.format_exc())
    raise
