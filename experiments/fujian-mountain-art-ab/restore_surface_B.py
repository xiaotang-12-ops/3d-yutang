import unreal,pathlib
ls=next(a for a in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors() if isinstance(a,unreal.Landscape))
ls.set_editor_property('landscape_material',unreal.load_asset('/Game/ArtAB/Materials/MI_AB_Terrain'))
unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
exec(compile(pathlib.Path('D:/3d-research/experiments/fujian-mountain-art-ab/capture_B_preview.py').read_text(),'<preview>','exec'))
