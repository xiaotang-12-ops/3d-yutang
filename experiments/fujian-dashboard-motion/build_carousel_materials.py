"""仅创建本批五个城市轮转衍生材质，原M_Stage图和背景保持只读。
完整复制原图后追加CarouselOpacity；不透明部件用Engine DitherTemporalAA裁切，透明部件保留原透明度再相乘。
必须由主任务关B后执行；NullRHI保存图不代表SM5渲染验收。-codex，2026-10-31
"""
import hashlib
import re
import traceback
from pathlib import Path

import unreal as u

ROOT = Path(__file__).resolve().parent
E, A = u.MaterialEditingLibrary, u.EditorAssetLibrary
OWNER = 'fujian-carousel-13-20261031'
TAG = 'MotionCarouselOwner'
REVISION = '1'
KINDS = ('Rock', 'Water', 'Bed', 'Cut', 'Plant')
DITHER = '/Engine/Functions/Engine_MaterialFunctions02/Utility/DitherTemporalAA.DitherTemporalAA'
NULL_RHI = re.search(r'(?:^|\s)-nullrhi(?=\s|$)', u.SystemLibrary.get_command_line(), re.I) is not None


def node(material, cls, **props):
    result = E.create_material_expression(material, cls)
    if result is None:
        raise RuntimeError(f'节点创建失败：{cls}')
    for key, value in props.items():
        result.set_editor_property(key, value)
    return result


def wire(source, target, pin, output=''):
    inputs = [str(x) for x in E.get_material_expression_input_names(target)]
    outputs = [str(x) for x in E.get_material_expression_output_names(source)]
    if pin not in inputs or (output and output not in outputs):
        raise RuntimeError(f'针脚不符：{outputs} -> {inputs}; 请求={output!r}/{pin!r}')
    if not E.connect_material_expressions(source, output, target, pin):
        raise RuntimeError(f'连线失败：{source.get_class().get_name()} -> {pin}')


def prop(source, material_property, output=''):
    if not E.connect_material_property(source, output, material_property):
        raise RuntimeError(f'属性连线失败：{material_property}')


def add_fade(material, kind, dither_function):
    fade = node(material, u.MaterialExpressionScalarParameter,
                parameter_name='CarouselOpacity', default_value=1.)
    if kind in ('Water', 'Cut'):
        original = E.get_material_property_input_node(material, u.MaterialProperty.MP_OPACITY)
        output = E.get_material_property_input_node_output_name(material, u.MaterialProperty.MP_OPACITY)
        if original is None:
            raise RuntimeError(f'{kind}原材质缺少Opacity；拒绝丢弃原透明结构')
        multiply = node(material, u.MaterialExpressionMultiply)
        wire(original, multiply, 'A', output)
        wire(fade, multiply, 'B')
        prop(multiply, u.MaterialProperty.MP_OPACITY)
        if material.get_editor_property('blend_mode') != u.BlendMode.BLEND_TRANSLUCENT:
            raise RuntimeError(f'{kind}原透明模式不符')
    else:
        # 本地头文件确认SetMaterialFunction为BlueprintCallable；实际函数针脚在运行时回读，拒绝猜接口。
        # masked保留真实深度写入；不用整塘translucent代替遮挡。-codex，2026-10-31
        call = node(material, u.MaterialExpressionMaterialFunctionCall)
        if not call.set_material_function(dither_function):
            raise RuntimeError('DitherTemporalAA函数绑定失败')
        inputs = [str(x) for x in E.get_material_expression_input_names(call)]
        alpha_pins = [x for x in inputs if re.sub(r'[^a-z]', '', x.lower()) == 'alphathreshold']
        if len(alpha_pins) != 1:
            raise RuntimeError(f'DitherTemporalAA输入与本机预期不符：{inputs}')
        outputs = [str(x) for x in E.get_material_expression_output_names(call)]
        if len(outputs) != 1:
            raise RuntimeError(f'DitherTemporalAA输出不唯一：{outputs}')
        wire(fade, call, alpha_pins[0])
        material.set_editor_property('blend_mode', u.BlendMode.BLEND_MASKED)
        prop(call, u.MaterialProperty.MP_OPACITY_MASK, outputs[0])
        material.set_editor_property('opacity_mask_clip_value', .3333)
        u.log(f'CAROUSEL_DITHER_PINS inputs={inputs!r} outputs={outputs!r}')


def main():
    # commandlet的Asset Registry可能尚未收录Engine函数；包实际存在不能据Registry空项判为缺失。
    # 本地PyCore.cpp确认load_object直接StaticLoadObject，按已核验的/Engine挂载路径读包。-codex，2026-10-31
    engine_content = u.Paths.convert_relative_path_to_full(u.Paths.engine_content_dir())
    engine_file = Path(engine_content) / 'Functions/Engine_MaterialFunctions02/Utility/DitherTemporalAA.uasset'
    if not engine_file.is_file():
        raise RuntimeError(f'DitherTemporalAA物理文件缺失：{engine_file.resolve()}')
    u.log(f'CAROUSEL_DITHER_PACKAGE file={engine_file.resolve()} object={DITHER}')
    dither = u.load_object(None, DITHER, u.MaterialFunction)
    if not isinstance(dither, u.MaterialFunction):
        raise RuntimeError(f'DitherTemporalAA直接包加载失败或类型不符：{DITHER}')
    records = []
    # 先检查全部来源和目标归属，任何目标冲突都在第一次资产写入前失败。-codex，2026-10-31
    for kind in KINDS:
        source_path = f'/Game/Motion/M_Stage{kind}'
        target_path = f'/Game/Motion/M_Carousel{kind}'
        source = A.load_asset(source_path)
        if not isinstance(source, u.Material):
            raise RuntimeError(f'源材质不可读：{source_path}')
        source_file = ROOT / 'Content' / 'Motion' / f'M_Stage{kind}.uasset'
        source_digest = hashlib.sha256(source_file.read_bytes()).hexdigest()
        target = A.load_asset(target_path) if A.does_asset_exist(target_path) else None
        if target is not None:
            if not isinstance(target, u.Material) or A.get_metadata_tag(target, TAG) != OWNER:
                raise RuntimeError(f'拒绝覆盖非本批资产：{target_path}')
            if A.get_metadata_tag(target, 'CarouselRevision') != REVISION or A.get_metadata_tag(target, 'CarouselSourceSHA256') != source_digest:
                raise RuntimeError(f'衍生图未完成或来源已变化，需明确修复此衍生资产：{target_path}')
        records.append((kind, source_path, target_path, target, source_digest))
    # transient上先验证函数/参数/Opacity连线，探针不保存资产；不调用原脚本main，不导入贴图。
    # transient水图只为验证接线API，成品仍复制真实原图。-codex，2026-10-31
    for kind in ('Rock', 'Water'):
        probe = u.new_object(u.Material)
        if kind == 'Water':
            probe.set_editor_property('blend_mode', u.BlendMode.BLEND_TRANSLUCENT)
            prop(node(probe, u.MaterialExpressionConstant, r=.75), u.MaterialProperty.MP_OPACITY)
        add_fade(probe, kind, dither)
    u.log('CAROUSEL_API_PREFLIGHT_OK')
    for kind, source_path, target_path, target, source_digest in records:
        if target is not None:
            u.log(f'CAROUSEL_MATERIAL_REUSED {target_path}')
            continue
        material = A.duplicate_asset(source_path, target_path)
        if not isinstance(material, u.Material):
            raise RuntimeError(f'复制失败：{source_path} -> {target_path}')
        A.set_metadata_tag(material, TAG, OWNER)
        # 先持久化归属，后续连线若失败仍能识别为本批未完成资产，禁止下一次误当外来资产覆盖。
        # 不自动删除/重建失败资产，避免重复追加Fade链。-codex，2026-10-31
        if not A.save_loaded_asset(material, False):
            raise RuntimeError(f'归属保存失败：{target_path}')
        add_fade(material, kind, dither)
        E.layout_material_expressions(material)
        if not NULL_RHI:
            errors = E.recompile_material(material)
            if errors:
                raise RuntimeError(f'{target_path}编译失败：{errors}')
        A.set_metadata_tag(material, 'CarouselRevision', REVISION)
        A.set_metadata_tag(material, 'CarouselSourceSHA256', source_digest)
        if not A.save_loaded_asset(material, False):
            raise RuntimeError(f'保存失败：{target_path}')
        status = 'GRAPH_SAVED_SHADER_NOT_VALIDATED' if NULL_RHI else 'RECOMPILE_RETURNED'
        u.log(f'CAROUSEL_MATERIAL_{status} {target_path}')
    for kind, _, _, _, source_digest in records:
        source_file = ROOT / 'Content' / 'Motion' / f'M_Stage{kind}.uasset'
        if hashlib.sha256(source_file.read_bytes()).hexdigest() != source_digest:
            raise RuntimeError(f'只读源资产摘要变化：M_Stage{kind}；停止验收')
    u.log('CAROUSEL_BUILD_SUCCESS materials=5; SM5 runtime and fade/occlusion acceptance still required')


try:
    main()
except Exception:
    u.log_error('CAROUSEL_BUILD_FAILED\n' + traceback.format_exc())
    raise
