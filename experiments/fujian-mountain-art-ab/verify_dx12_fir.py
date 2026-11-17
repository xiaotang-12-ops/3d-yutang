def verify_dx12_fir():
 import unreal,json,pathlib
 asys=unreal.get_editor_subsystem(unreal.EditorActorSubsystem);le=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
 actors=asys.get_all_level_actors();a=next(a for a in actors if a.get_actor_label()=='Showcase_Mature_Fir_Forest');c=a.get_components_by_class(unreal.HierarchicalInstancedStaticMeshComponent)[0];c.set_visibility(True)
 for a in actors:
  if a.get_actor_label()=='Showcase_Broadleaf_Canopy':a.get_components_by_class(unreal.HierarchicalInstancedStaticMeshComponent)[0].set_visibility(False)
  if a.get_actor_label()=='Showcase_Fir_Hero':a.static_mesh_component.set_visibility(True);a.static_mesh_component.set_static_mesh(c.get_editor_property('static_mesh'));p=a.get_actor_location()
 loc=unreal.Vector(p.x-1700,p.y-2200,p.z+238);rot=unreal.MathLibrary.find_look_at_rotation(loc,unreal.Vector(p.x,p.y,p.z+388));le.set_level_viewport_camera_info(loc,rot,le.get_active_viewport_config_key())
 asys.clear_actor_selection_set();unreal.EditorLevelLibrary.editor_invalidate_viewports()
 pathlib.Path('D:/3d-research/artifacts/fujian-mountain-art-ab/showcase-dx12-native.json').write_text(json.dumps({'nanite':unreal.SystemLibrary.get_console_variable_int_value('r.Nanite'),'projectNanite':unreal.SystemLibrary.get_console_variable_int_value('r.Nanite.ProjectEnabled'),'counts':{a.get_actor_label():sum(c.get_instance_count() for c in a.get_components_by_class(unreal.HierarchicalInstancedStaticMeshComponent)) for a in actors if a.get_components_by_class(unreal.HierarchicalInstancedStaticMeshComponent)},'viewmode':str(unreal.AutomationLibrary.get_editor_active_viewport_view_mode())}),encoding='utf-8')
 return unreal.AutomationLibrary.take_high_res_screenshot(1280,720,'D:/3d-research/artifacts/fujian-mountain-art-ab/fir-dx12-complete.png',delay=0.0)
AB_DX12_FIR_TASK=verify_dx12_fir()
