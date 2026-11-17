"""独立视觉样板，保留原版/A/B地图；按用户授权在副本降低高度夸张。"""
import unreal,json,pathlib
le=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
asys=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
assert le.new_level_from_template('/Game/ArtAB/Mountain_Showcase','/Game/ArtAB/B_Landscape')
for a in list(asys.get_all_level_actors()):
 if isinstance(a,unreal.PCGVolume):asys.destroy_actor(a)
ls=next(a for a in asys.get_all_level_actors() if isinstance(a,unreal.Landscape))
ls.set_actor_scale3d(unreal.Vector(7067.936507936358,6349.912988270929,800))
ls.set_actor_label('Mountain_Showcase_1_5x_OriginalElevation')
mi=unreal.EditorAssetLibrary.duplicate_asset('/Game/ArtAB/Materials/MI_AB_Terrain','/Game/ArtAB/Materials/MI_Mountain_Showcase')
assert mi
ls.set_editor_property('landscape_material',mi)
for a in asys.get_all_level_actors():
 if a.get_actor_label()=='AB_Common_FixedExposure':
  s=a.get_editor_property('settings');s.set_editor_property('auto_exposure_bias',0.0);a.set_editor_property('settings',s)
 if a.get_actor_label()=='AB_Common_Sun':
  a.set_actor_rotation(unreal.Rotator(pitch=-32,yaw=115,roll=0),False)
  c=a.get_components_by_class(unreal.DirectionalLightComponent)[0]
  c.set_editor_property('intensity',70000.0)
  c.set_editor_property('use_temperature',True);c.set_editor_property('temperature',5600.0)
  c.set_editor_property('dynamic_shadow_distance_movable_light',220000.0)
 if a.get_actor_label()=='AB_Common_SkyLight':
  c=a.get_components_by_class(unreal.SkyLightComponent)[0];c.set_editor_property('intensity',2.0)
ls.modify()
assert le.save_current_level()
pathlib.Path('D:/3d-research/artifacts/fujian-mountain-art-ab/showcase-created.json').write_text(json.dumps({'map':'/Game/ArtAB/Mountain_Showcase','landscape':ls.get_path_name(),'scale':str(ls.get_actor_scale3d()),'verticalExaggeration':1.5,'material':mi.get_path_name()},indent=2),encoding='utf-8')
unreal.AutomationLibrary.set_editor_viewport_view_mode(unreal.ViewModeIndex.VMI_LIT)
