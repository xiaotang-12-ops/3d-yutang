import unreal,json,pathlib
O=pathlib.Path('D:/3d-research/artifacts/fujian-mountain-art-ab')
assert pathlib.Path(unreal.Paths.project_dir()).resolve()==pathlib.Path('D:/3d-research/experiments/fujian-mountain-art-ab')
t=unreal.AssetImportTask()
t.filename='D:/3d-research/experiments/fujian-mountain-art-ab/AssetsSource/fir_sapling_medium/fir_showcase_b.gltf'
t.destination_path='/Game/ArtAB/Nature/fir_sapling_medium'
t.automated=True;t.save=True
unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([t])
assets=unreal.AssetRegistryHelpers.get_asset_registry().get_assets_by_path(t.destination_path,recursive=True)
report=[]
for a in assets:
 if str(a.asset_class_path.asset_name)=='StaticMesh':
  m=a.get_asset();b=m.get_bounds()
  report.append({'path':m.get_path_name(),'bounds':str(b),'materials':[m.get_material(i).get_path_name() for i in range(m.get_num_sections(0))]})
(O/'showcase-fir-import.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
