"""Author the optical atlas and the three rank palettes using the existing player bullet system."""
import json
from pathlib import Path
import unreal

ROOT=Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
LIB=unreal.EditorAssetLibrary
EDIT=unreal.MaterialEditingLibrary
TOOLS=unreal.AssetToolsHelpers.get_asset_tools()
FX="/Game/GuLiStrike/FX/GroundMech/EnhancedBullet"
UI="/Game/GuLiStrike/GroundMech/UI/AIEngine"


def require(value, label):
    if not value:
        raise RuntimeError(label)
    return value


def build(resume=()):
    require(not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor(),"Active play session")
    require(not any(p.get_name().startswith((UI+"/",FX+"/")) and p.get_name() not in resume for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()),"Unsaved target asset")
    name="T_AIEngine_Optics"
    task=unreal.AssetImportTask()
    for key,value in dict(filename=str(ROOT/"ArtSource/UI/GroundMech/AIEngineHUD_Reference_20261121"/(name+".png")),
                          destination_path=UI,destination_name=name,replace_existing=True,automated=True,save=False).items():
        task.set_editor_property(key,value)
    TOOLS.import_asset_tasks([task])
    texture=require(unreal.load_asset(UI+"/"+name),"Optics texture")
    texture.set_editor_property("compression_settings",unreal.TextureCompressionSettings.TC_EDITOR_ICON)
    texture.set_editor_property("lod_group",unreal.TextureGroup.TEXTUREGROUP_UI)
    texture.set_editor_property("mip_gen_settings",unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
    require(LIB.save_loaded_asset(texture,False),"Save optics atlas")
    rows=json.loads((ROOT/"data/Json/DT_GuLiStrikeVfx_Effects.json").read_text(encoding="utf-8"))
    source=next(r["ResourcePath"].split(".")[0] for r in rows if r["Name"]=="PlayerBullet")
    parent=require(unreal.load_asset(source.rsplit("/",1)[0]+"/M_GroundBulletTrail_Orange"),"Player trail material")
    palettes=[]
    for row in rows:
        if row["Name"] not in ("GroundEnhancedGold","GroundEnhancedViolet","GroundEnhancedSilver"):
            continue
        style=row["Name"].removeprefix("GroundEnhanced")
        material_name="MI_Enhanced"+style
        path=FX+"/"+material_name
        material=unreal.load_asset(path) if LIB.does_asset_exist(path) else TOOLS.create_asset(material_name,FX,unreal.MaterialInstanceConstant,unreal.MaterialInstanceConstantFactoryNew())
        require(material,path)
        EDIT.set_material_instance_parent(material,parent)
        rgb=bytes.fromhex(row["TrailTintSRGB"])
        linear=unreal.LinearColor(*[((v/255+.055)/1.055)**2.4 if v/255>.04045 else v/255/12.92 for v in rgb],1)
        core=unreal.LinearColor(*(v*.5+.5 for v in (linear.r,linear.g,linear.b)),1)
        for param,color in [("HeadColor",linear),("TailColor",linear),("CoreColor",core)]:
            EDIT.set_material_instance_vector_parameter_value(material,param,color)
            actual=EDIT.get_material_instance_vector_parameter_value(material,param)
            require(all(abs(getattr(actual,k)-getattr(color,k))<.0001 for k in "rgba"),"Material parameter "+param)
        EDIT.update_material_instance(material)
        require(LIB.save_loaded_asset(material,False),"Save "+material_name)
        system_path=row["ResourcePath"].split(".")[0]
        system=unreal.load_asset(system_path) if LIB.does_asset_exist(system_path) else LIB.duplicate_asset(source,system_path)
        require(system,system_path)
        count=0
        for emitter in unreal.NiagaraService.summarize(system_path).get_editor_property("emitter_names"):
            for index,renderer in enumerate(unreal.NiagaraEmitterService.list_renderers(system_path,str(emitter))):
                require(unreal.NiagaraEmitterService.set_renderer_property(system_path,str(emitter),index,"Material",path),"Bind trail material")
                count+=1
        require(count,"Trail renderer")
        compiled=unreal.NiagaraService.compile_with_results(system_path)
        require(compiled.success and not compiled.error_count,"Trail Niagara compile "+str(compiled))
        require(unreal.NiagaraService.save_system(system_path),"Save trail system")
        palettes.append(dict(id=row["Id"],tint=row["TrailTintSRGB"],material=path,renderers=count))
    require(len(palettes)==3,"Missing palettes")
    return dict(atlas=texture.get_path_name(),palettes=palettes)


if __name__=="__main__":
    unreal.MCPythonHelper.submit_result(json.dumps(build(),ensure_ascii=False))
