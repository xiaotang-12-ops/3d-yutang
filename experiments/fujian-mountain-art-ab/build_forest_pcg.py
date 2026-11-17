"""复用Epic SimpleForestWorldRayHits图；仅替换来源网格、尺度/密度并增加坡面筛选。"""
import unreal,json,pathlib
ea=unreal.EditorAssetLibrary
g=unreal.load_asset('/Game/ArtAB/PCG/PCG_AB_Forest') or ea.duplicate_asset('/PCG/SampleContent/SimpleForest/PCGGraphs/SimpleForestWorldRayHits','/Game/ArtAB/PCG/PCG_AB_Forest')
nd={n.get_name():n for n in g.nodes}
ray=nd['WorldRayHitQuery_0'].get_settings()
q=ray.get_editor_property('query_params')
q.set_editor_property('actor_tag_filter',unreal.PCGWorldQueryFilter.INCLUDE)
q.set_editor_property('actor_tags_list','ABTerrain')
q.set_editor_property('ignore_pcg_hits',True)
q.set_editor_property('trace_complex',True)
ray.set_editor_property('query_params',q)
meshes={str(a.asset_name):a.get_asset() for a in unreal.AssetRegistryHelpers.get_asset_registry().get_assets_by_path('/Game/ArtAB/Nature',recursive=True) if str(a.asset_class_path.asset_name)=='StaticMesh'}
# 三个独立生境分支：疏密林冠、坡地岩块、林下幼树；所有散布由原生PCG执行。
records=[]
for sampler,transform,spawn,keys,density,ext,scales in [
 ('SurfaceSampler_3','TransformPoints_27','StaticMeshSpawner_5',['island_tree_02_1k'],.00010,3500,(12,20)),
 ('SurfaceSampler_13','TransformPoints_26','StaticMeshSpawner_15',['rock_face_01_1k','rock_moss_set_01_rock02','boulder_01_1k'],.000008,8000,(15,28)),
 ('SurfaceSampler_7','TransformPoints_29','StaticMeshSpawner_9',['pine_sapling_small_a','pine_sapling_small_b','pine_sapling_small_c'],.000035,5000,(18,30))]:
 s=nd[sampler].get_settings();s.set_editor_property('points_per_squared_meter',density);s.set_editor_property('point_extents',unreal.Vector(ext,ext,1000));s.set_editor_property('looseness',.95)
 t=nd[transform].get_settings();t.set_editor_property('scale_min',unreal.Vector(scales[0],scales[0],scales[0]));t.set_editor_property('scale_max',unreal.Vector(scales[1],scales[1],scales[1]));t.set_editor_property('uniform_scale',True)
 t.set_editor_property('absolute_rotation',True);t.set_editor_property('rotation_min',unreal.Rotator(pitch=0,yaw=-180,roll=0));t.set_editor_property('rotation_max',unreal.Rotator(pitch=0,yaw=180,roll=0))
 sp=nd[spawn].get_settings();selector=sp.get_editor_property('mesh_selector_parameters')
 entries=[]
 for key in keys:
  desc=unreal.PCGSoftISMComponentDescriptor();desc.set_editor_property('static_mesh',meshes[key]);desc.set_editor_property('component_class',unreal.HierarchicalInstancedStaticMeshComponent)
  desc.set_editor_property('cast_shadow',True);desc.set_editor_property('can_ever_affect_navigation',False)
  entry=unreal.PCGMeshSelectorWeightedEntry();entry.set_editor_property('descriptor',desc);entry.set_editor_property('weight',1);entries.append(entry)
 selector.set_editor_property('mesh_entries',entries)
 sp.set_editor_property('synchronous_load',True)
 records.append({'sampler':sampler,'meshPaths':[meshes[k].get_path_name() for k in keys],'pointsPerM2':density,'uniformScale':scales})
# 模板树分支先按法线形成密度，去掉陡崖上的树；保留模板的变换、自动疏离与差集。
normal_node,normal_settings=g.add_node_of_type(unreal.PCGNormalToDensitySettings)
normal_settings.set_editor_property('normal',unreal.Vector(0,0,1));normal_settings.set_editor_property('strength',1.0)
filter_node,filter_settings=g.add_node_of_type(unreal.PCGDensityFilterSettings)
filter_settings.set_editor_property('lower_bound',.38);filter_settings.set_editor_property('upper_bound',1.0)
g.remove_edge(nd['SurfaceSampler_3'],'Out',nd['TransformPoints_27'],'In')
g.add_edge(nd['SurfaceSampler_3'],'Out',normal_node,'In');g.add_edge(normal_node,'Out',filter_node,'In');g.add_edge(filter_node,'Out',nd['TransformPoints_27'],'In')
g.modify();ea.save_loaded_asset(g)
pathlib.Path('D:/3d-research/artifacts/fujian-mountain-art-ab/pcg-bindings.json').write_text(json.dumps({'graph':g.get_path_name(),'template':'/PCG/SampleContent/SimpleForest/PCGGraphs/SimpleForestWorldRayHits','terrainFilterTag':'ABTerrain','branches':records},indent=2),encoding='utf-8')
