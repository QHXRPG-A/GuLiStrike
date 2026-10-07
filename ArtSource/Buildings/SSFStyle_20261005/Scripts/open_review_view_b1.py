"""Set a Blender review viewport without saving or changing asset geometry."""
import bpy


def setup_review():
    scene = bpy.data.scenes.get("Production_Assembly")
    if scene is None or not bpy.context.window_manager.windows:
        return 1.0
    for window in bpy.context.window_manager.windows:
        window.scene = scene
        for area in window.screen.areas:
            if area.type != "VIEW_3D":
                continue
            space = area.spaces.active
            space.shading.type = "RENDERED"
            space.shading.use_scene_world_render = True
            space.shading.use_scene_lights_render = True
            space.overlay.show_overlays = False
            space.show_region_ui = False
            space.region_3d.view_perspective = "CAMERA"
            space.region_3d.view_camera_zoom = 0.0
            space.region_3d.view_camera_offset = (0.0, 0.0)
            area.tag_redraw()
    return None


bpy.app.timers.register(setup_review, first_interval=1.0)
