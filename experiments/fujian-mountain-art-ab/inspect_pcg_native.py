import unreal,json,pathlib
names=['PCGWorldRayHitSettings','PCGWorldRayHitQueryParams','PCGWorldQueryFilter','PCGNormalToDensitySettings','PCGDensityFilterSettings','PCGSoftISMComponentDescriptor','PCGComponent','PCGVolume']
info={n:getattr(unreal,n).__doc__ if hasattr(unreal,n) else None for n in names}
g=unreal.load_asset('/PCG/SampleContent/SimpleForest/PCGGraphs/SimpleForestWorldRayHits')
info['nodes']=[{'name':n.get_name(),'class':n.get_settings().get_class().get_name(),'settings':str(n.get_settings()),'properties':[x for x in dir(n.get_settings()) if not x.startswith('_')]} for n in g.nodes]
info['node_methods']=[x for x in dir(unreal.PCGNode) if not x.startswith('_')]
info['component_methods']={x:str(getattr(unreal.PCGComponent,x).__doc__) for x in dir(unreal.PCGComponent) if any(k in x for k in ['generate','graph','cleanup'])}
pathlib.Path('D:/3d-research/artifacts/fujian-mountain-art-ab/pcg-native-api.json').write_text(json.dumps(info,indent=2),encoding='utf-8')
