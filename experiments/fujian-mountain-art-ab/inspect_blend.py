"""只读检查成熟混合材质连接，决定复用所需的地理遮罩桥接。-codex，2026-10-23"""
import json,pathlib,unreal
lib=unreal.MaterialEditingLibrary
m=unreal.load_asset('/Bridge/MSPresets/M_MS_SurfaceBlend_Material/M_MS_SurfaceBlend_Material')
info=[]
for n in lib.get_material_expressions(m):
    row={'name':n.get_name(),'class':n.get_class().get_name()}
    if any(x in row['class'] for x in ['VertexColor','TextureCoordinate','MaterialFunctionCall']):
        row['inputs']=list(lib.get_material_expression_input_names(n))
        if 'MaterialFunctionCall' in row['class']:row['function']=str(n.get_editor_property('material_function'))
    info.append(row)
pathlib.Path('D:/3d-research/artifacts/fujian-mountain-art-ab/blend-expressions.json').write_text(json.dumps(info,indent=2),encoding='utf-8')
print('SCREENSHOT_TASK',AB_SCREEN_TASK.is_task_done() if hasattr(AB_SCREEN_TASK,'is_task_done') else str(AB_SCREEN_TASK))
