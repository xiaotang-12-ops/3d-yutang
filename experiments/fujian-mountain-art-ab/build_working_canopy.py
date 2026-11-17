def build_working_canopy():
 import unreal,random,math,pathlib,json
 asys=unreal.get_editor_subsystem(unreal.EditorActorSubsystem);le=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem);w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
 assert w.get_path_name().startswith('/Game/ArtAB/Mountain_Showcase.')
 old=next(a for a in asys.get_all_level_actors() if a.get_actor_label()=='Showcase_Mature_Fir_Forest')
 old.get_components_by_class(unreal.HierarchicalInstancedStaticMeshComponent)[0].set_visibility(False)
 hero=next(a for a in asys.get_all_level_actors() if a.get_actor_label()=='Showcase_Fir_Hero');hero.static_mesh_component.set_visibility(False)
 original=next(a for a in asys.get_all_level_actors() if a.get_actor_label()=='Showcase_Foliage_island_tree_02_1k')
 mesh=original.get_components_by_class(unreal.HierarchicalInstancedStaticMeshComponent)[0].get_editor_property('static_mesh')
 rng=random.Random(7321);b=mesh.get_bounds();trans=[]
 for ix in range(-40,41):
  for iy in range(-48,49):
   x=296853+(ix+rng.uniform(-.4,.4))*1400;y=-457194+(iy+rng.uniform(-.4,.4))*1400
   density=.52+.25*math.sin(x/14000)+.2*math.sin(y/13000)
   if rng.random()>density:continue
   dx=x-291677;dy=y+476512
   if -1500<dy<21000 and abs(dx-dy*.268)<1000:continue
   hit=unreal.SystemLibrary.line_trace_single(w,unreal.Vector(x,y,150000),unreal.Vector(x,y,-10000),unreal.TraceTypeQuery.ECC_VISIBILITY,True,[],unreal.DrawDebugTrace.NONE,True)
   if not hit:continue
   d=hit.to_dict();p=d['location']
   if d['normal'].z<.76:continue
   sc=rng.uniform(2.5,3.8);yaw=rng.uniform(-180,180);a=math.radians(yaw)
   loc=unreal.Vector(p.x-sc*(b.origin.x*math.cos(a)-b.origin.y*math.sin(a)),p.y-sc*(b.origin.x*math.sin(a)+b.origin.y*math.cos(a)),p.z-sc*(b.origin.z-b.box_extent.z)-15)
   trans.append(unreal.Transform(location=loc,rotation=unreal.Rotator(yaw=yaw),scale=unreal.Vector(sc,sc,sc)))
 actor=asys.spawn_actor_from_class(unreal.Actor,unreal.Vector());actor.set_actor_label('Showcase_Broadleaf_Canopy');actor.set_folder_path('Showcase/Vegetation_and_Rocks')
 ss=unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem);sbl=unreal.SubobjectDataBlueprintFunctionLibrary;hs=ss.k2_gather_subobject_data_for_instance(actor)
 handle,reason=ss.add_new_subobject(unreal.AddNewSubobjectParams(parent_handle=hs[0],new_class=unreal.HierarchicalInstancedStaticMeshComponent,conform_transform_to_parent=True))
 c=sbl.get_associated_object(sbl.get_data(handle));c.set_static_mesh(mesh);c.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION);c.set_editor_property('can_ever_affect_navigation',False);c.add_instances(trans,False,True,False);c.set_cull_distances(110000,150000);c.modify();actor.modify()
 for a in asys.get_all_level_actors():
  if isinstance(a,unreal.SkyLight):a.light_component.set_editor_property('intensity',25000.0);a.light_component.recapture_sky()
  if isinstance(a,unreal.ExponentialHeightFog):a.component.set_editor_property('fog_density',.003)
 mi=unreal.load_asset('/Game/ArtAB/Materials/MI_Mountain_Showcase');ml=unreal.MaterialEditingLibrary
 ml.set_material_instance_vector_parameter_value(mi,'Top Layer Albedo Tint',unreal.LinearColor(r=.58,g=.8,b=.4,a=1));ml.update_material_instance(mi);unreal.EditorAssetLibrary.save_loaded_asset(mi)
 cam=next(a for a in asys.get_all_level_actors() if a.get_actor_label()=='Showcase_Camera_Overview')
 loc=unreal.Vector(285000,-486500,17000);tar=unreal.Vector(300000,-455000,13000);rot=unreal.MathLibrary.find_look_at_rotation(loc,tar)
 cam.set_actor_location_and_rotation(loc,rot,False,False);cam.camera_component.set_editor_property('field_of_view',50.0)
 le.set_level_viewport_camera_info(loc,rot,le.get_active_viewport_config_key());asys.clear_actor_selection_set();le.save_current_level();unreal.EditorLevelLibrary.editor_invalidate_viewports()
 pathlib.Path('D:/3d-research/artifacts/fujian-mountain-art-ab/showcase-working-canopy.json').write_text(json.dumps({'instances':c.get_instance_count(),'mesh':mesh.get_path_name(),'scaleRange':[2.5,3.8],'firHidden':True,'camera':str(loc)}),encoding='utf-8')
build_working_canopy()
