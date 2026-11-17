import unreal,json,pathlib
ea=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
pp=next(a for a in ea.get_all_level_actors() if a.get_actor_label()=='AB_Common_FixedExposure')
s=pp.get_editor_property('settings');s.set_editor_property('auto_exposure_bias',-8.0);pp.set_editor_property('settings',s)
info={'cvars':{n:unreal.SystemLibrary.get_console_variable_int_value(n) for n in ['r.EyeAdaptationQuality','r.DefaultFeature.AutoExposure','r.DefaultFeature.AutoExposure.ExtendDefaultLuminanceRange']},'settings':{n:str(s.get_editor_property(n)) for n in ['auto_exposure_method','auto_exposure_apply_physical_camera_exposure','camera_iso','camera_shutter_speed','depth_of_field_fstop','auto_exposure_bias']}}
pathlib.Path('D:/3d-research/artifacts/fujian-mountain-art-ab/exposure-probe.json').write_text(json.dumps(info,indent=2),encoding='utf-8')
cam=next(a for a in ea.get_all_level_actors() if a.get_actor_label()=='AB_Camera_Normal')
AB_EV_TASK=unreal.AutomationLibrary.take_high_res_screenshot(1920,1080,'D:/3d-research/artifacts/fujian-mountain-art-ab/exposure-minus8.png',camera=cam,delay=0)
unreal.EditorLevelLibrary.editor_invalidate_viewports()
