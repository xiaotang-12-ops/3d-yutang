import unreal,pathlib,json
assert pathlib.Path(unreal.Paths.project_dir()).resolve()==pathlib.Path('D:/3d-research/experiments/fujian-mountain-art-ab')
unreal.EditorAssetLibrary.save_directory('/Game/ArtAB',only_if_is_dirty=True,recursive=True)
assert unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
pathlib.Path('D:/3d-research/artifacts/fujian-mountain-art-ab/showcase-pre-dx12-save.json').write_text(json.dumps({'saved':True,'world':unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world().get_path_name()}),encoding='utf-8')
