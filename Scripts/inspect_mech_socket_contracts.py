import unreal,json,traceback
from pathlib import Path
out=Path('D:/UE5.7/test1/ArtSource/Mechs/StyleUnification_20260919/Source/Production_v1/socket_contracts.json')
data={}
try:
    for key,path,names in [('Mech_Legs_Lt','/Game/Assets/MechaController/Mech_Constructor_Lt_Med/Meshes_Skeletal/Mech_Legs_Lt',['Mount_Top']),('Cockpit_Jet','/Game/Assets/MechaController/Mech_Constructor_Lt_Med/Meshes/Cockpit_Jet',['Mount_Weapon_L','Mount_Weapon_R'])]:
        mesh=unreal.load_asset(path);data[key]={}
        for name in names:
            s=mesh.find_socket(name)
            if s:
                r={}
                for prop in ['bone_name','relative_location','relative_rotation','relative_scale']:
                    try:r[prop]=str(s.get_editor_property(prop))
                    except Exception:pass
                data[key][name]=r
            else:data[key][name]=None
except Exception:data['error']=traceback.format_exc()
out.write_text(json.dumps(data,indent=2),encoding='utf-8');unreal.SystemLibrary.quit_editor()
