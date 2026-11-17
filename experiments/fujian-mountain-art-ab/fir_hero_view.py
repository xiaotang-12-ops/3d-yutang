import unreal,pathlib,json
asys=unreal.get_editor_subsystem(unreal.EditorActorSubsystem);le=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem);w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
a=next(a for a in asys.get_all_level_actors() if a.get_actor_label()=='Showcase_Mature_Fir_Forest');c=a.get_components_by_class(unreal.HierarchicalInstancedStaticMeshComponent)[0];m=c.get_editor_property('static_mesh')
x,y=292477,-474012
h=unreal.SystemLibrary.line_trace_single(w,unreal.Vector(x,y,100000),unreal.Vector(x,y,-10000),unreal.TraceTypeQuery.ECC_VISIBILITY,True,[],unreal.DrawDebugTrace.NONE,True);p=h.to_dict()['location']
a=asys.spawn_actor_from_class(unreal.StaticMeshActor,unreal.Vector(p.x,p.y,p.z+12));a.set_actor_label('Showcase_Fir_Hero');a.static_mesh_component.set_static_mesh(m);a.static_mesh_component.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
loc=unreal.Vector(p.x-1700,p.y-2200,p.z+250);target=unreal.Vector(p.x,p.y,p.z+400)
le.set_level_viewport_camera_info(loc,unreal.MathLibrary.find_look_at_rotation(loc,target),le.get_active_viewport_config_key());asys.clear_actor_selection_set()
pathlib.Path('D:/3d-research/artifacts/fujian-mountain-art-ab/fir-hero-state.json').write_text(json.dumps({'ground':str(p),'meshSections':m.get_num_sections(0),'verts':unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem).get_number_verts(m,0)}),encoding='utf-8')
le.save_current_level();unreal.EditorLevelLibrary.editor_invalidate_viewports()
