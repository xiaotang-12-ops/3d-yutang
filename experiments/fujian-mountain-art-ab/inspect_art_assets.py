"""核对成熟材质参数、截图API以及导入选项。-codex，2026-10-23"""
import unreal,json,pathlib
info={}
lib=unreal.MaterialEditingLibrary
for p in ['/Bridge/MSPresets/M_MS_Surface_Material/M_MS_Surface_Material','/Bridge/MSPresets/M_MS_SurfaceBlend_Material/M_MS_SurfaceBlend_Material']:
    m=unreal.load_asset(p)
    if m:
        info[p]={'texture':[str(x) for x in lib.get_texture_parameter_names(m)],'scalar':[str(x) for x in lib.get_scalar_parameter_names(m)],'vector':[str(x) for x in lib.get_vector_parameter_names(m)]}
    else:info[p]='not available'
info['screenshot']=str(unreal.AutomationLibrary.take_high_res_screenshot.__doc__)
info['subsystem']={n:str(getattr(unreal.LevelEditorSubsystem,n).__doc__) for n in dir(unreal.LevelEditorSubsystem) if any(x in n for x in ['pilot','save','new_level'])}
pathlib.Path('D:/3d-research/artifacts/fujian-mountain-art-ab/art-api.json').write_text(json.dumps(info,indent=2),encoding='utf-8')
