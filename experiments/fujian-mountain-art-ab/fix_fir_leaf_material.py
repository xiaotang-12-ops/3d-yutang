import unreal,json,pathlib
at=unreal.AssetToolsHelpers.get_asset_tools();ml=unreal.MaterialEditingLibrary
m=unreal.load_asset('/Game/ArtAB/Materials/M_Showcase_FirNeedles')
assert not m,'single controlled material build'
m=at.create_asset('M_Showcase_FirNeedles','/Game/ArtAB/Materials',unreal.Material,unreal.MaterialFactoryNew())
m.set_editor_property('blend_mode',unreal.BlendMode.BLEND_MASKED);m.set_editor_property('two_sided',True);m.set_editor_property('opacity_mask_clip_value',.25)
base='/Game/ArtAB/Nature/fir_sapling_medium/fir_showcase_b/Textures/'
def sample(name,x,y):
 t=unreal.load_asset(base+name);e=ml.create_material_expression(m,unreal.MaterialExpressionTextureSample,x,y);e.set_editor_property('texture',t);return e,t
e,t=sample('fir_sapling_medium_twigs_rgba_showcase',-400,-250)
t.set_editor_property('compression_no_alpha',False);unreal.EditorAssetLibrary.save_loaded_asset(t)
ml.connect_material_property(e,'RGB',unreal.MaterialProperty.MP_BASE_COLOR);ml.connect_material_property(e,'A',unreal.MaterialProperty.MP_OPACITY_MASK)
normal,nt=sample('fir_sapling_medium_twigs_nor_gl',-400,50);normal.set_editor_property('sampler_type',unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL);ml.connect_material_property(normal,'RGB',unreal.MaterialProperty.MP_NORMAL)
rough,rt=sample('fir_sapling_medium_twigs_rough',-400,300);ml.connect_material_property(rough,'G',unreal.MaterialProperty.MP_ROUGHNESS)
sp=ml.create_material_expression(m,unreal.MaterialExpressionConstant,-400,500);sp.set_editor_property('r',.25);ml.connect_material_property(sp,'',unreal.MaterialProperty.MP_SPECULAR)
ml.recompile_material(m);unreal.EditorAssetLibrary.save_loaded_asset(m)
mesh=unreal.load_asset('/Game/ArtAB/Nature/fir_sapling_medium/fir_showcase_b/StaticMeshes/fir_showcase_b');mesh.set_material(1,m);unreal.EditorAssetLibrary.save_loaded_asset(mesh)
unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level();unreal.EditorLevelLibrary.editor_invalidate_viewports()
pathlib.Path('D:/3d-research/artifacts/fujian-mountain-art-ab/fir-leaf-direct.json').write_text(json.dumps({'material':m.get_path_name(),'texture':t.get_path_name(),'alpha':str(t.get_editor_property('compression_no_alpha'))}),encoding='utf-8')
