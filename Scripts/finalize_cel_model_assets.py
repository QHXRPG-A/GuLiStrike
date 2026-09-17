"""Finish the two cel imports: distance LODs and existing weapon attachment aliases."""
import json, traceback
from pathlib import Path
import unreal

ROOT=Path('D:/UE5.7/test1'); OUT=ROOT/'ArtSource/StylePass_20260917'
BASE='/Game/Commander/Units/Tactical/Cel/'
LIB=unreal.EditorAssetLibrary

def finish_models(units=None):
    assert not unreal.WidgetService.is_pie_running()
    editor=unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
    report={'success':False,'models':{}}
    for unit,limits in [('Sweeper',(900,220)),('WarMachine',(300,70))]:
        if units is not None and unit not in units:continue
        mesh=unreal.load_asset(BASE+unit+'/Meshes/SM_'+unit+'_Cel');assert mesh
        assert LIB.get_metadata_tag(mesh,'GuLi.ModelProduction.Owner')=='GuLi.CelPass.20260917'
        n=mesh.get_num_triangles(0)
        opts=unreal.StaticMeshReductionOptions(auto_compute_lod_screen_size=False,
            reduction_settings=[unreal.StaticMeshReductionSettings(percent_triangles=p,screen_size=s)
                for p,s in [(1.,1.),(.25,.32),(limits[0]/n,.09),(limits[1]/n,.025)]])
        assert editor.set_lods(mesh,opts)==4
        for lod in range(editor.get_lod_count(mesh)):
            if mesh.get_num_sections(lod)>1:editor.enable_section_cast_shadow(mesh,False,lod,1)
        data=json.loads((ROOT/'ArtSource/TacticalStyle_20260916/Production_Handbuilt'/(unit+'_delivery.json')).read_text(encoding='utf8'))
        sockets=data['sockets_m']
        aliases={'FX_AimTarget':(0.,0.,650. if unit=='Sweeper' else 1600.)}
        if unit=='Sweeper':aliases['FX_Muzzle_Basic_01']=tuple(v*100 for v in sockets['Muzzle_Gun'])
        else:
            for a,b in [('FX_Muzzle_Basic_01','Muzzle_Cannon_L'),('FX_Missile_01','Muzzle_Missile_L'),('FX_Missile_02','Muzzle_Missile_R')]:aliases[a]=tuple(v*100 for v in sockets[b])
        for name,loc in aliases.items():
            socket=mesh.find_socket(name)
            if not socket:socket=unreal.new_object(unreal.StaticMeshSocket,outer=mesh);socket.set_editor_property('socket_name',name);mesh.add_socket(socket)
            socket.set_editor_property('relative_location',unreal.Vector(*loc))
            socket.set_editor_property('relative_rotation',unreal.Rotator(pitch=45 if 'Missile' in name else 0))
            socket.set_editor_property('relative_scale',unreal.Vector(1,1,1))
        assert LIB.save_loaded_asset(mesh,False)
        report['models'][unit]={'asset':mesh.get_path_name(),'lod_triangles':[mesh.get_num_triangles(i) for i in range(editor.get_lod_count(mesh))],
            'screens':list(editor.get_lod_screen_sizes(mesh)),'sockets':{n:list(mesh.find_socket(n).get_editor_property('relative_location').to_tuple()) for n in aliases}}
    report['success']=True
    return report

if __name__=='__main__':
    try:report=finish_models()
    except Exception:report={'success':False,'error':traceback.format_exc()}
    (OUT/'model_finish.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
    unreal.MCPythonHelper.submit_result(json.dumps(report))
