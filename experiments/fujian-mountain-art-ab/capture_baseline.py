"""在任何A/B美术修改前锁定完整样板/近景两机位，原生截图逐张完成后再推进。-codex，2026-10-23"""
import unreal,pathlib,json,time
OUT=pathlib.Path('D:/3d-research/artifacts/fujian-mountain-art-ab')
es=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
ls=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
cams={a.get_actor_label().removeprefix('AB_Camera_'):a for a in es.get_all_level_actors() if a.get_actor_label().startswith('AB_Camera_')}
positions={'Normal':((-2800000,-2300000,2100000),(0,0,250000)),'Close':((-1550000,-1200000,1350000),(0,0,250000))}
records={}
for name,(loc,target) in positions.items():
    c=cams[name];rot=unreal.MathLibrary.find_look_at_rotation(unreal.Vector(*loc),unreal.Vector(*target))
    c.set_actor_location_and_rotation(unreal.Vector(*loc),rot,False,False)
    records[name]={'location':loc,'target':target,'rotation':{'pitch':rot.pitch,'yaw':rot.yaw,'roll':rot.roll},'fov':55,'projection':'perspective','output':[1920,1080]}
ls.save_current_level()
(OUT/'fixed-cameras.json').write_text(json.dumps(records,indent=2),encoding='utf-8')
if (OUT/'Original-Normal.png').exists(): (OUT/'Original-Normal.png').rename(OUT/'Draft-Original-cropped.png')
AB_CAP_SEQUENCE=['Normal','Close'];AB_CAP_INDEX=0;AB_CAP_TASK=None;AB_CAP_START=0
def ab_capture_tick(delta):
    global AB_CAP_INDEX,AB_CAP_TASK,AB_CAP_START
    if AB_CAP_INDEX>=len(AB_CAP_SEQUENCE):
        unreal.unregister_slate_post_tick_callback(AB_CAP_HANDLE);return
    name=AB_CAP_SEQUENCE[AB_CAP_INDEX]
    if AB_CAP_TASK is None:
        ls.pilot_level_actor(cams[name]);AB_CAP_START=time.time()
        AB_CAP_TASK=unreal.AutomationLibrary.take_high_res_screenshot(1920,1080,str(OUT/('Original-'+name+'.png')),camera=cams[name],delay=1.0)
    unreal.EditorLevelLibrary.editor_invalidate_viewports()
    if AB_CAP_TASK.is_task_done():
        AB_CAP_INDEX+=1;AB_CAP_TASK=None
    elif time.time()-AB_CAP_START>60:
        unreal.unregister_slate_post_tick_callback(AB_CAP_HANDLE);unreal.log_error('AB_CAPTURE_TIMEOUT: '+name)
AB_CAP_HANDLE=unreal.register_slate_post_tick_callback(ab_capture_tick)
