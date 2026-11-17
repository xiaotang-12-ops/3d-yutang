import unreal,json,pathlib
w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
h=unreal.SystemLibrary.line_trace_single(w,unreal.Vector(296853,-457194,250000),unreal.Vector(296853,-457194,-10000),unreal.TraceTypeQuery.TRACE_TYPE_QUERY1,True,[],unreal.DrawDebugTrace.NONE)
r={'hit':str(h),'hitdir':dir(h),'breakdoc':unreal.GameplayStatics.break_hit_result.__doc__,'visdoc':unreal.AutomationLibrary.set_editor_viewport_visualize_buffer.__doc__}
if h:r['broken']=str(unreal.GameplayStatics.break_hit_result(h))
pathlib.Path('D:/3d-research/artifacts/fujian-mountain-art-ab/showcase-ground-probe.json').write_text(json.dumps(r,indent=2),encoding='utf-8')
le=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
le.set_level_viewport_camera_info(unreal.Vector(254241,-538951,37591),unreal.MathLibrary.find_look_at_rotation(unreal.Vector(254241,-538951,37591),unreal.Vector(296853,-457194,17838)))
le.set_level_viewport_fov(62.0)
unreal.AutomationLibrary.set_editor_viewport_view_mode(unreal.ViewModeIndex.VMI_LIT)
