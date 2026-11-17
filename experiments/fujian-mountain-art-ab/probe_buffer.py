import unreal,json,pathlib
unreal.AutomationLibrary.set_editor_viewport_view_mode(unreal.ViewModeIndex.VMI_BUFFER_VISUALIZATION)
w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
unreal.SystemLibrary.execute_console_command(w,'r.BufferVisualizationTarget BaseColor')
r={}
for a in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors():
 if a.get_actor_label()=='AB_Common_FixedExposure':
  s=a.get_editor_property('settings')
  for n in ['auto_exposure_method','auto_exposure_bias','auto_exposure_apply_physical_camera_exposure','override_auto_exposure_method','override_auto_exposure_bias','override_auto_exposure_apply_physical_camera_exposure','override_camera_iso','override_camera_shutter_speed','override_depth_of_field_fstop','camera_iso','camera_shutter_speed','depth_of_field_fstop']:
   r[n]=str(s.get_editor_property(n))
pathlib.Path('D:/3d-research/artifacts/fujian-mountain-art-ab/exposure-overrides.json').write_text(json.dumps(r,indent=2),encoding='utf-8')
