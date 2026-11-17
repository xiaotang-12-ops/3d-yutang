import unreal,json,pathlib
info={}
for name in ['ShowFlag.Materials','ShowFlag.Lighting','ShowFlag.Landscape','r.MaterialQualityLevel','r.ForceDebugViewModes']:
 info[name]=unreal.SystemLibrary.get_console_variable_int_value(name)
info['editor_methods']=[x for x in dir(unreal.LevelEditorSubsystem) if any(k in x for k in ['viewport','view_mode'])]
info['screenshot_doc']=unreal.AutomationLibrary.take_high_res_screenshot.__doc__
pathlib.Path('D:/3d-research/artifacts/fujian-mountain-art-ab/view-flags.json').write_text(json.dumps(info,indent=2),encoding='utf-8')
