"""Build GuLiStrike's medium- and long-distance explosion and shell smoke systems."""
import unreal


DEST = '/Game/GuLiStrike/FX/CombatExplosions/LOD'
GROUND = '/Game/GuLiStrike/FX/CombatExplosions/'
PACK = '/Game/_VFXResources/Niagara_System/'
SYSTEMS = {
    'GroundDestruction': (GROUND + 'NS_GroundDestruction_03',
                          ('NE_Impact', 'NE_GroundShockwave', 'NE_Cloud_01', 'NE_Cloud_02', 'NE_CloudLine01'),
                          ('NE_Impact', 'NE_GroundShockwave', 'NE_Cloud_01')),
    'WingmanDestruction': (GROUND + 'NS_WingmanDestruction_05',
                          ('NE_Impact', 'NE_AirShockwave', 'NS_Cloud_02', 'NE_GlowOut', 'NE_DustOut_01'),
                          ('NE_Impact', 'NE_AirShockwave', 'NS_Cloud_02')),
    'XuntianBellyExplosion1': ('/Game/Stylized_Explosion_Pack_Vol1/VFX/N_Stylized_Explosion_1A_Classic_Explosion',
                              ('Explosion_Flipbook', 'OmnidirectionalBurst'), ('Explosion_Flipbook',)),
    'XuntianBellyExplosion2': (PACK + 'NS_Explosion_04',
                              ('NE_Cloud_01', 'NE_Cloud_02', 'NE_GlowOut'), ('NE_Cloud_01', 'NE_Cloud_02')),
    'XuntianBellyExplosion3': (PACK + 'NS_Explosion_07',
                              ('NS_Cloud03', 'NE_Cloud_02'), None),
}


def owned_variant(source, path):
    assets = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
    if assets.does_asset_exist(path):
        obj = assets.load_asset(path)
        if assets.get_metadata_tag(obj, 'GuLi.LOD.Source') != source:
            raise RuntimeError(f'Unexpected existing asset at {path}')
        return obj
    obj = assets.duplicate_asset(source, path)
    if not obj:
        raise RuntimeError(f'Could not duplicate {source} to {path}')
    assets.set_metadata_tag(obj, 'GuLi.LOD.Source', source)
    return obj


def compile_and_save(path, obj):
    compiled = unreal.NiagaraService.compile_with_results(path)
    assets = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
    if not compiled.success or compiled.errors or not assets.save_loaded_asset(obj, False):
        raise RuntimeError(f'{path}: {compiled.errors}')


def build_smoke():
    source = '/Game/GuLiStrike/FX/Xuntian/Belly/NS_XuntianBlackSmoke'
    niagara = unreal.NiagaraService
    result = {}
    for suffix, rate in (('Reduced', 50), ('Minimal', 25)):
        path = f'{DEST}/NS_XuntianBlackSmoke_{suffix}'
        obj = owned_variant(source, path)
        if not niagara.set_rapid_iteration_param_by_stage(path, 'WorldSmoke', 'EmitterUpdate',
                'Constants.WorldSmoke.SpawnRate.SpawnRate', str(rate)):
            raise RuntimeError(f'Could not set smoke rate at {path}')
        compile_and_save(path, obj)
        result[path] = rate
    return result


def build():
    niagara = unreal.NiagaraService
    result = {}
    for key, (source, reduced, minimal) in SYSTEMS.items():
        full = {str(e.emitter_name) for e in niagara.list_emitters(source)}
        if not set(reduced) < full or (minimal is not None and not set(minimal) < set(reduced)):
            raise RuntimeError(f'{key} source emitter contract has changed: {sorted(full)}')
        for suffix, keep in (('Reduced', reduced), ('Minimal', minimal)):
            if keep is None:
                continue  # Both distance tiers reference the Reduced package in the catalog.
            path = f'{DEST}/NS_{key}_{suffix}'
            asset = owned_variant(source, path)
            for emitter in niagara.list_emitters(path):
                name = str(emitter.emitter_name)
                if name not in keep and not niagara.remove_emitter(path, name):
                    raise RuntimeError(f'Could not remove {name} from {path}')
            if {str(e.emitter_name) for e in niagara.list_emitters(path)} != set(keep):
                raise RuntimeError(f'Incomplete emitter set at {path}')
            compile_and_save(path, asset)
            result[path] = list(keep)
    return result


if __name__ == '__main__':
    build()
    build_smoke()
