import unreal,json,pathlib
ml=unreal.MaterialEditingLibrary;info={}
for layer in ['Base','Middle','Top']:
 f=unreal.load_asset('/Game/ArtAB/Materials/MF_AB_'+layer+'Textures')
 info[layer]=[{'name':str(n.get_editor_property('output_name')),'node':n.get_name()} for n in ml.get_material_function_expressions(f) if isinstance(n,unreal.MaterialExpressionFunctionOutput)]
for a in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors():
 if a.get_actor_label()=='AB_Common_FixedExposure':
  s=a.get_editor_property('settings');s.set_editor_property('auto_exposure_bias',-2.0);a.set_editor_property('settings',s)
info['func_doc']=unreal.MaterialExpressionMaterialFunctionCall.__doc__
info['hitdoc']=unreal.SystemLibrary.line_trace_single.__doc__
pathlib.Path('D:/3d-research/artifacts/fujian-mountain-art-ab/function-outputs.json').write_text(json.dumps(info,indent=2),encoding='utf-8')
