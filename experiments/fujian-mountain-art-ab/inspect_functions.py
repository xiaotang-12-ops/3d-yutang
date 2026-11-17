"""读取Bridge混合链与UV函数的真实连接，不靠名称猜测。-codex，2026-10-23"""
import unreal,json,pathlib
lib=unreal.MaterialEditingLibrary
seen={}
def inspect(o,depth=0):
    if not o or o.get_path_name() in seen or depth>4:return
    path=o.get_path_name();rows=[];seen[path]=rows
    nodes=lib.get_material_expressions(o) if isinstance(o,unreal.Material) else lib.get_material_function_expressions(o)
    for n in nodes:
        r={'name':n.get_name(),'class':n.get_class().get_name(),'inputs':list(lib.get_material_expression_input_names(n))}
        if isinstance(n,unreal.MaterialExpressionMaterialFunctionCall):
            f=n.get_editor_property('material_function');r['function']=f.get_path_name() if f else None;inspect(f,depth+1)
        for prop in ['parameter_name','default_value','constant','const_a','const_b','mask_r','mask_g','mask_b','mask_a']:
            try:r[prop]=str(n.get_editor_property(prop))
            except:pass
        rows.append(r)
inspect(unreal.load_asset('/Bridge/MSPresets/M_MS_SurfaceBlend_Material/M_MS_SurfaceBlend_Material'))
pathlib.Path('D:/3d-research/artifacts/fujian-mountain-art-ab/bridge-function-tree.json').write_text(json.dumps(seen,indent=2),encoding='utf-8')
unreal.EditorLevelLibrary.editor_invalidate_viewports()
