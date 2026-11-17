"""只读检查本实验所需编辑器API与现有材质。-codex，2026-10-23"""
import pathlib,json
out=pathlib.Path('D:/3d-research/artifacts/fujian-mountain-art-ab')
info={}
for cls in ['ProceduralMeshComponent','Landscape','LandscapeProxy','LandscapeEditorSubsystem','EditorActorSubsystem','Actor','PythonScriptLibrary','EditorLevelLibrary','EditorAssetLibrary']:
    c=getattr(unreal,cls,None)
    if c:
        info[cls]={n:str(getattr(c,n).__doc__)[:6000] for n in dir(c) if any(x in n for x in ['import','create_mesh','add_component','spawn','save','copy','call_method','execute','level'])}
    else:info[cls]=None
ar=unreal.AssetRegistryHelpers.get_asset_registry()
info['materials']=[a.object_path if hasattr(a,'object_path') else str(a.package_name) for a in ar.get_assets_by_path('/Engine/Functions',recursive=True) if any(x in str(a.asset_name).lower() for x in ['blend','aligned','slope'])][:100]
(out/'editor-api.json').write_text(json.dumps(info,indent=2),encoding='utf-8')
