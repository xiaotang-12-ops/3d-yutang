import unreal,json,pathlib
asys=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
r={}
for a in asys.get_all_level_actors():
 if isinstance(a,unreal.Landscape):r['heightMethods']=[n for n in dir(a) if 'height' in n]
 if isinstance(a,unreal.SkyLight):
  c=a.light_component;c.set_mobility(unreal.ComponentMobility.MOVABLE);c.set_editor_property('real_time_capture',True);c.set_editor_property('intensity',1.5);c.recapture_sky()
  r['skyMobility']=str(c.get_editor_property('mobility'))
 if isinstance(a,unreal.DirectionalLight):a.light_component.set_mobility(unreal.ComponentMobility.MOVABLE)
r['hism_doc']=unreal.InstancedStaticMeshComponent.add_instances.__doc__
r['hismcull_doc']=unreal.InstancedStaticMeshComponent.set_cull_distances.__doc__
pathlib.Path('D:/3d-research/artifacts/fujian-mountain-art-ab/showcase-runtime-api.json').write_text(json.dumps(r,indent=2),encoding='utf-8')
