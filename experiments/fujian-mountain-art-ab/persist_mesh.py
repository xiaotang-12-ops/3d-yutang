"""以编辑器实例组件保存同一程序网格，重载后读取三角面数验证序列化。"""
import unreal,json,pathlib
R=pathlib.Path('D:/3d-research/experiments/fujian-mountain-art-ab')
O=pathlib.Path('D:/3d-research/artifacts/fujian-mountain-art-ab')
d=json.loads((R/'sample.json').read_text())
le=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
ea=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
ss=unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
lib=unreal.SubobjectDataBlueprintFunctionLibrary
report=[]
for path,mat in [('/Game/ArtAB/Original','/Game/Terrain/M_Terrain'),('/Game/ArtAB/A_Procedural','/Game/ArtAB/Materials/MI_AB_Terrain')]:
    assert le.load_level(path)
    a=next(a for a in ea.get_all_level_actors() if a.get_actor_label()=='Original_CPP_ProceduralMesh_SameData')
    handles=ss.k2_gather_subobject_data_for_instance(a)
    params=unreal.AddNewSubobjectParams(parent_handle=handles[0],new_class=unreal.ProceduralMeshComponent,conform_transform_to_parent=True)
    handle,reason=ss.add_new_subobject(params)
    pm=lib.get_associated_object(lib.get_data(handle))
    assert isinstance(pm,unreal.ProceduralMeshComponent),str(reason)
    pm.create_mesh_section_linear_color(0,[unreal.Vector(*v) for v in d['vertices']],d['triangles'],[unreal.Vector(*v) for v in d['normals']],[unreal.Vector2D(*v) for v in d['uv']],[],[],[],[],[],True,False)
    pm.set_material(0,unreal.load_asset(mat))
    a.modify();pm.modify()
    assert le.save_current_level()
    assert le.load_level('/Game/Maps/Entry')
    assert le.load_level(path)
    a=next(a for a in ea.get_all_level_actors() if a.get_actor_label()=='Original_CPP_ProceduralMesh_SameData')
    components=a.get_components_by_class(unreal.ProceduralMeshComponent)
    assert len(components)==1, str(components)
    pm=components[0]
    assert pm.get_num_sections()==1
    report.append({'level':path,'component':pm.get_path_name(),'sectionsAfterReload':pm.get_num_sections(),'materialAfterReload':pm.get_material(0).get_path_name()})
(O/'persistence-verified.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
