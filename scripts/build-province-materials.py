"""UE5.8省览资产构建；ProvinceContextOnly仅写背景；ProvinceDetailOnly仅写近景纹理及省内材质。
ProvinceSelectionOnly仅重编译省内材质；ProvinceLongyanOnly仅更新同名龙岩纹理。
原M_Terrain/A9、边界遮罩和地形数据只读。-codex，2026-10-30
"""
from pathlib import Path
import hashlib
import json
import unreal

ROOT=Path(__file__).resolve().parents[1]
PROJECT=ROOT/'unreal/FujianSandbox58'
if Path(unreal.Paths.project_dir()).resolve()!=PROJECT.resolve():
    raise RuntimeError('Province assets must only be built in FujianSandbox58')
DATA=PROJECT/'Content/Province'
# 仅背景模式不导入、不重编译省内资产，保护用户认可的R7外观。-codex，2026-10-27
selection_only='-provinceselectiononly' in unreal.SystemLibrary.get_command_line().lower().split()
longyan_only='-provincelongyanonly' in unreal.SystemLibrary.get_command_line().lower().split()
context_only='-provincecontextonly' in unreal.SystemLibrary.get_command_line().lower().split()
detail_only='-provincedetailonly' in unreal.SystemLibrary.get_command_line().lower().split()
if sum((context_only,detail_only,selection_only,longyan_only))>1:raise RuntimeError('Choose only one province asset build mode')
if longyan_only and not (DATA/'longyan-detail-source.json').exists():raise RuntimeError('Longyan source metadata required')
detail_source=DATA/'province-detail-source.json'
longyan_source=DATA/'longyan-detail-source.json'
if detail_only and not detail_source.exists():raise RuntimeError('Build near-view source first')
manifest=json.loads((DATA/'province-source.json').read_text('utf-8'))
for filename,expected in manifest['outputs'].items():
    if hashlib.sha256((DATA/filename).read_bytes()).hexdigest()!=expected:
        raise RuntimeError(f'Derived texture changed: {filename}')
longyan_meta=None
if longyan_source.exists() and not context_only:
    # 在任何资产保存前校验新素材，避免错误或半写入清单导致局部构建。-codex，2026-10-28
    longyan_meta=json.loads(longyan_source.read_text('utf-8'))
    if longyan_meta['baseSha256']!=manifest['outputs']['province-surface.png'] or longyan_meta['outputSha256']!=hashlib.sha256((DATA/'longyan-detail.png').read_bytes()).hexdigest():
        raise RuntimeError('Longyan imagery is stale or modified')
    pw,ps,pe,pn=longyan_meta['boundsShared']
    if not (pw<pe and ps<pn):raise RuntimeError('Invalid Longyan geographic bounds')
lib=unreal.MaterialEditingLibrary
assets=unreal.AssetToolsHelpers.get_asset_tools()
folder='/Game/Terrain'
owned=['T_ProvinceSurface','T_ProvinceContext','T_ProvinceBorders','T_ProvinceAtmosphere','M_ProvinceTerrain','M_ProvinceContext']
if detail_source.exists() and (not context_only or (PROJECT/'Content/Terrain/T_ProvinceDetail.uasset').exists()):owned+=['T_ProvinceDetail']
if longyan_source.exists() and (not context_only or (PROJECT/'Content/Terrain/T_LongyanDetail.uasset').exists()):owned+=['T_LongyanDetail']
receipt_path=DATA/'province-assets.json'
receipt_bytes=receipt_path.read_bytes() if receipt_path.exists() else b''
receipt=json.loads(receipt_bytes.decode('utf-8')) if receipt_bytes else {}
for name in owned:
    path=PROJECT/'Content/Terrain'/f'{name}.uasset'
    if path.exists() and hashlib.sha256(path.read_bytes()).hexdigest()!=receipt.get('assets',{}).get(name):
        raise RuntimeError(f'Unowned or externally modified asset: {name}')
protected_names=['M_Terrain','M_Glow','M_Stage','T_TerrainColor','T_TerrainBorders']
if detail_only:protected_names+=['T_ProvinceSurface','T_ProvinceBorders','M_ProvinceContext','T_ProvinceContext','T_ProvinceAtmosphere']
if context_only:protected_names+=['T_ProvinceSurface','T_ProvinceBorders','M_ProvinceTerrain']
if selection_only:
    protected_names=[p.stem for p in (PROJECT/'Content/Terrain').glob('*.uasset') if p.stem!='M_ProvinceTerrain']
if longyan_only:
    protected_names=[p.stem for p in (PROJECT/'Content/Terrain').glob('*.uasset') if p.stem!='T_LongyanDetail']
protected={n:hashlib.sha256((PROJECT/'Content/Terrain'/f'{n}.uasset').read_bytes()).hexdigest() for n in protected_names}


def texture(filename,name,mask=False):
    if longyan_only and name!='T_LongyanDetail':raise RuntimeError('Longyan-only build cannot import other textures')
    # 选城专用构建只读已有纹理，禁止重新导入或保存任何纹理。-codex，2026-10-30
    if selection_only:
        tex=unreal.load_asset(folder+'/'+name)
        if not tex:raise RuntimeError(f'Missing protected texture: {name}')
        return tex
    task=unreal.AssetImportTask()
    task.filename=str(DATA/filename);task.destination_path=folder;task.destination_name=name
    task.automated=True;task.replace_existing=True;task.save=False
    assets.import_asset_tasks([task])
    tex=unreal.load_asset(folder+'/'+name)
    if not tex:
        raise RuntimeError(f'Import failed: {name}')
    tex.set_editor_property('never_stream',True)
    tex.set_editor_property('srgb',not mask)
    if name in ('T_ProvinceDetail','T_LongyanDetail'):tex.set_editor_property('compression_settings',unreal.TextureCompressionSettings.TC_BC7)
    if name=='T_LongyanDetail':
        # 明确保留真实源最高级；不修改全项目纹理组或全局LOD策略。-codex，2026-10-30
        expected_size=int(longyan_meta['textureSize'])
        if expected_size not in (8192,16384):raise RuntimeError('Unsupported Longyan texture dimensions')
        tex.set_editor_property('max_texture_size',expected_size)
        tex.set_editor_property('lod_bias',0)
    if mask:tex.set_editor_property('compression_settings',unreal.TextureCompressionSettings.TC_MASKS)
    tex.set_editor_property('address_x',unreal.TextureAddress.TA_CLAMP)
    tex.set_editor_property('address_y',unreal.TextureAddress.TA_CLAMP)
    if not unreal.EditorAssetLibrary.save_loaded_asset(tex):
        raise RuntimeError(f'Save failed: {name}')
    if name=='T_LongyanDetail':
        actual=(int(tex.blueprint_get_size_x()),int(tex.blueprint_get_size_y()))
        if actual!=(expected_size,expected_size):raise RuntimeError(f'Longyan platform texture truncated: {actual}, expected {expected_size}')
        unreal.log(f'LONGYAN_TEXTURE_DIMENSIONS source={expected_size} platform={actual} max={tex.get_editor_property("max_texture_size")}')
    return tex


def material(name,unlit=False):
    if selection_only and name!='M_ProvinceTerrain':raise RuntimeError('Selection build may only edit M_ProvinceTerrain')
    mat=unreal.load_asset(folder+'/'+name)
    if selection_only and not mat:raise RuntimeError('Selection build requires existing M_ProvinceTerrain')
    if not mat:mat=assets.create_asset(name,folder,unreal.Material,unreal.MaterialFactoryNew())
    if not mat:raise RuntimeError(f'Create failed: {name}')
    # 只能重建上方已验证归属的省览资产，绝不调用旧全量材质脚本。-codex，2026-10-27
    lib.delete_all_material_expressions(mat)
    mat.set_editor_property('two_sided',True)
    mat.set_editor_property('shading_model',unreal.MaterialShadingModel.MSM_UNLIT if unlit else unreal.MaterialShadingModel.MSM_DEFAULT_LIT)
    return mat


def node(mat,kind,**properties):
    n=lib.create_material_expression(mat,getattr(unreal,'MaterialExpression'+kind),0,0)
    for key,value in properties.items():n.set_editor_property(key,value)
    return n


def link(source,target,pin='',output=''):
    if not lib.connect_material_expressions(source,output,target,pin):
        raise RuntimeError(f'Connection failed: {target.get_class().get_name()}.{pin}')


def scalar(mat,value):return node(mat,'Constant',r=value)
def vector(mat,value):return node(mat,'Constant3Vector',constant=unreal.LinearColor(*value,1.))
def op(mat,kind,a,b):
    n=node(mat,kind);link(a,n,'A');link(b,n,'B');return n
def prop(source,name):
    if not lib.connect_material_property(source,'',getattr(unreal.MaterialProperty,'MP_'+name)):
        raise RuntimeError(f'Property connection failed: {name}')
def finish(mat):
    if selection_only and mat.get_name()!='M_ProvinceTerrain':raise RuntimeError('Protected material save rejected')
    errors=lib.recompile_material(mat)
    if errors:raise RuntimeError(f'Material compile failed: {mat.get_name()}: {list(errors)}')
    if not unreal.EditorAssetLibrary.save_loaded_asset(mat):raise RuntimeError(f'Save failed: {mat.get_name()}')


if longyan_only:
    texture('longyan-detail.png','T_LongyanDetail')

if not context_only and not longyan_only:
    surface_tex=unreal.load_asset(folder+'/T_ProvinceSurface') if detail_only else texture('province-surface.png','T_ProvinceSurface')
    borders=unreal.load_asset(folder+'/T_ProvinceBorders') if detail_only else texture('province-borders.png','T_ProvinceBorders',True)
    if not surface_tex or not borders:raise RuntimeError('Missing original province assets')
    mat=material('M_ProvinceTerrain',True)
    tex=node(mat,'TextureSample',texture=surface_tex)
    mask=node(mat,'TextureSample',texture=borders,sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_MASKS)
    city=node(mat,'ComponentMask',r=True,g=False,b=False,a=False);link(mask,city)
    province=node(mat,'ComponentMask',r=False,g=True,b=False,a=False);link(mask,province)
    inverse=node(mat,'OneMinus');link(province,inverse)
    city_mask=city
    province_mask=province
    city=op(mat,'Multiply',city,inverse)
    city=op(mat,'Multiply',city,vector(mat,(.13,.23,.26)))
    province=op(mat,'Multiply',province,vector(mat,(.025,1.5,2.0)))
    # 地貌受光已按真实DEM烘入，固定制图光向避免旧场景填充光把暗谷再洗灰。
    # 旋转仍使用原三维网格；不将图作为替代地形的屏幕背景。-codex，2026-10-27
    # 全省保持原采样；只在入城距离平滑加入细节，原图及几何不改。-codex，2026-10-28
    if detail_source.exists():
        detail_meta=json.loads(detail_source.read_text('utf-8'))
        if detail_meta['baseSha256']!=manifest['outputs']['province-surface.png'] or detail_meta['outputSha256']!=hashlib.sha256((DATA/'province-detail.png').read_bytes()).hexdigest():
            raise RuntimeError('Near-view texture source is stale or modified')
        detail_tex=texture('province-detail.png','T_ProvinceDetail')
        detail=node(mat,'TextureSample',texture=detail_tex)
        distance=node(mat,'Distance');link(node(mat,'CameraPositionWS'),distance,'A');link(node(mat,'WorldPosition'),distance,'B')
        far=node(mat,'SmoothStep',const_min=1800.,const_max=6500.);link(distance,far,'Value')
        near=node(mat,'OneMinus');link(far,near)
        blend=node(mat,'LinearInterpolate');link(tex,blend,'A');link(detail,blend,'B');link(near,blend,'Alpha')
        tex=blend
    if longyan_meta is not None:
        # 龙岩真实遥感覆盖整市及邻区缓冲，不按行政边界截断；省外背景不接入。-codex，2026-10-28
        pw,ps,pe,pn=longyan_meta['boundsShared'];w,s,e,n=manifest['boundsShared']
        global_uv=node(mat,'TextureCoordinate')
        scale=node(mat,'Constant2Vector',r=(e-w)/(pe-pw),g=(n-s)/(pn-ps))
        offset=node(mat,'Constant2Vector',r=(w-pw)/(pe-pw),g=(pn-n)/(pn-ps))
        uv=op(mat,'Add',op(mat,'Multiply',global_uv,scale),offset)
        satellite=node(mat,'TextureSample',texture=texture('longyan-detail.png','T_LongyanDetail'));link(uv,satellite,'UVs')
        delta=op(mat,'Subtract',uv,scalar(mat,.5))
        absolute=node(mat,'Abs');link(delta,absolute)
        normalized=op(mat,'Multiply',absolute,scalar(mat,2.))
        ux=node(mat,'ComponentMask',r=True,g=False,b=False,a=False);link(normalized,ux)
        vy=node(mat,'ComponentMask',r=False,g=True,b=False,a=False);link(normalized,vy)
        edge=op(mat,'Max',ux,vy)
        fade=node(mat,'SmoothStep',const_min=.90,const_max=1.);link(edge,fade,'Value')
        inside=node(mat,'OneMinus');link(fade,inside)
        distance=node(mat,'Distance');link(node(mat,'CameraPositionWS'),distance,'A');link(node(mat,'WorldPosition'),distance,'B')
        far=node(mat,'SmoothStep',const_min=1800.,const_max=6500.);link(distance,far,'Value')
        near=node(mat,'OneMinus');link(far,near)
        # 原始缺测保持透明，不插值伪造地物；有效区继续按距离与范围淡入。-codex，2026-10-28
        coverage=node(mat,'Multiply');link(op(mat,'Multiply',inside,near),coverage,'A');link(satellite,coverage,'B',output='A')
        blend=node(mat,'LinearInterpolate');link(tex,blend,'A');link(satellite,blend,'B');link(coverage,blend,'Alpha')
        tex=blend
    ambient=op(mat,'Multiply',tex,scalar(mat,.9))
    # 默认选择量为0时完全保留旧表达；选中只在真实地形表面变暖，不新增覆盖几何。
    # 原mask用于细轮廓，省市共边以Max合并，不叠加漂白。-codex，2026-10-30
    selected=node(mat,'ScalarParameter',parameter_name='CitySelection',default_value=0.)
    amount=node(mat,'Saturate');link(selected,amount)
    warmth=node(mat,'VectorParameter',parameter_name='CityWarmth',default_value=unreal.LinearColor(1.12,1.055,.96,1.))
    warmed=op(mat,'Multiply',ambient,warmth)
    surface=node(mat,'LinearInterpolate');link(ambient,surface,'A');link(warmed,surface,'B');link(amount,surface,'Alpha')
    # 世界坐标上的连续双层纹理乘光：7秒同向错相，软窄层+宽肩填补，不造离地几何。
    # 原材质为unlit emissive，此处是制图亮度调制，不称真实动态灯光。-codex，2026-10-30
    frame=node(mat,'VectorParameter',parameter_name='CityLightFrame',default_value=unreal.LinearColor(0.,0.,1.,0.))
    span=node(mat,'ComponentMask',r=False,g=False,b=True,a=False);link(frame,span)
    safe_span=op(mat,'Max',span,scalar(mat,1.))
    relative=op(mat,'Subtract',node(mat,'WorldPosition'),frame)
    axis=op(mat,'DotProduct',relative,vector(mat,(.65,.76,0.)))
    position=op(mat,'Multiply',op(mat,'Divide',axis,safe_span),scalar(mat,.5))
    phase=node(mat,'ScalarParameter',parameter_name='CityLightPhase',default_value=0.)
    moving=op(mat,'Subtract',position,phase)
    layers=[]
    for offset,power,strength in [(0.,4.,.65),(.35,2.,.18)]:
        wave=node(mat,'Sine',period=1.)
        # Sine 的单输入使用默认空引脚名；UE Python连接接口不接受属性名Input。-codex，2026-10-30
        link(op(mat,'Add',moving,scalar(mat,.25-offset)),wave)
        positive=op(mat,'Max',wave,scalar(mat,0.))
        profile=node(mat,'Power',const_exponent=power);link(positive,profile,'Base')
        layers.append(op(mat,'Multiply',profile,scalar(mat,strength)))
    light=op(mat,'Multiply',op(mat,'Add',layers[0],layers[1]),amount)
    # 首版白色增益在固定镜头下近似静态；只增强移动前缘青蓝对比，理论各通道峰值1.325/1.65/1.845。
    # 不覆盖原影像、不添加膜或几何，选择量为0时仍严格还原原表面。-codex，2026-10-30
    surface=op(mat,'Multiply',surface,op(mat,'Add',scalar(mat,1.),op(mat,'Multiply',light,vector(mat,(.5,1.,1.3)))))
    border_color=node(mat,'VectorParameter',parameter_name='CityBoundaryColor',default_value=unreal.LinearColor(.62,.43,.18,1.))
    border_gain=node(mat,'ScalarParameter',parameter_name='CityBoundaryGain',default_value=1.)
    selected_border=op(mat,'Multiply',op(mat,'Max',city_mask,province_mask),op(mat,'Multiply',border_color,border_gain))
    boundary=node(mat,'LinearInterpolate');link(op(mat,'Add',city,province),boundary,'A');link(selected_border,boundary,'B');link(amount,boundary,'Alpha')
    prop(op(mat,'Add',surface,boundary),'EMISSIVE_COLOR')
    finish(mat)

if not detail_only and not selection_only and not longyan_only:
    context_tex=texture('province-context.png','T_ProvinceContext')
    atmosphere_tex=texture('province-atmosphere.png','T_ProvinceAtmosphere')

    mat=material('M_ProvinceContext',True)
    world=node(mat,'WorldPosition')
    east=node(mat,'ComponentMask',r=False,g=True,b=False,a=False);link(world,east)
    north=node(mat,'ComponentMask',r=True,g=False,b=False,a=False);link(world,north)
    xy=op(mat,'AppendVector',east,north)
    west,south,east_bound,north_bound=manifest['contextBoundsShared']
    scale=node(mat,'Constant2Vector',r=1/((east_bound-west)*10),g=-1/((north_bound-south)*10))
    offset=node(mat,'Constant2Vector',r=-west/(east_bound-west),g=north_bound/(north_bound-south))
    uv=op(mat,'Add',op(mat,'Multiply',xy,scale),offset)
    tex=node(mat,'TextureSample',texture=context_tex);link(uv,tex,'UVs')
    # 有限缓存范围外渐入深蓝底色，避免Clamp拉伸成条带；不是添加虚构地貌。-codex，2026-10-27
    delta=op(mat,'Subtract',uv,scalar(mat,.5))
    absolute=node(mat,'Abs');link(delta,absolute)
    distance=op(mat,'Multiply',absolute,scalar(mat,2.))
    dx=node(mat,'ComponentMask',r=True,g=False,b=False,a=False);link(distance,dx)
    dy=node(mat,'ComponentMask',r=False,g=True,b=False,a=False);link(distance,dy)
    edge=op(mat,'Max',dx,dy)
    fade=node(mat,'SmoothStep',const_min=.90,const_max=1.);link(edge,fade,'Value')
    context=op(mat,'Multiply',tex,scalar(mat,.72))
    # 薄雾只混入省外地台材质，省内原网格覆盖在其上，因此不会改动福建本体。
    # 坐标使用现有世界轴；同一透明素材分散在外缘，低透明度避免整屏蒙灰。-codex，2026-10-27
    for center,span,strength in [((-455.,30.),(440.,340.),.18),((-390.,-230.),(-400.,250.),.20),((-80.,460.),(650.,280.),.12),((390.,-230.),(280.,420.),.12)]:
        fog_scale=node(mat,'Constant2Vector',r=1/(span[0]*10),g=-1/(span[1]*10))
        fog_offset=node(mat,'Constant2Vector',r=.5-center[0]/span[0],g=.5+center[1]/span[1])
        fog_uv=op(mat,'Add',op(mat,'Multiply',xy,fog_scale),fog_offset)
        fog=node(mat,'TextureSample',texture=atmosphere_tex);link(fog_uv,fog,'UVs')
        # 默认输出仅RGB，透明度须明确接TextureSample的A输出，不能从RGB做A掩码。
        density=node(mat,'Multiply');link(fog,density,'A',output='A');link(scalar(mat,strength),density,'B')
        veil=node(mat,'LinearInterpolate');link(context,veil,'A');link(vector(mat,(.11,.14,.18)),veil,'B');link(density,veil,'Alpha')
        context=veil
    mix=node(mat,'LinearInterpolate');link(context,mix,'A');link(vector(mat,(.003,.009,.016)),mix,'B');link(fade,mix,'Alpha')
    prop(mix,'EMISSIVE_COLOR');finish(mat)
for name,sha in protected.items():
    if hashlib.sha256((PROJECT/'Content/Terrain'/f'{name}.uasset').read_bytes()).hexdigest()!=sha:
        raise RuntimeError(f'Protected asset changed: {name}')
if selection_only or longyan_only:
    if receipt_path.read_bytes()!=receipt_bytes:raise RuntimeError('Asset receipt changed during build; refusing overwrite')
    changed_name='T_LongyanDetail' if longyan_only else 'M_ProvinceTerrain'
    receipt['assets'][changed_name]=hashlib.sha256((PROJECT/'Content/Terrain'/f'{changed_name}.uasset').read_bytes()).hexdigest()
else:
    receipt={'assets':{n:hashlib.sha256((PROJECT/'Content/Terrain'/f'{n}.uasset').read_bytes()).hexdigest() for n in owned},
    'protectedUnchanged':True,'textureSource':manifest['outputs'],'boundsShared':manifest['boundsShared'],
    'contextBoundsShared':manifest['contextBoundsShared']}
receipt_path.write_text(json.dumps(receipt,indent=2),encoding='utf-8')
unreal.log('PROVINCE_MATERIALS_OK '+json.dumps(receipt))
