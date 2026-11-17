"""第二版双页环境资产导入；只写白名单独占名称及具有本脚本所有权的资产。
在 UE5.8 PythonScript commandlet 中执行；NullRHI 只保存图，不声称着色器/视觉通过。
City/Pond以原图回退并接运行期视频；Data保留独立水光分支。-codex，2026-10-26
"""
import hashlib
import re
import traceback
from pathlib import Path

import unreal as u

ROOT = Path(__file__).resolve().parent
SOURCE = ROOT / 'SourceAssets/Stage/pond-environment-clean.png'
DEST = '/Game/Motion'
NAMES = ('T_PondEnvironment', 'M_PondEnvironment', 'M_DataEnvironment')
VIDEO_NAME = 'MT_PondBackground'
OWNER = 'fujian-dashboard-motion-environment-20261024'
TAG = 'MotionEnvironmentOwner'
A = u.EditorAssetLibrary
E = u.MaterialEditingLibrary
NULL_RHI = re.search(r'(?:^|\s)-nullrhi(?=\s|$)', u.SystemLibrary.get_command_line(), re.IGNORECASE) is not None


def selected_materials(command_line):
    # 默认保留旧双页入口；本轮用Data白名单仅重建总览背景，不读写鱼塘页图片。-codex，2026-10-26
    allowed = ('Pond', 'Data')
    values = re.findall(r'(?:^|\s)-EnvironmentMaterials(?:=("[^"]*"|\S*))?(?=\s|$)', command_line, re.IGNORECASE)
    if not values:
        return allowed
    if len(values) != 1:
        raise RuntimeError('EnvironmentMaterials只能指定一次')
    selected = tuple(values[0].strip('"').split(','))
    if not selected or len(set(selected)) != len(selected) or any(name not in allowed for name in selected):
        raise RuntimeError(f'EnvironmentMaterials必须为不重复的严格白名单：{allowed}；收到={selected}')
    return selected


def custom_input(name):
    value = u.CustomInput()
    value.set_editor_property('input_name', name)
    return value


def owned(name):
    path = f'{DEST}/{name}'
    if not A.does_asset_exist(path):
        return None
    asset = A.load_asset(path)
    if not asset or A.get_metadata_tag(asset, TAG) != OWNER:
        raise RuntimeError(f'拒绝覆盖非本轮资产：{path}')
    return asset


def node(material, cls, **properties):
    result = E.create_material_expression(material, cls)
    if not result:
        raise RuntimeError(f'无法创建节点：{cls}')
    for key, value in properties.items():
        result.set_editor_property(key, value)
    return result


def prop(source, output=''):
    if not E.connect_material_property(source, output, u.MaterialProperty.MP_EMISSIVE_COLOR):
        raise RuntimeError('无法连接环境 Emissive')


def pond_photo_motion(material, texture, uv):
    # 原图含鱼群/岩石，不能整图平移或强扭曲；固定边缘，鱼群与前景区域减弱折射。
    # 仅重采样原图，不改纹理像素；两层不同方向的连续波相位模拟水中光路。-codex，2026-10-26
    time = node(material, u.MaterialExpressionTime)
    region = node(material, u.MaterialExpressionCustom,
                  output_type=u.CustomMaterialOutputType.CMOT_FLOAT1,
                  inputs=[custom_input('UV')],
                  code='''float edge=min(min(UV.x,1-UV.x),min(UV.y,1-UV.y));
float fixedEdge=smoothstep(.015,.085,edge);
float upperFish=(1-smoothstep(.46,.59,UV.x))*(1-smoothstep(.22,.32,UV.y));
float2 fish=(UV-float2(.36,.79))/float2(.25,.13);
float lowerFish=1-smoothstep(.65,1.18,length(fish));
float rocks=smoothstep(.69,.88,UV.y)*max(1-smoothstep(.19,.39,UV.x),smoothstep(.77,.92,UV.x));
float leftFish=(1-smoothstep(.11,.23,UV.x))*smoothstep(.35,.44,UV.y)*(1-smoothstep(.60,.69,UV.y));
return fixedEdge*(1-.92*saturate(max(max(upperFish,lowerFish),max(rocks,leftFish))));''')
    if not E.connect_material_expressions(uv, '', region, 'UV'):
        raise RuntimeError('图片静稳区域 UV 连接失败')
    refracted = node(material, u.MaterialExpressionCustom,
                     output_type=u.CustomMaterialOutputType.CMOT_FLOAT2,
                     inputs=[custom_input(n) for n in ('UV', 'T', 'Mask')],
                     code='''float a=UV.y*38+UV.x*9-T*1.15;
float b=UV.x*31-UV.y*17+T*.83;
float2 flow=float2(.0035*sin(a),.0018*cos(a*.73+T*.31));
flow+=float2(.0015*sin(b),.0012*cos(b*.81-T*.27));
return saturate(UV+flow*Mask);''')
    for source, pin in ((uv, 'UV'), (time, 'T'), (region, 'Mask')):
        if not E.connect_material_expressions(source, '', refracted, pin):
            raise RuntimeError(f'图片折射连接失败：{pin}')
    sample = node(material, u.MaterialExpressionTextureSample, texture=texture,
                  sampler_type=u.MaterialSamplerType.SAMPLERTYPE_COLOR)
    if not E.connect_material_expressions(refracted, '', sample, 'UVs'):
        raise RuntimeError('图片折射采样 UV 连接失败')
    glints = node(material, u.MaterialExpressionCustom,
                  output_type=u.CustomMaterialOutputType.CMOT_FLOAT3,
                  inputs=[custom_input(n) for n in ('UV', 'T', 'Mask', 'C')],
                  code='''float luminance=dot(C,float3(.21,.72,.07));
float detail=smoothstep(.045,.30,luminance);
float water=smoothstep(.24,.46,UV.y);
float wave=sin(UV.x*61+UV.y*85-T*1.4)*sin(UV.y*34-UV.x*19+T*.9);
float2 beamPos=(UV-float2(.69,.25))/float2(.18,.31);
float beam=exp(-dot(beamPos,beamPos)*2.0);
float pulse=.18*detail*water*wave+.075*beam*sin(UV.x*24-UV.y*11+T*.72);
return C*(1+Mask*pulse);''')
    for source, pin, output in ((uv, 'UV', ''), (time, 'T', ''), (region, 'Mask', ''), (sample, 'C', 'RGB')):
        if not E.connect_material_expressions(source, output, glints, pin):
            raise RuntimeError(f'图片局部光纹连接失败：{pin}')
    return glints


def video_background(material, texture, video, uv):
    # 静态图和视频共用原始UV，不调用旧pond_photo_motion；视频自身承担运动，禁止二次扭动/明灭。
    # Ready默认0，运行期绑定NewStyleOutput媒体纹理并确认可用帧后置1；失败回0。-codex，2026-10-26
    still = node(material, u.MaterialExpressionTextureSample, texture=texture,
                 sampler_type=u.MaterialSamplerType.SAMPLERTYPE_COLOR)
    moving = node(material, u.MaterialExpressionTextureSampleParameter2D,
                  parameter_name='VideoTexture', texture=video,
                  sampler_type=u.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
    ready = node(material, u.MaterialExpressionScalarParameter,
                 parameter_name='VideoReady', default_value=0.0)
    blend = node(material, u.MaterialExpressionLinearInterpolate)
    for sample in (still, moving):
        if not E.connect_material_expressions(uv, '', sample, 'UVs'):
            raise RuntimeError('背景视频/回退图原始UV连接失败')
    for source, output, pin in ((still, 'RGB', 'A'), (moving, 'RGB', 'B'), (ready, '', 'Alpha')):
        if not E.connect_material_expressions(source, output, blend, pin):
            raise RuntimeError(f'背景视频混合连接失败：{pin}')
    return blend


def configure_video(texture):
    # 与运行期Stage的媒体纹理合同一致；普通2D线性采样避免External类型与sRGB重复转换。
    # 不绑定媒体播放器，资产仅作为材质编译默认值，由Stage持有运行期播放器/纹理。-codex，2026-10-26
    for key, value in dict(new_style_output=True, srgb=False,
                           address_x=u.TextureAddress.TA_CLAMP, address_y=u.TextureAddress.TA_CLAMP,
                           auto_clear=False).items():
        texture.set_editor_property(key, value)
        if texture.get_editor_property(key) != value:
            raise RuntimeError(f'媒体纹理属性不符：{key}')


def media_texture():
    texture = owned(VIDEO_NAME)
    if texture is None:
        texture = u.AssetToolsHelpers.get_asset_tools().create_asset(
            VIDEO_NAME, DEST, u.MediaTexture, u.MediaTextureFactoryNew())
    if not isinstance(texture, u.MediaTexture):
        raise RuntimeError(f'资产不是MediaTexture：{VIDEO_NAME}')
    configure_video(texture)
    texture.update_resource()
    save(texture)
    return texture


def graph(material, texture=None, caustics=None, video=None):
    material.set_editor_property('blend_mode', u.BlendMode.BLEND_OPAQUE)
    material.set_editor_property('two_sided', True)
    material.set_editor_property('shading_model', u.MaterialShadingModel.MSM_UNLIT)
    if material.get_editor_property('shading_model') != u.MaterialShadingModel.MSM_UNLIT:
        raise RuntimeError('环境必须为 Unlit')
    uv = node(material, u.MaterialExpressionTextureCoordinate)
    water_uv = uv
    time = None
    if texture is None:
        # 焦散素材未承诺可平铺，始终在内部区域连续漂移，禁止frac回绕造成跳帧。
        # Time仅在总览分支进入图，鱼塘页仍用原始UV与图片。-codex，2026-10-26
        time = node(material, u.MaterialExpressionTime)
        water_uv = node(material, u.MaterialExpressionCustom,
                        output_type=u.CustomMaterialOutputType.CMOT_FLOAT2,
                        inputs=[custom_input('UV'), custom_input('T')],
                        code='return UV*.82+.09+float2(.035*sin(T*.42),.028*sin(T*.34+.6));')
        if not E.connect_material_expressions(uv, '', water_uv, 'UV') or not E.connect_material_expressions(time, '', water_uv, 'T'):
            raise RuntimeError('海水动态焦散 UV 连接失败')
    water_sample = node(material, u.MaterialExpressionTextureSample, texture=caustics,
                        sampler_type=u.MaterialSamplerType.SAMPLERTYPE_COLOR)
    if not E.connect_material_expressions(water_uv, '', water_sample, 'UVs'):
        raise RuntimeError('局部水光素材 UV 连接失败')
    if texture is not None:
        if video is None:
            raise RuntimeError('Pond背景缺少材质编译用媒体纹理')
        base, base_output = video_background(material, texture, video, uv), ''
    else:
        # 底色和接触阴影保持静止，仅波峰与焦散缓流；以两秒可察觉为调校目标，实帧仍需核验。
        # 这是远平面上的起伏表现，不改塘体或接触锚点，也不模拟真实水面几何。-codex，2026-10-26
        uv_input = u.CustomInput()
        uv_input.set_editor_property('input_name', 'UV')
        water_input = u.CustomInput()
        water_input.set_editor_property('input_name', 'W')
        tint = node(material, u.MaterialExpressionCustom,
                    output_type=u.CustomMaterialOutputType.CMOT_FLOAT3,
                    inputs=[uv_input, water_input, custom_input('T')],
                    code='''float2 delta=(UV-float2(.34,.58))*float2(2,1.5);
float glow=exp(-dot(delta,delta)*3);
float2 p=UV*float2(210,340)+T*float2(1.35,-.95);
p+=float2(sin(p.y*.27+sin(p.x*.17))*3.2,cos(p.x*.23+sin(p.y*.19))*2.7);
float w=sin(p.x+sin(p.y*.67)*2.1)*sin(p.y+cos(p.x*.57)*1.8);
float ripple=pow(saturate(.5+.5*w),7);
float mask=(1-smoothstep(.57,.68,UV.x))*smoothstep(.23,.46,UV.y);
float2 ellipse=(UV-float2(.3385,.575))*float2(3.2,2.8);
float rim=exp(-pow((length(ellipse)-1.0)*11,2))*smoothstep(.50,.82,UV.y);
float rimBreak=.08+.92*pow(saturate(dot(W,float3(.21,.72,.07))*2.5),1.4);
return float3(.002,.013,.009)+glow*float3(.003,.024,.014)
+ripple*mask*float3(.006,.038,.029)
+mask*pow(saturate(dot(W,float3(.21,.72,.07))*2.0),2)*float3(.003,.020,.016)
+rim*mask*rimBreak*float3(.010,.070,.035);''')
        if not E.connect_material_expressions(uv, '', tint, 'UV'):
            raise RuntimeError('数据环境 UV 连接失败')
        if not E.connect_material_expressions(water_sample, 'RGB', tint, 'W'):
            raise RuntimeError('环脚水光素材连接失败')
        if not E.connect_material_expressions(time, '', tint, 'T'):
            raise RuntimeError('海水起伏时间连接失败')
        base, base_output = tint, ''
    # 软阴影/反光只作用于已有背景；位置由真实塘底投影实时提供，避免烘焙固定塘体。
    # 参数未设置时半径为零，不产生假阴影；高斯衰减没有硬椭圆边。-codex，2026-10-24
    inputs = []
    for name in ['UV', 'Base', 'W'] + [f'C{i}' for i in range(6)]:
        value = u.CustomInput()
        value.set_editor_property('input_name', name)
        inputs.append(value)
    # 数据页反光止于图表左界；反光仅在投影塘底与环脚附近增强，不抬高全画面底色。
    # 环脚椭圆沿立面下移到真正前沿下方，避免增强的光仍被Slate玉带遮住。-codex，2026-10-24
    reflection_mask = '((1-smoothstep(.61,.68,UV.x))*smoothstep(.23,.46,UV.y))' if texture is None else '1.0'
    grounding = node(material, u.MaterialExpressionCustom,
                     output_type=u.CustomMaterialOutputType.CMOT_FLOAT3, inputs=inputs,
                     code='''float3 contacts[6]={C0,C1,C2,C3,C4,C5};
float shadow=0,reflection=0;
float wave=pow(saturate(dot(W,float3(.21,.72,.07))*2.5),1.4);
for(int i=0;i<6;i++){
float3 c=contacts[i]; if(c.z>0.0001){
float2 q=(UV-c.xy)/float2(c.z,c.z*.48);
float soft=exp(-dot(q,q)*2.6)*.48;
float contact=exp(-q.x*q.x*2.2-pow(q.y+.08,2)*13)*.48;
shadow=max(shadow,1-(1-soft)*(1-contact));
float2 r=(UV-c.xy-float2(0,c.z*.45))/float2(c.z*1.05,c.z*.98);
r.x+=sin(UV.y*180+sin(UV.x*95))*.10;
reflection+=exp(-dot(r,r)*1.9)*(.10+.90*wave);
}}
return Base*(1-shadow)+min(reflection,1.0)*float3(.006,.045,.028)*''' + reflection_mask + ';')
    if not E.connect_material_expressions(uv, '', grounding, 'UV'):
        raise RuntimeError('接触阴影 UV 连接失败')
    if not E.connect_material_expressions(base, base_output, grounding, 'Base'):
        raise RuntimeError('接触阴影底色连接失败')
    if not E.connect_material_expressions(water_sample, 'RGB', grounding, 'W'):
        raise RuntimeError('塘底水光素材连接失败')
    for i in range(6):
        parameter = node(material, u.MaterialExpressionVectorParameter,
                         parameter_name=f'Contact{i}', default_value=u.LinearColor(0, 0, 0, 0))
        if not E.connect_material_expressions(parameter, '', grounding, f'C{i}'):
            raise RuntimeError(f'接触阴影参数连接失败：{i}')
    prop(grounding)


def save(asset):
    A.set_metadata_tag(asset, TAG, OWNER)
    if not A.save_loaded_asset(asset, False):
        raise RuntimeError(f'资产保存失败：{asset.get_path_name()}')


def material(name, texture=None, caustics=None, video=None):
    asset = owned(name)
    if asset is None:
        asset = u.AssetToolsHelpers.get_asset_tools().create_asset(name, DEST, u.Material, u.MaterialFactoryNew())
    if not isinstance(asset, u.Material):
        raise RuntimeError(f'资产不是 Material：{name}')
    E.delete_all_material_expressions(asset)
    graph(asset, texture, caustics, video)
    E.layout_material_expressions(asset)
    if not NULL_RHI:
        errors = E.recompile_material(asset)
        if errors:
            raise RuntimeError(f'材质编译失败：{name}: {list(errors)}')
    save(asset)
    u.log(f'MOTION_ENVIRONMENT_MATERIAL_SAVED {name} shader_validated={not NULL_RHI}')


def main():
    selected = selected_materials(u.SystemLibrary.get_command_line())
    u.log(f'MOTION_ENVIRONMENT_SELECTED {",".join(selected)}')
    digest = None
    if 'Pond' in selected:
        if not SOURCE.is_file():
            raise RuntimeError(f'干净环境图不存在：{SOURCE}')
        digest = hashlib.sha256(SOURCE.read_bytes()).hexdigest()
    # 只读复用舞台脚本拥有的焦散贴图；先执行build_stage_materials.py，环境脚本不取得其写入权。-codex，2026-10-24
    caustics = A.load_asset('/Game/Motion/T_StageWaterCaustics')
    if not isinstance(caustics, u.Texture2D) or A.get_metadata_tag(caustics, 'MotionStageOwner') != 'fujian-dashboard-motion-stage-20261023' or A.get_metadata_tag(caustics, 'SourceSHA256') != 'bc6733405b0fe4e9eb952cac2ad6c9947ed5cfeca81687db43f7708cb4e4e6fa':
        raise RuntimeError('焦散贴图缺失或来源不符，请先执行build_stage_materials.py -StageMaterials=Water,Bed')
    # 写入前检查所有名称和完整节点API；不触及原六种舞台材质。-codex，2026-10-24
    targets = (list(NAMES[:2]) + [VIDEO_NAME] if 'Pond' in selected else []) + ([NAMES[2]] if 'Data' in selected else [])
    for name in targets:
        existing = owned(name)
        expected = u.MediaTexture if name == VIDEO_NAME else (u.Texture2D if name.startswith('T_') else u.Material)
        if existing is not None and not isinstance(existing, expected):
            raise RuntimeError(f'资产类型不符：{name}')
    if 'Pond' in selected:
        placeholder = u.new_object(u.Texture2D)
        # 在任何资产落盘前验证Python反射属性与节点；工厂不可用也提前失败。-codex，2026-10-26
        u.MediaTextureFactoryNew()
        video_placeholder = u.new_object(u.MediaTexture)
        configure_video(video_placeholder)
        graph(u.new_object(u.Material), placeholder, caustics, video_placeholder)
    if 'Data' in selected:
        graph(u.new_object(u.Material), caustics=caustics)
    u.log(f'MOTION_ENVIRONMENT_API_PREFLIGHT_OK graphs={len(selected)}')
    if 'Pond' in selected:
        import_pond(digest, caustics)
    if 'Data' in selected:
        material(NAMES[2], caustics=caustics)
    u.log(f'MOTION_ENVIRONMENT_SUCCESS selected={",".join(selected)} source_sha256={digest} assets={len(targets)} shader_validated={not NULL_RHI}')


def import_pond(digest, caustics):
    # Data选择不触发媒体资产；既有图片仍按来源/采样状态只读复用。-codex，2026-10-26
    video = media_texture()
    texture = owned(NAMES[0])
    # 来源和采样状态一致就只读复用；只重建材质，不因动效试用重存已有图片资产。-codex，2026-10-26
    if texture is not None and A.get_metadata_tag(texture, 'SourceSHA256') == digest:
        if (not isinstance(texture, u.Texture2D) or not texture.get_editor_property('srgb')
                or texture.get_editor_property('compression_settings') != u.TextureCompressionSettings.TC_DEFAULT
                or texture.get_editor_property('address_x') != u.TextureAddress.TA_CLAMP
                or texture.get_editor_property('address_y') != u.TextureAddress.TA_CLAMP):
            raise RuntimeError('已有环境贴图采样状态不符，拒绝静默改写')
        u.log(f'MOTION_ENVIRONMENT_TEXTURE_REUSED {NAMES[0]} source_sha256={digest}')
        material(NAMES[1], texture, caustics, video)
        return
    if texture is None or A.get_metadata_tag(texture, 'SourceSHA256') != digest:
        task = u.AssetImportTask()
        for key, value in dict(filename=str(SOURCE), destination_path=DEST, destination_name=NAMES[0],
                               automated=True, replace_existing=True, save=False).items():
            task.set_editor_property(key, value)
        u.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
        texture = A.load_asset(f'{DEST}/{NAMES[0]}')
    if not isinstance(texture, u.Texture2D):
        raise RuntimeError('环境贴图导入失败')
    texture.set_editor_property('srgb', True)
    texture.set_editor_property('compression_settings', u.TextureCompressionSettings.TC_DEFAULT)
    texture.set_editor_property('address_x', u.TextureAddress.TA_CLAMP)
    texture.set_editor_property('address_y', u.TextureAddress.TA_CLAMP)
    A.set_metadata_tag(texture, 'SourceSHA256', digest)
    save(texture)
    material(NAMES[1], texture, caustics, video)


try:
    main()
except Exception:
    u.log_error('MOTION_ENVIRONMENT_FAILED\n' + traceback.format_exc())
    raise
