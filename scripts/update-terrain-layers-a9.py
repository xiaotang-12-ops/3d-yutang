"""A9受限地表色调更新；主控串行在UE5.8.4中执行，仅保存M_Terrain。

默认只读预检并输出preflight.json。主控复核实际图后设置POND_A9_APPLY=1才应用。
POND_A9_LAYER_MIX仅允许0或0.5，默认0。首次插入，后续按回执严格匹配后切换。
颜色仅为展示分层，不是植被/岩石/水体分类。-codex，2026-10-23
"""
import hashlib
import json
import math
import os
from pathlib import Path

import unreal

ROOT = Path(__file__).resolve().parents[1]
PROJECT = ROOT / 'unreal/FujianSandbox58'
OUT = ROOT / 'artifacts/pond-v7-a-proof/terrain-layers-a9'
ASSET = '/Game/Terrain/M_Terrain'
TARGET = PROJECT / 'Content/Terrain/M_Terrain.uasset'
PREFIX = 'A9/'
LIB = unreal.MaterialEditingLibrary
PROPS = ('MP_BASE_COLOR', 'MP_EMISSIVE_COLOR', 'MP_ROUGHNESS',
         'MP_SPECULAR', 'MP_NORMAL', 'MP_WORLD_POSITION_OFFSET')
# 本机5.8.4 GetExpressionInputDescription实际支持的30个值；其他值的查询会空指针解引用。
# 不能用try/except试探原生崩溃。别名/废弃/哨兵仅报告，不查询。-codex，2026-10-23
QUERYABLE_PROPERTY_VALUES = {0, 1, 2, *range(5, 13), *range(15, 34)}
BASELINE_GRAPH_SHA = '2f7cb6b6ba111d00d981d62e5b1e043356f3a9d26cd5b6ccee475e3dae04a1e4'
NODE_PATH = ASSET + '.M_Terrain:'
FIELDS = {
    'Constant': ('r',), 'Constant3Vector': ('constant',),
    'ScalarParameter': ('parameter_name', 'default_value'),
    'TextureSample': ('texture', 'sampler_type'),
    'Multiply': ('const_a', 'const_b'), 'Add': ('const_a', 'const_b'),
    'Subtract': ('const_a', 'const_b'), 'Divide': ('const_a', 'const_b'),
    'Max': ('const_a', 'const_b'),
    'LinearInterpolate': ('const_a', 'const_b', 'const_alpha'),
    'SmoothStep': ('const_min', 'const_max', 'const_value'),
    'ComponentMask': ('r', 'g', 'b', 'a'),
    'WorldPosition': ('world_position_shader_offset',),
    'Desaturation': ('luminance_factors',),
}


def require(ok, message):
    if not ok:
        raise RuntimeError('A9拒绝：' + message)


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def ident(node):
    return node.get_path_name() if node else None


def kind(node):
    return node.get_class().get_name().removeprefix('MaterialExpression') if node else 'None'


def value(v):
    if isinstance(v, (bool, int, float, str)) or v is None:
        return v
    if hasattr(v, 'get_path_name'):
        return v.get_path_name()
    if all(hasattr(v, c) for c in ('r', 'g', 'b', 'a')):
        return [float(getattr(v, c)) for c in ('r', 'g', 'b', 'a')]
    return str(v)


def snapshot(mat):
    nodes = {}
    for n in LIB.get_material_expressions(mat):
        nodes[ident(n)] = {
            'class': kind(n), 'desc': str(n.get_editor_property('desc')),
            'inputs': [ident(x) for x in LIB.get_inputs_for_material_expression(mat, n)],
            'fields': {p: value(n.get_editor_property(p)) for p in FIELDS.get(kind(n), ())},
        }
    roots, skipped, errors, queried_values, exposed = {}, {}, {}, set(), {}
    for p in sorted(x for x in dir(unreal.MaterialProperty) if x.startswith('MP_')):
        prop = getattr(unreal.MaterialProperty, p)
        try:
            number = int(prop.value)
        except (AttributeError, TypeError, ValueError) as exc:
            errors[p] = '无法读取枚举值：' + str(exc)
            continue
        exposed[p] = number
        if number not in QUERYABLE_PROPERTY_VALUES or p.startswith(('MP_LAST_', 'MP_NUM_')):
            skipped[p] = '本机C++不支持的属性值或枚举别名，不调用原生查询'
            continue
        try:
            roots[p] = [ident(LIB.get_material_property_input_node(mat, prop)),
                        str(LIB.get_material_property_input_node_output_name(mat, prop))]
            queried_values.add(number)
        except Exception as exc:
            errors[p] = str(exc)
    return {
        'nodes': nodes,
        'roots': roots,
        'root_query': {
            'exposed_enum_values': exposed, 'skipped': skipped, 'errors': errors,
            'queried_supported_values': sorted(queried_values),
            'unexposed_supported_values': sorted(QUERYABLE_PROPERTY_VALUES - {
                v for p, v in exposed.items() if not p.startswith(('MP_LAST_', 'MP_NUM_'))}),
            'coverage': 'Only Python-exposed supported properties queried; unexposed values NOT checked.'},
        'settings': {p: value(mat.get_editor_property(p))
                     for p in ('two_sided', 'blend_mode', 'shading_model')},
    }


def pins(mat, node):
    return list(LIB.get_inputs_for_material_expression(mat, node))


def validate_original_graph(mat, installed):
    """固定已核37节点图；安装后仅把批准的两条A输入还原来核验指纹，不修改真实图。"""
    graph = snapshot(mat)
    old_nodes = {k: dict(n, inputs=list(n['inputs'])) for k, n in graph['nodes'].items()
                 if not n['desc'].startswith(PREFIX)}
    require(len(old_nodes) == 37, '精确旧图节点数不符')
    if installed:
        outputs = [k for k, n in graph['nodes'].items() if n['desc'] == PREFIX + 'surface-color']
        require(len(outputs) == 1, 'A9地表输出描述不唯一')
        output = outputs[0]
        require(graph['nodes'][output]['class'] == 'LinearInterpolate' and
                graph['nodes'][output]['inputs'][0] == NODE_PATH + 'MaterialExpressionMultiply_8',
                'A9基线输入不符')
        for suffix in ('MaterialExpressionMultiply_10', 'MaterialExpressionMultiply_12'):
            require(old_nodes[NODE_PATH + suffix]['inputs'][0] == output, '批准的A9插入连接不符')
            old_nodes[NODE_PATH + suffix]['inputs'][0] = NODE_PATH + 'MaterialExpressionMultiply_8'
    require(all(p in graph['roots'] for p in PROPS), '原六种输出查询不全')
    original = {'nodes': old_nodes, 'roots': {p: graph['roots'][p] for p in PROPS},
                'settings': graph['settings']}
    digest = hashlib.sha256(json.dumps(original, sort_keys=True, separators=(',', ':')).encode()).hexdigest()
    require(digest == BASELINE_GRAPH_SHA, '完整旧图连接/字段/默认值指纹不符')


def parameter_nodes(mat, installed=False):
    validate_original_graph(mat, installed)
    groups = {}
    for n in LIB.get_material_expressions(mat):
        if kind(n) == 'ScalarParameter':
            name = str(n.get_editor_property('parameter_name'))
            groups.setdefault(name, []).append(n)
    result = {}
    for name, entries in groups.items():
        if name == 'SurfaceGain':
            by_path = {ident(n): n for n in entries}
            expected = {NODE_PATH + 'MaterialExpressionScalarParameter_' + i for i in ('0', '3')}
            require(len(entries) == 2 and set(by_path) == expected, 'SurfaceGain不是已批准的精确双节点')
            require(all(float(n.get_editor_property('default_value')) == 1. for n in entries),
                    '两个SurfaceGain默认值必须都为1')
            result[name] = by_path[NODE_PATH + 'MaterialExpressionScalarParameter_3']
        else:
            require(len(entries) == 1, '参数重名：' + name)
            result[name] = entries[0]
    return result


def defaults(mat, installed):
    params = parameter_nodes(mat, installed)
    expected = {'SurfaceGain', 'SurfaceLightingMix', 'BoundaryGain'}
    require(set(params) == expected | ({'LayerMix'} if installed else set()), '参数集合不符')
    for name in expected:
        require(float(params[name].get_editor_property('default_value')) == 1., name + '默认值必须为1')
        require(abs(LIB.get_material_default_scalar_parameter_value(mat, name) - 1.) < 1e-6,
                name + '默认值缓存不符')
    return params


def connect(src, dst, pin):
    available = [str(x) for x in LIB.get_material_expression_input_names(dst)]
    mat = unreal.load_asset(ASSET)
    before = pins(mat, dst)
    detail = kind(dst) + '/' + repr(pin) + '; reflected=' + repr(available)
    if pin == '':
        # C++ GetExpressionInputByName对NAME_None直接返回GetInput(0)，不搜索名称列表。
        # Input短名为NAME_None，ToString表示不保证是空串；只对已核单输入类型采用第0槽。
        # 不尝试猜名字，结构或连接读回不符就中止。-codex，2026-10-23
        require(kind(dst) in {'ComponentMask', 'Saturate', 'SquareRoot', 'Abs'} and
                len(available) == len(before) == 1, '首输入结构与本机源码不符：' + detail)
        index = 0
    else:
        require(available.count(pin) == 1 and len(available) == len(before),
                '输入短名不存在或不唯一：' + detail)
        index = available.index(pin)
    require(LIB.connect_material_expressions(src, '', dst, pin), '连接失败：' + detail)
    after = pins(mat, dst)
    require(len(after) == len(before) and after[index] == src and
            all(after[i] == before[i] for i in range(len(before)) if i != index),
            '连接读回或其他输入保持失败：' + detail)


def preflight():
    require(Path(unreal.Paths.project_dir()).resolve() == PROJECT.resolve(), '不是58目标工程')
    require(str(unreal.SystemLibrary.get_engine_version()).startswith('5.8.4-'), '不是UE5.8.4')
    version = json.loads((Path(unreal.Paths.engine_dir()) / 'Build/Build.version').read_text('utf-8-sig'))
    require([version[k] for k in ('MajorVersion', 'MinorVersion', 'PatchVersion')] == [5, 8, 4], '引擎版本文件不符')
    mix = float(os.environ.get('POND_A9_LAYER_MIX', '0'))
    require(mix in (0., .5), 'LayerMix只允许0或0.5')
    manifest = json.loads((OUT / 'baseline.json').read_text('utf-8'))
    require(manifest['baseline_sha256'] == '31fa4a4763d7e9a7715a6ae9ade486c39a387c61e06071cec88dbac9c954914b', '基线清单不符')
    require(sha(OUT / 'M_Terrain.baseline.uasset') == manifest['baseline_sha256'], '备份损坏/缺失')
    for path, digest in manifest['locked'].items():
        require(sha(ROOT / path) == digest, '锁定文件已变化：' + path)
    require(ASSET not in [p.get_path_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()],
            'M_Terrain已有未保存改动')
    receipt = OUT / 'applied.json'
    previous = json.loads(receipt.read_text('utf-8')) if receipt.exists() else None
    require(sha(TARGET) == (previous['saved_sha256'] if previous else manifest['baseline_sha256']),
            '实际材质不是基线或上次受限保存版本')
    if previous:
        require(previous['script_sha256'] == sha(Path(__file__)), '脚本与应用回执版本不符')
    mat = unreal.load_asset(ASSET)
    require(mat is not None, 'M_Terrain不存在')
    before = snapshot(mat)
    # 先保留真实图诊断，再做参数/拓扑门禁；重名拒绝时仍可分析，绝不据此放行。
    # checks_complete=False明确表示只完成文件门禁，不能作为应用通过回执。-codex，2026-10-23
    (OUT / 'preflight.json').write_text(json.dumps({
        'graph': before, 'asset_sha256': sha(TARGET), 'installed': previous is not None,
        'layer_mix_requested': mix, 'saved_assets': [], 'checks_complete': False,
        'note': 'Read-only raw snapshot before parameter/topology checks; subsequent rejection is still fatal.'
    }, ensure_ascii=False, indent=2), encoding='utf-8')
    # C++支持不表示Python反射会暴露；其余未暴露项只报告未检查，不伪造枚举。
    # 原六项必须可查，实际暴露项的任何查询错误仍中止。-codex，2026-10-23
    require(not before['root_query']['errors'] and all(p in before['roots'] for p in PROPS),
            '材质输出查询不全：' + str(before['root_query']))
    params = defaults(mat, previous is not None)
    if previous:
        require(before == previous['graph'], '已安装图与上次回执不符')
    else:
        require(len(before['nodes']) == 37, '原图不是37节点')
        require(not any(n['desc'].startswith(PREFIX) for n in before['nodes'].values()), '已有A9节点但缺回执')
    return mat, before, params, mix, previous


def check_locked():
    manifest = json.loads((OUT / 'baseline.json').read_text('utf-8'))
    for path, digest in manifest['locked'].items():
        require(sha(ROOT / path) == digest, '锁定文件已变化：' + path)


def insertion_points(mat):
    albedo = LIB.get_material_property_input_node(mat, unreal.MaterialProperty.MP_BASE_COLOR)
    surface = LIB.get_material_property_input_node(mat, unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    require(kind(albedo) == 'Multiply' and kind(surface) == 'Add', '旧输出根类型不符')
    ambient, edge = pins(mat, surface)
    require(kind(ambient) == 'Multiply' and kind(edge) == 'Multiply', '旧底光/边界类型不符')
    old, _ = pins(mat, albedo)
    require(pins(mat, ambient)[0] == old and kind(old) == 'Multiply', '地表共享输入不符')
    desat, gain = pins(mat, old)
    require(kind(desat) == 'Desaturation' and str(gain.get_editor_property('parameter_name')) == 'SurfaceGain',
            '旧地表色链不符')
    require(LIB.get_material_property_input_node(mat, unreal.MaterialProperty.MP_NORMAL) is None,
            'Normal已有连接，不能保证坡度来源')
    require(LIB.get_material_property_input_node(mat, unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET) is None,
            'WPO已有连接，世界高程不再匹配')
    return old, albedo, ambient


def install(mat, old, created):
    # 只创建A9节点，不重命名/重建旧图；所有输出为空名，使用第0输出。-codex，2026-10-23
    def node(role, cls, **props):
        n = LIB.create_material_expression(mat, getattr(unreal, 'MaterialExpression' + cls), -900, 300)
        require(n is not None, '创建节点失败：' + role)
        created.append(n)
        n.set_editor_property('desc', PREFIX + role)
        for p, v in props.items():
            n.set_editor_property(p, v)
        return n

    def scalar(role, x):
        return node(role, 'Constant', r=x)

    def color(role, rgb):
        return node(role, 'Constant3Vector', constant=unreal.LinearColor(*rgb, 1.))

    def binary(role, cls, a, b):
        n = node(role, cls)
        connect(a, n, 'A')
        connect(b, n, 'B')
        return n

    def lerp(role, a, b, alpha):
        n = binary(role, 'LinearInterpolate', a, b)
        connect(alpha, n, 'Alpha')
        return n

    def smooth(role, x, low, high):
        n = node(role, 'SmoothStep', const_min=low, const_max=high)
        connect(x, n, 'Value')
        return n

    def mask(role, src, **channels):
        n = node(role, 'ComponentMask', r=False, g=False, b=False, a=False)
        for ch, enabled in channels.items():
            n.set_editor_property(ch, enabled)
        connect(src, n, '')
        return n

    normal = node('world-normal', 'PixelNormalWS')
    xy = mask('normal-xy', normal, r=True, g=True)
    xy2 = binary('normal-xy-square', 'DotProduct', xy, xy)
    xylen = node('normal-xy-length', 'SquareRoot')
    connect(xy2, xylen, '')
    z = mask('normal-z', normal, b=True)
    absz = node('normal-z-abs', 'Abs')
    connect(z, absz, '')
    safez = binary('normal-z-safe', 'Max', absz, scalar('normal-epsilon', .0001))
    denom = binary('undo-sixfold', 'Multiply', safez, scalar('exaggeration', 6.))
    tan = binary('true-slope-tan', 'Divide', xylen, denom)
    rock = smooth('rock-weight', tan, math.tan(math.radians(12)), math.tan(math.radians(22)))
    world = node('world-position', 'WorldPosition')
    worldz = mask('world-z', world, b=True)
    relative = binary('height-from-base', 'Subtract', worldz, scalar('base', 140.))
    height = binary('height-metres', 'Divide', relative, scalar('height-scale', .05389867049946101))
    high = smooth('height-weight', height, 150., 1000.)
    forest = lerp('forest-height', color('forest-low', (.035, .13, .045)),
                  color('forest-high', (.12, .19, .06)), high)
    palette = lerp('palette', forest, color('warm-stone', (.27, .25, .20)), rock)
    weights = color('luminance-weights', (.2126, .7152, .0722))
    oldy = binary('old-luminance', 'DotProduct', old, weights)
    newy = binary('palette-luminance', 'DotProduct', palette, weights)
    safey = binary('palette-luminance-safe', 'Max', newy, scalar('luminance-epsilon', .00001))
    ratio = binary('luminance-ratio', 'Divide', oldy, safey)
    normalized = binary('same-luminance-color', 'Multiply', palette, ratio)
    mix = node('layer-mix', 'ScalarParameter', parameter_name='LayerMix', default_value=0.)
    alpha = node('layer-mix-clamp', 'Saturate')
    connect(mix, alpha, '')
    return lerp('surface-color', old, normalized, alpha), mix


def main():
    mat, before, params, mix, previous = preflight()
    if not previous:
        insertion_points(mat)
    if os.environ.get('POND_A9_APPLY', '0') != '1':
        (OUT / 'preflight.json').write_text(json.dumps({
            'graph': before, 'asset_sha256': sha(TARGET), 'installed': previous is not None,
            'layer_mix_requested': mix, 'saved_assets': [], 'checks_complete': True,
            'note': 'Read-only material inspection; does not prove compilation or rendering.'
        }, ensure_ascii=False, indent=2), encoding='utf-8')
        unreal.log('A9_PREFLIGHT_ONLY_OK: no material mutation or asset save')
        return
    created = []
    endpoints = None
    mix_node = params.get('LayerMix')
    old_mix = float(mix_node.get_editor_property('default_value')) if mix_node else None
    saved = False
    try:
        if not previous:
            old, albedo, ambient = insertion_points(mat)
            endpoints = old, albedo, ambient
            output, mix_node = install(mat, old, created)
            connect(output, albedo, 'A')
            connect(output, ambient, 'A')
            after = snapshot(mat)
            require(after['roots'] == before['roots'] and after['settings'] == before['settings'], '原材质输出/设置变化')
            for key, original in before['nodes'].items():
                expected = dict(original, inputs=list(original['inputs']))
                if key in (ident(albedo), ident(ambient)):
                    expected['inputs'][0] = ident(output)
                require(after['nodes'][key] == expected, '旧节点发生非授权改变：' + key)
            tags = [str(n.get_editor_property('desc')) for n in created]
            require(len(tags) == len(set(tags)), 'A9节点描述重名')
        mix_node.set_editor_property('default_value', mix)
        defaults(mat, True)
        errors = LIB.recompile_material(mat)
        require(not errors, '材质编译失败：' + str(errors))
        validate_original_graph(mat, True)
        require(snapshot(mat)['roots'] == before['roots'], '编译后材质输出连接变化')
        check_locked()
        require(sha(TARGET) == (previous['saved_sha256'] if previous else
                              '31fa4a4763d7e9a7715a6ae9ade486c39a387c61e06071cec88dbac9c954914b'), '保存前磁盘材质已变化')
        require(unreal.EditorAssetLibrary.save_loaded_asset(mat, only_if_is_dirty=False), '保存M_Terrain失败')
        saved = True
        check_locked()
        receipt = {'saved_sha256': sha(TARGET), 'script_sha256': sha(Path(__file__)),
                   'layer_mix': mix, 'graph': snapshot(mat), 'only_saved_asset': ASSET,
                   'baseline_sha256': sha(OUT / 'M_Terrain.baseline.uasset')}
        (OUT / 'applied.json').write_text(json.dumps(receipt, ensure_ascii=False, indent=2), encoding='utf-8')
        unreal.log('A9_TERRAIN_LAYERS_OK ' + json.dumps({'layer_mix': mix, 'nodes_added': len(created),
                   'saved_sha256': receipt['saved_sha256'], 'only_saved_asset': ASSET}))
    except Exception:
        # 保存前失败恢复两条旧输入并删本轮新节点；不保存，也不覆盖磁盘备份。-codex，2026-10-23
        if not saved:
            if endpoints:
                old, albedo, ambient = endpoints
                connect(old, albedo, 'A')
                connect(old, ambient, 'A')
            for n in reversed(created):
                LIB.delete_material_expression(mat, n)
            if previous and mix_node:
                mix_node.set_editor_property('default_value', old_mix)
            require(snapshot(mat) == before, '失败后内存恢复核验不符，停止并重载未保存资产')
            LIB.recompile_material(mat)
        else:
            unreal.log_error('A9材质已保存但回执未完成；禁止重跑，先检查M_Terrain磁盘哈希和applied.json。')
        raise


if __name__ == '__main__':
    main()
