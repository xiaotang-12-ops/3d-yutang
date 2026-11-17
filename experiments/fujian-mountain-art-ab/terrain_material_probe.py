import unreal,pathlib,json
ea=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
ls=next(a for a in ea.get_all_level_actors() if isinstance(a,unreal.Landscape))
ls.set_editor_property('landscape_material',unreal.load_asset('/Game/ArtAB/Materials/M_AB_RenderProbe'))
le=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
le.editor_set_viewport_realtime(True)
exec(compile(pathlib.Path('D:/3d-research/experiments/fujian-mountain-art-ab/capture_B_preview.py').read_text(),'<probe>','exec'))
