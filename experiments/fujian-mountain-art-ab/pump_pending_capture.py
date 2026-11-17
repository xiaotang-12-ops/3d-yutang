import unreal,time
AB_PUMP_START=time.monotonic()
def ab_pump_frame(dt):
 unreal.EditorLevelLibrary.editor_invalidate_viewports()
 if time.monotonic()-AB_PUMP_START>8:
  unreal.unregister_slate_post_tick_callback(AB_PUMP_HANDLE)
AB_PUMP_HANDLE=unreal.register_slate_post_tick_callback(ab_pump_frame)
