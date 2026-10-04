"""Save observation entries in the existing commander map, then read their entity data. Never runs PIE."""
import json
import math
import traceback
from pathlib import Path
import unreal

ROOT = Path('D:/UE5.7/test1')
MAP = '/Game/Maps/LVL_CommanderMassPrototype'
TAG = 'CommanderCameraReview20260930'
REPORT = ROOT / globals().get('GULI_CAMERA_REPORT_DIR', 'Artifacts/CommanderCamera/20260930') / 'scene-readback.json'


def main():
    report = {'map': MAP, 'success': False, 'saved': False, 'runtime_validation': 'not_run', 'actors': []}
    try:
        level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
        editor = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
        world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
        assert not level.is_in_play_in_editor(), 'Do not interrupt a running player session.'
        assert world.get_path_name().split('.')[0] == MAP, 'Open the designated existing map before authoring.'
        actors = editor.get_all_level_actors()
        by_label = {}
        for actor in actors:
            by_label.setdefault(actor.get_actor_label(), []).append(actor)
        assert len(by_label.get('RogueCards_F4_Entry', [])) == 1
        row = json.loads((ROOT / 'Data/Json/DT_GuLiStrikeCommander_Camera.json').read_text(encoding='utf8'))[0]
        definition = unreal.load_asset('/Game/GuLiStrike/Data/Resources/DA_CommanderResourceMap')
        minimum = definition.get_editor_property('playable_minimum')
        maximum = definition.get_editor_property('playable_maximum')
        report['battlefield_bounds_cm'] = [list(minimum.to_tuple()), list(maximum.to_tuple())]
        report['battlefield_size_m'] = [(maximum.x-minimum.x)/100, (maximum.y-minimum.y)/100]
        report['camera_row'] = row
        report['camera_struct_loaded'] = unreal.find_object(None, '/Script/GuLiStrike.GuLiStrikeCommanderCameraRow') is not None
        report['camera_table_exists'] = unreal.EditorAssetLibrary.does_asset_exist('/Game/GuLiStrike/Data/DT_GuLiStrikeCommander_Camera')
        ignored = [a for a in actors if not isinstance(a, unreal.LandscapeProxy)]

        def terrain(x, y):
            hit = unreal.SystemLibrary.line_trace_single(world, unreal.Vector(x,y,500000), unreal.Vector(x,y,-500000),
                unreal.TraceTypeQuery.TRACE_TYPE_QUERY1, True, ignored, unreal.DrawDebugTrace.NONE)
            assert hit is not None, f'Landscape collision missing at {x}, {y}'
            # HitResult's Python native-break tuple exposes ImpactPoint at index 5.
            return hit.to_tuple()[5].z

        def upsert(cls, label, location):
            found = by_label.get(label, [])
            assert len(found) <= 1, f'Ambiguous scene entry: {label}'
            actor = found[0] if found else editor.spawn_actor_from_class(cls, unreal.Vector(*location))
            assert isinstance(actor, cls), f'Unexpected actor class: {label}'
            actor.set_actor_label(label)
            actor.set_actor_location(unreal.Vector(*location), False, True)
            actor.set_folder_path('Review/CommanderCamera')
            actor.set_editor_property('tags', list(dict.fromkeys([*actor.tags, unreal.Name(TAG)])))
            return actor

        entry = by_label['RogueCards_F4_Entry'][0].get_actor_location()
        focus = (entry.x, entry.y - 3000, terrain(entry.x, entry.y-3000))
        upsert(unreal.TargetPoint, 'CommanderCamera_EntryFocus', focus)
        for name, height, pitch in [('Near35',row['MinimumHeightMeters'],row['NearPitchDegrees']),
                ('Tactical300',row['TacticalStartHeightMeters'],row['TacticalPitchDegrees']),
                ('Tactical700',row['TacticalMaximumHeightMeters'],row['TacticalPitchDegrees'])]:
            x, y = focus[0] - height*100/math.tan(math.radians(pitch)), focus[1]
            camera = upsert(unreal.CameraActor, 'CommanderCamera_Observe_'+name, (x,y,terrain(x,y)+height*100))
            camera.set_actor_rotation(unreal.Rotator(pitch=-pitch, yaw=0, roll=0), True)
            camera.camera_component.set_field_of_view(row['FieldOfViewDegrees'])
            camera.camera_component.set_editor_property('constrain_aspect_ratio', False)
        for label, x, y in [('SouthWest',minimum.x+1000,minimum.y+1000), ('NorthEast',maximum.x-1000,maximum.y-1000)]:
            upsert(unreal.TargetPoint, 'CommanderCamera_FarCorner_'+label, (x,y,terrain(x,y)))
        samples=[]
        for fx in (.2,.5,.8):
            for fy in (.2,.5,.8):
                x=minimum.x+(maximum.x-minimum.x)*fx; y=minimum.y+(maximum.y-minimum.y)*fy
                samples.append((x,y,terrain(x,y)))
        upsert(unreal.TargetPoint, 'CommanderCamera_TerrainFollow_High', max(samples,key=lambda p:p[2]))
        note = upsert(unreal.Note, 'CommanderCamera_Instructions', (entry.x+1500,entry.y,entry.z))
        ready = report['camera_struct_loaded'] and report['camera_table_exists']
        readiness = ('已加载原生Camera行并导入Camera表；玩家可进入指挥官验收。' if ready else
            '前置：获准构建并加载最新 GuLiStrikeEditor，再运行 Scripts/CommanderCamera/import_camera_data.py 导入 Camera 表。')
        note.set_editor_property('text',
            '指挥官三档镜头与总览 LOD（2026-09-30）\n' + readiness +
            '当前场景保存/实体回读只证明布置，不证明新代码或视觉效果已通过。\n'
            '参数唯一入口：Data/Excel/GuLiStrikeCommander.xlsx → Camera → Default；运行时 DT_GuLiStrikeCommander_Camera。\n'
            '真实入口：原型图 Play 后进入指挥官，现有 F4 为真实选牌入口；观察 CameraActor 只供编辑器定位，不替代 CameraPawn。\n'
            '1. gs.GM.Commander.Camera.Debug 1 显示档位/目标离地/实际离地/俯角/过渡。'
            'Height 命令用完整前缀 gs.GM.Commander.Camera.Height 35、300、700 分别观察边界；第一档 25→55度，第二档55度。\n'
            '2. 在山坡方向平移，观察镜头平滑跟地；Z/C绕焦点旋转。避障只抬高实际高度，档位按滚轮设定高度。\n'
            '边缘平移复验：Height 306和700，方向键/屏幕下边缘将南北两侧部队移到画面中间；'
            '观察焦点只受战场安全边界限制，相机和视野可越界，不应被底部HUD前方卡住。'
            '在四边与四角分别转向和平移，检查贴边滑动、缩放及总览返回；地形外相机沿用最近地形边缘高度。\n'
            '3. 滚轮目标超过700米的当帧立即开始0.6秒平滑过渡，不等实际镜头追上700米；'
            '进入后90度俯视，按进入朝向就近对齐0/90/180/270度，最多转45度回正；'
            '高度按可用画面算定并锁住；向内一格立即触发返回原焦点/朝向/700米。'
            '过渡中反向滚轮可返回；中途不提交世界选取/落点。总览平移和旋转锁定。\n'
            '切档复验：Height 650后向外一格应立即进入过渡；先Z/C旋转再总览，地图应沿最短方向小角度回正；'
            '接近90/180/270度时不应转回0度，已经正向时只升高和俯视；'
            '过渡中不应横滚或临近结束突然扭转。总览连续外滚保持同一高度，内滚返回700米边缘及原朝向。\n'
            '4. 总览所有单位/建筑模型及附属效果消失，地形/环境/矿藏保留；单位14px、建筑20px图标随DPI缩放，选中/受击显示血条。'
            '友军图标点选/框选、右键下令沿现有规则；建筑和敌方图标可本地观察，不能因此取得控制权。\n'
            '5. 4:3、16:9、21:9及调整窗口，检查战场留在HUD外可用矩形内，四边5%留白。远端角标对应两个 FarCorner 标记。'
            '当前资产实读为1.8×1.8km，计划的2.8km不作为硬编码。\n'
            '6. 总览时新生/死亡/建成/销毁持续更新；退出恢复模型，不补播旧特效。多本地视图分别隐藏。'
            '小地图、F5–F8书签及定位直接退出总览，在目标位置回到第二档。\n'
            '7. gs.Commander.LOD.Stats WM01MissileBatches 观察导弹目标/预算；指挥官分档来自Camera，旧near/mid距离覆盖仅用于非指挥视图。\n'
            '手工结果请按镜头、取景、选择下令、对象生命周期、多视图分别反馈。助手未运行PIE或自动化。')
        marker='\n[三档镜头 20260930]'
        for label in ('RogueCards_F4_Entry','CommanderLOD_Instructions'):
            if by_label.get(label):
                actor=by_label[label][0]
                old=str(actor.get_editor_property('text')).split(marker)[0]
                actor.set_editor_property('text',old+marker+'\n新版分档与总览操作见 CommanderCamera_Instructions；指挥官档位读取Camera，旧距离分档仅作历史说明。'+readiness)
        report['saved']=bool(level.save_current_level())
        assert report['saved'], 'Map save failed'
        related=[a for a in editor.get_all_level_actors() if a.actor_has_tag(TAG)]
        assert len(related)==8, f'Expected 8 camera review entities, got {len(related)}'
        for actor in sorted(related,key=lambda a:a.get_actor_label()):
            entity={'label':actor.get_actor_label(),'class':actor.get_class().get_name(), 'object':actor.get_path_name(),
                'location':list(actor.get_actor_location().to_tuple()),'rotation':list(actor.get_actor_rotation().to_tuple())}
            if isinstance(actor,unreal.CameraActor): entity['fov']=float(actor.camera_component.field_of_view)
            if isinstance(actor,unreal.Note): entity['text']=str(actor.get_editor_property('text'))
            report['actors'].append(entity)
        report['game_mode']=world.get_world_settings().get_editor_property('default_game_mode').get_path_name()
        report['success']=True
    except Exception:
        report['error']=traceback.format_exc()
    REPORT.parent.mkdir(parents=True,exist_ok=True)
    REPORT.write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
    unreal.MCPythonHelper.submit_result(json.dumps({'success':report['success'],'saved':report['saved'],
        'actors':len(report['actors']),'report':str(REPORT),'error':report.get('error')},ensure_ascii=False))


main()
