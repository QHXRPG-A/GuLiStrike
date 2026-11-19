"""Optional live Blender panel. Saved native root properties work without this UI."""
import bpy

OWNER='GuLi.OutpostSymmetric.B_v1'


class GULIOUTPOST_OT_core_color(bpy.types.Operator):
    bl_idname='guli_outpost.core_color'
    bl_label='据点光柱颜色'
    bl_options={'REGISTER','UNDO'}
    mode:bpy.props.IntProperty(default=0,min=0,max=2)

    def execute(self,context):
        root=bpy.data.objects.get('OP_Root_Midplane')
        if not root or root.get('asset_owner')!=OWNER:return {'CANCELLED'}
        root['Core_Color_Mode']=self.mode
        root.update_tag()
        context.scene.frame_set(context.scene.frame_current)
        context.view_layer.update()
        for area in context.screen.areas:area.tag_redraw()
        return {'FINISHED'}


class GULIOUTPOST_PT_core_light(bpy.types.Panel):
    bl_label='据点 · 光柱'
    bl_idname='GULIOUTPOST_PT_core_light'
    bl_space_type='VIEW_3D'
    bl_region_type='UI'
    bl_category='GuLi'

    @classmethod
    def poll(cls,context):
        return context.scene.get('asset_owner')==OWNER

    def draw(self,context):
        layout=self.layout
        root=bpy.data.objects.get('OP_Root_Midplane')
        if not root:return
        row=layout.row(align=True)
        for label,mode in (('中立白',0),('己方蓝',1),('敌方红',2)):
            op=row.operator('guli_outpost.core_color',text=label,
                            depress=int(root['Core_Color_Mode'])==mode)
            op.mode=mode
        layout.prop(root,'["Core_Emission_Strength"]',text='光柱强度')
        layout.separator()
        layout.label(text='A_v2 还原 / 上下镜像')
        layout.label(text='仅中央光柱使用归属颜色')


def register():
    for cls in (GULIOUTPOST_OT_core_color,GULIOUTPOST_PT_core_light):
        previous=getattr(bpy.types,cls.__name__,None)
        if previous:bpy.utils.unregister_class(previous)
        bpy.utils.register_class(cls)


register()
