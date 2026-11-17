"""复用已导入模型，以原生持久化HISM建立局部林地；所有落点使用UE真实地面射线。"""
import unreal,json,pathlib,random,math
R=pathlib.Path('D:/3d-research/experiments/fujian-mountain-art-ab')
O=pathlib.Path('D:/3d-research/artifacts/fujian-mountain-art-ab')
asys=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
le=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
assert w.get_path_name().startswith('/Game/ArtAB/Mountain_Showcase.')
assert not any(a.get_actor_label().startswith('Showcase_Foliage_') for a in asys.get_all_level_actors()),'No duplicate scatter'
meshes={str(a.asset_name):a.get_asset() for a in unreal.AssetRegistryHelpers.get_asset_registry().get_assets_by_path('/Game/ArtAB/Nature',recursive=True) if str(a.asset_class_path.asset_name)=='StaticMesh'}
ss=unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem);sbl=unreal.SubobjectDataBlueprintFunctionLibrary
rng=random.Random(20261023);groups={};report=[]
CX,CY=296853,-457194
# 留出低机位沿谷观察的视廊，树石不挡满镜头。
def clear_lane(x,y):
 dx=x-291677;dy=y+476512
 return -1500<dy<28000 and abs(dx-dy*.268)<650

def ground(x,y):
 h=unreal.SystemLibrary.line_trace_single(w,unreal.Vector(x,y,230000),unreal.Vector(x,y,-10000),unreal.TraceTypeQuery.ECC_VISIBILITY,True,[],unreal.DrawDebugTrace.NONE,True)
 if not h:return None
 d=h.to_dict();return d['location'],d['normal']

def add(key,x,y,scale,yaw,embed=0,rock=False):
 g=ground(x,y)
 if not g:return
 pos,normal=g
 if not rock and normal.z<.78:return
 mesh=meshes[key];b=mesh.get_bounds();a=math.radians(yaw)
 ox=b.origin.x*scale;oy=b.origin.y*scale
 # 纠正glTF组装遗留的横向偏移；根部落地，岩石适当埋入。
 loc=unreal.Vector(pos.x-(ox*math.cos(a)-oy*math.sin(a)),pos.y-(ox*math.sin(a)+oy*math.cos(a)),pos.z-(b.origin.z-b.box_extent.z)*scale-embed)
 rot=unreal.Rotator(pitch=rng.uniform(-8,8) if rock else 0,yaw=yaw,roll=rng.uniform(-7,7) if rock else 0)
 groups.setdefault(key,[]).append(unreal.Transform(location=loc,rotation=rot,scale=unreal.Vector(scale,scale,scale)))

# 约1.7×1.8km内有疏有密的林冠，使用5m级阔叶树而非放大为几十米。
for ix in range(-61,62):
 for iy in range(-64,65):
  x=CX+(ix+rng.uniform(-.48,.48))*1400;y=CY+(iy+rng.uniform(-.48,.48))*1400
  cluster=.5+.25*math.sin(x/13500)+.25*math.sin(y/17000+.8*math.cos(x/21000))
  if rng.random()>max(.08,cluster*.75) or clear_lane(x,y):continue
  add('island_tree_02_1k',x,y,rng.uniform(1.0,1.8),rng.uniform(-180,180),12)
# 近景幼松、苔石和低矮灌木层，保持源模型米制尺度。
for i in range(1900):
 x=CX+rng.uniform(-48000,48000);y=CY+rng.uniform(-60000,60000)
 if clear_lane(x,y):continue
 add(rng.choice(['pine_sapling_small_a','pine_sapling_small_b','pine_sapling_small_c']),x,y,rng.uniform(.75,1.5),rng.uniform(-180,180),4)
for i in range(430):
 x=CX+rng.uniform(-60000,60000);y=CY+rng.uniform(-65000,65000)
 add(rng.choice(['rock_moss_set_01_rock01','rock_moss_set_01_rock02','rock_moss_set_01_rock03','boulder_01_1k']),x,y,rng.uniform(.65,1.8),rng.uniform(-180,180),rng.uniform(12,35),True)
# 崖脚岩组及近镜头几块独立可读的岩石，避免全是远处黑点。
for i in range(28):
 x=291677+rng.uniform(-5000,6500);y=-476512+rng.uniform(1500,23000)
 if clear_lane(x,y):continue
 add(rng.choice(['rock_face_01_1k','boulder_01_1k','rock_moss_set_01_rock02']),x,y,rng.uniform(.8,1.8),rng.uniform(-180,180),25,True)
for key,transforms in groups.items():
 actor=asys.spawn_actor_from_class(unreal.Actor,unreal.Vector())
 actor.set_actor_label('Showcase_Foliage_'+key);actor.set_folder_path('Showcase/Vegetation_and_Rocks')
 handles=ss.k2_gather_subobject_data_for_instance(actor)
 handle,reason=ss.add_new_subobject(unreal.AddNewSubobjectParams(parent_handle=handles[0],new_class=unreal.HierarchicalInstancedStaticMeshComponent,conform_transform_to_parent=True))
 c=sbl.get_associated_object(sbl.get_data(handle));assert isinstance(c,unreal.HierarchicalInstancedStaticMeshComponent),str(reason)
 c.set_static_mesh(meshes[key]);c.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
 c.set_editor_property('cast_shadow',True);c.set_editor_property('can_ever_affect_navigation',False)
 c.add_instances(transforms,False,True,False)
 c.set_cull_distances(180000,240000)
 actor.modify();c.modify()
 report.append({'actor':actor.get_actor_label(),'mesh':meshes[key].get_path_name(),'instances':c.get_instance_count(),'materials':[c.get_material(i).get_path_name() if c.get_material(i) else None for i in range(c.get_num_materials())]})
mi=unreal.load_asset('/Game/ArtAB/Materials/MI_Mountain_Showcase');ml=unreal.MaterialEditingLibrary
for layer in ['Base','Middle','Top']:
 ml.set_material_instance_vector_parameter_value(mi,layer+' Layer Albedo Tint',unreal.LinearColor(r=1,g=1,b=1,a=1))
ml.update_material_instance(mi);unreal.EditorAssetLibrary.save_loaded_asset(mi)
# 保存可跳转的主/近景相机，但编辑器不锁相机，用户可直接环绕。
for name,loc,target in [('Overview',(254241,-538951,37591),(296853,-457194,17838)),('Ground',(291677,-476512,10570),(303324,-433046,15558))]:
 cam=asys.spawn_actor_from_class(unreal.CameraActor,unreal.Vector(*loc),unreal.MathLibrary.find_look_at_rotation(unreal.Vector(*loc),unreal.Vector(*target)))
 cam.set_actor_label('Showcase_Camera_'+name);cam.camera_component.set_editor_property('field_of_view',62.0)
asys.clear_actor_selection_set();assert le.save_current_level()
(O/'showcase-scatter.json').write_text(json.dumps({'seed':20261023,'method':'native ground traces + persistent HISM','groups':report,'total':sum(r['instances'] for r in report)},indent=2),encoding='utf-8')
