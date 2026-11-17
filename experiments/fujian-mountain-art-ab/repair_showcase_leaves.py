import unreal,json,pathlib
asys=unreal.get_editor_subsystem(unreal.EditorActorSubsystem);ml=unreal.MaterialEditingLibrary
actor=next(a for a in asys.get_all_level_actors() if a.get_actor_label()=='Showcase_Foliage_island_tree_02_1k')
c=actor.get_components_by_class(unreal.HierarchicalInstancedStaticMeshComponent)[0]
old=c.get_material(1)
leaf=unreal.EditorAssetLibrary.duplicate_asset(old.get_path_name(),'/Game/ArtAB/Materials/MI_Showcase_Leaves')
assert leaf
parent=unreal.load_asset('/InterchangeAssets/gltf/MaterialInstances/MI_Default_Opaque_DS');assert parent
ml.set_material_instance_parent(leaf,parent);ml.update_material_instance(leaf)
c.set_material(1,leaf);unreal.EditorAssetLibrary.save_loaded_asset(leaf)
r={'leaf':leaf.get_path_name(),'parent':parent.get_path_name(),'textures':{str(n):str(ml.get_material_instance_texture_parameter_value(leaf,n)) for n in ml.get_texture_parameter_names(leaf)}}
for a in asys.get_all_level_actors():
 if isinstance(a,unreal.SkyLight):
  c=a.light_component;c.set_editor_property('real_time_capture',False);c.recapture_sky()
  r['skyIntensity']=c.get_editor_property('intensity')
r['cubemaps']=[a.object_path_string if hasattr(a,'object_path_string') else str(a.package_name) for a in unreal.AssetRegistryHelpers.get_asset_registry().get_assets_by_class(unreal.TopLevelAssetPath('/Script/Engine','TextureCube'),True)][:40]
pathlib.Path('D:/3d-research/artifacts/fujian-mountain-art-ab/showcase-leaf-repair.json').write_text(json.dumps(r,indent=2),encoding='utf-8')
unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
