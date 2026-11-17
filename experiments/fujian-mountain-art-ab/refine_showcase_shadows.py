import unreal,json,pathlib
asys=unreal.get_editor_subsystem(unreal.EditorActorSubsystem);r=[]
for a in asys.get_all_level_actors():
 if isinstance(a,unreal.DirectionalLight):
  c=a.light_component
  before={k:str(c.get_editor_property(k)) for k in ['dynamic_shadow_distance_movable_light','dynamic_shadow_cascades','shadow_bias','shadow_slope_bias']}
  c.set_editor_property('dynamic_shadow_distance_movable_light',80000.0);c.set_editor_property('dynamic_shadow_cascades',4);c.set_editor_property('shadow_bias',.8);c.set_editor_property('shadow_slope_bias',1.2)
  r.append({'before':before,'after':{k:str(c.get_editor_property(k)) for k in before}})
 if a.get_actor_label()=='AB_Common_FixedExposure':
  s=a.get_editor_property('settings');s.set_editor_property('auto_exposure_bias',.5);a.set_editor_property('settings',s)
unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
pathlib.Path('D:/3d-research/artifacts/fujian-mountain-art-ab/showcase-shadow-pass.json').write_text(json.dumps(r,indent=2),encoding='utf-8')
