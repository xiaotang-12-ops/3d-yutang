"""将已校验的CC0自然素材导入实验内容；不修改Bridge引擎母材质。-codex，2026-10-23"""
import unreal,pathlib,json
ROOT=pathlib.Path('D:/3d-research/experiments/fujian-mountain-art-ab')
OUT=pathlib.Path('D:/3d-research/artifacts/fujian-mountain-art-ab')
at=unreal.AssetToolsHelpers.get_asset_tools()
records=[]
for ident in ['aerial_grass_rock','forest_ground_04','rocky_terrain_02']:
    for p in (ROOT/'AssetsSource'/ident).glob('*.jpg'):
        task=unreal.AssetImportTask();task.filename=str(p);task.destination_path='/Game/ArtAB/Textures/'+ident;task.automated=True;task.save=True
        at.import_asset_tasks([task])
        for name in task.imported_object_paths:
            obj=unreal.load_asset(name)
            if '_nor_' in p.name:
                obj.set_editor_property('compression_settings',unreal.TextureCompressionSettings.TC_NORMALMAP)
                obj.set_editor_property('srgb',False)
            elif '_arm_' in p.name:
                obj.set_editor_property('compression_settings',unreal.TextureCompressionSettings.TC_MASKS)
                obj.set_editor_property('srgb',False)
            unreal.EditorAssetLibrary.save_loaded_asset(obj)
            records.append({'source':str(p),'asset':name})
for ident in ['island_tree_02','pine_sapling_small','rock_moss_set_01','rock_face_01','boulder_01']:
    p=next((ROOT/'AssetsSource'/ident).glob('*.gltf'))
    task=unreal.AssetImportTask();task.filename=str(p);task.destination_path='/Game/ArtAB/Nature/'+ident;task.automated=True;task.save=True
    at.import_asset_tasks([task])
    records.append({'source':str(p),'assets':list(task.imported_object_paths)})
(OUT/'imported-assets.json').write_text(json.dumps(records,indent=2),encoding='utf-8')
unreal.EditorLevelLibrary.editor_invalidate_viewports()
