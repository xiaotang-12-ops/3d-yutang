import unreal,json,pathlib
m=unreal.load_asset('/Game/ArtAB/Materials/MI_AB_Terrain')
asys=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
asys.clear_actor_selection_set()
r={}
for a in asys.get_all_level_actors():
 if isinstance(a,unreal.Landscape):a.set_editor_property('landscape_material',m)
 if a.get_actor_label()=='AB_Common_FixedExposure':
  s=a.get_editor_property('settings');s.set_editor_property('auto_exposure_bias',-.7);a.set_editor_property('settings',s)
 for cls in [unreal.DirectionalLightComponent,unreal.SkyLightComponent]:
  for c in a.get_components_by_class(cls):
   r[a.get_actor_label()]={'intensity':c.get_editor_property('intensity'),'rotation':str(a.get_actor_rotation()),'color':str(c.get_editor_property('light_color'))}
r['quality']={n:unreal.SystemLibrary.get_console_variable_int_value(n) for n in ['r.MaterialQualityLevel','sg.ShadingQuality','r.EyeAdaptationQuality']}
pathlib.Path('D:/3d-research/artifacts/fujian-mountain-art-ab/lighting-probe.json').write_text(json.dumps(r,indent=2),encoding='utf-8')
unreal.EditorLevelLibrary.editor_invalidate_viewports()
