import unreal,json,pathlib
unreal.EditorLevelLibrary.editor_invalidate_viewports()
info={}
for name in ['Landscape','LandscapeEditorObject','LandscapeImportHelper','PCGGraph','PCGGraphFactory','PCGStaticMeshSpawnerSettings','PCGSurfaceSamplerSettings','PCGGetLandscapeSettings','PCGVolume','PCGComponent','StaticMeshReductionOptions','MeshReductionSettings','EditorStaticMeshLibrary']:
 c=getattr(unreal,name,None)
 info[name]={'doc':c.__doc__,'methods':[n for n in dir(c) if not n.startswith('_')]} if c else None
pathlib.Path('D:/3d-research/artifacts/fujian-mountain-art-ab/terrain-lod-api.json').write_text(json.dumps(info,indent=2),encoding='utf-8')
