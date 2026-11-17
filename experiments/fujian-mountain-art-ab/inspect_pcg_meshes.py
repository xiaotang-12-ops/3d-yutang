import unreal,json,pathlib
r=unreal.AssetRegistryHelpers.get_asset_registry()
info={'meshes':[],'api':{}}
for a in r.get_assets_by_path('/Game/ArtAB/Nature',recursive=True):
 if a.asset_class_path.asset_name=='StaticMesh':
  m=a.get_asset();b=m.get_bounds();info['meshes'].append({'path':m.get_path_name(),'bounds':str(b),'verticesLOD0':m.get_num_vertices(0)})
for name in ['PCGWorldRayHitQuerySettings','PCGSurfaceSamplerSettings','PCGStaticMeshSpawnerSettings','PCGMeshSelectorWeighted','PCGMeshSelectorWeightedEntry','PCGMeshSelectorByAttribute','PCGTransformPointsSettings','PCGGraphFactory','StaticMeshReductionSettings']:
 c=getattr(unreal,name,None); info['api'][name]=c.__doc__ if c else None
for name in ['add_node_of_type','add_edge']:
 info[name]=getattr(unreal.PCGGraph,name).__doc__
pathlib.Path('D:/3d-research/artifacts/fujian-mountain-art-ab/pcg-mesh-api.json').write_text(json.dumps(info,indent=2),encoding='utf-8')
