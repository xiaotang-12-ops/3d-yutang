def build_grass_glade():
 import unreal,pathlib,json,math,random
 asys=unreal.get_editor_subsystem(unreal.EditorActorSubsystem);le=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem);w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world();ml=unreal.MaterialEditingLibrary
 assert w.get_path_name().startswith('/Game/ArtAB/Mountain_Showcase.')
 assets=unreal.AssetRegistryHelpers.get_asset_registry().get_assets_by_path('/Game/ArtAB/Nature/grass_medium_02',recursive=True)
 meshes=[a.get_asset() for a in assets if str(a.asset_class_path.asset_name)=='StaticMesh'];assert len(meshes)==2
 def ground(x,y):
  h=unreal.SystemLibrary.line_trace_single(w,unreal.Vector(x,y,150000),unreal.Vector(x,y,-10000),unreal.TraceTypeQuery.ECC_VISIBILITY,True,[],unreal.DrawDebugTrace.NONE,True)
  return h.to_dict() if h else None
 groups=[[],[]];rng=random.Random(8318)
 for ix in range(-32,33):
  for iy in range(-50,51):
   x=293300+(ix+rng.uniform(-.4,.4))*85;y=-473000+(iy+rng.uniform(-.4,.4))*85
   if rng.random()<.12+.17*math.sin(x/700)*math.sin(y/1100):continue
   d=ground(x,y)
   if not d or d['normal'].z<.78:continue
   j=rng.randrange(2);b=meshes[j].get_bounds();sc=rng.uniform(.85,1.6);p=d['location'];yaw=rng.uniform(-180,180);rad=math.radians(yaw)
   loc=unreal.Vector(p.x-sc*(b.origin.x*math.cos(rad)-b.origin.y*math.sin(rad)),p.y-sc*(b.origin.x*math.sin(rad)+b.origin.y*math.cos(rad)),p.z-sc*(b.origin.z-b.box_extent.z)-2)
   groups[j].append(unreal.Transform(location=loc,rotation=unreal.Rotator(yaw=yaw),scale=unreal.Vector(sc,sc,sc)))
 ss=unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem);sbl=unreal.SubobjectDataBlueprintFunctionLibrary;counts=[]
 for mesh,trans in zip(meshes,groups):
  actor=asys.spawn_actor_from_class(unreal.Actor,unreal.Vector());actor.set_actor_label('Showcase_Gl ade_Grass_'+mesh.get_name());actor.set_folder_path('Showcase/Vegetation_and_Rocks');hs=ss.k2_gather_subobject_data_for_instance(actor)
  handle,reason=ss.add_new_subobject(unreal.AddNewSubobjectParams(parent_handle=hs[0],new_class=unreal.HierarchicalInstancedStaticMeshComponent,conform_transform_to_parent=True))
  c=sbl.get_associated_object(sbl.get_data(handle));c.set_static_mesh(mesh);c.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION);c.set_editor_property('can_ever_affect_navigation',False);c.add_instances(trans,False,True,False);c.set_cull_distances(18000,22000);c.modify();actor.modify();counts.append(c.get_instance_count())
 # Restore the original UV direction after the independent readback disproved the flip hypothesis.
 mat=unreal.load_asset('/Game/ArtAB/Materials/M_Showcase_FirNeedles');uv=unreal.find_object(None,mat.get_path_name()+':MaterialExpressionTextureCoordinate_0')
 if uv:uv.set_editor_property('v_tiling',1.0);ml.recompile_material(mat);unreal.EditorAssetLibrary.save_loaded_asset(mat)
 leaf=unreal.load_asset('/Game/ArtAB/Materials/MI_Showcase_Leaves');names=[str(n) for n in ml.get_vector_parameter_names(leaf)]
 if 'BaseColorFactor' in names:ml.set_material_instance_vector_parameter_value(leaf,'BaseColorFactor',unreal.LinearColor(r=.55,g=.95,b=.36,a=1));ml.update_material_instance(leaf);unreal.EditorAssetLibrary.save_loaded_asset(leaf)
 p=ground(291677,-476512)['location'];loc=unreal.Vector(p.x,p.y,p.z+210);d=ground(294000,-471000);target=d['location']+unreal.Vector(0,0,180);rot=unreal.MathLibrary.find_look_at_rotation(loc,target)
 cam=next(a for a in asys.get_all_level_actors() if a.get_actor_label()=='Showcase_Camera_Ground');cam.set_actor_location_and_rotation(loc,rot,False,False);cam.camera_component.set_editor_property('field_of_view',55.0)
 le.set_level_viewport_camera_info(loc,rot,le.get_active_viewport_config_key());asys.clear_actor_selection_set();le.save_current_level();unreal.EditorLevelLibrary.editor_invalidate_viewports()
 pathlib.Path('D:/3d-research/artifacts/fujian-mountain-art-ab/showcase-grass-glade.json').write_text(json.dumps({'meshes':[m.get_path_name() for m in meshes],'grassCounts':counts,'leafVectors':names,'camera':str(loc),'target':str(target),'firUVRestored':bool(uv)},indent=2),encoding='utf-8')
build_grass_glade()
