import unreal,json,pathlib,math,random
O=pathlib.Path('D:/3d-research/artifacts/fujian-mountain-art-ab')
asys=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
assert w.get_path_name().startswith('/Game/ArtAB/Mountain_Showcase.')
assert not any(a.get_actor_label()=='Showcase_Mature_Fir_Forest' for a in asys.get_all_level_actors())
assets=unreal.AssetRegistryHelpers.get_asset_registry().get_assets_by_path('/Game/ArtAB/Nature/fir_sapling_medium',recursive=True)
m=next(a.get_asset() for a in assets if str(a.asset_class_path.asset_name)=='StaticMesh')
sm=unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem);s=sm.get_nanite_settings(m)
et=type(s.get_editor_property('fallback_target'));en=next(n for n in dir(et) if 'PERCENT' in n)
s.set_editor_property('fallback_target',getattr(et,en));s.set_editor_property('fallback_percent_triangles',.075);s.set_editor_property('fallback_relative_error',0.0)
sm.set_nanite_settings(m,s,True);unreal.EditorAssetLibrary.save_loaded_asset(m)
rng=random.Random(102317);transforms=[];b=m.get_bounds()
for ix in range(-48,49):
 for iy in range(-65,66):
  x=296853+(ix+rng.uniform(-.45,.45))*950;y=-457194+(iy+rng.uniform(-.45,.45))*950
  cluster=.5+.24*math.sin(x/12000)+.24*math.sin(y/15000+.7*math.cos(x/13000))
  if rng.random()>cluster*.85:continue
  dx=x-291677;dy=y+476512
  if -1500<dy<28000 and abs(dx-dy*.268)<650:continue
  h=unreal.SystemLibrary.line_trace_single(w,unreal.Vector(x,y,230000),unreal.Vector(x,y,-10000),unreal.TraceTypeQuery.ECC_VISIBILITY,True,[],unreal.DrawDebugTrace.NONE,True)
  if not h:continue
  d=h.to_dict();p=d['location']
  if d['normal'].z<.8:continue
  sc=rng.uniform(.82,1.25);yaw=rng.uniform(-180,180);a=math.radians(yaw)
  loc=unreal.Vector(p.x-sc*(b.origin.x*math.cos(a)-b.origin.y*math.sin(a)),p.y-sc*(b.origin.x*math.sin(a)+b.origin.y*math.cos(a)),p.z-sc*(b.origin.z-b.box_extent.z)-5)
  transforms.append(unreal.Transform(location=loc,rotation=unreal.Rotator(yaw=yaw),scale=unreal.Vector(sc,sc,sc)))
actor=asys.spawn_actor_from_class(unreal.Actor,unreal.Vector());actor.set_actor_label('Showcase_Mature_Fir_Forest');actor.set_folder_path('Showcase/Vegetation_and_Rocks')
ss=unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem);sbl=unreal.SubobjectDataBlueprintFunctionLibrary
hs=ss.k2_gather_subobject_data_for_instance(actor)
handle,reason=ss.add_new_subobject(unreal.AddNewSubobjectParams(parent_handle=hs[0],new_class=unreal.HierarchicalInstancedStaticMeshComponent,conform_transform_to_parent=True))
c=sbl.get_associated_object(sbl.get_data(handle));assert isinstance(c,unreal.HierarchicalInstancedStaticMeshComponent),str(reason)
c.set_static_mesh(m);c.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION);c.add_instances(transforms,False,True,False);c.set_cull_distances(120000,160000)
c.set_editor_property('cast_shadow',True);c.set_editor_property('can_ever_affect_navigation',False);actor.modify();c.modify()
mi=unreal.load_asset('/Game/ArtAB/Materials/MI_Mountain_Showcase')
unreal.MaterialEditingLibrary.set_material_instance_vector_parameter_value(mi,'Top Layer Tiling/Offset',unreal.LinearColor(r=.35,g=.35,b=0,a=0))
unreal.MaterialEditingLibrary.update_material_instance(mi);unreal.EditorAssetLibrary.save_loaded_asset(mi)
asys.clear_actor_selection_set();unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
(O/'showcase-fir-scatter.json').write_text(json.dumps({'mesh':m.get_path_name(),'instances':c.get_instance_count(),'heightCm':b.box_extent.z*2,'fallbackVertices':sm.get_number_verts(m,0),'materials':[c.get_material(i).get_path_name() for i in range(c.get_num_materials())]},indent=2),encoding='utf-8')
