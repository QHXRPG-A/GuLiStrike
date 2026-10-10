"""Read runtime-relevant Niagara asset settings without modifying or saving assets."""
import json
from pathlib import Path
import unreal

def read_stress_settings():
    rows=[]
    for effect_id in [5,52,36,45]:
        asset=unreal.GuLiVfxRegistrySubsystem.get_definition_without_world(effect_id).resource_path
        item={'id':effect_id,'path':asset.get_path_name()}
        for prop in ['max_pool_size','pool_prime_size','warmup_time','warmup_tick_count',
                     'fixed_tick_delta_time','fixed_tick_delta','require_current_frame_data',
                     'max_time_without_render','effect_type']:
            try:item[prop]=str(asset.get_editor_property(prop))
            except Exception:item[prop]='unavailable'
        rows.append(item)
    report={'success':True,'assets':rows,'scope':'Read-only native Niagara asset API.'}
    path=Path(unreal.Paths.project_dir())/'outputs/performance/20261009-stress-frame-analysis/pool-settings-readback.json'
    path.write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
    unreal.MCPythonHelper.submit_result(json.dumps(report))

read_stress_settings()
