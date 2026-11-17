"""验证原生程序网格组件的编辑器序列化入口，不更换地形算法。-codex，2026-10-23"""
import pathlib
es=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
actor=es.spawn_actor_from_class(unreal.Actor,unreal.Vector())
actor.set_actor_label('AB_ProceduralMesh_AdapterTest')
component=actor.call_method('AddComponentByClass',args=(unreal.ProceduralMeshComponent.static_class(),False,unreal.Transform(),False))
pathlib.Path('D:/3d-research/artifacts/fujian-mountain-art-ab/component-test.txt').write_text(str(component),encoding='utf-8')
