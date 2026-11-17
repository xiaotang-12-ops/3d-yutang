import unreal,json,pathlib
ml=unreal.MaterialEditingLibrary;m=unreal.load_asset('/Game/ArtAB/Materials/M_Showcase_FirNeedles')
uv=ml.create_material_expression(m,unreal.MaterialExpressionTextureCoordinate,-700,-250);uv.set_editor_property('u_tiling',1.0);uv.set_editor_property('v_tiling',-1.0)
for e in unreal.MaterialEditingLibrary.get_material_expressions(m) if hasattr(unreal.MaterialEditingLibrary,'get_material_expressions') else []:
 if isinstance(e,unreal.MaterialExpressionTextureSample):ml.connect_material_expressions(uv,'',e,'UVs')
# Native material expression collection is available through MaterialEditingLibrary only on some builds.
# Reuse the exact expressions created by the preceding controlled material job, kept by the experiment bridge.
for e in [globals().get('e'),globals().get('normal'),globals().get('rough')]:
 if isinstance(e,unreal.MaterialExpressionTextureSample):ml.connect_material_expressions(uv,'',e,'UVs')
ml.recompile_material(m);unreal.EditorAssetLibrary.save_loaded_asset(m);unreal.EditorLevelLibrary.editor_invalidate_viewports()
pathlib.Path('D:/3d-research/artifacts/fujian-mountain-art-ab/fir-uv-correction.json').write_text(json.dumps({'material':m.get_path_name(),'uvV':-1,'bindings':[str(globals().get(n)) for n in ['e','normal','rough']]}),encoding='utf-8')
