import unreal,json,pathlib
asys=unreal.get_editor_subsystem(unreal.EditorActorSubsystem);le=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
a=next(a for a in asys.get_all_level_actors() if a.get_actor_label()=='Showcase_Foliage_island_tree_02_1k');c=a.get_components_by_class(unreal.HierarchicalInstancedStaticMeshComponent)[0]
before=c.get_instance_count();keep=[]
for i in range(before):
 t=c.get_instance_transform(i,True);v=t.translation
 if (v.x-291677)**2+(v.y+476512)**2<30000**2 and i%3!=0:keep.append(t)
assert keep
c.clear_instances();c.add_instances(keep,False,True,False);c.set_cull_distances(100000,130000);a.modify();c.modify()
assert le.save_current_level()
pathlib.Path('D:/3d-research/artifacts/fujian-mountain-art-ab/showcase-understory.json').write_text(json.dumps({'before':before,'after':c.get_instance_count(),'reason':'retain real-scale low trees near the walk-through; reserve middle canopy for 7.8m fir'},indent=2),encoding='utf-8')
