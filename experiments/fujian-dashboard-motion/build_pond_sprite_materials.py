"""仅创建本批六张PNG纹理与一个UI水面材质；白名单、所有权与源SHA在首次写入前全部检查。
岸树/透明轮廓使用原UV，水面内域才扰动；不导入任何会顶层执行的旧环境脚本。
NullRHI仅保存材质图，不能代替SM5与原生画面验收。-codex，2026-11-03
"""
import hashlib
import json
import re
import struct
import traceback
from pathlib import Path

import unreal as u

ROOT = Path(__file__).resolve().parent
SOURCE_DIR = ROOT / 'SourceAssets/PondSprites'
DEST = '/Game/Motion/PondSprites'
OWNER = 'fujian-pond-sprites-14-20261103'
TAG = 'MotionPondSpriteOwner'
REVISION = '3'
NAMES = ('T_PondHero', 'T_PondOval', 'T_PondRect',
         'T_PondSmartBean', 'T_PondSmartRound', 'T_PondSmartRect', 'M_PondSpriteWater')
FILES = ('pond-hero.png', 'pond-oval.png', 'pond-rect.png',
         'pond-smart-bean.png', 'pond-smart-round.png', 'pond-smart-rect.png')
A, E = u.EditorAssetLibrary, u.MaterialEditingLibrary
NULL_RHI = re.search(r'(?:^|\s)-nullrhi(?=\s|$)', u.SystemLibrary.get_command_line(), re.I) is not None


def sha(data):
    return hashlib.sha256(data).hexdigest()


def node(material, cls, **props):
    value = E.create_material_expression(material, cls)
    if value is None:
        raise RuntimeError(f'无法创建节点：{cls}')
    for key, item in props.items():
        value.set_editor_property(key, item)
    return value


def custom(material, names, code, output_type):
    inputs = []
    for name in names:
        entry = u.CustomInput()
        entry.set_editor_property('input_name', name)
        inputs.append(entry)
    return node(material, u.MaterialExpressionCustom, inputs=inputs, code=code, output_type=output_type)


def wire(source, target, pin, output=''):
    inputs = [str(x) for x in E.get_material_expression_input_names(target)]
    outputs = [str(x) for x in E.get_material_expression_output_names(source)]
    if pin not in inputs or (output and output not in outputs):
        raise RuntimeError(f'针脚不符：{outputs} -> {inputs}; 请求={output}/{pin}')
    if not E.connect_material_expressions(source, output, target, pin):
        raise RuntimeError(f'无法连接：{output} -> {pin}')


def prop(source, property_name, output=''):
    if not E.connect_material_property(source, output, property_name):
        raise RuntimeError(f'材质属性连接失败：{property_name}')


def polygon_code(polygon, width, exclusion=False):
    coords = ','.join(f'float2({x/width:.9f},{y/width:.9f})' for x, y in polygon)
    count = len(polygon)
    # 排除区内部严格为1，外侧额外留8千分之一图宽的软保护，禁止设备边缘被邻近水样本扯动。
    # 水域沿用原来的内缩公式，原三图不改变范围。-codex，2026-11-03
    result = ('excluded=max(excluded,inside?1:1-smoothstep(0,.008,d));' if exclusion
              else 'geometry=max(geometry,inside?smoothstep(.007,.016,d):0);')
    return f'''{{float2 v[{count}]={{{coords}}};float d=100;bool inside=false;
for(int i=0,k={count-1};i<{count};k=i++){{
float2 a=v[k],b=v[i],e=b-a,w=p-a;
float2 q=w-e*saturate(dot(w,e)/max(dot(e,e),1e-8));d=min(d,length(q));
if((a.y>p.y)!=(b.y>p.y)){{if(p.x<(b.x-a.x)*(p.y-a.y)/(b.y-a.y)+a.x)inside=!inside;}}
}}
{result}}}'''


def mask_code(variants):
    # 多边形用像素记录，HLSL统一到图宽坐标后计算有符号边距；柔化并内缩岸线约13源像素。
    # 几何内域再结合水色排除植株/岩石；青蓝外沿即使同色也不能进入几何内域。-codex，2026-11-03
    chunks = ['float geometry=0;']
    for variant in variants:
        width, height = variant['size']
        chunks.append(f"if(abs(Shape-{variant['index']}.0)<.5){{float2 p=UV*float2(1,{height/width:.9f});float excluded=0;")
        for polygon in variant['water_polygons_px']:
            chunks.append(polygon_code(polygon, width))
        for polygon in variant['water_exclusions_px']:
            chunks.append(polygon_code(polygon, width, exclusion=True))
        chunks.append('geometry*=1-excluded;}')
    chunks.append('''float water=smoothstep(.008,.055,min(C.g,C.b)-C.r)*smoothstep(0,.035,C.b-C.g*.45);
return saturate(geometry*water);''')
    return '\n'.join(chunks)


def graph(material, texture, variants):
    material.set_editor_property('material_domain', u.MaterialDomain.MD_UI)
    material.set_editor_property('blend_mode', u.BlendMode.BLEND_TRANSLUCENT)
    # 本机Material.cpp的MD_UI普通分支使用EmissiveColor/Opacity；无需Surface照明或VertexColor节点。
    # Alpha保持原PNG一次，普通Slate Box shader负责再乘顶点淡出。-codex，2026-11-03
    uv = node(material, u.MaterialExpressionTextureCoordinate)
    clock = node(material, u.MaterialExpressionTime)
    shape = node(material, u.MaterialExpressionScalarParameter, parameter_name='ShapeType', default_value=0.)
    phase = node(material, u.MaterialExpressionScalarParameter, parameter_name='Phase', default_value=0.)
    original = node(material, u.MaterialExpressionTextureSampleParameter2D, parameter_name='PondTexture',
                    texture=texture, sampler_type=u.MaterialSamplerType.SAMPLERTYPE_COLOR)
    wire(uv, original, 'UVs')
    mask = custom(material, ('UV', 'Shape', 'C'), mask_code(variants), u.CustomMaterialOutputType.CMOT_FLOAT1)
    for source, pin, output in ((uv, 'UV', ''), (shape, 'Shape', ''), (original, 'C', 'RGB')):
        wire(source, mask, pin, output)
    flow = custom(material, ('UV', 'T', 'Phase', 'Mask'), '''float t=T+Phase;
float a=UV.y*47+UV.x*13-t*.81;
float b=UV.x*59-UV.y*23+t*.57;
float2 pixels=float2(1.1*sin(a)+.3*sin(b),.65*cos(a*.83+t*.21)+.2*cos(b));
return saturate(UV+pixels*fwidth(UV)*Mask);''', u.CustomMaterialOutputType.CMOT_FLOAT2)
    for source, pin in ((uv, 'UV'), (clock, 'T'), (phase, 'Phase'), (mask, 'Mask')):
        wire(source, flow, pin)
    moving = node(material, u.MaterialExpressionTextureSampleParameter2D, parameter_name='PondTexture',
                  texture=texture, sampler_type=u.MaterialSamplerType.SAMPLERTYPE_COLOR)
    wire(flow, moving, 'UVs')
    # 只柔化原三款水域的白亮焦散峰；保留阴暗水色、原UV透明度与全部非水域。
    # 新三款设备塘不应用压峰，材质不可用时静态PNG回退也保持原像素。-codex，2026-11-03
    light = custom(material, ('UV', 'T', 'Phase', 'Shape', 'Mask', 'Base', 'Moving'), '''if(Mask<=0)return Base;
float t=T+Phase;
float a=sin(UV.x*73+UV.y*97-t*.93);
float b=sin(UV.x*31-UV.y*61+t*.67);
float glint=.033*a*b+.016*sin(UV.x*25+UV.y*39+t*.41);
float3 color=lerp(Base,Moving,Mask)*(1+Mask*glint);
if(Shape<2.5){
float peak=max(color.r,max(color.g,color.b));
float highlight=smoothstep(.35,.80,peak);
color*=1-.20*Mask*highlight;
}
return color;''', u.CustomMaterialOutputType.CMOT_FLOAT3)
    for source, pin, output in ((uv, 'UV', ''), (clock, 'T', ''), (phase, 'Phase', ''), (shape, 'Shape', ''),
                                 (mask, 'Mask', ''), (original, 'Base', 'RGB'), (moving, 'Moving', 'RGB')):
        wire(source, light, pin, output)
    prop(light, u.MaterialProperty.MP_EMISSIVE_COLOR)
    prop(original, u.MaterialProperty.MP_OPACITY, 'A')
    if (material.get_editor_property('material_domain') != u.MaterialDomain.MD_UI
            or material.get_editor_property('blend_mode') != u.BlendMode.BLEND_TRANSLUCENT):
        raise RuntimeError('UI domain或透明混合模式回读不符')


def texture_properties():
    # RGBA8保留PNG软透明边，不改源尺寸或强行补为2幂；UI贴图不流送、不生成mip。-codex，2026-11-03
    return dict(srgb=True, compression_settings=u.TextureCompressionSettings.TC_EDITOR_ICON,
                compression_no_alpha=False, lod_group=u.TextureGroup.TEXTUREGROUP_UI,
                mip_gen_settings=u.TextureMipGenSettings.TMGS_NO_MIPMAPS, never_stream=True,
                address_x=u.TextureAddress.TA_CLAMP, address_y=u.TextureAddress.TA_CLAMP)


def save(asset, digest):
    A.set_metadata_tag(asset, TAG, OWNER)
    A.set_metadata_tag(asset, 'PondSpriteSourceSHA256', digest)
    A.set_metadata_tag(asset, 'PondSpriteRevision', REVISION)
    if not A.save_loaded_asset(asset, False):
        raise RuntimeError(f'资产保存失败：{asset.get_path_name()}')


def main():
    raw_manifest = (SOURCE_DIR / 'manifest.json').read_bytes()
    manifest = json.loads(raw_manifest)
    variants = manifest['variants']
    if (manifest['owner'] != OWNER or len(variants) != len(FILES)
            or [v['index'] for v in variants] != list(range(len(FILES)))
            or [v['texture'] for v in variants] != list(NAMES[:-1])
            or [v['file'] for v in variants] != list(FILES)
            or [v['presentation_vertical_scale'] for v in variants] != [1] * len(FILES)):
        raise RuntimeError('manifest不符合固定六变体白名单或原图等比约束')
    for entry in variants:
        data = (SOURCE_DIR / entry['file']).read_bytes()
        if (data[:8] != b'\x89PNG\r\n\x1a\n' or data[24:26] != b'\x08\x06'
                or list(struct.unpack('>II', data[16:24])) != entry['size'] or sha(data) != entry['sha256']):
            raise RuntimeError(f"源RGBA8 PNG尺寸或SHA不符：{entry['file']}")
        if len(entry['water_polygons_px']) != (2, 1, 3, 2, 1, 3)[entry['index']]:
            raise RuntimeError('水面数必须按六款顺序为2/1/3/2/1/3')
        if len(entry['water_exclusions_px']) != (0, 0, 0, 4, 4, 3)[entry['index']]:
            raise RuntimeError('设备排除区数与批准设备塘记录不符')
        width, height = entry['size']
        for polygon in entry['water_polygons_px'] + entry['water_exclusions_px']:
            if len(polygon) < 3 or any(not (0 <= x < width and 0 <= y < height) for x, y in polygon):
                raise RuntimeError(f"水域/设备排除多边形越界：{entry['file']}")
    build_digest = sha(raw_manifest + Path(__file__).read_bytes())
    targets = {}
    # 全部目标预检先于任何导入/保存；即使Registry未扫描到物理包，也直接加载核实归属。
    # 仅允许重建本批自有资产，不删除、不复制旧背景/舞台包。-codex，2026-11-03
    for name in NAMES:
        path = f'{DEST}/{name}'
        physical = ROOT / 'Content/Motion/PondSprites' / f'{name}.uasset'
        existing = u.load_object(None, f'{path}.{name}') if physical.exists() or A.does_asset_exist(path) else None
        expected = u.Material if name == NAMES[-1] else u.Texture2D
        if (physical.exists() or A.does_asset_exist(path)) and existing is None:
            raise RuntimeError(f'目标包存在但无法加载：{path}')
        if existing is not None and (not isinstance(existing, expected) or A.get_metadata_tag(existing, TAG) != OWNER):
            raise RuntimeError(f'拒绝覆盖非本批或类型不符资产：{path}')
        targets[name] = existing
    probe = u.new_object(u.Texture2D)
    for key, value in texture_properties().items():
        probe.set_editor_property(key, value)
        if probe.get_editor_property(key) != value:
            raise RuntimeError(f'纹理属性预检回读不符：{key}')
    graph(u.new_object(u.Material), probe, variants)
    u.log(f'POND_SPRITE_API_PREFLIGHT_OK targets={len(NAMES)}')
    textures = []
    for entry in variants:
        name, digest = entry['texture'], entry['sha256']
        texture = targets[name]
        if texture is None or A.get_metadata_tag(texture, 'PondSpriteSourceSHA256') != digest:
            # 再核对源文件，避免预检到导入之间源素材被并发替换。-codex，2026-11-03
            source = SOURCE_DIR / entry['file']
            if sha(source.read_bytes()) != digest:
                raise RuntimeError(f'导入前来源变化：{source}')
            task = u.AssetImportTask()
            for key, value in dict(filename=str(source), destination_path=DEST, destination_name=name,
                                   automated=True, replace_existing=True, save=False).items():
                task.set_editor_property(key, value)
            u.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
            texture = A.load_asset(f'{DEST}/{name}')
            if not isinstance(texture, u.Texture2D):
                raise RuntimeError(f'贴图导入失败：{name}')
            A.set_metadata_tag(texture, TAG, OWNER)
        changed = (A.get_metadata_tag(texture, 'PondSpriteSourceSHA256') != digest
                   or A.get_metadata_tag(texture, 'PondSpriteRevision') != REVISION)
        for key, value in texture_properties().items():
            if texture.get_editor_property(key) != value:
                texture.set_editor_property(key, value)
                changed = True
            if texture.get_editor_property(key) != value:
                raise RuntimeError(f'纹理设置回读不符：{name}/{key}')
        size = [texture.blueprint_get_size_x(), texture.blueprint_get_size_y()]
        if size != entry['size']:
            raise RuntimeError(f"导入尺寸变化：{name} actual={size} expected={entry['size']}")
        if changed:
            save(texture, digest)
        if (A.get_metadata_tag(texture, TAG) != OWNER
                or A.get_metadata_tag(texture, 'PondSpriteSourceSHA256') != digest
                or A.get_metadata_tag(texture, 'PondSpriteRevision') != REVISION):
            raise RuntimeError(f'纹理所有权/来源SHA/版本回读不符：{name}')
        textures.append(texture)
        u.log(f'POND_SPRITE_TEXTURE_READY {name} sha256={digest} changed={changed}')
    material = targets[NAMES[-1]]
    if material is None:
        material = u.AssetToolsHelpers.get_asset_tools().create_asset(NAMES[-1], DEST, u.Material, u.MaterialFactoryNew())
        if not isinstance(material, u.Material):
            raise RuntimeError('创建UI材质失败')
        A.set_metadata_tag(material, TAG, OWNER)
        if not A.save_loaded_asset(material, False):
            raise RuntimeError('首次材质归属保存失败')
    if (A.get_metadata_tag(material, 'PondSpriteSourceSHA256') != build_digest
            or A.get_metadata_tag(material, 'PondSpriteRevision') != REVISION):
        E.delete_all_material_expressions(material)
        graph(material, textures[0], variants)
        E.layout_material_expressions(material)
        save(material, build_digest)
        u.log('POND_SPRITE_MATERIAL_GRAPH_SAVED')
    else:
        u.log('POND_SPRITE_MATERIAL_REUSED')
    # NullRHI保存后再用真实RHI执行时，即便图未变化也必须编译；幂等复用不能跳过该门槛。-codex，2026-11-03
    if not NULL_RHI:
        errors = E.recompile_material(material)
        if errors:
            raise RuntimeError(f'UI水面材质编译失败：{list(errors)}')
        if not A.save_loaded_asset(material, False):
            raise RuntimeError('编译后材质保存失败')
    for entry in variants:
        if sha((SOURCE_DIR / entry['file']).read_bytes()) != entry['sha256']:
            raise RuntimeError('完成时来源SHA变化；停止验收')
    u.log(f'POND_SPRITE_BUILD_SUCCESS assets={len(NAMES)} nullrhi={NULL_RHI}; native dimensions/SM5/GC/alpha/water/device acceptance required')


try:
    main()
except Exception:
    u.log_error('POND_SPRITE_BUILD_FAILED\n' + traceback.format_exc())
    raise
