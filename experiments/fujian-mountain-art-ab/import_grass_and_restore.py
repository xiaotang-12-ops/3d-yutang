def import_grass_and_restore():
 import unreal,pathlib,json
 t=unreal.AssetImportTask();t.filename='D:/3d-research/experiments/fujian-mountain-art-ab/AssetsSource/grass_medium_02/grass_showcase_ce.gltf';t.destination_path='/Game/ArtAB/Nature/grass_medium_02';t.automated=True;t.save=True
 unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([t])
 asys=unreal.get_editor_subsystem(unreal.EditorActorSubsystem);le=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
 for a in asys.get_all_level_actors():
  if a.get_actor_label()=='Showcase_Mature_Fir_Forest':a.get_components_by_class(unreal.HierarchicalInstancedStaticMeshComponent)[0].set_visibility(False)
  if a.get_actor_label()=='Showcase_Fir_Hero':a.static_mesh_component.set_visibility(False)
  if a.get_actor_label()=='Showcase_Broadleaf_Canopy':a.get_components_by_class(unreal.HierarchicalInstancedStaticMeshComponent)[0].set_visibility(True)
 # Export existing imported alpha once for an independent pixel check; no more fir shader changes.
 tx=unreal.load_asset('/Game/ArtAB/Nature/fir_sapling_medium/fir_showcase_b/Textures/fir_sapling_medium_twigs_rgba_showcase')
 exp=unreal.AssetExportTask();exp.object=tx;exp.filename='D:/3d-research/artifacts/fujian-mountain-art-ab/fir-native-rgba.png';exp.exporter=unreal.TextureExporterPNG();exp.automated=True;exp.prompt=False;exp.replace_identical=True
 ok=unreal.Exporter.run_asset_export_task(exp)
 assets=unreal.AssetRegistryHelpers.get_asset_registry().get_assets_by_path(t.destination_path,recursive=True)
 r={'meshes':[a.get_object_path_string() for a in assets if str(a.asset_class_path.asset_name)=='StaticMesh'],'firTextureExport':ok,'firTextureAddressY':str(tx.get_editor_property('address_y'))}
 pathlib.Path('D:/3d-research/artifacts/fujian-mountain-art-ab/grass-native-import.json').write_text(json.dumps(r,indent=2),encoding='utf-8')
 le.save_current_level();asys.clear_actor_selection_set()
import_grass_and_restore()
