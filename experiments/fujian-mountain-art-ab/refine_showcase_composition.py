import unreal,json,pathlib
asys=unreal.get_editor_subsystem(unreal.EditorActorSubsystem);le=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
r=[]
for a in asys.get_all_level_actors():
 if a.get_actor_label().startswith('Showcase_Foliage_') and any(k in a.get_actor_label() for k in ['rock_face','rock_moss','boulder']):
  c=a.get_components_by_class(unreal.HierarchicalInstancedStaticMeshComponent)[0];m=c.get_editor_property('static_mesh');b=m.get_bounds()
  for i in range(c.get_instance_count()):
   t=c.get_instance_transform(i,True)
   v=t.translation;v.z-=b.box_extent.z*t.scale3d.z*(.7 if 'rock_face' in a.get_actor_label() else .25)
   t.translation=v;c.update_instance_transform(i,t,True,i==c.get_instance_count()-1,True)
  a.modify();r.append({'actor':a.get_actor_label(),'count':c.get_instance_count()})
# 显示更集中于地表与中景的林地观察点，镜头无需锁定。
cam=next(a for a in asys.get_all_level_actors() if a.get_actor_label()=='Showcase_Camera_Ground')
loc=unreal.Vector(291677,-476512,11200);target=unreal.Vector(303324,-433046,12700)
cam.set_actor_location(loc,False,False);cam.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(loc,target),False)
le.set_level_viewport_camera_info(loc,cam.get_actor_rotation(),le.get_active_viewport_config_key());le.set_level_viewport_fov(55.0,le.get_active_viewport_config_key())
assert le.save_current_level()
pathlib.Path('D:/3d-research/artifacts/fujian-mountain-art-ab/showcase-rock-pass.json').write_text(json.dumps(r,indent=2),encoding='utf-8')
