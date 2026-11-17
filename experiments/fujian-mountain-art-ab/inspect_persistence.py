"""只读核对动态组件丢失位置与蓝图原生组件保存接口。-codex，2026-10-23"""
import unreal,pathlib,json
info={}
es=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
info['actors']=[{'actor':a.get_actor_label(),'components':[{ 'path':c.get_path_name(),'class':c.get_class().get_name()} for c in a.get_components_by_class(unreal.ActorComponent)]} for a in es.get_all_level_actors()]
for cls in ['SubobjectDataSubsystem','SubobjectDataBlueprintFunctionLibrary','AddNewSubobjectParams','StaticMeshEditorSubsystem','ProceduralMeshComponent','KismetProceduralMeshLibrary']:
    c=getattr(unreal,cls,None); info[cls]={n:str(getattr(c,n).__doc__)[:5000] for n in dir(c) if any(x in n for x in ['add_new','gather','rename','set_lod','reduction','tangent','get_data','attach','get_object'])} if c else None
pathlib.Path('D:/3d-research/artifacts/fujian-mountain-art-ab/persistence-api.json').write_text(json.dumps(info,indent=2),encoding='utf-8')
