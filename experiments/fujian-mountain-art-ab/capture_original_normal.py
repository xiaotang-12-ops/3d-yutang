"""固定原版光向与机位并用UE原生截图捕获，不输出截图耗时作为帧率。-codex，2026-10-23"""
import unreal
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
sun=next(a for a in actors if a.get_actor_label()=='AB_Common_Sun')
sun.set_actor_rotation(unreal.Rotator(pitch=-38,yaw=-42,roll=0),False)
camera=next(a for a in actors if a.get_actor_label()=='AB_Camera_Normal')
unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).pilot_level_actor(camera)
unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
AB_SCREEN_TASK=unreal.AutomationLibrary.take_high_res_screenshot(1920,1080,'D:/3d-research/artifacts/fujian-mountain-art-ab/Original-Normal.png',camera=camera,delay=3.0)
