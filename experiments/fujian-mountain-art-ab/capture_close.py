"""使用已锁定近景机位输出原版原图。-codex，2026-10-23"""
import unreal
try:unreal.unregister_slate_post_tick_callback(AB_CAP_HANDLE)
except:pass
c=next(a for a in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors() if a.get_actor_label()=='AB_Camera_Close')
AB_CLOSE_TASK=unreal.AutomationLibrary.take_high_res_screenshot(1920,1080,'D:/3d-research/artifacts/fujian-mountain-art-ab/Original-Close.png',camera=c,delay=0.0)
unreal.EditorLevelLibrary.editor_invalidate_viewports()
